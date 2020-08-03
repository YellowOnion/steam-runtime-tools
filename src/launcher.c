/*
 * pressure-vessel-launcher — accept IPC requests to create child processes
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

#include "config.h"
#include "subprojects/libglnx/config.h"

#include <errno.h>
#include <locale.h>
#include <sysexits.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "glib-backports.h"
#include "libglnx/libglnx.h"

#include "flatpak-utils-base-private.h"
#include "launcher.h"
#include "utils.h"

typedef GCredentials AutoCredentials;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AutoCredentials, g_object_unref)

typedef GDBusAuthObserver AutoDBusAuthObserver;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AutoDBusAuthObserver, g_object_unref)

typedef GDBusServer AutoDBusServer;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AutoDBusServer, g_object_unref)

static FILE *original_stdout = NULL;
static GDBusConnection *session_bus = NULL;
static GHashTable *client_pid_data_hash = NULL;
static guint name_owner_id = 0;
static GMainLoop *main_loop;
static PvLauncher1 *launcher;

static void
skeleton_died_cb (gpointer data)
{
  g_debug ("skeleton finalized, exiting");
  g_main_loop_quit (main_loop);
}

static gboolean
unref_skeleton_in_timeout_cb (gpointer user_data)
{
  g_clear_object (&launcher);
  return G_SOURCE_REMOVE;
}

static void
unref_skeleton_in_timeout (void)
{
  if (name_owner_id)
    g_bus_unown_name (name_owner_id);

  name_owner_id = 0;

  /* After we've lost the name we drop the main ref on the helper
     so that we'll exit when it drops to zero. However, if there are
     outstanding calls these will keep the refcount up during the
     execution of them. We do the unref on a timeout to make sure
     we're completely draining the queue of (stale) requests. */
  g_timeout_add (500, unref_skeleton_in_timeout_cb, NULL);
}

typedef struct
{
  GDBusConnection *connection;
  GPid pid;
  gchar *client;
  guint child_watch;
} PidData;

static void
pid_data_free (PidData *data)
{
  g_clear_object (&data->connection);
  g_free (data->client);
  g_free (data);
}

static void
terminate_children (int signum)
{
  GHashTableIter iter;
  PidData *pid_data = NULL;
  gpointer value = NULL;

  /* pass the signal on to each process group led by one of our
   * child processes */
  g_hash_table_iter_init (&iter, client_pid_data_hash);

  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      pid_data = value;
      killpg (pid_data->pid, signum);
    }
}

static void
child_watch_died (GPid     pid,
                  gint     status,
                  gpointer user_data)
{
  PidData *pid_data = user_data;
  g_autoptr(GVariant) signal_variant = NULL;

  g_debug ("Client Pid %d died", pid_data->pid);

  signal_variant = g_variant_ref_sink (g_variant_new ("(uu)", pid, status));
  g_dbus_connection_emit_signal (pid_data->connection,
                                 pid_data->client,
                                 LAUNCHER_PATH,
                                 LAUNCHER_IFACE,
                                 "ProcessExited",
                                 signal_variant,
                                 NULL);

  /* This frees the pid_data, so be careful */
  g_hash_table_remove (client_pid_data_hash, GUINT_TO_POINTER (pid_data->pid));
}

typedef struct
{
  int from;
  int to;
  int final;
} FdMapEntry;

typedef struct
{
  FdMapEntry *fd_map;
  int         fd_map_len;
} ChildSetupData;

static void
drop_cloexec (int fd)
{
  fcntl (fd, F_SETFD, 0);
}

