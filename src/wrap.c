/* pressure-vessel-wrap — run a program in a container that protects $HOME,
 * optionally using a Flatpak-style runtime.
 *
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

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <locale.h>
#include <stdlib.h>

#include "glib-backports.h"
#include "libglnx/libglnx.h"

#include "bwrap.h"
#include "bwrap-lock.h"
#include "flatpak-bwrap-private.h"
#include "flatpak-utils-base-private.h"
#include "flatpak-utils-private.h"
#include "runtime.h"
#include "utils.h"
#include "wrap-interactive.h"

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
  g_autofree gchar *bwrap_executable = find_bwrap (tools_dir);
  const char *bwrap_test_argv[] =
  {
    NULL,
    "--bind", "/", "/",
    "true",
    NULL
  };

  g_return_val_if_fail (tools_dir != NULL, NULL);

  if (bwrap_executable == NULL)
    {
      g_warning ("Cannot find bwrap");
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

      if (!g_spawn_sync (NULL,  /* cwd */
                         (gchar **) bwrap_test_argv,
                         NULL,  /* environ */
                         G_SPAWN_DEFAULT,
                         NULL, NULL,    /* child setup */
                         &child_stdout,
                         &child_stderr,
                         &wait_status,
                         error))
        {
          g_warning ("Cannot run bwrap: %s", local_error->message);
          g_clear_error (&local_error);
        }
      else if (wait_status != 0)
        {
          g_warning ("Cannot run bwrap: wait status %d", wait_status);

          if (child_stdout != NULL && child_stdout[0] != '\0')
            g_warning ("Output:\n%s", child_stdout);

          if (child_stderr != NULL && child_stderr[0] != '\0')
            g_warning ("Diagnostic output:\n%s", child_stderr);
        }
      else
        {
          return g_steal_pointer (&bwrap_executable);
        }
    }

  return NULL;
}

static void
bind_from_environ (const char *variable,
                   FlatpakBwrap *bwrap)
{
  const char *value = g_getenv (variable);

  if (value == NULL)
    return;

  if (!g_file_test (value, G_FILE_TEST_EXISTS))
    {
      g_debug ("Not bind-mounting %s=\"%s\" because it does not exist",
               variable, value);
      return;
    }

  g_debug ("Bind-mounting %s=\"%s\"", variable, value);

  /* TODO: If it's a symbolic link, ideally we should jump through the
   * same hoops as Flatpak to bind-mount the *target* of the symlink
   * instead, and then create the same symlink in the container. */
  flatpak_bwrap_add_args (bwrap,
                          "--bind", value, value,
                          NULL);
}

/* Order matters here: root, steam and steambeta are or might be symlinks
 * to the root of the Steam installation, so we want to bind-mount their
 * targets before we deal with the rest. */
static const char * const steam_api_subdirs[] =
{
  "root", "steam", "steambeta", "bin", "bin32", "bin64", "sdk32", "sdk64",
};

