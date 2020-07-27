/*
 * pressure-vessel-launch — send IPC requests to create child processes
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
 * Based on flatpak-spawn from the flatpak-xdg-utils package.
 * Copyright © 2018 Red Hat, Inc.
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#include "config.h"
#include "subprojects/libglnx/config.h"

#include <locale.h>
#include <sysexits.h>
#include <sys/signalfd.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "glib-backports.h"
#include "libglnx/libglnx.h"

#include "launcher.h"
#include "utils.h"

static const char *service_bus_name = NULL;
static const char *service_iface = LAUNCHER_IFACE;
static const char *service_obj_path = LAUNCHER_PATH;

typedef GUnixFDList AutoUnixFDList;
G_DEFINE_AUTOPTR_CLEANUP_FUNC(AutoUnixFDList, g_object_unref)

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
  guint32 exit_status = 0;

  if (!g_variant_is_of_type (parameters, G_VARIANT_TYPE ("(uu)")))
    return;

  g_variant_get (parameters, "(uu)", &client_pid, &exit_status);
  g_debug ("child exited %d: %d", client_pid, exit_status);

  if (child_pid == client_pid)
    {
      int exit_code = 0;

      if (WIFEXITED (exit_status))
        {
          exit_code = WEXITSTATUS (exit_status);
        }
      else if (WIFSIGNALED (exit_status))
        {
          /* Smush the signal into an unsigned byte, as the shell does. This is
           * not quite right from the perspective of whatever ran flatpak-spawn
           * — it will get WIFEXITED() not WIFSIGNALED() — but the
           *  alternative is to disconnect all signal() handlers then send this
           *  signal to ourselves and hope it kills us.
           */
          exit_code = 128 + WTERMSIG (exit_status);
        }
      else
        {
          /* wait(3p) claims that if the waitpid() call that returned the exit
           * code specified neither WUNTRACED nor WIFSIGNALED, then exactly one
           * of WIFEXITED() or WIFSIGNALED() will be true.
           */
          g_warning ("exit status %d is neither WIFEXITED() nor WIFSIGNALED()",
                     exit_status);
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
  g_autoptr(GVariant) reply = NULL;
  gboolean to_process_group = FALSE;
  g_autoptr(GError) error = NULL;

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
                                       service_bus_name,    /* NULL if p2p */
                                       service_obj_path,
                                       service_iface,
                                       "SendSignal",
                                       g_variant_new ("(uub)",
                                                      child_pid, sig,
                                                      to_process_group),
                                       G_VARIANT_TYPE ("()"),
                                       G_DBUS_CALL_FLAGS_NONE,
                                       -1, NULL, &error);

  if (error)
    g_debug ("Failed to forward signal: %s", error->message);

  if (sig == SIGSTOP)
    {
      g_debug ("SIGSTOP:ing myself");
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
forward_signals (void)
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

  return g_unix_fd_add (sfd, G_IO_IN, forward_signal_handler, NULL);
}

