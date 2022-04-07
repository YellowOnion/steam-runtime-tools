/*
 * Common code for portal-like services
 *
 * Copyright © 2018 Red Hat, Inc.
 * Copyright © 2020 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Based on xdg-desktop-portal, flatpak-portal and flatpak-spawn.
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#include "steam-runtime-tools/portal-listener-internal.h"

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <sysexits.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>

#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/launcher-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

typedef GCredentials AutoCredentials;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AutoCredentials, g_object_unref)

typedef GDBusAuthObserver AutoDBusAuthObserver;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AutoDBusAuthObserver, g_object_unref)

typedef GDBusServer AutoDBusServer;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AutoDBusServer, g_object_unref)

struct _SrtPortalListenerClass
{
  GObjectClass parent;

  gboolean (*new_peer_connection) (SrtPortalListener *self,
                                   GDBusConnection *connection,
                                   gpointer user_data);
};

G_DEFINE_TYPE (SrtPortalListener, _srt_portal_listener, G_TYPE_OBJECT)

enum
{
  NEW_PEER_CONNECTION,
  SESSION_BUS_CONNECTED,
  SESSION_BUS_NAME_ACQUIRED,
  SESSION_BUS_NAME_LOST,
  N_SIGNALS
};

static guint signal_ids[N_SIGNALS] = { 0 };

void
_srt_portal_listener_init (SrtPortalListener *self)
{
  self->original_environ = g_get_environ ();
  _srt_get_current_dirs (NULL, &self->original_cwd_l);
}

/*
 * Divert stdout to stderr, and set up the --info-fd to be the
 * original stdout or a specified fd (if strictly positive).
 */
gboolean
_srt_portal_listener_set_up_info_fd (SrtPortalListener *self,
                                   int fd,
                                   GError **error)
{
  /* writing output to fd 0 (stdin) makes no sense */
  g_return_val_if_fail (fd != STDIN_FILENO, FALSE);

  self->original_stdout = _srt_divert_stdout_to_stderr (error);

  if (self->original_stdout == NULL)
    return FALSE;

  if (fd == STDOUT_FILENO)
    {
      self->info_fh = self->original_stdout;
    }
  else if (fd > 0)
    {
      self->info_fh = fdopen (fd, "w");

      if (self->info_fh == NULL)
        return glnx_throw_errno_prefix (error,
                                        "Unable to create a stdio wrapper for fd %d",
                                        fd);
    }

  return TRUE;
}

gboolean
_srt_portal_listener_check_socket_arguments (SrtPortalListener *listener,
                                             const char *opt_bus_name,
                                             const char *opt_socket,
                                             const char *opt_socket_directory,
                                             GError **error)
{
  gsize i;

  if ((opt_socket != NULL) + (opt_socket_directory != NULL) + (opt_bus_name != NULL) != 1)
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
                   "Exactly one of --bus-name, --socket, --socket-directory"
                   "is required");
      return FALSE;
    }

  /* --socket argument needs to be printable so we can print
   * "socket=%s\n" without escaping */
  if (opt_socket != NULL)
    {
      for (i = 0; opt_socket[i] != '\0'; i++)
        {
          if (!g_ascii_isprint (opt_socket[i]))
            return glnx_throw (error,
                               "Non-printable characters not allowed in --socket");
        }
    }

  /* --socket-directory argument likewise */
  if (opt_socket_directory != NULL)
    {
      for (i = 0; opt_socket_directory[i] != '\0'; i++)
        {
          if (!g_ascii_isprint (opt_socket_directory[i]))
            return glnx_throw (error,
                               "Non-printable characters not allowed in "
                               "--socket-directory");
        }
    }

  return TRUE;
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar *name,
                 gpointer user_data)
{
  SrtPortalListener *self = user_data;

  g_signal_emit (self, signal_ids[SESSION_BUS_CONNECTED], 0, connection);
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar *name,
                  gpointer user_data)
{
  SrtPortalListener *self = user_data;

  g_signal_emit (self, signal_ids[SESSION_BUS_NAME_ACQUIRED], 0,
                 connection, name);
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar *name,
              gpointer user_data)
{
  SrtPortalListener *self = user_data;

  g_signal_emit (self, signal_ids[SESSION_BUS_NAME_LOST], 0,
                 connection, name);
}

