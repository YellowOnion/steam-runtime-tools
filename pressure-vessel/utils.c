/*
 * Contains code taken from Flatpak.
 *
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2019 Collabora Ltd.
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

#include "utils.h"

#include <ftw.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <glib.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "libglnx/libglnx.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/profiling-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "flatpak-bwrap-private.h"
#include "flatpak-utils-base-private.h"
#include "flatpak-utils-private.h"

void
pv_search_path_append (GString *search_path,
                       const gchar *item)
{
  g_return_if_fail (search_path != NULL);

  if (item == NULL || item[0] == '\0')
    return;

  if (search_path->len != 0)
    g_string_append (search_path, ":");

  g_string_append (search_path, item);
}

gboolean
pv_run_sync (const char * const * argv,
             const char * const * envp,
             int *exit_status_out,
             char **output_out,
             GError **error)
{
  gsize len;
  gint wait_status;
  g_autofree gchar *output = NULL;
  g_autofree gchar *errors = NULL;
  gsize i;
  g_autoptr(GString) command = g_string_new ("");

  g_return_val_if_fail (argv != NULL, FALSE);
  g_return_val_if_fail (argv[0] != NULL, FALSE);
  g_return_val_if_fail (output_out == NULL || *output_out == NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (exit_status_out != NULL)
    *exit_status_out = -1;

  for (i = 0; argv[i] != NULL; i++)
    {
      g_autofree gchar *quoted = g_shell_quote (argv[i]);

      g_string_append_printf (command, " %s", quoted);
    }

  g_debug ("run:%s", command->str);

  /* We use LEAVE_DESCRIPTORS_OPEN to work around a deadlock in older GLib,
   * and to avoid wasting a lot of time closing fds if the rlimit for
   * maximum open file descriptors is high. Because we're waiting for the
   * subprocess to finish anyway, it doesn't really matter that any fds
   * that are not close-on-execute will get leaked into the child. */
  if (!g_spawn_sync (NULL,  /* cwd */
                     (char **) argv,
                     (char **) envp,
                     (G_SPAWN_SEARCH_PATH |
                      G_SPAWN_LEAVE_DESCRIPTORS_OPEN),
                     NULL, NULL,
                     &output,
                     &errors,
                     &wait_status,
                     error))
    return FALSE;

  g_printerr ("%s", errors);

  if (exit_status_out != NULL)
    {
      if (WIFEXITED (wait_status))
        *exit_status_out = WEXITSTATUS (wait_status);
    }

  len = strlen (output);

  /* Emulate shell $() */
  if (len > 0 && output[len - 1] == '\n')
    output[len - 1] = '\0';

  g_debug ("-> %s", output);

  if (!g_spawn_check_wait_status (wait_status, error))
    return FALSE;

  if (output_out != NULL)
    *output_out = g_steal_pointer (&output);

  return TRUE;
}

/*
 * Returns: (transfer none): The first key in @table in iteration order,
 *  or %NULL if @table is empty.
 */
gpointer
pv_hash_table_get_arbitrary_key (GHashTable *table)
{
  GHashTableIter iter;
  gpointer key = NULL;

  g_hash_table_iter_init (&iter, table);
  if (g_hash_table_iter_next (&iter, &key, NULL))
    return key;
  else
    return NULL;
}

/**
 * pv_wait_for_child_processes:
 * @main_process: process for which we will report wait-status;
 *  zero or negative if there is no main process
 * @wait_status_out: (out): Used to store the wait status of `@main_process`,
 *  as if from `wait()`, on success
 * @error: Used to raise an error on failure
 *
 * Wait for child processes of this process to exit, until the @main_process
 * has exited. If there is no main process, wait until there are no child
 * processes at all.
 *
 * If the process is a subreaper (`PR_SET_CHILD_SUBREAPER`),
 * indirect child processes whose parents have exited will be reparented
 * to it, so this will have the effect of waiting for all descendants.
 *
 * If @main_process is positive, return when @main_process has exited.
 * Child processes that exited before @main_process will also have been
 * "reaped", but child processes that exit after @main_process will not
 * (use `pv_wait_for_child_processes (0, &error)` to resume waiting).
 *
 * If @main_process is zero or negative, wait for all child processes
 * to exit.
 *
 * This function cannot be called in a process that is using
 * g_child_watch_source_new() or similar functions, because it waits
 * for all child processes regardless of their process IDs, and that is
 * incompatible with waiting for individual child processes.
 *
 * Returns: %TRUE when @main_process has exited, or if @main_process
 *  is zero or negative and all child processes have exited
 */
