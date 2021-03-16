/* pressure-vessel-wrap — run a program in a container that protects $HOME,
 * optionally using a Flatpak-style runtime.
 *
 * Contains code taken from Flatpak.
 *
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <locale.h>
#include <stdlib.h>
#include <string.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx/libglnx.h"

#include "bwrap.h"
#include "bwrap-lock.h"
#include "environ.h"
#include "flatpak-bwrap-private.h"
#include "flatpak-run-private.h"
#include "flatpak-utils-base-private.h"
#include "flatpak-utils-private.h"
#include "runtime.h"
#include "utils.h"
#include "wrap-interactive.h"
#include "wrap-setup.h"

/* List of variables that are stripped down from the environment when
 * using the secure-execution mode.
 * List taken from glibc sysdeps/generic/unsecvars.h */
static const char* unsecure_environment_variables[] = {
  "GCONV_PATH",
  "GETCONF_DIR",
  "GLIBC_TUNABLES",
  "HOSTALIASES",
  "LD_AUDIT",
  "LD_DEBUG",
  "LD_DEBUG_OUTPUT",
  "LD_DYNAMIC_WEAK",
  "LD_HWCAP_MASK",
  "LD_LIBRARY_PATH",
  "LD_ORIGIN_PATH",
  "LD_PRELOAD",
  "LD_PROFILE",
  "LD_SHOW_AUXV",
  "LD_USE_LOAD_BIAS",
  "LOCALDOMAIN",
  "LOCPATH",
  "MALLOC_TRACE",
  "NIS_PATH",
  "NLSPATH",
  "RESOLV_HOST_CONF",
  "RES_OPTIONS",
  "TMPDIR",
  "TZDIR",
  NULL,
};

static gchar *
find_executable_dir (GError **error)
{
  g_autofree gchar *target = glnx_readlinkat_malloc (-1, "/proc/self/exe",
                                                     NULL, error);

  if (target == NULL)
    return glnx_prefix_error_null (error, "Unable to resolve /proc/self/exe");

  return g_path_get_dirname (target);
}

static gchar *
find_bwrap (const char *tools_dir)
{
  static const char * const flatpak_libexecdirs[] =
  {
    "/usr/local/libexec",
    "/usr/libexec",
    "/usr/lib/flatpak"
  };
  const char *tmp;
  g_autofree gchar *candidate = NULL;
  gsize i;

  g_return_val_if_fail (tools_dir != NULL, NULL);

  tmp = g_getenv ("BWRAP");

  if (tmp != NULL)
    return g_strdup (tmp);

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

  candidate = g_build_filename (tools_dir, "bwrap", NULL);

  if (g_file_test (candidate, G_FILE_TEST_IS_EXECUTABLE))
    return g_steal_pointer (&candidate);
  else
    g_clear_pointer (&candidate, g_free);

  return NULL;
}

static gchar *
check_bwrap (const char *tools_dir,
             gboolean only_prepare)
{
  g_autoptr(GError) local_error = NULL;
  GError **error = &local_error;
  g_autofree gchar *bwrap_executable = NULL;
  const char *bwrap_test_argv[] =
  {
    NULL,
    "--bind", "/", "/",
    "true",
    NULL
  };

  g_return_val_if_fail (tools_dir != NULL, NULL);

  bwrap_executable = find_bwrap (tools_dir);

  if (bwrap_executable == NULL)
    {
      pv_log_failure ("Cannot find bwrap");
    }
  else if (only_prepare)
    {
      /* With --only-prepare we don't necessarily expect to be able to run
       * it anyway (we are probably in a Docker container that doesn't allow
       * creation of nested user namespaces), so just assume that it's the
       * right one. */
      return g_steal_pointer (&bwrap_executable);
    }
  else
    {
      int wait_status;
      g_autofree gchar *child_stdout = NULL;
      g_autofree gchar *child_stderr = NULL;

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
          pv_log_failure ("Cannot run bwrap: %s", local_error->message);
          g_clear_error (&local_error);
        }
      else if (wait_status != 0)
        {
          pv_log_failure ("Cannot run bwrap: wait status %d", wait_status);

          if (child_stdout != NULL && child_stdout[0] != '\0')
            pv_log_failure ("Output:\n%s", child_stdout);

          if (child_stderr != NULL && child_stderr[0] != '\0')
            pv_log_failure ("Diagnostic output:\n%s", child_stderr);
        }
      else
        {
          return g_steal_pointer (&bwrap_executable);
        }
    }

  return NULL;
}

static gboolean
check_launch_on_host (const char *launch_executable,
                      GError **error)
{
  g_autofree gchar *child_stdout = NULL;
  g_autofree gchar *child_stderr = NULL;
  int wait_status;
  const char *test_argv[] =
  {
    NULL,
    "--bus-name=org.freedesktop.Flatpak",
    "--",
    "true",
    NULL
  };

  test_argv[0] = launch_executable;

  if (!g_spawn_sync (NULL,  /* cwd */
                     (gchar **) test_argv,
                     NULL,  /* environ */
                     G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                     NULL, NULL,    /* child setup */
                     &child_stdout,
                     &child_stderr,
                     &wait_status,
                     error))
    {
      return FALSE;
    }

  if (wait_status != 0)
    {
      pv_log_failure ("Cannot run commands on host system: wait status %d",
                 wait_status);

      if (child_stdout != NULL && child_stdout[0] != '\0')
        pv_log_failure ("Output:\n%s", child_stdout);

      if (child_stderr != NULL && child_stderr[0] != '\0')
        pv_log_failure ("Diagnostic output:\n%s", child_stderr);

      return glnx_throw (error, "Unable to run a command on the host system");
    }

  return TRUE;
}

/* Nvidia Vulkan ray-tracing requires to load the `nvidia_uvm.ko` kernel
 * module, and this is usually done in `libcuda.so.1` by running the setuid
 * binary `nvidia-modprobe`. But when we are inside a container we don't bind
 * `nvidia-modprobe` and, even if we did, its setuid would not be effective
 * because we have `PR_SET_NO_NEW_PRIVS` and we don't have `CAP_SYS_MODULE` in
 * our capability bounding set.
 * For this reason if the current system is using the proprietary Nvidia
 * drivers, and `nvidia_uvm.ko` has not been already loaded, we should execute
 * `nvidia-modprobe` before entering in the container environment. */
static gboolean
maybe_load_nvidia_modules (GError **error)
{
  const char *nvidia_modprobe_argv[] =
  {
    "nvidia-modprobe",
    "-u",
    NULL
  };

  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (g_file_test ("/sys/module/nvidia/version", G_FILE_TEST_IS_REGULAR)
      && !g_file_test ("/sys/module/nvidia_uvm", G_FILE_TEST_IS_DIR))
    return pv_run_sync (nvidia_modprobe_argv, NULL, NULL, NULL, error);

  return TRUE;
}

typedef enum
{
  ENV_MOUNT_FLAGS_COLON_DELIMITED = (1 << 0),
  ENV_MOUNT_FLAGS_DEPRECATED = (1 << 1),
  ENV_MOUNT_FLAGS_READ_ONLY = (1 << 2),
  ENV_MOUNT_FLAGS_NONE = 0
} EnvMountFlags;

typedef struct
{
  const char *name;
  EnvMountFlags flags;
} EnvMount;

static const EnvMount known_required_env[] =
{
    { "PRESSURE_VESSEL_FILESYSTEMS_RO",
      ENV_MOUNT_FLAGS_READ_ONLY | ENV_MOUNT_FLAGS_COLON_DELIMITED },
    { "PRESSURE_VESSEL_FILESYSTEMS_RW", ENV_MOUNT_FLAGS_COLON_DELIMITED },
    { "PROTON_LOG_DIR", ENV_MOUNT_FLAGS_NONE },
    { "STEAM_COMPAT_APP_LIBRARY_PATH", ENV_MOUNT_FLAGS_DEPRECATED },
    { "STEAM_COMPAT_APP_LIBRARY_PATHS",
      ENV_MOUNT_FLAGS_COLON_DELIMITED | ENV_MOUNT_FLAGS_DEPRECATED },
    { "STEAM_COMPAT_CLIENT_INSTALL_PATH", ENV_MOUNT_FLAGS_NONE },
    { "STEAM_COMPAT_DATA_PATH", ENV_MOUNT_FLAGS_NONE },
    { "STEAM_COMPAT_INSTALL_PATH", ENV_MOUNT_FLAGS_NONE },
    { "STEAM_COMPAT_LIBRARY_PATHS", ENV_MOUNT_FLAGS_COLON_DELIMITED },
    { "STEAM_COMPAT_MOUNT_PATHS",
      ENV_MOUNT_FLAGS_COLON_DELIMITED | ENV_MOUNT_FLAGS_DEPRECATED },
    { "STEAM_COMPAT_MOUNTS", ENV_MOUNT_FLAGS_COLON_DELIMITED },
    { "STEAM_COMPAT_SHADER_PATH", ENV_MOUNT_FLAGS_NONE },
    { "STEAM_COMPAT_TOOL_PATH", ENV_MOUNT_FLAGS_DEPRECATED },
    { "STEAM_COMPAT_TOOL_PATHS", ENV_MOUNT_FLAGS_COLON_DELIMITED },
    { "STEAM_EXTRA_COMPAT_TOOLS_PATHS", ENV_MOUNT_FLAGS_COLON_DELIMITED },
};

static void
bind_and_propagate_from_environ (FlatpakExports *exports,
                                 PvEnviron *container_env,
                                 const char *variable,
                                 EnvMountFlags flags)
{
  g_auto(GStrv) values = NULL;
  FlatpakFilesystemMode mode = FLATPAK_FILESYSTEM_MODE_READ_WRITE;
  const char *value;
  const char *before;
  const char *after;
  gboolean changed = FALSE;
  gsize i;

  g_return_if_fail (exports != NULL);
  g_return_if_fail (variable != NULL);

  value = g_getenv (variable);

  if (value == NULL)
    return;

  if (flags & ENV_MOUNT_FLAGS_DEPRECATED)
    g_message ("Setting $%s is deprecated", variable);

  if (flags & ENV_MOUNT_FLAGS_READ_ONLY)
    mode = FLATPAK_FILESYSTEM_MODE_READ_ONLY;

  if (flags & ENV_MOUNT_FLAGS_COLON_DELIMITED)
    {
      values = g_strsplit (value, ":", -1);
      before = "...:";
      after = ":...";
    }
  else
    {
      values = g_new0 (gchar *, 2);
      values[0] = g_strdup (value);
      values[1] = NULL;
      before = "";
      after = "";
    }

  for (i = 0; values[i] != NULL; i++)
    {
      g_autofree gchar *value_host = NULL;
      g_autofree gchar *canon = NULL;

      if (values[i][0] == '\0')
        continue;

      if (!g_file_test (values[i], G_FILE_TEST_EXISTS))
        {
          g_info ("Not bind-mounting %s=\"%s%s%s\" because it does not exist",
                  variable, before, values[i], after);
          continue;
        }

      canon = g_canonicalize_filename (values[i], NULL);
      value_host = pv_current_namespace_path_to_host_path (canon);

      g_info ("Bind-mounting %s=\"%s%s%s\" from the current env as %s=\"%s%s%s\" in the host",
              variable, before, values[i], after,
              variable, before, value_host, after);
      flatpak_exports_add_path_expose (exports, mode, canon);

      if (strcmp (values[i], value_host) != 0)
        {
          g_clear_pointer (&values[i], g_free);
          values[i] = g_steal_pointer (&value_host);
          changed = TRUE;
        }
    }

  if (changed || g_file_test ("/.flatpak-info", G_FILE_TEST_IS_REGULAR))
    {
      g_autofree gchar *joined = g_strjoinv (":", values);

      pv_environ_lock_env (container_env, variable, joined);
    }
}

