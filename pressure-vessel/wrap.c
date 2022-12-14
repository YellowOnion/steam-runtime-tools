/* pressure-vessel-wrap — run a program in a container that protects $HOME,
 * optionally using a Flatpak-style runtime.
 *
 * Contains code taken from Flatpak.
 *
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
#include "steam-runtime-tools/log-internal.h"
#include "steam-runtime-tools/profiling-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

#include "bwrap.h"
#include "bwrap-lock.h"
#include "environ.h"
#include "flatpak-bwrap-private.h"
#include "flatpak-run-private.h"
#include "flatpak-utils-base-private.h"
#include "flatpak-utils-private.h"
#include "graphics-provider.h"
#include "runtime.h"
#include "supported-architectures.h"
#include "utils.h"
#include "wrap-flatpak.h"
#include "wrap-home.h"
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

      if (flatpak_has_path_prefix (canon, "/overrides"))
        {
          g_warning_once ("The path \"/overrides/\" is reserved and cannot be shared");
          continue;
        }

      if (flatpak_has_path_prefix (canon, "/usr"))
        g_warning_once ("Binding directories that are located under \"/usr/\" is not supported!");

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

      pv_environ_setenv (container_env, variable, joined);
    }
}

/*
 * @to: The exported files and directories of @from will be adjusted and
  appended to this FlatpakBwrap
 * @from: Arguments produced by flatpak_exports_append_bwrap_args(),
 *  not including an executable name (the 0'th argument must be
 *  `--bind` or similar)
 * @home: The home directory
 * @interpreter_root (nullable): Path to the interpreter root, or %NULL if
 *  there isn't one
 * @error: Used to return an error on failure
 *
 * Adjust arguments in @from to cope with potentially running in a
 * container or interpreter and append them to @to.
 * This function will steal the fds of @from.
 */
