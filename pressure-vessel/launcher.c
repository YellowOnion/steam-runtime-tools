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

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <sysexits.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <sys/socket.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx/libglnx.h"

#include "flatpak-utils-base-private.h"
#include "launcher.h"
#include "portal-listener.h"
#include "utils.h"

static PvPortalListener *global_listener;

static GHashTable *client_pid_data_hash = NULL;
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
  pv_portal_listener_release_name (global_listener);

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
  gboolean terminate_after;
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
  gboolean terminate_after = pid_data->terminate_after;

  g_debug ("Child %d died: wait status %d", pid_data->pid, status);

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

  if (terminate_after)
    {
      g_debug ("Main pid %d died, terminating...", pid);
      terminate_children (SIGTERM);
      unref_skeleton_in_timeout ();
    }
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
    _srt_async_signal_safe_error ("Failed to unblock signals when starting child\n",
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
  g_auto(GStrv) unset_env = NULL;
  gint32 max_fd;
  gboolean terminate_after = FALSE;

  if (fd_list != NULL)
    fds = g_unix_fd_list_peek_fds (fd_list, &fds_len);

  if (*arg_cwd_path == 0)
    arg_cwd_path = NULL;

  if (arg_argv == NULL || *arg_argv == NULL)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR,
                                             G_DBUS_ERROR_INVALID_ARGS,
                                             "No command given");
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  if ((arg_flags & ~PV_LAUNCH_FLAGS_MASK) != 0)
    {
      g_dbus_method_invocation_return_error (invocation, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                                             "Unsupported flags enabled: 0x%x", arg_flags & ~PV_LAUNCH_FLAGS_MASK);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  g_variant_lookup (arg_options, "terminate-after", "b", &terminate_after);

  g_info ("Running spawn command %s", arg_argv[0]);

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
      env = g_strdupv (global_listener->original_environ);
    }

  n_envs = g_variant_n_children (arg_envs);
  for (i = 0; i < n_envs; i++)
    {
      const char *var = NULL;
      const char *val = NULL;
      g_variant_get_child (arg_envs, i, "{&s&s}", &var, &val);

      /* Ignore PWD: we special-case that later */
      if (g_strcmp0 (var, "PWD") == 0)
        continue;

      env = g_environ_setenv (env, var, val, TRUE);
    }

  g_variant_lookup (arg_options, "unset-env", "^as", &unset_env);

  for (i = 0; unset_env != NULL && unset_env[i] != NULL; i++)
    {
      /* Again ignore PWD */
      if (g_strcmp0 (unset_env[i], "PWD") == 0)
        continue;

      g_debug ("Unsetting the environment variable %s...", unset_env[i]);
      env = g_environ_unsetenv (env, unset_env[i]);
    }

  if (arg_cwd_path == NULL)
    env = g_environ_setenv (env, "PWD", global_listener->original_cwd_l,
                            TRUE);
  else
    env = g_environ_setenv (env, "PWD", arg_cwd_path, TRUE);

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
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  pid_data = g_new0 (PidData, 1);
  pid_data->connection = g_object_ref (g_dbus_method_invocation_get_connection (invocation));
  pid_data->pid = pid;
  pid_data->client = g_strdup (g_dbus_method_invocation_get_sender (invocation));
  pid_data->terminate_after = terminate_after;
  pid_data->child_watch = g_child_watch_add_full (G_PRIORITY_DEFAULT,
                                                  pid,
                                                  child_watch_died,
                                                  pid_data,
                                                  NULL);

  g_debug ("Client Pid is %d", pid_data->pid);

  g_hash_table_replace (client_pid_data_hash, GUINT_TO_POINTER (pid_data->pid),
                        pid_data);

  pv_launcher1_complete_launch (object, invocation, NULL, pid);
  return G_DBUS_METHOD_INVOCATION_HANDLED;
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
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  g_debug ("Sending signal %d to client pid %d", arg_signal, arg_pid);

  if (arg_to_process_group)
    killpg (pid_data->pid, arg_signal);
  else
    kill (pid_data->pid, arg_signal);

  pv_launcher1_complete_send_signal (launcher, invocation);

  return G_DBUS_METHOD_INVOCATION_HANDLED;
}

static gboolean
handle_terminate (PvLauncher1           *object,
                  GDBusMethodInvocation *invocation)
{
  terminate_children (SIGTERM);
  pv_launcher1_complete_terminate (object, invocation);
  unref_skeleton_in_timeout ();
  return G_DBUS_METHOD_INVOCATION_HANDLED;
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
on_bus_acquired (PvPortalListener *listener,
                 GDBusConnection *connection,
                 gpointer user_data)
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
      _srt_log_failure ("Unable to export object: %s", error->message);
      *ret = EX_SOFTWARE;
      g_main_loop_quit (main_loop);
    }
}