static void
name_owner_changed (G_GNUC_UNUSED GDBusConnection *connection,
                    G_GNUC_UNUSED const gchar     *sender_name,
                    G_GNUC_UNUSED const gchar     *object_path,
                    G_GNUC_UNUSED const gchar     *interface_name,
                    G_GNUC_UNUSED const gchar     *signal_name,
                    GVariant                      *parameters,
                    gpointer                       user_data)
{
  GMainLoop *loop = user_data;
  const char *name, *from, *to;

  g_variant_get (parameters, "(sss)", &name, &from, &to);

  /* Check if the service dies, then we exit, because we can't track it anymore */
  if (strcmp (name, service_bus_name) == 0 &&
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

static gchar **forward_fds = NULL;
static gboolean opt_clear_env = FALSE;
static gchar *opt_dbus_address = NULL;
static gchar *opt_directory = NULL;
static gchar *opt_socket = NULL;
static gchar **opt_envs = NULL;
static gboolean opt_verbose = FALSE;
static gboolean opt_version = FALSE;

static const GOptionEntry options[] =
{
  { "bus-name", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &service_bus_name,
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
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING_ARRAY, &opt_envs,
    "Set environment variable.", "VAR=VALUE" },
  { "forward-fd", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING_ARRAY, &forward_fds,
    "Connect a file descriptor to the launched process. "
    "fds 0, 1 and 2 are automatically forwarded.",
    "FD" },
  { "socket", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &opt_socket,
    "Connect to a Launcher server listening on this AF_UNIX socket.",
    "ABSPATH|@ABSTRACT" },
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
  g_autoptr(GMainLoop) loop = NULL;
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) local_error = NULL;
  GError **error = &local_error;
  char **command_and_args;
  guint forward_signals_id = 0;
  g_autoptr(FILE) original_stdout = NULL;
  g_autoptr(GDBusConnection) session_bus = NULL;
  g_autoptr(GDBusConnection) peer_connection = NULL;
  g_auto(GVariantBuilder) fd_builder = {};
  g_auto(GVariantBuilder) env_builder = {};
  g_auto(GVariantBuilder) options_builder = {};
  g_autoptr(AutoUnixFDList) fd_list = NULL;
  g_autoptr(GVariant) reply = NULL;
  gint stdin_handle = -1;
  gint stdout_handle = -1;
  gint stderr_handle = -1;
  PvLaunchFlags spawn_flags = 0;
  guint signal_source = 0;
  gsize i;

  setlocale (LC_ALL, "");

  g_set_prgname ("pressure-vessel-launcher");

  g_log_set_handler (G_LOG_DOMAIN,
                     G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE,
                     cli_log_func, (void *) g_get_prgname ());

  context = g_option_context_new ("COMMAND [ARG...]");
  g_option_context_set_summary (context,
                                "Accept IPC requests to create child "
                                "processes.");

  g_option_context_add_main_entries (context, options, NULL);
  opt_verbose = pv_boolean_environment ("PRESSURE_VESSEL_VERBOSE", FALSE);

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
    g_log_set_handler (G_LOG_DOMAIN,
                       G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_INFO,
                       cli_log_func, (void *) g_get_prgname ());

  original_stdout = pv_divert_stdout_to_stderr (error);

  if (original_stdout == NULL)
    {
      launch_exit_status = LAUNCH_EX_FAILED;
      goto out;
    }

  /* We have to block the signals we want to forward before we start any
   * other thread, and in particular the GDBus worker thread, because
   * the signal mask is per-thread. We need all threads to have the same
   * mask, otherwise a thread that doesn't have the mask will receive
   * process-directed signals, causing the whole process to exit. */
  signal_source = forward_signals ();

  if (signal_source == 0)
    {
      launch_exit_status = LAUNCH_EX_FAILED;
      goto out;
    }

  pv_avoid_gvfs ();

  if (service_bus_name != NULL && opt_socket != NULL)
    {
      glnx_throw (error, "--bus-name and --socket cannot both be used");
      goto out;
    }

  if (argc >= 2 && strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  if (argc < 2)
    {
      glnx_throw (error, "Usage: %s [OPTIONS] COMMAND [ARG...]",
                  g_get_prgname ());
      goto out;
    }

  command_and_args = argv + 1;

  launch_exit_status = LAUNCH_EX_FAILED;
  loop = g_main_loop_new (NULL, FALSE);

  if (service_bus_name != NULL)
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

  g_dbus_connection_signal_subscribe (bus_or_peer_connection,
                                      service_bus_name,   /* NULL if p2p */
                                      service_iface,
                                      "ProcessExited",
                                      service_obj_path,
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
      g_variant_builder_add (&fd_builder, "{uh}", fd, handle);
    }

  for (i = 0; opt_envs != NULL && opt_envs[i] != NULL; i++)
    {
      const char *opt_env = opt_envs[i];
      g_auto(GStrv) split = g_strsplit (opt_env, "=", 2);

      if (split == NULL || split[0] == NULL || split[0][0] == 0 || split[1] == NULL)
        {
          glnx_throw (error, "Invalid env format %s", opt_env);
          goto out;
        }
      g_variant_builder_add (&env_builder, "{ss}", split[0], split[1]);
    }

  spawn_flags = 0;

  if (opt_clear_env)
    spawn_flags |= PV_LAUNCH_FLAGS_CLEAR_ENV;

  /* There are no options yet, so just leave this empty */
  g_variant_builder_init (&options_builder, G_VARIANT_TYPE ("a{sv}"));

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

    fds = g_variant_ref_sink (g_variant_builder_end (&fd_builder));
    env = g_variant_ref_sink (g_variant_builder_end (&env_builder));
    opts = g_variant_ref_sink (g_variant_builder_end (&options_builder));

    reply = g_dbus_connection_call_with_unix_fd_list_sync (bus_or_peer_connection,
                                                           service_bus_name,
                                                           service_obj_path,
                                                           service_iface,
                                                           "Launch",
                                                           g_variant_new ("(^ay^aay@a{uh}@a{ss}u@a{sv})",
                                                                          opt_directory,
                                                                          (const char * const *) command_and_args,
                                                                          fds,
                                                                          env,
                                                                          spawn_flags,
                                                                          opts),
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

  g_signal_connect (bus_or_peer_connection, "closed",
                    G_CALLBACK (connection_closed_cb), loop);

  g_main_loop_run (loop);

out:
  if (local_error != NULL)
    g_warning ("%s", local_error->message);

  if (forward_signals_id > 0)
    g_source_remove (forward_signals_id);

  g_strfreev (forward_fds);
  g_free (opt_directory);
  g_free (opt_socket);
  g_strfreev (opt_envs);

  return launch_exit_status;
}