static void
child_setup_func (gpointer user_data)
{
  ChildSetupData *data = (ChildSetupData *) user_data;
  FdMapEntry *fd_map = data->fd_map;
  sigset_t set;
  int i;

  flatpak_close_fds_workaround (3);

  /* Unblock all signals */
  sigemptyset (&set);
  if (pthread_sigmask (SIG_SETMASK, &set, NULL) == -1)
    pv_async_signal_safe_error ("Failed to unblock signals when starting child\n",
                                LAUNCH_EX_FAILED);

  /* Reset the handlers for all signals to their defaults. */
  for (i = 1; i < NSIG; i++)
    {
      if (i != SIGSTOP && i != SIGKILL)
        signal (i, SIG_DFL);
    }

  for (i = 0; i < data->fd_map_len; i++)
    {
      if (fd_map[i].from != fd_map[i].to)
        {
          dup2 (fd_map[i].from, fd_map[i].to);
          close (fd_map[i].from);
        }
    }

  /* Second pass in case we needed an in-between fd value to avoid conflicts */
  for (i = 0; i < data->fd_map_len; i++)
    {
      if (fd_map[i].to != fd_map[i].final)
        {
          dup2 (fd_map[i].to, fd_map[i].final);
          close (fd_map[i].to);
        }

      /* Ensure we inherit the final fd value */
      drop_cloexec (fd_map[i].final);
    }

  /* We become our own session and process group, because it never makes sense
     to share the flatpak-session-helper dbus activated process group */
  setsid ();
  setpgid (0, 0);
}

static gboolean
handle_launch (PvLauncher1           *object,
               GDBusMethodInvocation *invocation,
               GUnixFDList           *fd_list,
               const gchar           *arg_cwd_path,
               const gchar *const    *arg_argv,
               GVariant              *arg_fds,
               GVariant              *arg_envs,
               guint                  arg_flags,
               GVariant              *arg_options)
{
  g_autoptr(GError) error = NULL;
  ChildSetupData child_setup_data = { NULL };
  GPid pid;
  PidData *pid_data;
  gsize i, j, n_fds, n_envs;
  const gint *fds = NULL;
  gint fds_len = 0;
  g_autofree FdMapEntry *fd_map = NULL;
  g_auto(GStrv) env = NULL;
  gint32 max_fd;

  if (fd_list != NULL)
    fds = g_unix_fd_list_peek_fds (fd_list, &fds_len);

  if (*arg_cwd_path == 0)
    arg_cwd_path = NULL;

  if (arg_argv == NULL || *arg_argv == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "No command given");
      return TRUE;
    }

  if ((arg_flags & ~PV_LAUNCH_FLAGS_MASK) != 0)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                             "Unsupported flags enabled: 0x%x", arg_flags & ~PV_LAUNCH_FLAGS_MASK);
      return TRUE;
    }

  g_debug ("Running spawn command %s", arg_argv[0]);

  n_fds = 0;
  if (fds != NULL)
    n_fds = g_variant_n_children (arg_fds);
  fd_map = g_new0 (FdMapEntry, n_fds);

  child_setup_data.fd_map = fd_map;
  child_setup_data.fd_map_len = n_fds;

  max_fd = -1;
  for (i = 0; i < n_fds; i++)
    {
      gint32 handle, dest_fd;
      int handle_fd;

      g_variant_get_child (arg_fds, i, "{uh}", &dest_fd, &handle);
      if (handle >= fds_len)
        continue;
      handle_fd = fds[handle];

      fd_map[i].to = dest_fd;
      fd_map[i].from = handle_fd;
      fd_map[i].final = fd_map[i].to;

      max_fd = MAX (max_fd, fd_map[i].to);
      max_fd = MAX (max_fd, fd_map[i].from);
    }

  /* We make a second pass over the fds to find if any "to" fd index
     overlaps an already in use fd (i.e. one in the "from" category
     that are allocated randomly). If a fd overlaps "to" fd then its
     a caller issue and not our fault, so we ignore that. */
  for (i = 0; i < n_fds; i++)
    {
      int to_fd = fd_map[i].to;
      gboolean conflict = FALSE;

      /* At this point we're fine with using "from" values for this
         value (because we handle to==from in the code), or values
         that are before "i" in the fd_map (because those will be
         closed at this point when dup:ing). However, we can't
         reuse a fd that is in "from" for j > i. */
      for (j = i + 1; j < n_fds; j++)
        {
          int from_fd = fd_map[j].from;
          if (from_fd == to_fd)
            {
              conflict = TRUE;
              break;
            }
        }

      if (conflict)
        fd_map[i].to = ++max_fd;
    }

  if (arg_flags & PV_LAUNCH_FLAGS_CLEAR_ENV)
    {
      char *empty[] = { NULL };
      env = g_strdupv (empty);
    }
  else
    {
      env = g_get_environ ();
    }

  n_envs = g_variant_n_children (arg_envs);
  for (i = 0; i < n_envs; i++)
    {
      const char *var = NULL;
      const char *val = NULL;
      g_variant_get_child (arg_envs, i, "{&s&s}", &var, &val);

      env = g_environ_setenv (env, var, val, TRUE);
    }

  /* We use LEAVE_DESCRIPTORS_OPEN to work around dead-lock, see flatpak_close_fds_workaround */
  if (!g_spawn_async_with_pipes (arg_cwd_path,
                                 (gchar **) arg_argv,
                                 env,
                                 G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                                 child_setup_func, &child_setup_data,
                                 &pid,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &error))
    {
      gint code = G_DBUS_ERROR_FAILED;

      if (g_error_matches (error, G_SPAWN_ERROR, G_SPAWN_ERROR_ACCES))
        code = G_DBUS_ERROR_ACCESS_DENIED;
      else if (g_error_matches (error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT))
        code = G_DBUS_ERROR_FILE_NOT_FOUND;

      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, code,
                                             "Failed to start command: %s",
                                             error->message);
      return TRUE;
    }

  pid_data = g_new0 (PidData, 1);
  pid_data->connection = g_object_ref (g_dbus_method_invocation_get_connection (invocation));
  pid_data->pid = pid;
  pid_data->client = g_strdup (g_dbus_method_invocation_get_sender (invocation));
  pid_data->child_watch = g_child_watch_add_full (G_PRIORITY_DEFAULT,
                                                  pid,
                                                  child_watch_died,
                                                  pid_data,
                                                  NULL);

  g_debug ("Client Pid is %d", pid_data->pid);

  g_hash_table_replace (client_pid_data_hash, GUINT_TO_POINTER (pid_data->pid),
                        pid_data);

  pv_launcher1_complete_launch (object, invocation, NULL, pid);
  return TRUE;
}