static void
on_name_acquired (PvPortalListener *listener,
                  GDBusConnection *connection,
                  const gchar *name,
                  gpointer user_data)
{
  int *ret = user_data;

  g_debug ("Name acquired");

  /* If exporting the launcher didn't fail, then we are now happy */
  if (*ret == EX_UNAVAILABLE)
    {
      *ret = 0;
      pv_portal_listener_close_info_fh (global_listener, name);
    }
}

static void
on_name_lost (PvPortalListener *listener,
              GDBusConnection *connection,
              const gchar *name,
              gpointer user_data)
{
  g_debug ("Name lost");
  unref_skeleton_in_timeout ();
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

static gboolean
new_connection_cb (PvPortalListener *listener,
                   GDBusConnection *connection,
                   gpointer user_data)
{
  GError *error = NULL;

  /* Paired with g_object_unref() in peer_connection_closed_cb() */
  g_object_ref (connection);
  g_signal_connect (connection, "closed",
                    G_CALLBACK (peer_connection_closed_cb), NULL);

  if (!export_launcher (connection, &error))
    {
      g_warning ("Unable to export object: %s", error->message);
      g_dbus_connection_close (connection, NULL, NULL, NULL);
      return G_DBUS_METHOD_INVOCATION_HANDLED;
    }

  return G_DBUS_METHOD_INVOCATION_HANDLED;
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
      _srt_log_failure ("Unable to watch signals: %s", g_strerror (errno));
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
static gint opt_info_fd = -1;
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
  { "info-fd", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_INT, &opt_info_fd,
    "Indicate readiness and print details of how to connect on this "
    "file descriptor instead of stdout.",
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

int
main (int argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = NULL;
  guint signals_id = 0;
  guint exit_on_readable_id = 0;
  g_autoptr(GError) local_error = NULL;
  GError **error = &local_error;
  int ret = EX_USAGE;
  GBusNameOwnerFlags flags;
  int result;

  global_listener = pv_portal_listener_new ();

  setlocale (LC_ALL, "");

  g_set_prgname ("pressure-vessel-launcher");

  /* Set up the initial base logging */
  _srt_util_set_glib_log_handler (FALSE);

  context = g_option_context_new ("");
  g_option_context_set_summary (context,
                                "Accept IPC requests to create child "
                                "processes.");

  g_option_context_add_main_entries (context, options, NULL);
  opt_verbose = _srt_boolean_environment ("PRESSURE_VESSEL_VERBOSE", FALSE);

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
    _srt_util_set_glib_log_handler (opt_verbose);

  if ((result = _srt_set_compatible_resource_limits (0)) < 0)
    g_warning ("Unable to set normal resource limits: %s",
               g_strerror (-result));

  if (!pv_portal_listener_set_up_info_fd (global_listener,
                                          opt_info_fd,
                                          error))
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

  _srt_setenv_disable_gio_modules ();

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

  if (!pv_portal_listener_check_socket_arguments (global_listener,
                                                  opt_bus_name,
                                                  opt_socket,
                                                  opt_socket_directory,
                                                  error))
    goto out;

  /* Exit with this status until we know otherwise */
  ret = EX_SOFTWARE;

  g_signal_connect (global_listener,
                    "new-peer-connection", G_CALLBACK (new_connection_cb),
                    NULL);
  g_signal_connect (global_listener,
                    "session-bus-connected", G_CALLBACK (on_bus_acquired),
                    &ret);
  g_signal_connect (global_listener,
                    "session-bus-name-acquired", G_CALLBACK (on_name_acquired),
                    &ret);
  g_signal_connect (global_listener,
                    "session-bus-name-lost", G_CALLBACK (on_name_lost),
                    NULL);

  flags = G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT;

  if (opt_replace)
    flags |= G_BUS_NAME_OWNER_FLAGS_REPLACE;

  if (!pv_portal_listener_listen (global_listener,
                                  opt_bus_name,
                                  flags,
                                  opt_socket,
                                  opt_socket_directory,
                                  error))
    goto out;

  /* If we're using the bus name method, we can't exit successfully
   * until we claimed the bus name at least once. Otherwise we're
   * already content. */
  if (opt_bus_name != NULL)
    ret = EX_UNAVAILABLE;
  else
    ret = 0;

  g_debug ("Entering main loop");

  main_loop = g_main_loop_new (NULL, FALSE);
  g_main_loop_run (main_loop);

out:
  if (local_error != NULL)
    _srt_log_failure ("%s", local_error->message);

  if (exit_on_readable_id > 0)
    g_source_remove (exit_on_readable_id);

  if (signals_id > 0)
    g_source_remove (signals_id);

  g_free (opt_bus_name);
  g_free (opt_socket);
  g_free (opt_socket_directory);
  g_clear_object (&global_listener);

  if (local_error == NULL)
    ret = 0;
  else if (local_error->domain == G_OPTION_ERROR)
    ret = EX_USAGE;
  else
    ret = EX_UNAVAILABLE;

  g_clear_error (&local_error);

  g_debug ("Exiting with status %d", ret);
  return ret;
}
