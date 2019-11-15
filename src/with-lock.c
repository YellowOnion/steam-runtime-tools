/* pressure-vessel-with-lock — run a command with a lock held.
 * Basically flock(1), but using fcntl locks compatible with those used
 * by bubblewrap and Flatpak.
 *
 * Copyright © 2019 Collabora Ltd.
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
#include "libglnx.h"

#include "bwrap-lock.h"
#include "utils.h"

#ifndef PR_SET_CHILD_SUBREAPER
#define PR_SET_CHILD_SUBREAPER 36
#endif

static GPtrArray *global_locks = NULL;
static gboolean opt_create = FALSE;
static gboolean opt_subreaper = FALSE;
static gboolean opt_verbose = FALSE;
static gboolean opt_version = FALSE;
static gboolean opt_wait = FALSE;
static gboolean opt_write = FALSE;

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

  g_ptr_array_add (global_locks, pv_bwrap_lock_new_take (fd));
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

  lock = pv_bwrap_lock_new (value, flags, error);

  if (lock == NULL)
    return FALSE;

  g_ptr_array_add (global_locks, lock);
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

  { "subreaper", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_subreaper,
    "Do not exit until all child processes have exited.",
    NULL },
  { "no-subreaper", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_subreaper,
    "Only wait for a direct child process [default].",
    NULL },

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

  setlocale (LC_ALL, "");
  pv_avoid_gvfs ();

  locks = g_ptr_array_new_with_free_func ((GDestroyNotify) pv_bwrap_lock_free);
  global_locks = locks;

  g_set_prgname ("pressure-vessel-with-lock");

  g_log_set_handler (G_LOG_DOMAIN,
                     G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE,
                     cli_log_func, (void *) g_get_prgname ());

  context = g_option_context_new (
      "COMMAND [ARG...]\n"
      "Run COMMAND [ARG...] with a lock held, a subreaper, or similar.\n");

  g_option_context_add_main_entries (context, options, NULL);

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

  if (opt_subreaper
      && prctl (PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0)
    {
      glnx_throw_errno_prefix (error,
                               "Unable to manage background processes");
      goto out;
    }

  g_debug ("Launching child process...");

  if (!g_spawn_async (NULL,   /* working directory */
                      command_and_args,
                      NULL,   /* environment */
                      G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                      NULL, NULL,   /* child setup + user_data */
                      &child_pid,
                      &local_error))
    {
      ret = 127;
      goto out;
    }

  while (1)
    {
      pid_t died = wait (&wait_status);

      if (died < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }
          else if (errno == ECHILD)
            {
              g_debug ("No more child processes, exiting");
              break;
            }
          else
            {
              glnx_throw_errno_prefix (error, "wait");
              goto out;
            }
        }
      else if (died == child_pid)
        {
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
        }
      else
        {
          g_debug ("Indirect child %lld exited with wait status %d",
                   (long long) died, wait_status);
          g_warn_if_fail (opt_subreaper);
        }
    }

out:
  global_locks = NULL;

  if (local_error != NULL)
    g_warning ("%s", local_error->message);

  return ret;
}
