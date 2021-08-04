/*
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2021 Collabora Ltd.
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

#include "wrap-setup.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx/libglnx.h"

#include <string.h>

#include "bwrap.h"
#include "flatpak-run-private.h"
#include "flatpak-utils-private.h"

/*
 * Use code borrowed from Flatpak to share various bits of the
 * execution environment with the host system, in particular Wayland,
 * X11 and PulseAudio sockets.
 */
void
pv_wrap_share_sockets (FlatpakBwrap *bwrap,
                       PvEnviron *container_env,
                       gboolean using_a_runtime,
                       gboolean is_flatpak_env)
{
  g_autoptr(FlatpakBwrap) sharing_bwrap =
    flatpak_bwrap_new (flatpak_bwrap_empty_env);
  g_auto(GStrv) envp = NULL;
  gsize i;

  g_return_if_fail (bwrap != NULL);
  g_return_if_fail (container_env != NULL);

  /* If these are set by flatpak_run_add_x11_args(), etc., we'll
   * change them from unset to set later.
   * Every variable that is unset with flatpak_bwrap_unset_env() in
   * the functions we borrow from Flatpak (below) should be listed
   * here. */
  pv_environ_setenv (container_env, "DISPLAY", NULL);
  pv_environ_setenv (container_env, "PULSE_SERVER", NULL);
  pv_environ_setenv (container_env, "XAUTHORITY", NULL);

  flatpak_run_add_font_path_args (sharing_bwrap);

  /* We need to set up IPC rendezvous points relatively late, so that
   * even if we are sharing /tmp via --filesystem=/tmp, we'll still
   * mount our own /tmp/.X11-unix over the top of the OS's. */
  if (using_a_runtime)
    {
      flatpak_run_add_wayland_args (sharing_bwrap);

      /* When in a Flatpak container the "DISPLAY" env is equal to ":99.0",
       * but it might be different on the host system. As a workaround we simply
       * bind the whole "/tmp/.X11-unix" directory and later unset the container
       * "DISPLAY" env.
       */
      if (is_flatpak_env)
        {
          flatpak_bwrap_add_args (sharing_bwrap,
                                  "--ro-bind", "/tmp/.X11-unix", "/tmp/.X11-unix",
                                  NULL);
        }
      else
        {
          flatpak_run_add_x11_args (sharing_bwrap, TRUE);
        }

      flatpak_run_add_pulseaudio_args (sharing_bwrap);
      flatpak_run_add_session_dbus_args (sharing_bwrap);
      flatpak_run_add_system_dbus_args (sharing_bwrap);
      flatpak_run_add_resolved_args (sharing_bwrap);
      pv_wrap_add_pipewire_args (sharing_bwrap, container_env);
    }

  envp = pv_bwrap_steal_envp (sharing_bwrap);

  for (i = 0; envp[i] != NULL; i++)
    {
      static const char * const known_vars[] =
      {
        "DBUS_SESSION_BUS_ADDRESS",
        "DBUS_SYSTEM_BUS_ADDRESS",
        "DISPLAY",
        "PULSE_CLIENTCONFIG",
        "PULSE_SERVER",
        "XAUTHORITY",
      };
      char *equals = strchr (envp[i], '=');
      const char *var = envp[i];
      const char *val = NULL;
      gsize j;

      if (equals != NULL)
        {
          *equals = '\0';
          val = equals + 1;
        }

      for (j = 0; j < G_N_ELEMENTS (known_vars); j++)
        {
          if (strcmp (var, known_vars[j]) == 0)
            break;
        }

      /* If this warning is reached, we might need to add this
       * variable to the block of
       * pv_environ_setenv (container_env, ., NULL) calls above */
      if (j >= G_N_ELEMENTS (known_vars))
        g_warning ("Extra environment variable %s set during container "
                   "setup but not in known_vars; check logic",
                   var);

      pv_environ_setenv (container_env, var, val);
    }

  g_warn_if_fail (g_strv_length (sharing_bwrap->envp) == 0);
  flatpak_bwrap_append_bwrap (bwrap, sharing_bwrap);
}

/*
 * Export most root directories, but not the ones that
 * "flatpak run --filesystem=host" would skip.
 * (See flatpak_context_export(), which might replace this function
 * later on.)
 *
 * If we are running inside Flatpak, we assume that any directory
 * that is made available in the root, and is not in dont_mount_in_root,
 * came in via --filesystem=host or similar and matches its equivalent
 * on the real root filesystem.
 */
