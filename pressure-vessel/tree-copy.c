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

#include "steam-runtime-tools/glib-backports-internal.h"
#include "flatpak-bwrap-private.h"
#include "flatpak-utils-base-private.h"
#include "flatpak-utils-private.h"

/* Enabling debug logging for this is rather too verbose, so only
 * enable it when actively debugging this module */
#if 0
#define trace(...) g_debug (__VA_ARGS__)
#else
#define trace(...) do { } while (0)
#endif

static inline gboolean
gets_usrmerged (const char *path)
{
  while (path[0] == '/')
    path++;

  if (strcmp (path, "bin") == 0 ||
      strcmp (path, "sbin") == 0 ||
      g_str_has_prefix (path, "bin/") ||
      g_str_has_prefix (path, "sbin/") ||
      (g_str_has_prefix (path, "lib") &&
       strcmp (path, "libexec") != 0 &&
       !g_str_has_prefix (path, "libexec/")))
    return TRUE;

  return FALSE;
}

/* nftw() doesn't have a user_data argument so we need to use a global
 * variable :-( */
static struct
{
  gchar *source_root;
  gchar *dest_root;
  PvCopyFlags flags;
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
  gboolean usrmerge;
  int link_errno = 0;

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

  while (suffix[0] == '/')
    suffix++;

  trace ("\"%s\": suffix=\"%s\"", fpath, suffix);

  /* If source_root was /path/to/source and fpath was /path/to/source/foo/bar,
   * then suffix is now foo/bar. */

  if ((nftw_data.flags & PV_COPY_FLAGS_USRMERGE) != 0 &&
      gets_usrmerged (suffix))
    {
      trace ("Transforming to \"usr/%s\" for /usr merge", suffix);
      usrmerge = TRUE;
      /* /path/to/dest/usr/foo/bar */
      dest = g_build_filename (nftw_data.dest_root, "usr", suffix, NULL);
    }
  else
    {
      usrmerge = FALSE;
      /* /path/to/dest/foo/bar */
      dest = g_build_filename (nftw_data.dest_root, suffix, NULL);
    }

