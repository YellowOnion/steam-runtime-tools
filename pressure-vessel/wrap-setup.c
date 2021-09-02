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
#include "steam-runtime-tools/libdl-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx/libglnx.h"

#include <string.h>

#include "bwrap.h"
#include "flatpak-run-private.h"
#include "flatpak-utils-private.h"
#include "supported-architectures.h"
#include "utils.h"

static gchar *
find_system_bwrap (void)
{
  static const char * const flatpak_libexecdirs[] =
  {
    "/usr/local/libexec",
    "/usr/libexec",
    "/usr/lib/flatpak"
  };
  g_autofree gchar *candidate = NULL;
  gsize i;

  candidate = g_find_program_in_path ("bwrap");

  if (candidate != NULL)
    return g_steal_pointer (&candidate);

  for (i = 0; i < G_N_ELEMENTS (flatpak_libexecdirs); i++)
    {
      candidate = g_build_filename (flatpak_libexecdirs[i],
                                    "flatpak-bwrap", NULL);

      if (g_file_test (candidate, G_FILE_TEST_IS_EXECUTABLE))
        return g_steal_pointer (&candidate);
      else
        g_clear_pointer (&candidate, g_free);
    }

  return NULL;
}

static gboolean
test_bwrap_executable (const char *bwrap_executable,
                       GLogLevelFlags log_level)
{
  const char *bwrap_test_argv[] =
  {
    NULL,
    "--bind", "/", "/",
    "true",
    NULL
  };
  int wait_status;
  g_autofree gchar *child_stdout = NULL;
  g_autofree gchar *child_stderr = NULL;
  g_autoptr(GError) local_error = NULL;
  GError **error = &local_error;

  bwrap_test_argv[0] = bwrap_executable;

  /* We use LEAVE_DESCRIPTORS_OPEN to work around a deadlock in older GLib,
   * see flatpak_close_fds_workaround */
  if (!g_spawn_sync (NULL,  /* cwd */
                     (gchar **) bwrap_test_argv,
                     NULL,  /* environ */
                     G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                     flatpak_bwrap_child_setup_cb, NULL,
                     &child_stdout,
                     &child_stderr,
                     &wait_status,
                     error))
    {
      g_log (G_LOG_DOMAIN, log_level, "Cannot run %s: %s",
             bwrap_executable, local_error->message);
      g_clear_error (&local_error);
      return FALSE;
    }
  else if (wait_status != 0)
    {
      g_log (G_LOG_DOMAIN, log_level, "Cannot run %s: wait status %d",
             bwrap_executable, wait_status);

      if (child_stdout != NULL && child_stdout[0] != '\0')
        g_log (G_LOG_DOMAIN, log_level, "Output:\n%s", child_stdout);

      if (child_stderr != NULL && child_stderr[0] != '\0')
        g_log (G_LOG_DOMAIN, log_level, "Diagnostic output:\n%s", child_stderr);

      return FALSE;
    }
  else
    {
      g_debug ("Successfully ran: %s --bind / / true", bwrap_executable);
      return TRUE;
    }
}

static gchar *
check_bwrap (const char *tools_dir,
             gboolean only_prepare)
{
  g_autofree gchar *local_bwrap = NULL;
  g_autofree gchar *system_bwrap = NULL;
  const char *tmp;

  g_return_val_if_fail (tools_dir != NULL, NULL);

  tmp = g_getenv ("PRESSURE_VESSEL_BWRAP");

  if (tmp == NULL)
    tmp = g_getenv ("BWRAP");

  if (tmp != NULL)
    {
      /* If the user specified an environment variable, then we don't
       * try anything else. */
      g_info ("Using bubblewrap from environment: %s", tmp);

      if (!only_prepare && !test_bwrap_executable (tmp, PV_LOG_LEVEL_FAILURE))
        return NULL;

      return g_strdup (tmp);
    }

  local_bwrap = g_build_filename (tools_dir, "pv-bwrap", NULL);

  /* If our local copy works, use it. If not, keep relatively quiet
   * about it for now - we might need to use a setuid system copy, for
   * example on Debian 10, RHEL 7, Arch linux-hardened kernel. */
  if (only_prepare || test_bwrap_executable (local_bwrap, G_LOG_LEVEL_DEBUG))
    return g_steal_pointer (&local_bwrap);

  g_assert (!only_prepare);
  system_bwrap = find_system_bwrap ();

  /* Try the system copy: if it exists, then it should work, so print failure
   * messages if it doesn't work. */
  if (system_bwrap != NULL
      && test_bwrap_executable (system_bwrap, PV_LOG_LEVEL_FAILURE))
    return g_steal_pointer (&system_bwrap);

  /* If there was no system copy, try the local copy again. We expect
   * this to fail, and are really just doing this to print error messages
   * at the appropriate severity - but if it somehow works, great,
   * I suppose? */
  if (test_bwrap_executable (local_bwrap, PV_LOG_LEVEL_FAILURE))
    {
      g_warning ("Local bwrap executable didn't work first time but "
                 "worked second time?");
      return g_steal_pointer (&local_bwrap);
    }

  return NULL;
}

