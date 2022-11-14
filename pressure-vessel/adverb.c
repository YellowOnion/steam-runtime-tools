/* pressure-vessel-adverb — run a command with an altered execution environment,
 * e.g. holding a lock.
 * The lock is basically flock(1), but using fcntl locks compatible with
 * those used by bubblewrap and Flatpak.
 *
 * Copyright © 2019-2021 Collabora Ltd.
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
#include "steam-runtime-tools/launcher-internal.h"
#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/profiling-internal.h"
#include "steam-runtime-tools/steam-runtime-tools.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

#include "bwrap-lock.h"
#include "flatpak-utils-base-private.h"
#include "per-arch-dirs.h"
#include "supported-architectures.h"
#include "utils.h"
#include "wrap-interactive.h"

static const char * const *global_original_environ = NULL;
static GPtrArray *global_locks = NULL;
static GPtrArray *global_ld_so_conf_entries = NULL;
static GArray *global_pass_fds = NULL;
static gboolean opt_batch = FALSE;
static gboolean opt_create = FALSE;
static gboolean opt_exit_with_parent = FALSE;
static gboolean opt_generate_locales = FALSE;
static gchar *opt_regenerate_ld_so_cache = NULL;
static gchar *opt_set_ld_library_path = NULL;
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

typedef enum
{
  PRELOAD_VARIABLE_INDEX_LD_AUDIT,
  PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
} PreloadVariableIndex;

/* Indexed by PreloadVariableIndex */
static const char *preload_variables[] =
{
  "LD_AUDIT",
  "LD_PRELOAD",
};

typedef struct
{
  char *name;
  gsize index_in_preload_variables;
  /* An index in pv_multiarch_details, or G_MAXSIZE if unspecified */
  gsize abi_index;
} AdverbPreloadModule;

static void
adverb_preload_module_clear (gpointer p)
{
  AdverbPreloadModule *self = p;

  g_free (self->name);
}

static GArray *opt_preload_modules = NULL;

static gpointer
generic_strdup (gpointer p)
{
  return g_strdup (p);
}

static void
ptr_array_add_unique (GPtrArray *arr,
                      const void *item,
                      GEqualFunc equal_func,
                      GBoxedCopyFunc copy_func)
{
  if (!g_ptr_array_find_with_equal_func (arr, item, equal_func, NULL))
    g_ptr_array_add (arr, copy_func ((gpointer) item));
}

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
    _srt_async_signal_safe_error ("Failed to set up parent-death signal\n",
                                  LAUNCH_EX_FAILED);

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

  /* Put back the original stdout for the child process */
  if (data != NULL &&
      data->original_stdout_fd > 0 &&
      dup2 (data->original_stdout_fd, STDOUT_FILENO) != STDOUT_FILENO)
    _srt_async_signal_safe_error ("pressure-vessel-adverb: Unable to reinstate original stdout\n", LAUNCH_EX_FAILED);

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
            _srt_async_signal_safe_error ("pressure-vessel-adverb: Invalid fd?\n",
                                          LAUNCH_EX_FAILED);

          if ((fd_flags & FD_CLOEXEC) != 0
              && fcntl (fd, F_SETFD, fd_flags & ~FD_CLOEXEC) != 0)
            _srt_async_signal_safe_error ("pressure-vessel-adverb: Unable to clear close-on-exec\n",
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
opt_add_ld_so_cb (const char *name,
                  const char *value,
                  gpointer data,
                  GError **error)
{
  g_return_val_if_fail (global_ld_so_conf_entries != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  g_ptr_array_add (global_ld_so_conf_entries, g_strdup (value));
  return TRUE;
}

static gboolean
opt_ld_something (const char *option,
                  gsize index_in_preload_variables,
                  const char *value,
                  gpointer data,
                  GError **error)
{
  AdverbPreloadModule module = { NULL, 0, G_MAXSIZE };
  g_auto(GStrv) parts = NULL;
  const char *architecture = NULL;

  parts = g_strsplit (value, ":", 0);

  if (parts[0] != NULL)
    {
      gsize i;

      for (i = 1; parts[i] != NULL; i++)
        {
          if (g_str_has_prefix (parts[i], "abi="))
            {
              gsize abi;

              architecture = parts[i] + strlen ("abi=");

              for (abi = 0; abi < PV_N_SUPPORTED_ARCHITECTURES; abi++)
                {
                  if (strcmp (architecture, pv_multiarch_details[abi].tuple) == 0)
                    {
                      module.abi_index = abi;
                      break;
                    }
                }

              if (module.abi_index == G_MAXSIZE)
                {
                  g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                               "Unsupported ABI %s",
                               architecture);
                  return FALSE;
                }
            }
          else
            {
              g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                           "Unexpected option in %s=\"%s\": %s",
                           option, value, parts[i]);
              return FALSE;
            }
        }

      value = parts[0];
    }

  if (opt_preload_modules == NULL)
    {
      opt_preload_modules = g_array_new (FALSE, FALSE, sizeof (AdverbPreloadModule));
      g_array_set_clear_func (opt_preload_modules, adverb_preload_module_clear);
    }

  module.index_in_preload_variables = index_in_preload_variables;
  module.name = g_strdup (value);
  g_array_append_val (opt_preload_modules, module);
  return TRUE;
}