/* Order matters here: root, steam and steambeta are or might be symlinks
 * to the root of the Steam installation, so we want to bind-mount their
 * targets before we deal with the rest. */
static const char * const steam_api_subdirs[] =
{
  "root", "steam", "steambeta", "bin", "bin32", "bin64", "sdk32", "sdk64",
};

static gboolean expose_steam (FlatpakExports *exports,
                              FlatpakFilesystemMode mode,
                              const char *real_home,
                              const char *fake_home,
                              GError **error);

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

  pv_environ_lock_env (container_env, "XDG_CACHE_HOME", cache);
  pv_environ_lock_env (container_env, "XDG_CONFIG_HOME", config);
  pv_environ_lock_env (container_env, "XDG_DATA_HOME", data);

  flatpak_exports_add_path_expose (exports,
                                   FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                                   fake_home);

  return expose_steam (exports, FLATPAK_FILESYSTEM_MODE_READ_ONLY,
                       real_home, fake_home, error);
}

static gboolean
expose_steam (FlatpakExports *exports,
              FlatpakFilesystemMode mode,
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
  if (fake_home != NULL)
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

/*
 * @bwrap: Arguments produced by flatpak_exports_append_bwrap_args(),
 *  not including an executable name (the 0'th argument must be
 *  `--bind` or similar)
 * @home: The home directory
 *
 * Adjust arguments in @bwrap to cope with potentially running in a
 * container.
 */
static void
adjust_exports (FlatpakBwrap *bwrap,
                const char *home)
{
  gsize i = 0;

  g_debug ("Exported directories:");

  while (i < bwrap->argv->len)
    {
      const char *opt = bwrap->argv->pdata[i];

      g_assert (opt != NULL);

      if (g_str_equal (opt, "--symlink"))
        {
          g_assert (i + 3 <= bwrap->argv->len);
          /* pdata[i + 1] is the target: unchanged. */
          /* pdata[i + 2] is a path in the final container: unchanged. */
          g_debug ("%s %s %s",
                   opt,
                   (const char *) bwrap->argv->pdata[i + 1],
                   (const char *) bwrap->argv->pdata[i + 2]);
          i += 3;
        }
      else if (g_str_equal (opt, "--dir") ||
               g_str_equal (opt, "--tmpfs"))
        {
          g_assert (i + 2 <= bwrap->argv->len);
          /* pdata[i + 1] is a path in the final container: unchanged. */
          g_debug ("%s %s",
                   opt,
                   (const char *) bwrap->argv->pdata[i + 1]);
          i += 2;
        }
      else if (g_str_equal (opt, "--ro-bind") ||
               g_str_equal (opt, "--bind"))
        {
          g_autofree gchar *src = NULL;

          g_assert (i + 3 <= bwrap->argv->len);
          src = g_steal_pointer (&bwrap->argv->pdata[i + 1]);
          /* pdata[i + 2] is a path in the final container: unchanged. */

          /* Paths in the home directory might need adjusting.
           * Paths outside the home directory do not: if they're part of
           * /run/host, they've been adjusted already by
           * flatpak_exports_take_host_fd(), and if not, they appear in
           * the container with the same path as on the host. */
          if (flatpak_has_path_prefix (src, home))
            bwrap->argv->pdata[i + 1] = pv_current_namespace_path_to_host_path (src);
          else
            bwrap->argv->pdata[i + 1] = g_steal_pointer (&src);

          g_debug ("%s %s %s",
                   opt,
                   (const char *) bwrap->argv->pdata[i + 1],
                   (const char *) bwrap->argv->pdata[i + 2]);
          i += 3;
        }
      else
        {
          g_return_if_reached ();
        }
    }
}

typedef enum
{
  TRISTATE_NO = 0,
  TRISTATE_YES,
  TRISTATE_MAYBE
} Tristate;

static gboolean opt_batch = FALSE;
static gboolean opt_copy_runtime = FALSE;
static char **opt_env_if_host = NULL;
static char *opt_fake_home = NULL;
static char **opt_filesystems = NULL;
static char *opt_freedesktop_app_id = NULL;
static char *opt_steam_app_id = NULL;
static gboolean opt_gc_legacy_runtimes = FALSE;
static gboolean opt_gc_runtimes = TRUE;
static gboolean opt_generate_locales = TRUE;
static char *opt_home = NULL;
static char *opt_graphics_provider = NULL;
static char *graphics_provider_mount_point = NULL;
static gboolean opt_launcher = FALSE;
static gboolean opt_only_prepare = FALSE;
static gboolean opt_remove_game_overlay = FALSE;
static gboolean opt_import_vulkan_layers = TRUE;
static PvShell opt_shell = PV_SHELL_NONE;
static GArray *opt_pass_fds = NULL;
static char *opt_runtime = NULL;
static char *opt_runtime_archive = NULL;
static char *opt_runtime_base = NULL;
static char *opt_runtime_id = NULL;
static Tristate opt_share_home = TRISTATE_MAYBE;
static gboolean opt_share_pid = TRUE;
static double opt_terminate_idle_timeout = 0.0;
static double opt_terminate_timeout = -1.0;
static char *opt_variable_dir = NULL;
static gboolean opt_verbose = FALSE;
static gboolean opt_version = FALSE;
static gboolean opt_version_only = FALSE;
static gboolean opt_test = FALSE;
static PvTerminal opt_terminal = PV_TERMINAL_AUTO;
static char *opt_write_final_argv = NULL;

typedef struct
{
  const char *variable;
  GPtrArray *values;
} PreloadModule;

static PreloadModule opt_preload_modules[] =
{
  { "LD_AUDIT", NULL },
  { "LD_PRELOAD", NULL },
};

static gboolean
append_preload_module (const char *variable,
                       const char *value,
                       GError **error)
{
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (opt_preload_modules); i++)
    {
      if (strcmp (variable, opt_preload_modules[i].variable) == 0)
        break;
    }

  g_return_val_if_fail (i < G_N_ELEMENTS (opt_preload_modules), FALSE);

  if (opt_preload_modules[i].values == NULL)
    opt_preload_modules[i].values = g_ptr_array_new_with_free_func (g_free);

  g_ptr_array_add (opt_preload_modules[i].values, g_strdup (value));
  return TRUE;
}

/*
 * check_main_program:
 *
 * Return %TRUE if the command to execute is the main program.
 */
static gboolean
check_main_program (int argc,
                    char *argv[])
{
  return g_getenv ("SteamAppId") != NULL;
}

static gboolean
opt_host_ld_preload_cb (const gchar *option_name,
                        const gchar *value,
                        gpointer data,
                        GError **error)
{
  g_warning ("%s is deprecated, use --ld-preload=%s instead",
             option_name, value);
  return append_preload_module ("LD_PRELOAD", value, error);
}

static gboolean
opt_ld_audit_cb (const gchar *option_name,
                 const gchar *value,
                 gpointer data,
                 GError **error)
{
  return append_preload_module ("LD_AUDIT", value, error);
}

static gboolean
opt_ld_preload_cb (const gchar *option_name,
                   const gchar *value,
                   gpointer data,
                   GError **error)
{
  return append_preload_module ("LD_PRELOAD", value, error);
}

static gboolean
opt_copy_runtime_into_cb (const gchar *option_name,
                          const gchar *value,
                          gpointer data,
                          GError **error)
{
  if (value == NULL)
    {
      opt_copy_runtime = FALSE;
    }
  else if (value[0] == '\0')
    {
      g_warning ("%s is deprecated, disable with --no-copy-runtime instead",
                 option_name);
      opt_copy_runtime = FALSE;
    }
  else
    {
      g_warning ("%s is deprecated, use --copy-runtime and "
                 "--variable-dir instead",
                 option_name);
      opt_copy_runtime = TRUE;
      g_free (opt_variable_dir);
      opt_variable_dir = g_strdup (value);
    }

  return TRUE;
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

  if (opt_pass_fds == NULL)
    opt_pass_fds = g_array_new (FALSE, FALSE, sizeof (int));

  g_array_append_val (opt_pass_fds, fd);
  return TRUE;
}