static gboolean
export_root_dirs_like_filesystem_host (FlatpakExports *exports,
                                       FlatpakFilesystemMode mode,
                                       GError **error)
{
  g_autoptr(GDir) dir = NULL;
  const char *member = NULL;

  g_return_val_if_fail (exports != NULL, FALSE);
  g_return_val_if_fail ((unsigned) mode <= FLATPAK_FILESYSTEM_MODE_LAST, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  dir = g_dir_open ("/", 0, error);

  if (dir == NULL)
    return FALSE;

  for (member = g_dir_read_name (dir);
       member != NULL;
       member = g_dir_read_name (dir))
    {
      g_autofree gchar *path = NULL;

      if (g_strv_contains (dont_mount_in_root, member))
        continue;

      path = g_build_filename ("/", member, NULL);
      flatpak_exports_add_path_expose (exports, mode, path);
    }

  /* For parity with Flatpak's handling of --filesystem=host */
  flatpak_exports_add_path_expose (exports, mode, "/run/media");

  return TRUE;
}

/*
 * This function assumes that /run on the host is the same as in the
 * current namespace, so it won't work in Flatpak.
 */
static gboolean
export_contents_of_run (FlatpakBwrap *bwrap,
                        GError **error)
{
  static const char *ignore[] =
  {
    "gfx",              /* can be created by pressure-vessel */
    "host",             /* created by pressure-vessel */
    "media",            /* see export_root_dirs_like_filesystem_host() */
    "pressure-vessel",  /* created by pressure-vessel */
    NULL
  };
  g_autoptr(GDir) dir = NULL;
  const char *member = NULL;

  g_return_val_if_fail (bwrap != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (!g_file_test ("/.flatpak-info", G_FILE_TEST_IS_REGULAR),
                        FALSE);

  dir = g_dir_open ("/run", 0, error);

  if (dir == NULL)
    return FALSE;

  for (member = g_dir_read_name (dir);
       member != NULL;
       member = g_dir_read_name (dir))
    {
      g_autofree gchar *path = NULL;

      if (g_strv_contains (ignore, member))
        continue;

      path = g_build_filename ("/run", member, NULL);
      flatpak_bwrap_add_args (bwrap,
                              "--bind", path, path,
                              NULL);
    }

  return TRUE;
}

/*
 * Configure @exports and @bwrap to use the host operating system to
 * provide basically all directories.
 *
 * /app and /boot are excluded, but are assumed to be unnecessary.
 *
 * /dev, /proc and /sys are assumed to have been handled by
 * pv_bwrap_add_api_filesystems() already.
 */
gboolean
pv_wrap_use_host_os (FlatpakExports *exports,
                     FlatpakBwrap *bwrap,
                     GError **error)
{
  static const char * const export_os_mutable[] = { "/etc", "/tmp", "/var" };
  gsize i;

  g_return_val_if_fail (exports != NULL, FALSE);
  g_return_val_if_fail (bwrap != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!pv_bwrap_bind_usr (bwrap, "/", "/", "/", error))
    return FALSE;

  for (i = 0; i < G_N_ELEMENTS (export_os_mutable); i++)
    {
      const char *dir = export_os_mutable[i];

      if (g_file_test (dir, G_FILE_TEST_EXISTS))
        flatpak_bwrap_add_args (bwrap, "--bind", dir, dir, NULL);
    }

  /* We do each subdirectory of /run separately, so that we can
   * always create /run/host and /run/pressure-vessel. */
  if (!export_contents_of_run (bwrap, error))
    return FALSE;

  /* This handles everything except:
   *
   * /app (should be unnecessary)
   * /boot (should be unnecessary)
   * /dev (handled by pv_bwrap_add_api_filesystems())
   * /etc (handled by export_os_mutable above)
   * /proc (handled by pv_bwrap_add_api_filesystems())
   * /root (should be unnecessary)
   * /run (handled by export_contents_of_run() above)
   * /sys (handled by pv_bwrap_add_api_filesystems())
   * /tmp (handled by export_os_mutable above)
   * /usr, /lib, /lib32, /lib64, /bin, /sbin
   *  (all handled by pv_bwrap_bind_usr() above)
   * /var (handled by export_os_mutable above)
   */
  if (!export_root_dirs_like_filesystem_host (exports,
                                              FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                                              error))
    return FALSE;

  return TRUE;
}

const char *
pv_wrap_get_steam_app_id (const char *from_command_line)
{
  const char *value;

  if (from_command_line != NULL)
    return from_command_line;

  if ((value = g_getenv ("STEAM_COMPAT_APP_ID")) != NULL)
    return value;

  if ((value = g_getenv ("SteamAppId")) != NULL)
    return value;

  return NULL;
}

/*
 * Try to move the current process into a scope defined by the given
 * Steam app ID. If that's not possible, ignore.
 */
void
pv_wrap_move_into_scope (const char *steam_app_id)
{
  g_autoptr(GError) local_error = NULL;

  if (steam_app_id != NULL)
    {
      if (steam_app_id[0] == '\0')
        steam_app_id = NULL;
      else if (strcmp (steam_app_id, "0") == 0)
        steam_app_id = NULL;
    }

  if (steam_app_id != NULL)
    flatpak_run_in_transient_unit ("steam", "app", steam_app_id, &local_error);
  else
    flatpak_run_in_transient_unit ("steam", "", "unknown", &local_error);

  if (local_error != NULL)
    g_debug ("Cannot move into a systemd scope: %s", local_error->message);
}

static void
append_preload_internal (GPtrArray *argv,
                         const char *option,
                         const char *original_path,
                         GStrv env,
                         PvAppendPreloadFlags flags,
                         PvRuntime *runtime,
                         FlatpakExports *exports)
{
  gboolean flatpak_subsandbox = ((flags & PV_APPEND_PRELOAD_FLAGS_FLATPAK_SUBSANDBOX) != 0);

  if (runtime != NULL
      && (g_str_has_prefix (original_path, "/usr/")
          || g_str_has_prefix (original_path, "/lib")
          || (flatpak_subsandbox && g_str_has_prefix (original_path, "/app/"))))
    {
      g_autofree gchar *adjusted_path = NULL;
      const char *target = flatpak_subsandbox ? "/run/parent" : "/run/host";

      adjusted_path = g_build_filename (target, original_path, NULL);
      g_debug ("%s -> %s", original_path, adjusted_path);

      g_ptr_array_add (argv, g_strdup_printf ("%s=%s",
                                              option, adjusted_path));
    }
  else
    {
      g_debug ("%s -> unmodified", original_path);

      g_ptr_array_add (argv, g_strdup_printf ("%s=%s", option, original_path));

      if (exports != NULL && original_path[0] == '/')
        {
          const gchar *steam_path = g_environ_getenv (env, "STEAM_COMPAT_CLIENT_INSTALL_PATH");

          if (steam_path != NULL
              && flatpak_has_path_prefix (original_path, steam_path))
            {
              g_debug ("Skipping exposing \"%s\" because it is located "
                       "under the Steam client install path that we "
                       "bind by default", original_path);
            }
          else
            {
              g_debug ("%s needs adding to exports", original_path);
              flatpak_exports_add_path_expose (exports,
                                               FLATPAK_FILESYSTEM_MODE_READ_ONLY,
                                               original_path);
            }
        }
    }
}

/**
 * pv_wrap_append_preload:
 * @argv: (element-type filename): Array of command-line options to populate
 * @variable: (type filename): Environment variable from which this
 *  preload module was taken, either `LD_AUDIT` or `LD_PRELOAD`
 * @option: (type filename): Command-line option to add to @argv,
 *  either `--ld-audit` or `--ld-preload`
 * @preload: (type filename): Path of preloadable module in current
 *  namespace, possibly including special ld.so tokens such as `$LIB`,
 *  or basename of a preloadable module to be found in the standard
 *  library search path
 * @env: (array zero-terminated=1) (element-type filename): Environment
 *  variables to be used instead of `environ`
 * @flags: Flags to adjust behaviour
 * @runtime: (nullable): Runtime to be used in container
 * @exports: (nullable): Used to configure extra paths that need to be
 *  exported into the container
 *
 * Adjust @preload to be valid for the container and append it
 * to @argv.
 */
void
pv_wrap_append_preload (GPtrArray *argv,
                        const char *variable,
                        const char *option,
                        const char *preload,
                        GStrv env,
                        PvAppendPreloadFlags flags,
                        PvRuntime *runtime,
                        FlatpakExports *exports)
{
  g_return_if_fail (argv != NULL);
  g_return_if_fail (option != NULL);
  g_return_if_fail (preload != NULL);
  g_return_if_fail (runtime == NULL || PV_IS_RUNTIME (runtime));

  if (*preload == '\0')
    return;

  if (strstr (preload, "gtk3-nocsd") != NULL)
    {
      g_warning ("Disabling gtk3-nocsd %s: it is known to cause crashes.",
                 variable);
      return;
    }

  if ((flags & PV_APPEND_PRELOAD_FLAGS_REMOVE_GAME_OVERLAY)
      && g_str_has_suffix (preload, "/gameoverlayrenderer.so"))
    {
      g_info ("Disabling Steam Overlay: %s", preload);
      return;
    }

  append_preload_internal (argv,
                           option,
                           preload,
                           env,
                           flags,
                           runtime,
                           exports);
}