#if GLIB_CHECK_VERSION(2, 34, 0)
/*
 * Callback for GDBusServer::allow-mechanism.
 * Only allow the (most secure) EXTERNAL authentication mechanism,
 * if possible.
 */
static gboolean
allow_external_cb (G_GNUC_UNUSED GDBusAuthObserver *observer,
                   const char *mechanism,
                   G_GNUC_UNUSED gpointer user_data)
{
  return (g_strcmp0 (mechanism, "EXTERNAL") == 0);
}
#endif

/*
 * Callback for GDBusServer::authorize-authenticated-peer.
 * Only allow D-Bus connections from a matching uid.
 */
static gboolean
authorize_authenticated_peer_cb (G_GNUC_UNUSED GDBusAuthObserver *observer,
                                 G_GNUC_UNUSED GIOStream *stream,
                                 GCredentials *credentials,
                                 G_GNUC_UNUSED gpointer user_data)
{
  g_autoptr(AutoCredentials) retry_credentials = NULL;
  g_autoptr(AutoCredentials) myself = NULL;
  g_autofree gchar *credentials_str = NULL;

  if (g_credentials_get_unix_user (credentials, NULL) == (uid_t) -1)
    {
      /* Work around https://gitlab.gnome.org/GNOME/glib/issues/1831:
       * older GLib versions might retrieve incomplete credentials.
       * Make them try again. */
      credentials = NULL;
    }

  if (credentials == NULL && G_IS_SOCKET_CONNECTION (stream))
    {
      /* Continue to work around
       * https://gitlab.gnome.org/GNOME/glib/issues/1831:
       * older GLib versions might retrieve incomplete or no credentials. */
      GSocket *sock;

      sock = g_socket_connection_get_socket (G_SOCKET_CONNECTION (stream));
      retry_credentials = g_socket_get_credentials (sock, NULL);
      credentials = retry_credentials;
    }

  myself = g_credentials_new ();

  if (credentials != NULL &&
      g_credentials_is_same_user (credentials, myself, NULL))
    return TRUE;

  if (credentials == NULL)
    credentials_str = g_strdup ("peer with unknown credentials");
  else
    credentials_str = g_credentials_to_string (credentials);

  g_warning ("Rejecting connection from %s", credentials_str);
  return FALSE;
}

static GDBusAuthObserver *
observer_new (void)
{
  g_autoptr(AutoDBusAuthObserver) observer = g_dbus_auth_observer_new ();

#if GLIB_CHECK_VERSION(2, 34, 0)
  g_signal_connect (observer, "allow-mechanism",
                    G_CALLBACK (allow_external_cb), NULL);
#endif
  g_signal_connect (observer, "authorize-authenticated-peer",
                    G_CALLBACK (authorize_authenticated_peer_cb), NULL);

  return g_steal_pointer (&observer);
}

/*
 * Double-check credentials of a peer, since we are working with older
 * versions of GLib that can't necessarily be completely relied on.
 * We are willing to execute arbitrary code on behalf of an authenticated
 * connection, so it seems worthwhile to be extra-careful.
 */
static gboolean
check_credentials (GDBusConnection *connection,
                   GError **error)
{
  struct ucred creds;
  socklen_t len;
  GIOStream *stream;
  GSocket *sock;
  int sockfd;

  stream = g_dbus_connection_get_stream (connection);
  len = sizeof (creds);

  if (!G_IS_SOCKET_CONNECTION (stream))
    return glnx_throw (error, "Incoming D-Bus connection is not a socket?");

  sock = g_socket_connection_get_socket (G_SOCKET_CONNECTION (stream));

  if (sock == NULL)
    return glnx_throw (error,
                       "Incoming D-Bus connection does not have a socket?");

  sockfd = g_socket_get_fd (sock);

  if (getsockopt (sockfd, SOL_SOCKET, SO_PEERCRED, &creds, &len) < 0)
    return glnx_throw_errno_prefix (error, "Unable to check credentials");

  if (creds.uid != geteuid ())
    return glnx_throw (error,
                       "Connection from uid %ld != %ld should have been "
                       "rejected already",
                       (long) creds.uid, (long) geteuid ());

  return TRUE;
}

/*
 * Callback for GDBusServer::new-connection.
 *
 * Returns: %TRUE if the new connection attempt was handled, even if
 *  unsuccessfully.
 */
