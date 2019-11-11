/*
 * Backports from GLib
 *
 *  Copyright 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *  Copyright 1997-2000 GLib team
 *  Copyright 2000 Red Hat, Inc.
 *  Copyright 2019 Collabora Ltd.
 *  g_execvpe implementation based on GNU libc execvp:
 *   Copyright 1991, 92, 95, 96, 97, 98, 99 Free Software Foundation, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "glib-backports.h"

#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* We have no internationalization */
#define _(x) x

#if !GLIB_CHECK_VERSION (2, 34, 0)
G_DEFINE_QUARK (g-spawn-exit-error-quark, my_g_spawn_exit_error)
#endif

#if !GLIB_CHECK_VERSION (2, 36, 0)
/**
 * g_close:
 * @fd: A file descriptor
 * @error: a #GError
 *
 * This wraps the close() call; in case of error, %errno will be
 * preserved, but the error will also be stored as a #GError in @error.
 *
 * Besides using #GError, there is another major reason to prefer this
 * function over the call provided by the system; on Unix, it will
 * attempt to correctly handle %EINTR, which has platform-specific
 * semantics.
 *
 * Returns: %TRUE on success, %FALSE if there was an error.
 *
 * Since: 2.36
 */
gboolean
my_g_close (gint       fd,
            GError   **error)
{
  int res;
  res = close (fd);
  /* Just ignore EINTR for now; a retry loop is the wrong thing to do
   * on Linux at least.  Anyone who wants to add a conditional check
   * for e.g. HP-UX is welcome to do so later...
   *
   * http://lkml.indiana.edu/hypermail/linux/kernel/0509.1/0877.html
   * https://bugzilla.gnome.org/show_bug.cgi?id=682819
   * http://utcc.utoronto.ca/~cks/space/blog/unix/CloseEINTR
   * https://sites.google.com/site/michaelsafyan/software-engineering/checkforeintrwheninvokingclosethinkagain
   */
  if (G_UNLIKELY (res == -1 && errno == EINTR))
    return TRUE;
  else if (res == -1)
    {
      int errsv = errno;
      g_set_error_literal (error, G_FILE_ERROR,
                           g_file_error_from_errno (errsv),
                           g_strerror (errsv));
      errno = errsv;
      return FALSE;
    }
  return TRUE;
}
#endif

#if !GLIB_CHECK_VERSION (2, 34, 0)
/**
 * g_spawn_check_exit_status:
 * @exit_status: An exit code as returned from g_spawn_sync()
 * @error: a #GError
 *
 * Set @error if @exit_status indicates the child exited abnormally
 * (e.g. with a nonzero exit code, or via a fatal signal).
 *
 * The g_spawn_sync() and g_child_watch_add() family of APIs return an
 * exit status for subprocesses encoded in a platform-specific way.
 * On Unix, this is guaranteed to be in the same format waitpid() returns,
 * and on Windows it is guaranteed to be the result of GetExitCodeProcess().
 *
 * Prior to the introduction of this function in GLib 2.34, interpreting
 * @exit_status required use of platform-specific APIs, which is problematic
 * for software using GLib as a cross-platform layer.
 *
 * Additionally, many programs simply want to determine whether or not
 * the child exited successfully, and either propagate a #GError or
 * print a message to standard error. In that common case, this function
 * can be used. Note that the error message in @error will contain
 * human-readable information about the exit status.
 *
 * The @domain and @code of @error have special semantics in the case
 * where the process has an "exit code", as opposed to being killed by
 * a signal. On Unix, this happens if WIFEXITED() would be true of
 * @exit_status. On Windows, it is always the case.
 *
 * The special semantics are that the actual exit code will be the
 * code set in @error, and the domain will be %G_SPAWN_EXIT_ERROR.
 * This allows you to differentiate between different exit codes.
 *
 * If the process was terminated by some means other than an exit
 * status, the domain will be %G_SPAWN_ERROR, and the code will be
 * %G_SPAWN_ERROR_FAILED.
 *
 * This function just offers convenience; you can of course also check
 * the available platform via a macro such as %G_OS_UNIX, and use
 * WIFEXITED() and WEXITSTATUS() on @exit_status directly. Do not attempt
 * to scan or parse the error message string; it may be translated and/or
 * change in future versions of GLib.
 *
 * Returns: %TRUE if child exited successfully, %FALSE otherwise (and
 *     @error will be set)
 *
 * Since: 2.34
 */
gboolean
my_g_spawn_check_exit_status (gint      exit_status,
                              GError  **error)
{
  gboolean ret = FALSE;

  if (WIFEXITED (exit_status))
    {
      if (WEXITSTATUS (exit_status) != 0)
        {
          g_set_error (error, G_SPAWN_EXIT_ERROR, WEXITSTATUS (exit_status),
                       _("Child process exited with code %ld"),
                       (long) WEXITSTATUS (exit_status));
          goto out;
        }
    }
  else if (WIFSIGNALED (exit_status))
    {
      g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                   _("Child process killed by signal %ld"),
                   (long) WTERMSIG (exit_status));
      goto out;
    }
  else if (WIFSTOPPED (exit_status))
    {
      g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                   _("Child process stopped by signal %ld"),
                   (long) WSTOPSIG (exit_status));
      goto out;
    }
  else
    {
      g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                   _("Child process exited abnormally"));
      goto out;
    }

  ret = TRUE;
 out:
  return ret;
}
#endif

#if !GLIB_CHECK_VERSION (2, 40, 0)
/*
 * A reimplementation of g_ptr_array_insert()
 */
void
my_g_ptr_array_insert (GPtrArray *arr,
                       gint index_,
                       gpointer data)
{
  g_return_if_fail (arr != NULL);
  g_return_if_fail (index_ >= -1);
  g_return_if_fail (index_ <= (gint) arr->len);

  if (index_ < 0)
    index_ = arr->len;

  if ((guint) index_ == arr->len)
    {
      g_ptr_array_add (arr, data);
    }
  else
    {
      g_ptr_array_add (arr, NULL);
      memmove (&(arr->pdata[index_ + 1]),
               &(arr->pdata[index_]),
               (arr->len - 1 - index_) * sizeof (gpointer));
      arr->pdata[index_] = data;
    }
}
#endif
