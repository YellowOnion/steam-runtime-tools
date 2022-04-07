/*
 * steam-runtime-launch-client — send IPC requests to create child processes
 *
 * Copyright © 2018 Red Hat, Inc.
 * Copyright © 2020-2021 Collabora Ltd.
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
 * Based on flatpak-spawn from the flatpak-xdg-utils package.
 * Copyright © 2018 Red Hat, Inc.
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#include "config.h"

#include <fnmatch.h>
#include <locale.h>
#include <sysexits.h>
#include <sys/signalfd.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/launcher-internal.h"
#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

#include "flatpak-portal.h"
#include "flatpak-session-helper.h"

typedef enum {
  FLATPAK_HOST_COMMAND_FLAGS_CLEAR_ENV = 1 << 0,
  FLATPAK_HOST_COMMAND_FLAGS_WATCH_BUS = 1 << 1, /* Since 1.2 */
} FlatpakHostCommandFlags;

typedef struct
{
  const char *service_iface;
  const char *service_obj_path;
  const char *service_bus_name;
  const char *send_signal_method;
  const char *exit_signal;
  const char *launch_method;
  guint clear_env_flag;
} Api;

static Api launcher_api =
{
  .service_iface = LAUNCHER_IFACE,
  .service_obj_path = LAUNCHER_PATH,
  .service_bus_name = NULL,
  .send_signal_method = "SendSignal",
  .exit_signal = "ProcessExited",
  .launch_method = "Launch",
  .clear_env_flag = PV_LAUNCH_FLAGS_CLEAR_ENV,
};

static const Api host_api =
{
  .service_iface = FLATPAK_SESSION_HELPER_INTERFACE_DEVELOPMENT,
  .service_obj_path = FLATPAK_SESSION_HELPER_PATH_DEVELOPMENT,
  .service_bus_name = FLATPAK_SESSION_HELPER_BUS_NAME,
  .send_signal_method = "HostCommandSignal",
  .exit_signal = "HostCommandExited",
  .launch_method = "HostCommand",
  .clear_env_flag = FLATPAK_HOST_COMMAND_FLAGS_CLEAR_ENV,
};

static const Api subsandbox_api =
{
  .service_iface = FLATPAK_PORTAL_INTERFACE,
  .service_obj_path = FLATPAK_PORTAL_PATH,
  .service_bus_name = FLATPAK_PORTAL_BUS_NAME,
  .send_signal_method = "SpawnSignal",
  .exit_signal = "SpawnExited",
  .launch_method = "Spawn",
  .clear_env_flag = FLATPAK_SPAWN_FLAGS_CLEAR_ENV,
};

static const Api *api = NULL;

typedef GUnixFDList AutoUnixFDList;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AutoUnixFDList, g_object_unref)

static const char * const *global_original_environ = NULL;
static GDBusConnection *bus_or_peer_connection = NULL;
static guint child_pid = 0;
static int launch_exit_status = LAUNCH_EX_USAGE;

static void
process_exited_cb (G_GNUC_UNUSED GDBusConnection *connection,
                   G_GNUC_UNUSED const gchar     *sender_name,
                   G_GNUC_UNUSED const gchar     *object_path,
                   G_GNUC_UNUSED const gchar     *interface_name,
                   G_GNUC_UNUSED const gchar     *signal_name,
                   GVariant                      *parameters,
                   gpointer                       user_data)
{
  GMainLoop *loop = user_data;
  guint32 client_pid = 0;
  guint32 wait_status = 0;

  if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(uu)")))
    return;

  g_variant_get (parameters, "(uu)", &client_pid, &wait_status);
  g_debug ("child %d exited: wait status %d", client_pid, wait_status);

  if (child_pid == client_pid)
    {
      int exit_code = 0;

      if (WIFEXITED (wait_status))
        {
          exit_code = WEXITSTATUS (wait_status);
        }
      else if (WIFSIGNALED (wait_status))
        {
          /* Smush the signal into an unsigned byte, as the shell does. This is
           * not quite right from the perspective of whatever ran flatpak-spawn
           * — it will get WIFEXITED() not WIFSIGNALED() — but the
           *  alternative is to disconnect all signal() handlers then send this
           *  signal to ourselves and hope it kills us.
           */
          exit_code = 128 + WTERMSIG (wait_status);
        }
      else
        {
          /* wait(3p) claims that if the waitpid() call that returned the exit
           * code specified neither WUNTRACED nor WIFSIGNALED, then exactly one
           * of WIFEXITED() or WIFSIGNALED() will be true.
           */
          g_warning ("wait status %d is neither WIFEXITED() nor WIFSIGNALED()",
                     wait_status);
          exit_code = LAUNCH_EX_CANNOT_REPORT;
        }

      g_debug ("child exit code %d: %d", client_pid, exit_code);
      launch_exit_status = exit_code;
      g_main_loop_quit (loop);
    }
}