static gboolean
new_connection_cb (GDBusServer *server,
                   GDBusConnection *connection,
                   gpointer user_data)
{
  SrtPortalListener *self = user_data;
  g_autoptr(GError) error = NULL;
  gboolean handled;

  g_return_val_if_fail (self->server == server, FALSE);

  if (!check_credentials (connection, &error))
    {
      g_warning ("Credentials verification failed: %s", error->message);
      g_dbus_connection_close (connection, NULL, NULL, NULL);
      return TRUE;  /* handled, unsuccessfully */
    }

  g_signal_emit (self, signal_ids[NEW_PEER_CONNECTION], 0, connection,
                 &handled);
  g_return_val_if_fail (handled, FALSE);
  return TRUE;
}

static GDBusServer *
listen_on_address (SrtPortalListener *self,
                   const char *address,
                   GError **error)
{
  g_autofree gchar *guid = NULL;
  g_autoptr(AutoDBusAuthObserver) observer = NULL;
  g_autoptr(AutoDBusServer) server = NULL;

  guid = g_dbus_generate_guid ();

  observer = observer_new ();

  server = g_dbus_server_new_sync (address,
                                   G_DBUS_SERVER_FLAGS_NONE,
                                   guid,
                                   observer,
                                   NULL,
                                   error);

  if (server == NULL)
    return NULL;

  g_signal_connect (server, "new-connection",
                    G_CALLBACK (new_connection_cb), self);
  g_dbus_server_start (server);

  return g_steal_pointer (&server);
}

static GDBusServer *
listen_on_socket (SrtPortalListener *self,
                  GError **error)
{
  g_autofree gchar *address = NULL;
  g_autofree gchar *escaped = NULL;

  if (self->server_socket[0] == '@')
    {
      escaped = g_dbus_address_escape_value (&self->server_socket[1]);
      address = g_strdup_printf ("unix:abstract=%s", escaped);
    }
  else if (self->server_socket[0] == '/')
    {
      escaped = g_dbus_address_escape_value (self->server_socket);
      address = g_strdup_printf ("unix:path=%s", escaped);
    }
  else
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid socket address '%s'", self->server_socket);
      return FALSE;
    }

  return listen_on_address (self, address, error);
}

gboolean
_srt_portal_listener_listen (SrtPortalListener *self,
                             const char *opt_bus_name,
                             GBusNameOwnerFlags flags,
                             const char *opt_socket,
                             const char *opt_socket_directory,
                             GError **error)
{
  g_return_val_if_fail ((opt_bus_name != NULL)
                        + (opt_socket != NULL)
                        + (opt_socket_directory != NULL) == 1, FALSE);

  if (opt_bus_name != NULL)
    {
      g_debug ("Connecting to D-Bus session bus...");

      self->session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);

      if (self->session_bus == NULL)
        return glnx_prefix_error (error, "Can't find session bus");

      g_debug ("Claiming bus name %s...", opt_bus_name);
      self->name_owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                            opt_bus_name,
                                            flags,
                                            on_bus_acquired,
                                            on_name_acquired,
                                            on_name_lost,
                                            self,
                                            NULL);
    }
  else if (opt_socket != NULL)
    {
      g_debug ("Listening on socket %s...", opt_socket);
      self->server_socket = g_strdup (opt_socket);
      self->server = listen_on_socket (self, error);

      if (self->server == NULL)
        return glnx_prefix_error (error,
                                  "Unable to listen on socket \"%s\"",
                                  opt_socket);
    }
  else if (opt_socket_directory != NULL)
    {
      g_autofree gchar *unique = NULL;
      g_autofree gchar *dir = NULL;

      if (strlen (opt_socket_directory) > PV_MAX_SOCKET_DIRECTORY_LEN)
        return glnx_throw (error, "Socket directory path \"%s\" too long",
                           opt_socket_directory);

      dir = realpath (opt_socket_directory, NULL);

      if (strlen (dir) > PV_MAX_SOCKET_DIRECTORY_LEN)
        return glnx_throw (error,
                           "Socket directory path \"%s\" too long",
                           dir);

      g_debug ("Listening on a socket in %s...", opt_socket_directory);

      /* @unique is long and random, so we assume it is not guessable
       * by an attacker seeking to deny service by using the name we
       * intended to use; so we don't need a retry loop for alternative
       * names in the same directory. */
      unique = _srt_get_random_uuid (error);

      if (unique == NULL)
        return FALSE;

      self->server_socket = g_build_filename (dir, unique, NULL);
      g_debug ("Chosen socket is %s", self->server_socket);
      self->server = listen_on_socket (self, error);

      if (self->server == NULL)
        return glnx_prefix_error (error,
                                  "Unable to listen on socket \"%s\"",
                                  self->server_socket);
    }
  else
    {
      g_return_val_if_reached (FALSE);
    }

  if (self->info_fh == NULL)
    return TRUE;

  if (self->server_socket != NULL)
    fprintf (self->info_fh, "socket=%s\n", self->server_socket);

  if (self->server != NULL)
    fprintf (self->info_fh, "dbus_address=%s\n",
             g_dbus_server_get_client_address (self->server));

  return TRUE;
}