static gboolean
use_fake_home (FlatpakBwrap *bwrap,
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
  g_autofree gchar *steam_pid = NULL;
  g_autofree gchar *steam_pipe = NULL;
  g_autoptr(GHashTable) mounted = NULL;
  gsize i;

  g_return_val_if_fail (bwrap != NULL, FALSE);
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

  flatpak_bwrap_add_args (bwrap,
                          "--bind", fake_home, real_home,
                          "--bind", fake_home, fake_home,
                          "--bind", tmp, "/var/tmp",
                          "--setenv", "XDG_CACHE_HOME", cache,
                          "--setenv", "XDG_CONFIG_HOME", config,
                          "--setenv", "XDG_DATA_HOME", data,
                          NULL);

  mounted = g_hash_table_new_full (g_str_hash, g_str_equal,
                                   g_free, NULL);

  /*
   * These might be API entry points, according to Steam/steam.sh.
   * They're usually symlinks into the Steam root, except for in
   * older steam Debian packages that had Debian bug #916303.
   *
   * TODO: We probably want to hide part or all of root, steam,
   * steambeta?
   */
  for (i = 0; i < G_N_ELEMENTS (steam_api_subdirs); i++)
    {
      g_autofree gchar *dir = g_build_filename (real_home, ".steam",
                                                steam_api_subdirs[i], NULL);
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

          flatpak_bwrap_add_args (bwrap, "--symlink", target, dir, NULL);

          if (strcmp (steam_api_subdirs[i], "root") == 0
              || strcmp (steam_api_subdirs[i], "steam") == 0
              || strcmp (steam_api_subdirs[i], "steambeta") == 0)
            {
              flatpak_bwrap_add_args (bwrap,
                                      "--ro-bind", target, target,
                                      NULL);
              g_hash_table_add (mounted, g_steal_pointer (&target));
            }
        }
      else if (g_file_test (dir, G_FILE_TEST_EXISTS) &&
               !g_hash_table_contains (mounted, dir))
        {
          flatpak_bwrap_add_args (bwrap, "--ro-bind", dir, dir, NULL);
          g_hash_table_add (mounted, g_steal_pointer (&dir));
        }
    }

  /* steamclient.so relies on this for communication with Steam */
  steam_pid = g_build_filename (real_home, ".steam", "steam.pid", NULL);

  if (g_file_test (steam_pid, G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--ro-bind", steam_pid, steam_pid,
                            NULL);

  /* Make sure Steam IPC is available.
   * TODO: do we need this? do we need more? */
  steam_pipe = g_build_filename (real_home, ".steam", "steam.pipe", NULL);

  if (g_file_test (steam_pipe, G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--bind", steam_pipe, steam_pipe,
                            NULL);

  return TRUE;
}

typedef enum
{
  TRISTATE_NO = 0,
  TRISTATE_YES,
  TRISTATE_MAYBE
} Tristate;

static gboolean opt_batch = FALSE;
static char *opt_copy_runtime_into = NULL;
static char **opt_env_if_host = NULL;
static char *opt_fake_home = NULL;
static char *opt_freedesktop_app_id = NULL;
static char *opt_steam_app_id = NULL;
static gboolean opt_gc_runtimes = TRUE;
static gboolean opt_generate_locales = TRUE;
static char *opt_home = NULL;
static gboolean opt_host_fallback = FALSE;
static gboolean opt_host_graphics = TRUE;
static gboolean opt_only_prepare = FALSE;
static gboolean opt_remove_game_overlay = FALSE;
static PvShell opt_shell = PV_SHELL_NONE;
static GPtrArray *opt_ld_preload = NULL;
static char *opt_runtime_base = NULL;
static char *opt_runtime = NULL;
static Tristate opt_share_home = TRISTATE_MAYBE;
static gboolean opt_share_pid = TRUE;
static gboolean opt_verbose = FALSE;
static gboolean opt_version = FALSE;
static gboolean opt_version_only = FALSE;
static gboolean opt_test = FALSE;
static PvTerminal opt_terminal = PV_TERMINAL_AUTO;

static gboolean
opt_host_ld_preload_cb (const gchar *option_name,
                        const gchar *value,
                        gpointer data,
                        GError **error)
{
  gchar *preload = g_strdup_printf ("host:%s", value);

  if (opt_ld_preload == NULL)
    opt_ld_preload = g_ptr_array_new_with_free_func (g_free);

  g_ptr_array_add (opt_ld_preload, g_steal_pointer (&preload));

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

static GOptionEntry options[] =
{
  { "batch", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_batch,
    "Disable all interactivity and redirection: ignore --shell*, "
    "--terminal, --xterm, --tty. [Default: if $PRESSURE_VESSEL_BATCH]", NULL },
  { "copy-runtime-into", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_copy_runtime_into,
    "If a --runtime is used, copy it into DIR and edit the copy in-place. "
    "[Default: $PRESSURE_VESSEL_COPY_RUNTIME_INTO or empty]",
    "DIR" },
  { "env-if-host", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING_ARRAY, &opt_env_if_host,
    "Set VAR=VAL if COMMAND is run with /usr from the host system, "
    "but not if it is run with /usr from RUNTIME.", "VAR=VAL" },
  { "freedesktop-app-id", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &opt_freedesktop_app_id,
    "Make --unshare-home use ~/.var/app/ID as home directory, where ID "
    "is com.example.MyApp or similar. This interoperates with Flatpak. "
    "[Default: $PRESSURE_VESSEL_FDO_APP_ID if set]",
    "ID" },
  { "steam-app-id", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_STRING, &opt_steam_app_id,
    "Make --unshare-home use ~/.var/app/com.steampowered.AppN "
    "as home directory. [Default: $SteamAppId]", "N" },
  { "gc-runtimes", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_gc_runtimes,
    "If using --copy-runtime-into, garbage-collect old temporary "
    "runtimes. [Default, unless $PRESSURE_VESSEL_GC_RUNTIMES is 0]",
    NULL },
  { "no-gc-runtimes", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_gc_runtimes,
    "If using --copy-runtime-into, don't garbage-collect old "
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
  { "host-fallback", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_host_fallback,
    "Run COMMAND on the host system if we cannot run it in a container.", NULL },
  { "host-ld-preload", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_CALLBACK, &opt_host_ld_preload_cb,
    "Add MODULE from the host system to LD_PRELOAD when executing COMMAND.",
    "MODULE" },
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
  { "runtime", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_runtime,
    "Mount the given sysroot or merged /usr in the container, and augment "
    "it with the host system's graphics stack. The empty string "
    "means don't use a runtime. [Default: $PRESSURE_VESSEL_RUNTIME or '']",
    "RUNTIME" },
  { "runtime-base", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_FILENAME, &opt_runtime_base,
    "If a --runtime is a relative path, look for it relative to BASE. "
    "[Default: $PRESSURE_VESSEL_RUNTIME_BASE or '.']",
    "BASE" },
  { "share-home", '\0',
    G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_share_home_cb,
    "Use the real home directory. "
    "[Default unless $PRESSURE_VESSEL_HOME is set or "
    "$PRESSURE_VESSEL_SHARE_HOME is 0]",
    NULL },
  { "unshare-home", '\0',
    G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_share_home_cb,
    "Use an app-specific home directory chosen according to --home, "
    "--freedesktop-app-id, --steam-app-id or $SteamAppId. "
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
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_host_graphics,
    "If using --runtime, use the host graphics stack (default)", NULL },
  { "without-host-graphics", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_host_graphics,
    "If using --runtime, don't use the host graphics stack. "
    "This is likely to result in software rendering or a crash.",
    NULL },
  { "test", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_test,
    "Smoke test pressure-vessel-wrap and exit.", NULL },
  { "only-prepare", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_only_prepare,
    "Prepare runtime, but do not actually run anything.", NULL },
  { NULL }
};

static gboolean
boolean_environment (const gchar *name,
                     gboolean def)
{
  const gchar *value = g_getenv (name);

  if (g_strcmp0 (value, "1") == 0)
    return TRUE;

  if (g_strcmp0 (value, "") == 0 || g_strcmp0 (value, "0") == 0)
    return FALSE;

  if (value != NULL)
    g_warning ("Unrecognised value \"%s\" for $%s", value, name);

  return def;
}

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

static void
cli_log_func (const gchar *log_domain,
              GLogLevelFlags log_level,
              const gchar *message,
              gpointer user_data)
{
  g_printerr ("%s: %s\n", (const char *) user_data, message);
}

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
  int original_argc = argc;
  g_autoptr(FlatpakBwrap) bwrap = NULL;
  g_autoptr(FlatpakBwrap) wrapped_command = NULL;
  g_autofree gchar *bwrap_executable = NULL;
  g_autoptr(GString) adjusted_ld_preload = g_string_new ("");
  g_autofree gchar *cwd_p = NULL;
  g_autofree gchar *cwd_l = NULL;
  const gchar *home;
  g_autofree gchar *bwrap_help = NULL;
  g_autofree gchar *tools_dir = NULL;
  const gchar *bwrap_help_argv[] = { "<bwrap>", "--help", NULL };
  g_autoptr(PvRuntime) runtime = NULL;

  setlocale (LC_ALL, "");
  pv_avoid_gvfs ();

  g_set_prgname ("pressure-vessel-wrap");

  g_log_set_handler (G_LOG_DOMAIN,
                     G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE,
                     cli_log_func, (void *) g_get_prgname ());

  original_argv = g_new0 (char *, argc + 1);

  for (i = 0; i < argc; i++)
    original_argv[i] = g_strdup (argv[i]);

  if (g_getenv ("STEAM_RUNTIME") != NULL)
    {
      g_printerr ("%s: This program should not be run in the Steam Runtime.",
                  g_get_prgname ());
      g_printerr ("%s: Use pressure-vessel-unruntime instead.",
                  g_get_prgname ());
      ret = 2;
      goto out;
    }

  /* Set defaults */
  opt_batch = boolean_environment ("PRESSURE_VESSEL_BATCH", FALSE);

  opt_freedesktop_app_id = g_strdup (g_getenv ("PRESSURE_VESSEL_FDO_APP_ID"));

  if (opt_freedesktop_app_id != NULL && opt_freedesktop_app_id[0] == '\0')
    g_clear_pointer (&opt_freedesktop_app_id, g_free);

  opt_home = g_strdup (g_getenv ("PRESSURE_VESSEL_HOME"));

  if (opt_home != NULL && opt_home[0] == '\0')
    g_clear_pointer (&opt_home, g_free);

  opt_remove_game_overlay = boolean_environment ("PRESSURE_VESSEL_REMOVE_GAME_OVERLAY",
                                                 FALSE);
  opt_share_home = tristate_environment ("PRESSURE_VESSEL_SHARE_HOME");
  opt_gc_runtimes = boolean_environment ("PRESSURE_VESSEL_GC_RUNTIMES", TRUE);
  opt_generate_locales = boolean_environment ("PRESSURE_VESSEL_GENERATE_LOCALES", TRUE);
  opt_host_graphics = boolean_environment ("PRESSURE_VESSEL_HOST_GRAPHICS",
                                           TRUE);
  opt_share_pid = boolean_environment ("PRESSURE_VESSEL_SHARE_PID", TRUE);
  opt_verbose = boolean_environment ("PRESSURE_VESSEL_VERBOSE", FALSE);

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

  if (opt_runtime == NULL)
    opt_runtime = g_strdup (g_getenv ("PRESSURE_VESSEL_RUNTIME"));

  if (opt_runtime_base == NULL)
    opt_runtime_base = g_strdup (g_getenv ("PRESSURE_VESSEL_RUNTIME_BASE"));

  if (opt_runtime != NULL
      && opt_runtime[0] != '\0'
      && !g_path_is_absolute (opt_runtime)
      && opt_runtime_base != NULL
      && opt_runtime_base[0] != '\0')
    {
      g_autofree gchar *tmp = g_steal_pointer (&opt_runtime);

      opt_runtime = g_build_filename (opt_runtime_base, tmp, NULL);
    }

  if (opt_copy_runtime_into == NULL)
    opt_copy_runtime_into = g_strdup (g_getenv ("PRESSURE_VESSEL_COPY_RUNTIME_INTO"));

  if (opt_copy_runtime_into != NULL
      && opt_copy_runtime_into[0] == '\0')
    opt_copy_runtime_into = NULL;

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

  if (argc < 2 && !opt_test && !opt_only_prepare)
    {
      g_printerr ("%s: An executable to run is required\n",
                  g_get_prgname ());
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
      g_printerr ("%s: --terminal=none is incompatible with --shell\n",
                  g_get_prgname ());
      goto out;
    }

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
  else if (g_getenv ("SteamAppId") != NULL)
    {
      opt_freedesktop_app_id = g_strdup_printf ("com.steampowered.App%s",
                                                g_getenv ("SteamAppId"));
      opt_fake_home = g_build_filename (home, ".var", "app",
                                        opt_freedesktop_app_id, NULL);
    }
  else
    {
      g_printerr ("%s: Either --home, --freedesktop-app-id, --steam-app-id "
                  "or $SteamAppId is required\n",
                  g_get_prgname ());
      goto out;
    }

  if (opt_env_if_host != NULL)
    {
      for (i = 0; opt_env_if_host[i] != NULL; i++)
        {
          const char *equals = strchr (opt_env_if_host[i], '=');

          if (equals == NULL)
            {
              g_printerr ("%s: --env-if-host argument must be of the form "
                          "NAME=VALUE, not \"%s\"\n",
                          g_get_prgname (), opt_env_if_host[i]);
              goto out;
            }
        }
    }

  if (opt_only_prepare && opt_test)
    {
      g_printerr ("%s: --only-prepare and --test are mutually exclusive",
                  g_get_prgname ());
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
      g_auto(GStrv) env = g_get_environ ();

      g_log_set_handler (G_LOG_DOMAIN,
                         G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_INFO,
                         cli_log_func, (void *) g_get_prgname ());

      g_message ("Original argv:");

      for (i = 0; i < original_argc; i++)
        {
          g_autofree gchar *quoted = g_shell_quote (original_argv[i]);

          g_message ("\t%" G_GSIZE_FORMAT ": %s", i, quoted);
        }

      g_message ("Current working directory:");
      g_message ("\tPhysical: %s", cwd_p);
      g_message ("\tLogical: %s", cwd_l);

      g_message ("Environment variables:");

      qsort (env, g_strv_length (env), sizeof (char *), pv_envp_cmp);

      for (i = 0; env[i] != NULL; i++)
        {
          g_autofree gchar *quoted = g_shell_quote (env[i]);

          g_message ("\t%s", quoted);
        }

      g_message ("Wrapped command:");

      for (i = 1; i < argc; i++)
        {
          g_autofree gchar *quoted = g_shell_quote (argv[i]);

          g_message ("\t%" G_GSIZE_FORMAT ": %s", i, quoted);
        }
    }

  tools_dir = find_executable_dir (error);

  if (tools_dir == NULL)
    goto out;

  g_debug ("Found executable directory: %s", tools_dir);

  wrapped_command = flatpak_bwrap_new (NULL);

  switch (opt_terminal)
    {
      case PV_TERMINAL_TTY:
        g_debug ("Wrapping command to use tty");

        if (!pv_bwrap_wrap_tty (wrapped_command, error))
          goto out;

        break;

      case PV_TERMINAL_XTERM:
        g_debug ("Wrapping command with xterm");
        pv_bwrap_wrap_in_xterm (wrapped_command);
        break;

      case PV_TERMINAL_AUTO:
      case PV_TERMINAL_NONE:
      default:
        /* do nothing */
        break;
    }

  if (opt_shell != PV_SHELL_NONE || opt_terminal == PV_TERMINAL_XTERM)
    {
      /* In the (PV_SHELL_NONE, PV_TERMINAL_XTERM) case, just don't let the
       * xterm close before the user has had a chance to see the output */
      pv_bwrap_wrap_interactive (wrapped_command, opt_shell);
    }

  if (argc > 1 && argv[1][0] == '-')
    {
      /* Make sure wrapped_command is something we can validly pass to env(1) */
      if (strchr (argv[1], '=') != NULL)
        flatpak_bwrap_add_args (wrapped_command,
                                "sh", "-euc", "exec \"$@\"", "sh",
                                NULL);

      /* Make sure bwrap will interpret wrapped_command as the end of its
       * options */
      flatpak_bwrap_add_arg (wrapped_command, "env");
    }

  g_debug ("Setting arguments for wrapped command");
  flatpak_bwrap_append_argsv (wrapped_command, &argv[1], argc - 1);

  g_debug ("Checking for bwrap...");
  bwrap_executable = check_bwrap (tools_dir, opt_only_prepare);

  if (opt_test)
    {
      if (bwrap_executable == NULL)
        {
          ret = 1;
          goto out;
        }
      else
        {
          g_debug ("OK (%s)", bwrap_executable);
          ret = 0;
          goto out;
        }
    }

  if (bwrap_executable == NULL)
    {
      if (opt_host_fallback)
        {
          g_message ("Falling back to executing wrapped command directly");

          if (opt_env_if_host != NULL)
            {
              for (i = 0; opt_env_if_host[i] != NULL; i++)
                {
                  char *equals = strchr (opt_env_if_host[i], '=');

                  g_assert (equals != NULL);

                  *equals = '\0';
                  flatpak_bwrap_set_env (wrapped_command, opt_env_if_host[i],
                                         equals + 1, TRUE);
                }
            }

          flatpak_bwrap_finish (wrapped_command);

          /* flatpak_bwrap_finish did this */
          g_assert (g_ptr_array_index (wrapped_command->argv,
                                       wrapped_command->argv->len - 1) == NULL);

          execvpe (g_ptr_array_index (wrapped_command->argv, 0),
                   (char * const *) wrapped_command->argv->pdata,
                   wrapped_command->envp);

          glnx_throw_errno_prefix (error, "execvpe %s",
                                   (gchar *) g_ptr_array_index (wrapped_command->argv, 0));
          goto out;
        }
      else
        {
          goto out;
        }
    }

  g_debug ("Checking bwrap features...");
  bwrap_help_argv[0] = bwrap_executable;
  bwrap_help = pv_capture_output (bwrap_help_argv, error);

  if (bwrap_help == NULL)
    goto out;

  bwrap = flatpak_bwrap_new (NULL);
  flatpak_bwrap_add_arg (bwrap, bwrap_executable);

  /* Protect the controlling terminal from the app/game, unless we are
   * running an interactive shell in which case that would break its
   * job control. */
  if (opt_terminal != PV_TERMINAL_TTY)
    flatpak_bwrap_add_arg (bwrap, "--new-session");

  if (opt_runtime != NULL && opt_runtime[0] != '\0')
    {
      PvRuntimeFlags flags = PV_RUNTIME_FLAGS_NONE;

      if (opt_gc_runtimes)
        flags |= PV_RUNTIME_FLAGS_GC_RUNTIMES;

      if (opt_generate_locales)
        flags |= PV_RUNTIME_FLAGS_GENERATE_LOCALES;

      if (opt_host_graphics)
        flags |= PV_RUNTIME_FLAGS_HOST_GRAPHICS_STACK;

      g_debug ("Configuring runtime %s...", opt_runtime);

      runtime = pv_runtime_new (opt_runtime,
                                opt_copy_runtime_into,
                                bwrap_executable,
                                tools_dir,
                                flags,
                                error);

      if (runtime == NULL)
        goto out;

      if (!pv_runtime_bind (runtime, bwrap, error))
        goto out;
    }
  else
    {
      flatpak_bwrap_add_args (bwrap,
                              "--bind", "/", "/",
                              NULL);
      /* /dev is already visible, because we mounted the entire root
       * filesystem, but we need to remount parts of it without nodev */
      pv_bwrap_add_api_filesystems (bwrap);
    }

  /* Protect other users' homes (but guard against the unlikely
   * situation that they don't exist) */
  if (g_file_test ("/home", G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--tmpfs", "/home",
                            NULL);

  g_debug ("Making home directory available...");

  if (opt_fake_home == NULL)
    {
      flatpak_bwrap_add_args (bwrap,
                              "--bind", home, home,
                              NULL);
    }
  else
    {
      if (!use_fake_home (bwrap, opt_fake_home, error))
        goto out;
    }

  if (!opt_share_pid)
    {
      g_warning ("Unsharing process ID namespace. This is not expected "
                 "to work...");
      flatpak_bwrap_add_arg (bwrap, "--unshare-pid");
    }

  g_debug ("Adjusting LD_PRELOAD...");

  /* We need the LD_PRELOADs from Steam visible at the paths that were
   * used for them, which might be their physical rather than logical
   * locations. */
  if (opt_ld_preload != NULL)
    {
      for (i = 0; i < opt_ld_preload->len; i++)
        {
          const char *preload = g_ptr_array_index (opt_ld_preload, i);

          g_assert (preload != NULL);

          if (*preload == '\0')
            continue;

          /* We have the beginnings of infrastructure to set a LD_PRELOAD
           * from inside the container, but currently the only thing we
           * support is it coming from the host. */
          g_assert (g_str_has_prefix (preload, "host:"));
          preload = preload + 5;

          if (g_file_test (preload, G_FILE_TEST_EXISTS))
            {
              if (opt_remove_game_overlay
                  && g_str_has_suffix (preload, "/gameoverlayrenderer.so"))
                {
                  g_debug ("Disabling Steam Overlay: %s", preload);
                  continue;
                }

              if (runtime != NULL
                  && (g_str_has_prefix (preload, "/usr/")
                      || g_str_has_prefix (preload, "/lib")))
                {
                  g_autofree gchar *in_run_host = g_build_filename ("/run/host",
                                                                    preload,
                                                                    NULL);

                  /* When using a runtime we can't write to /usr/ or /libQUAL/,
                   * so redirect this preloaded module to the corresponding
                   * location in /run/host. */
                  pv_search_path_append (adjusted_ld_preload, in_run_host);
                }
              else
                {
                  flatpak_bwrap_add_args (bwrap,
                                          "--ro-bind", preload, preload,
                                          NULL);
                  pv_search_path_append (adjusted_ld_preload, preload);
                }
            }
          else
            {
              g_debug ("LD_PRELOAD module '%s' does not exist", preload);
            }
        }
    }

  /* Put the caller's LD_PRELOAD back.
   * This would be filtered out by a setuid bwrap, so we have to go
   * via --setenv. */

  if (adjusted_ld_preload->len != 0)
      flatpak_bwrap_add_args (bwrap,
                              "--setenv", "LD_PRELOAD",
                              adjusted_ld_preload->str,
                              NULL);
  else
      flatpak_bwrap_add_args (bwrap,
                              "--unsetenv", "LD_PRELOAD",
                              NULL);

  g_debug ("Making Steam compat tools available if required...");
  bind_from_environ ("STEAM_COMPAT_CLIENT_INSTALL_PATH", bwrap);
  bind_from_environ ("STEAM_COMPAT_DATA_PATH", bwrap);
  bind_from_environ ("STEAM_COMPAT_TOOL_PATH", bwrap);

  /* Make sure the current working directory (the game we are going to
   * run) is available. Some games write here. */
  g_debug ("Making current working directory available...");

  if (pv_is_same_file (home, cwd_p))
    {
      g_debug ("Not making physical working directory \"%s\" available to "
               "container because it is the home directory",
               cwd_p);
    }
  else
    {
      flatpak_bwrap_add_args (bwrap,
                              "--bind", cwd_p, cwd_p,
                              NULL);
    }

  flatpak_bwrap_add_args (bwrap,
                          "--chdir", cwd_p,
                          "--unsetenv", "PWD",
                          NULL);

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

              if (g_str_has_prefix (opt_env_if_host[i], "STEAM_RUNTIME=/"))
                {
                  flatpak_bwrap_add_args (bwrap,
                                          "--ro-bind", equals + 1,
                                          equals + 1,
                                          NULL);
                }

              *equals = '\0';
              /* We do this via --setenv instead of flatpak_bwrap_set_env()
               * to make sure they aren't filtered out by a setuid bwrap. */
              flatpak_bwrap_add_args (bwrap,
                                      "--setenv", opt_env_if_host[i],
                                      equals + 1,
                                      NULL);
              *equals = '=';
            }
        }
    }

  if (opt_verbose)
    {
      g_message ("%s options before bundling:", bwrap_executable);

      for (i = 0; i < bwrap->argv->len; i++)
        {
          g_autofree gchar *quoted = NULL;

          quoted = g_shell_quote (g_ptr_array_index (bwrap->argv, i));
          g_message ("\t%s", quoted);
        }
    }

  if (!flatpak_bwrap_bundle_args (bwrap, 1, -1, FALSE, error))
    goto out;

  if (runtime != NULL)
    pv_runtime_append_adverbs (runtime, bwrap);

  g_debug ("Adding wrapped command...");
  flatpak_bwrap_append_args (bwrap, wrapped_command->argv);

  if (opt_verbose)
    {
      g_message ("Final %s options:", bwrap_executable);

      for (i = 0; i < bwrap->argv->len; i++)
        {
          g_autofree gchar *quoted = NULL;

          quoted = g_shell_quote (g_ptr_array_index (bwrap->argv, i));
          g_message ("\t%s", quoted);
        }

      g_message ("%s environment:", bwrap_executable);

      for (i = 0; bwrap->envp != NULL && bwrap->envp[i] != NULL; i++)
        {
          g_autofree gchar *quoted = NULL;

          quoted = g_shell_quote (bwrap->envp[i]);
          g_message ("\t%s", quoted);
        }
    }

  /* Clean up temporary directory before running our long-running process */
  if (runtime != NULL)
    pv_runtime_cleanup (runtime);

  flatpak_bwrap_finish (bwrap);

  if (opt_only_prepare)
    ret = 0;
  else
    pv_bwrap_execve (bwrap, error);

out:
  if (local_error != NULL)
    g_warning ("%s", local_error->message);

  g_clear_pointer (&opt_ld_preload, g_ptr_array_unref);
  g_clear_pointer (&opt_env_if_host, g_strfreev);
  g_clear_pointer (&opt_freedesktop_app_id, g_free);
  g_clear_pointer (&opt_steam_app_id, g_free);
  g_clear_pointer (&opt_home, g_free);
  g_clear_pointer (&opt_fake_home, g_free);
  g_clear_pointer (&opt_runtime_base, g_free);
  g_clear_pointer (&opt_runtime, g_free);

  return ret;
}