static void
forward_signal (int sig)
{
  G_GNUC_UNUSED g_autoptr(GVariant) reply = NULL;
  gboolean to_process_group = FALSE;
  g_autoptr(GError) error = NULL;

  g_return_if_fail (api != NULL);

  if (child_pid == 0)
    {
      /* We are not monitoring a child yet, so let the signal act on
       * this main process instead */
      if (sig == SIGTSTP || sig == SIGSTOP || sig == SIGTTIN || sig == SIGTTOU)
        {
          raise (SIGSTOP);
        }
      else if (sig != SIGCONT)
        {
          sigset_t mask;

          sigemptyset (&mask);
          sigaddset (&mask, sig);
          /* Unblock it, so that it will be delivered properly this time.
           * Use pthread_sigmask instead of sigprocmask because the latter
           * has unspecified behaviour in a multi-threaded process. */
          pthread_sigmask (SIG_UNBLOCK, &mask, NULL);
          raise (sig);
        }

      return;
    }

  g_debug ("Forwarding signal: %d", sig);

  /* We forward stop requests as real stop, because the default doesn't
     seem to be to stop for non-kernel sent TSTP??? */
  if (sig == SIGTSTP)
    sig = SIGSTOP;

  /* ctrl-c/z is typically for the entire process group */
  if (sig == SIGINT || sig == SIGSTOP || sig == SIGCONT)
    to_process_group = TRUE;

  reply = g_dbus_connection_call_sync (bus_or_peer_connection,
                                       api->service_bus_name,   /* NULL if p2p */
                                       api->service_obj_path,
                                       api->service_iface,
                                       api->send_signal_method,
                                       g_variant_new ("(uub)",
                                                      child_pid, sig,
                                                      to_process_group),
                                       G_VARIANT_TYPE ("()"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1, NULL, &error);

  if (error)
    g_info ("Failed to forward signal: %s", error->message);

  if (sig == SIGSTOP)
    {
      g_info ("SIGSTOP:ing myself");
      raise (SIGSTOP);
    }
}

static gboolean
forward_signal_handler (int sfd,
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
      forward_signal (info.ssi_signo);
    }

  return G_SOURCE_CONTINUE;
}

