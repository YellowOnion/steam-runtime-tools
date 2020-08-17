/* pressure-vessel-adverb — run a command with an altered execution environment,
 * e.g. holding a lock.
 * The lock is basically flock(1), but using fcntl locks compatible with
 * those used by bubblewrap and Flatpak.
 *
 * Copyright © 2019-2020 Collabora Ltd.
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
 */

#include "config.h"
#include "subprojects/libglnx/config.h"

#include <fcntl.h>
#include <locale.h>
#include <sysexits.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "glib-backports.h"
#include "libglnx/libglnx.h"

#include "bwrap-lock.h"
#include "flatpak-utils-base-private.h"
#include "launcher.h"
#include "utils.h"

static GPtrArray *global_locks = NULL;
static GArray *global_pass_fds = NULL;
static gboolean opt_create = FALSE;
static gboolean opt_exit_with_parent = FALSE;
static gboolean opt_generate_locales = FALSE;
static gboolean opt_subreaper = FALSE;
static double opt_terminate_idle_timeout = 0.0;
static double opt_terminate_timeout = -1.0;
static gboolean opt_verbose = FALSE;
static gboolean opt_version = FALSE;
static gboolean opt_wait = FALSE;
static gboolean opt_write = FALSE;

typedef struct
{
  int original_stdout_fd;
  int *pass_fds;
} ChildSetupData;

static void
child_setup_cb (gpointer user_data)
{
  ChildSetupData *data = user_data;
  sigset_t set;
  const int *iter;
  int i;

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

  /* Put back the original stdout for the child process */
  if (data != NULL &&
      data->original_stdout_fd > 0 &&
      dup2 (data->original_stdout_fd, STDOUT_FILENO) != STDOUT_FILENO)
    pv_async_signal_safe_error ("pressure-vessel-adverb: Unable to reinstate original stdout\n", LAUNCH_EX_FAILED);

  /* Make all other file descriptors close-on-exec */
  flatpak_close_fds_workaround (3);

  /* Make the fds we pass through *not* be close-on-exec */
  if (data != NULL && data->pass_fds)
    {
      for (iter = data->pass_fds; *iter >= 0; iter++)
        {
          int fd = *iter;
          int fd_flags;

          fd_flags = fcntl (fd, F_GETFD);

          if (fd_flags < 0)
            pv_async_signal_safe_error ("pressure-vessel-adverb: Invalid fd?\n",
                                        LAUNCH_EX_FAILED);

          if ((fd_flags & FD_CLOEXEC) != 0
              && fcntl (fd, F_SETFD, fd_flags & ~FD_CLOEXEC) != 0)
            pv_async_signal_safe_error ("pressure-vessel-adverb: Unable to clear close-on-exec\n",
                                        LAUNCH_EX_FAILED);
        }
    }
}

static gboolean
opt_fd_cb (const char *name,
           const char *value,
           gpointer data,
           GError **error)
{
  char *endptr;
  gint64 i64 = g_ascii_strtoll (value, &endptr, 10);
  int fd;
  int fd_flags;

  g_return_val_if_fail (global_locks != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  if (i64 < 0 || i64 > G_MAXINT || endptr == value || *endptr != '\0')
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "Integer out of range or invalid: %s", value);
      return FALSE;
    }

  fd = (int) i64;

  fd_flags = fcntl (fd, F_GETFD);

  if (fd_flags < 0)
    return glnx_throw_errno_prefix (error, "Unable to receive --fd %d", fd);

  if ((fd_flags & FD_CLOEXEC) == 0
      && fcntl (fd, F_SETFD, fd_flags | FD_CLOEXEC) != 0)
    return glnx_throw_errno_prefix (error,
                                    "Unable to configure --fd %d for "
                                    "close-on-exec",
                                    fd);

  /* We don't know whether this is an OFD lock or not. Assume it is:
   * it won't change our behaviour either way, and if it was passed
   * to us across a fork(), it had better be an OFD. */
  g_ptr_array_add (global_locks, pv_bwrap_lock_new_take (fd, TRUE));
  return TRUE;
}