gboolean
pv_wait_for_child_processes (pid_t main_process,
                             int *wait_status_out,
                             GError **error)
{
  if (wait_status_out != NULL)
    *wait_status_out = -1;

  while (1)
    {
      int wait_status = -1;
      pid_t died = wait (&wait_status);

      if (died < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }
          else if (errno == ECHILD)
            {
              g_debug ("No more child processes");
              break;
            }
          else
            {
              return glnx_throw_errno_prefix (error, "wait");
            }
        }

      g_debug ("Child %lld exited with wait status %d",
               (long long) died, wait_status);

      if (died == main_process)
        {
          if (wait_status_out != NULL)
            *wait_status_out = wait_status;

          return TRUE;
        }
    }

  if (main_process > 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "Process %lld was not seen to exit",
                   (long long) main_process);
      return FALSE;
    }

  return TRUE;
}

typedef struct
{
  GError *error;
  GMainContext *context;
  GSource *wait_source;
  GSource *kill_source;
  GSource *sigchld_source;
  gchar *children_file;
  /* GINT_TO_POINTER (pid) => arbitrary */
  GHashTable *sent_sigterm;
  /* GINT_TO_POINTER (pid) => arbitrary */
  GHashTable *sent_sigkill;
  /* Scratch space to build temporary strings */
  GString *buffer;
  /* 0, SIGTERM or SIGKILL */
  int sending_signal;
  /* Nonzero if wait_source has been attached to context */
  guint wait_source_id;
  guint kill_source_id;
  guint sigchld_source_id;
  /* TRUE if we reach a point where we have no more child processes. */
  gboolean finished;
} TerminationData;

/*
 * Free everything in @data.
 */
