/*
 * Backports from GLib
 *
 *  Copyright 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *  Copyright 1997-2000 GLib team
 *  Copyright 2000-2016 Red Hat, Inc.
 *  Copyright 2013-2022 Collabora Ltd.
 *  Copyright 2017-2022 Endless OS Foundation, LLC
 *  Copyright 2018 Georges Basile Stavracas Neto
 *  Copyright 2018 Philip Withnall
 *  Copyright 2018 Will Thompson
 *  Copyright 2021 Joshua Lee
 *  g_execvpe implementation based on GNU libc execvp:
 *   Copyright 1991, 92, 95, 96, 97, 98, 99 Free Software Foundation, Inc.
 * SPDX-License-Identifier: LGPL-2.1-or-later
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

#include "steam-runtime-tools/glib-backports-internal.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glib-object.h>

#if !GLIB_CHECK_VERSION (2, 34, 0)
G_DEFINE_QUARK (g-spawn-exit-error-quark, my_g_spawn_exit_error)
#endif

#if !GLIB_CHECK_VERSION (2, 36, 0)
/*
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
/*
 * g_spawn_check_wait_status:
 * @wait_status: An exit code as returned from g_spawn_sync()
 * @error: a #GError
 *
 * Set @error if @wait_status indicates the child exited abnormally
 * (e.g. with a nonzero exit code, or via a fatal signal).
 *
 * The g_spawn_sync() and g_child_watch_add() family of APIs return an
 * exit status for subprocesses encoded in a platform-specific way.
 * On Unix, this is guaranteed to be in the same format waitpid() returns,
 * and on Windows it is guaranteed to be the result of GetExitCodeProcess().
 *
 * Prior to the introduction of this function in GLib 2.34, interpreting
 * @wait_status required use of platform-specific APIs, which is problematic
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
 * @wait_status. On Windows, it is always the case.
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
 * WIFEXITED() and WEXITSTATUS() on @wait_status directly. Do not attempt
 * to scan or parse the error message string; it may be translated and/or
 * change in future versions of GLib.
 *
 * Returns: %TRUE if child exited successfully, %FALSE otherwise (and
 *     @error will be set)
 *
 * Since: 2.34
 */
gboolean
my_g_spawn_check_wait_status (gint      wait_status,
                              GError  **error)
{
  gboolean ret = FALSE;

  if (WIFEXITED (wait_status))
    {
      if (WEXITSTATUS (wait_status) != 0)
        {
          g_set_error (error, G_SPAWN_EXIT_ERROR, WEXITSTATUS (wait_status),
                       _("Child process exited with code %ld"),
                       (long) WEXITSTATUS (wait_status));
          goto out;
        }
    }
  else if (WIFSIGNALED (wait_status))
    {
      g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                   _("Child process killed by signal %ld"),
                   (long) WTERMSIG (wait_status));
      goto out;
    }
  else if (WIFSTOPPED (wait_status))
    {
      g_set_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
                   _("Child process stopped by signal %ld"),
                   (long) WSTOPSIG (wait_status));
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

#if !GLIB_CHECK_VERSION (2, 36, 0)
/*
 * g_dbus_address_escape_value:
 * @string: an unescaped string to be included in a D-Bus address
 *     as the value in a key-value pair
 *
 * Escape @string so it can appear in a D-Bus address as the value
 * part of a key-value pair.
 *
 * For instance, if @string is `/run/bus-for-:0`,
 * this function would return `/run/bus-for-%3A0`,
 * which could be used in a D-Bus address like
 * `unix:nonce-tcp:host=127.0.0.1,port=42,noncefile=/run/bus-for-%3A0`.
 *
 * Returns: (transfer full): a copy of @string with all
 *     non-optionally-escaped bytes escaped
 *
 * Since: 2.36
 */
gchar *
my_g_dbus_address_escape_value (const gchar *string)
{
  GString *s;
  gsize i;

  g_return_val_if_fail (string != NULL, NULL);

  /* There will often not be anything needing escaping at all. */
  s = g_string_sized_new (strlen (string));

  /* D-Bus address escaping is mostly the same as URI escaping... */
  g_string_append_uri_escaped (s, string, "\\/", FALSE);

  /* ... but '~' is an unreserved character in URIs, but a
   * non-optionally-escaped character in D-Bus addresses. */
  for (i = 0; i < s->len; i++)
    {
      if (G_UNLIKELY (s->str[i] == '~'))
        {
          s->str[i] = '%';
          g_string_insert (s, i + 1, "7E");
          i += 2;
        }
    }

  return g_string_free (s, FALSE);
}
#endif