static gboolean
handle_send_signal (PvLauncher1           *object,
                    GDBusMethodInvocation *invocation,
                    guint                  arg_pid,
                    guint                  arg_signal,
                    gboolean               arg_to_process_group)
{
  PidData *pid_data = NULL;

  g_debug ("SendSignal(%d, %d)", arg_pid, arg_signal);

  pid_data = g_hash_table_lookup (client_pid_data_hash, GUINT_TO_POINTER (arg_pid));
  if (pid_data == NULL ||
      pid_data->connection != g_dbus_method_invocation_get_connection (invocation) ||
      g_strcmp0 (pid_data->client, g_dbus_method_invocation_get_sender (invocation)) != 0)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_UNIX_PROCESS_ID_UNKNOWN,
                                             "No such pid");
      return TRUE;
    }

  g_debug ("Sending signal %d to client pid %d", arg_signal, arg_pid);

  if (arg_to_process_group)
    killpg (pid_data->pid, arg_signal);
  else
    kill (pid_data->pid, arg_signal);

  pv_launcher1_complete_send_signal (launcher, invocation);

  return TRUE;
}

static gboolean
handle_terminate (PvLauncher1           *object,
                  GDBusMethodInvocation *invocation)
{
  terminate_children (SIGTERM);
  pv_launcher1_complete_terminate (object, invocation);
  unref_skeleton_in_timeout ();
  return TRUE;    /* handled */
}