static void
termination_data_clear (TerminationData *data)
{
  if (data->wait_source_id != 0)
    {
      g_source_destroy (data->wait_source);
      data->wait_source_id = 0;
    }

  if (data->kill_source_id != 0)
    {
      g_source_destroy (data->kill_source);
      data->kill_source_id = 0;
    }

  if (data->sigchld_source_id != 0)
    {
      g_source_destroy (data->sigchld_source);
      data->sigchld_source_id = 0;
    }

  if (data->wait_source != NULL)
    g_source_unref (g_steal_pointer (&data->wait_source));

  if (data->kill_source != NULL)
    g_source_unref (g_steal_pointer (&data->kill_source));

  if (data->sigchld_source != NULL)
    g_source_unref (g_steal_pointer (&data->sigchld_source));

  g_clear_pointer (&data->children_file, g_free);
  g_clear_pointer (&data->context, g_main_context_unref);
  g_clear_error (&data->error);
  g_clear_pointer (&data->sent_sigterm, g_hash_table_unref);
  g_clear_pointer (&data->sent_sigkill, g_hash_table_unref);

  if (data->buffer != NULL)
    g_string_free (g_steal_pointer (&data->buffer), TRUE);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (TerminationData, termination_data_clear)

/*
 * Do whatever the next step for pv_terminate_all_child_processes() is.
 *
 * First, reap child processes that already exited, without blocking.
 *
 * Then, act according to the phase we are in:
 * - before wait_period: do nothing
 * - after wait_period but before grace_period: send SIGTERM
 * - after wait_period and grace_period: send SIGKILL
 */
static void
termination_data_refresh (TerminationData *data)
{
  g_autofree gchar *contents = NULL;
  gboolean has_child = FALSE;
  const char *p;
  char *endptr;

  if (data->error != NULL)
    return;

  g_debug ("Checking for child processes");

  while (1)
    {
      int wait_status = -1;
      pid_t died = waitpid (-1, &wait_status, WNOHANG);

      if (died < 0)
        {
          if (errno == EINTR)
            {
              continue;
            }
          else if (errno == ECHILD)
            {
              /* No child processes at all. We'll double-check this
               * a bit later. */
              break;
            }
          else
            {
              glnx_throw_errno_prefix (&data->error, "wait");
              return;
            }
        }
      else if (died == 0)
        {
          /* No more child processes have exited, but at least one is
           * still running. */
          break;
        }

      /* This process has gone away, so remove any record that we have
       * sent it signals. If the pid is reused, we'll want to send
       * the same signals again. */
      g_debug ("Process %d exited", died);
      g_hash_table_remove (data->sent_sigkill, GINT_TO_POINTER (died));
      g_hash_table_remove (data->sent_sigterm, GINT_TO_POINTER (died));
    }

  /* See whether we have any remaining children. These could be direct
   * child processes, or they could be children we adopted because
   * their parent was one of our descendants and has exited, leaving the
   * child to be reparented to us (their (great)*grandparent) because we
   * are a subreaper. */
  if (!g_file_get_contents (data->children_file, &contents, NULL, &data->error))
    return;

  g_debug ("Child tasks: %s", contents);

  for (p = contents;
       p != NULL && *p != '\0';
       p = endptr)
    {
      guint64 maybe_child;
      int child;
      GHashTable *already;

      while (*p != '\0' && g_ascii_isspace (*p))
        p++;

      if (*p == '\0')
        break;

      maybe_child = g_ascii_strtoull (p, &endptr, 10);

      if (*endptr != '\0' && !g_ascii_isspace (*endptr))
        {
          glnx_throw (&data->error, "Non-numeric string found in %s: %s",
                      data->children_file, p);
          return;
        }

      if (maybe_child > G_MAXINT)
        {
          glnx_throw (&data->error, "Out-of-range number found in %s: %s",
                      data->children_file, p);
          return;
        }

      child = (int) maybe_child;
      g_string_printf (data->buffer, "/proc/%d", child);

      /* If the task is just a thread, it won't have a /proc/%d directory
       * in its own right. We don't kill threads, only processes. */
      if (!g_file_test (data->buffer->str, G_FILE_TEST_IS_DIR))
        {
          g_debug ("Task %d is a thread, not a process", child);
          continue;
        }

      has_child = TRUE;

      if (data->sending_signal == 0)
        break;
      else if (data->sending_signal == SIGKILL)
        already = data->sent_sigkill;
      else
        already = data->sent_sigterm;

      if (!g_hash_table_contains (already, GINT_TO_POINTER (child)))
        {
          g_debug ("Sending signal %d to process %d",
                   data->sending_signal, child);
          g_hash_table_add (already, GINT_TO_POINTER (child));

          if (kill (child, data->sending_signal) < 0)
            g_warning ("Unable to send signal %d to process %d: %s",
                       data->sending_signal, child, g_strerror (errno));

          /* In case the child is stopped, wake it up to receive the signal */
          if (kill (child, SIGCONT) < 0)
            g_warning ("Unable to send SIGCONT to process %d: %s",
                       child, g_strerror (errno));

          /* When the child terminates, we will get SIGCHLD and come
           * back to here. */
        }
    }

  if (!has_child)
    data->finished = TRUE;
}

/*
 * Move from wait period to grace period: start sending SIGTERM to
 * child processes.
 */
static gboolean
start_sending_sigterm (gpointer user_data)
{
  TerminationData *data = user_data;

  g_debug ("Wait period finished, starting to send SIGTERM...");

  if (data->sending_signal == 0)
    data->sending_signal = SIGTERM;

  termination_data_refresh (data);

  data->wait_source_id = 0;
  return G_SOURCE_REMOVE;
}

/*
 * End of grace period: start sending SIGKILL to child processes.
 */
static gboolean
start_sending_sigkill (gpointer user_data)
{
  TerminationData *data = user_data;

  g_debug ("Grace period finished, starting to send SIGKILL...");

  data->sending_signal = SIGKILL;
  termination_data_refresh (data);

  data->kill_source_id = 0;
  return G_SOURCE_REMOVE;
}

/*
 * Called when at least one child process has exited, resulting in
 * SIGCHLD to this process.
 */
static gboolean
sigchld_cb (int sfd,
            G_GNUC_UNUSED GIOCondition condition,
            gpointer user_data)
{
  TerminationData *data = user_data;
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

  g_debug ("One or more child processes exited");
  termination_data_refresh (data);
  return G_SOURCE_CONTINUE;
}

/**
 * pv_terminate_all_child_processes:
 * @wait_period: If greater than 0, wait this many microseconds before
 *  sending `SIGTERM`
 * @grace_period: If greater than 0, after @wait_period plus this many
 *  microseconds, use `SIGKILL` instead of `SIGTERM`. If 0, proceed
 *  directly to sending `SIGKILL`.
 * @error: Used to raise an error on failure
 *
 * Make sure all child processes are terminated.
 *
 * If a child process catches `SIGTERM` but does not exit promptly and
 * does not pass the signal on to its descendants, note that its
 * descendant processes are not guaranteed to be terminated gracefully
 * with `SIGTERM`; they might only receive `SIGKILL`.
 *
 * Return when all child processes have exited or when an error has
 * occurred.
 *
 * This function cannot be called in a process that is using
 * g_child_watch_source_new() or similar functions.
 *
 * The process must be a subreaper, and must have `SIGCHLD` blocked.
 *
 * Returns: %TRUE on success.
 */
gboolean
pv_terminate_all_child_processes (GTimeSpan wait_period,
                                  GTimeSpan grace_period,
                                  GError **error)
{
  g_auto(TerminationData) data = {};
  sigset_t mask;
  int is_subreaper = -1;
  glnx_autofd int sfd = -1;

  if (prctl (PR_GET_CHILD_SUBREAPER, (unsigned long) &is_subreaper, 0, 0, 0) != 0)
    return glnx_throw_errno_prefix (error, "prctl PR_GET_CHILD_SUBREAPER");

  if (is_subreaper != 1)
    return glnx_throw (error, "Process is not a subreaper");

  sigemptyset (&mask);

  if (pthread_sigmask (SIG_BLOCK, NULL, &mask) != 0)
    return glnx_throw_errno_prefix (error, "pthread_sigmask");

  if (!sigismember (&mask, SIGCHLD))
    return glnx_throw (error, "Process has not blocked SIGCHLD");

  data.children_file = g_strdup_printf ("/proc/%d/task/%d/children",
                                        getpid (), getpid ());
  data.context = g_main_context_new ();
  data.sent_sigterm = g_hash_table_new (NULL, NULL);
  data.sent_sigkill = g_hash_table_new (NULL, NULL);
  data.buffer = g_string_new ("/proc/2345678901");

  if (wait_period > 0 && grace_period > 0)
    {
      data.wait_source = g_timeout_source_new (wait_period / G_TIME_SPAN_MILLISECOND);

      g_source_set_callback (data.wait_source, start_sending_sigterm,
                             &data, NULL);
      data.wait_source_id = g_source_attach (data.wait_source, data.context);
    }
  else if (grace_period > 0)
    {
      start_sending_sigterm (&data);
    }

  if (wait_period + grace_period > 0)
    {
      data.kill_source = g_timeout_source_new ((wait_period + grace_period) / G_TIME_SPAN_MILLISECOND);

      g_source_set_callback (data.kill_source, start_sending_sigkill,
                             &data, NULL);
      data.kill_source_id = g_source_attach (data.kill_source, data.context);
    }
  else
    {
      start_sending_sigkill (&data);
    }

  sigemptyset (&mask);
  sigaddset (&mask, SIGCHLD);

  sfd = signalfd (-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);

  if (sfd < 0)
    return glnx_throw_errno_prefix (error, "signalfd");

  data.sigchld_source = g_unix_fd_source_new (sfd, G_IO_IN);
  g_source_set_callback (data.sigchld_source,
                         (GSourceFunc) G_CALLBACK (sigchld_cb), &data, NULL);
  data.sigchld_source_id = g_source_attach (data.sigchld_source, data.context);

  termination_data_refresh (&data);

  while (data.error == NULL
         && !data.finished
         && (data.wait_source_id != 0
             || data.kill_source_id != 0
             || data.sigchld_source_id != 0))
    g_main_context_iteration (data.context, TRUE);

  if (data.error != NULL)
    {
      g_propagate_error (error, g_steal_pointer (&data.error));
      return FALSE;
    }

  return TRUE;
}

/**
 * pv_current_namespace_path_to_host_path:
 * @current_env_path: a path in the current environment
 *
 * Returns: (transfer full): The @current_env_path converted to the host
 *  system, or a copy of @current_env_path if we are not in a Flatpak
 *  environment or it's unknown how to convert the given path.
 */
gchar *
pv_current_namespace_path_to_host_path (const gchar *current_env_path)
{
  gchar *path_on_host = NULL;

  g_return_val_if_fail (g_path_is_absolute (current_env_path),
                        g_strdup (current_env_path));

  if (g_file_test ("/.flatpak-info", G_FILE_TEST_IS_REGULAR))
    {
      struct stat via_current_env_stat;
      struct stat via_persist_stat;
      const gchar *home = g_getenv ("HOME");
      const gchar *after = NULL;

      if (home == NULL)
        home = g_get_home_dir ();

      if (home != NULL)
        after = _srt_get_path_after (current_env_path, home);

      /* If we are inside a Flatpak container, usually, the home
       * folder is '${HOME}/.var/app/${FLATPAK_ID}' on the host system */
      if (after != NULL)
        {
          path_on_host = g_build_filename (home,
                                           ".var",
                                           "app",
                                           g_getenv ("FLATPAK_ID"),
                                           after,
                                           NULL);

          if (lstat (path_on_host, &via_persist_stat) < 0)
            {
              /* The file doesn't exist in ~/.var/app, so assume it was
               * exposed via --filesystem */
              g_clear_pointer (&path_on_host, g_free);
            }
          else if (lstat (current_env_path, &via_current_env_stat) == 0
                   && (via_persist_stat.st_dev != via_current_env_stat.st_dev
                       || via_persist_stat.st_ino != via_current_env_stat.st_ino))
            {
              /* The file exists in ~/.var/app, but is not the same there -
              * presumably a different version was mounted over the top via
              * --filesystem */
              g_clear_pointer (&path_on_host, g_free);
            }
        }

      after = _srt_get_path_after (current_env_path, "/run/host");

      /* In a Flatpak container, usually, '/run/host' is the root of the
       * host system */
      if (after != NULL && path_on_host == NULL)
        path_on_host = g_build_filename ("/", after, NULL);
    }
  /* Either we are not in a Flatpak container or it's not obvious how the
   * container to host translation should happen. Just keep the same path. */
  if (path_on_host == NULL)
    path_on_host = g_strdup (current_env_path);

  return path_on_host;
}

/**
 * pv_delete_dangling_symlink:
 * @dirfd: An open file descriptor for a directory
 * @debug_path: Path to directory represented by @dirfd, used in debug messages
 * @name: A filename in @dirfd that is thought to be a symbolic link
 *
 * If @name exists in @dirfd and is a symbolic link whose target does not
 * exist, delete it.
 */
void
pv_delete_dangling_symlink (int dirfd,
                            const char *debug_path,
                            const char *name)
{
  struct stat stat_buf, lstat_buf;

  g_return_if_fail (dirfd >= 0);
  g_return_if_fail (name != NULL);
  g_return_if_fail (strcmp (name, "") != 0);
  g_return_if_fail (strcmp (name, ".") != 0);
  g_return_if_fail (strcmp (name, "..") != 0);

  if (fstatat (dirfd, name, &lstat_buf, AT_SYMLINK_NOFOLLOW) == 0)
    {
      if (!S_ISLNK (lstat_buf.st_mode))
        {
          g_debug ("Ignoring %s/%s: not a symlink",
                   debug_path, name);
        }
      else if (fstatat (dirfd, name, &stat_buf, 0) == 0)
        {
          g_debug ("Ignoring %s/%s: symlink target still exists",
                   debug_path, name);
        }
      else if (errno != ENOENT)
        {
          int saved_errno = errno;

          g_debug ("Ignoring %s/%s: fstatat(!NOFOLLOW): %s",
                   debug_path, name, g_strerror (saved_errno));
        }
      else
        {
          g_debug ("Target of %s/%s no longer exists, deleting it",
                   debug_path, name);

          if (unlinkat (dirfd, name, 0) != 0)
            {
              int saved_errno = errno;

              g_debug ("Could not delete %s/%s: unlinkat: %s",
                       debug_path, name, g_strerror (saved_errno));
            }
        }
    }
  else if (errno == ENOENT)
    {
      /* Silently ignore: symlink doesn't exist so we don't need to
       * delete it */
    }
  else
    {
      int saved_errno = errno;

      g_debug ("Ignoring %s/%s: fstatat(NOFOLLOW): %s",
               debug_path, name, g_strerror (saved_errno));
    }
}
