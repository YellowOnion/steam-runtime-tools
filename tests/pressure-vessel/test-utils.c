/*
 * Copyright © 2019-2020 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "test-utils.h"

#include <glib.h>
#include "libglnx/libglnx.h"

TestsOpenFdSet
tests_check_fd_leaks_enter (void)
{
  g_autoptr(GHashTable) ret = NULL;
  g_auto(GLnxDirFdIterator) iter = { FALSE };
  g_autoptr(GError) error = NULL;

  ret = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

  glnx_dirfd_iterator_init_at (AT_FDCWD, "/proc/self/fd", TRUE,
                               &iter, &error);
  g_assert_no_error (error);

  while (TRUE)
    {
      g_autofree gchar *target = NULL;
      struct dirent *dent;
      gint64 which;
      char *endptr;

      glnx_dirfd_iterator_next_dent (&iter, &dent, NULL, &error);
      g_assert_no_error (error);

      if (dent == NULL)
        break;

      if (g_str_equal (dent->d_name, ".") || g_str_equal (dent->d_name, ".."))
        continue;

      which = g_ascii_strtoll (dent->d_name, &endptr, 10);

      if (endptr == NULL || *endptr != '\0')
        {
          g_warning ("Found unexpected entry \"%s\" in /proc/self/fd",
                     dent->d_name);
          continue;
        }

      if (which == (gint64) iter.fd)
        continue;

      /* ignore error, just let it be NULL */
      target = glnx_readlinkat_malloc (iter.fd, dent->d_name, NULL, NULL);
      g_hash_table_replace (ret, g_strdup (dent->d_name),
                            g_steal_pointer (&target));
    }

  return g_steal_pointer (&ret);
}

void
tests_check_fd_leaks_leave (TestsOpenFdSet fds)
{
  g_auto(GLnxDirFdIterator) iter = { FALSE };
  g_autoptr(GError) error = NULL;

  glnx_dirfd_iterator_init_at (AT_FDCWD, "/proc/self/fd", TRUE,
                               &iter, &error);
  g_assert_no_error (error);

  while (TRUE)
    {
      g_autofree gchar *target = NULL;
      gpointer val = NULL;
      gint64 which;
      struct dirent *dent;
      char *endptr;

      glnx_dirfd_iterator_next_dent (&iter, &dent, NULL, &error);
      g_assert_no_error (error);

      if (dent == NULL)
        break;

      if (g_str_equal (dent->d_name, ".") || g_str_equal (dent->d_name, ".."))
        continue;

      which = g_ascii_strtoll (dent->d_name, &endptr, 10);

      if (endptr == NULL || *endptr != '\0')
        {
          g_warning ("Found unexpected entry \"%s\" in /proc/self/fd",
                     dent->d_name);
          continue;
        }

      if (which == (gint64) iter.fd)
        continue;

      /* ignore error, just let it be NULL */
      target = glnx_readlinkat_malloc (iter.fd, dent->d_name, NULL, NULL);

      if (g_hash_table_lookup_extended (fds, dent->d_name, NULL, &val))
        {
          g_assert_cmpstr (target, ==, val);
        }
      else
        {
          g_error ("fd %s \"%s\" was leaked",
                   dent->d_name, target == NULL ? "(null)" : target);
        }
    }

  g_hash_table_unref (fds);
}