gchar *
pv_wrap_check_bwrap (const char *tools_dir,
                     gboolean only_prepare)
{
  g_autofree gchar *bwrap = check_bwrap (tools_dir, only_prepare);
  const char *argv[] = { NULL, "--version", NULL };
  struct stat statbuf;

  if (bwrap == NULL)
    return NULL;

  /* We're just running this so that the output ends up in the
   * debug log, so it's OK that the exit status and stdout are ignored. */
  argv[0] = bwrap;
  pv_run_sync (argv, NULL, NULL, NULL, NULL);

  if (stat (bwrap, &statbuf) < 0)
    {
      g_warning ("stat(%s): %s", bwrap, g_strerror (errno));
    }
  else if (statbuf.st_mode & S_ISUID)
    {
      g_info ("Using setuid bubblewrap executable %s (permissions: %o)",
              bwrap, statbuf.st_mode & 07777);
    }

  return g_steal_pointer (&bwrap);
}

/*
 * Use code borrowed from Flatpak to share various bits of the
 * execution environment with the host system, in particular Wayland,
 * X11 and PulseAudio sockets.
 */
void
pv_wrap_share_sockets (FlatpakBwrap *bwrap,
                       PvEnviron *container_env,
                       const GStrv original_environ,
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

  flatpak_run_add_icon_path_args (sharing_bwrap);

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

  pv_wrap_set_icons_env_vars (container_env, original_environ);

  g_warn_if_fail (g_strv_length (sharing_bwrap->envp) == 0);
  flatpak_bwrap_append_bwrap (bwrap, sharing_bwrap);
}

/*
 * Set the environment variables XCURSOR_PATH and XDG_DATA_DIRS to
 * support the icons from the host system.
 */