static void
name_owner_changed (GDBusConnection *connection,
                    const gchar     *sender_name,
                    const gchar     *object_path,
                    const gchar     *interface_name,
                    const gchar     *signal_name,
                    GVariant        *parameters,
                    gpointer         user_data)
{
  const char *name, *from, *to;

  g_variant_get (parameters, "(&s&s&s)", &name, &from, &to);

  if (name[0] == ':' &&
      strcmp (name, from) == 0 &&
      strcmp (to, "") == 0)
    {
      GHashTableIter iter;
      PidData *pid_data = NULL;
      gpointer value = NULL;
      GList *list = NULL, *l;

      g_hash_table_iter_init (&iter, client_pid_data_hash);
      while (g_hash_table_iter_next (&iter, NULL, &value))
        {
          pid_data = value;

          if (g_str_equal (pid_data->client, name))
            list = g_list_prepend (list, pid_data);
        }

      for (l = list; l; l = l->next)
        {
          pid_data = l->data;
          g_debug ("%s dropped off the bus, killing %d", pid_data->client, pid_data->pid);
          killpg (pid_data->pid, SIGINT);
        }

      g_list_free (list);
    }
}

static gboolean
export_launcher (GDBusConnection *connection,
                 GError **error)
{
  if (launcher == NULL)
    {
      launcher = pv_launcher1_skeleton_new ();

      g_object_set_data_full (G_OBJECT (launcher), "track-alive",
                              launcher,   /* an arbitrary non-NULL pointer */
                              skeleton_died_cb);

      pv_launcher1_set_version (PV_LAUNCHER1 (launcher), 0);
      pv_launcher1_set_supported_launch_flags (PV_LAUNCHER1 (launcher),
                                               PV_LAUNCH_FLAGS_MASK);

      g_signal_connect (launcher, "handle-launch",
                        G_CALLBACK (handle_launch), NULL);
      g_signal_connect (launcher, "handle-send-signal",
                        G_CALLBACK (handle_send_signal), NULL);
      g_signal_connect (launcher, "handle-terminate",
                        G_CALLBACK (handle_terminate), NULL);
    }

  if (!g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (launcher),
                                         connection,
                                         LAUNCHER_PATH,
                                         error))
    return FALSE;

  return TRUE;
}

static void
on_bus_acquired (GDBusConnection *connection,
                 const gchar     *name,
                 gpointer         user_data)
{
  g_autoptr(GError) error = NULL;
  int *ret = user_data;

  g_debug ("Bus acquired, creating skeleton");

  g_dbus_connection_set_exit_on_close (connection, FALSE);
  g_dbus_connection_signal_subscribe (connection,
                                      DBUS_NAME_DBUS,
                                      DBUS_INTERFACE_DBUS,
                                      "NameOwnerChanged",
                                      DBUS_PATH_DBUS,
                                      NULL,
                                      G_DBUS_SIGNAL_FLAGS_NONE,
                                      name_owner_changed,
                                      NULL, NULL);

  if (!export_launcher (connection, &error))
    {
      g_warning ("Unable to export object: %s", error->message);
      *ret = EX_SOFTWARE;
      g_main_loop_quit (main_loop);
    }
}

static void
on_name_acquired (GDBusConnection *connection,
                  const gchar     *name,
                  gpointer         user_data)
{
  int *ret = user_data;

  g_debug ("Name acquired");

  /* If exporting the launcher didn't fail, then we are now happy */
  if (*ret == EX_UNAVAILABLE)
    {
      *ret = 0;
      g_assert (original_stdout != NULL);
      fprintf (original_stdout, "bus_name=%s\n", name);
      fflush (original_stdout);
      g_clear_pointer (&original_stdout, fclose);
    }
}