#if !GLIB_CHECK_VERSION(2, 36, 0)
/*
 * Reimplement GUnixFDSourceFunc API in terms of older GLib versions
 */

typedef struct
{
  GSource parent;
  GPollFD pollfd;
} MyUnixFDSource;

static gboolean
my_unix_fd_source_prepare (GSource *source,
                           int *timeout_out)
{
  if (timeout_out != NULL)
    *timeout_out = -1;

  /* We don't know we're ready to be dispatched until we have polled. */
  return FALSE;
}

static gboolean
my_unix_fd_source_check (GSource *source)
{
  MyUnixFDSource *self = (MyUnixFDSource *) source;

  /* We're ready to be dispatched if an event occurred. */
  return (self->pollfd.revents != 0);
}

static gboolean
my_unix_fd_source_dispatch (GSource *source,
                            GSourceFunc callback,
                            gpointer user_data)
{
  MyUnixFDSource *self = (MyUnixFDSource *) source;
  MyGUnixFDSourceFunc func = (MyGUnixFDSourceFunc) G_CALLBACK (callback);

  g_return_val_if_fail (func != NULL, G_SOURCE_REMOVE);
  return func (self->pollfd.fd, self->pollfd.revents, user_data);
}

static void
my_unix_fd_source_finalize (GSource *source)
{
}

static GSourceFuncs my_unix_fd_source_funcs =
{
  my_unix_fd_source_prepare,
  my_unix_fd_source_check,
  my_unix_fd_source_dispatch,
  my_unix_fd_source_finalize
};

GSource *
my_g_unix_fd_source_new (int fd,
                         GIOCondition condition)
{
  GSource *source = g_source_new (&my_unix_fd_source_funcs,
                                  sizeof (MyUnixFDSource));
  MyUnixFDSource *self = (MyUnixFDSource *) source;

  self->pollfd.fd = fd;
  self->pollfd.events = condition;

  g_source_add_poll (source, &self->pollfd);
  return source;
}

guint
my_g_unix_fd_add_full (int priority,
                       int fd,
                       GIOCondition condition,
                       MyGUnixFDSourceFunc func,
                       gpointer user_data,
                       GDestroyNotify destroy)
{
  GSource *source;
  guint ret;

  g_return_val_if_fail (fd >= 0, 0);
  g_return_val_if_fail (func != NULL, 0);

  source = my_g_unix_fd_source_new (fd, condition);
  g_source_set_callback (source, (GSourceFunc) G_CALLBACK (func),
                         user_data, destroy);
  g_source_set_priority (source, priority);
  ret = g_source_attach (source, NULL);
  g_source_unref (source);
  return ret;
}
#endif

#if !GLIB_CHECK_VERSION(2, 58, 0)
/*
 * g_canonicalize_filename:
 * @filename: (type filename): the name of the file
 * @relative_to: (type filename) (nullable): the relative directory, or %NULL
 * to use the current working directory
 *
 * Gets the canonical file name from @filename. All triple slashes are turned into
 * single slashes, and all `..` and `.`s resolved against @relative_to.
 *
 * Symlinks are not followed, and the returned path is guaranteed to be absolute.
 *
 * If @filename is an absolute path, @relative_to is ignored. Otherwise,
 * @relative_to will be prepended to @filename to make it absolute. @relative_to
 * must be an absolute path, or %NULL. If @relative_to is %NULL, it'll fallback
 * to g_get_current_dir().
 *
 * This function never fails, and will canonicalize file paths even if they don't
 * exist.
 *
 * No file system I/O is done.
 *
 * Returns: (type filename) (transfer full): a newly allocated string with the
 * canonical file path
 * Since: 2.58
 */