void
pv_wrap_set_icons_env_vars (PvEnviron *container_env,
                            const GStrv original_environ)
{
  g_autoptr(GString) new_data_dirs = g_string_new ("");
  g_autoptr(GString) new_xcursor_path = g_string_new ("");
  const gchar *initial_xdg_data_dirs = NULL;
  const gchar *original_xcursor_path = NULL;
  const gchar *container_xdg_data_home = NULL;
  g_autofree gchar *data_home_icons = NULL;

  original_xcursor_path = g_environ_getenv (original_environ, "XCURSOR_PATH");
  /* Cursors themes are searched in a few hardcoded paths. However if "XCURSOR_PATH"
   * is set, the user specified paths will override the hardcoded ones.
   * In order to keep the hardcoded paths in place, if "XCURSOR_PATH" is unset, we
   * append the default values first. Reference:
   * https://gitlab.freedesktop.org/xorg/lib/libxcursor/-/blob/80192583/src/library.c#L32 */
  if (original_xcursor_path == NULL)
    {
      /* We assume that this function is called after use_tmpfs_home() or
       * use_fake_home(), if we are going to. */
      container_xdg_data_home = pv_environ_getenv (container_env, "XDG_DATA_HOME");
      if (container_xdg_data_home == NULL)
        container_xdg_data_home = "~/.local/share";
      data_home_icons = g_build_filename (container_xdg_data_home, "icons", NULL);

      /* Note that unlike most path-searching implementations, libXcursor and
       * the derived code in Wayland expand '~' to the home directory. */
      pv_search_path_append (new_xcursor_path, data_home_icons);
      pv_search_path_append (new_xcursor_path, "~/.icons");
      pv_search_path_append (new_xcursor_path, "/usr/share/icons");
      pv_search_path_append (new_xcursor_path, "/usr/share/pixmaps");
      pv_search_path_append (new_xcursor_path, "/usr/X11R6/lib/X11/icons");
    }
  /* Finally append the binded paths from the host */
  pv_search_path_append (new_xcursor_path, "/run/host/user-share/icons");
  pv_search_path_append (new_xcursor_path, "/run/host/share/icons");
  pv_environ_setenv (container_env, "XCURSOR_PATH", new_xcursor_path->str);

  initial_xdg_data_dirs = pv_environ_getenv (container_env, "XDG_DATA_DIRS");
  if (initial_xdg_data_dirs == NULL)
    initial_xdg_data_dirs = g_environ_getenv (original_environ, "XDG_DATA_DIRS");

  /* Reference:
   * https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html */
  if (initial_xdg_data_dirs == NULL)
    initial_xdg_data_dirs = "/usr/local/share:/usr/share";

  /* Append the host "share" directories to "XDG_DATA_DIRS".
   * Currently this is only useful to load the provider's icons */
  pv_search_path_append (new_data_dirs, initial_xdg_data_dirs);
  pv_search_path_append (new_data_dirs, "/run/host/user-share");
  pv_search_path_append (new_data_dirs, "/run/host/share");
  pv_environ_setenv (container_env, "XDG_DATA_DIRS", new_data_dirs->str);
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
                         const char *multiarch_tuple,
                         const char *export_path,
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

      if (multiarch_tuple != NULL)
        g_ptr_array_add (argv, g_strdup_printf ("%s=%s:abi=%s",
                                                option, adjusted_path,
                                                multiarch_tuple));
      else
        g_ptr_array_add (argv, g_strdup_printf ("%s=%s",
                                                option, adjusted_path));
    }
  else
    {
      g_debug ("%s -> unmodified", original_path);

      if (multiarch_tuple != NULL)
        g_ptr_array_add (argv, g_strdup_printf ("%s=%s:abi=%s",
                                                option, original_path,
                                                multiarch_tuple));
      else
        g_ptr_array_add (argv, g_strdup_printf ("%s=%s",
                                                option, original_path));

      if (exports != NULL && export_path != NULL && export_path[0] == '/')
        {
          const gchar *steam_path = g_environ_getenv (env, "STEAM_COMPAT_CLIENT_INSTALL_PATH");

          if (steam_path != NULL
              && flatpak_has_path_prefix (export_path, steam_path))
            {
              g_debug ("Skipping exposing \"%s\" because it is located "
                       "under the Steam client install path that we "
                       "bind by default", export_path);
            }
          else
            {
              g_debug ("%s needs adding to exports", export_path);
              flatpak_exports_add_path_expose (exports,
                                               FLATPAK_FILESYSTEM_MODE_READ_ONLY,
                                               export_path);
            }
        }
    }
}

/*
 * Deal with a LD_PRELOAD or LD_AUDIT module that contains tokens whose
 * expansion we can't control or predict, such as ${ORIGIN} or future
 * additions. We can't do much with these, because we can't assume that
 * the dynamic string tokens will expand in the same way for us as they
 * will for other programs.
 *
 * We mostly have to pass them into the container and hope for the best.
 * We can rewrite a /usr/, /lib or /app/ prefix, and we can export the
 * directory containing the first path component that has a dynamic
 * string token: for example, /opt/plat-${PLATFORM}/preload.so or
 * /opt/$PLATFORM/preload.so both have to be exported as /opt.
 *
 * Arguments are the same as for pv_wrap_append_preload().
 */