static void
on_name_lost (GDBusConnection *connection,
              const gchar     *name,
              gpointer         user_data)
{
  g_debug ("Name lost");
  unref_skeleton_in_timeout ();
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
 * Callback for GDBusConnection::closed.
 */
static void
peer_connection_closed_cb (GDBusConnection *connection,
                           gboolean remote_peer_vanished,
                           GError *error,
                           gpointer user_data)
{
  /* Paired with g_object_ref() in new_connection_cb() */
  g_object_unref (connection);
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
  g_autoptr(GError) error = NULL;

  if (!check_credentials (connection, &error))
    {
      g_warning ("Credentials verification failed: %s", error->message);
      g_dbus_connection_close (connection, NULL, NULL, NULL);
      return TRUE;  /* handled, unsuccessfully */
    }

  /* Paired with g_object_unref() in peer_connection_closed_cb() */
  g_object_ref (connection);
  g_signal_connect (connection, "closed",
                    G_CALLBACK (peer_connection_closed_cb), NULL);

  if (!export_launcher (connection, &error))
    {
      g_warning ("Unable to export object: %s", error->message);
      g_dbus_connection_close (connection, NULL, NULL, NULL);
      return TRUE;  /* handled, unsuccessfully */
    }

  return TRUE;
}

static GDBusServer *
listen_on_address (const char *address,
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
                    G_CALLBACK (new_connection_cb), NULL);
  g_dbus_server_start (server);

  return g_steal_pointer (&server);
}

static GDBusServer *
listen_on_socket (const char *name,
                  GError **error)
{
  g_autofree gchar *address = NULL;
  g_autofree gchar *escaped = NULL;

  if (name[0] == '@')
    {
      escaped = g_dbus_address_escape_value (&name[1]);
      address = g_strdup_printf ("unix:abstract=%s", escaped);
    }
  else if (name[0] == '/')
    {
      escaped = g_dbus_address_escape_value (name);
      address = g_strdup_printf ("unix:path=%s", escaped);
    }
  else
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Invalid socket address '%s'", name);
      return FALSE;
    }

  return listen_on_address (address, error);
}

static gboolean
signal_handler (int sfd,
                G_GNUC_UNUSED GIOCondition condition,
                G_GNUC_UNUSED gpointer data)
{
  struct signalfd_siginfo info;
  ssize_t size;

  size = read (sfd, &info, sizeof (info));

  if (size < 0)
    {
      if (errno != EINTR && errno != EAGAIN)
        g_warning ("Unable to read struct signalfd_siginfo: %s",
                   g_strerror (errno));
    }
  else if (size != sizeof (info))
    {
      g_warning ("Expected struct signalfd_siginfo of size %"
                 G_GSIZE_FORMAT ", got %" G_GSSIZE_FORMAT,
                 sizeof (info), size);
    }
  else
    {
      terminate_children (info.ssi_signo);
      g_main_loop_quit (main_loop);
    }

  return G_SOURCE_CONTINUE;
}

static guint
connect_to_signals (void)
{
  static int signals[] = { SIGHUP, SIGINT, SIGTERM };
  sigset_t mask;
  guint i;
  int sfd;

  sigemptyset (&mask);

  for (i = 0; i < G_N_ELEMENTS (signals); i++)
    sigaddset (&mask, signals[i]);

  sfd = signalfd (-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);

  if (sfd < 0)
    {
      g_warning ("Unable to watch signals: %s", g_strerror (errno));
      return 0;
    }

  /*
   * We have to block the signals, for two reasons:
   * - If we didn't, most of them would kill our process.
   *   Listening for a signal with a signalfd does not prevent the signal's
   *   default disposition from being acted on.
   * - Reading from a signalfd only returns information about the signals
   *   that are still pending for the process. If we ignored them instead
   *   of blocking them, they would no longer be pending by the time the
   *   main loop wakes up and reads from the signalfd.
   */
  pthread_sigmask (SIG_BLOCK, &mask, NULL);

  return g_unix_fd_add (sfd, G_IO_IN, signal_handler, NULL);
}

/*
 * If @fd is `stdin`, make `stdin` point to /dev/null and return a
 * new fd that is a duplicate of the original `stdin`, so that the
 * `stdin` inherited by child processes will not collide with the fd
 * we are using for some other purpose.
 */