static gboolean
opt_ld_audit_cb (const gchar *option_name,
                 const gchar *value,
                 gpointer data,
                 GError **error)
{
  return opt_ld_something (option_name, PRELOAD_VARIABLE_INDEX_LD_AUDIT,
                           value, data, error);
}

static gboolean
opt_ld_preload_cb (const gchar *option_name,
                   const gchar *value,
                   gpointer data,
                   GError **error)
{
  return opt_ld_something (option_name, PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
                           value, data, error);
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
regenerate_ld_so_cache (const GPtrArray *ld_so_cache_paths,
                        const char *dir,
                        GError **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GPtrArray) argv = g_ptr_array_new ();
  g_autoptr(GString) conf = g_string_new ("");
  g_autofree gchar *child_stdout = NULL;
  g_autofree gchar *child_stderr = NULL;
  g_autofree gchar *conf_path = g_build_filename (dir, "ld.so.conf", NULL);
  g_autofree gchar *rt_conf_path = g_build_filename (dir, "runtime-ld.so.conf", NULL);
  g_autofree gchar *replace_path = g_build_filename (dir, "ld.so.cache", NULL);
  g_autofree gchar *new_path = g_build_filename (dir, "new-ld.so.cache", NULL);
  g_autofree gchar *contents = NULL;
  int wait_status;
  gsize i;

  for (i = 0; ld_so_cache_paths != NULL && i < ld_so_cache_paths->len; i++)
    {
      const gchar *value = g_ptr_array_index (ld_so_cache_paths, i);
      if (strchr (value, '\n') != NULL
          || strchr (value, '\t') != NULL
          || value[0] != '/')
        return glnx_throw (error,
                           "Cannot include path entry \"%s\" in ld.so.conf",
                           value);

      g_debug ("%s: Adding \"%s\" to beginning of ld.so.conf",
               G_STRFUNC, value);
      g_string_append (conf, value);
      g_string_append_c (conf, '\n');
    }

  /* Ignore read error, if any */
  if (g_file_get_contents (rt_conf_path, &contents, NULL, NULL))
    {
      g_debug ("%s: Appending runtime's ld.so.conf:\n%s", G_STRFUNC, contents);
      g_string_append (conf, contents);
    }

  /* This atomically replaces conf_path, so we don't need to do the
   * atomic bit ourselves */
  if (!g_file_set_contents (conf_path, conf->str, -1, error))
    return FALSE;

  while (TRUE)
    {
      char *newline = strchr (conf->str, '\n');

      if (newline != NULL)
        *newline = '\0';

      g_debug ("%s: final ld.so.conf: %s", G_STRFUNC, conf->str);

      if (newline != NULL)
        g_string_erase (conf, 0, newline + 1 - conf->str);
      else
        break;
    }

  /* Items in this GPtrArray are borrowed, not copied.
   *
   * /sbin/ldconfig might be a symlink into /run/host, or it might
   * be from the runtime, depending which version of glibc we're
   * using.
   *
   * ldconfig overwrites the file in-place rather than atomically,
   * so we write to a new filename, and do the atomic-overwrite
   * ourselves if ldconfig succeeds. */
  g_ptr_array_add (argv, (char *) "/sbin/ldconfig");
  g_ptr_array_add (argv, (char *) "-f");    /* Path to ld.so.conf */
  g_ptr_array_add (argv, conf_path);
  g_ptr_array_add (argv, (char *) "-C");    /* Path to new cache */
  g_ptr_array_add (argv, new_path);
  g_ptr_array_add (argv, (char *) "-X");    /* Don't update symlinks */

  if (opt_verbose)
    g_ptr_array_add (argv, (char *) "-v");

  g_ptr_array_add (argv, NULL);

  if (!run_helper_sync (dir,
                        (const char * const *) argv->pdata,
                        global_original_environ,
                        &child_stdout,
                        &child_stderr,
                        &wait_status,
                        error))
    return glnx_prefix_error (error, "Cannot run /sbin/ldconfig");

  if (!g_spawn_check_wait_status (wait_status, &local_error))
    {
      if (child_stderr != NULL && child_stderr[0] != '\0')
        {
          g_set_error (error, local_error->domain, local_error->code,
                       ("Unable to generate %s: %s.\n"
                        "Diagnostic output:\n%s"),
                       new_path,
                       local_error->message,
                       child_stderr);
        }
      else
        {
          g_set_error (error, local_error->domain, local_error->code,
                       "Unable to generate %s: %s",
                       new_path,
                       local_error->message);
        }

      return FALSE;
    }

  if (child_stdout != NULL && child_stdout[0] != '\0')
    g_debug ("Output:\n%s", child_stdout);

  if (child_stderr != NULL && child_stderr[0] != '\0')
    g_debug ("Diagnostic output:\n%s", child_stderr);

  /* Atomically replace ld.so.cache with new-ld.so.cache. */
  if (!glnx_renameat (AT_FDCWD, new_path, AT_FDCWD, replace_path, error))
    return glnx_prefix_error (error, "Cannot move %s to %s",
                              new_path, replace_path);

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
  g_autofree gchar *this_dir = NULL;
  gboolean ret = FALSE;
  const char *locale_gen_argv[] =
  {
    NULL,   /* placeholder for /path/to/pressure-vessel-locale-gen */
    "--output-dir", NULL,
    "--verbose",
    NULL
  };

  g_return_val_if_fail (locpath_out != NULL && *locpath_out == NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  this_dir = _srt_find_executable_dir (error);

  if (this_dir == NULL)
    goto out;

  pvlg = g_build_filename (this_dir, "pressure-vessel-locale-gen", NULL);
  locale_gen_argv[0] = pvlg;

  temp_dir = g_dir_make_tmp ("pressure-vessel-locales-XXXXXX", error);
  locale_gen_argv[2] = temp_dir;

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
       * missing locales at OS level. This is not an error, but deserves
       * a warning, since it costs around 10 seconds even on a fast SSD. */
      g_printerr ("%s", child_stderr);
      g_warning ("Container startup will be faster if missing locales are created at OS level");
    }
  else if (!g_spawn_check_wait_status (wait_status, error))
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

static void
append_to_search_path (const gchar *item, GString *search_path)
{
  pv_search_path_append (search_path, item);
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

  { "regenerate-ld.so-cache", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_regenerate_ld_so_cache,
    "Regenerate ld.so.cache in the given directory, incorporating "
    "the paths from \"add-ld.so-path\", if any. An empty argument results in "
    "not doing this [default].",
    "PATH" },
  { "add-ld.so-path", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_add_ld_so_cb,
    "While regenerating the ld.so.cache, include PATH as an additional "
    "ld.so.conf.d entry. May be repeated.",
    "PATH" },
  { "set-ld-library-path", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_set_ld_library_path,
    "Set the environment variable LD_LIBRARY_PATH to VALUE before "
    "executing COMMAND.",
    "VALUE" },

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

  { "ld-audit", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, &opt_ld_audit_cb,
    "Add MODULE to LD_AUDIT before executing COMMAND.",
    "MODULE" },
  { "ld-preload", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, &opt_ld_preload_cb,
    "Add MODULE to LD_PRELOAD before executing COMMAND. Some adjustments "
    "may be performed, e.g. joining together multiple gameoverlayrenderer.so "
    "preloads into a single path by leveraging the dynamic linker token expansion",
    "MODULE" },

  { "lock-file", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, opt_lock_file_cb,
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
  g_autoptr(GPtrArray) ld_so_conf_entries = NULL;
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
  g_autoptr(PvPerArchDirs) lib_temp_dirs = NULL;
  gsize i;

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

  ld_so_conf_entries = g_ptr_array_new_with_free_func (g_free);
  global_ld_so_conf_entries = ld_so_conf_entries;

  locks = g_ptr_array_new_with_free_func ((GDestroyNotify) pv_bwrap_lock_free);
  global_locks = locks;

  g_set_prgname ("pressure-vessel-adverb");

  /* Set up the initial base logging */
  _srt_util_set_glib_log_handler (G_LOG_DOMAIN, SRT_LOG_FLAGS_NONE);

  context = g_option_context_new (
      "COMMAND [ARG...]\n"
      "Run COMMAND [ARG...] with a lock held, a subreaper, or similar.\n");

  g_option_context_add_main_entries (context, options, NULL);

  opt_batch = _srt_boolean_environment ("PRESSURE_VESSEL_BATCH", FALSE);
  opt_verbose = _srt_boolean_environment ("PRESSURE_VESSEL_VERBOSE", FALSE);

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
    _srt_util_set_glib_log_handler (G_LOG_DOMAIN, SRT_LOG_FLAGS_DEBUG);

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
        pv_bwrap_wrap_in_xterm (wrapped_command, g_getenv ("XCURSOR_PATH"));
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

  lib_temp_dirs = pv_per_arch_dirs_new (error);

  if (lib_temp_dirs == NULL)
    {
      g_warning ("%s", local_error->message);
      g_clear_error (error);
    }

  if (opt_preload_modules != NULL)
    {
      GPtrArray *preload_search_paths[G_N_ELEMENTS (preload_variables)] = { NULL };

      /* Iterate through all modules, populating preload_search_paths */
      for (i = 0; i < opt_preload_modules->len; i++)
        {
          const AdverbPreloadModule *module = &g_array_index (opt_preload_modules,
                                                              AdverbPreloadModule, i);
          GPtrArray *search_path = preload_search_paths[module->index_in_preload_variables];
          const char *preload = module->name;
          const char *base;
          gsize abi_index = module->abi_index;

          g_assert (preload != NULL);

          if (*preload == '\0')
            continue;

          base = glnx_basename (preload);

          if (search_path == NULL)
            {
              preload_search_paths[module->index_in_preload_variables]
                = search_path
                = g_ptr_array_new_full (opt_preload_modules->len, g_free);
            }

          /* If we were not able to create the temporary library
           * directories, we simply avoid any adjustment and try to continue */
          if (lib_temp_dirs == NULL)
            {
              g_ptr_array_add (search_path, g_strdup (preload));
              continue;
            }

          if (abi_index == G_MAXSIZE
              && module->index_in_preload_variables == PRELOAD_VARIABLE_INDEX_LD_PRELOAD
              && strcmp (base, "gameoverlayrenderer.so") == 0)
            {
              for (gsize abi = 0; abi < PV_N_SUPPORTED_ARCHITECTURES; abi++)
                {
                  g_autofree gchar *expected_suffix = g_strdup_printf ("/%s/gameoverlayrenderer.so",
                                                                       pv_multiarch_details[abi].gameoverlayrenderer_dir);

                  if (g_str_has_suffix (preload, expected_suffix))
                    {
                      abi_index = abi;
                      break;
                    }
                }

              if (abi_index == G_MAXSIZE)
                {
                  g_debug ("Preloading %s from an unexpected path \"%s\", "
                           "just leave it as is without adjusting",
                           base, preload);
                }
            }

          if (abi_index != G_MAXSIZE)
            {
              g_autofree gchar *link = NULL;
              g_autofree gchar *platform_path = NULL;

              g_debug ("Module %s is for %s",
                       preload, pv_multiarch_details[abi_index].tuple);
              platform_path = g_build_filename (lib_temp_dirs->libdl_token_path,
                                                base, NULL);
              link = g_build_filename (lib_temp_dirs->abi_paths[abi_index],
                                       base, NULL);

              if (symlink (preload, link) != 0)
                {
                  /* This might also happen if the same gameoverlayrenderer.so
                   * was given multiple times. We don't expect this under normal
                   * circumstances, so we bail out. */
                  glnx_throw_errno_prefix (error,
                                           "Unable to create symlink %s -> %s",
                                           link, preload);
                  goto out;
                }

              g_debug ("created symlink %s -> %s", link, preload);
              ptr_array_add_unique (search_path, platform_path,
                                    g_str_equal, generic_strdup);
            }
          else
            {
              g_debug ("Module %s is for all architectures", preload);
              g_ptr_array_add (search_path, g_strdup (preload));
            }
        }

      /* Serialize search_paths[PRELOAD_VARIABLE_INDEX_LD_AUDIT] into
       * LD_AUDIT, etc. */
      for (i = 0; i < G_N_ELEMENTS (preload_variables); i++)
        {
          GPtrArray *search_path = preload_search_paths[i];
          g_autoptr(GString) buffer = g_string_new ("");
          const char *variable = preload_variables[i];

          if (search_path != NULL)
            g_ptr_array_foreach (search_path, (GFunc) append_to_search_path, buffer);

          if (buffer->len != 0)
            flatpak_bwrap_set_env (wrapped_command, variable, buffer->str, TRUE);
        }
    }

  if (opt_regenerate_ld_so_cache != NULL
      && opt_regenerate_ld_so_cache[0] != '\0')
    {
      if (regenerate_ld_so_cache (global_ld_so_conf_entries, opt_regenerate_ld_so_cache,
                                  error))
        {
          g_debug ("Generated ld.so.cache in %s", opt_regenerate_ld_so_cache);
          g_debug ("Setting LD_LIBARY_PATH to \"%s\"", opt_set_ld_library_path);
          flatpak_bwrap_set_env (wrapped_command, "LD_LIBRARY_PATH",
                                 opt_set_ld_library_path, TRUE);
        }
      else
        {
          /* If this fails, it is not fatal - carry on anyway. However,
           * we must not use opt_set_ld_library_path in this case, because
           * in the case where we're not regenerating the ld.so.cache,
           * we have to rely on the longer LD_LIBRARY_PATH with which we
           * were invoked, which includes the library paths that were in
           * global_ld_so_conf_entries. */
          g_warning ("%s", local_error->message);
          g_warning ("Recovering by keeping our previous LD_LIBRARY_PATH");
          g_clear_error (error);
        }
    }
  else if (opt_set_ld_library_path != NULL)
    {
      g_debug ("Setting LD_LIBARY_PATH to \"%s\"", opt_set_ld_library_path);
      flatpak_bwrap_set_env (wrapped_command, "LD_LIBRARY_PATH",
                             opt_set_ld_library_path, TRUE);
    }

  if (opt_generate_locales)
    {
      G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) profiling =
        _srt_profiling_start ("Making sure locales are available");

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
  global_ld_so_conf_entries = NULL;
  global_locks = NULL;
  g_clear_pointer (&global_pass_fds, g_array_unref);
  g_clear_pointer (&opt_regenerate_ld_so_cache, g_free);

  if (locales_temp_dir != NULL)
    _srt_rm_rf (locales_temp_dir);

  g_clear_pointer (&opt_preload_modules, g_array_unref);

  if (local_error != NULL)
    _srt_log_failure ("%s", local_error->message);

  global_original_environ = NULL;
  g_debug ("Exiting with status %d", ret);
  return ret;
}