static void
append_preload_unsupported_token (GPtrArray *argv,
                                  const char *option,
                                  const char *preload,
                                  GStrv env,
                                  PvAppendPreloadFlags flags,
                                  PvRuntime *runtime,
                                  FlatpakExports *exports)
{
  g_autofree gchar *export_path = NULL;
  char *dollar;
  char *slash;

  g_debug ("Found $ORIGIN or unsupported token in \"%s\"",
           preload);

  if (preload[0] == '/')
    {
      export_path = g_strdup (preload);
      dollar = strchr (export_path, '$');
      g_assert (dollar != NULL);
      /* Truncate before '$' */
      dollar[0] = '\0';
      slash = strrchr (export_path, '/');
      /* It's an absolute path, so there is definitely a '/' before '$' */
      g_assert (slash != NULL);
      /* Truncate before last '/' before '$' */
      slash[0] = '\0';

      /* If that truncation leaves it empty, don't try to expose
       * the whole root filesystem */
      if (export_path[0] != '/')
        {
          g_debug ("Not exporting root filesystem for \"%s\"",
                   preload);
          g_clear_pointer (&export_path, g_free);
        }
      else
        {
          g_debug ("Exporting \"%s\" for \"%s\"",
                   export_path, preload);
        }
    }
  else
    {
      /* Original path was relative and contained an unsupported
       * token like $ORIGIN. Pass it through as-is, without any extra
       * exports (because we don't know what the token means!), and
       * hope for the best. export_path stays NULL. */
      g_debug ("Not exporting \"%s\": not an absolute path, or starts "
               "with $ORIGIN",
               preload);
    }

  append_preload_internal (argv,
                           option,
                           NULL,
                           export_path,
                           preload,
                           env,
                           flags,
                           runtime,
                           exports);
}

/*
 * Deal with a LD_PRELOAD or LD_AUDIT module that contains tokens whose
 * expansion is ABI-dependent but otherwise fixed. We do these by
 * breaking it up into several ABI-dependent LD_PRELOAD modules, which
 * are recombined by pv-adverb. We have to do this because the expansion
 * of the ABI-dependent tokens could be different in the container, due
 * to using a different glibc.
 *
 * Arguments are the same as for pv_wrap_append_preload().
 */
static void
append_preload_per_architecture (GPtrArray *argv,
                                 const char *option,
                                 const char *preload,
                                 GStrv env,
                                 PvAppendPreloadFlags flags,
                                 PvRuntime *runtime,
                                 FlatpakExports *exports)
{
  g_autoptr(SrtSystemInfo) system_info = srt_system_info_new (NULL);
  gsize i;

  if (system_info == NULL)
    system_info = srt_system_info_new (NULL);

  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      g_autoptr(GString) mock_path = NULL;
      g_autoptr(SrtLibrary) details = NULL;
      const char *path;

      srt_system_info_check_library (system_info,
                                     pv_multiarch_details[i].tuple,
                                     preload,
                                     &details);
      path = srt_library_get_absolute_path (details);

      if (flags & PV_APPEND_PRELOAD_FLAGS_IN_UNIT_TESTS)
        {
          /* Use mock results to get predictable behaviour in the unit
           * tests, replacing the real result (above). This avoids needing
           * to have real libraries in place when we do unit testing.
           *
           * tests/pressure-vessel/wrap-setup.c is the other side of this. */
          g_autofree gchar *lib = NULL;
          const char *platform = NULL;

          /* As a mock ${LIB}, behave like Debian or the fdo SDK. */
          lib = g_strdup_printf ("lib/%s", pv_multiarch_details[i].tuple);

          /* As a mock ${PLATFORM}, use the first one listed. */
          platform = pv_multiarch_details[i].platforms[0];

          mock_path = g_string_new (preload);

          if (strchr (preload, '/') == NULL)
            {
              g_string_printf (mock_path, "/path/to/%s/%s", lib, preload);
            }
          else
            {
              g_string_replace (mock_path, "$LIB", lib, 0);
              g_string_replace (mock_path, "${LIB}", lib, 0);
              g_string_replace (mock_path, "$PLATFORM", platform, 0);
              g_string_replace (mock_path, "${PLATFORM}", platform, 0);
            }

          path = mock_path->str;

          /* As a special case, pretend one 64-bit library failed to load,
           * so we can exercise what happens when there's only a 32-bit
           * library available. */
          if (strstr (path, "only-32-bit") != NULL
              && strcmp (pv_multiarch_details[i].tuple,
                         SRT_ABI_I386) != 0)
            path = NULL;
        }

      if (path != NULL)
        {
          g_debug ("Found %s version of %s at %s",
                   pv_multiarch_details[i].tuple, preload, path);
          append_preload_internal (argv,
                                   option,
                                   pv_multiarch_details[i].tuple,
                                   path,
                                   path,
                                   env,
                                   flags,
                                   runtime,
                                   exports);
        }
      else
        {
          g_info ("Unable to load %s version of %s",
                  pv_multiarch_details[i].tuple,
                  preload);
        }
    }
}