static int
avoid_stdin (int fd,
             GError **error)
{
  g_return_val_if_fail (fd >= 0, FALSE);

  if (fd == STDIN_FILENO)
    {
      glnx_autofd int old_stdin = -1;
      glnx_autofd int new_stdin = -1;
      int fd_flags;

      old_stdin = dup (STDIN_FILENO);

      if (old_stdin < 0)
        {
          glnx_throw_errno_prefix (error,
                                   "Unable to duplicate standard input");
          return -1;
        }

      fd_flags = fcntl (old_stdin, F_GETFD);

      if (fd_flags < 0 ||
          fcntl (old_stdin, F_SETFD, fd_flags | FD_CLOEXEC) != 0)
        {
          glnx_throw_errno_prefix (error, "Unable to set flags on fd %d",
                                   old_stdin);
          return -1;
        }

      new_stdin = open ("/dev/null", O_RDONLY | O_CLOEXEC);

      if (new_stdin < 0)
        {
          glnx_throw_errno_prefix (error, "Unable to open /dev/null");
          return -1;
        }

      if (dup2 (new_stdin, STDIN_FILENO) != STDIN_FILENO)
        {
          glnx_throw_errno_prefix (error,
                                   "Unable to make stdin point to /dev/null");
          return -1;
        }

      fd = glnx_steal_fd (&old_stdin);
    }

  return fd;
}

static gboolean
exit_on_readable_cb (int fd,
                     GIOCondition condition,
                     gpointer user_data)
{
  guint *id_p = user_data;

  terminate_children (SIGTERM);
  g_main_loop_quit (main_loop);
  *id_p = 0;
  return G_SOURCE_REMOVE;
}

static gboolean
set_up_exit_on_readable (int fd,
                         guint *id_p,
                         GError **error)
{
  g_return_val_if_fail (fd >= 0, FALSE);
  g_return_val_if_fail (id_p != NULL, FALSE);
  g_return_val_if_fail (*id_p == 0, FALSE);

  if (fd == STDOUT_FILENO || fd == STDERR_FILENO)
    {
      return glnx_throw (error,
                         "--exit-on-readable fd cannot be stdout or stderr");
    }

  fd = avoid_stdin (fd, error);

  if (fd < 0)
    return FALSE;

  *id_p = g_unix_fd_add (fd, G_IO_IN|G_IO_ERR|G_IO_HUP, exit_on_readable_cb, id_p);
  return TRUE;
}

static gchar *opt_bus_name = NULL;
static gint opt_exit_on_readable_fd = -1;
static gboolean opt_replace = FALSE;
static gchar *opt_socket = NULL;
static gchar *opt_socket_directory = NULL;
static gboolean opt_verbose = FALSE;
static gboolean opt_version = FALSE;

static GOptionEntry options[] =
{
  { "bus-name", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &opt_bus_name,
    "Use this well-known name on the D-Bus session bus.",
    "NAME" },
  { "exit-on-readable", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &opt_exit_on_readable_fd,
    "Exit when data is available for reading or when end-of-file is "
    "reached on this fd, usually 0 for stdin.",
    "FD" },
  { "replace", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_replace,
    "Replace a previous instance with the same bus name. "
    "Ignored if --bus-name is not used.",
    NULL },
  { "socket", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &opt_socket,
    "Listen on this AF_UNIX socket.",
    "ABSPATH|@ABSTRACT" },
  { "socket-directory", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_socket_directory,
    "Listen on an arbitrary AF_UNIX socket in this directory. "
    "Print the filename (socket=/path/to/socket), the "
    "D-Bus address (dbus_address=unix:...) and possibly other "
    "fields on stdout, one per line.",
    "PATH" },
  { "verbose", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_verbose,
    "Be more verbose.", NULL },
  { "version", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_version,
    "Print version number and exit.", NULL },
  { NULL }
};

static void
cli_log_func (const gchar *log_domain,
              GLogLevelFlags log_level,
              const gchar *message,
              gpointer user_data)
{
  g_printerr ("%s: %s\n", (const char *) user_data, message);
}

