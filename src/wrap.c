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
#include <sysexits.h>

/* Include these before steam-runtime-tools.h so that their backport of
 * G_DEFINE_AUTOPTR_CLEANUP_FUNC will be visible to it */
#include "glib-backports.h"
#include "libglnx.h"

#include <steam-runtime-tools/steam-runtime-tools.h>

#include "bwrap.h"
#include "bwrap-lock.h"
#include "flatpak-bwrap-private.h"
#include "flatpak-run-private.h"
#include "flatpak-utils-private.h"
#include "utils.h"
#include "wrap-interactive.h"

/*
 * Supported Debian-style multiarch tuples
 */
static const char * const multiarch_tuples[] =
{
  "x86_64-linux-gnu",
  "i386-linux-gnu",
  NULL
};

/*
 * Directories other than /usr/lib that we must search for loadable
 * modules, in the same order as multiarch_tuples
 */
static const char * const libquals[] =
{
  "lib64",
  "lib32"
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

static void
search_path_append (GString *search_path,
                    const gchar *item)
{
  if (item == NULL || item[0] == '\0')
    return;

  if (search_path->len != 0)
    g_string_append (search_path, ":");

  g_string_append (search_path, item);
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
check_bwrap (const char *tools_dir)
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
  else
    {
      int exit_status;
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
                         &exit_status,
                         error))
        {
          g_warning ("Cannot run bwrap: %s", local_error->message);
          g_clear_error (&local_error);
        }
      else if (exit_status != 0)
        {
          g_warning ("Cannot run bwrap: exit status %d", exit_status);

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

static gchar *
capture_output (const char * const * argv,
                GError **error)
{
  gsize len;
  gint exit_status;
  g_autofree gchar *output = NULL;
  g_autofree gchar *errors = NULL;
  gsize i;
  g_autoptr(GString) command = g_string_new ("");

  for (i = 0; argv[i] != NULL; i++)
    {
      g_autofree gchar *quoted = g_shell_quote (argv[i]);

      g_string_append_printf (command, " %s", quoted);
    }

  g_debug ("run:%s", command->str);

  if (!g_spawn_sync (NULL,  /* cwd */
                     (char **) argv,
                     NULL,  /* env */
                     G_SPAWN_SEARCH_PATH,
                     NULL, NULL,    /* child setup */
                     &output,
                     &errors,
                     &exit_status,
                     error))
    return NULL;

  g_printerr ("%s", errors);

  if (!g_spawn_check_exit_status (exit_status, error))
    return NULL;

  len = strlen (output);

  /* Emulate shell $() */
  if (len > 0 && output[len - 1] == '\n')
    output[len - 1] = '\0';

  g_debug ("-> %s", output);

  return g_steal_pointer (&output);
}

static gboolean
try_bind_dri (FlatpakBwrap *bwrap,
              FlatpakBwrap *mount_runtime_on_scratch,
              const char *overrides,
              const char *scratch,
              const char *tool_path,
              const char *libdir,
              const char *libdir_on_host,
              GError **error)
{
  g_autofree gchar *dri = g_build_filename (libdir, "dri", NULL);
  g_autofree gchar *s2tc = g_build_filename (libdir, "libtxc_dxtn.so", NULL);

  /* mount_runtime_on_scratch can't own any fds, because if it did,
   * flatpak_bwrap_append_bwrap() would steal them. */
  g_return_val_if_fail (mount_runtime_on_scratch->fds == NULL
                        || mount_runtime_on_scratch->fds->len == 0,
                        FALSE);

  if (g_file_test (dri, G_FILE_TEST_IS_DIR))
    {
      g_autoptr(FlatpakBwrap) temp_bwrap = NULL;
      g_autofree gchar *expr = NULL;
      g_autofree gchar *host_dri = NULL;
      g_autofree gchar *dest_dri = NULL;

      expr = g_strdup_printf ("only-dependencies:if-exists:path-match:%s/dri/*.so",
                              libdir);

      temp_bwrap = flatpak_bwrap_new (NULL);
      g_warn_if_fail (mount_runtime_on_scratch->fds == NULL
                      || mount_runtime_on_scratch->fds->len == 0);
      flatpak_bwrap_append_bwrap (temp_bwrap, mount_runtime_on_scratch);
      flatpak_bwrap_add_args (temp_bwrap,
                              tool_path,
                              "--container", scratch,
                              "--link-target", "/run/host",
                              "--dest", libdir_on_host,
                              "--provider", "/",
                              expr,
                              NULL);
      flatpak_bwrap_finish (temp_bwrap);

      if (!pv_bwrap_run_sync (temp_bwrap, NULL, error))
        return FALSE;

      g_clear_pointer (&temp_bwrap, flatpak_bwrap_free);

      host_dri = g_build_filename ("/run/host", libdir, "dri", NULL);
      dest_dri = g_build_filename (libdir_on_host, "dri", NULL);
      temp_bwrap = flatpak_bwrap_new (NULL);
      flatpak_bwrap_add_args (temp_bwrap,
                              bwrap->argv->pdata[0],
                              "--ro-bind", "/", "/",
                              "--tmpfs", "/run",
                              "--ro-bind", "/", "/run/host",
                              "--bind", overrides, overrides,
                              "sh", "-c",
                              "ln -fns \"$1\"/* \"$2\"",
                              "sh",   /* $0 */
                              host_dri,
                              dest_dri,
                              NULL);
      flatpak_bwrap_finish (temp_bwrap);

      if (!pv_bwrap_run_sync (temp_bwrap, NULL, error))
        return FALSE;
    }

  if (g_file_test (s2tc, G_FILE_TEST_EXISTS))
    {
      g_autoptr(FlatpakBwrap) temp_bwrap = NULL;
      g_autofree gchar *expr = NULL;

      expr = g_strdup_printf ("path-match:%s", s2tc);
      temp_bwrap = flatpak_bwrap_new (NULL);
      g_warn_if_fail (mount_runtime_on_scratch->fds == NULL
                      || mount_runtime_on_scratch->fds->len == 0);
      flatpak_bwrap_append_bwrap (temp_bwrap, mount_runtime_on_scratch);
      flatpak_bwrap_add_args (temp_bwrap,
                              tool_path,
                              "--container", scratch,
                              "--link-target", "/run/host",
                              "--dest", libdir_on_host,
                              "--provider", "/",
                              expr,
                              NULL);
      flatpak_bwrap_finish (temp_bwrap);

      if (!pv_bwrap_run_sync (temp_bwrap, NULL, error))
        return FALSE;
    }

  return TRUE;
}

/*
 * Try to make sure we have all the locales we need, by running
 * the helper from steam-runtime-tools in the container. If this
 * fails, it isn't fatal - carry on anyway.
 *
 * @bwrap must be set up to have the same libc that we will be using
 * for the container.
 */
static void
ensure_locales (gboolean on_host,
                const char *tools_dir,
                FlatpakBwrap *bwrap,
                const char *overrides)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(FlatpakBwrap) run_locale_gen = NULL;
  g_autofree gchar *locale_gen = NULL;
  g_autofree gchar *locales = g_build_filename (overrides, "locales", NULL);
  g_autoptr(GDir) dir = NULL;
  int exit_status;

  /* bwrap can't own any fds yet, because if it did,
   * flatpak_bwrap_append_bwrap() would steal them. */
  g_return_if_fail (bwrap->fds == NULL || bwrap->fds->len == 0);

  g_mkdir (locales, 0700);

  run_locale_gen = flatpak_bwrap_new (NULL);

  if (on_host)
    {
      locale_gen = g_build_filename (tools_dir,
                                     "pressure-vessel-locale-gen",
                                     NULL);

      flatpak_bwrap_add_args (run_locale_gen,
                              bwrap->argv->pdata[0],
                              "--ro-bind", "/", "/",
                              NULL);
      pv_bwrap_add_api_filesystems (run_locale_gen);
      flatpak_bwrap_add_args (run_locale_gen,
                              "--bind", locales, locales,
                              "--chdir", locales,
                              locale_gen,
                              "--verbose",
                              NULL);
    }
  else
    {
      locale_gen = g_build_filename ("/run/host/tools",
                                     "pressure-vessel-locale-gen",
                                     NULL);

      flatpak_bwrap_append_bwrap (run_locale_gen, bwrap);
      pv_bwrap_copy_tree (run_locale_gen, overrides, "/overrides");

      if (!flatpak_bwrap_bundle_args (run_locale_gen, 1, -1, FALSE,
                                      &local_error))
        {
          g_warning ("Unable to set up locale-gen command: %s",
                     local_error->message);
          g_clear_error (&local_error);
        }

      flatpak_bwrap_add_args (run_locale_gen,
                              "--ro-bind", tools_dir, "/run/host/tools",
                              "--bind", locales, "/overrides/locales",
                              "--chdir", "/overrides/locales",
                              locale_gen,
                              "--verbose",
                              NULL);
    }

  flatpak_bwrap_finish (run_locale_gen);

  /* locale-gen exits 72 (EX_OSFILE) if it had to correct for
   * missing locales at OS level. This is not an error. */
  if (!pv_bwrap_run_sync (run_locale_gen, &exit_status, &local_error))
    {
      if (exit_status == EX_OSFILE)
        g_debug ("pressure-vessel-locale-gen created missing locales");
      else
        g_warning ("Unable to generate locales: %s", local_error->message);

      g_clear_error (&local_error);
    }
  else
    {
      g_debug ("No locales generated");
    }

  dir = g_dir_open (locales, 0, NULL);

  /* If the directory is not empty, make it the container's LOCPATH */
  if (dir != NULL && g_dir_read_name (dir) != NULL)
    {
      g_autoptr(GString) locpath = NULL;

      g_debug ("%s is non-empty", locales);

      locpath = g_string_new ("/overrides/locales");
      search_path_append (locpath, g_getenv ("LOCPATH"));
      flatpak_bwrap_add_args (bwrap,
                              "--setenv", "LOCPATH", locpath->str,
                              NULL);
    }
  else
    {
      g_debug ("%s is empty", locales);
    }
}

typedef enum
{
  ICD_KIND_NONEXISTENT,
  ICD_KIND_ABSOLUTE,
  ICD_KIND_SONAME
} IcdKind;

typedef struct
{
  /* (type SrtEglIcd) or (type SrtVulkanIcd) */
  gpointer icd;
  gchar *resolved_library;
  /* Last entry is always NONEXISTENT */
  IcdKind kinds[G_N_ELEMENTS (multiarch_tuples)];
  /* Last entry is always NULL */
  gchar *paths_in_container[G_N_ELEMENTS (multiarch_tuples)];
} IcdDetails;

static IcdDetails *
icd_details_new (gpointer icd)
{
  IcdDetails *self;
  gsize i;

  g_return_val_if_fail (G_IS_OBJECT (icd), NULL);
  g_return_val_if_fail (SRT_IS_EGL_ICD (icd) || SRT_IS_VULKAN_ICD (icd),
                        NULL);

  self = g_slice_new0 (IcdDetails);
  self->icd = g_object_ref (icd);
  self->resolved_library = NULL;

  for (i = 0; i < G_N_ELEMENTS (multiarch_tuples); i++)
    {
      self->kinds[i] = ICD_KIND_NONEXISTENT;
      self->paths_in_container[i] = NULL;
    }

  return self;
}

static void
icd_details_free (IcdDetails *self)
{
  gsize i;

  g_object_unref (self->icd);
  g_free (self->resolved_library);

  for (i = 0; i < G_N_ELEMENTS (multiarch_tuples); i++)
    g_free (self->paths_in_container[i]);

  g_slice_free (IcdDetails, self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (IcdDetails, icd_details_free)

static gboolean
bind_icd (gsize multiarch_index,
          gsize sequence_number,
          const char *tool_path,
          FlatpakBwrap *mount_runtime_on_scratch,
          const char *scratch,
          const char *libdir_on_host,
          const char *libdir_in_container,
          const char *subdir,
          IcdDetails *details,
          GError **error)
{
  static const char options[] = "if-exists:if-same-abi";
  g_autofree gchar *on_host = NULL;
  g_autofree gchar *pattern = NULL;
  g_autofree gchar *dependency_pattern = NULL;
  g_autofree gchar *seq_str = NULL;
  const char *mode;
  g_autoptr(FlatpakBwrap) temp_bwrap = NULL;

  g_return_val_if_fail (tool_path != NULL, FALSE);
  g_return_val_if_fail (mount_runtime_on_scratch != NULL, FALSE);
  g_return_val_if_fail (mount_runtime_on_scratch->fds == NULL
                        || mount_runtime_on_scratch->fds->len == 0,
                        FALSE);
  g_return_val_if_fail (scratch != NULL, FALSE);
  g_return_val_if_fail (libdir_on_host != NULL, FALSE);
  g_return_val_if_fail (subdir != NULL, FALSE);
  g_return_val_if_fail (multiarch_index < G_N_ELEMENTS (multiarch_tuples) - 1,
                        FALSE);
  g_return_val_if_fail (details != NULL, FALSE);
  g_return_val_if_fail (details->resolved_library != NULL, FALSE);
  g_return_val_if_fail (details->kinds[multiarch_index] == ICD_KIND_NONEXISTENT,
                        FALSE);
  g_return_val_if_fail (details->paths_in_container[multiarch_index] == NULL,
                        FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (g_path_is_absolute (details->resolved_library))
    {
      details->kinds[multiarch_index] = ICD_KIND_ABSOLUTE;
      mode = "path";

      /* Because the ICDs might have collisions among their
       * basenames (might differ only by directory), we put each
       * in its own numbered directory. */
      seq_str = g_strdup_printf ("%" G_GSIZE_FORMAT, sequence_number);
      on_host = g_build_filename (libdir_on_host, subdir, seq_str, NULL);

      g_debug ("Ensuring %s exists", on_host);

      if (g_mkdir_with_parents (on_host, 0700) != 0)
        return glnx_throw_errno_prefix (error, "Unable to create %s", on_host);
    }
  else
    {
      /* ICDs in the default search path by definition can't collide:
       * one of them is the first one we find, and we use that one. */
      details->kinds[multiarch_index] = ICD_KIND_SONAME;
      mode = "soname";
    }

  pattern = g_strdup_printf ("no-dependencies:even-if-older:%s:%s:%s",
                             options, mode, details->resolved_library);
  dependency_pattern = g_strdup_printf ("only-dependencies:%s:%s:%s",
                                        options, mode, details->resolved_library);

  temp_bwrap = flatpak_bwrap_new (NULL);
  flatpak_bwrap_append_bwrap (temp_bwrap, mount_runtime_on_scratch);
  flatpak_bwrap_add_args (temp_bwrap,
                          tool_path,
                          "--container", scratch,
                          "--link-target", "/run/host",
                          "--dest", on_host == NULL ? libdir_on_host : on_host,
                          "--provider", "/",
                          pattern,
                          NULL);
  flatpak_bwrap_finish (temp_bwrap);

  if (!pv_bwrap_run_sync (temp_bwrap, NULL, error))
    return FALSE;

  g_clear_pointer (&temp_bwrap, flatpak_bwrap_free);

  if (on_host != NULL)
    {
      /* Try to remove the directory we created. If it succeeds, then we
       * can optimize slightly by not capturing the dependencies: there's
       * no point, because we know we didn't create a symlink to the ICD
       * itself. (It must have been nonexistent or for a different ABI.) */
      if (g_rmdir (on_host) == 0)
        {
          details->kinds[multiarch_index] = ICD_KIND_NONEXISTENT;
          return TRUE;
        }
    }

  temp_bwrap = flatpak_bwrap_new (NULL);
  g_warn_if_fail (mount_runtime_on_scratch->fds == NULL
                  || mount_runtime_on_scratch->fds->len == 0);
  flatpak_bwrap_append_bwrap (temp_bwrap, mount_runtime_on_scratch);
  flatpak_bwrap_add_args (temp_bwrap,
                          tool_path,
                          "--container", scratch,
                          "--link-target", "/run/host",
                          "--dest", libdir_on_host,
                          "--provider", "/",
                          dependency_pattern,
                          NULL);
  flatpak_bwrap_finish (temp_bwrap);

  if (!pv_bwrap_run_sync (temp_bwrap, NULL, error))
    return FALSE;

  g_clear_pointer (&temp_bwrap, flatpak_bwrap_free);

  if (details->kinds[multiarch_index] == ICD_KIND_ABSOLUTE)
    {
      g_assert (seq_str != NULL);
      g_assert (on_host != NULL);
      details->paths_in_container[multiarch_index] = g_build_filename (libdir_in_container,
                                                                       subdir,
                                                                       seq_str,
                                                                       glnx_basename (details->resolved_library),
                                                                       NULL);
    }

  return TRUE;
}

static gboolean
bind_runtime (FlatpakBwrap *bwrap,
              const char *tools_dir,
              const char *runtime,
              const char *overrides,
              const char *scratch,
              GError **error)
{
  static const char * const bind_mutable[] =
  {
    "etc",
    "var/cache",
    "var/lib"
  };
  static const char * const dont_bind[] =
  {
    "/etc/group",
    "/etc/passwd",
    "/etc/host.conf",
    "/etc/hosts",
    "/etc/localtime",
    "/etc/machine-id",
    "/etc/resolv.conf",
    "/var/lib/dbus",
    "/var/lib/dhcp",
    "/var/lib/sudo",
    "/var/lib/urandom",
    NULL
  };
  g_autofree gchar *xrd = g_strdup_printf ("/run/user/%ld", (long) geteuid ());
  gsize i, j;
  const gchar *member;
  g_autoptr(GString) dri_path = g_string_new ("");
  g_autoptr(GString) egl_path = g_string_new ("");
  g_autoptr(GString) vulkan_path = g_string_new ("");
  gboolean any_architecture_works = FALSE;
  gboolean any_libc_from_host = FALSE;
  gboolean all_libc_from_host = TRUE;
  g_autofree gchar *localedef = NULL;
  g_autofree gchar *dir_on_host = NULL;
  g_autoptr(SrtSystemInfo) system_info = srt_system_info_new (NULL);
  g_autoptr(SrtObjectList) egl_icds = NULL;
  g_autoptr(SrtObjectList) vulkan_icds = NULL;
  g_autoptr(GPtrArray) egl_icd_details = NULL;      /* (element-type IcdDetails) */
  g_autoptr(GPtrArray) vulkan_icd_details = NULL;   /* (element-type IcdDetails) */
  guint n_egl_icds;
  guint n_vulkan_icds;
  const GList *icd_iter;

  g_return_val_if_fail (tools_dir != NULL, FALSE);
  g_return_val_if_fail (runtime != NULL, FALSE);
  g_return_val_if_fail (overrides != NULL, FALSE);
  g_return_val_if_fail (scratch != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!pv_bwrap_bind_usr (bwrap, runtime, "/", error))
    return FALSE;

  flatpak_bwrap_add_args (bwrap,
                          "--setenv", "XDG_RUNTIME_DIR", xrd,
                          "--tmpfs", "/run",
                          "--tmpfs", "/tmp",
                          "--tmpfs", "/var",
                          "--symlink", "../run", "/var/run",
                          NULL);

  for (i = 0; i < G_N_ELEMENTS (bind_mutable); i++)
    {
      g_autofree gchar *path = g_build_filename (runtime, bind_mutable[i],
                                                 NULL);
      g_autoptr(GDir) dir = NULL;

      dir = g_dir_open (path, 0, NULL);

      if (dir == NULL)
        continue;

      for (member = g_dir_read_name (dir);
           member != NULL;
           member = g_dir_read_name (dir))
        {
          g_autofree gchar *dest = g_build_filename ("/", bind_mutable[i],
                                                     member, NULL);
          g_autofree gchar *full = NULL;
          g_autofree gchar *target = NULL;

          if (g_strv_contains (dont_bind, dest))
            continue;

          full = g_build_filename (runtime, bind_mutable[i], member, NULL);
          target = glnx_readlinkat_malloc (-1, full, NULL, NULL);

          if (target != NULL)
            flatpak_bwrap_add_args (bwrap, "--symlink", target, dest, NULL);
          else
            flatpak_bwrap_add_args (bwrap, "--ro-bind", full, dest, NULL);
        }
    }

  if (g_file_test ("/etc/machine-id", G_FILE_TEST_EXISTS))
    {
      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind", "/etc/machine-id", "/etc/machine-id",
                              "--symlink", "/etc/machine-id",
                              "/var/lib/dbus/machine-id",
                              NULL);
    }
  else if (g_file_test ("/var/lib/dbus/machine-id", G_FILE_TEST_EXISTS))
    {
      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind", "/var/lib/dbus/machine-id",
                              "/etc/machine-id",
                              "--symlink", "/etc/machine-id",
                              "/var/lib/dbus/machine-id",
                              NULL);
    }

  if (g_file_test ("/etc/resolv.conf", G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--ro-bind", "/etc/resolv.conf", "/etc/resolv.conf",
                            NULL);
  if (g_file_test ("/etc/host.conf", G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--ro-bind", "/etc/host.conf", "/etc/host.conf",
                            NULL);
  if (g_file_test ("/etc/hosts", G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--ro-bind", "/etc/hosts", "/etc/hosts",
                            NULL);

  /* TODO: Synthesize a passwd with only the user and nobody,
   * like Flatpak does? */
  if (g_file_test ("/etc/passwd", G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--ro-bind", "/etc/passwd", "/etc/passwd",
                            NULL);
  if (g_file_test ("/etc/group", G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--ro-bind", "/etc/group", "/etc/group",
                            NULL);

  g_debug ("Enumerating EGL ICDs on host system...");
  egl_icds = srt_system_info_list_egl_icds (system_info, multiarch_tuples);
  n_egl_icds = g_list_length (egl_icds);

  egl_icd_details = g_ptr_array_new_full (n_egl_icds,
                                          (GDestroyNotify) G_CALLBACK (icd_details_free));

  for (icd_iter = egl_icds, j = 0;
       icd_iter != NULL;
       icd_iter = icd_iter->next, j++)
    {
      SrtEglIcd *icd = icd_iter->data;
      const gchar *path = srt_egl_icd_get_json_path (icd);
      GError *local_error = NULL;

      if (!srt_egl_icd_check_error (icd, &local_error))
        {
          g_debug ("Failed to load EGL ICD #%" G_GSIZE_FORMAT  " from %s: %s",
                   j, path, local_error->message);
          g_clear_error (&local_error);
          continue;
        }

      g_debug ("EGL ICD #%" G_GSIZE_FORMAT " at %s: %s",
               j, path, srt_egl_icd_get_library_path (icd));

      g_ptr_array_add (egl_icd_details, icd_details_new (icd));
    }

  g_debug ("Enumerating Vulkan ICDs on host system...");
  vulkan_icds = srt_system_info_list_vulkan_icds (system_info,
                                                  multiarch_tuples);
  n_vulkan_icds = g_list_length (vulkan_icds);

  vulkan_icd_details = g_ptr_array_new_full (n_vulkan_icds,
                                             (GDestroyNotify) G_CALLBACK (icd_details_free));

  for (icd_iter = vulkan_icds, j = 0;
       icd_iter != NULL;
       icd_iter = icd_iter->next, j++)
    {
      SrtVulkanIcd *icd = icd_iter->data;
      const gchar *path = srt_vulkan_icd_get_json_path (icd);
      GError *local_error = NULL;

      if (!srt_vulkan_icd_check_error (icd, &local_error))
        {
          g_debug ("Failed to load Vulkan ICD #%" G_GSIZE_FORMAT " from %s: %s",
                   j, path, local_error->message);
          g_clear_error (&local_error);
          continue;
        }

      g_debug ("Vulkan ICD #%" G_GSIZE_FORMAT " at %s: %s",
               j, path, srt_vulkan_icd_get_library_path (icd));

      g_ptr_array_add (vulkan_icd_details, icd_details_new (icd));
    }

  g_assert (multiarch_tuples[G_N_ELEMENTS (multiarch_tuples) - 1] == NULL);

  for (i = 0; i < G_N_ELEMENTS (multiarch_tuples) - 1; i++)
    {
      g_autofree gchar *tool = g_strdup_printf ("%s-capsule-capture-libs",
                                                multiarch_tuples[i]);
      g_autofree gchar *tool_path = NULL;
      g_autofree gchar *ld_so = NULL;
      const gchar *argv[] = { NULL, "--print-ld.so", NULL };

      g_debug ("Checking for %s libraries...", multiarch_tuples[i]);

      tool_path = g_build_filename (tools_dir, tool, NULL);
      argv[0] = tool_path;

      /* This has the side-effect of testing whether we can run binaries
       * for this architecture on the host system. */
      ld_so = capture_output (argv, NULL);

      if (ld_so != NULL)
        {
          g_auto(GStrv) dirs = NULL;
          g_autoptr(FlatpakBwrap) mount_runtime_on_scratch = NULL;
          g_autoptr(FlatpakBwrap) temp_bwrap = NULL;
          g_autofree gchar *libdir_in_container = g_build_filename ("/overrides",
                                                                    "lib",
                                                                    multiarch_tuples[i],
                                                                    NULL);
          g_autofree gchar *libdir_on_host = g_build_filename (overrides, "lib",
                                                               multiarch_tuples[i],
                                                               NULL);
          g_autofree gchar *this_dri_path_on_host = g_build_filename (libdir_on_host,
                                                                      "dri", NULL);
          g_autofree gchar *this_dri_path_in_container = g_build_filename (libdir_in_container,
                                                                           "dri", NULL);
          g_autofree gchar *libc = NULL;
          g_autofree gchar *ld_so_in_runtime = NULL;
          const gchar *libqual = NULL;

          temp_bwrap = flatpak_bwrap_new (NULL);
          flatpak_bwrap_add_args (temp_bwrap,
                                  bwrap->argv->pdata[0],
                                  NULL);

          if (!pv_bwrap_bind_usr (temp_bwrap, runtime, "/", error))
            return FALSE;

          flatpak_bwrap_add_args (temp_bwrap,
                                  "readlink", "-e", ld_so,
                                  NULL);
          flatpak_bwrap_finish (temp_bwrap);

          ld_so_in_runtime = capture_output ((const char * const *) temp_bwrap->argv->pdata,
                                             NULL);

          g_clear_pointer (&temp_bwrap, flatpak_bwrap_free);

          if (ld_so_in_runtime == NULL)
            {
              g_debug ("Container does not have %s so it cannot run "
                       "%s binaries", ld_so, multiarch_tuples[i]);
              continue;
            }

          any_architecture_works = TRUE;
          g_debug ("Container path: %s -> %s", ld_so, ld_so_in_runtime);

          search_path_append (dri_path, this_dri_path_in_container);

          g_mkdir_with_parents (libdir_on_host, 0755);
          g_mkdir_with_parents (this_dri_path_on_host, 0755);

          mount_runtime_on_scratch = flatpak_bwrap_new (NULL);
          flatpak_bwrap_add_args (mount_runtime_on_scratch,
                                  bwrap->argv->pdata[0],
                                  "--ro-bind", "/", "/",
                                  "--bind", overrides, overrides,
                                  "--tmpfs", scratch,
                                  NULL);
          if (!pv_bwrap_bind_usr (mount_runtime_on_scratch, runtime, scratch,
                         error))
            return FALSE;

          g_debug ("Collecting GLX drivers from host system...");

          temp_bwrap = flatpak_bwrap_new (NULL);
          /* mount_runtime_on_scratch can't own any fds, because if it did,
           * flatpak_bwrap_append_bwrap() would steal them. */
          g_warn_if_fail (mount_runtime_on_scratch->fds == NULL
                          || mount_runtime_on_scratch->fds->len == 0);
          flatpak_bwrap_append_bwrap (temp_bwrap, mount_runtime_on_scratch);
          flatpak_bwrap_add_args (temp_bwrap,
                                  tool_path,
                                  "--container", scratch,
                                  "--link-target", "/run/host",
                                  "--dest", libdir_on_host,
                                  "--provider", "/",
                                  "gl:",
                                  NULL);
          flatpak_bwrap_finish (temp_bwrap);

          if (!pv_bwrap_run_sync (temp_bwrap, NULL, error))
            return FALSE;

          g_clear_pointer (&temp_bwrap, flatpak_bwrap_free);

          temp_bwrap = flatpak_bwrap_new (NULL);
          g_warn_if_fail (mount_runtime_on_scratch->fds == NULL
                          || mount_runtime_on_scratch->fds->len == 0);
          flatpak_bwrap_append_bwrap (temp_bwrap, mount_runtime_on_scratch);
          flatpak_bwrap_add_args (temp_bwrap,
                                  tool_path,
                                  "--container", scratch,
                                  "--link-target", "/run/host",
                                  "--dest", libdir_on_host,
                                  "--provider", "/",
                                  "if-exists:even-if-older:soname-match:libEGL.so.*",
                                  "if-exists:even-if-older:soname-match:libEGL_nvidia.so.*",
                                  "if-exists:even-if-older:soname-match:libGL.so.*",
                                  "if-exists:even-if-older:soname-match:libGLESv1_CM.so.*",
                                  "if-exists:even-if-older:soname-match:libGLESv1_CM_nvidia.so.*",
                                  "if-exists:even-if-older:soname-match:libGLESv2.so.*",
                                  "if-exists:even-if-older:soname-match:libGLESv2_nvidia.so.*",
                                  "if-exists:even-if-older:soname-match:libGLX.so.*",
                                  "if-exists:even-if-older:soname-match:libGLX_nvidia.so.*",
                                  "if-exists:even-if-older:soname-match:libGLX_indirect.so.*",
                                  "if-exists:even-if-older:soname-match:libGLdispatch.so.*",
                                  "if-exists:even-if-older:soname-match:libOpenGL.so.*",
                                  "if-exists:even-if-older:soname-match:libcuda.so.*",
                                  "if-exists:even-if-older:soname-match:libglx.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-cbl.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-cfg.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-compiler.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-egl-wayland.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-eglcore.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-encode.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-fatbinaryloader.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-fbc.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-glcore.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-glsi.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-glvkspirv.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-ifr.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-ml.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-opencl.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-opticalflow.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-ptxjitcompiler.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-rtcore.so.*",
                                  "if-exists:even-if-older:soname-match:libnvidia-tls.so.*",
                                  "if-exists:even-if-older:soname-match:libOpenCL.so.*",
                                  "if-exists:even-if-older:soname-match:libvdpau_nvidia.so.*",
                                  NULL);
          flatpak_bwrap_finish (temp_bwrap);

          if (!pv_bwrap_run_sync (temp_bwrap, NULL, error))
            return FALSE;

          g_clear_pointer (&temp_bwrap, flatpak_bwrap_free);

          g_debug ("Collecting %s EGL drivers from host system...",
                   multiarch_tuples[i]);

          for (j = 0; j < egl_icd_details->len; j++)
            {
              IcdDetails *details = g_ptr_array_index (egl_icd_details, j);
              SrtEglIcd *icd = SRT_EGL_ICD (details->icd);

              if (!srt_egl_icd_check_error (icd, NULL))
                continue;

              details->resolved_library = srt_egl_icd_resolve_library_path (icd);
              g_assert (details->resolved_library != NULL);

              if (!bind_icd (i,
                             j,
                             tool_path,
                             mount_runtime_on_scratch,
                             scratch,
                             libdir_on_host,
                             libdir_in_container,
                             "glvnd",
                             details,
                             error))
                return FALSE;
            }

          g_debug ("Collecting %s Vulkan drivers from host system...",
                   multiarch_tuples[i]);

          temp_bwrap = flatpak_bwrap_new (NULL);
          g_warn_if_fail (mount_runtime_on_scratch->fds == NULL
                          || mount_runtime_on_scratch->fds->len == 0);
          flatpak_bwrap_append_bwrap (temp_bwrap, mount_runtime_on_scratch);
          flatpak_bwrap_add_args (temp_bwrap,
                                  tool_path,
                                  "--container", scratch,
                                  "--link-target", "/run/host",
                                  "--dest", libdir_on_host,
                                  "--provider", "/",
                                  "if-exists:if-same-abi:soname:libvulkan.so.1",
                                  NULL);
          flatpak_bwrap_finish (temp_bwrap);

          if (!pv_bwrap_run_sync (temp_bwrap, NULL, error))
            return FALSE;

          g_clear_pointer (&temp_bwrap, flatpak_bwrap_free);

          for (j = 0; j < vulkan_icd_details->len; j++)
            {
              IcdDetails *details = g_ptr_array_index (vulkan_icd_details, j);
              SrtVulkanIcd *icd = SRT_VULKAN_ICD (details->icd);

              if (!srt_vulkan_icd_check_error (icd, NULL))
                continue;

              details->resolved_library = srt_vulkan_icd_resolve_library_path (icd);
              g_assert (details->resolved_library != NULL);

              if (!bind_icd (i,
                             j,
                             tool_path,
                             mount_runtime_on_scratch,
                             scratch,
                             libdir_on_host,
                             libdir_in_container,
                             "vulkan",
                             details,
                             error))
                return FALSE;
            }

          libc = g_build_filename (libdir_on_host, "libc.so.6", NULL);

          /* If we are going to use the host system's libc6 (likely)
           * then we have to use its ld.so too. */
          if (g_file_test (libc, G_FILE_TEST_IS_SYMLINK))
            {
              g_autofree gchar *ld_so_in_host = NULL;

              g_debug ("Making host ld.so visible in container");

              ld_so_in_host = flatpak_canonicalize_filename (ld_so);
              g_debug ("Host path: %s -> %s", ld_so, ld_so_in_host);

              g_clear_pointer (&temp_bwrap, flatpak_bwrap_free);

              temp_bwrap = flatpak_bwrap_new (NULL);
              flatpak_bwrap_add_args (temp_bwrap,
                                      bwrap->argv->pdata[0],
                                      NULL);

              if (!pv_bwrap_bind_usr (temp_bwrap, runtime, "/", error))
                return FALSE;

              flatpak_bwrap_add_args (temp_bwrap,
                                      "readlink", "-f", ld_so,
                                      NULL);
              flatpak_bwrap_finish (temp_bwrap);

              g_debug ("Container path: %s -> %s", ld_so, ld_so_in_runtime);
              flatpak_bwrap_add_args (bwrap,
                                      "--ro-bind", ld_so_in_host,
                                      ld_so_in_runtime,
                                      NULL);

              /* Collect miscellaneous libraries that libc might dlopen.
               * At the moment this is just libidn2. */
              temp_bwrap = flatpak_bwrap_new (NULL);
              g_warn_if_fail (mount_runtime_on_scratch->fds == NULL
                              || mount_runtime_on_scratch->fds->len == 0);
              flatpak_bwrap_append_bwrap (temp_bwrap, mount_runtime_on_scratch);
              flatpak_bwrap_add_args (temp_bwrap,
                                      tool_path,
                                      "--container", scratch,
                                      "--link-target", "/run/host",
                                      "--dest", libdir_on_host,
                                      "--provider", "/",
                                      "if-exists:libidn2.so.0",
                                      NULL);
              flatpak_bwrap_finish (temp_bwrap);

              if (!pv_bwrap_run_sync (temp_bwrap, NULL, error))
                return FALSE;

              g_clear_pointer (&temp_bwrap, flatpak_bwrap_free);

              any_libc_from_host = TRUE;
            }
          else
            {
              all_libc_from_host = FALSE;
            }

          /* /lib32 or /lib64 */
          g_assert (i < G_N_ELEMENTS (libquals));
          libqual = libquals[i];

          dirs = g_new0 (gchar *, 7);
          dirs[0] = g_build_filename ("/lib", multiarch_tuples[i], NULL);
          dirs[1] = g_build_filename ("/usr", "lib", multiarch_tuples[i], NULL);
          dirs[2] = g_strdup ("/lib");
          dirs[3] = g_strdup ("/usr/lib");
          dirs[4] = g_build_filename ("/", libqual, NULL);
          dirs[5] = g_build_filename ("/usr", libqual, NULL);

          for (j = 0; j < 6; j++)
            {
              if (!try_bind_dri (bwrap, mount_runtime_on_scratch,
                                 overrides, scratch, tool_path, dirs[j],
                                 libdir_on_host, error))
                return FALSE;
            }
        }
      else
        {
          g_debug ("Cannot determine ld.so for %s", multiarch_tuples[i]);
        }
    }

  if (!any_architecture_works)
    {
      GString *archs = g_string_new ("");

      g_assert (multiarch_tuples[G_N_ELEMENTS (multiarch_tuples) - 1] == NULL);

      for (i = 0; i < G_N_ELEMENTS (multiarch_tuples) - 1; i++)
        {
          if (archs->len > 0)
            g_string_append (archs, ", ");

          g_string_append (archs, multiarch_tuples[i]);
        }

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "None of the supported CPU architectures are common to "
                   "the host system and the container (tried: %s)",
                   archs->str);
      g_string_free (archs, TRUE);
      return FALSE;
    }

  if (any_libc_from_host && !all_libc_from_host)
    {
      /*
       * This shouldn't happen. It would mean that there exist at least
       * two architectures (let's say aaa and bbb) for which we have:
       * host libc6:aaa < container libc6 < host libc6:bbb
       * (we know that the container's libc6:aaa and libc6:bbb are
       * constrained to be the same version because that's how multiarch
       * works).
       *
       * If the host system locales work OK with both the aaa and bbb
       * versions, let's assume they will also work with the intermediate
       * version from the container...
       */
      g_warning ("Using glibc from host system for some but not all "
                 "architectures! Arbitrarily using host locales.");
    }

  if (any_libc_from_host)
    {
      g_debug ("Making host locale data visible in container");

      if (g_file_test ("/usr/lib/locale", G_FILE_TEST_EXISTS))
        flatpak_bwrap_add_args (bwrap,
                                "--ro-bind", "/usr/lib/locale",
                                "/usr/lib/locale",
                                NULL);

      if (g_file_test ("/usr/share/i18n", G_FILE_TEST_EXISTS))
        flatpak_bwrap_add_args (bwrap,
                                "--ro-bind", "/usr/share/i18n",
                                "/usr/share/i18n",
                                NULL);

      localedef = g_find_program_in_path ("localedef");

      if (localedef == NULL)
        {
          g_warning ("Cannot find localedef in PATH");
        }
      else
        {
          g_autofree gchar *target = g_build_filename ("/run/host",
                                                       localedef, NULL);

          flatpak_bwrap_add_args (bwrap,
                                  "--symlink", target,
                                      "/overrides/bin/localedef",
                                  NULL);
        }
    }
  else
    {
      g_debug ("Using included locale data from container");
    }

  g_debug ("Setting up EGL ICD JSON...");

  dir_on_host = g_build_filename (overrides,
                                  "share", "glvnd", "egl_vendor.d", NULL);

  if (g_mkdir_with_parents (dir_on_host, 0700) != 0)
    {
      glnx_throw_errno_prefix (error, "Unable to create %s", dir_on_host);
      return FALSE;
    }

  for (j = 0; j < egl_icd_details->len; j++)
    {
      IcdDetails *details = g_ptr_array_index (egl_icd_details, j);
      SrtEglIcd *icd = SRT_EGL_ICD (details->icd);
      gboolean need_host_json = FALSE;

      if (!srt_egl_icd_check_error (icd, NULL))
        continue;

      for (i = 0; i < G_N_ELEMENTS (multiarch_tuples) - 1; i++)
        {
          g_assert (i < G_N_ELEMENTS (details->kinds));
          g_assert (i < G_N_ELEMENTS (details->paths_in_container));

          if (details->kinds[i] == ICD_KIND_ABSOLUTE)
            {
              g_autoptr(SrtEglIcd) replacement = NULL;
              g_autofree gchar *json_on_host = NULL;
              g_autofree gchar *json_in_container = NULL;
              g_autofree gchar *json_base = NULL;

              g_assert (details->paths_in_container[i] != NULL);

              json_base = g_strdup_printf ("%" G_GSIZE_FORMAT "-%s.json",
                                           j, multiarch_tuples[i]);
              json_on_host = g_build_filename (dir_on_host, json_base, NULL);
              json_in_container = g_build_filename ("/overrides", "share",
                                                    "glvnd", "egl_vendor.d",
                                                    json_base, NULL);

              replacement = srt_egl_icd_new_replace_library_path (icd,
                                                                  details->paths_in_container[i]);

              if (!srt_egl_icd_write_to_file (replacement, json_on_host,
                                                 error))
                return FALSE;

              search_path_append (egl_path, json_in_container);
            }
          else if (details->kinds[i] == ICD_KIND_SONAME)
            {
              need_host_json = TRUE;
            }
        }

      if (need_host_json)
        {
          g_autofree gchar *json_in_container = NULL;
          g_autofree gchar *json_base = NULL;
          const char *json_on_host = srt_egl_icd_get_json_path (icd);

          json_base = g_strdup_printf ("%" G_GSIZE_FORMAT ".json", j);
          json_in_container = g_build_filename ("/overrides", "share",
                                                "glvnd", "egl_vendor.d",
                                                json_base, NULL);

          flatpak_bwrap_add_args (bwrap,
                                  "--ro-bind", json_on_host, json_in_container,
                                  NULL);
          search_path_append (egl_path, json_in_container);
        }
    }

  g_debug ("Setting up Vulkan ICD JSON...");

  dir_on_host = g_build_filename (overrides,
                                  "share", "vulkan", "icd.d", NULL);

  if (g_mkdir_with_parents (dir_on_host, 0700) != 0)
    {
      glnx_throw_errno_prefix (error, "Unable to create %s", dir_on_host);
      return FALSE;
    }

  for (j = 0; j < vulkan_icd_details->len; j++)
    {
      IcdDetails *details = g_ptr_array_index (vulkan_icd_details, j);
      SrtVulkanIcd *icd = SRT_VULKAN_ICD (details->icd);
      gboolean need_host_json = FALSE;

      if (!srt_vulkan_icd_check_error (icd, NULL))
        continue;

      for (i = 0; i < G_N_ELEMENTS (multiarch_tuples) - 1; i++)
        {
          g_assert (i < G_N_ELEMENTS (details->kinds));
          g_assert (i < G_N_ELEMENTS (details->paths_in_container));

          if (details->kinds[i] == ICD_KIND_ABSOLUTE)
            {
              g_autoptr(SrtVulkanIcd) replacement = NULL;
              g_autofree gchar *json_on_host = NULL;
              g_autofree gchar *json_in_container = NULL;
              g_autofree gchar *json_base = NULL;

              g_assert (details->paths_in_container[i] != NULL);

              json_base = g_strdup_printf ("%" G_GSIZE_FORMAT "-%s.json",
                                           j, multiarch_tuples[i]);
              json_on_host = g_build_filename (dir_on_host, json_base, NULL);
              json_in_container = g_build_filename ("/overrides", "share",
                                                    "vulkan", "icd.d",
                                                    json_base, NULL);

              replacement = srt_vulkan_icd_new_replace_library_path (icd,
                                                                     details->paths_in_container[i]);

              if (!srt_vulkan_icd_write_to_file (replacement, json_on_host,
                                                 error))
                return FALSE;

              search_path_append (vulkan_path, json_in_container);
            }
          else if (details->kinds[i] == ICD_KIND_SONAME)
            {
              need_host_json = TRUE;
            }
        }

      if (need_host_json)
        {
          g_autofree gchar *json_in_container = NULL;
          g_autofree gchar *json_base = NULL;
          const char *json_on_host = srt_vulkan_icd_get_json_path (icd);

          json_base = g_strdup_printf ("%" G_GSIZE_FORMAT ".json", j);
          json_in_container = g_build_filename ("/overrides", "share",
                                                "vulkan", "icd.d",
                                                json_base, NULL);

          flatpak_bwrap_add_args (bwrap,
                                  "--ro-bind", json_on_host, json_in_container,
                                  NULL);
          search_path_append (vulkan_path, json_in_container);
        }
    }

  if (!pv_bwrap_bind_usr (bwrap, "/", "/run/host", error))
    return FALSE;

  ensure_locales (any_libc_from_host, tools_dir, bwrap, overrides);

  if (dri_path->len != 0)
      flatpak_bwrap_add_args (bwrap,
                              "--setenv", "LIBGL_DRIVERS_PATH", dri_path->str,
                              NULL);
  else
      flatpak_bwrap_add_args (bwrap,
                              "--unsetenv", "LIBGL_DRIVERS_PATH",
                              NULL);

  if (egl_path->len != 0)
      flatpak_bwrap_add_args (bwrap,
                              "--setenv", "__EGL_VENDOR_LIBRARY_FILENAMES",
                              egl_path->str, NULL);
  else
      flatpak_bwrap_add_args (bwrap,
                              "--unsetenv", "__EGL_VENDOR_LIBRARY_FILENAMES",
                              NULL);

  flatpak_bwrap_add_args (bwrap,
                          "--unsetenv", "__EGL_VENDOR_LIBRARY_DIRS",
                          NULL);

  if (vulkan_path->len != 0)
      flatpak_bwrap_add_args (bwrap,
                              "--setenv", "VK_ICD_FILENAMES",
                              vulkan_path->str, NULL);
  else
      flatpak_bwrap_add_args (bwrap,
                              "--unsetenv", "VK_ICD_FILENAMES",
                              NULL);

  /* These can add data fds to @bwrap, so they must come last - after
   * other functions stop using @bwrap as a basis for their own bwrap
   * invocations with flatpak_bwrap_append_bwrap().
   * Otherwise, when flatpak_bwrap_append_bwrap() calls
   * flatpak_bwrap_steal_fds(), it will make the original FlatpakBwrap
   * unusable. */

  flatpak_run_add_wayland_args (bwrap);
  flatpak_run_add_x11_args (bwrap, TRUE);
  flatpak_run_add_pulseaudio_args (bwrap);
  flatpak_run_add_session_dbus_args (bwrap);
  flatpak_run_add_system_dbus_args (bwrap);
  pv_bwrap_copy_tree (bwrap, overrides, "/overrides");

  /* /etc/localtime and /etc/resolv.conf can not exist (or be symlinks to
   * non-existing targets), in which case we don't want to attempt to create
   * bogus symlinks or bind mounts, as that will cause flatpak run to fail.
   */
  if (g_file_test ("/etc/localtime", G_FILE_TEST_EXISTS))
    {
      g_autofree char *target = NULL;
      gboolean is_reachable = FALSE;
      g_autofree char *tz = flatpak_get_timezone ();
      g_autofree char *timezone_content = g_strdup_printf ("%s\n", tz);

      target = glnx_readlinkat_malloc (-1, "/etc/localtime", NULL, NULL);

      if (target != NULL)
        {
          g_autoptr(GFile) base_file = NULL;
          g_autoptr(GFile) target_file = NULL;
          g_autofree char *target_canonical = NULL;

          base_file = g_file_new_for_path ("/etc");
          target_file = g_file_resolve_relative_path (base_file, target);
          target_canonical = g_file_get_path (target_file);

          is_reachable = g_str_has_prefix (target_canonical, "/usr/");
        }

      if (is_reachable)
        {
          flatpak_bwrap_add_args (bwrap,
                                  "--symlink", target, "/etc/localtime",
                                  NULL);
        }
      else
        {
          flatpak_bwrap_add_args (bwrap,
                                  "--ro-bind", "/etc/localtime", "/etc/localtime",
                                  NULL);
        }

      flatpak_bwrap_add_args_data (bwrap, "timezone",
                                   timezone_content, -1, "/etc/timezone",
                                   NULL);
    }

  return TRUE;
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

static char **opt_env_if_host = NULL;
static char *opt_fake_home = NULL;
static char *opt_freedesktop_app_id = NULL;
static char *opt_steam_app_id = NULL;
static char *opt_home = NULL;
static gboolean opt_host_fallback = FALSE;
static PvShell opt_shell = PV_SHELL_NONE;
static GPtrArray *opt_ld_preload = NULL;
static char *opt_runtime_base = NULL;
static char *opt_runtime = NULL;
static Tristate opt_share_home = TRISTATE_MAYBE;
static gboolean opt_verbose = FALSE;
static gboolean opt_version = FALSE;
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
  { "test", '\0',
    G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_test,
    "Smoke test pressure-vessel-wrap and exit.", NULL },
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
  g_autofree gchar *tmpdir = NULL;
  g_autofree gchar *pressure_vessel_prefix = NULL;
  g_autofree gchar *scratch = NULL;
  g_autofree gchar *overrides = NULL;
  g_autofree gchar *overrides_bin = NULL;
  g_autofree gchar *bwrap_executable = NULL;
  g_autoptr(GString) ld_library_path = g_string_new ("");
  g_autoptr(GString) bin_path = g_string_new ("");
  g_autoptr(GString) adjusted_ld_preload = g_string_new ("");
  g_autofree gchar *cwd_p = NULL;
  g_autofree gchar *cwd_l = NULL;
  g_autoptr(PvBwrapLock) runtime_lock = NULL;
  const gchar *home;
  g_autofree gchar *bwrap_help = NULL;
  g_autofree gchar *tools_dir = NULL;
  const gchar *bwrap_help_argv[] = { "<bwrap>", "--help", NULL };

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
  opt_freedesktop_app_id = g_strdup (g_getenv ("PRESSURE_VESSEL_FDO_APP_ID"));

  if (opt_freedesktop_app_id != NULL && opt_freedesktop_app_id[0] == '\0')
    g_clear_pointer (&opt_freedesktop_app_id, g_free);

  opt_home = g_strdup (g_getenv ("PRESSURE_VESSEL_HOME"));

  if (opt_home != NULL && opt_home[0] == '\0')
    g_clear_pointer (&opt_home, g_free);

  opt_share_home = tristate_environment ("PRESSURE_VESSEL_SHARE_HOME");
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

  if (opt_version)
    {
      g_print ("%s:\n"
               " Package: pressure-vessel\n"
               " Version: %s\n",
               argv[0], VERSION);
      ret = 0;
      goto out;
    }

  if (argc < 2 && !opt_test)
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
            g_printerr ("%s: --env-if-host argument must be of the form "
                        "NAME=VALUE, not \"%s\"\n",
                        g_get_prgname (), opt_env_if_host[i]);
        }
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

  pressure_vessel_prefix = g_path_get_dirname (tools_dir);

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
  bwrap_executable = check_bwrap (tools_dir);

  if (opt_test)
    {
      if (bwrap_executable == NULL)
        {
          ret = 1;
          goto out;
        }
      else
        {
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
  bwrap_help = capture_output (bwrap_help_argv, error);

  if (bwrap_help == NULL)
    goto out;

  g_debug ("Creating temporary directories...");
  tmpdir = g_dir_make_tmp ("pressure-vessel-wrap.XXXXXX", error);

  if (tmpdir == NULL)
    goto out;

  scratch = g_build_filename (tmpdir, "scratch", NULL);
  g_mkdir (scratch, 0700);
  overrides = g_build_filename (tmpdir, "overrides", NULL);
  g_mkdir (overrides, 0700);
  overrides_bin = g_build_filename (overrides, "bin", NULL);
  g_mkdir (overrides_bin, 0700);

  bwrap = flatpak_bwrap_new (NULL);
  flatpak_bwrap_add_arg (bwrap, bwrap_executable);

  /* Protect the controlling terminal from the app/game, unless we are
   * running an interactive shell in which case that would break its
   * job control. */
  if (opt_terminal != PV_TERMINAL_TTY)
    flatpak_bwrap_add_arg (bwrap, "--new-session");

  pv_bwrap_add_api_filesystems (bwrap);

  if (opt_runtime != NULL && opt_runtime[0] != '\0')
    {
      g_autofree gchar *files_ref = NULL;

      g_debug ("Configuring runtime...");

      /* Take a lock on the runtime until we're finished with setup,
       * to make sure it doesn't get deleted. */
      files_ref = g_build_filename (opt_runtime, ".ref", NULL);
      runtime_lock = pv_bwrap_lock_new (files_ref,
                                        PV_BWRAP_LOCK_FLAGS_CREATE,
                                        error);

      /* If the runtime is being deleted, ... don't use it, I suppose? */
      if (runtime_lock == NULL)
        goto out;

      search_path_append (bin_path, "/overrides/bin");
      search_path_append (bin_path, g_getenv ("PATH"));
      flatpak_bwrap_add_args (bwrap,
                              "--setenv", "PATH",
                              bin_path->str,
                              NULL);

      /* TODO: Adapt the use_ld_so_cache code from Flatpak instead
       * of setting LD_LIBRARY_PATH, for better robustness against
       * games that set their own LD_LIBRARY_PATH ignoring what they
       * got from the environment */
      g_assert (multiarch_tuples[G_N_ELEMENTS (multiarch_tuples) - 1] == NULL);

      for (i = 0; i < G_N_ELEMENTS (multiarch_tuples) - 1; i++)
        {
          g_autofree gchar *ld_path = NULL;

          ld_path = g_build_filename ("/overrides", "lib",
                                     multiarch_tuples[i], NULL);

          search_path_append (ld_library_path, ld_path);
        }

      /* This would be filtered out by a setuid bwrap, so we have to go
       * via --setenv. */
      flatpak_bwrap_add_args (bwrap,
                              "--setenv", "LD_LIBRARY_PATH",
                              ld_library_path->str,
                              NULL);

      if (!bind_runtime (bwrap, tools_dir, opt_runtime, overrides,
                         scratch, error))
        goto out;

      /* Make sure pressure-vessel itself is visible there. */
      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind",
                              pressure_vessel_prefix,
                              "/run/pressure-vessel",
                              NULL);
    }
  else
    {
      flatpak_bwrap_add_args (bwrap,
                              "--bind", "/", "/",
                              NULL);
    }

  /* Protect other users' homes (but guard against the unlikely
   * situation that they don't exist) */
  if (g_file_test ("/home", G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--tmpfs", "/home",
                            NULL);

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
              if (opt_runtime != NULL
                  && opt_runtime[0] != '\0'
                  && (g_str_has_prefix (preload, "/usr/")
                      || g_str_has_prefix (preload, "/lib")))
                {
                  g_autofree gchar *in_run_host = g_build_filename ("/run/host",
                                                                    preload,
                                                                    NULL);

                  /* When using a runtime we can't write to /usr/ or /libQUAL/,
                   * so redirect this preloaded module to the corresponding
                   * location in /run/host. */
                  search_path_append (adjusted_ld_preload, in_run_host);
                }
              else
                {
                  flatpak_bwrap_add_args (bwrap,
                                          "--ro-bind", preload, preload,
                                          NULL);
                  search_path_append (adjusted_ld_preload, preload);
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

  /* Make sure the current working directory (the game we are going to
   * run) is available. Some games write here. */

  g_debug ("Making home directory available...");

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

  /* TODO: Potential future expansion: use --unshare-pid for more isolation */

  /* Put Steam Runtime environment variables back, if /usr is mounted
   * from the host. */
  if (opt_runtime == NULL || opt_runtime[0] == '\0')
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

  /* If we are using a runtime, pass the lock fd to the executed process,
   * and make it act as a subreaper for the game itself.
   *
   * If we were using --unshare-pid then we could use bwrap --sync-fd
   * and rely on bubblewrap's init process for this, but we currently
   * can't do that without breaking gameoverlayrender.so's assumptions. */
  if (runtime_lock != NULL)
    {
      flatpak_bwrap_add_args (bwrap,
                              "/run/pressure-vessel/bin/pressure-vessel-with-lock",
                              "--subreaper",
                              NULL);

      if (pv_bwrap_lock_is_ofd (runtime_lock))
        {
          int fd = pv_bwrap_lock_steal_fd (runtime_lock);
          g_autofree gchar *fd_str = NULL;

          g_debug ("Passing lock fd %d down to with-lock", fd);
          flatpak_bwrap_add_fd (bwrap, fd);
          fd_str = g_strdup_printf ("%d", fd);
          flatpak_bwrap_add_args (bwrap,
                                  "--fd", fd_str,
                                  NULL);
        }
      else
        {
          /*
           * We were unable to take out an open file descriptor lock,
           * so it will be released on fork(). Tell the with-lock process
           * to take out its own compatible lock instead. There will be
           * a short window during which we have lost our lock but the
           * with-lock process has not taken its lock - that's unavoidable
           * if we want to use exec() to replace ourselves with the
           * container.
           *
           * pv_bwrap_bind_usr() arranges for /.ref to either be a
           * symbolic link to /usr/.ref which is the runtime_lock
           * (if opt_runtime is a merged /usr), or the runtime_lock
           * itself (otherwise).
           */
          g_debug ("Telling process in container to lock /.ref");
          flatpak_bwrap_add_args (bwrap,
                                  "--lock-file", "/.ref",
                                  NULL);
        }

      flatpak_bwrap_add_args (bwrap,
                              "--",
                              NULL);
    }

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
  if (tmpdir != NULL &&
      !glnx_shutil_rm_rf_at (-1, tmpdir, NULL, error))
    {
      g_warning ("Unable to delete temporary directory: %s",
                 local_error->message);
      g_clear_error (&local_error);
    }

  g_clear_pointer (&tmpdir, g_free);

  flatpak_bwrap_finish (bwrap);
  pv_bwrap_execve (bwrap, error);

out:
  if (local_error != NULL)
    g_warning ("%s", local_error->message);

  if (tmpdir != NULL &&
      !glnx_shutil_rm_rf_at (-1, tmpdir, NULL, error))
    {
      g_warning ("Unable to delete temporary directory: %s",
                 local_error->message);
      g_clear_error (&local_error);
    }

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