static guint
forward_signals (GError **error)
{
  static int forward[] = {
    SIGHUP, SIGINT, SIGQUIT, SIGTERM, SIGCONT, SIGTSTP, SIGUSR1, SIGUSR2
  };
  sigset_t mask;
  guint i;
  int sfd;

  sigemptyset (&mask);

  for (i = 0; i < G_N_ELEMENTS (forward); i++)
    sigaddset (&mask, forward[i]);

  sfd = signalfd (-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);

  if (sfd < 0)
    {
      glnx_throw_errno_prefix (error, "Unable to watch signals");
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
  if (pthread_sigmask (SIG_BLOCK, &mask, NULL) != 0)
    {
      glnx_throw_errno_prefix (error, "Unable to block signals");
      return 0;
    }

  return g_unix_fd_add (sfd, G_IO_IN, forward_signal_handler, NULL);
}

static void
name_owner_changed (G_GNUC_UNUSED GDBusConnection *connection,
                    G_GNUC_UNUSED const gchar     *sender_name,
                    G_GNUC_UNUSED const gchar     *object_path,
                    G_GNUC_UNUSED const gchar     *interface_name,
                    G_GNUC_UNUSED const gchar     *signal_name,
                    GVariant                      *parameters,
                    G_GNUC_UNUSED gpointer         user_data)
{
  GMainLoop *loop = user_data;
  const char *name, *from, *to;

  g_return_if_fail (api != NULL);

  g_variant_get (parameters, "(sss)", &name, &from, &to);

  /* Check if the service dies, then we exit, because we can't track it anymore */
  if (strcmp (name, api->service_bus_name) == 0 &&
      strcmp (to, "") == 0)
    {
      g_debug ("portal exited");

      if (child_pid == 0)
        launch_exit_status = LAUNCH_EX_FAILED;
      else
        launch_exit_status = LAUNCH_EX_CANNOT_REPORT;

      g_main_loop_quit (loop);
    }
}

static void
connection_closed_cb (G_GNUC_UNUSED GDBusConnection *conn,
                      G_GNUC_UNUSED gboolean remote_peer_vanished,
                      G_GNUC_UNUSED GError *error,
                      GMainLoop *loop)
{
  g_debug ("D-Bus connection closed, quitting");

  if (child_pid == 0)
    launch_exit_status = LAUNCH_EX_FAILED;
  else
    launch_exit_status = LAUNCH_EX_CANNOT_REPORT;

  g_main_loop_quit (loop);
}

static guint32
get_portal_version (void)
{
  static guint32 version = 0;

  g_return_val_if_fail (api != NULL, 0);
  g_return_val_if_fail (api == &host_api || api == &subsandbox_api, 0);

  if (version == 0)
    {
      g_autoptr(GError) error = NULL;
      g_autoptr(GVariant) reply =
        g_dbus_connection_call_sync (bus_or_peer_connection,
                                     api->service_bus_name,
                                     api->service_obj_path,
                                     "org.freedesktop.DBus.Properties",
                                     "Get",
                                     g_variant_new ("(ss)", api->service_iface, "version"),
                                     G_VARIANT_TYPE ("(v)"),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL, &error);

      if (reply == NULL)
        g_debug ("Failed to get version: %s", error->message);
      else
        {
          g_autoptr(GVariant) v = g_variant_get_child_value (reply, 0);
          g_autoptr(GVariant) v2 = g_variant_get_variant (v);
          version = g_variant_get_uint32 (v2);
        }
    }

  return version;
}

static void
check_portal_version (const char *option, guint32 version_needed)
{
  guint32 portal_version = get_portal_version ();
  if (portal_version < version_needed)
    {
      g_printerr ("--%s not supported by host portal version (need version %d, has %d)\n", option, version_needed, portal_version);
      exit (1);
    }
}

static guint32
get_portal_supports (void)
{
  static guint32 supports = 0;
  static gboolean ran = FALSE;

  g_return_val_if_fail (api != NULL, 0);
  g_return_val_if_fail (api == &host_api || api == &subsandbox_api, 0);

  if (!ran)
    {
      g_autoptr(GError) error = NULL;
      g_autoptr(GVariant) reply = NULL;

      ran = TRUE;

      /* Support flags were added in version 3 */
      if (get_portal_version () >= 3)
        {
          reply = g_dbus_connection_call_sync (bus_or_peer_connection,
                                               api->service_bus_name,
                                               api->service_obj_path,
                                               "org.freedesktop.DBus.Properties",
                                               "Get",
                                               g_variant_new ("(ss)", api->service_iface, "supports"),
                                               G_VARIANT_TYPE ("(v)"),
                                               G_DBUS_CALL_FLAGS_NONE,
                                               -1,
                                               NULL, &error);
          if (reply == NULL)
            g_debug ("Failed to get supports: %s", error->message);
          else
            {
              g_autoptr(GVariant) v = g_variant_get_child_value (reply, 0);
              g_autoptr(GVariant) v2 = g_variant_get_variant (v);
              supports = g_variant_get_uint32 (v2);
            }
        }
    }

  return supports;
}

#define NOT_SETUID_ROOT_MESSAGE \
"This feature requires Flatpak to be using a bubblewrap (bwrap) executable\n" \
"that is not setuid root.\n" \
"\n" \
"The non-setuid version of bubblewrap requires a kernel that allows\n" \
"unprivileged users to create new user namespaces.\n" \
"\n" \
"For more details please see:\n" \
"https://github.com/flatpak/flatpak/wiki/User-namespace-requirements\n" \
"\n"

static void
check_portal_supports (const char *option, guint32 supports_needed)
{
  guint32 supports = get_portal_supports ();

  if ((supports & supports_needed) != supports_needed)
    {
      g_printerr ("--%s not supported by host portal\n", option);

      if (supports_needed == FLATPAK_SPAWN_SUPPORT_FLAGS_EXPOSE_PIDS)
        g_printerr ("\n%s", NOT_SETUID_ROOT_MESSAGE);

      exit (1);
    }
}

static gint32
path_to_handle (GUnixFDList *fd_list,
                const char *path,
                const char *home_realpath,
                const char *flatpak_id,
                GError **error)
{
  int path_fd = open (path, O_PATH|O_CLOEXEC|O_NOFOLLOW|O_RDONLY);
  int saved_errno;
  gint32 handle;

  if (path_fd < 0)
    {
      saved_errno = errno;
      g_set_error (error, G_IO_ERROR, g_io_error_from_errno (saved_errno),
                   "Failed to open %s to expose in sandbox: %s",
                   path, g_strerror (saved_errno));
      return -1;
    }

  if (home_realpath != NULL && flatpak_id != NULL)
    {
      g_autofree char *real = NULL;
      const char *after = NULL;

      real = realpath (path, NULL);

      if (real != NULL)
        after = _srt_get_path_after (real, home_realpath);

      if (after != NULL)
        {
          g_autofree char *var_path = NULL;
          int var_fd = -1;
          struct stat path_buf;
          struct stat var_buf;

          /* @after is possibly "", but that's OK: if @path is exactly $HOME,
           * we want to check whether it's the same file as
           * ~/.var/app/$FLATPAK_ID, with no suffix */
          var_path = g_build_filename (home_realpath, ".var", "app", flatpak_id,
                                       after, NULL);

          var_fd = open (var_path, O_PATH|O_CLOEXEC|O_NOFOLLOW|O_RDONLY);

          if (var_fd >= 0 &&
              fstat (path_fd, &path_buf) == 0 &&
              fstat (var_fd, &var_buf) == 0 &&
              path_buf.st_dev == var_buf.st_dev &&
              path_buf.st_ino == var_buf.st_ino)
            {
              close (path_fd);
              path_fd = var_fd;
            }
          else
            {
              close (var_fd);
            }
        }
    }


  handle = g_unix_fd_list_append (fd_list, path_fd, error);

  if (handle < 0)
    {
      g_prefix_error (error, "Failed to add fd to list for %s: ", path);
      return -1;
    }

  /* The GUnixFdList keeps a duplicate, so we should release the original */
  close (path_fd);
  return handle;
}

static gchar **forward_fds = NULL;
static gchar *opt_app_path = NULL;
static gboolean opt_clear_env = FALSE;
static gchar *opt_dbus_address = NULL;
static gchar *opt_directory = NULL;
static gchar *opt_socket = NULL;
static GHashTable *opt_env = NULL;
static GHashTable *opt_unsetenv = NULL;
static gboolean opt_share_pids = FALSE;
static gboolean opt_terminate = FALSE;
static gchar *opt_usr_path = NULL;
static gboolean opt_verbose = FALSE;
static gboolean opt_version = FALSE;

static gboolean
opt_env_cb (const char *option_name,
            const gchar *value,
            G_GNUC_UNUSED gpointer data,
            GError **error)
{
  g_assert (opt_env != NULL);
  g_assert (opt_unsetenv != NULL);

  if (g_strcmp0 (option_name, "--env") == 0)
    {
      g_auto(GStrv) split = g_strsplit (value, "=", 2);

      if (split == NULL ||
          split[0] == NULL ||
          split[0][0] == 0 ||
          split[1] == NULL)
        {
          g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                       "Invalid env format %s", value);
          return FALSE;
        }

      g_hash_table_remove (opt_unsetenv, split[0]);
      g_hash_table_replace (opt_env,
                            g_steal_pointer (&split[0]),
                            g_steal_pointer (&split[1]));
      return TRUE;
    }

  if (g_strcmp0 (option_name, "--pass-env") == 0)
    {
      const gchar *env = g_getenv (value);

      if (env != NULL)
        {
          g_hash_table_remove (opt_unsetenv, value);
          g_hash_table_replace (opt_env, g_strdup (value), g_strdup (env));
        }
      else
        {
          g_hash_table_remove (opt_env, value);
          g_hash_table_add (opt_unsetenv, g_strdup (value));
        }

      return TRUE;
    }

  if (g_strcmp0 (option_name, "--pass-env-matching") == 0)
    {
      const char * const *iter;

      if (global_original_environ == NULL)
        return TRUE;

      for (iter = global_original_environ; *iter != NULL; iter++)
        {
          g_auto(GStrv) split = g_strsplit (*iter, "=", 2);

          if (split == NULL ||
              split[0] == NULL ||
              split[0][0] == 0 ||
              split[1] == NULL)
            continue;

          if (fnmatch (value, split[0], 0) == 0)
            {
              g_hash_table_remove (opt_unsetenv, split[0]);
              g_hash_table_replace (opt_env,
                                    g_steal_pointer (&split[0]),
                                    g_steal_pointer (&split[1]));
            }
        }

      return TRUE;
    }

  if (g_strcmp0 (option_name, "--unset-env") == 0)
    {
      g_hash_table_remove (opt_env, value);
      g_hash_table_add (opt_unsetenv, g_strdup (value));
      return TRUE;
    }

  g_return_val_if_reached (FALSE);
}