static gboolean
opt_pass_fd_cb (const char *name,
                const char *value,
                gpointer data,
                GError **error)
{
  char *endptr;
  gint64 i64 = g_ascii_strtoll (value, &endptr, 10);
  int fd;
  int fd_flags;

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  if (i64 < 0 || i64 > G_MAXINT || endptr == value || *endptr != '\0')
    {
      g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                   "Integer out of range or invalid: %s", value);
      return FALSE;
    }

  fd = (int) i64;

  fd_flags = fcntl (fd, F_GETFD);

  if (fd_flags < 0)
    return glnx_throw_errno_prefix (error, "Unable to receive --fd %d", fd);

  if (global_pass_fds == NULL)
    global_pass_fds = g_array_new (FALSE, FALSE, sizeof (int));

  g_array_append_val (global_pass_fds, fd);
  return TRUE;
}

static gboolean
opt_lock_file_cb (const char *name,
                  const char *value,
                  gpointer data,
                  GError **error)
{
  PvBwrapLock *lock;
  PvBwrapLockFlags flags = PV_BWRAP_LOCK_FLAGS_NONE;

  g_return_val_if_fail (global_locks != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  if (opt_create)
    flags |= PV_BWRAP_LOCK_FLAGS_CREATE;

  if (opt_write)
    flags |= PV_BWRAP_LOCK_FLAGS_WRITE;

  if (opt_wait)
    flags |= PV_BWRAP_LOCK_FLAGS_WAIT;

  lock = pv_bwrap_lock_new (AT_FDCWD, value, flags, error);

  if (lock == NULL)
    return FALSE;

  g_ptr_array_add (global_locks, lock);
  return TRUE;
}

static gboolean
generate_locales (gchar **locpath_out,
                  GError **error)
{
  g_autofree gchar *temp_dir = NULL;
  g_autoptr(GDir) dir = NULL;
  int wait_status;
  g_autofree gchar *child_stdout = NULL;
  g_autofree gchar *child_stderr = NULL;
  g_autofree gchar *pvlg = NULL;
  g_autofree gchar *this_path = NULL;
  g_autofree gchar *this_dir = NULL;
  gboolean ret;
  sigset_t mask;
  sigset_t old_mask;

  g_return_val_if_fail (locpath_out == NULL || *locpath_out == NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  sigemptyset (&mask);
  sigemptyset (&old_mask);
  sigaddset (&mask, SIGCHLD);

  temp_dir = g_dir_make_tmp ("pressure-vessel-locales-XXXXXX", error);

  if (temp_dir == NULL)
    {
      if (error != NULL)
        glnx_prefix_error (error,
                           "Cannot create temporary directory for locales");
      return FALSE;
    }

  this_path = g_file_read_link ("/proc/self/exe", NULL);
  this_dir = g_path_get_dirname (this_path);
  pvlg = g_build_filename (this_dir, "pressure-vessel-locale-gen", NULL);

  const char *locale_gen_argv[] =
  {
    pvlg,
    "--output-dir", temp_dir,
    "--verbose",
    NULL
  };

  /* Unblock SIGCHLD in case g_spawn_sync() needs it in some version */
  if (pthread_sigmask (SIG_UNBLOCK, &mask, &old_mask) != 0)
    return glnx_throw_errno_prefix (error, "pthread_sigmask");

  /* We use LEAVE_DESCRIPTORS_OPEN to work around a deadlock in older GLib,
   * see flatpak_close_fds_workaround */
  ret = g_spawn_sync (NULL,  /* cwd */
                      (gchar **) locale_gen_argv,
                      NULL,  /* environ */
                      G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                      child_setup_cb, NULL,
                      &child_stdout,
                      &child_stderr,
                      &wait_status,
                      error);

  if (pthread_sigmask (SIG_SETMASK, &old_mask, NULL) != 0)
    return glnx_throw_errno_prefix (error, "pthread_sigmask");

  if (!ret)
    {
      if (error != NULL)
        glnx_prefix_error (error, "Cannot run pressure-vessel-locale-gen");
      return FALSE;
    }

  if (child_stdout != NULL && child_stdout[0] != '\0')
    g_debug ("Output:\n%s", child_stdout);

  if (child_stderr != NULL && child_stderr[0] != '\0')
    g_debug ("Diagnostic output:\n%s", child_stderr);

  if (WIFEXITED (wait_status) && WEXITSTATUS (wait_status) == EX_OSFILE)
    {
      /* locale-gen exits 72 (EX_OSFILE) if it had to correct for
       * missing locales at OS level. This is not an error. */
      g_debug ("pressure-vessel-locale-gen created missing locales");
    }
  else if (!g_spawn_check_exit_status (wait_status, error))
    {
      if (error != NULL)
        glnx_prefix_error (error, "Unable to generate locales");
      return FALSE;
    }
  /* else all locales were already present (exit status 0) */

  dir = g_dir_open (temp_dir, 0, error);
  
  if (dir == NULL || g_dir_read_name (dir) == NULL)
    {
      g_debug ("No locales have been generated");
      return TRUE;
    }

  if (locpath_out != NULL)
    *locpath_out = g_steal_pointer (&temp_dir);

  return TRUE;
}

static GOptionEntry options[] =
{
  { "fd", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_fd_cb,
    "Take a file descriptor, already locked if desired, and keep it "
    "open. May be repeated.",
    NULL },

  { "create", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_create,
    "Create each subsequent lock file if it doesn't exist.",
    NULL },
  { "no-create", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_create,
    "Don't create subsequent nonexistent lock files [default].",
    NULL },

  { "exit-with-parent", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_exit_with_parent,
    "Terminate child process and self with SIGTERM when parent process "
    "exits.",
    NULL },
  { "no-exit-with-parent", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_exit_with_parent,
    "Don't do anything special when parent process exits [default].",
    NULL },

  { "generate-locales", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_generate_locales,
    "Attempt to generate any missing locales.", NULL },
  { "no-generate-locales", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_generate_locales,
    "Don't generate any missing locales [default].", NULL },

  { "write", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_write,
    "Lock each subsequent lock file for write access.",
    NULL },
  { "no-write", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_write,
    "Lock each subsequent lock file for read-only access [default].",
    NULL },

  { "wait", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_wait,
    "Wait for each subsequent lock file.",
    NULL },
  { "no-wait", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_wait,
    "Exit unsuccessfully if a lock-file is busy [default].",
    NULL },

  { "lock-file", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_lock_file_cb,
    "Open the given file and lock it, affected by options appearing "
    "earlier on the command-line. May be repeated.",
    NULL },

  { "pass-fd", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_pass_fd_cb,
    "Let the launched process inherit the given fd.",
    NULL },

  { "subreaper", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_subreaper,
    "Do not exit until all descendant processes have exited.",
    NULL },
  { "no-subreaper", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_subreaper,
    "Only wait for a direct child process [default].",
    NULL },

  { "terminate-idle-timeout", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_DOUBLE, &opt_terminate_idle_timeout,
    "If --terminate-timeout is used, wait this many seconds before "
    "sending SIGTERM. [default: 0.0]",
    "SECONDS" },
  { "terminate-timeout", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_DOUBLE, &opt_terminate_timeout,
    "Send SIGTERM and SIGCONT to descendant processes that didn't "
    "exit within --terminate-idle-timeout. If they don't all exit within "
    "this many seconds, send SIGKILL and SIGCONT to survivors. If 0.0, "
    "skip SIGTERM and use SIGKILL immediately. Implies --subreaper. "
    "[Default: -1.0, meaning don't signal].",
    "SECONDS" },

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
  g_autoptr(GPtrArray) locks = NULL;
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) local_error = NULL;
  GError **error = &local_error;
  int ret = EX_USAGE;
  char **command_and_args;
  GPid child_pid;
  int wait_status = -1;
  g_autofree gchar *locales_temp_dir = NULL;
  g_auto(GStrv) my_environ = NULL;
  g_autoptr(FILE) original_stdout = NULL;
  ChildSetupData child_setup_data = { -1, NULL };
  sigset_t mask;

  sigemptyset (&mask);
  sigaddset (&mask, SIGCHLD);

  /* Must be called before we start any threads */
  if (pthread_sigmask (SIG_BLOCK, &mask, NULL) != 0)
    {
      ret = EX_UNAVAILABLE;
      glnx_throw_errno_prefix (error, "pthread_sigmask");
      goto out;
    }


  setlocale (LC_ALL, "");

  locks = g_ptr_array_new_with_free_func ((GDestroyNotify) pv_bwrap_lock_free);
  global_locks = locks;

  g_set_prgname ("pressure-vessel-adverb");

  g_log_set_handler (G_LOG_DOMAIN,
                     G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE,
                     cli_log_func, (void *) g_get_prgname ());

  context = g_option_context_new (
      "COMMAND [ARG...]\n"
      "Run COMMAND [ARG...] with a lock held, a subreaper, or similar.\n");

  g_option_context_add_main_entries (context, options, NULL);

  opt_verbose = pv_boolean_environment ("PRESSURE_VESSEL_VERBOSE", FALSE);

  if (!g_option_context_parse (context, &argc, &argv, error))
    {
      if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_BUSY))
        ret = EX_TEMPFAIL;
      else if (local_error->domain == G_OPTION_ERROR)
        ret = EX_USAGE;
      else
        ret = EX_UNAVAILABLE;

      goto out;
    }

  if (opt_version)
    {
      g_print ("%s:\n"
               " Package: pressure-vessel\n"
               " Version: %s\n",
               argv[0], VERSION);
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
      ret = 1;
      goto out;
    }

  pv_avoid_gvfs ();

  if (argc >= 2 && strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  if (argc < 2)
    {
      g_printerr ("%s: Usage: %s [OPTIONS] COMMAND [ARG...]\n",
                  g_get_prgname (),
                  g_get_prgname ());
      goto out;
    }

  ret = EX_UNAVAILABLE;

  command_and_args = argv + 1;

  if (opt_exit_with_parent)
    {
      g_debug ("Setting up to exit when parent does");

      if (prctl (PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0) != 0)
        {
          glnx_throw_errno_prefix (error,
                                   "Unable to set parent death signal");
          goto out;
        }
    }

  if ((opt_subreaper || opt_terminate_timeout >= 0)
      && prctl (PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0)
    {
      glnx_throw_errno_prefix (error,
                               "Unable to manage background processes");
      goto out;
    }

  my_environ = g_get_environ ();

  if (opt_generate_locales)
    {
      g_debug ("Making sure locales are available");

      /* If this fails, it is not fatal - carry on anyway */
      if (!generate_locales (&locales_temp_dir, error))
        {
          g_warning ("%s", local_error->message);
          g_clear_error (error);
        }
      else if (locales_temp_dir != NULL)
        {
          g_debug ("Generated locales in %s", locales_temp_dir);
          my_environ = g_environ_setenv (my_environ, "LOCPATH", locales_temp_dir, TRUE);
        }
      else
        {
          g_debug ("No locales were missing");
        }
    }

  g_debug ("Launching child process...");
  fflush (stdout);
  child_setup_data.original_stdout_fd = fileno (original_stdout);

  if (global_pass_fds != NULL)
    {
      int terminator = -1;

      g_array_append_val (global_pass_fds, terminator);
      child_setup_data.pass_fds = (int *) g_array_free (g_steal_pointer (&global_pass_fds),
                                                        FALSE);
    }

  /* We use LEAVE_DESCRIPTORS_OPEN to work around a deadlock in older GLib,
   * see flatpak_close_fds_workaround */
  if (!g_spawn_async (NULL,   /* working directory */
                      command_and_args,
                      my_environ,
                      (G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD |
                       G_SPAWN_LEAVE_DESCRIPTORS_OPEN |
                       G_SPAWN_CHILD_INHERITS_STDIN),
                      child_setup_cb, &child_setup_data,
                      &child_pid,
                      &local_error))
    {
      ret = 127;
      goto out;
    }

  g_free (child_setup_data.pass_fds);

  /* If the child writes to stdout and closes it, don't interfere */
  g_clear_pointer (&original_stdout, fclose);

  /* Reap child processes until child_pid exits */
  if (!pv_wait_for_child_processes (child_pid, &wait_status, error))
    goto out;

  if (WIFEXITED (wait_status))
    {
      ret = WEXITSTATUS (wait_status);
      g_debug ("Command exited with status %d", ret);
    }
  else if (WIFSIGNALED (wait_status))
    {
      ret = 128 + WTERMSIG (wait_status);
      g_debug ("Command killed by signal %d", ret - 128);
    }
  else
    {
      ret = EX_SOFTWARE;
      g_debug ("Command terminated in an unknown way (wait status %d)",
               wait_status);
    }

  if (opt_terminate_idle_timeout < 0.0)
    opt_terminate_idle_timeout = 0.0;

  /* Wait for the other child processes, if any, possibly killing them */
  if (opt_terminate_timeout >= 0.0)
    {
      if (!pv_terminate_all_child_processes (opt_terminate_idle_timeout * G_TIME_SPAN_SECOND,
                                             opt_terminate_timeout * G_TIME_SPAN_SECOND,
                                             error))
        goto out;
    }
  else
    {
      if (!pv_wait_for_child_processes (0, NULL, error))
        goto out;
    }

out:
  global_locks = NULL;
  g_clear_pointer (&global_pass_fds, g_array_unref);

  if (locales_temp_dir != NULL)
    pv_rm_rf (locales_temp_dir);

  if (local_error != NULL)
    g_warning ("%s", local_error->message);

  return ret;
}