/*
 * If @bus_name is non-NULL, print it to the info fd. Then
 * close the --info-fd, and also close standard output (if different).
 */
void
_srt_portal_listener_close_info_fh (SrtPortalListener *self,
                                    const char *bus_name)
{
  if (self->info_fh != NULL)
    {
      if (bus_name != NULL)
        fprintf (self->info_fh, "bus_name=%s\n", bus_name);

      fflush (self->info_fh);
    }

  if (self->info_fh == self->original_stdout)
    self->original_stdout = NULL;
  else
    g_clear_pointer (&self->original_stdout, fclose);

  g_clear_pointer (&self->info_fh, fclose);
}

void
_srt_portal_listener_stop_listening (SrtPortalListener *self)
{
  if (self->name_owner_id)
    {
      g_debug ("Releasing bus name");
      g_bus_unown_name (self->name_owner_id);
    }

  self->name_owner_id = 0;

  if (self->server != NULL && self->server_socket != NULL)
    unlink (self->server_socket);

  g_clear_object (&self->server);
  g_clear_object (&self->session_bus);
}

static void
_srt_portal_listener_dispose (GObject *object)
{
  SrtPortalListener *self = _SRT_PORTAL_LISTENER (object);

  _srt_portal_listener_close_info_fh (self, NULL);
  _srt_portal_listener_stop_listening (self);

  G_OBJECT_CLASS (_srt_portal_listener_parent_class)->dispose (object);
}

static void
_srt_portal_listener_finalize (GObject *object)
{
  SrtPortalListener *self = _SRT_PORTAL_LISTENER (object);

  g_clear_pointer (&self->server_socket, g_free);
  g_clear_pointer (&self->original_environ, g_strfreev);
  g_clear_pointer (&self->original_cwd_l, g_free);

  G_OBJECT_CLASS (_srt_portal_listener_parent_class)->finalize (object);
}

static void
_srt_portal_listener_class_init (SrtPortalListenerClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->dispose = _srt_portal_listener_dispose;
  object_class->finalize = _srt_portal_listener_finalize;

  /*
   * Basically the same as GDBusServer::new-connection
   */
  signal_ids[NEW_PEER_CONNECTION] =
    g_signal_new ("new-peer-connection",
                  _SRT_TYPE_PORTAL_LISTENER,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (SrtPortalListenerClass, new_peer_connection),
                  g_signal_accumulator_true_handled, NULL,
                  NULL,
                  G_TYPE_BOOLEAN,
                  1,
                  G_TYPE_DBUS_CONNECTION);

  signal_ids[SESSION_BUS_CONNECTED] =
    g_signal_new ("session-bus-connected",
                  _SRT_TYPE_PORTAL_LISTENER,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  NULL,
                  G_TYPE_NONE,
                  1,
                  G_TYPE_DBUS_CONNECTION);

  signal_ids[SESSION_BUS_NAME_ACQUIRED] =
    g_signal_new ("session-bus-name-acquired",
                  _SRT_TYPE_PORTAL_LISTENER,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  NULL,
                  G_TYPE_NONE,
                  2,
                  G_TYPE_DBUS_CONNECTION,
                  G_TYPE_STRING);

  signal_ids[SESSION_BUS_NAME_LOST] =
    g_signal_new ("session-bus-name-lost",
                  _SRT_TYPE_PORTAL_LISTENER,
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  NULL,
                  G_TYPE_NONE,
                  2,
                  G_TYPE_DBUS_CONNECTION,
                  G_TYPE_STRING);
}

SrtPortalListener *
_srt_portal_listener_new (void)
{
  return g_object_new (_SRT_TYPE_PORTAL_LISTENER,
                       NULL);
}