static const GOptionEntry options[] =
{
  { "app-path", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_app_path,
    "Use DIR as the /app for a Flatpak sub-sandbox. "
    "Requires '--bus-name=org.freedesktop.portal.Flatpak'.",
    "DIR" },
  { "bus-name", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &launcher_api.service_bus_name,
    "Connect to a Launcher service with this name on the session bus.",
    "NAME" },
  { "dbus-address", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &opt_dbus_address,
    "Connect to a Launcher server listening on this D-Bus address.",
    "ADDRESS" },
  { "clear-env", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_clear_env,
    "Run with clean environment.", NULL },
  { "directory", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_directory,
    "Working directory in which to run the command.", "DIR" },
  { "env", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_env_cb,
    "Set environment variable.", "VAR=VALUE" },
  { "forward-fd", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING_ARRAY, &forward_fds,
    "Connect a file descriptor to the launched process. "
    "fds 0, 1 and 2 are automatically forwarded.",
    "FD" },
  { "pass-env", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_env_cb,
    "Pass environment variable through, or unset if set.", "VAR" },
  { "pass-env-matching", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_env_cb,
    "Pass environment variables matching a shell-style wildcard.",
    "WILDCARD" },
  { "share-pids", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_share_pids,
    "Use same pid namespace as calling sandbox.", NULL },
  { "usr-path", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_usr_path,
    "Use DIR as the /usr for a Flatpak sub-sandbox. "
    "Requires '--bus-name=org.freedesktop.portal.Flatpak'.",
    "DIR" },
  { "socket", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &opt_socket,
    "Connect to a Launcher server listening on this AF_UNIX socket.",
    "ABSPATH|@ABSTRACT" },
  { "terminate", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_terminate,
    "Terminate the Launcher server after the COMMAND (if any) has run.",
    NULL },
  { "unset-env", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_env_cb,
    "Unset environment variable, like env -u.", "VAR" },
  { "verbose", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_verbose,
    "Be more verbose.", NULL },
  { "version", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_version,
    "Print version number and exit.", NULL },
  { NULL }
};