gchar *
my_g_canonicalize_filename (const gchar *filename,
                            const gchar *relative_to)
{
  gchar *canon, *start, *p, *q;
  guint i;

  g_return_val_if_fail (relative_to == NULL || g_path_is_absolute (relative_to), NULL);

  if (!g_path_is_absolute (filename))
    {
      gchar *cwd_allocated = NULL;
      const gchar  *cwd;

      if (relative_to != NULL)
        cwd = relative_to;
      else
        cwd = cwd_allocated = g_get_current_dir ();

      canon = g_build_filename (cwd, filename, NULL);
      g_free (cwd_allocated);
    }
  else
    {
      canon = g_strdup (filename);
    }

  start = (char *)g_path_skip_root (canon);

  if (start == NULL)
    {
      /* This shouldn't really happen, as g_get_current_dir() should
         return an absolute pathname, but bug 573843 shows this is
         not always happening */
      g_free (canon);
      return g_build_filename (G_DIR_SEPARATOR_S, filename, NULL);
    }

  /* POSIX allows double slashes at the start to
   * mean something special (as does windows too).
   * So, "//" != "/", but more than two slashes
   * is treated as "/".
   */
  i = 0;
  for (p = start - 1;
       (p >= canon) &&
         G_IS_DIR_SEPARATOR (*p);
       p--)
    i++;
  if (i > 2)
    {
      i -= 1;
      start -= i;
      memmove (start, start+i, strlen (start+i) + 1);
    }

  /* Make sure we're using the canonical dir separator */
  p++;
  while (p < start && G_IS_DIR_SEPARATOR (*p))
    *p++ = G_DIR_SEPARATOR;

  p = start;
  while (*p != 0)
    {
      if (p[0] == '.' && (p[1] == 0 || G_IS_DIR_SEPARATOR (p[1])))
        {
          memmove (p, p+1, strlen (p+1)+1);
        }
      else if (p[0] == '.' && p[1] == '.' && (p[2] == 0 || G_IS_DIR_SEPARATOR (p[2])))
        {
          q = p + 2;
          /* Skip previous separator */
          p = p - 2;
          if (p < start)
            p = start;
          while (p > start && !G_IS_DIR_SEPARATOR (*p))
            p--;
          if (G_IS_DIR_SEPARATOR (*p))
            *p++ = G_DIR_SEPARATOR;
          memmove (p, q, strlen (q)+1);
        }
      else
        {
          /* Skip until next separator */
          while (*p != 0 && !G_IS_DIR_SEPARATOR (*p))
            p++;

          if (*p != 0)
            {
              /* Canonicalize one separator */
              *p++ = G_DIR_SEPARATOR;
            }
        }

      /* Remove additional separators */
      q = p;
      while (*q && G_IS_DIR_SEPARATOR (*q))
        q++;

      if (p != q)
        memmove (p, q, strlen (q) + 1);
    }

  /* Remove trailing slashes */
  if (p > start && G_IS_DIR_SEPARATOR (*(p-1)))
    *(p-1) = 0;

  return canon;
}
#endif

#if !GLIB_CHECK_VERSION(2, 40, 0)
gpointer *
my_g_hash_table_get_keys_as_array (GHashTable *hash,
                                   guint *len)
{
  GPtrArray *arr = g_ptr_array_sized_new (g_hash_table_size (hash));
  GHashTableIter iter;
  gpointer k;

  g_hash_table_iter_init (&iter, hash);

  while (g_hash_table_iter_next (&iter, &k, NULL))
    g_ptr_array_add (arr, k);

  if (len != NULL)
    *len = arr->len;

  g_ptr_array_add (arr, NULL);

  return g_ptr_array_free (arr, FALSE);
}
#endif

#if !GLIB_CHECK_VERSION(2, 50, 0)
/*
 * g_log_writer_is_journald:
 * @output_fd: output file descriptor to check
 *
 * Check whether the given @output_fd file descriptor is a connection to the
 * systemd journal, or something else (like a log file or `stdout` or
 * `stderr`).
 *
 * Invalid file descriptors are accepted and return %FALSE, which allows for
 * the following construct without needing any additional error handling:
 * |[<!-- language="C" -->
 *   is_journald = g_log_writer_is_journald (fileno (stderr));
 * ]|
 *
 * Returns: %TRUE if @output_fd points to the journal, %FALSE otherwise
 * Since: 2.50
 */
gboolean
my_g_log_writer_is_journald (gint output_fd)
{
  /* This is actually a backport of the internal _g_fd_is_journal() which
   * is used to implement the public g_log_writer_is_journald(). */
  union {
    struct sockaddr_storage storage;
    struct sockaddr sa;
    struct sockaddr_un un;
  } addr;
  socklen_t addr_len;
  int err;

  if (output_fd < 0)
    return FALSE;

  /* Namespaced journals start with `/run/systemd/journal.${name}/` (see
   * `RuntimeDirectory=systemd/journal.%i` in `systemd-journald@.service`. The
   * default journal starts with `/run/systemd/journal/`. */
  memset (&addr, 0, sizeof (addr));
  addr_len = sizeof(addr);
  err = getpeername (output_fd, &addr.sa, &addr_len);
  if (err == 0 && addr.storage.ss_family == AF_UNIX)
    return (g_str_has_prefix (addr.un.sun_path, "/run/systemd/journal/") ||
            g_str_has_prefix (addr.un.sun_path, "/run/systemd/journal."));

  return FALSE;
}
#endif

