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

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx/libglnx.h"

#include "bwrap-lock.h"
#include "flatpak-utils-base-private.h"
#include "launcher.h"
#include "utils.h"
#include "wrap-interactive.h"

static const char * const *global_original_environ = NULL;
static GPtrArray *global_locks = NULL;
static GArray *global_pass_fds = NULL;
static gboolean opt_batch = FALSE;
static gboolean opt_create = FALSE;
static gboolean opt_exit_with_parent = FALSE;
static gboolean opt_generate_locales = FALSE;
static PvShell opt_shell = PV_SHELL_NONE;
static gboolean opt_subreaper = FALSE;
static PvTerminal opt_terminal = PV_TERMINAL_AUTO;
static double opt_terminate_idle_timeout = 0.0;
static double opt_terminate_timeout = -1.0;
static gboolean opt_verbose = FALSE;
static gboolean opt_version = FALSE;
static gboolean opt_wait = FALSE;
static gboolean opt_write = FALSE;
static GPid child_pid;

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

  /* The adverb should wait for its child before it exits, but if it
   * gets terminated prematurely, we want the child to terminate too.
   * The child could reset this, but we assume it usually won't.
   * This makes it exit even if we are killed by SIGKILL, unless it
   * takes steps not to be. */
  if (opt_exit_with_parent
      && prctl (PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0) != 0)
    pv_async_signal_safe_error ("Failed to set up parent-death signal\n",
                                LAUNCH_EX_FAILED);

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

  /* Make the fds we pass through *not* be close-on-exec */
  if (data != NULL && data->pass_fds)
    {
      /* Make all other file descriptors close-on-exec */
      flatpak_close_fds_workaround (3);

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
opt_shell_cb (const gchar *option_name,
              const gchar *value,
              gpointer data,
              GError **error)
{
  if (value == NULL || *value == '\0')
    {
      opt_shell = PV_SHELL_NONE;
      return TRUE;
    }

  switch (value[0])
    {
      case 'a':
        if (g_strcmp0 (value, "after") == 0)
          {
            opt_shell = PV_SHELL_AFTER;
            return TRUE;
          }
        break;

      case 'f':
        if (g_strcmp0 (value, "fail") == 0)
          {
            opt_shell = PV_SHELL_FAIL;
            return TRUE;
          }
        break;

      case 'i':
        if (g_strcmp0 (value, "instead") == 0)
          {
            opt_shell = PV_SHELL_INSTEAD;
            return TRUE;
          }
        break;

      case 'n':
        if (g_strcmp0 (value, "none") == 0 || g_strcmp0 (value, "no") == 0)
          {
            opt_shell = PV_SHELL_NONE;
            return TRUE;
          }
        break;

      default:
        /* fall through to error */
        break;
    }

  g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
               "Unknown choice \"%s\" for %s", value, option_name);
  return FALSE;
}