int
main (int argc,
      char *argv[])
{
  g_auto(GStrv) original_environ = NULL;
  g_autoptr(GMainLoop) loop = NULL;
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GPtrArray) replacement_command_and_args = NULL;
  g_autoptr(GError) local_error = NULL;
  GError **error = &local_error;
  char **command_and_args;
  g_autoptr(FILE) original_stdout = NULL;
  g_autoptr(GDBusConnection) session_bus = NULL;
  g_autoptr(GDBusConnection) peer_connection = NULL;
  g_auto(GVariantBuilder) fd_builder = {};
  g_auto(GVariantBuilder) env_builder = {};
  g_auto(GVariantBuilder) options_builder = {};
  g_autoptr(AutoUnixFDList) fd_list = NULL;
  G_GNUC_UNUSED g_autoptr(GVariant) reply = NULL;
  gint stdin_handle = -1;
  gint stdout_handle = -1;
  gint stderr_handle = -1;
  guint spawn_flags = 0;
  guint signal_source = 0;
  gsize i;
  GHashTableIter iter;
  gpointer key, value;
  g_autofree char *home_realpath = NULL;
  const char *flatpak_id = NULL;

  setlocale (LC_ALL, "");

  original_environ = g_get_environ ();
  global_original_environ = (const char * const *) original_environ;

  g_set_prgname ("steam-runtime-launch-client");

  /* Set up the initial base logging */
  _srt_util_set_glib_log_handler (FALSE);

  context = g_option_context_new ("COMMAND [ARG...]");
  g_option_context_set_summary (context,
                                "Send IPC requests to create child "
                                "processes.");

  g_option_context_add_main_entries (context, options, NULL);
  opt_env = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  opt_unsetenv = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  opt_verbose = _srt_boolean_environment ("PRESSURE_VESSEL_VERBOSE", FALSE);

  if (!g_option_context_parse (context, &argc, &argv, error))
    goto out;

  if (opt_version)
    {
      g_print ("%s:\n"
               " Package: pressure-vessel\n"
               " Version: %s\n",
               g_get_prgname (), VERSION);
      launch_exit_status = 0;
      goto out;
    }

  if (opt_verbose)
    _srt_util_set_glib_log_handler (opt_verbose);

  original_stdout = _srt_divert_stdout_to_stderr (error);

  if (original_stdout == NULL)
    {
      launch_exit_status = LAUNCH_EX_FAILED;
      goto out;
    }

  if (argc > 1)
    {
      /* We have to block the signals we want to forward before we start any
       * other thread, and in particular the GDBus worker thread, because
       * the signal mask is per-thread. We need all threads to have the same
       * mask, otherwise a thread that doesn't have the mask will receive
       * process-directed signals, causing the whole process to exit. */
      signal_source = forward_signals (error);

      if (signal_source == 0)
        {
          launch_exit_status = LAUNCH_EX_FAILED;
          goto out;
        }
    }

  _srt_setenv_disable_gio_modules ();

  flatpak_id = g_environ_getenv (original_environ, "FLATPAK_ID");

  if (flatpak_id != NULL)
    home_realpath = realpath (g_get_home_dir (), NULL);

  if (launcher_api.service_bus_name != NULL && opt_socket != NULL)
    {
      glnx_throw (error, "--bus-name and --socket cannot both be used");
      goto out;
    }

  if (g_strcmp0 (launcher_api.service_bus_name,
                 host_api.service_bus_name) == 0)
    api = &host_api;
  else if (g_strcmp0 (launcher_api.service_bus_name,
                      subsandbox_api.service_bus_name) == 0)
    api = &subsandbox_api;
  else
    api = &launcher_api;

  if (api != &launcher_api && opt_terminate)
    {
      glnx_throw (error,
                  "--terminate cannot be used with Flatpak services");
      goto out;
    }

  if (api != &subsandbox_api && opt_app_path != NULL)
    {
      glnx_throw (error,
                  "--app-path can only be used with a Flatpak subsandbox");
      goto out;
    }

  if (api != &subsandbox_api && opt_usr_path != NULL)
    {
      glnx_throw (error,
                  "--usr-path can only be used with a Flatpak subsandbox");
      goto out;
    }

  if (argc >= 2 && strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  if (argc < 2)
    {
      if (!opt_terminate)
        {
          glnx_throw (error, "Usage: %s [OPTIONS] COMMAND [ARG...]",
                      g_get_prgname ());
          goto out;
        }

      command_and_args = NULL;
    }
  else
    {
      command_and_args = argv + 1;
    }

  launch_exit_status = LAUNCH_EX_FAILED;
  loop = g_main_loop_new (NULL, FALSE);

  g_assert (api != NULL);
  if (api->service_bus_name != NULL)
    {
      if (opt_dbus_address != NULL || opt_socket != NULL)
        {
          glnx_throw (error,
                      "--bus-name cannot be combined with "
                      "--dbus-address or --socket");
          launch_exit_status = LAUNCH_EX_USAGE;
          goto out;
        }

      session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, error);
      if (session_bus == NULL)
        {
          glnx_prefix_error (error, "Can't find session bus");
          goto out;
        }

      bus_or_peer_connection = session_bus;
    }
  else if (opt_dbus_address != NULL)
    {
      if (opt_socket != NULL)
        {
          glnx_throw (error,
                      "--dbus-address cannot be combined with --socket");
          launch_exit_status = LAUNCH_EX_USAGE;
          goto out;
        }

      peer_connection = g_dbus_connection_new_for_address_sync (opt_dbus_address,
                                                                G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                                                NULL,
                                                                NULL,
                                                                error);
      if (peer_connection == NULL)
        {
          glnx_prefix_error (error, "Can't connect to peer address");
          goto out;
        }

      bus_or_peer_connection = peer_connection;
    }
  else if (opt_socket != NULL)
    {
      g_autofree gchar *address = NULL;
      g_autofree gchar *escaped = NULL;

      if (opt_socket[0] == '@')
        {
          escaped = g_dbus_address_escape_value (&opt_socket[1]);
          address = g_strdup_printf ("unix:abstract=%s", escaped);
        }
      else if (opt_socket[0] == '/')
        {
          escaped = g_dbus_address_escape_value (opt_socket);
          address = g_strdup_printf ("unix:path=%s", escaped);
        }
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Invalid socket address '%s'", opt_socket);
          goto out;
        }

      peer_connection = g_dbus_connection_new_for_address_sync (address,
                                                                G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
                                                                NULL,
                                                                NULL,
                                                                error);
      if (peer_connection == NULL)
        {
          glnx_prefix_error (error, "Can't connect to peer socket");
          goto out;
        }

      bus_or_peer_connection = peer_connection;
    }
  else
    {
      glnx_throw (error, "--bus-name or --dbus-address or --socket is required");
      launch_exit_status = LAUNCH_EX_USAGE;
      goto out;
    }

  g_assert (bus_or_peer_connection != NULL);

  if (command_and_args == NULL)
    {
      g_assert (opt_terminate);   /* already checked */

      reply = g_dbus_connection_call_sync (bus_or_peer_connection,
                                           api->service_bus_name,
                                           api->service_obj_path,
                                           api->service_iface,
                                           "Terminate",
                                           g_variant_new ("()"),
                                           G_VARIANT_TYPE ("()"),
                                           G_DBUS_CALL_FLAGS_NONE,
                                           -1,
                                           NULL, error);

      if (local_error != NULL)
        g_dbus_error_strip_remote_error (local_error);

      goto out;
    }

  g_assert (command_and_args != NULL);
  g_dbus_connection_signal_subscribe (bus_or_peer_connection,
                                      api->service_bus_name,    /* NULL if p2p */
                                      api->service_iface,
                                      api->exit_signal,
                                      api->service_obj_path,
                                      NULL,
                                      G_DBUS_SIGNAL_FLAGS_NONE,
                                      process_exited_cb,
                                      g_main_loop_ref (loop),
                                      (GDestroyNotify) g_main_loop_unref);

  g_variant_builder_init (&fd_builder, G_VARIANT_TYPE ("a{uh}"));
  g_variant_builder_init (&env_builder, G_VARIANT_TYPE ("a{ss}"));
  fd_list = g_unix_fd_list_new ();

  stdin_handle = g_unix_fd_list_append (fd_list, 0, error);
  if (stdin_handle < 0)
    {
      glnx_prefix_error (error, "Can't append fd 0");
      goto out;
    }
  /* Remember that our stdout is now a copy of our original stderr,
   * so we need to bypass that and use our *original* stdout here. */
  stdout_handle = g_unix_fd_list_append (fd_list,
                                         fileno (original_stdout),
                                         error);
  if (stdout_handle < 0)
    {
      glnx_prefix_error (error, "Can't append fd 1");
      goto out;
    }
  stderr_handle = g_unix_fd_list_append (fd_list, 2, error);
  if (stderr_handle < 0)
    {
      glnx_prefix_error (error, "Can't append fd 2");
      goto out;
    }

  g_variant_builder_add (&fd_builder, "{uh}", 0, stdin_handle);
  g_variant_builder_add (&fd_builder, "{uh}", 1, stdout_handle);
  g_variant_builder_add (&fd_builder, "{uh}", 2, stderr_handle);

  for (i = 0; forward_fds != NULL && forward_fds[i] != NULL; i++)
    {
      int fd = strtol (forward_fds[i],  NULL, 10);
      gint handle = -1;

      if (fd == 0)
        {
          glnx_throw (error, "Invalid fd '%s'", forward_fds[i]);
          goto out;
        }

      if (fd >= 0 && fd <= 2)
        continue; // We always forward these

      handle = g_unix_fd_list_append (fd_list, fd, error);
      if (handle == -1)
        {
          glnx_prefix_error (error, "Can't append fd");
          goto out;
        }
      /* The GUnixFdList keeps a duplicate, so we should release the original */
      close (fd);
      g_variant_builder_add (&fd_builder, "{uh}", fd, handle);
    }

  g_hash_table_iter_init (&iter, opt_env);

  while (g_hash_table_iter_next (&iter, &key, &value))
    g_variant_builder_add (&env_builder, "{ss}", key, value);

  spawn_flags = 0;

  if (opt_clear_env)
    spawn_flags |= api->clear_env_flag;

  g_variant_builder_init (&options_builder, G_VARIANT_TYPE ("a{sv}"));

  if (opt_app_path != NULL)
    {
      gint32 handle;

      g_debug ("Using \"%s\" as /app instead of runtime", opt_app_path);

      g_assert (api == &subsandbox_api);
      check_portal_version ("app-path", 6);

      if (opt_app_path[0] == '\0')
        {
          /* Empty path is special-cased to mean an empty directory */
          spawn_flags |= FLATPAK_SPAWN_FLAGS_EMPTY_APP;
        }
      else
        {
          handle = path_to_handle (fd_list, opt_app_path, home_realpath,
                                   flatpak_id, error);

          if (handle < 0)
            goto out;

          g_variant_builder_add (&options_builder, "{s@v}", "app-fd",
                                 g_variant_new_variant (g_variant_new_handle (handle)));
        }
    }

  if (opt_usr_path != NULL)
    {
      gint32 handle;

      g_debug ("Using %s as /usr instead of runtime", opt_usr_path);

      g_assert (api == &subsandbox_api);
      check_portal_version ("usr-path", 6);

      handle = path_to_handle (fd_list, opt_usr_path, home_realpath,
                               flatpak_id, error);

      if (handle < 0)
        goto out;

      g_variant_builder_add (&options_builder, "{s@v}", "usr-fd",
                             g_variant_new_variant (g_variant_new_handle (handle)));
    }

  if (opt_terminate)
    {
      g_assert (api == &launcher_api);
      g_variant_builder_add (&options_builder, "{s@v}", "terminate-after",
                             g_variant_new_variant (g_variant_new_boolean (TRUE)));
    }

  /* We just ignore this option when not using a subsandbox:
   * host_api and launcher_api always share process IDs anyway */
  if (opt_share_pids && api == &subsandbox_api)
    {
      check_portal_version ("share-pids", 5);
      check_portal_supports ("share-pids", FLATPAK_SPAWN_SUPPORT_FLAGS_SHARE_PIDS);

      spawn_flags |= FLATPAK_SPAWN_FLAGS_SHARE_PIDS;
    }

  if (g_hash_table_size (opt_unsetenv) > 0)
    {
      g_hash_table_iter_init (&iter, opt_unsetenv);

      /* The host portal doesn't support options, so we always have to do
       * this the hard way. The subsandbox portal supports unset-env in
       * versions >= 5. steam-runtime-launcher-service always supports it. */
      if (api == &launcher_api
          || (api == &subsandbox_api && get_portal_version () >= 5))
        {
          GVariantBuilder strv_builder;

          g_variant_builder_init (&strv_builder, G_VARIANT_TYPE_STRING_ARRAY);

          while (g_hash_table_iter_next (&iter, &key, NULL))
            g_variant_builder_add (&strv_builder, "s", key);

          g_variant_builder_add (&options_builder, "{s@v}", "unset-env",
                                 g_variant_new_variant (g_variant_builder_end (&strv_builder)));
        }
      else
        {
          replacement_command_and_args = g_ptr_array_new_with_free_func (g_free);

          g_ptr_array_add (replacement_command_and_args, g_strdup ("/usr/bin/env"));

          while (g_hash_table_iter_next (&iter, &key, NULL))
            {
              g_ptr_array_add (replacement_command_and_args, g_strdup ("-u"));
              g_ptr_array_add (replacement_command_and_args, g_strdup (key));
            }

          if (strchr (command_and_args[0], '=') != NULL)
            {
              g_ptr_array_add (replacement_command_and_args, g_strdup ("/bin/sh"));
              g_ptr_array_add (replacement_command_and_args, g_strdup ("-euc"));
              g_ptr_array_add (replacement_command_and_args, g_strdup ("exec \"$@\""));
              g_ptr_array_add (replacement_command_and_args, g_strdup ("sh"));  /* argv[0] */
            }

          for (i = 0; command_and_args[i] != NULL; i++)
            g_ptr_array_add (replacement_command_and_args, g_strdup (command_and_args[i]));

          g_ptr_array_add (replacement_command_and_args, NULL);
          command_and_args = (char **) replacement_command_and_args->pdata;
        }
    }

  if (!opt_directory)
    {
      opt_directory = g_get_current_dir ();
    }

  if (session_bus != NULL)
    g_dbus_connection_signal_subscribe (session_bus,
                                        DBUS_NAME_DBUS,
                                        DBUS_INTERFACE_DBUS,
                                        "NameOwnerChanged",
                                        DBUS_PATH_DBUS,
                                        NULL,
                                        G_DBUS_SIGNAL_FLAGS_NONE,
                                        name_owner_changed,
                                        g_main_loop_ref (loop),
                                        (GDestroyNotify) g_main_loop_unref);

  {
    g_autoptr(GVariant) fds = NULL;
    g_autoptr(GVariant) env = NULL;
    g_autoptr(GVariant) opts = NULL;
    GVariant *arguments = NULL;   /* floating */

    g_debug ("Forwarding command:");

    for (i = 0; command_and_args[i] != NULL; i++)
      g_debug ("\t%s", command_and_args[i]);

    fds = g_variant_ref_sink (g_variant_builder_end (&fd_builder));
    env = g_variant_ref_sink (g_variant_builder_end (&env_builder));
    opts = g_variant_ref_sink (g_variant_builder_end (&options_builder));

    if (api == &host_api)
      {
        /* o.fd.Flatpak.Development doesn't take arbitrary options a{sv} */
        arguments = g_variant_new ("(^ay^aay@a{uh}@a{ss}u)",
                                   opt_directory,
                                   (const char * const *) command_and_args,
                                   fds,
                                   env,
                                   spawn_flags);
      }
    else
      {
        arguments = g_variant_new ("(^ay^aay@a{uh}@a{ss}u@a{sv})",
                                   opt_directory,
                                   (const char * const *) command_and_args,
                                   fds,
                                   env,
                                   spawn_flags,
                                   opts);
      }

    reply = g_dbus_connection_call_with_unix_fd_list_sync (bus_or_peer_connection,
                                                           api->service_bus_name,
                                                           api->service_obj_path,
                                                           api->service_iface,
                                                           api->launch_method,
                                                           /* sinks floating reference */
                                                           g_steal_pointer (&arguments),
                                                           G_VARIANT_TYPE ("(u)"),
                                                           G_DBUS_CALL_FLAGS_NONE,
                                                           -1,
                                                           fd_list,
                                                           NULL,
                                                           NULL, error);

    if (reply == NULL)
      {
        g_dbus_error_strip_remote_error (local_error);
        goto out;
      }

    g_variant_get (reply, "(u)", &child_pid);
  }

  g_debug ("child_pid: %d", child_pid);

  /* Release our reference to the fds, so that only the copy we sent over
   * D-Bus remains open */
  g_clear_object (&fd_list);

  g_signal_connect (bus_or_peer_connection, "closed",
                    G_CALLBACK (connection_closed_cb), loop);

  g_main_loop_run (loop);

out:
  if (local_error != NULL)
    _srt_log_failure ("%s", local_error->message);

  if (signal_source > 0)
    g_source_remove (signal_source);

  g_strfreev (forward_fds);
  g_free (opt_app_path);
  g_free (opt_directory);
  g_free (opt_socket);
  g_hash_table_unref (opt_env);
  g_hash_table_unref (opt_unsetenv);
  g_free (opt_usr_path);
  global_original_environ = NULL;

  g_debug ("Exiting with status %d", launch_exit_status);
  return launch_exit_status;
}