#if !GLIB_CHECK_VERSION(2, 52, 0)
gchar *
my_g_utf8_make_valid (const gchar *str,
                      gssize       len)
{
  GString *string;
  const gchar *remainder, *invalid;
  gsize remaining_bytes, valid_bytes;

  g_return_val_if_fail (str != NULL, NULL);

  if (len < 0)
    len = strlen (str);

  string = NULL;
  remainder = str;
  remaining_bytes = len;

  while (remaining_bytes != 0)
    {
      if (g_utf8_validate (remainder, remaining_bytes, &invalid))
        break;
      valid_bytes = invalid - remainder;

      if (string == NULL)
        string = g_string_sized_new (remaining_bytes);

      g_string_append_len (string, remainder, valid_bytes);
      /* append U+FFFD REPLACEMENT CHARACTER */
      g_string_append (string, "\357\277\275");

      remaining_bytes -= valid_bytes + 1;
      remainder = invalid + 1;
    }

  if (string == NULL)
    return g_strndup (str, len);

  g_string_append_len (string, remainder, remaining_bytes);
  g_string_append_c (string, '\0');

  g_assert (g_utf8_validate (string->str, -1, NULL));

  return g_string_free (string, FALSE);
}
#endif

#if !GLIB_CHECK_VERSION(2, 54, 0)
gboolean
my_g_ptr_array_find_with_equal_func (GPtrArray     *haystack,
                                     gconstpointer  needle,
                                     GEqualFunc     equal_func,
                                     guint         *index_)
{
  guint i;

  g_return_val_if_fail (haystack != NULL, FALSE);

  if (equal_func == NULL)
    equal_func = g_direct_equal;

  for (i = 0; i < haystack->len; i++)
    {
      if (equal_func (g_ptr_array_index (haystack, i), needle))
        {
          if (index_ != NULL)
            *index_ = i;
          return TRUE;
        }
    }

  return FALSE;
}
#endif

#if !GLIB_CHECK_VERSION(2, 68, 0)
/*
 * g_string_replace:
 * @string: a #GString
 * @find: the string to find in @string
 * @replace: the string to insert in place of @find
 * @limit: the maximum instances of @find to replace with @replace, or `0` for
 * no limit
 *
 * Replaces the string @find with the string @replace in a #GString up to
 * @limit times. If the number of instances of @find in the #GString is
 * less than @limit, all instances are replaced. If @limit is `0`,
 * all instances of @find are replaced.
 *
 * Returns: the number of find and replace operations performed.
 *
 * Since: 2.68
 */
guint
my_g_string_replace (GString     *string,
                     const gchar *find,
                     const gchar *replace,
                     guint        limit)
{
  gsize f_len, r_len, pos;
  gchar *cur, *next;
  guint n = 0;

  g_return_val_if_fail (string != NULL, 0);
  g_return_val_if_fail (find != NULL, 0);
  g_return_val_if_fail (replace != NULL, 0);

  f_len = strlen (find);
  r_len = strlen (replace);
  cur = string->str;

  while ((next = strstr (cur, find)) != NULL)
    {
      pos = next - string->str;
      g_string_erase (string, pos, f_len);
      g_string_insert (string, pos, replace);
      cur = string->str + pos + r_len;
      n++;
      if (n == limit)
        break;
    }

  return n;
}
#endif

#if !GLIB_CHECK_VERSION(2, 60, 0)
/**
 * g_strv_equal:
 * @strv1: a %NULL-terminated array of strings
 * @strv2: another %NULL-terminated array of strings
 *
 * Checks if @strv1 and @strv2 contain exactly the same elements in exactly the
 * same order. Elements are compared using g_str_equal(). To match independently
 * of order, sort the arrays first (using g_qsort_with_data() or similar).
 *
 * Two empty arrays are considered equal. Neither @strv1 not @strv2 may be
 * %NULL.
 *
 * Returns: %TRUE if @strv1 and @strv2 are equal
 * Since: 2.60
 */
gboolean
my_g_strv_equal (const gchar * const *strv1,
                 const gchar * const *strv2)
{
  g_return_val_if_fail (strv1 != NULL, FALSE);
  g_return_val_if_fail (strv2 != NULL, FALSE);

  if (strv1 == strv2)
    return TRUE;

  for (; *strv1 != NULL && *strv2 != NULL; strv1++, strv2++)
    {
      if (!g_str_equal (*strv1, *strv2))
        return FALSE;
    }

  return (*strv1 == NULL && *strv2 == NULL);
}
#endif