static gboolean
opt_terminal_cb (const gchar *option_name,
                 const gchar *value,
                 gpointer data,
                 GError **error)
{
  if (value == NULL || *value == '\0')
    {
      opt_terminal = PV_TERMINAL_AUTO;
      return TRUE;
    }

  switch (value[0])
    {
      case 'a':
        if (g_strcmp0 (value, "auto") == 0)
          {
            opt_terminal = PV_TERMINAL_AUTO;
            return TRUE;
          }
        break;

      case 'n':
        if (g_strcmp0 (value, "none") == 0 || g_strcmp0 (value, "no") == 0)
          {
            opt_terminal = PV_TERMINAL_NONE;
            return TRUE;
          }
        break;

      case 't':
        if (g_strcmp0 (value, "tty") == 0)
          {
            opt_terminal = PV_TERMINAL_TTY;
            return TRUE;
          }
        break;

      case 'x':
        if (g_strcmp0 (value, "xterm") == 0)
          {
            opt_terminal = PV_TERMINAL_XTERM;
            return TRUE;
          }
        break;

      default:
        /* fall through to error */
        break;
    }

  g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
               "Unknown choice \"%s\" for %s", value, option_name);
  return FALSE;
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
run_helper_sync (const char *cwd,
                 const char * const *argv,
                 const char * const *envp,
                 gchar **child_stdout,
                 gchar **child_stderr,
                 int *wait_status,
                 GError **error)
{
  sigset_t mask;
  sigset_t old_mask;
  gboolean ret;

  g_return_val_if_fail (argv != NULL, FALSE);
  g_return_val_if_fail (argv[0] != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (envp == NULL)
    envp = global_original_environ;

  sigemptyset (&mask);
  sigemptyset (&old_mask);
  sigaddset (&mask, SIGCHLD);

  /* Unblock SIGCHLD in case g_spawn_sync() needs it in some version */
  if (pthread_sigmask (SIG_UNBLOCK, &mask, &old_mask) != 0)
    return glnx_throw_errno_prefix (error, "pthread_sigmask");

  /* We use LEAVE_DESCRIPTORS_OPEN to work around a deadlock in older GLib,
   * and to avoid wasting a lot of time closing fds if the rlimit for
   * maximum open file descriptors is high. Because we're waiting for the
   * subprocess to finish anyway, it doesn't really matter that any fds
   * that are not close-on-execute will get leaked into the child. */
  ret = g_spawn_sync (cwd,
                      (gchar **) argv,
                      (gchar **) envp,
                      G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                      child_setup_cb, NULL,
                      child_stdout,
                      child_stderr,
                      wait_status,
                      error);

  if (pthread_sigmask (SIG_SETMASK, &old_mask, NULL) != 0 && ret)
    return glnx_throw_errno_prefix (error, "pthread_sigmask");

  return ret;
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
  gboolean ret = FALSE;

  g_return_val_if_fail (locpath_out != NULL && *locpath_out == NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  this_path = g_file_read_link ("/proc/self/exe", NULL);
  this_dir = g_path_get_dirname (this_path);
  pvlg = g_build_filename (this_dir, "pressure-vessel-locale-gen", NULL);

  temp_dir = g_dir_make_tmp ("pressure-vessel-locales-XXXXXX", error);

  const char * const locale_gen_argv[] =
  {
    pvlg,
    "--output-dir", temp_dir,
    "--verbose",
    NULL
  };

  if (temp_dir == NULL)
    {
      if (error != NULL)
        glnx_prefix_error (error,
                           "Cannot create temporary directory for locales");
      goto out;
    }

  if (!run_helper_sync (NULL,
                        locale_gen_argv,
                        NULL,
                        &child_stdout,
                        &child_stderr,
                        &wait_status,
                        error))
    {
      if (error != NULL)
        glnx_prefix_error (error, "Cannot run pressure-vessel-locale-gen");
      goto out;
    }

  if (child_stdout != NULL && child_stdout[0] != '\0')
    g_debug ("Output:\n%s", child_stdout);

  if (child_stderr != NULL && child_stderr[0] != '\0')
    g_debug ("Diagnostic output:\n%s", child_stderr);

  if (WIFEXITED (wait_status) && WEXITSTATUS (wait_status) == EX_OSFILE)
    {
      /* locale-gen exits 72 (EX_OSFILE) if it had to correct for
       * missing locales at OS level. This is not an error. */
      g_info ("pressure-vessel-locale-gen created missing locales");
    }
  else if (!g_spawn_check_exit_status (wait_status, error))
    {
      if (error != NULL)
        glnx_prefix_error (error, "Unable to generate locales");
      goto out;
    }
  /* else all locales were already present (exit status 0) */

  dir = g_dir_open (temp_dir, 0, error);

  ret = TRUE;

  if (dir == NULL || g_dir_read_name (dir) == NULL)
    {
      g_info ("No locales have been generated");
      goto out;
    }

  *locpath_out = g_steal_pointer (&temp_dir);

out:
  if (*locpath_out == NULL && temp_dir != NULL)
    _srt_rm_rf (temp_dir);

  return ret;
}

/* Only do async-signal-safe things here: see signal-safety(7) */
static void
terminate_child_cb (int signum)
{
  if (child_pid != 0)
    {
      /* pass it on to the child we're going to wait for */
      kill (child_pid, signum);
    }
  else
    {
      /* guess I'll just die, then */
      signal (signum, SIG_DFL);
      raise (signum);
    }
}

static GOptionEntry options[] =
{
  { "batch", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_batch,
    "Disable all interactivity and redirection: ignore --shell*, "
    "--terminal. [Default: if $PRESSURE_VESSEL_BATCH]", NULL },

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

  { "shell", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_shell_cb,
    "Run an interactive shell: never, after COMMAND, "
    "after COMMAND if it fails, or instead of COMMAND. "
    "[Default: none]",
    "{none|after|fail|instead}" },

  { "subreaper", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_subreaper,
    "Do not exit until all descendant processes have exited.",
    NULL },
  { "no-subreaper", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_subreaper,
    "Only wait for a direct child process [default].",
    NULL },

  { "terminal", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_terminal_cb,
    "none: disable features that would use a terminal; "
    "auto: equivalent to xterm if a --shell option is used, or none; "
    "xterm: put game output (and --shell if used) in an xterm; "
    "tty: put game output (and --shell if used) on Steam's "
    "controlling tty. "
    "[Default: auto]",
    "{none|auto|xterm|tty}" },

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

int
main (int argc,
      char *argv[])
{
  g_auto(GStrv) original_environ = NULL;
  g_autoptr(GPtrArray) locks = NULL;
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) local_error = NULL;
  GError **error = &local_error;
  int ret = EX_USAGE;
  int wait_status = -1;
  g_autofree gchar *locales_temp_dir = NULL;
  g_autoptr(FILE) original_stdout = NULL;
  ChildSetupData child_setup_data = { -1, NULL };
  sigset_t mask;
  struct sigaction terminate_child_action = {};
  g_autoptr(FlatpakBwrap) wrapped_command = NULL;

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

  original_environ = g_get_environ ();
  global_original_environ = (const char * const *) original_environ;

  locks = g_ptr_array_new_with_free_func ((GDestroyNotify) pv_bwrap_lock_free);
  global_locks = locks;

  g_set_prgname ("pressure-vessel-adverb");

  /* Set up the initial base logging */
  pv_set_up_logging (FALSE);

  context = g_option_context_new (
      "COMMAND [ARG...]\n"
      "Run COMMAND [ARG...] with a lock held, a subreaper, or similar.\n");

  g_option_context_add_main_entries (context, options, NULL);

  opt_batch = pv_boolean_environment ("PRESSURE_VESSEL_BATCH", FALSE);
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
    pv_set_up_logging (opt_verbose);

  original_stdout = _srt_divert_stdout_to_stderr (error);

  if (original_stdout == NULL)
    {
      ret = 1;
      goto out;
    }

  _srt_setenv_disable_gio_modules ();

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

  wrapped_command = flatpak_bwrap_new (original_environ);

  if (opt_terminal == PV_TERMINAL_AUTO)
    {
      if (opt_shell != PV_SHELL_NONE)
        opt_terminal = PV_TERMINAL_XTERM;
      else
        opt_terminal = PV_TERMINAL_NONE;
    }

  if (opt_terminal == PV_TERMINAL_NONE && opt_shell != PV_SHELL_NONE)
    {
      g_printerr ("%s: --terminal=none is incompatible with --shell\n",
                  g_get_prgname ());
      goto out;
    }

  if (opt_batch)
    {
      opt_shell = PV_SHELL_NONE;
      opt_terminal = PV_TERMINAL_NONE;
    }

  switch (opt_terminal)
    {
      case PV_TERMINAL_TTY:
        g_debug ("Wrapping command to use tty");

        if (!pv_bwrap_wrap_tty (wrapped_command, error))
          goto out;

        break;

      case PV_TERMINAL_XTERM:
        g_debug ("Wrapping command with xterm");
        pv_bwrap_wrap_in_xterm (wrapped_command);
        break;

      case PV_TERMINAL_AUTO:
          g_warn_if_reached ();
          break;

      case PV_TERMINAL_NONE:
      default:
        /* do nothing */
        break;
    }

  if (opt_shell != PV_SHELL_NONE || opt_terminal == PV_TERMINAL_XTERM)
    {
      /* In the (PV_SHELL_NONE, PV_TERMINAL_XTERM) case, just don't let the
       * xterm close before the user has had a chance to see the output */
      pv_bwrap_wrap_interactive (wrapped_command, opt_shell);
    }

  flatpak_bwrap_append_argsv (wrapped_command, &argv[1], argc - 1);
  flatpak_bwrap_finish (wrapped_command);

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
          g_info ("Generated locales in %s", locales_temp_dir);
          flatpak_bwrap_set_env (wrapped_command, "LOCPATH", locales_temp_dir, TRUE);
        }
      else
        {
          g_info ("No locales were missing");
        }
    }

  /* Respond to common termination signals by killing the child instead of
   * ourselves */
  terminate_child_action.sa_handler = terminate_child_cb;
  sigaction (SIGHUP, &terminate_child_action, NULL);
  sigaction (SIGINT, &terminate_child_action, NULL);
  sigaction (SIGQUIT, &terminate_child_action, NULL);
  sigaction (SIGTERM, &terminate_child_action, NULL);
  sigaction (SIGUSR1, &terminate_child_action, NULL);
  sigaction (SIGUSR2, &terminate_child_action, NULL);

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

  if (opt_verbose)
    {
      gsize i;

      g_message ("Command-line:");

      for (i = 0; i < wrapped_command->argv->len - 1; i++)
        {
          g_autofree gchar *quoted = NULL;

          quoted = g_shell_quote (g_ptr_array_index (wrapped_command->argv, i));
          g_message ("\t%s", quoted);
        }

      g_assert (wrapped_command->argv->pdata[i] == NULL);

      g_message ("Environment:");

      for (i = 0; wrapped_command->envp != NULL && wrapped_command->envp[i] != NULL; i++)
        {
          g_autofree gchar *quoted = NULL;

          quoted = g_shell_quote (wrapped_command->envp[i]);
          g_message ("\t%s", quoted);
        }
    }

  /* We use LEAVE_DESCRIPTORS_OPEN to work around a deadlock in older GLib,
   * see flatpak_close_fds_workaround */
  if (!g_spawn_async (NULL,   /* working directory */
                      (char **) wrapped_command->argv->pdata,
                      wrapped_command->envp,
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

  /* If the parent or child writes to a passed fd and closes it,
   * don't stand in the way of that. Skip fds 0 (stdin),
   * 1 (stdout) and 2 (stderr); we have moved our original stdout
   * to another fd which will be dealt with below, and we want to keep
   * our stdin and stderr open. */
  if (child_setup_data.pass_fds != NULL)
    {
      gsize i;

      for (i = 0; child_setup_data.pass_fds[i] > -1; i++)
        {
          if (child_setup_data.pass_fds[i] > 2)
            close (child_setup_data.pass_fds[i]);
        }
    }

  g_free (child_setup_data.pass_fds);

  /* If the child writes to stdout and closes it, don't interfere */
  g_clear_pointer (&original_stdout, fclose);

  /* Reap child processes until child_pid exits */
  if (!pv_wait_for_child_processes (child_pid, &wait_status, error))
    goto out;

  child_pid = 0;

  if (WIFEXITED (wait_status))
    {
      ret = WEXITSTATUS (wait_status);
      if (ret == 0)
        g_debug ("Command exited with status %d", ret);
      else
        g_info ("Command exited with status %d", ret);
    }
  else if (WIFSIGNALED (wait_status))
    {
      ret = 128 + WTERMSIG (wait_status);
      g_info ("Command killed by signal %d", ret - 128);
    }
  else
    {
      ret = EX_SOFTWARE;
      g_info ("Command terminated in an unknown way (wait status %d)",
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
    _srt_rm_rf (locales_temp_dir);

  if (local_error != NULL)
    g_warning ("%s", local_error->message);

  global_original_environ = NULL;
  g_debug ("Exiting with status %d", ret);
  return ret;
}