static void
append_preload_basename (GPtrArray *argv,
                         const char *option,
                         const char *preload,
                         GStrv env,
                         PvAppendPreloadFlags flags,
                         PvRuntime *runtime,
                         FlatpakExports *exports)
{
  gboolean runtime_has_library = FALSE;

  if (runtime != NULL)
    runtime_has_library = pv_runtime_has_library (runtime, preload);

  if (flags & PV_APPEND_PRELOAD_FLAGS_IN_UNIT_TESTS)
    {
      /* Mock implementation for unit tests: behave as though the
       * container has everything except libfakeroot/libfakechroot. */
      if (g_str_has_prefix (preload, "libfake"))
        runtime_has_library = FALSE;
      else
        runtime_has_library = TRUE;
    }

  if (runtime_has_library)
    {
      /* If the library exists in the container runtime or in the
       * stack we imported from the graphics provider, e.g.
       * LD_PRELOAD=libpthread.so.0, then we certainly don't want
       * to be loading it from the current namespace: that would
       * bypass our logic for comparing library versions and picking
       * the newest. Just pass through the LD_PRELOAD item into the
       * container, and let the dynamic linker in the container choose
       * what it means (container runtime or graphics provider as
       * appropriate). */
      g_debug ("Found \"%s\" in runtime or graphics stack provider, "
               "passing %s through as-is",
               preload, option);
      append_preload_internal (argv,
                               option,
                               NULL,
                               NULL,
                               preload,
                               env,
                               flags,
                               runtime,
                               NULL);
    }
  else
    {
      /* There's no such library in the container runtime or in the
       * graphics provider, so it's OK to inject the version from the
       * current namespace. Use the same trick as for ${PLATFORM} to
       * turn it into (up to) one absolute path per ABI. */
      g_debug ("Did not find \"%s\" in runtime or graphics stack provider, "
               "splitting architectures",
               preload);
      append_preload_per_architecture (argv,
                                       option,
                                       preload,
                                       env,
                                       flags,
                                       runtime,
                                       exports);
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
  SrtLoadableKind kind;
  SrtLoadableFlags loadable_flags;

  g_return_if_fail (argv != NULL);
  g_return_if_fail (option != NULL);
  g_return_if_fail (preload != NULL);
  g_return_if_fail (runtime == NULL || PV_IS_RUNTIME (runtime));

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

  kind = _srt_loadable_classify (preload, &loadable_flags);

  switch (kind)
    {
      case SRT_LOADABLE_KIND_BASENAME:
        /* Basenames can't have dynamic string tokens. */
        g_warn_if_fail ((loadable_flags & SRT_LOADABLE_FLAGS_DYNAMIC_TOKENS) == 0);
        append_preload_basename (argv,
                                 option,
                                 preload,
                                 env,
                                 flags,
                                 runtime,
                                 exports);
        break;

      case SRT_LOADABLE_KIND_PATH:
        /* Paths can have dynamic string tokens. */
        if (loadable_flags & (SRT_LOADABLE_FLAGS_ORIGIN
                              | SRT_LOADABLE_FLAGS_UNKNOWN_TOKENS))
          {
            append_preload_unsupported_token (argv,
                                              option,
                                              preload,
                                              env,
                                              flags,
                                              runtime,
                                              exports);
          }
        else if (loadable_flags & SRT_LOADABLE_FLAGS_ABI_DEPENDENT)
          {
            g_debug ("Found $LIB or $PLATFORM in \"%s\", splitting architectures",
                     preload);
            append_preload_per_architecture (argv,
                                             option,
                                             preload,
                                             env,
                                             flags,
                                             runtime,
                                             exports);
          }
        else
          {
            /* All dynamic tokens should be handled above, so we can
             * assume that preload is a concrete filename */
            g_warn_if_fail ((loadable_flags & SRT_LOADABLE_FLAGS_DYNAMIC_TOKENS) == 0);
            append_preload_internal (argv,
                                     option,
                                     NULL,
                                     preload,
                                     preload,
                                     env,
                                     flags,
                                     runtime,
                                     exports);
          }
        break;

      case SRT_LOADABLE_KIND_ERROR:
      default:
        /* Empty string or similar syntactically invalid token:
         * ignore with a warning. Since steam-runtime-tools!352 and
         * steamlinuxruntime!64, the wrapper scripts don't give us
         * an empty argument any more. */
        g_warning ("Ignoring invalid loadable module \"%s\"", preload);

        break;
    }
}
