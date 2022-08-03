/*
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2022 Collabora Ltd.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "wrap-home.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "libglnx.h"

#include "flatpak-utils-base-private.h"
#include "flatpak-utils-private.h"

/* Order matters here: root, steam and steambeta are or might be symlinks
 * to the root of the Steam installation, so we want to bind-mount their
 * targets before we deal with the rest. */
static const char * const steam_api_subdirs[] =
{
  "root", "steam", "steambeta", "bin", "bin32", "bin64", "sdk32", "sdk64",
};

static gboolean expose_steam (FlatpakExports *exports,
                              FlatpakFilesystemMode mode,
                              PvHomeMode home_mode,
                              const char *real_home,
                              const char *fake_home,
                              GError **error);

static gboolean
use_tmpfs_home (FlatpakExports *exports,
                FlatpakBwrap *bwrap,
                PvEnviron *container_env,
                GError **error)
{
  const gchar *home = g_get_home_dir ();
  g_autofree char *real_home = realpath (home, NULL);
  g_autofree gchar *cache = g_build_filename (real_home, ".cache", NULL);
  g_autofree gchar *cache2 = g_build_filename (real_home, "cache", NULL);
  g_autofree gchar *tmp = g_build_filename (cache, "tmp", NULL);
  g_autofree gchar *config = g_build_filename (real_home, ".config", NULL);
  g_autofree gchar *config2 = g_build_filename (real_home, "config", NULL);
  g_autofree gchar *local = g_build_filename (real_home, ".local", NULL);
  g_autofree gchar *data = g_build_filename (local, "share", NULL);
  g_autofree gchar *data2 = g_build_filename (real_home, "data", NULL);

  g_return_val_if_fail (bwrap != NULL, FALSE);
  g_return_val_if_fail (exports != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* If the logical path to the home dir has a symlink among its ancestors
   * (e.g. /home/user when /home -> var/home exists), make sure the
   * symlink structure gets mirrored in the container */
  flatpak_exports_add_path_dir (exports, home);

  /* Mount the tmpfs home directory onto the physical path to real_home,
   * so that it will not conflict with symlinks created by the exports.
   * See also https://github.com/flatpak/flatpak/issues/1278 and
   * Flatpak commit f1df5cb1 */
  flatpak_bwrap_add_args (bwrap, "--tmpfs", real_home, NULL);

  flatpak_bwrap_add_args (bwrap,
                          "--dir", cache,
                          "--dir", tmp,
                          "--dir", config,
                          "--dir", local,
                          "--dir", data,
                          "--symlink", ".cache", cache2,
                          "--symlink", ".config", config2,
                          "--symlink", ".local/share", data2,
                          "--symlink", tmp, "/var/tmp",
                          NULL);

  pv_environ_setenv (container_env, "XDG_CACHE_HOME", cache);
  pv_environ_setenv (container_env, "XDG_CONFIG_HOME", config);
  pv_environ_setenv (container_env, "XDG_DATA_HOME", data);

  return expose_steam (exports, FLATPAK_FILESYSTEM_MODE_READ_ONLY,
                       PV_HOME_MODE_TRANSIENT, real_home, NULL, error);
}

static gboolean
use_fake_home (FlatpakExports *exports,
               FlatpakBwrap *bwrap,
               PvEnviron *container_env,
               const gchar *fake_home,
               GError **error)
{
  const gchar *real_home = g_get_home_dir ();
  g_autofree gchar *cache = g_build_filename (fake_home, ".cache", NULL);
  g_autofree gchar *cache2 = g_build_filename (fake_home, "cache", NULL);
  g_autofree gchar *tmp = g_build_filename (cache, "tmp", NULL);
  g_autofree gchar *config = g_build_filename (fake_home, ".config", NULL);
  g_autofree gchar *config2 = g_build_filename (fake_home, "config", NULL);
  g_autofree gchar *local = g_build_filename (fake_home, ".local", NULL);
  g_autofree gchar *data = g_build_filename (local, "share", NULL);
  g_autofree gchar *data2 = g_build_filename (fake_home, "data", NULL);

  g_return_val_if_fail (bwrap != NULL, FALSE);
  g_return_val_if_fail (exports != NULL, FALSE);
  g_return_val_if_fail (fake_home != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_mkdir_with_parents (fake_home, 0700);
  g_mkdir_with_parents (cache, 0700);
  g_mkdir_with_parents (tmp, 0700);
  g_mkdir_with_parents (config, 0700);
  g_mkdir_with_parents (local, 0700);
  g_mkdir_with_parents (data, 0700);

  if (!g_file_test (cache2, G_FILE_TEST_EXISTS))
    {
      g_unlink (cache2);

      if (symlink (".cache", cache2) != 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to create symlink %s -> .cache",
                                        cache2);
    }

  if (!g_file_test (config2, G_FILE_TEST_EXISTS))
    {
      g_unlink (config2);

      if (symlink (".config", config2) != 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to create symlink %s -> .config",
                                        config2);
    }

  if (!g_file_test (data2, G_FILE_TEST_EXISTS))
    {
      g_unlink (data2);

      if (symlink (".local/share", data2) != 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to create symlink %s -> .local/share",
                                        data2);
    }

  /* If the logical path to real_home has a symlink among its ancestors
   * (e.g. /home/user when /home -> var/home exists), make sure the
   * symlink structure gets mirrored in the container */
  flatpak_exports_add_path_dir (exports, g_get_home_dir ());

  /* Mount the fake home directory onto the physical path to real_home,
   * so that it will not conflict with symlinks created by the exports.
   * See also https://github.com/flatpak/flatpak/issues/1278 and
   * Flatpak commit f1df5cb1 */
  flatpak_bwrap_add_bind_arg (bwrap, "--bind", fake_home, real_home);

  flatpak_bwrap_add_args (bwrap,
                          "--bind", tmp, "/var/tmp",
                          NULL);

  pv_environ_setenv (container_env, "XDG_CACHE_HOME", cache);
  pv_environ_setenv (container_env, "XDG_CONFIG_HOME", config);
  pv_environ_setenv (container_env, "XDG_DATA_HOME", data);

  flatpak_exports_add_path_expose (exports,
                                   FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                                   fake_home);

  return expose_steam (exports, FLATPAK_FILESYSTEM_MODE_READ_ONLY,
                       PV_HOME_MODE_PRIVATE, real_home, fake_home, error);
}

static gboolean
expose_steam (FlatpakExports *exports,
              FlatpakFilesystemMode mode,
              PvHomeMode home_mode,
              const char *real_home,
              const char *fake_home,
              GError **error)
{
  g_autofree gchar *dot_steam = g_build_filename (real_home, ".steam", NULL);
  gsize i;

  g_return_val_if_fail (exports != NULL, FALSE);
  g_return_val_if_fail (real_home != NULL, FALSE);
  g_return_val_if_fail ((unsigned) mode <= FLATPAK_FILESYSTEM_MODE_LAST, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* We need ~/.steam to be visible in the container, even if it's a
   * symlink to somewhere outside $HOME. (It's better not to do this; use
   * a separate Steam library instead, or use bind-mounts.) */
  if (home_mode != PV_HOME_MODE_SHARED)
    {
      flatpak_exports_add_path_expose (exports, mode, dot_steam);
    }
  else
    {
      /* Expose the target, but don't try to create the symlink itself:
       * that will fail, because we are already sharing the home directory
       * with the container, and there's already a symlink where we want
       * to put it. */
      g_autofree gchar *target = flatpak_resolve_link (dot_steam, NULL);

      if (target != NULL)
        flatpak_exports_add_path_expose (exports, mode, target);
    }

  /*
   * These might be API entry points, according to Steam/steam.sh.
   * They're usually symlinks into the Steam root, except for in
   * older steam Debian packages that had Debian bug #916303.
   *
   * Even though the symlinks themselves are exposed as part of ~/.steam,
   * we need to tell FlatpakExports to also expose the directory to which
   * they point, typically (but not necessarily!) ~/.local/share/Steam.
   *
   * TODO: We probably want to hide part or all of root, steam,
   * steambeta?
   */
  for (i = 0; i < G_N_ELEMENTS (steam_api_subdirs); i++)
    {
      g_autofree gchar *dir = g_build_filename (dot_steam,
                                                steam_api_subdirs[i], NULL);

      if (fake_home != NULL)
        {
          g_autofree gchar *mount_point = g_build_filename (fake_home, ".steam",
                                                            steam_api_subdirs[i],
                                                            NULL);
          g_autofree gchar *target = NULL;

          target = glnx_readlinkat_malloc (-1, dir, NULL, NULL);

          if (target != NULL)
            {
              /* We used to bind-mount these directories, so transition them
               * to symbolic links if we can. */
              if (rmdir (mount_point) != 0 && errno != ENOENT && errno != ENOTDIR)
                g_debug ("rmdir %s: %s", mount_point, g_strerror (errno));

              /* Remove any symlinks that might have already been there. */
              if (unlink (mount_point) != 0 && errno != ENOENT)
                g_debug ("unlink %s: %s", mount_point, g_strerror (errno));
            }
        }

      flatpak_exports_add_path_expose (exports, mode, dir);
    }

  return TRUE;
}

gboolean
pv_wrap_use_home (PvHomeMode mode,
                  const char *real_home,
                  const char *private_home,
                  FlatpakExports *exports,
                  FlatpakBwrap *bwrap_home_arguments,
                  PvEnviron *container_env,
                  GError **error)
{
  g_return_val_if_fail (real_home != NULL, FALSE);
  g_return_val_if_fail (exports != NULL, FALSE);
  g_return_val_if_fail (bwrap_home_arguments != NULL, FALSE);
  g_return_val_if_fail (container_env != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  switch (mode)
    {
      case PV_HOME_MODE_SHARED:
        flatpak_exports_add_path_expose (exports,
                                         FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                                         real_home);

        /* We always export /tmp for now (see below) and it seems odd
         * to share /tmp with the host, but not /var/tmp.
         * We don't do this when not sharing the home directory, since
         * in that case the replacement home directory provides /var/tmp
         * as a symlink or bind-mount pointing to its .cache/tmp,
         * consistent with Flatpak. */
        flatpak_exports_add_path_expose (exports,
                                         FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                                         "/var/tmp");

        /* TODO: All of ~/.steam has traditionally been read/write when not
         * using a per-game home directory, but does it need to be? Maybe we
         * should have a future "compat level" in which it's read-only,
         * like it already is when using a per-game home directory. */
        if (!expose_steam (exports, FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                           mode, real_home, NULL, error))
          return FALSE;

        return TRUE;

      case PV_HOME_MODE_TRANSIENT:
        return use_tmpfs_home (exports, bwrap_home_arguments,
                               container_env, error);

      case PV_HOME_MODE_PRIVATE:
        g_return_val_if_fail (private_home != NULL, FALSE);
        return use_fake_home (exports, bwrap_home_arguments,
                              container_env, private_home, error);

      default:
        break;
    }

  g_return_val_if_reached (FALSE);
}