static gboolean
append_adjusted_exports (FlatpakBwrap *to,
                         FlatpakBwrap *from,
                         const char *home,
                         const char *interpreter_root,
                         PvBwrapFlags bwrap_flags,
                         GError **error)
{
  g_autofree int *fds = NULL;
  glnx_autofd int interpreter_fd = -1;
  glnx_autofd int root_fd = -1;
  /* Bypass FEX-Emu transparent rewrite by using
   * "/proc/self/root" as the root path. */
  const gchar *root = "/proc/self/root";
  gsize n_fds;
  gsize i;

  g_return_val_if_fail (to != NULL, FALSE);
  g_return_val_if_fail (from != NULL, FALSE);
  g_return_val_if_fail (home != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  fds = flatpak_bwrap_steal_fds (from, &n_fds);
  for (i = 0; i < n_fds; i++)
    flatpak_bwrap_add_fd (to, fds[i]);

  if (interpreter_root != NULL)
    {
      if (!glnx_opendirat (-1, root, TRUE, &root_fd, error))
        return FALSE;

      if (!glnx_opendirat (-1, interpreter_root, TRUE, &interpreter_fd, error))
        return FALSE;
    }

  g_debug ("Exported directories:");

  for (i = 0; i < from->argv->len;)
    {
      const char *opt = from->argv->pdata[i];

      g_assert (opt != NULL);

      if (g_str_equal (opt, "--bind-data") ||
          g_str_equal (opt, "--chmod") ||
          g_str_equal (opt, "--ro-bind-data") ||
          g_str_equal (opt, "--file") ||
          g_str_equal (opt, "--symlink"))
        {
          g_assert (i + 3 <= from->argv->len);
          /* pdata[i + 1] is the target, fd or permissions: unchanged. */
          /* pdata[i + 2] is a path in the final container: unchanged. */
          g_debug ("%s %s %s",
                   opt,
                   (const char *) from->argv->pdata[i + 1],
                   (const char *) from->argv->pdata[i + 2]);

          flatpak_bwrap_add_args (to, opt, from->argv->pdata[i + 1],
                                  from->argv->pdata[i + 2], NULL);

          i += 3;
        }
      else if (g_str_equal (opt, "--dev") ||
               g_str_equal (opt, "--dir") ||
               g_str_equal (opt, "--mqueue") ||
               g_str_equal (opt, "--proc") ||
               g_str_equal (opt, "--remount-ro") ||
               g_str_equal (opt, "--tmpfs"))
        {
          g_assert (i + 2 <= from->argv->len);
          /* pdata[i + 1] is a path in the final container, or a non-path:
           * unchanged. */
          g_debug ("%s %s",
                   opt,
                   (const char *) from->argv->pdata[i + 1]);

          flatpak_bwrap_add_args (to, opt, from->argv->pdata[i + 1], NULL);

          i += 2;
        }
      else if (g_str_equal (opt, "--bind") ||
               g_str_equal (opt, "--bind-try") ||
               g_str_equal (opt, "--dev-bind") ||
               g_str_equal (opt, "--dev-bind-try") ||
               g_str_equal (opt, "--ro-bind") ||
               g_str_equal (opt, "--ro-bind-try"))
        {
          g_assert (i + 3 <= from->argv->len);
          const char *from_src = from->argv->pdata[i + 1];
          const char *from_dest = from->argv->pdata[i + 2];

          /* If we're using FEX-Emu or similar, Flatpak code might think it
           * has found a particular file either because it's in the rootfs,
           * or because it's in the real root filesystem.
           * If it exists in FEX rootfs, we add an additional mount entry
           * where the source is from the FEX rootfs and the destination is
           * prefixed with the pressure-vessel interpreter root location. */
          if (interpreter_root != NULL
              && _srt_file_test_in_sysroot (interpreter_root, interpreter_fd,
                                            from_src, G_FILE_TEST_EXISTS))
            {
              g_autofree gchar *inter_src = g_build_filename (interpreter_root,
                                                              from_src, NULL);
              g_autofree gchar *inter_dest = g_build_filename (PV_RUNTIME_PATH_INTERPRETER_ROOT,
                                                               from_dest, NULL);

              g_debug ("Adjusted \"%s\" to \"%s\" and \"%s\" to \"%s\" for the interpreter root",
                       from_src, inter_src, from_dest, inter_dest);
              g_debug ("%s %s %s", opt, inter_src, inter_dest);
              flatpak_bwrap_add_args (to, opt, inter_src, inter_dest, NULL);
            }

          if (interpreter_root == NULL
              || _srt_file_test_in_sysroot (root, root_fd, from_src,
                                            G_FILE_TEST_EXISTS))
            {
              g_autofree gchar *src = NULL;
              /* Paths in the home directory might need adjusting.
               * Paths outside the home directory do not: if they're part of
               * /run/host, they've been adjusted already by
               * flatpak_exports_take_host_fd(), and if not, they appear in
               * the container with the same path as on the host. */
              if (flatpak_has_path_prefix (from_src, home))
                {
                  src = pv_current_namespace_path_to_host_path (from_src);

                  if (!g_str_equal (from_src, src))
                    g_debug ("Adjusted \"%s\" to \"%s\" to be reachable from host",
                             from_src, src);
                }
              else
                {
                  src = g_strdup (from_src);
                }

              g_debug ("%s %s %s", opt, src, from_dest);
              flatpak_bwrap_add_args (to, opt, src, from_dest, NULL);
            }

          i += 3;
        }
      else if (g_str_equal (opt, "--perms"))
        {
          g_assert (i + 2 <= from->argv->len);
          const char *perms = from->argv->pdata[i + 1];
          /* pdata[i + 1] is a non-path: unchanged. */
          g_debug ("%s %s",
                   opt,
                   (const char *) from->argv->pdata[i + 1]);

          /* A system copy of bubblewrap older than 0.5.0
           * (Debian 11 or older) won't support --perms. Fall back to
           * creating mount-points with the default permissions if
           * necessary. */
          if (bwrap_flags & PV_BWRAP_FLAGS_HAS_PERMS)
            flatpak_bwrap_add_args (to, opt, perms, NULL);
          else
            g_debug ("Ignoring \"--perms %s\" because bwrap is too old",
                     perms);

          i += 2;
        }
      else
        {
          g_return_val_if_reached (FALSE);
        }
    }

  return TRUE;
}

typedef enum
{
  PV_WRAP_LOG_FLAGS_OVERRIDES = (1 << 0),
  PV_WRAP_LOG_FLAGS_CONTAINER = (1 << 1),
  PV_WRAP_LOG_FLAGS_NONE = 0
} PvWrapLogFlags;

static const GDebugKey pv_debug_keys[] =
{
  { "overrides", PV_WRAP_LOG_FLAGS_OVERRIDES },
  { "container", PV_WRAP_LOG_FLAGS_CONTAINER },
};

typedef enum
{
  TRISTATE_NO = 0,
  TRISTATE_YES,
  TRISTATE_MAYBE
} Tristate;

static gboolean opt_batch = FALSE;
static gboolean opt_copy_runtime = FALSE;
static gboolean opt_deterministic = FALSE;
static gboolean opt_devel = FALSE;
static char **opt_env_if_host = NULL;
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
static GArray *opt_preload_modules = NULL;
static char *opt_runtime = NULL;
static char *opt_runtime_archive = NULL;
static char *opt_runtime_base = NULL;
static char *opt_runtime_id = NULL;
static Tristate opt_share_home = TRISTATE_MAYBE;
static gboolean opt_share_pid = TRUE;
static gboolean opt_single_thread = FALSE;
static gboolean opt_systemd_scope = FALSE;
static double opt_terminate_idle_timeout = 0.0;
static double opt_terminate_timeout = -1.0;
static char *opt_variable_dir = NULL;
static gboolean opt_verbose = FALSE;
static gboolean opt_version = FALSE;
static gboolean opt_version_only = FALSE;
static gboolean opt_test = FALSE;
static PvTerminal opt_terminal = PV_TERMINAL_AUTO;
static char *opt_write_final_argv = NULL;

typedef enum
{
  PRELOAD_VARIABLE_INDEX_LD_AUDIT,
  PRELOAD_VARIABLE_INDEX_LD_PRELOAD,
} PreloadVariableIndex;

static const struct
{
  const char *variable;
  const char *adverb_option;
} preload_options[] =
{
  [PRELOAD_VARIABLE_INDEX_LD_AUDIT] = { "LD_AUDIT", "--ld-audit" },
  [PRELOAD_VARIABLE_INDEX_LD_PRELOAD] = { "LD_PRELOAD", "--ld-preload" },
};

typedef struct
{
  PreloadVariableIndex which;
  gchar *preload;
} WrapPreloadModule;

static void
wrap_preload_module_clear (gpointer p)
{
  WrapPreloadModule *self = p;

  g_clear_pointer (&self->preload, g_free);
}

static gboolean
opt_ld_something (PreloadVariableIndex which,
                  const char *value,
                  GError **error)
{
  WrapPreloadModule module = { which, g_strdup (value) };

  if (opt_preload_modules == NULL)
    {
      opt_preload_modules = g_array_new (FALSE, FALSE, sizeof (WrapPreloadModule));
      g_array_set_clear_func (opt_preload_modules, wrap_preload_module_clear);
    }

  g_array_append_val (opt_preload_modules, module);
  return TRUE;
}

static gboolean
opt_ld_audit_cb (const gchar *option_name,
                 const gchar *value,
                 gpointer data,
                 GError **error)
{
  return opt_ld_something (PRELOAD_VARIABLE_INDEX_LD_AUDIT, value, error);
}

static gboolean
opt_ld_preload_cb (const gchar *option_name,
                   const gchar *value,
                   gpointer data,
                   GError **error)
{
  return opt_ld_something (PRELOAD_VARIABLE_INDEX_LD_PRELOAD, value, error);
}

static gboolean
opt_host_ld_preload_cb (const gchar *option_name,
                        const gchar *value,
                        gpointer data,
                        GError **error)
{
  g_warning ("%s is deprecated, use --ld-preload=%s instead",
             option_name, value);
  return opt_ld_preload_cb (option_name, value, data, error);
}

static gboolean
opt_copy_runtime_into_cb (const gchar *option_name,
                          const gchar *value,
                          gpointer data,
                          GError **error)
{
  if (value == NULL)
    {
      /* Do nothing, keep previous setting */
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
  { "deterministic", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_deterministic,
    "Enforce a deterministic sort order on arbitrarily-ordered things, "
    "even if that's slower. [Default if $PRESSURE_VESSEL_DETERMINISTIC is 1]",
    NULL },
  { "devel", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_devel,
    "Use a more permissive configuration that is helpful during development "
    "but not intended for production use. "
    "[Default if $PRESSURE_VESSEL_DEVEL is 1]",
    NULL },
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
    G_OPTION_FLAG_FILENAME | G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_CALLBACK,
    &opt_host_ld_preload_cb,
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
    "'steam-runtime-launcher-service'. All the environment variables that are "
    "edited by pressure-vessel, or that are known to be wrong in the new "
    "container, or that needs to inherit the value from the host system, "
    "will be locked. This option implies --batch.", NULL },
  { "ld-audit", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, &opt_ld_audit_cb,
    "Add MODULE from current execution environment to LD_AUDIT when "
    "executing COMMAND.",
    "MODULE" },
  { "ld-preload", '\0',
    G_OPTION_FLAG_FILENAME, G_OPTION_ARG_CALLBACK, &opt_ld_preload_cb,
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
  { "single-thread", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_single_thread,
    "Disable multi-threaded code paths, for debugging",
    NULL },
  { "systemd-scope", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_systemd_scope,
    "Attempt to run the game in a systemd scope", NULL },
  { "no-systemd-scope", '\0',
    G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &opt_systemd_scope,
    "Do not run the game in a systemd scope", NULL },
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

#define usage_error(...) _srt_log_failure (__VA_ARGS__)

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
  PvHomeMode home_mode;
  g_autoptr(FlatpakBwrap) flatpak_subsandbox = NULL;
  g_autoptr(PvEnviron) container_env = NULL;
  g_autoptr(FlatpakBwrap) bwrap = NULL;
  g_autoptr(FlatpakBwrap) bwrap_filesystem_arguments = NULL;
  g_autoptr(FlatpakBwrap) bwrap_home_arguments = NULL;
  g_autoptr(FlatpakBwrap) argv_in_container = NULL;
  g_autoptr(FlatpakBwrap) final_argv = NULL;
  g_autoptr(FlatpakExports) exports = NULL;
  g_autofree gchar *bwrap_executable = NULL;
  PvBwrapFlags bwrap_flags = PV_BWRAP_FLAGS_NONE;
  g_autofree gchar *cwd_p = NULL;
  g_autofree gchar *cwd_l = NULL;
  g_autofree gchar *cwd_p_host = NULL;
  g_autofree gchar *interpreter_root = NULL;
  g_autofree gchar *private_home = NULL;
  const gchar *home;
  g_autofree gchar *tools_dir = NULL;
  g_autoptr(PvRuntime) runtime = NULL;
  glnx_autofd int original_stdout = -1;
  glnx_autofd int original_stderr = -1;
  g_autoptr(GArray) pass_fds_through_adverb = g_array_new (FALSE, FALSE, sizeof (int));
  const char *steam_app_id;
  g_autoptr(GPtrArray) adverb_preload_argv = NULL;
  int result;
  PvAppendPreloadFlags append_preload_flags = PV_APPEND_PRELOAD_FLAGS_NONE;
  glnx_autofd int root_fd = -1;
  SrtMachineType host_machine = SRT_MACHINE_TYPE_UNKNOWN;
  SrtLogFlags log_flags;
  PvWrapLogFlags pv_log_flags = PV_WRAP_LOG_FLAGS_NONE;

  setlocale (LC_ALL, "");

  /* Set up the initial base logging */
  if (!_srt_util_set_glib_log_handler ("pressure-vessel-wrap",
                                       G_LOG_DOMAIN, SRT_LOG_FLAGS_NONE,
                                       &original_stdout, &original_stderr, error))
    goto out;

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

  /* Set defaults */
  opt_batch = _srt_boolean_environment ("PRESSURE_VESSEL_BATCH", FALSE);
  opt_copy_runtime = is_flatpak_env;
  /* Process COPY_RUNTIME_INFO first so that COPY_RUNTIME and VARIABLE_DIR
   * can override it */
  opt_copy_runtime_into_cb ("$PRESSURE_VESSEL_COPY_RUNTIME_INTO",
                            g_getenv ("PRESSURE_VESSEL_COPY_RUNTIME_INTO"),
                            NULL, NULL);
  opt_copy_runtime = _srt_boolean_environment ("PRESSURE_VESSEL_COPY_RUNTIME",
                                               opt_copy_runtime);
  opt_deterministic = _srt_boolean_environment ("PRESSURE_VESSEL_DETERMINISTIC",
                                                FALSE);
  opt_devel = _srt_boolean_environment ("PRESSURE_VESSEL_DEVEL", FALSE);
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

  opt_remove_game_overlay = _srt_boolean_environment ("PRESSURE_VESSEL_REMOVE_GAME_OVERLAY",
                                                      FALSE);
  opt_systemd_scope = _srt_boolean_environment ("PRESSURE_VESSEL_SYSTEMD_SCOPE",
                                                opt_systemd_scope);
  opt_import_vulkan_layers = _srt_boolean_environment ("PRESSURE_VESSEL_IMPORT_VULKAN_LAYERS",
                                                       TRUE);

  opt_share_home = tristate_environment ("PRESSURE_VESSEL_SHARE_HOME");
  opt_gc_legacy_runtimes = _srt_boolean_environment ("PRESSURE_VESSEL_GC_LEGACY_RUNTIMES", FALSE);
  opt_gc_runtimes = _srt_boolean_environment ("PRESSURE_VESSEL_GC_RUNTIMES", TRUE);
  opt_generate_locales = _srt_boolean_environment ("PRESSURE_VESSEL_GENERATE_LOCALES", TRUE);

  opt_share_pid = _srt_boolean_environment ("PRESSURE_VESSEL_SHARE_PID", TRUE);
  opt_single_thread = _srt_boolean_environment ("PRESSURE_VESSEL_SINGLE_THREAD",
                                                opt_single_thread);
  opt_verbose = _srt_boolean_environment ("PRESSURE_VESSEL_VERBOSE", FALSE);

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

  log_flags = SRT_LOG_FLAGS_DIVERT_STDOUT | SRT_LOG_FLAGS_OPTIONALLY_JOURNAL;

  if (opt_deterministic)
    log_flags |= SRT_LOG_FLAGS_DIFFABLE;

  if (opt_verbose)
    {
      log_flags |= SRT_LOG_FLAGS_DEBUG;

      /* We share the same environment variable as the rest of s-r-t, but look
       * for additional flags in it */
      pv_log_flags = g_parse_debug_string (g_getenv ("SRT_LOG"),
                                           pv_debug_keys,
                                           G_N_ELEMENTS (pv_debug_keys));
    }

  if (!_srt_util_set_glib_log_handler (NULL, G_LOG_DOMAIN, log_flags,
                                       NULL, NULL, error))
    goto out;

  pv_wrap_detect_virtualization (&interpreter_root, &host_machine);

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
          if (interpreter_root != NULL)
            opt_graphics_provider = g_strdup (interpreter_root);
          else
            opt_graphics_provider = g_strdup ("/");
        }
      else
        {
          g_warning ("$PRESSURE_VESSEL_HOST_GRAPHICS is deprecated, "
                     "please use PRESSURE_VESSEL_GRAPHICS_PROVIDER instead");

          if (value == TRISTATE_NO)
            opt_graphics_provider = g_strdup ("");
          else if (interpreter_root != NULL)
            opt_graphics_provider = g_strdup (interpreter_root);
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

  if (opt_version_only || opt_version)
    {
      if (original_stdout >= 0
          && !_srt_util_restore_saved_fd (original_stdout, STDOUT_FILENO, error))
        goto out;

      if (opt_version_only)
        g_print ("%s\n", VERSION);
      else
        g_print ("%s:\n"
                 " Package: pressure-vessel\n"
                 " Version: %s\n",
                 argv[0], VERSION);

      ret = 0;
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

  if (opt_steam_app_id != NULL)
    steam_app_id = opt_steam_app_id;
  else
    steam_app_id = _srt_get_steam_app_id ();

  home = g_get_home_dir ();

  if (opt_share_home == TRISTATE_YES)
    {
      home_mode = PV_HOME_MODE_SHARED;
    }
  else if (opt_home)
    {
      home_mode = PV_HOME_MODE_PRIVATE;
      private_home = g_strdup (opt_home);
    }
  else if (opt_share_home == TRISTATE_MAYBE)
    {
      home_mode = PV_HOME_MODE_SHARED;
    }
  else if (opt_freedesktop_app_id)
    {
      home_mode = PV_HOME_MODE_PRIVATE;
      private_home = g_build_filename (home, ".var", "app",
                                       opt_freedesktop_app_id, NULL);
    }
  else if (steam_app_id != NULL)
    {
      home_mode = PV_HOME_MODE_PRIVATE;
      opt_freedesktop_app_id = g_strdup_printf ("com.steampowered.App%s",
                                                steam_app_id);
      private_home = g_build_filename (home, ".var", "app",
                                       opt_freedesktop_app_id, NULL);
    }
  else if (opt_batch)
    {
      home_mode = PV_HOME_MODE_TRANSIENT;
      private_home = NULL;
      g_info ("Unsharing the home directory without choosing a valid "
              "candidate, using tmpfs as a fallback");
    }
  else
    {
      usage_error ("Either --home, --freedesktop-app-id, --steam-app-id "
                   "or $SteamAppId is required");
      goto out;
    }

  if (home_mode == PV_HOME_MODE_PRIVATE)
    g_assert (private_home != NULL);
  else
    g_assert (private_home == NULL);

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

  if ((result = _srt_set_compatible_resource_limits (0)) < 0)
    g_warning ("Unable to set normal resource limits: %s",
               g_strerror (-result));

  if (opt_terminal != PV_TERMINAL_TTY && !opt_devel)
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

  _srt_get_current_dirs (&cwd_p, &cwd_l);

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

      qsort (env, g_strv_length (env), sizeof (char *), flatpak_envp_cmp);

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

  tools_dir = _srt_find_executable_dir (error);

  if (tools_dir == NULL)
    goto out;

  g_debug ("Found executable directory: %s", tools_dir);

  /* If we are in a Flatpak environment we can't use bwrap directly */
  if (is_flatpak_env)
    {
      if (!pv_wrap_check_flatpak (tools_dir, &flatpak_subsandbox, error))
        goto out;
    }
  else
    {
      g_debug ("Checking for bwrap...");

      /* if this fails, it will warn */
      bwrap_executable = pv_wrap_check_bwrap (tools_dir, opt_only_prepare,
                                              &bwrap_flags);

      if (bwrap_executable == NULL)
        goto out;

      g_debug ("OK (%s)", bwrap_executable);
    }

  if (opt_test)
    {
      ret = 0;
      goto out;
    }

  /* FEX-Emu transparently rewrites most file I/O to check its "rootfs"
   * first, and only use the real root if the corresponding file
   * doesn't exist in the "rootfs". In many places we actively don't want
   * this, because we're inspecting paths in order to pass them to bwrap,
   * which will use them to set up bind-mounts, which are not subject to
   * FEX-Emu's rewriting; so bypass it here. */
  if (!glnx_opendirat (-1, "/proc/self/root", TRUE, &root_fd, error))
    return FALSE;

  /* Invariant: we are in exactly one of these two modes */
  g_assert (((flatpak_subsandbox != NULL)
             + (!is_flatpak_env))
            == 1);

  if (flatpak_subsandbox == NULL)
    {
      /* Start with an empty environment and populate it later */
      bwrap = flatpak_bwrap_new (flatpak_bwrap_empty_env);
      g_assert (bwrap_executable != NULL);
      flatpak_bwrap_add_arg (bwrap, bwrap_executable);
      bwrap_filesystem_arguments = flatpak_bwrap_new (flatpak_bwrap_empty_env);
      exports = flatpak_exports_new ();
    }
  else
    {
      append_preload_flags |= PV_APPEND_PRELOAD_FLAGS_FLATPAK_SUBSANDBOX;
    }

  /* Invariant: we have bwrap or exports iff we also have the other */
  g_assert ((bwrap != NULL) == (exports != NULL));
  g_assert ((bwrap != NULL) == (bwrap_filesystem_arguments != NULL));
  g_assert ((bwrap != NULL) == (bwrap_executable != NULL));

  container_env = pv_environ_new ();

  if (bwrap != NULL)
    {
      FlatpakFilesystemMode sysfs_mode = FLATPAK_FILESYSTEM_MODE_READ_ONLY;

      g_assert (exports != NULL);
      g_assert (bwrap_filesystem_arguments != NULL);

      if (g_strcmp0 (opt_graphics_provider, "/") == 0)
        graphics_provider_mount_point = g_strdup ("/run/host");
      else
        graphics_provider_mount_point = g_strdup ("/run/gfx");

      /* Protect the controlling terminal from the app/game, unless we are
       * running an interactive shell in which case that would break its
       * job control. */
      if (opt_terminal != PV_TERMINAL_TTY && !opt_devel)
        flatpak_bwrap_add_arg (bwrap, "--new-session");

      /* Start with just the root tmpfs (which appears automatically)
       * and the standard API filesystems */
      if (opt_devel)
        sysfs_mode = FLATPAK_FILESYSTEM_MODE_READ_WRITE;

      pv_bwrap_add_api_filesystems (bwrap_filesystem_arguments, sysfs_mode);

      flatpak_bwrap_add_args (bwrap_filesystem_arguments,
                              "--ro-bind", "/etc", "/run/host/etc", NULL);
      if (!pv_bwrap_bind_usr (bwrap, "/", root_fd, "/run/host", error))
        goto out;

      if (interpreter_root != NULL)
        {
          /* If we are in an emulator, we also need to populate /run/host
           * in the interpreter mount point */
          glnx_autofd int interpreter_fd = -1;
          g_autofree gchar *inter_run_host = g_build_filename (PV_RUNTIME_PATH_INTERPRETER_ROOT,
                                                               "/run/host", NULL);
          g_autofree gchar *etc_src = g_build_filename (interpreter_root,
                                                        "etc", NULL);
          g_autofree gchar *etc_dest = g_build_filename (inter_run_host, "etc", NULL);

          if (!glnx_opendirat (-1, interpreter_root, TRUE, &interpreter_fd, error))
            goto out;

          flatpak_bwrap_add_args (bwrap_filesystem_arguments,
                                  "--ro-bind", etc_src, etc_dest, NULL);

          if (!pv_bwrap_bind_usr (bwrap, interpreter_root, interpreter_fd, inter_run_host, error))
            goto out;
        }

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
      SrtDirentCompareFunc cmp = NULL;

      if (opt_deterministic)
        cmp = _srt_dirent_strcmp;

      if (!pv_runtime_garbage_collect_legacy (opt_variable_dir,
                                              opt_runtime_base,
                                              cmp,
                                              &local_error))
        {
          g_warning ("Unable to clean up old runtimes: %s",
                     local_error->message);
          g_clear_error (&local_error);
        }
    }

  if (opt_runtime != NULL || opt_runtime_archive != NULL)
    {
      G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer =
        _srt_profiling_start ("Setting up runtime");
      g_autoptr(PvGraphicsProvider) graphics_provider = NULL;
      g_autoptr(PvGraphicsProvider) interpreter_host_provider = NULL;
      PvRuntimeFlags flags = PV_RUNTIME_FLAGS_NONE;
      g_autofree gchar *runtime_resolved = NULL;
      const char *runtime_path = NULL;

      if (opt_deterministic)
        flags |= PV_RUNTIME_FLAGS_DETERMINISTIC;

      if (opt_gc_runtimes)
        flags |= PV_RUNTIME_FLAGS_GC_RUNTIMES;

      if (opt_generate_locales)
        flags |= PV_RUNTIME_FLAGS_GENERATE_LOCALES;

      if (opt_graphics_provider != NULL && opt_graphics_provider[0] != '\0')
        {
          g_assert (graphics_provider_mount_point != NULL);
          graphics_provider = pv_graphics_provider_new (opt_graphics_provider,
                                                        graphics_provider_mount_point,
                                                        TRUE, error);

          if (graphics_provider == NULL)
            goto out;
        }

      if (opt_verbose)
        flags |= PV_RUNTIME_FLAGS_VERBOSE;

      if (opt_import_vulkan_layers)
        flags |= PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS;

      if (opt_copy_runtime)
        flags |= PV_RUNTIME_FLAGS_COPY_RUNTIME;

      if (opt_deterministic || opt_single_thread)
        flags |= PV_RUNTIME_FLAGS_SINGLE_THREAD;

      if (flatpak_subsandbox != NULL)
        flags |= PV_RUNTIME_FLAGS_FLATPAK_SUBSANDBOX;

      if (interpreter_root != NULL)
        {
          flags |= PV_RUNTIME_FLAGS_INTERPRETER_ROOT;

          /* Also include the real host graphics stack to allow thunking.
           * To avoid enumerating the same DRIs/layers twice, we only do
           * this if the host is not a supported architecture. */
          if (!pv_supported_architectures_include_machine_type (host_machine))
            {
              /* The trailing slash is needed to allow open(2) to work even if
               * it's using the O_NOFOLLOW flag. */
              interpreter_host_provider = pv_graphics_provider_new ("/proc/self/root/",
                                                                    "/proc/self/root/",
                                                                    FALSE, error);
              if (interpreter_host_provider == NULL)
                goto out;
            }
        }

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
                                graphics_provider,
                                interpreter_host_provider,
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
          const char *app = pv_runtime_get_modified_app (runtime);
          const char *usr = pv_runtime_get_modified_usr (runtime);

          flatpak_bwrap_add_args (flatpak_subsandbox,
                                  "--app-path", app == NULL ? "" : app,
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
  else
    {
      SrtDirentCompareFunc cmp = NULL;

      if (opt_deterministic)
        cmp = _srt_dirent_strcmp;

      g_assert (!is_flatpak_env);
      g_assert (bwrap != NULL);
      g_assert (bwrap_filesystem_arguments != NULL);
      g_assert (exports != NULL);

      if (!pv_wrap_use_host_os (root_fd, exports, bwrap_filesystem_arguments,
                                cmp, error))
        goto out;
    }

  /* Protect other users' homes (but guard against the unlikely
   * situation that they don't exist). We use the FlatpakExports for this
   * so that it can be overridden by --filesystem=/home or
   * pv_wrap_use_home(), and so that it is sorted correctly with
   * respect to all the other home-directory-related exports. */
  if (exports != NULL
      && g_file_test ("/home", G_FILE_TEST_EXISTS))
    flatpak_exports_add_path_tmpfs (exports, "/home");

  g_debug ("Making home directory available...");

  if (flatpak_subsandbox != NULL)
    {
      if (home_mode == PV_HOME_MODE_SHARED)
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
      g_assert (!is_flatpak_env);
      g_assert (bwrap != NULL);
      g_assert (bwrap_filesystem_arguments != NULL);
      g_assert (exports != NULL);

      bwrap_home_arguments = flatpak_bwrap_new (flatpak_bwrap_empty_env);

      if (!pv_wrap_use_home (home_mode, home, private_home,
                             exports, bwrap_home_arguments, container_env,
                             error))
        goto out;
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
          /* steam-runtime-launch-client currently hard-codes this */
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

  if (flatpak_subsandbox != NULL)
    {
      /* We special case libshared-library-guard because usually
       * its blockedlist file is located in `/app` and we need
       * to change that to the `/run/parent` counterpart */
      const gchar *blockedlist = g_getenv ("SHARED_LIBRARY_GUARD_CONFIG");

      if (blockedlist == NULL)
        blockedlist = "/app/etc/freedesktop-sdk.ld.so.blockedlist";

      if (g_file_test (blockedlist, G_FILE_TEST_EXISTS)
          && (g_str_has_prefix (blockedlist, "/app/")
              || g_str_has_prefix (blockedlist, "/usr/")
              || g_str_has_prefix (blockedlist, "/lib")))
        {
          g_autofree gchar *adjusted_blockedlist = NULL;
          adjusted_blockedlist = g_build_filename ("/run/parent",
                                                   blockedlist, NULL);
          pv_environ_setenv (container_env, "SHARED_LIBRARY_GUARD_CONFIG",
                             adjusted_blockedlist);
        }
    }

  adverb_preload_argv = g_ptr_array_new_with_free_func (g_free);

  if (opt_remove_game_overlay)
    append_preload_flags |= PV_APPEND_PRELOAD_FLAGS_REMOVE_GAME_OVERLAY;

  /* We need the LD_PRELOADs from Steam visible at the paths that were
   * used for them, which might be their physical rather than logical
   * locations. Steam doesn't generally use LD_AUDIT, but the Steam app
   * on Flathub does, and it needs similar handling. */
  if (opt_preload_modules != NULL)
    {
      gsize j;

      g_debug ("Adjusting LD_AUDIT/LD_PRELOAD modules...");

      for (j = 0; j < opt_preload_modules->len; j++)
        {
          const WrapPreloadModule *module = &g_array_index (opt_preload_modules,
                                                            WrapPreloadModule,
                                                            j);

          g_assert (module->which >= 0);
          g_assert (module->which < G_N_ELEMENTS (preload_options));
          pv_wrap_append_preload (adverb_preload_argv,
                                  preload_options[module->which].variable,
                                  preload_options[module->which].adverb_option,
                                  module->preload,
                                  environ,
                                  append_preload_flags,
                                  runtime,
                                  exports);
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

      /* Bind-mount /run/udev to support games that detect joysticks by using
       * udev directly. We only do that when the host's version of libudev.so.1
       * is in use, because there is no guarantees that the container's libudev
       * is compatible with the host's udevd. */
      if (runtime != NULL)
        {
          for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
            {
              GStatBuf ignored;
              g_autofree gchar *override = NULL;

              override = g_build_filename (pv_runtime_get_overrides (runtime),
                                           "lib", pv_multiarch_tuples[i],
                                           "libudev.so.1", NULL);

              if (g_lstat (override, &ignored) == 0)
                {
                  g_debug ("We are using the host's version of \"libudev.so.1\", trying to bind-mount /run/udev too...");
                  flatpak_exports_add_path_expose (exports,
                                                   FLATPAK_FILESYSTEM_MODE_READ_ONLY,
                                                   "/run/udev");
                  break;
                }
            }
        }

      /* On NixOS, all paths hard-coded into libraries are in here */
      flatpak_exports_add_path_expose (exports,
                                       FLATPAK_FILESYSTEM_MODE_READ_ONLY,
                                       "/nix");
      /* Same, but for Guix */
      flatpak_exports_add_path_expose (exports,
                                       FLATPAK_FILESYSTEM_MODE_READ_ONLY,
                                       "/gnu/store");

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

              if (flatpak_has_path_prefix (opt_filesystems[i], "/overrides"))
                {
                  g_warning_once ("The path \"/overrides/\" is reserved and cannot be shared");
                  continue;
                }

              if (flatpak_has_path_prefix (opt_filesystems[i], "/usr"))
                g_warning_once ("Binding directories that are located under \"/usr/\" "
                                "is not supported!");
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
        pv_environ_setenv (container_env,
                           known_required_env[i].name,
                           g_getenv (known_required_env[i].name));

      flatpak_bwrap_add_args (flatpak_subsandbox,
                              "--directory", cwd_p,
                              NULL);
    }

  pv_environ_setenv (container_env, "PWD", NULL);

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

              pv_environ_setenv (container_env, opt_env_if_host[i],
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
      g_warn_if_fail (g_strv_length (exports_bwrap->envp) == 0);
      if (!append_adjusted_exports (bwrap, exports_bwrap, home,
                                   interpreter_root, bwrap_flags, error))
        goto out;

      /* The other filesystem arguments have to come after the exports
       * so that if the exports set up symlinks, the other filesystem
       * arguments like --dir work with the symlinks' targets. */
      g_warn_if_fail (g_strv_length (bwrap_filesystem_arguments->envp) == 0);
      flatpak_bwrap_append_bwrap (bwrap, bwrap_filesystem_arguments);
      g_clear_pointer (&bwrap_filesystem_arguments, flatpak_bwrap_free);
    }

  if (bwrap != NULL)
    {
      g_autoptr(FlatpakBwrap) sharing_bwrap = NULL;

      sharing_bwrap = pv_wrap_share_sockets (container_env,
                                             original_environ,
                                             (runtime != NULL),
                                             is_flatpak_env);
      g_warn_if_fail (g_strv_length (sharing_bwrap->envp) == 0);

      if (!append_adjusted_exports (bwrap, sharing_bwrap, home,
                                    interpreter_root, bwrap_flags, error))
        goto out;
    }
  else if (flatpak_subsandbox != NULL)
    {
      pv_wrap_set_icons_env_vars (container_env, original_environ);
    }

  if (runtime != NULL)
    {
      if (!pv_runtime_use_shared_sockets (runtime, bwrap, container_env,
                                          error))
        goto out;
    }

  if (is_flatpak_env)
    {
      g_autoptr(GList) vars = NULL;
      const GList *iter;

      /* Let these inherit from the sub-sandbox environment */
      pv_environ_inherit_env (container_env, "FLATPAK_ID");
      pv_environ_inherit_env (container_env, "FLATPAK_SANDBOX_DIR");
      pv_environ_inherit_env (container_env, "DBUS_SESSION_BUS_ADDRESS");
      pv_environ_inherit_env (container_env, "DBUS_SYSTEM_BUS_ADDRESS");
      pv_environ_inherit_env (container_env, "DISPLAY");
      pv_environ_inherit_env (container_env, "XDG_RUNTIME_DIR");

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
          /* This includes the arguments necessary to regenerate the
           * ld.so cache */
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

      flatpak_bwrap_append_args (adverb_argv, adverb_preload_argv);

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
      g_autofree gchar *launcher_service = g_build_filename (tools_dir,
                                                             "steam-runtime-launcher-service",
                                                             NULL);
      g_debug ("Adding steam-runtime-launcher-service '%s'...", launcher_service);
      flatpak_bwrap_add_arg (launcher_argv, launcher_service);

      if (opt_verbose)
        flatpak_bwrap_add_arg (launcher_argv, "--verbose");

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
  else
    {
      g_assert (bwrap != NULL);
      g_warn_if_fail (g_strv_length (bwrap->envp) == 0);
      flatpak_bwrap_append_bwrap (final_argv, bwrap);
    }

  g_warn_if_fail (g_strv_length (argv_in_container->envp) == 0);
  flatpak_bwrap_append_bwrap (final_argv, argv_in_container);

  /* We'll have permuted the order anyway, so we might as well sort it,
   * to make debugging a bit easier. */
  flatpak_bwrap_sort_envp (final_argv);

  if (opt_verbose)
    {
      if (runtime != NULL && (pv_log_flags & PV_WRAP_LOG_FLAGS_OVERRIDES))
        pv_runtime_log_overrides (runtime);

      if (runtime != NULL && (pv_log_flags & PV_WRAP_LOG_FLAGS_CONTAINER))
        pv_runtime_log_container (runtime);

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
      if (!pv_wrap_maybe_load_nvidia_modules (error))
        {
          g_debug ("Cannot load nvidia modules: %s", local_error->message);
          g_clear_error (&local_error);
        }
    }

  if (opt_only_prepare)
    {
      ret = 0;
      goto out;
    }

  if (opt_systemd_scope)
    pv_wrap_move_into_scope (steam_app_id);

  pv_bwrap_execve (final_argv, original_stdout, original_stderr, error);

out:
  if (local_error != NULL)
    _srt_log_failure ("%s", local_error->message);

  g_clear_pointer (&opt_preload_modules, g_array_unref);
  g_clear_pointer (&adverb_preload_argv, g_ptr_array_unref);
  g_clear_pointer (&opt_env_if_host, g_strfreev);
  g_clear_pointer (&opt_freedesktop_app_id, g_free);
  g_clear_pointer (&opt_steam_app_id, g_free);
  g_clear_pointer (&opt_home, g_free);
  g_clear_pointer (&opt_runtime, g_free);
  g_clear_pointer (&opt_runtime_archive, g_free);
  g_clear_pointer (&opt_runtime_base, g_free);
  g_clear_pointer (&opt_runtime_id, g_free);
  g_clear_pointer (&opt_pass_fds, g_array_unref);
  g_clear_pointer (&opt_variable_dir, g_free);

  g_debug ("Exiting with status %d", ret);
  return ret;
}
