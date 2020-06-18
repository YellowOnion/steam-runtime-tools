/*
 * Contains code taken from Flatpak.
 *
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2020 Collabora Ltd.
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

#include "tree-copy.h"

#include <ftw.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "libglnx/libglnx.h"

#include "glib-backports.h"
#include "flatpak-bwrap-private.h"
#include "flatpak-utils-base-private.h"
#include "flatpak-utils-private.h"

/* nftw() doesn't have a user_data argument so we need to use a global
 * variable :-( */
static struct
{
  gchar *source_root;
  gchar *dest_root;
  GError *error;
} nftw_data;

static int
copy_tree_helper (const char *fpath,
                  const struct stat *sb,
                  int typeflag,
                  struct FTW *ftwbuf)
{
  size_t len;
  const char *suffix;
  g_autofree gchar *dest = NULL;
  g_autofree gchar *target = NULL;
  GError **error = &nftw_data.error;

  g_return_val_if_fail (g_str_has_prefix (fpath, nftw_data.source_root), 1);

  if (strcmp (fpath, nftw_data.source_root) == 0)
    {
      if (typeflag != FTW_D)
        {
          glnx_throw (error, "\"%s\" is not a directory", fpath);
          return 1;
        }

      if (!glnx_shutil_mkdir_p_at (-1, nftw_data.dest_root,
                                   sb->st_mode & 07777, NULL, error))
        return 1;

      return 0;
    }

  len = strlen (nftw_data.source_root);
  g_return_val_if_fail (fpath[len] == '/', 1);
  suffix = &fpath[len + 1];
  dest = g_build_filename (nftw_data.dest_root, suffix, NULL);

  switch (typeflag)
    {
      case FTW_D:
        if (!glnx_shutil_mkdir_p_at (-1, dest, sb->st_mode & 07777,
                                     NULL, error))
          return 1;
        break;

      case FTW_SL:
        target = glnx_readlinkat_malloc (-1, fpath, NULL, error);

        if (target == NULL)
          return 1;

        if (symlink (target, dest) != 0)
          {
            glnx_throw_errno_prefix (error,
                                     "Unable to create symlink at \"%s\"",
                                     dest);
            return 1;
          }
        break;

      case FTW_F:
        /* Fast path: try to make a hard link. */
        if (link (fpath, dest) == 0)
          break;

        /* Slow path: fall back to copying.
         *
         * This does a FICLONE or copy_file_range to get btrfs reflinks
         * if possible, making the copy as cheap as cp --reflink=auto.
         *
         * Rather than second-guessing which errno values would result
         * in link() failing but a copy succeeding, we just try it
         * unconditionally - the worst that can happen is that this
         * fails too. */
        if (!glnx_file_copy_at (AT_FDCWD, fpath, sb,
                                AT_FDCWD, dest,
                                GLNX_FILE_COPY_OVERWRITE,
                                NULL, error))
          {
            glnx_prefix_error (error, "Unable to copy \"%s\" to \"%s\"",
                               fpath, dest);
            return 1;
          }
        break;

      default:
        glnx_throw (&nftw_data.error,
                    "Don't know how to handle ftw type flag %d at %s",
                    typeflag, fpath);
        return 1;
    }

  return 0;
}

gboolean
pv_cheap_tree_copy (const char *source_root,
                    const char *dest_root,
                    GError **error)
{
  int res;

  g_return_val_if_fail (source_root != NULL, FALSE);
  g_return_val_if_fail (dest_root != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  /* Can't run concurrently */
  g_return_val_if_fail (nftw_data.source_root == NULL, FALSE);

  nftw_data.source_root = flatpak_canonicalize_filename (source_root);
  nftw_data.dest_root = flatpak_canonicalize_filename (dest_root);
  nftw_data.error = NULL;

  res = nftw (nftw_data.source_root, copy_tree_helper, 100, FTW_PHYS);

  if (res == -1)
    {
      g_assert (nftw_data.error == NULL);
      glnx_throw_errno_prefix (error, "Unable to copy \"%s\" to \"%s\"",
                               source_root, dest_root);
    }
  else if (res != 0)
    {
      g_propagate_error (error, g_steal_pointer (&nftw_data.error));
    }

  g_clear_pointer (&nftw_data.source_root, g_free);
  g_clear_pointer (&nftw_data.dest_root, g_free);
  g_assert (nftw_data.error == NULL);
  return (res == 0);
}