static gboolean
opt_shell_cb (const gchar *option_name,
              const gchar *value,
              gpointer data,
              GError **error)
{
  if (g_strcmp0 (option_name, "--shell-after") == 0)
    value = "after";
  else if (g_strcmp0 (option_name, "--shell-fail") == 0)
    value = "fail";
  else if (g_strcmp0 (option_name, "--shell-instead") == 0)
    value = "instead";

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
  if (g_strcmp0 (option_name, "--tty") == 0)
    value = "tty";
  else if (g_strcmp0 (option_name, "--xterm") == 0)
    value = "xterm";

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
opt_share_home_cb (const gchar *option_name,
                   const gchar *value,
                   gpointer data,
                   GError **error)
{
  if (g_strcmp0 (option_name, "--share-home") == 0)
    opt_share_home = TRISTATE_YES;
  else if (g_strcmp0 (option_name, "--unshare-home") == 0)
    opt_share_home = TRISTATE_NO;
  else
    g_return_val_if_reached (FALSE);

  return TRUE;
}

static gboolean
opt_with_host_graphics_cb (const gchar *option_name,
                           const gchar *value,
                           gpointer data,
                           GError **error)
{
  /* This is the old way to get the graphics from the host system */
  if (g_strcmp0 (option_name, "--with-host-graphics") == 0)
    {
      if (g_file_test ("/run/host/usr", G_FILE_TEST_IS_DIR)
          && g_file_test ("/run/host/etc", G_FILE_TEST_IS_DIR))
        opt_graphics_provider = g_strdup ("/run/host");
      else
        opt_graphics_provider = g_strdup ("/");
    }
  /* This is the old way to avoid using graphics from the host */
  else if (g_strcmp0 (option_name, "--without-host-graphics") == 0)
    {
      opt_graphics_provider = g_strdup ("");
    }
  else
    {
      g_return_val_if_reached (FALSE);
    }

  g_warning ("\"--with-host-graphics\" and \"--without-host-graphics\" have "
             "been deprecated and could be removed in future releases. Please use "
             "use \"--graphics-provider=/\", \"--graphics-provider=/run/host\" or "
             "\"--graphics-provider=\" instead.");

  return TRUE;
}

static GOptionEntry options[] =
{
  { "batch", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_batch,
    "Disable all interactivity and redirection: ignore --shell*, "
    "--terminal, --xterm, --tty. [Default: if $PRESSURE_VESSEL_BATCH]", NULL },
  { "copy-runtime", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_copy_runtime,
    "If a --runtime is used, copy it into --variable-dir and edit the "
    "copy in-place.",
    NULL },
  { "no-copy-runtime", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_copy_runtime,
    "Don't behave as described for --copy-runtime. "
    "[Default unless $PRESSURE_VESSEL_COPY_RUNTIME is 1 or running in Flatpak]",
    NULL },
  { "copy-runtime-into", '\0',
    G_OPTION_FLAG_FILENAME|G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_CALLBACK,
    &opt_copy_runtime_into_cb,
    "Deprecated alias for --copy-runtime and --variable-dir", "DIR" },
  { "env-if-host", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME_ARRAY, &opt_env_if_host,
    "Set VAR=VAL if COMMAND is run with /usr from the host system, "
    "but not if it is run with /usr from RUNTIME.", "VAR=VAL" },
  { "filesystem", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME_ARRAY, &opt_filesystems,
    "Share filesystem directories with the container. "
    "They must currently be given as absolute paths.",
    "PATH" },
  { "freedesktop-app-id", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &opt_freedesktop_app_id,
    "Make --unshare-home use ~/.var/app/ID as home directory, where ID "
    "is com.example.MyApp or similar. This interoperates with Flatpak. "
    "[Default: $PRESSURE_VESSEL_FDO_APP_ID if set]",
    "ID" },
  { "steam-app-id", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &opt_steam_app_id,
    "Make --unshare-home use ~/.var/app/com.steampowered.AppN "
    "as home directory. [Default: $STEAM_COMPAT_APP_ID or $SteamAppId]",
    "N" },
  { "gc-legacy-runtimes", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_gc_legacy_runtimes,
    "Garbage-collect old unpacked runtimes in $PRESSURE_VESSEL_RUNTIME_BASE.",
    NULL },
  { "no-gc-legacy-runtimes", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_gc_legacy_runtimes,
    "Don't garbage-collect old unpacked runtimes in "
    "$PRESSURE_VESSEL_RUNTIME_BASE [default].",
    NULL },
  { "gc-runtimes", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_gc_runtimes,
    "If using --variable-dir, garbage-collect old temporary "
    "runtimes. [Default, unless $PRESSURE_VESSEL_GC_RUNTIMES is 0]",
    NULL },
  { "no-gc-runtimes", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_gc_runtimes,
    "If using --variable-dir, don't garbage-collect old "
    "temporary runtimes.", NULL },
  { "generate-locales", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_generate_locales,
    "If using --runtime, attempt to generate any missing locales. "
    "[Default, unless $PRESSURE_VESSEL_GENERATE_LOCALES is 0]",
    NULL },
  { "no-generate-locales", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_generate_locales,
    "If using --runtime, don't generate any missing locales.", NULL },
  { "home", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_home,
    "Use HOME as home directory. Implies --unshare-home. "
    "[Default: $PRESSURE_VESSEL_HOME if set]", "HOME" },
  { "host-ld-preload", '\0',
    G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_CALLBACK, &opt_host_ld_preload_cb,
    "Deprecated alias for --ld-preload=MODULE, which despite its name "
    "does not necessarily take the module from the host system",
    "MODULE" },
  { "graphics-provider", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_graphics_provider,
    "If using --runtime, use PATH as the graphics provider. "
    "The path is assumed to be relative to the current namespace, "
    "and will be adjusted for use on the host system if pressure-vessel "
    "is run in a container. The empty string means use the graphics "
    "stack from container."
    "[Default: $PRESSURE_VESSEL_GRAPHICS_PROVIDER or '/']", "PATH" },
  { "launcher", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_launcher,
    "Instead of specifying a command with its arguments to execute, all the "
    "elements after '--' will be used as arguments for "
    "'pressure-vessel-launcher'. All the environment variables that are "
    "edited by pressure-vessel, or that are known to be wrong in the new "
    "container, or that needs to inherit the value from the host system, "
    "will be locked. This option implies --batch.", NULL },
  { "ld-audit", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, &opt_ld_audit_cb,
    "Add MODULE from current execution environment to LD_AUDIT when "
    "executing COMMAND.",
    "MODULE" },
  { "ld-preload", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, &opt_ld_preload_cb,
    "Add MODULE from current execution environment to LD_PRELOAD when "
    "executing COMMAND.",
    "MODULE" },
  { "pass-fd", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_pass_fd_cb,
    "Let the launched process inherit the given fd.",
    NULL },
  { "remove-game-overlay", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_remove_game_overlay,
    "Disable the Steam Overlay. "
    "[Default if $PRESSURE_VESSEL_REMOVE_GAME_OVERLAY is 1]",
    NULL },
  { "keep-game-overlay", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_remove_game_overlay,
    "Do not disable the Steam Overlay. "
    "[Default unless $PRESSURE_VESSEL_REMOVE_GAME_OVERLAY is 1]",
    NULL },
  { "import-vulkan-layers", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_import_vulkan_layers,
    "Import Vulkan layers from the host system. "
    "[Default unless $PRESSURE_VESSEL_IMPORT_VULKAN_LAYERS is 0]",
    NULL },
  { "no-import-vulkan-layers", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_import_vulkan_layers,
    "Do not import Vulkan layers from the host system. Please note that "
    "certain Vulkan layers might still continue to be reachable from inside "
    "the container. This could be the case for all the layers located in "
    " `~/.local/share/vulkan` for example, because we usually share the real "
    "home directory."
    "[Default if $PRESSURE_VESSEL_IMPORT_VULKAN_LAYERS is 0]",
    NULL },
  { "runtime", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_runtime,
    "Mount the given sysroot or merged /usr in the container, and augment "
    "it with the provider's graphics stack. The empty string "
    "means don't use a runtime. [Default: $PRESSURE_VESSEL_RUNTIME or '']",
    "RUNTIME" },
  { "runtime-archive", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_runtime_archive,
    "Unpack the ARCHIVE and use it as the runtime, using --runtime-id to "
    "avoid repeatedly unpacking the same archive. "
    "[Default: $PRESSURE_VESSEL_RUNTIME_ARCHIVE]",
    "ARCHIVE" },
  { "runtime-base", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_runtime_base,
    "If a --runtime or --runtime-archive is a relative path, look for "
    "it relative to BASE. "
    "[Default: $PRESSURE_VESSEL_RUNTIME_BASE or '.']",
    "BASE" },
  { "runtime-id", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_runtime_id,
    "Reuse a previously-unpacked --runtime-archive if its ID matched this",
    "ID" },
  { "share-home", '\0',
    G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_share_home_cb,
    "Use the real home directory. "
    "[Default unless $PRESSURE_VESSEL_HOME is set or "
    "$PRESSURE_VESSEL_SHARE_HOME is 0]",
    NULL },
  { "unshare-home", '\0',
    G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_share_home_cb,
    "Use an app-specific home directory chosen according to --home, "
    "--freedesktop-app-id, --steam-app-id or $STEAM_COMPAT_APP_ID. "
    "[Default if $PRESSURE_VESSEL_HOME is set or "
    "$PRESSURE_VESSEL_SHARE_HOME is 0]",
    NULL },
  { "share-pid", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_share_pid,
    "Do not create a new process ID namespace for the app. "
    "[Default, unless $PRESSURE_VESSEL_SHARE_PID is 0]",
    NULL },
  { "unshare-pid", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_share_pid,
    "Create a new process ID namespace for the app. "
    "[Default if $PRESSURE_VESSEL_SHARE_PID is 0]",
    NULL },
  { "shell", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_shell_cb,
    "--shell=after is equivalent to --shell-after, and so on. "
    "[Default: $PRESSURE_VESSEL_SHELL or 'none']",
    "{none|after|fail|instead}" },
  { "shell-after", '\0',
    G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_shell_cb,
    "Run an interactive shell after COMMAND. Executing \"$@\" in that "
    "shell will re-run COMMAND [ARGS].",
    NULL },
  { "shell-fail", '\0',
    G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_shell_cb,
    "Run an interactive shell after COMMAND, but only if it fails.",
    NULL },
  { "shell-instead", '\0',
    G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_shell_cb,
    "Run an interactive shell instead of COMMAND. Executing \"$@\" in that "
    "shell will run COMMAND [ARGS].",
    NULL },
  { "terminal", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, opt_terminal_cb,
    "none: disable features that would use a terminal; "
    "auto: equivalent to xterm if a --shell option is used, or none; "
    "xterm: put game output (and --shell if used) in an xterm; "
    "tty: put game output (and --shell if used) on Steam's "
    "controlling tty "
    "[Default: $PRESSURE_VESSEL_TERMINAL or 'auto']",
    "{none|auto|xterm|tty}" },
  { "tty", '\0',
    G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_terminal_cb,
    "Equivalent to --terminal=tty", NULL },
  { "xterm", '\0',
    G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_terminal_cb,
    "Equivalent to --terminal=xterm", NULL },
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
  { "variable-dir", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_variable_dir,
    "If a runtime needs to be unpacked or copied, put it in DIR.",
    "DIR" },
  { "verbose", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_verbose,
    "Be more verbose.", NULL },
  { "version", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_version,
    "Print version number and exit.", NULL },
  { "version-only", '\0',
    G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_NONE, &opt_version_only,
    "Print version number (no other information) and exit.", NULL },
  { "with-host-graphics", '\0',
    G_OPTION_FLAG_NO_ARG | G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_CALLBACK,
    opt_with_host_graphics_cb,
    "Deprecated alias for \"--graphics-provider=/\" or "
    "\"--graphics-provider=/run/host\"", NULL },
  { "without-host-graphics", '\0',
    G_OPTION_FLAG_NO_ARG | G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_CALLBACK,
    opt_with_host_graphics_cb,
    "Deprecated alias for \"--graphics-provider=\"", NULL },
  { "write-final-argv", '\0',
    G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_FILENAME, &opt_write_final_argv,
    "Write the final argument vector, as null terminated strings, to the "
    "given file path.", "PATH" },
  { "test", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_test,
    "Smoke test pressure-vessel-wrap and exit.", NULL },
  { "only-prepare", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_only_prepare,
    "Prepare runtime, but do not actually run anything.", NULL },
  { NULL }
};

static Tristate
tristate_environment (const gchar *name)
{
  const gchar *value = g_getenv (name);

  if (g_strcmp0 (value, "1") == 0)
    return TRISTATE_YES;

  if (g_strcmp0 (value, "0") == 0)
    return TRISTATE_NO;

  if (value != NULL && value[0] != '\0')
    g_warning ("Unrecognised value \"%s\" for $%s", value, name);

  return TRISTATE_MAYBE;
}

#define usage_error(...) pv_log_failure (__VA_ARGS__)

int
main (int argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) local_error = NULL;
  GError **error = &local_error;
  int ret = 2;
  gsize i;
  g_auto(GStrv) original_argv = NULL;
  g_auto(GStrv) original_environ = NULL;
  int original_argc = argc;
  gboolean is_flatpak_env = g_file_test ("/.flatpak-info", G_FILE_TEST_IS_REGULAR);
  gboolean is_main_program;
  gboolean search_cwd = FALSE;
  g_autoptr(FlatpakBwrap) flatpak_subsandbox = NULL;
  g_autoptr(FlatpakBwrap) flatpak_run_on_host = NULL;
  g_autoptr(PvEnviron) container_env = NULL;
  g_autoptr(FlatpakBwrap) bwrap = NULL;
  g_autoptr(FlatpakBwrap) bwrap_filesystem_arguments = NULL;
  g_autoptr(FlatpakBwrap) bwrap_home_arguments = NULL;
  g_autoptr(FlatpakBwrap) argv_in_container = NULL;
  g_autoptr(FlatpakBwrap) final_argv = NULL;
  g_autoptr(FlatpakExports) exports = NULL;
  g_autofree gchar *launch_executable = NULL;
  g_autofree gchar *bwrap_executable = NULL;
  g_autofree gchar *cwd_p = NULL;
  g_autofree gchar *cwd_l = NULL;
  g_autofree gchar *cwd_p_host = NULL;
  const gchar *home;
  const gchar *steam_compat_flags;
  g_autofree gchar *tools_dir = NULL;
  g_autoptr(PvRuntime) runtime = NULL;
  g_autoptr(FILE) original_stdout = NULL;
  g_autoptr(GString) lock_env = g_string_new ("");
  g_auto(GLnxTmpfile) lock_env_tmpf  = { 0, };
  g_autofree char *lock_env_fd = NULL;
  g_autoptr(GArray) pass_fds_through_adverb = g_array_new (FALSE, FALSE, sizeof (int));

  setlocale (LC_ALL, "");

  g_set_prgname ("pressure-vessel-wrap");

  /* Set up the initial base logging */
  pv_set_up_logging (FALSE);

  g_info ("pressure-vessel version %s", VERSION);

  original_argv = g_new0 (char *, argc + 1);

  for (i = 0; i < argc; i++)
    original_argv[i] = g_strdup (argv[i]);

  if (g_getenv ("STEAM_RUNTIME") != NULL)
    {
      usage_error ("This program should not be run in the Steam Runtime. "
                   "Use pressure-vessel-unruntime instead.");
      ret = 2;
      goto out;
    }

  original_environ = g_get_environ ();

  if (is_flatpak_env)
    opt_copy_runtime = TRUE;

  is_main_program = check_main_program (argc, argv);

  /* Set defaults */
  opt_batch = pv_boolean_environment ("PRESSURE_VESSEL_BATCH", FALSE);
  /* Process COPY_RUNTIME_INFO first so that COPY_RUNTIME and VARIABLE_DIR
   * can override it */
  opt_copy_runtime_into_cb ("$PRESSURE_VESSEL_COPY_RUNTIME_INTO",
                            g_getenv ("PRESSURE_VESSEL_COPY_RUNTIME_INTO"),
                            NULL, NULL);
  opt_copy_runtime = pv_boolean_environment ("PRESSURE_VESSEL_COPY_RUNTIME",
                                             opt_copy_runtime);
  opt_runtime_id = g_strdup (g_getenv ("PRESSURE_VESSEL_RUNTIME_ID"));

    {
      const char *value = g_getenv ("PRESSURE_VESSEL_VARIABLE_DIR");

      if (value != NULL)
        {
          g_free (opt_variable_dir);
          opt_variable_dir = g_strdup (value);
        }
    }

  opt_freedesktop_app_id = g_strdup (g_getenv ("PRESSURE_VESSEL_FDO_APP_ID"));

  if (opt_freedesktop_app_id != NULL && opt_freedesktop_app_id[0] == '\0')
    g_clear_pointer (&opt_freedesktop_app_id, g_free);

  opt_home = g_strdup (g_getenv ("PRESSURE_VESSEL_HOME"));

  if (opt_home != NULL && opt_home[0] == '\0')
    g_clear_pointer (&opt_home, g_free);

  opt_remove_game_overlay = pv_boolean_environment ("PRESSURE_VESSEL_REMOVE_GAME_OVERLAY",
                                                    FALSE);
  opt_import_vulkan_layers = pv_boolean_environment ("PRESSURE_VESSEL_IMPORT_VULKAN_LAYERS",
                                                     TRUE);

  opt_share_home = tristate_environment ("PRESSURE_VESSEL_SHARE_HOME");
  opt_gc_legacy_runtimes = pv_boolean_environment ("PRESSURE_VESSEL_GC_LEGACY_RUNTIMES", FALSE);
  opt_gc_runtimes = pv_boolean_environment ("PRESSURE_VESSEL_GC_RUNTIMES", TRUE);
  opt_generate_locales = pv_boolean_environment ("PRESSURE_VESSEL_GENERATE_LOCALES", TRUE);

  opt_share_pid = pv_boolean_environment ("PRESSURE_VESSEL_SHARE_PID", TRUE);
  opt_verbose = pv_boolean_environment ("PRESSURE_VESSEL_VERBOSE", FALSE);

  if (!opt_shell_cb ("$PRESSURE_VESSEL_SHELL",
                     g_getenv ("PRESSURE_VESSEL_SHELL"), NULL, error))
    goto out;

  if (!opt_terminal_cb ("$PRESSURE_VESSEL_TERMINAL",
                        g_getenv ("PRESSURE_VESSEL_TERMINAL"), NULL, error))
    goto out;

  context = g_option_context_new ("[--] COMMAND [ARGS]\n"
                                  "Run COMMAND [ARGS] in a container.\n");

  g_option_context_add_main_entries (context, options, NULL);

  if (!g_option_context_parse (context, &argc, &argv, error))
    goto out;

  if (opt_verbose)
    pv_set_up_logging (opt_verbose);

  /* Specifying either one of these mutually-exclusive options as a
   * command-line option disables use of the environment variable for
   * the other one */
  if (opt_runtime == NULL && opt_runtime_archive == NULL)
    {
      opt_runtime = g_strdup (g_getenv ("PRESSURE_VESSEL_RUNTIME"));

      /* Normalize empty string to NULL to simplify later code */
      if (opt_runtime != NULL && opt_runtime[0] == '\0')
        g_clear_pointer (&opt_runtime, g_free);

      opt_runtime_archive = g_strdup (g_getenv ("PRESSURE_VESSEL_RUNTIME_ARCHIVE"));

      if (opt_runtime_archive != NULL && opt_runtime_archive[0] == '\0')
        g_clear_pointer (&opt_runtime_archive, g_free);
    }

  if (opt_runtime_id != NULL)
    {
      const char *p;

      if (opt_runtime_id[0] == '-' || opt_runtime_id[0] == '.')
        {
          usage_error ("--runtime-id must not start with dash or dot");
          goto out;
        }

      for (p = opt_runtime_id; *p != '\0'; p++)
        {
          if (!g_ascii_isalnum (*p) && *p != '_' && *p != '-' && *p != '.')
            {
              usage_error ("--runtime-id may only contain "
                           "alphanumerics, underscore, dash or dot");
              goto out;
            }
        }
    }

  if (opt_runtime_base == NULL)
    opt_runtime_base = g_strdup (g_getenv ("PRESSURE_VESSEL_RUNTIME_BASE"));

  if (opt_runtime != NULL && opt_runtime_archive != NULL)
    {
      usage_error ("--runtime and --runtime-archive cannot both be used");
      goto out;
    }

  if (opt_graphics_provider == NULL)
    opt_graphics_provider = g_strdup (g_getenv ("PRESSURE_VESSEL_GRAPHICS_PROVIDER"));

  if (opt_graphics_provider == NULL)
    {
      /* Also check the deprecated 'PRESSURE_VESSEL_HOST_GRAPHICS' */
      Tristate value = tristate_environment ("PRESSURE_VESSEL_HOST_GRAPHICS");

      if (value == TRISTATE_MAYBE)
        {
          opt_graphics_provider = g_strdup ("/");
        }
      else
        {
          g_warning ("$PRESSURE_VESSEL_HOST_GRAPHICS is deprecated, "
                     "please use PRESSURE_VESSEL_GRAPHICS_PROVIDER instead");

          if (value == TRISTATE_NO)
            opt_graphics_provider = g_strdup ("");
          else if (g_file_test ("/run/host/usr", G_FILE_TEST_IS_DIR)
                   && g_file_test ("/run/host/etc", G_FILE_TEST_IS_DIR))
            opt_graphics_provider = g_strdup ("/run/host");
          else
            opt_graphics_provider = g_strdup ("/");
        }
    }

  g_assert (opt_graphics_provider != NULL);
  if (opt_graphics_provider[0] != '\0' && opt_graphics_provider[0] != '/')
    {
      usage_error ("--graphics-provider path must be absolute, not \"%s\"",
                   opt_graphics_provider);
      goto out;
    }

  if (opt_version_only)
    {
      g_print ("%s\n", VERSION);
      ret = 0;
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

  original_stdout = _srt_divert_stdout_to_stderr (error);

  if (original_stdout == NULL)
    {
      ret = 1;
      goto out;
    }

  _srt_setenv_disable_gio_modules ();

  if (argc < 2 && !opt_test && !opt_only_prepare)
    {
      usage_error ("An executable to run is required");
      goto out;
    }

  if (opt_terminal == PV_TERMINAL_AUTO)
    {
      if (opt_shell != PV_SHELL_NONE)
        opt_terminal = PV_TERMINAL_XTERM;
      else
        opt_terminal = PV_TERMINAL_NONE;
    }

  if (opt_terminal == PV_TERMINAL_NONE && opt_shell != PV_SHELL_NONE)
    {
      usage_error ("--terminal=none is incompatible with --shell");
      goto out;
    }

  /* --launcher implies --batch */
  if (opt_launcher)
    opt_batch = TRUE;

  if (opt_batch)
    {
      /* --batch or PRESSURE_VESSEL_BATCH=1 overrides these */
      opt_shell = PV_SHELL_NONE;
      opt_terminal = PV_TERMINAL_NONE;
    }

  if (argc > 1 && strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  home = g_get_home_dir ();

  if (opt_share_home == TRISTATE_YES)
    {
      opt_fake_home = NULL;
    }
  else if (opt_home)
    {
      opt_fake_home = g_strdup (opt_home);
    }
  else if (opt_share_home == TRISTATE_MAYBE)
    {
      opt_fake_home = NULL;
    }
  else if (opt_freedesktop_app_id)
    {
      opt_fake_home = g_build_filename (home, ".var", "app",
                                        opt_freedesktop_app_id, NULL);
    }
  else if (opt_steam_app_id)
    {
      opt_freedesktop_app_id = g_strdup_printf ("com.steampowered.App%s",
                                                opt_steam_app_id);
      opt_fake_home = g_build_filename (home, ".var", "app",
                                        opt_freedesktop_app_id, NULL);
    }
  else if (g_getenv ("STEAM_COMPAT_APP_ID") != NULL)
    {
      opt_freedesktop_app_id = g_strdup_printf ("com.steampowered.App%s",
                                                g_getenv ("STEAM_COMPAT_APP_ID"));
      opt_fake_home = g_build_filename (home, ".var", "app",
                                        opt_freedesktop_app_id, NULL);
    }
  else if (g_getenv ("SteamAppId") != NULL)
    {
      opt_freedesktop_app_id = g_strdup_printf ("com.steampowered.App%s",
                                                g_getenv ("SteamAppId"));
      opt_fake_home = g_build_filename (home, ".var", "app",
                                        opt_freedesktop_app_id, NULL);
    }
  else
    {
      usage_error ("Either --home, --freedesktop-app-id, --steam-app-id "
                   "or $SteamAppId is required");
      goto out;
    }

  if (opt_env_if_host != NULL)
    {
      for (i = 0; opt_env_if_host[i] != NULL; i++)
        {
          const char *equals = strchr (opt_env_if_host[i], '=');

          if (equals == NULL)
            {
              usage_error ("--env-if-host argument must be of the form "
                           "NAME=VALUE, not \"%s\"", opt_env_if_host[i]);
              goto out;
            }
        }
    }

  if (opt_only_prepare && opt_test)
    {
      usage_error ("--only-prepare and --test are mutually exclusive");
      goto out;
    }

  if (opt_filesystems != NULL)
    {
      for (i = 0; opt_filesystems[i] != NULL; i++)
        {
          if (strchr (opt_filesystems[i], ':') != NULL ||
              strchr (opt_filesystems[i], '\\') != NULL)
            {
              usage_error ("':' and '\\' in --filesystem argument "
                           "not handled yet");
              goto out;
            }
          else if (!g_path_is_absolute (opt_filesystems[i]))
            {
              usage_error ("--filesystem argument must be an absolute "
                           "path, not \"%s\"", opt_filesystems[i]);
              goto out;
            }
        }
    }

  if (opt_copy_runtime && opt_variable_dir == NULL)
    {
      usage_error ("--copy-runtime requires --variable-dir");
      goto out;
    }

  /* Finished parsing arguments, so any subsequent failures will make
   * us exit 1. */
  ret = 1;

  if (opt_terminal != PV_TERMINAL_TTY)
    {
      int fd;

      if (!glnx_openat_rdonly (-1, "/dev/null", TRUE, &fd, error))
          goto out;

      if (dup2 (fd, STDIN_FILENO) < 0)
        {
          glnx_throw_errno_prefix (error,
                                   "Cannot replace stdin with /dev/null");
          goto out;
        }
    }

  pv_get_current_dirs (&cwd_p, &cwd_l);

  if (opt_verbose)
    {
      g_auto(GStrv) env = g_strdupv (original_environ);

      g_debug ("Original argv:");

      for (i = 0; i < original_argc; i++)
        {
          g_autofree gchar *quoted = g_shell_quote (original_argv[i]);

          g_debug ("\t%" G_GSIZE_FORMAT ": %s", i, quoted);
        }

      g_debug ("Current working directory:");
      g_debug ("\tPhysical: %s", cwd_p);
      g_debug ("\tLogical: %s", cwd_l);

      g_debug ("Environment variables:");

      qsort (env, g_strv_length (env), sizeof (char *), pv_envp_cmp);

      for (i = 0; env[i] != NULL; i++)
        {
          g_autofree gchar *quoted = g_shell_quote (env[i]);

          g_debug ("\t%s", quoted);
        }

      if (opt_launcher)
        g_debug ("Arguments for pv-launcher:");
      else
        g_debug ("Wrapped command:");

      for (i = 1; i < argc; i++)
        {
          g_autofree gchar *quoted = g_shell_quote (argv[i]);

          g_debug ("\t%" G_GSIZE_FORMAT ": %s", i, quoted);
        }
    }

  tools_dir = find_executable_dir (error);

  if (tools_dir == NULL)
    goto out;

  g_debug ("Found executable directory: %s", tools_dir);

  /* If we are in a Flatpak environment we can't use bwrap directly */
  if (is_flatpak_env)
    {

      launch_executable = g_build_filename (tools_dir,
                                            "pressure-vessel-launch",
                                            NULL);
      /* Assume "bwrap" to exist in the host system and to be in its PATH */
      bwrap_executable = g_strdup ("bwrap");

      /* Deliberately not documented: only people who are in a position
       * to run their own modified versions of Flatpak and pressure-vessel
       * should be using this, and those people can find this in the
       * source code */
      if (g_getenv ("PRESSURE_VESSEL_FLATPAK_PR4018") != NULL)
        {
          g_warning ("Assuming your version of Flatpak contains unmerged "
                     "changes (#4018, #4125, #4126, #4093)");

          /* Use a sub-sandbox */
          flatpak_subsandbox = flatpak_bwrap_new (flatpak_bwrap_empty_env);
          flatpak_bwrap_add_arg (flatpak_subsandbox,
                                 launch_executable);
          /* Tell pressure-vessel-launch to send its whole environment
           * to the subsandbox, except for the parts that we edit later.
           * This effectively matches bwrap's behaviour. */
          flatpak_bwrap_add_arg (flatpak_subsandbox, "--pass-env-matching=*");
          flatpak_bwrap_add_arg (flatpak_subsandbox,
                                 "--bus-name=org.freedesktop.portal.Flatpak");
        }
      /* Also deliberately not documented */
      else if (g_getenv ("PRESSURE_VESSEL_FLATPAK_SANDBOX_ESCAPE") != NULL)
        {
          g_warning ("Assuming permissions have been set to allow Steam "
                     "to escape from the Flatpak sandbox");

          /* If we have permission to escape from the sandbox, we'll do that,
           * and launch bwrap that way */
          flatpak_run_on_host = flatpak_bwrap_new (flatpak_bwrap_empty_env);
          flatpak_bwrap_add_arg (flatpak_run_on_host,
                                 launch_executable);
          flatpak_bwrap_add_arg (flatpak_run_on_host,
                                 "--bus-name=org.freedesktop.Flatpak");

          /* If we can't launch a command on the host, just fail. */
          if (!check_launch_on_host (launch_executable, error))
            goto out;
        }
      else
        {
          glnx_throw (error,
                      "pressure-vessel (SteamLinuxRuntime) cannot be run "
                      "in a Flatpak environment. For Proton 5.13+, "
                      "unofficial community builds that do not use "
                      "pressure-vessel are available.");
          goto out;
        }
    }
  else
    {
      g_debug ("Checking for bwrap...");

      /* if this fails, it will warn */
      bwrap_executable = check_bwrap (tools_dir, opt_only_prepare);

      if (bwrap_executable == NULL)
        goto out;

      g_debug ("OK (%s)", bwrap_executable);
    }

  if (opt_test)
    {
      ret = 0;
      goto out;
    }

  /* Invariant: we are in exactly one of these three modes */
  g_assert (((flatpak_subsandbox != NULL)
             + (flatpak_run_on_host != NULL)
             + (!is_flatpak_env))
            == 1);

  if (flatpak_subsandbox == NULL)
    {
      /* Start with an empty environment and populate it later */
      bwrap = flatpak_bwrap_new (flatpak_bwrap_empty_env);
      flatpak_bwrap_add_arg (bwrap, bwrap_executable);
      bwrap_filesystem_arguments = flatpak_bwrap_new (flatpak_bwrap_empty_env);
      exports = flatpak_exports_new ();
    }

  /* Invariant: we have bwrap or exports iff we also have the other */
  g_assert ((bwrap != NULL) == (exports != NULL));
  g_assert ((bwrap != NULL) == (bwrap_filesystem_arguments != NULL));

  container_env = pv_environ_new ();

  if (bwrap != NULL)
    {
      g_assert (exports != NULL);
      g_assert (bwrap_filesystem_arguments != NULL);

      if (g_strcmp0 (opt_graphics_provider, "/") == 0)
        graphics_provider_mount_point = g_strdup ("/run/host");
      else
        graphics_provider_mount_point = g_strdup ("/run/gfx");

      if (is_flatpak_env)
        {
          glnx_autofd int fd = TEMP_FAILURE_RETRY (open ("/run/host",
                                                         O_CLOEXEC | O_PATH));

          if (fd < 0)
            {
              glnx_throw_errno_prefix (error, "Unable to open /run/host");
              goto out;
            }

          flatpak_exports_take_host_fd (exports, glnx_steal_fd (&fd));
        }

      /* Protect the controlling terminal from the app/game, unless we are
       * running an interactive shell in which case that would break its
       * job control. */
      if (opt_terminal != PV_TERMINAL_TTY)
        flatpak_bwrap_add_arg (bwrap, "--new-session");

      /* Start with just the root tmpfs (which appears automatically)
       * and the standard API filesystems */
      pv_bwrap_add_api_filesystems (bwrap_filesystem_arguments);

      /* The FlatpakExports will populate /run/host for us */
      flatpak_exports_add_host_etc_expose (exports,
                                           FLATPAK_FILESYSTEM_MODE_READ_ONLY);
      flatpak_exports_add_host_os_expose (exports,
                                          FLATPAK_FILESYSTEM_MODE_READ_ONLY);

      /* steam-runtime-system-info uses this to detect pressure-vessel, so we
       * need to create it even if it will be empty */
      flatpak_bwrap_add_args (bwrap_filesystem_arguments,
                              "--dir",
                              "/run/pressure-vessel",
                              NULL);
    }
  else
    {
      g_assert (flatpak_subsandbox != NULL);

      if (g_strcmp0 (opt_graphics_provider, "/") == 0)
        {
          graphics_provider_mount_point = g_strdup ("/run/parent");
        }
      else if (g_strcmp0 (opt_graphics_provider, "/run/host") == 0)
        {
          g_warning ("Using host graphics drivers in a Flatpak subsandbox "
                     "probably won't work");
          graphics_provider_mount_point = g_strdup ("/run/host");
        }
      else
        {
          glnx_throw (error,
                      "Flatpak subsandboxing can only use / or /run/host "
                      "to provide graphics drivers");
          goto out;
        }
    }

  if (opt_gc_legacy_runtimes
      && opt_runtime_base != NULL
      && opt_runtime_base[0] != '\0'
      && opt_variable_dir != NULL)
    {
      if (!pv_runtime_garbage_collect_legacy (opt_variable_dir,
                                              opt_runtime_base,
                                              &local_error))
        {
          g_warning ("Unable to clean up old runtimes: %s",
                     local_error->message);
          g_clear_error (&local_error);
        }
    }

  steam_compat_flags = g_getenv ("STEAM_COMPAT_FLAGS");
  if (steam_compat_flags != NULL)
    {
      g_auto(GStrv) flags = g_strsplit (steam_compat_flags, ",", -1);
      for (i = 0; flags[i] != NULL; i++)
        {
          if (g_strcmp0 (flags[i], "search-cwd") == 0)
            /* This option is used to append the game install path to
             * LD_LIBRARY_PATH for legacy purposes, to cope with games that
             * relied on the old behaviour of LD_LIBRARY_PATH of ending with
             * a colon, which ld.so interprets as the current working directory. */
            search_cwd = TRUE;
          else
            g_info ("STEAM_COMPAT_FLAGS has the unexpected flag \"%s\"", flags[i]);
        }
    }

  if (opt_runtime != NULL || opt_runtime_archive != NULL)
    {
      PvRuntimeFlags flags = PV_RUNTIME_FLAGS_NONE;
      g_autofree gchar *runtime_resolved = NULL;
      const char *runtime_path = NULL;

      if (opt_gc_runtimes)
        flags |= PV_RUNTIME_FLAGS_GC_RUNTIMES;

      if (opt_generate_locales)
        flags |= PV_RUNTIME_FLAGS_GENERATE_LOCALES;

      if (opt_graphics_provider != NULL && opt_graphics_provider[0] != '\0')
        flags |= PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK;

      if (opt_verbose)
        flags |= PV_RUNTIME_FLAGS_VERBOSE;

      if (opt_import_vulkan_layers)
        flags |= PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS;

      if (opt_copy_runtime)
        flags |= PV_RUNTIME_FLAGS_COPY_RUNTIME;

      if (flatpak_subsandbox != NULL)
        flags |= PV_RUNTIME_FLAGS_FLATPAK_SUBSANDBOX;

      /* Only if we are running the main program, append the working directory
       * of the game to LD_LIBRARY_PATH. This option is not intended for the
       * setup phase. */
      if (search_cwd && is_main_program)
        flags |= PV_RUNTIME_FLAGS_SEARCH_CWD;

      if (opt_runtime != NULL)
        {
          /* already checked for mutually exclusive options */
          g_assert (opt_runtime_archive == NULL);
          runtime_path = opt_runtime;
        }
      else
        {
          flags |= PV_RUNTIME_FLAGS_UNPACK_ARCHIVE;
          runtime_path = opt_runtime_archive;
        }

      if (!g_path_is_absolute (runtime_path)
          && opt_runtime_base != NULL
          && opt_runtime_base[0] != '\0')
        {
          runtime_resolved = g_build_filename (opt_runtime_base,
                                               runtime_path, NULL);
          runtime_path = runtime_resolved;
        }

      g_debug ("Configuring runtime %s...", runtime_path);

      if (is_flatpak_env && !opt_copy_runtime)
        {
          glnx_throw (error,
                      "Cannot set up a runtime inside Flatpak without "
                      "making a mutable copy");
          goto out;
        }

      runtime = pv_runtime_new (runtime_path,
                                opt_runtime_id,
                                opt_variable_dir,
                                bwrap_executable,
                                tools_dir,
                                opt_graphics_provider,
                                graphics_provider_mount_point,
                                original_environ,
                                flags,
                                error);

      if (runtime == NULL)
        goto out;

      if (!pv_runtime_bind (runtime,
                            exports,
                            bwrap_filesystem_arguments,
                            container_env,
                            error))
        goto out;

      if (flatpak_subsandbox != NULL)
        {
          const char *usr = pv_runtime_get_modified_usr (runtime);

          flatpak_bwrap_add_args (flatpak_subsandbox,
                                  "--app-path=",
                                  "--share-pids",
                                  "--usr-path", usr,
                                  NULL);
        }
    }
  else if (flatpak_subsandbox != NULL)
    {
      /* Nothing special to do here: we'll just create the subsandbox
       * without changing the runtime, which means we inherit the
       * Flatpak's normal runtime. */
    }
  else if (flatpak_run_on_host != NULL)
    {
      glnx_throw (error,
                  "Cannot operate without a runtime when escaping from "
                  "a Flatpak app");
      goto out;
    }
  else
    {
      g_assert (!is_flatpak_env);
      g_assert (bwrap != NULL);
      g_assert (bwrap_filesystem_arguments != NULL);
      g_assert (exports != NULL);

      if (!pv_wrap_use_host_os (exports, bwrap_filesystem_arguments, error))
        goto out;
    }

  /* Protect other users' homes (but guard against the unlikely
   * situation that they don't exist). We use the FlatpakExports for this
   * so that it can be overridden by --filesystem=/home, and so that it
   * is sorted correctly with respect to all the other
   * home-directory-related exports. */
  if (exports != NULL
      && g_file_test ("/home", G_FILE_TEST_EXISTS))
    flatpak_exports_add_path_tmpfs (exports, "/home");

  g_debug ("Making home directory available...");

  if (flatpak_subsandbox != NULL)
    {
      if (opt_fake_home == NULL)
        {
          /* Nothing special to do here: we'll use the same home directory
           * and exports that the parent Flatpak sandbox used. */
        }
      else if (flatpak_subsandbox != NULL)
        {
          /* Not yet supported */
          glnx_throw (error,
                      "Cannot use a game-specific home directory in a "
                      "Flatpak subsandbox");
          goto out;
        }
    }
  else
    {
      g_assert (flatpak_run_on_host != NULL || !is_flatpak_env);
      g_assert (bwrap != NULL);
      g_assert (bwrap_filesystem_arguments != NULL);
      g_assert (exports != NULL);

      if (opt_fake_home == NULL)
        {
          flatpak_exports_add_path_expose (exports,
                                           FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                                           home);
          /* We always export /tmp for now (see below) and it seems odd
           * to share /tmp with the host, but not /var/tmp. */
          flatpak_exports_add_path_expose (exports,
                                           FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                                           "/var/tmp");

          /* TODO: All of ~/.steam has traditionally been read/write when not
           * using a per-game home directory, but does it need to be? Maybe we
           * should have a future "compat level" in which it's read-only,
           * like it already is when using a per-game home directory. */
          if (!expose_steam (exports, FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                             home, NULL, error))
            goto out;
        }
      else
        {
          bwrap_home_arguments = flatpak_bwrap_new (flatpak_bwrap_empty_env);

          if (!use_fake_home (exports, bwrap_home_arguments,
                              container_env, opt_fake_home,
                              error))
            goto out;
        }
    }

  if (!opt_share_pid)
    {
      if (bwrap != NULL)
        {
          g_warning ("Unsharing process ID namespace. This is not expected "
                     "to work...");
          flatpak_bwrap_add_arg (bwrap, "--unshare-pid");
        }
      else
        {
          g_assert (flatpak_subsandbox != NULL);
          /* pressure-vessel-launch currently hard-codes this */
          g_warning ("Process ID namespace is always shared when using a "
                     "Flatpak subsandbox");
        }
    }

  if (exports != NULL)
    /* Always export /tmp for now. SteamVR uses this as a rendezvous
     * directory for IPC. */
    flatpak_exports_add_path_expose (exports,
                                     FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                                     "/tmp");

  /* We need the LD_PRELOADs from Steam visible at the paths that were
   * used for them, which might be their physical rather than logical
   * locations. Steam doesn't generally use LD_AUDIT, but the Steam app
   * on Flathub does, and it needs similar handling. */
  for (i = 0; i < G_N_ELEMENTS (opt_preload_modules); i++)
    {
      const char *variable = opt_preload_modules[i].variable;
      const GPtrArray *values = opt_preload_modules[i].values;
      g_autoptr(GString) adjusted = NULL;
      gsize len;
      gsize j;

      adjusted = g_string_new ("");

      g_debug ("Adjusting %s...", variable);

      if (values == NULL)
        len = 0;
      else
        len = values->len;

      for (j = 0; j < len; j++)
        {
          const char *preload = g_ptr_array_index (values, j);

          g_assert (preload != NULL);

          if (*preload == '\0')
            continue;

          if (strstr (preload, "gtk3-nocsd") != NULL)
            {
              g_warning ("Disabling gtk3-nocsd %s: it is known to cause crashes.",
                         variable);
              continue;
            }

          if (opt_remove_game_overlay
              && g_str_has_suffix (preload, "/gameoverlayrenderer.so"))
            {
              g_info ("Disabling Steam Overlay: %s", preload);
              continue;
            }

          /* A subsandbox will just have the same LD_PRELOAD as the
           * Flatpak itself, except that we have to redirect /usr and /app
           * into /run/parent. */
          if (flatpak_subsandbox != NULL)
            {
              if (runtime != NULL
                  && (g_str_has_prefix (preload, "/usr/")
                      || g_str_has_prefix (preload, "/app/")
                      || g_str_has_prefix (preload, "/lib")))
                {
                  g_autofree gchar *adjusted_path = NULL;

                  adjusted_path = g_build_filename ("/run/parent", preload, NULL);
                  g_debug ("%s -> %s", preload, adjusted_path);
                  pv_search_path_append (adjusted, adjusted_path);
                }
              else
                {
                  g_debug ("%s -> unmodified", preload);
                  pv_search_path_append (adjusted, preload);
                }

              /* No FlatpakExports here: any file not in /usr or /app that
               * is visible to our "parent" Flatpak app is also visible
               * to us. */
              continue;
            }

          if (g_file_test (preload, G_FILE_TEST_EXISTS))
            {
              if (runtime != NULL
                  && (g_str_has_prefix (preload, "/usr/")
                      || g_str_has_prefix (preload, "/lib")))
                {
                  g_autofree gchar *adjusted_path = NULL;

                  adjusted_path = g_build_filename ("/run/host", preload, NULL);
                  g_debug ("%s -> %s", preload, adjusted_path);
                  /* When using a runtime we can't write to /usr/ or /libQUAL/,
                   * so redirect this preloaded module to the corresponding
                   * location in /run/host. */
                  pv_search_path_append (adjusted, adjusted_path);
                }
              else
                {
                  const gchar *steam_path = NULL;
                  steam_path = g_getenv ("STEAM_COMPAT_CLIENT_INSTALL_PATH");

                  if (steam_path != NULL && flatpak_has_path_prefix (preload, steam_path))
                    {
                      g_debug ("Skipping exposing \"%s\" because it is located "
                               "under the Steam client install path that we "
                               "bind by default", preload);
                    }
                  else
                    {
                      g_debug ("%s -> unmodified, but added to exports", preload);
                      flatpak_exports_add_path_expose (exports,
                                                       FLATPAK_FILESYSTEM_MODE_READ_ONLY,
                                                       preload);
                    }

                  pv_search_path_append (adjusted, preload);
                }
            }
          else
            {
              g_info ("%s module '%s' does not exist", variable, preload);
            }
        }

      if (bwrap != NULL)
        {
          /* If we adjusted the module paths from the one provided by the host
           * to something that is valid in the container, we shouldn't add them to
           * the bwrap envp. Otherwise when we call "pv_bwrap_execve()" we will
           * create an environment that tries to preload libraries that are not
           * available, until it actually executes "bwrap".
           * This can be avoided by using the bwrap option "--setenv" instead. */
          if (adjusted->len != 0)
            flatpak_bwrap_add_args (bwrap, "--setenv", variable,
                                    adjusted->str, NULL);
        }
      else
        {
          if (adjusted->len != 0)
            pv_environ_lock_env (container_env, variable, adjusted->str);
          else
            pv_environ_lock_env (container_env, variable, NULL);
        }
    }

  if (flatpak_subsandbox == NULL)
    {
      g_assert (bwrap != NULL);
      g_assert (bwrap_filesystem_arguments != NULL);
      g_assert (exports != NULL);

      g_debug ("Making Steam environment variables available if required...");

      for (i = 0; i < G_N_ELEMENTS (known_required_env); i++)
        bind_and_propagate_from_environ (exports, container_env,
                                         known_required_env[i].name,
                                         known_required_env[i].flags);

      /* On NixOS, all paths hard-coded into libraries are in here */
      flatpak_exports_add_path_expose (exports,
                                       FLATPAK_FILESYSTEM_MODE_READ_ONLY,
                                       "/nix");

      /* Make arbitrary filesystems available. This is not as complete as
       * Flatpak yet. */
      if (opt_filesystems != NULL)
        {
          g_debug ("Processing --filesystem arguments...");

          for (i = 0; opt_filesystems[i] != NULL; i++)
            {
              /* We already checked this */
              g_assert (g_path_is_absolute (opt_filesystems[i]));

              g_info ("Bind-mounting \"%s\"", opt_filesystems[i]);
              flatpak_exports_add_path_expose (exports,
                                               FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                                               opt_filesystems[i]);
            }
        }

      /* Make sure the current working directory (the game we are going to
       * run) is available. Some games write here. */
      g_debug ("Making current working directory available...");

      cwd_p_host = pv_current_namespace_path_to_host_path (cwd_p);

      if (_srt_is_same_file (home, cwd_p))
        {
          g_info ("Not making physical working directory \"%s\" available to "
                  "container because it is the home directory",
                  cwd_p);
        }
      else
        {
          /* If in Flatpak, we assume that cwd_p_host is visible in the
           * current namespace as well as in the host, because it's
           * either in our ~/.var/app/$FLATPAK_ID, or a --filesystem that
           * was exposed from the host. */
          flatpak_exports_add_path_expose (exports,
                                           FLATPAK_FILESYSTEM_MODE_READ_WRITE,
                                           cwd_p_host);
        }

      flatpak_bwrap_add_args (bwrap,
                              "--chdir", cwd_p_host,
                              NULL);
    }
  else
    {
      for (i = 0; i < G_N_ELEMENTS (known_required_env); i++)
        pv_environ_lock_env (container_env,
                             known_required_env[i].name,
                             g_getenv (known_required_env[i].name));

      flatpak_bwrap_add_args (flatpak_subsandbox,
                              "--directory", cwd_p,
                              NULL);
    }

  pv_environ_lock_env (container_env, "PWD", NULL);

  /* Put Steam Runtime environment variables back, if /usr is mounted
   * from the host. */
  if (runtime == NULL)
    {
      g_debug ("Making Steam Runtime available...");

      /* We need libraries from the Steam Runtime, so make sure that's
       * visible (it should never need to be read/write though) */
      if (opt_env_if_host != NULL)
        {
          for (i = 0; opt_env_if_host[i] != NULL; i++)
            {
              char *equals = strchr (opt_env_if_host[i], '=');

              g_assert (equals != NULL);

              if (exports != NULL
                  && g_str_has_prefix (opt_env_if_host[i], "STEAM_RUNTIME=/"))
                flatpak_exports_add_path_expose (exports,
                                                 FLATPAK_FILESYSTEM_MODE_READ_ONLY,
                                                 equals + 1);

              *equals = '\0';

              pv_environ_lock_env (container_env, opt_env_if_host[i],
                                   equals + 1);

              *equals = '=';
            }
        }
    }

  /* Convert the exported directories into extra bubblewrap arguments */
  if (exports != NULL)
    {
      g_autoptr(FlatpakBwrap) exports_bwrap =
        flatpak_bwrap_new (flatpak_bwrap_empty_env);

      g_assert (bwrap != NULL);
      g_assert (bwrap_filesystem_arguments != NULL);

      if (bwrap_home_arguments != NULL)
        {
          /* The filesystem arguments to set up a fake $HOME (if any) have
           * to come before the exports, as they do in Flatpak, so that
           * mounting the fake $HOME will not mask the exports used for
           * ~/.steam, etc. */
          g_warn_if_fail (g_strv_length (bwrap_home_arguments->envp) == 0);
          flatpak_bwrap_append_bwrap (bwrap, bwrap_home_arguments);
          g_clear_pointer (&bwrap_home_arguments, flatpak_bwrap_free);
        }

      flatpak_exports_append_bwrap_args (exports, exports_bwrap);
      adjust_exports (exports_bwrap, home);
      g_warn_if_fail (g_strv_length (exports_bwrap->envp) == 0);
      flatpak_bwrap_append_bwrap (bwrap, exports_bwrap);

      /* The other filesystem arguments have to come after the exports
       * so that if the exports set up symlinks, the other filesystem
       * arguments like --dir work with the symlinks' targets. */
      g_warn_if_fail (g_strv_length (bwrap_filesystem_arguments->envp) == 0);
      flatpak_bwrap_append_bwrap (bwrap, bwrap_filesystem_arguments);
      g_clear_pointer (&bwrap_filesystem_arguments, flatpak_bwrap_free);
    }

  if (bwrap != NULL)
    pv_wrap_share_sockets (bwrap, container_env,
                           (runtime != NULL),
                           is_flatpak_env);

  if (pv_environ_is_locked(container_env, "PULSE_SERVER")
      || is_flatpak_env)
    {
      /* Make the PulseAudio driver the default.
       * We do this unconditionally when we are under Flatpak for parity
       * with the freedesktop.org Platform. */
      const gchar *alsa_config = "pcm.!default pulse\n"
                                 "ctl.!default pulse\n";

      flatpak_bwrap_add_args_data (bwrap, "99-pulseaudio-default.conf",
                                   alsa_config, -1,
                                   "/etc/alsa/conf.d/99-pulseaudio-default.conf",
                                   NULL);
    }

  if (is_flatpak_env)
    {
      g_autoptr(GList) vars = NULL;
      const GList *iter;

      if (flatpak_run_on_host != NULL)
        {
          /* These are the environment variables that will be wrong, or useless,
           * in the new container that will be created by escaping from the
           * sandbox. Force them to be unset. */
          pv_environ_lock_env (container_env, "FLATPAK_ID", NULL);
          pv_environ_lock_env (container_env, "FLATPAK_SANDBOX_DIR", NULL);
        }
      else
        {
          /* Let these inherit from the sub-sandbox environment */
          pv_environ_lock_inherit_env (container_env, "FLATPAK_ID");
          pv_environ_lock_inherit_env (container_env, "FLATPAK_SANDBOX_DIR");
        }

      /* These are the environment variables that might differ in the host
       * system. However from inside a container we are not able to know the
       * host's value. So we allow them to inherit the value from the host.
       * Similarly, if we're starting a sub-sandbox, they should take the
       * sub-sandbox's value if different. */
      pv_environ_lock_inherit_env (container_env, "DBUS_SESSION_BUS_ADDRESS");
      pv_environ_lock_inherit_env (container_env, "DBUS_SYSTEM_BUS_ADDRESS");
      pv_environ_lock_inherit_env (container_env, "DISPLAY");
      pv_environ_lock_inherit_env (container_env, "XDG_RUNTIME_DIR");

      /* The bwrap envp will be completely ignored when calling
       * pv-launch, and in fact putting them in its environment
       * variables would be wrong, because pv-launch needs to see the
       * current execution environment's DBUS_SESSION_BUS_ADDRESS
       * (if different). For this reason we convert them to `--setenv`. */
      vars = pv_environ_get_vars (container_env);

      for (iter = vars; iter != NULL; iter = iter->next)
        {
          const char *var = iter->data;
          const char *val = pv_environ_getenv (container_env, var);

          if (flatpak_subsandbox != NULL)
            {
              if (val != NULL)
                flatpak_bwrap_add_arg_printf (flatpak_subsandbox,
                                              "--env=%s=%s",
                                              var, val);
              else
                flatpak_bwrap_add_args (flatpak_subsandbox,
                                        "--unset-env", var,
                                        NULL);
            }
          else
            {
              g_assert (bwrap != NULL);

              if (val != NULL)
                flatpak_bwrap_add_args (bwrap,
                                        "--setenv", var, val,
                                        NULL);
              else
                flatpak_bwrap_add_args (bwrap,
                                        "--unsetenv", var,
                                        NULL);
            }
        }
    }

  final_argv = flatpak_bwrap_new (original_environ);

  /* Lock variables where appropriate */
    {
      g_autoptr(GList) vars = pv_environ_get_locked (container_env);
      const GList *iter;

      for (iter = vars; iter != NULL; iter = iter->next)
        {
          const char *var = iter->data;

          g_debug ("Locking environment variable: %s", var);
          g_string_append (lock_env, var);
          g_string_append_c (lock_env, '\0');
        }
    }

  /* Populate final_argv->envp, overwriting its copy of original_environ.
   * We skip this if we are in a Flatpak environment, because in that case
   * we already used `--setenv` for all the variables that we care about and
   * the final_argv->envp will be ignored anyway, other than as a way to
   * invoke pv-launch (for which original_environ is appropriate). */
  if (!is_flatpak_env)
    {
      g_autoptr(GList) vars = NULL;
      const GList *iter;

      vars = pv_environ_get_vars (container_env);

      for (iter = vars; iter != NULL; iter = iter->next)
        {
          const char *var = iter->data;
          const char *val = pv_environ_getenv (container_env, var);

          if (val != NULL)
            flatpak_bwrap_set_env (final_argv, var, val, TRUE);
          else
            flatpak_bwrap_unset_env (final_argv, var);
        }

      /* The setuid bwrap will filter out some of the environment variables,
       * so we still have to go via --setenv for these. */
      for (i = 0; unsecure_environment_variables[i] != NULL; i++)
        {
          const char *var = unsecure_environment_variables[i];
          const gchar *val = pv_environ_getenv (container_env, var);

          if (val != NULL)
            flatpak_bwrap_add_args (bwrap, "--setenv", var, val, NULL);
        }
    }

  /* Now that we've populated final_argv->envp, it's too late to change
   * any environment variables. Make sure we get an assertion failure
   * if we try. */
  g_clear_pointer (&container_env, pv_environ_free);

  if (opt_launcher)
    {
      if (!flatpak_buffer_to_sealed_memfd_or_tmpfile (&lock_env_tmpf, "lock-env",
                                                      lock_env->str, lock_env->len,
                                                      error))
        goto out;

      lock_env_fd = g_strdup_printf ("%d", lock_env_tmpf.fd);
    }

  if (flatpak_run_on_host != NULL)
    {
      int userns_fd, userns2_fd, pidns_fd;

      /* Tell the bwrap instance on the host to join the same user and pid
       * namespaces as Steam in Flatpak. Otherwise, pid-based IPC between
       * the Steam client and the game will fail.
       *
       * This is not expected to work if bwrap on the host is setuid,
       * so it will not work for users of Debian, Arch linux-hardened, etc.,
       * but it's better than nothing. */
      userns_fd = open ("/run/.userns", O_RDONLY | O_CLOEXEC);

      if (userns_fd >= 0)
        {
          g_array_append_val (pass_fds_through_adverb, userns_fd);
          flatpak_bwrap_add_args_data_fd (bwrap, "--userns",
                                          glnx_steal_fd (&userns_fd),
                                          NULL);

          userns2_fd = open_namespace_fd_if_needed ("/proc/self/ns/user",
                                                    "/run/.userns");

          if (userns2_fd >= 0)
            {
              g_array_append_val (pass_fds_through_adverb, userns2_fd);
              flatpak_bwrap_add_args_data_fd (bwrap, "--userns2",
                                              glnx_steal_fd (&userns2_fd),
                                              NULL);
            }
        }

      pidns_fd = open ("/proc/self/ns/pid", O_RDONLY | O_CLOEXEC);

      if (pidns_fd >= 0)
        {
          g_array_append_val (pass_fds_through_adverb, pidns_fd);
          flatpak_bwrap_add_args_data_fd (bwrap, "--pidns",
                                          glnx_steal_fd (&pidns_fd),
                                          NULL);
        }
    }

  if (bwrap != NULL)
    {
      /* Tell the application that it's running under a container manager
       * in a generic way (based on https://systemd.io/CONTAINER_INTERFACE/,
       * although a lot of that document is intended for "system"
       * containers and is less suitable for "app" containers like
       * Flatpak and pressure-vessel). */
      flatpak_bwrap_add_args (bwrap,
                              "--setenv", "container", "pressure-vessel",
                              NULL);
      if (!flatpak_bwrap_add_args_data (bwrap,
                                        "container-manager",
                                        "pressure-vessel\n", -1,
                                        "/run/host/container-manager",
                                        error))
        return FALSE;


      if (opt_verbose)
        {
          g_debug ("%s options before bundling:", bwrap_executable);

          for (i = 0; i < bwrap->argv->len; i++)
            {
              g_autofree gchar *quoted = NULL;

              quoted = g_shell_quote (g_ptr_array_index (bwrap->argv, i));
              g_debug ("\t%s", quoted);
            }
        }

      if (!opt_only_prepare)
        {
          if (!flatpak_bwrap_bundle_args (bwrap, 1, -1, FALSE, error))
            goto out;
        }
    }

  argv_in_container = flatpak_bwrap_new (flatpak_bwrap_empty_env);

  /* Set up adverb inside container */
    {
      g_autoptr(FlatpakBwrap) adverb_argv = NULL;

      adverb_argv = flatpak_bwrap_new (flatpak_bwrap_empty_env);

      if (runtime != NULL)
        {
          if (!pv_runtime_get_adverb (runtime, adverb_argv))
            goto out;
        }
      else
        {
          /* If not using a runtime, the adverb in the container has the
           * same path as outside */
          g_autofree gchar *adverb_in_container =
            g_build_filename (tools_dir, "pressure-vessel-adverb", NULL);

          flatpak_bwrap_add_arg (adverb_argv, adverb_in_container);
        }

      if (opt_terminate_timeout >= 0.0)
        {
          char idle_buf[G_ASCII_DTOSTR_BUF_SIZE] = {};
          char timeout_buf[G_ASCII_DTOSTR_BUF_SIZE] = {};

          g_ascii_dtostr (idle_buf, sizeof (idle_buf),
                          opt_terminate_idle_timeout);
          g_ascii_dtostr (timeout_buf, sizeof (timeout_buf),
                          opt_terminate_timeout);

          if (opt_terminate_idle_timeout > 0.0)
            flatpak_bwrap_add_arg_printf (adverb_argv,
                                          "--terminate-idle-timeout=%s",
                                          idle_buf);

          flatpak_bwrap_add_arg_printf (adverb_argv,
                                        "--terminate-timeout=%s",
                                        timeout_buf);
        }

      flatpak_bwrap_add_args (adverb_argv,
                              "--exit-with-parent",
                              "--subreaper",
                              NULL);

      if (opt_pass_fds != NULL)
        {
          for (i = 0; i < opt_pass_fds->len; i++)
            {
              int fd = g_array_index (opt_pass_fds, int, i);

              flatpak_bwrap_add_fd (adverb_argv, fd);
              flatpak_bwrap_add_arg_printf (adverb_argv, "--pass-fd=%d", fd);
            }
        }

      for (i = 0; i < pass_fds_through_adverb->len; i++)
        {
          int fd = g_array_index (pass_fds_through_adverb, int, i);

          flatpak_bwrap_add_arg_printf (adverb_argv, "--pass-fd=%d", fd);
        }

      if (lock_env_fd != NULL)
        flatpak_bwrap_add_arg_printf (adverb_argv, "--pass-fd=%s", lock_env_fd);

      switch (opt_shell)
        {
          case PV_SHELL_AFTER:
            flatpak_bwrap_add_arg (adverb_argv, "--shell=after");
            break;

          case PV_SHELL_FAIL:
            flatpak_bwrap_add_arg (adverb_argv, "--shell=fail");
            break;

          case PV_SHELL_INSTEAD:
            flatpak_bwrap_add_arg (adverb_argv, "--shell=instead");
            break;

          case PV_SHELL_NONE:
            flatpak_bwrap_add_arg (adverb_argv, "--shell=none");
            break;

          default:
            g_warn_if_reached ();
        }

      switch (opt_terminal)
        {
          case PV_TERMINAL_AUTO:
            flatpak_bwrap_add_arg (adverb_argv, "--terminal=auto");
            break;

          case PV_TERMINAL_NONE:
            flatpak_bwrap_add_arg (adverb_argv, "--terminal=none");
            break;

          case PV_TERMINAL_TTY:
            flatpak_bwrap_add_arg (adverb_argv, "--terminal=tty");
            break;

          case PV_TERMINAL_XTERM:
            flatpak_bwrap_add_arg (adverb_argv, "--terminal=xterm");
            break;

          default:
            g_warn_if_reached ();
            break;
        }

      if (opt_verbose)
        flatpak_bwrap_add_arg (adverb_argv, "--verbose");

      flatpak_bwrap_add_arg (adverb_argv, "--");

      g_warn_if_fail (g_strv_length (adverb_argv->envp) == 0);
      flatpak_bwrap_append_bwrap (argv_in_container, adverb_argv);
    }

  if (opt_launcher)
    {
      g_autoptr(FlatpakBwrap) launcher_argv =
        flatpak_bwrap_new (flatpak_bwrap_empty_env);
      g_autofree gchar *pressure_vessel_launcher = g_build_filename (tools_dir,
                                                                     "pressure-vessel-launcher",
                                                                     NULL);
      g_debug ("Adding pressure-vessel-launcher '%s'...", pressure_vessel_launcher);
      flatpak_bwrap_add_arg (launcher_argv, pressure_vessel_launcher);

      if (opt_verbose)
        flatpak_bwrap_add_arg (launcher_argv, "--verbose");

      g_debug ("Adding locked environment variables...");
      g_assert (lock_env_fd != NULL);
      g_assert (lock_env_tmpf.fd >= 0);
      flatpak_bwrap_add_fd (launcher_argv, glnx_steal_fd (&lock_env_tmpf.fd));
      flatpak_bwrap_add_args (launcher_argv,
                              "--lock-env-from-fd", lock_env_fd, NULL);

      /* In --launcher mode, arguments after the "--" separator are
       * passed to the launcher */
      flatpak_bwrap_append_argsv (launcher_argv, &argv[1], argc - 1);

      g_warn_if_fail (g_strv_length (launcher_argv->envp) == 0);
      flatpak_bwrap_append_bwrap (argv_in_container, launcher_argv);
    }
  else
    {
      /* In non-"--launcher" mode, arguments after the "--" separator
       * are the command to execute, passed to the adverb after "--".
       * Because we always use the adverb, we don't need to worry about
       * whether argv[1] starts with "-". */
      g_debug ("Setting arguments for wrapped command");
      flatpak_bwrap_append_argsv (argv_in_container, &argv[1], argc - 1);
    }

  if (flatpak_subsandbox != NULL)
    {
      for (i = 0; i < argv_in_container->fds->len; i++)
        {
          g_autofree char *fd_str = g_strdup_printf ("--forward-fd=%d",
                                                     g_array_index (argv_in_container->fds, int, i));
          flatpak_bwrap_add_arg (flatpak_subsandbox, fd_str);
        }

      flatpak_bwrap_add_arg (flatpak_subsandbox, "--");

      g_warn_if_fail (g_strv_length (flatpak_subsandbox->envp) == 0);
      flatpak_bwrap_append_bwrap (final_argv, flatpak_subsandbox);
    }

  if (flatpak_run_on_host != NULL)
    {
      g_assert (bwrap != NULL);

      for (i = 0; i < bwrap->fds->len; i++)
        {
          g_autofree char *fd_str = g_strdup_printf ("--forward-fd=%d",
                                                     g_array_index (bwrap->fds, int, i));
          flatpak_bwrap_add_arg (flatpak_run_on_host, fd_str);
        }

      for (i = 0; i < argv_in_container->fds->len; i++)
        {
          g_autofree char *fd_str = g_strdup_printf ("--forward-fd=%d",
                                                     g_array_index (argv_in_container->fds, int, i));
          flatpak_bwrap_add_arg (flatpak_run_on_host, fd_str);
        }

      /* Change the current working directory where pv-launch will run bwrap.
       * Bwrap will then set its directory by itself. For this reason here
       * we just need a directory that it's known to exist. */
      flatpak_bwrap_add_arg (flatpak_run_on_host, "--directory=/");

      flatpak_bwrap_add_arg (flatpak_run_on_host, "--");

      g_warn_if_fail (g_strv_length (flatpak_run_on_host->envp) == 0);
      flatpak_bwrap_append_bwrap (final_argv, flatpak_run_on_host);
    }

  if (flatpak_subsandbox == NULL)
    {
      g_warn_if_fail (g_strv_length (bwrap->envp) == 0);
      flatpak_bwrap_append_bwrap (final_argv, bwrap);
    }

  g_warn_if_fail (g_strv_length (argv_in_container->envp) == 0);
  flatpak_bwrap_append_bwrap (final_argv, argv_in_container);

  /* We'll have permuted the order anyway, so we might as well sort it,
   * to make debugging a bit easier. */
  if (final_argv->envp != NULL)
    qsort (final_argv->envp, g_strv_length (final_argv->envp),
           sizeof (char *), pv_envp_cmp);

  if (opt_verbose)
    {
      g_debug ("Final command to execute:");

      for (i = 0; i < final_argv->argv->len; i++)
        {
          g_autofree gchar *quoted = NULL;

          quoted = g_shell_quote (g_ptr_array_index (final_argv->argv, i));
          g_debug ("\t%s", quoted);
        }

      g_debug ("Final environment:");

      for (i = 0; final_argv->envp != NULL && final_argv->envp[i] != NULL; i++)
        {
          g_autofree gchar *quoted = NULL;

          quoted = g_shell_quote (final_argv->envp[i]);
          g_debug ("\t%s", quoted);
        }
    }

  /* Clean up temporary directory before running our long-running process */
  if (runtime != NULL)
    pv_runtime_cleanup (runtime);

  flatpak_bwrap_finish (final_argv);

  if (opt_write_final_argv != NULL)
    {
      FILE *file = fopen (opt_write_final_argv, "w");
      if (file == NULL)
        {
          g_warning ("An error occurred trying to write out the arguments: %s",
                    g_strerror (errno));
          /* This is not a fatal error, try to continue */
        }
      else
        {
          for (i = 0; i < final_argv->argv->len; i++)
            fprintf (file, "%s%c",
                     (gchar *) g_ptr_array_index (final_argv->argv, i), '\0');

          fclose (file);
        }
    }

  if (!is_flatpak_env)
    {
      if (!maybe_load_nvidia_modules (error))
        {
          g_debug ("Cannot load nvidia modules: %s", local_error->message);
          g_clear_error (&local_error);
        }
    }

  if (opt_only_prepare)
    ret = 0;
  else
    pv_bwrap_execve (final_argv, fileno (original_stdout), error);

out:
  if (local_error != NULL)
    pv_log_failure ("%s", local_error->message);

  for (i = 0; i < G_N_ELEMENTS (opt_preload_modules); i++)
    g_clear_pointer (&opt_preload_modules[i].values, g_ptr_array_unref);

  g_clear_pointer (&opt_env_if_host, g_strfreev);
  g_clear_pointer (&opt_freedesktop_app_id, g_free);
  g_clear_pointer (&opt_steam_app_id, g_free);
  g_clear_pointer (&opt_home, g_free);
  g_clear_pointer (&opt_fake_home, g_free);
  g_clear_pointer (&opt_runtime, g_free);
  g_clear_pointer (&opt_runtime_archive, g_free);
  g_clear_pointer (&opt_runtime_base, g_free);
  g_clear_pointer (&opt_runtime_id, g_free);
  g_clear_pointer (&opt_pass_fds, g_array_unref);
  g_clear_pointer (&opt_variable_dir, g_free);

  g_debug ("Exiting with status %d", ret);
  return ret;
}