  switch (typeflag)
    {
      case FTW_D:
      trace ("Is a directory");

        /* If merging /usr, replace /bin, /sbin, /lib* with symlinks like
         * /bin -> usr/bin */
        if (usrmerge && strchr (suffix, '/') == NULL)
          {
            /* /path/to/dest/bin or similar */
            g_autofree gchar *in_root = g_build_filename (nftw_data.dest_root,
                                                          suffix, NULL);

            target = g_build_filename ("usr", suffix, NULL);

            if (TEMP_FAILURE_RETRY (symlink (target, in_root)) != 0)
              {
                glnx_throw_errno_prefix (error,
                                         "Unable to create symlink \"%s\" -> \"%s\"",
                                         dest, target);
                return 1;
              }

            /* Fall through to create usr/bin or similar too */
          }

        if (!glnx_shutil_mkdir_p_at (-1, dest, sb->st_mode & 07777,
                                     NULL, error))
          return 1;
        break;

      case FTW_SL:
        target = glnx_readlinkat_malloc (-1, fpath, NULL, error);

        if (target == NULL)
          return 1;

        trace ("Is a symlink to \"%s\"", target);

        if (usrmerge)
          {
            trace ("Checking for compat symlinks into /usr");

            /* Ignore absolute compat symlinks /lib/foo -> /usr/lib/foo.
             * In this case suffix would be lib/foo. (In a Debian-based
             * source root, Debian Policy §10.5 says this is the only
             * form of compat symlink that should exist in this
             * direction.) */
            if (g_str_has_prefix (target, "/usr/") &&
                strcmp (target + 5, suffix) == 0)
              {
                trace ("Ignoring compat symlink \"%s\" -> \"%s\"",
                       fpath, target);
                return 0;
              }

            /* Ignore relative compat symlinks /lib/foo -> ../usr/lib/foo. */
            if (target[0] != '/')
              {
                g_autofree gchar *dir = g_path_get_dirname (suffix);
                g_autofree gchar *joined = NULL;
                g_autofree gchar *canon = NULL;

                joined = g_build_filename (dir, target, NULL);
                trace ("Joined: \"%s\"", joined);
                canon = g_canonicalize_filename (joined, "/");
                trace ("Canonicalized: \"%s\"", canon);

                if (g_str_has_prefix (canon, "/usr/") &&
                    strcmp (canon + 5, suffix) == 0)
                  {
                    trace ("Ignoring compat symlink \"%s\" -> \"%s\"",
                           fpath, target);
                    return 0;
                  }
              }
          }

        if ((nftw_data.flags & PV_COPY_FLAGS_USRMERGE) != 0 &&
             g_str_has_prefix (suffix, "usr/") &&
             gets_usrmerged (suffix + 4))
          {
            trace ("Checking for compat symlinks out of /usr");

            /* Ignore absolute compat symlinks /usr/lib/foo -> /lib/foo.
             * In this case suffix would be usr/lib/foo. (In a Debian-based
             * source root, Debian Policy §10.5 says this is the only
             * form of compat symlink that should exist in this
             * direction.) */
            if (strcmp (suffix + 3, target) == 0)
              {
                trace ("Ignoring compat symlink \"%s\" -> \"%s\"",
                       fpath, target);
                return 0;
              }

            /* Ignore relative compat symlinks
             * /usr/lib/foo -> ../../lib/foo. */
            if (target[0] != '/')
              {
                g_autofree gchar *dir = g_path_get_dirname (suffix);
                g_autofree gchar *joined = NULL;
                g_autofree gchar *canon = NULL;

                joined = g_build_filename (dir, target, NULL);
                trace ("Joined: \"%s\"", joined);
                canon = g_canonicalize_filename (joined, "/");
                trace ("Canonicalized: \"%s\"", canon);
                g_assert (canon[0] == '/');

                if (strcmp (suffix + 3, canon) == 0)
                  {
                    trace ("Ignoring compat symlink \"%s\" -> \"%s\"",
                           fpath, target);
                    return 0;
                  }
              }
          }

        if (TEMP_FAILURE_RETRY (symlink (target, dest)) != 0)
          {
            glnx_throw_errno_prefix (error,
                                     "Unable to create symlink \"%s\" -> \"%s\"",
                                     dest, target);
            return 1;
          }
        break;

      case FTW_F:
        trace ("Is a regular file");

        /* Fast path: try to make a hard link. */
        if (link (fpath, dest) == 0)
          break;

        link_errno = errno;

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
                                (GLNX_FILE_COPY_OVERWRITE
                                 | GLNX_FILE_COPY_NOCHOWN
                                 | GLNX_FILE_COPY_NOXATTRS),
                                NULL, error))
          {
            glnx_prefix_error (error, "Unable to copy \"%s\" to \"%s\"",
                               fpath, dest);
            return 1;
          }

        /* If link() failed but copying succeeded, then we might have
         * a problem that we need to warn about. */
        if ((nftw_data.flags & PV_COPY_FLAGS_EXPECT_HARD_LINKS) != 0)
          {
            g_warning ("Unable to create hard link \"%s\" to \"%s\": %s",
                       fpath, dest, g_strerror (link_errno));
            g_warning ("Falling back to copying, but this will take more "
                       "time and disk space.");
            g_warning ("For best results, \"%s\" and \"%s\" should both "
                       "be on the same fully-featured Linux filesystem.",
                       nftw_data.source_root, nftw_data.dest_root);
            /* Only warn once per tree copied */
            nftw_data.flags &= ~PV_COPY_FLAGS_EXPECT_HARD_LINKS;
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
                    PvCopyFlags flags,
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
  nftw_data.flags = flags;
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