int
main (int argc,
      char *argv[])
{
  g_autoptr(AutoDBusServer) server = NULL;
  g_autoptr(GOptionContext) context = NULL;
  guint signals_id = 0;
  guint exit_on_readable_id = 0;
  g_autoptr(GError) local_error = NULL;
  GError **error = &local_error;
  gsize i;
  int ret = EX_USAGE;

  setlocale (LC_ALL, "");

  g_set_prgname ("pressure-vessel-launcher");

  g_log_set_handler (G_LOG_DOMAIN,
                     G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE,
                     cli_log_func, (void *) g_get_prgname ());

  context = g_option_context_new ("");
  g_option_context_set_summary (context,
                                "Accept IPC requests to create child "
                                "processes.");

  g_option_context_add_main_entries (context, options, NULL);
  opt_verbose = pv_boolean_environment ("PRESSURE_VESSEL_VERBOSE", FALSE);

  if (!g_option_context_parse (context, &argc, &argv, error))
    {
      ret = EX_USAGE;
      goto out;
    }

  if (opt_version)
    {
      g_print ("%s:\n"
               " Package: pressure-vessel\n"
               " Version: %s\n",
               g_get_prgname (), VERSION);
      ret = 0;
      goto out;
    }

  if (opt_verbose)
    g_log_set_handler (G_LOG_DOMAIN,
                       G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_INFO,
                       cli_log_func, (void *) g_get_prgname ());

  original_stdout = pv_divert_stdout_to_stderr (error);

  if (original_stdout == NULL)
    {
      ret = EX_OSERR;
      goto out;
    }

  if (opt_exit_on_readable_fd >= 0)
    {
      if (!set_up_exit_on_readable (opt_exit_on_readable_fd,
                                    &exit_on_readable_id, error))
        {
          ret = EX_OSERR;
          goto out;
        }
    }

  /* We have to block the signals we want to forward before we start any
   * other thread, and in particular the GDBus worker thread, because
   * the signal mask is per-thread. We need all threads to have the same
   * mask, otherwise a thread that doesn't have the mask will receive
   * process-directed signals, causing the whole process to exit. */
  signals_id = connect_to_signals ();

  if (signals_id == 0)
    {
      ret = EX_OSERR;
      goto out;
    }

  pv_avoid_gvfs ();

  if (argc >= 2 && strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  if (argc != 1)
    {
      glnx_throw (error, "Usage: %s [OPTIONS]", g_get_prgname ());
      goto out;
    }

  client_pid_data_hash = g_hash_table_new_full (NULL, NULL, NULL, (GDestroyNotify) pid_data_free);

  /* --socket argument needs to be printable so we can print
   * "socket=%s\n" without escaping */
  if (opt_socket != NULL)
    {
      for (i = 0; opt_socket[i] != '\0'; i++)
        {
          if (!g_ascii_isprint (opt_socket[i]))
            {
              glnx_throw (error,
                          "Non-printable characters not allowed in --socket");
              goto out;
            }
        }
    }

  /* --socket-directory argument likewise */
  if (opt_socket_directory != NULL)
    {
      for (i = 0; opt_socket_directory[i] != '\0'; i++)
        {
          if (!g_ascii_isprint (opt_socket_directory[i]))
            {
              glnx_throw (error,
                          "Non-printable characters not allowed in "
                          "--socket-directory");
              goto out;
            }
        }
    }

  /* Exit with this status until we know otherwise */
  ret = EX_SOFTWARE;

  if (opt_bus_name != NULL)
    {
      GBusNameOwnerFlags flags;

      if (opt_socket != NULL || opt_socket_directory != NULL)
        {
          glnx_throw (error,
                      "--bus-name cannot be combined with --socket or "
                      "--socket-directory");
          ret = EX_USAGE;
          goto out;
        }

      g_debug ("Connecting to D-Bus session bus...");

      session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);

      if (session_bus == NULL)
        {
          glnx_prefix_error (error, "Can't find session bus");
          goto out;
        }

      flags = G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT;
      if (opt_replace)
        flags |= G_BUS_NAME_OWNER_FLAGS_REPLACE;

      ret = EX_UNAVAILABLE;
      g_debug ("Claiming bus name %s...", opt_bus_name);
      name_owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
                                      opt_bus_name,
                                      flags,
                                      on_bus_acquired,
                                      on_name_acquired,
                                      on_name_lost,
                                      &ret,
                                      NULL);
    }
  else if (opt_socket != NULL)
    {
      if (opt_socket_directory != NULL)
        {
          glnx_throw (error,
                      "--socket and --socket-directory cannot both be used");
          ret = EX_USAGE;
          goto out;
        }

      g_debug ("Listening on socket %s...", opt_socket);
      server = listen_on_socket (opt_socket, error);

      if (server == NULL)
        {
          glnx_prefix_error (error,
                             "Unable to listen on socket \"%s\"", opt_socket);
          goto out;
        }

      ret = 0;
    }
  else if (opt_socket_directory != NULL)
    {
      g_autofree gchar *unique = NULL;
      g_autofree gchar *dir = NULL;

      if (strlen (opt_socket_directory) > PV_MAX_SOCKET_DIRECTORY_LEN)
        {
          glnx_throw (error, "Socket directory path \"%s\" too long",
                      opt_socket_directory);
          goto out;
        }

      dir = realpath (opt_socket_directory, NULL);

      if (strlen (dir) > PV_MAX_SOCKET_DIRECTORY_LEN)
        {
          glnx_throw (error, "Socket directory path \"%s\" too long", dir);
          goto out;
        }

      g_debug ("Listening on a socket in %s...", opt_socket_directory);

      /* @unique is long and random, so we assume it is not guessable
       * by an attacker seeking to deny service by using the name we
       * intended to use; so we don't need a retry loop for alternative
       * names in the same directory. */
      unique = pv_get_random_uuid (error);

      if (unique == NULL)
        goto out;

      opt_socket = g_build_filename (dir, unique, NULL);
      g_debug ("Chosen socket is %s", opt_socket);
      server = listen_on_socket (opt_socket, error);

      if (server == NULL)
        {
          glnx_prefix_error (error,
                             "Unable to listen on socket \"%s\"", opt_socket);
          goto out;
        }

      ret = 0;
    }
  else
    {
      glnx_throw (error,
                  "--bus-name, --socket or --socket-directory is required");
      ret = EX_USAGE;
      goto out;
    }

  if (opt_socket != NULL)
    fprintf (original_stdout, "socket=%s\n", opt_socket);

  if (server != NULL)
    fprintf (original_stdout, "dbus_address=%s\n",
             g_dbus_server_get_client_address (server));

  if (opt_bus_name == NULL)
    {
      fflush (original_stdout);
      g_clear_pointer (&original_stdout, fclose);
    }

  g_debug ("Entering main loop");

  main_loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (main_loop);
  g_debug ("Exiting");

out:
  if (local_error != NULL)
    g_warning ("%s", local_error->message);

  if (exit_on_readable_id > 0)
    g_source_remove (exit_on_readable_id);

  if (signals_id > 0)
    g_source_remove (signals_id);

  if (name_owner_id)
    g_bus_unown_name (name_owner_id);

  if (server != NULL && opt_socket != NULL)
    unlink (opt_socket);

  g_free (opt_bus_name);
  g_free (opt_socket);
  g_free (opt_socket_directory);
  g_clear_object (&session_bus);

  if (local_error == NULL)
    ret = 0;
  else if (local_error->domain == G_OPTION_ERROR)
    ret = EX_USAGE;
  else
    ret = EX_UNAVAILABLE;

  g_clear_error (&local_error);
  g_clear_pointer (&original_stdout, fclose);

  return ret;
}
