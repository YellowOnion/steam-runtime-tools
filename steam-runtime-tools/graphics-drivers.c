/*
 * Copyright © 2019-2022 Collabora Ltd.
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

#include "steam-runtime-tools/graphics.h"

#include "steam-runtime-tools/glib-backports-internal.h"

#include "steam-runtime-tools/architecture.h"
#include "steam-runtime-tools/architecture-internal.h"
#include "steam-runtime-tools/graphics-internal.h"
#include "steam-runtime-tools/library.h"
#include "steam-runtime-tools/library-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include <gelf.h>
#include <libelf.h>

/*
 * Returns: (nullable) (element-type filename) (transfer container): The
 *  initial `argv` for the capsule-capture-libs helper, with g_free() set
 *  as the free-function, and no %NULL terminator. Free with g_ptr_array_unref()
 *  or g_ptr_array_free().
 */
static GPtrArray *
_initial_capsule_capture_libs_argv (const char *sysroot,
                                    const char *helpers_path,
                                    const char *multiarch_tuple,
                                    const char *temp_dir,
                                    GError **error)
{
  GPtrArray *argv = NULL;

  argv = _srt_get_helper (helpers_path, multiarch_tuple, "capsule-capture-libs",
                          SRT_HELPER_FLAGS_SEARCH_PATH, error);

  if (argv == NULL)
    return NULL;

  g_ptr_array_add (argv, g_strdup ("--dest"));
  g_ptr_array_add (argv, g_strdup (temp_dir));
  g_ptr_array_add (argv, g_strdup ("--provider"));
  g_ptr_array_add (argv, g_strdup (sysroot));

  return argv;
}

static GPtrArray *
_argv_for_list_vdpau_drivers (gchar **envp,
                              const char *sysroot,
                              const char *helpers_path,
                              const char *multiarch_tuple,
                              const char *temp_dir,
                              GError **error)
{
  const gchar *vdpau_driver = NULL;
  GPtrArray *argv;

  g_return_val_if_fail (envp != NULL, NULL);

  vdpau_driver = g_environ_getenv (envp, "VDPAU_DRIVER");
  argv = _initial_capsule_capture_libs_argv (sysroot, helpers_path, multiarch_tuple,
                                             temp_dir, error);

  if (argv == NULL)
    return NULL;

  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname-match:libvdpau_*.so"));
  /* If the driver is not in the ld.so.cache the wildcard-matching will not find it.
   * To increase our chances we specifically search for the chosen driver and some
   * commonly used drivers. */
  if (vdpau_driver != NULL)
    {
      g_ptr_array_add (argv, g_strjoin (NULL,
                                        "no-dependencies:if-exists:even-if-older:soname:libvdpau_",
                                        vdpau_driver, ".so", NULL));
    }
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libvdpau_nouveau.so"));
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libvdpau_nvidia.so"));
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libvdpau_r300.so"));
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libvdpau_r600.so"));
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libvdpau_radeonsi.so"));
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libvdpau_va_gl.so"));
  g_ptr_array_add (argv, NULL);
  return argv;
}

static GPtrArray *
_argv_for_list_loader_libraries (gchar **envp,
                                 const char *sysroot,
                                 const char *helpers_path,
                                 const char *multiarch_tuple,
                                 const char *temp_dir,
                                 const char * const *loader_libraries,
                                 GError **error)
{
  g_autoptr(GPtrArray) argv = NULL;
  gsize i;

  g_return_val_if_fail (envp != NULL, NULL);
  g_return_val_if_fail (loader_libraries != NULL, NULL);
  g_return_val_if_fail (loader_libraries[0] != NULL, NULL);

  argv = _initial_capsule_capture_libs_argv (sysroot, helpers_path, multiarch_tuple,
                                             temp_dir, error);

  if (argv == NULL)
    return NULL;

  /* We want the symlink to be valid in the provider namespace */
  g_ptr_array_add (argv, g_strdup ("--link-target=/"));

  for (i = 0; loader_libraries[i] != NULL; i++)
    {
      /* they must all be SONAMEs to be looked up in the ld.so cache */
      g_return_val_if_fail (loader_libraries[i][0] != '/', NULL);
      g_ptr_array_add (argv, g_strdup_printf ("no-dependencies:if-exists:even-if-older:soname:%s",
                                              loader_libraries[i]));
    }

  g_ptr_array_add (argv, NULL);
  return g_steal_pointer (&argv);
}

static GPtrArray *
_argv_for_list_glx_icds (const char *sysroot,
                         const char *helpers_path,
                         const char *multiarch_tuple,
                         const char *temp_dir,
                         GError **error)
{
  GPtrArray *argv;

  argv = _initial_capsule_capture_libs_argv (sysroot, helpers_path, multiarch_tuple,
                                             temp_dir, error);

  if (argv == NULL)
    return NULL;

  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname-match:libGLX_*.so.0"));
  /* This one might seem redundant but it is required because "libGLX_indirect"
   * is usually a symlink to someone else's implementation and can't be found
   * in the ld.so cache, that "capsule-capture-libs" uses. So instead of using
   * a wildcard-matching we have to look it up explicitly. */
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libGLX_indirect.so.0"));
  /* If we are in a container the same might happen also for the other GLX drivers.
   * To increase our chances to find all the libraries we hard code "mesa" and
   * "nvidia" that, in the vast majority of the cases, are all we care about. */
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libGLX_mesa.so.0"));
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libGLX_nvidia.so.0"));
  g_ptr_array_add (argv, NULL);
  return argv;
}

static GPtrArray *
_argv_for_list_glx_icds_in_path (const char *sysroot,
                                 const char *helpers_path,
                                 const char *multiarch_tuple,
                                 const char *temp_dir,
                                 const char *base_path,
                                 GError **error)
{
  GPtrArray *argv;

  argv = _initial_capsule_capture_libs_argv (sysroot, helpers_path, multiarch_tuple,
                                             temp_dir, error);

  if (argv == NULL)
    return NULL;

  gchar *lib_full_path = g_build_filename (base_path, "lib", multiarch_tuple, "libGLX_*.so.*", NULL);

  g_ptr_array_add (argv, g_strjoin (NULL,
                                    "no-dependencies:if-exists:even-if-older:path-match:", lib_full_path,
                                    NULL));
  g_ptr_array_add (argv, NULL);
  g_free (lib_full_path);
  return argv;
}

/*
 * _srt_list_modules_from_directory:
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ` was this array
 * @argv: (array zero-terminated=1) (not nullable): The `argv` of the helper to use
 * @tmp_directory: (not nullable) (type filename): Full path to the destination
 *  directory used by the "capsule-capture-libs" helper
 * @known_table: (not optional): set of library names, plus their links, that
 *  we already found. Newely found libraries will be added to this list.
 *  For VDPAU provide a set with just paths where we already looked into, and in
 *  the VDPAU case the set will not be changed by this function.
 * @module: Which graphic module to search
 * @is_extra: If this path should be considered an extra or not. This is used only if
 *  @module is #SRT_GRAPHICS_VDPAU_MODULE.
 * @modules_out: (not optional) (inout): Prepend the found modules to this list.
 *  If @module is #SRT_GRAPHICS_GLX_MODULE, the element-type will be #SrtGlxIcd.
 *  Otherwise if @module is #SRT_GRAPHICS_VDPAU_MODULE, the element-type will be #SrtVdpauDriver.
 *
 * Modules are added to @modules_out in reverse lexicographic order
 * (`libvdpau_r600.so` is before `libvdpau_r300.so`, which is before `libvdpau_nouveau.so`).
 */
static void
_srt_list_modules_from_directory (gchar **envp,
                                  GPtrArray *argv,
                                  const gchar *tmp_directory,
                                  GHashTable *known_table,
                                  SrtGraphicsModule module,
                                  gboolean is_extra,
                                  GList **modules_out)
{
  int exit_status = -1;
  GError *error = NULL;
  gchar *stderr_output = NULL;
  gchar *output = NULL;
  GDir *dir_iter = NULL;
  GPtrArray *members = NULL;
  const gchar *member;
  gchar *full_path = NULL;
  gchar *driver_path = NULL;
  gchar *driver_directory = NULL;
  gchar *driver_link = NULL;
  gchar *soname_path = NULL;

  g_return_if_fail (argv != NULL);
  g_return_if_fail (envp != NULL);
  g_return_if_fail (tmp_directory != NULL);
  g_return_if_fail (known_table != NULL);
  g_return_if_fail (modules_out != NULL);

  if (!g_spawn_sync (NULL,    /* working directory */
                     (gchar **) argv->pdata,
                     envp,
                     G_SPAWN_SEARCH_PATH,       /* flags */
                     _srt_child_setup_unblock_signals,
                     NULL,    /* user data */
                     &output, /* stdout */
                     &stderr_output,
                     &exit_status,
                     &error))
    {
      g_debug ("An error occurred calling the helper: %s", error->message);
      goto out;
    }

  if (exit_status != 0)
    {
      g_debug ("... wait status %d", exit_status);
      goto out;
    }

  dir_iter = g_dir_open (tmp_directory, 0, &error);

  if (dir_iter == NULL)
    {
      g_debug ("Failed to open \"%s\": %s", tmp_directory, error->message);
      goto out;
    }

  members = g_ptr_array_new_with_free_func (g_free);

  while ((member = g_dir_read_name (dir_iter)) != NULL)
    g_ptr_array_add (members, g_strdup (member));

  g_ptr_array_sort (members, _srt_indirect_strcmp0);

  for (gsize i = 0; i < members->len; i++)
    {
      member = g_ptr_array_index (members, i);

      full_path = g_build_filename (tmp_directory, member, NULL);
      driver_path = g_file_read_link (full_path, &error);
      if (driver_path == NULL)
        {
          g_debug ("An error occurred trying to read the symlink: %s", error->message);
          g_free (full_path);
          goto out;
        }
      if (!g_path_is_absolute (driver_path))
        {
          g_debug ("We were expecting an absolute path, instead we have: %s", driver_path);
          g_free (full_path);
          g_free (driver_path);
          goto out;
        }

      switch (module)
        {
          case SRT_GRAPHICS_GLX_MODULE:
            /* Instead of just using just the library name to filter duplicates, we use it in
             * combination with its path. Because in one of the multiple iterations we might
             * find the same library that points to two different locations. And in this
             * case we want to log both of them.
             *
             * `member` cannot contain `/`, so we know we can use `/` to make
             * a composite key for deduplication. */
            soname_path = g_strjoin ("/", member, driver_path, NULL);
            if (!g_hash_table_contains (known_table, soname_path))
              {
                g_hash_table_add (known_table, g_strdup (soname_path));
                *modules_out = g_list_prepend (*modules_out, srt_glx_icd_new (member, driver_path));
              }
            g_free (soname_path);
            break;

          case SRT_GRAPHICS_VDPAU_MODULE:
            driver_directory = g_path_get_dirname (driver_path);
            if (!g_hash_table_contains (known_table, driver_directory))
              {
                /* We do not add `driver_directory` to the hash table because it contains
                 * a list of directories where we already looked into. In this case we are
                 * just adding a single driver instead of searching for all the `libvdpau_*`
                 * files in `driver_directory`. */
                driver_link = g_file_read_link (driver_path, NULL);
                *modules_out = g_list_prepend (*modules_out, srt_vdpau_driver_new (driver_path,
                                                                                  driver_link,
                                                                                  is_extra));
                g_free (driver_link);
              }
            g_free (driver_directory);
            break;

          case SRT_GRAPHICS_DRI_MODULE:
          case SRT_GRAPHICS_VAAPI_MODULE:
          case NUM_SRT_GRAPHICS_MODULES:
          default:
            g_return_if_reached ();
        }

      g_free (full_path);
      g_free (driver_path);
    }

out:
  if (dir_iter != NULL)
    g_dir_close (dir_iter);
  g_clear_pointer (&members, g_ptr_array_unref);
  g_free (output);
  g_free (stderr_output);
  g_clear_error (&error);
}

/*
 * Run @argv with environment @envp.
 * On success, @argv is expected to populate @tmp_directory
 * with symbolic links to absolute targets. Return their targets.
 */
static char **
_srt_list_links_from_directory (gchar **envp,
                                GPtrArray *argv,
                                const gchar *tmp_directory)
{
  int exit_status = -1;
  g_autoptr(GError) error = NULL;
  g_autoptr(GDir) dir_iter = NULL;
  g_autoptr(GPtrArray) members = NULL;
  const gchar *member;

  g_autoptr(GPtrArray) lib_links = g_ptr_array_new_with_free_func (g_free);

  g_return_val_if_fail (envp != NULL, NULL);
  g_return_val_if_fail (argv != NULL, NULL);
  g_return_val_if_fail (tmp_directory != NULL, NULL);

  if (!g_spawn_sync (NULL,    /* working directory */
                     (gchar **) argv->pdata,
                     envp,
                     G_SPAWN_SEARCH_PATH,  /* flags */
                     _srt_child_setup_unblock_signals,
                     NULL,  /* user data */
                     NULL,  /* stdout */
                     NULL,  /* stderr */
                     &exit_status,
                     &error))
    {
      g_debug ("An error occurred calling the helper: %s", error->message);
      return NULL;
    }

  if (exit_status != 0)
    {
      g_debug ("... wait status %d", exit_status);
      return NULL;
    }

  dir_iter = g_dir_open (tmp_directory, 0, &error);

  if (dir_iter == NULL)
    {
      g_debug ("Failed to open \"%s\": %s", tmp_directory, error->message);
      return NULL;
    }

  members = g_ptr_array_new_with_free_func (g_free);

  while ((member = g_dir_read_name (dir_iter)) != NULL)
    g_ptr_array_add (members, g_strdup (member));

  for (gsize i = 0; i < members->len; i++)
    {
      g_autofree gchar *full_path = NULL;
      g_autofree gchar *lib_link = NULL;
      member = g_ptr_array_index (members, i);

      full_path = g_build_filename (tmp_directory, member, NULL);
      lib_link = g_file_read_link (full_path, &error);
      if (lib_link == NULL)
        {
          g_debug ("An error occurred trying to read the symlink: %s", error->message);
          return NULL;
        }
      if (!g_path_is_absolute (lib_link))
        {
          g_debug ("We were expecting an absolute path, instead we have: %s", lib_link);
          return NULL;
        }

      g_ptr_array_add (lib_links, g_steal_pointer (&lib_link));
    }

  g_ptr_array_add (lib_links, NULL);
  return (GStrv) g_ptr_array_free (g_steal_pointer (&lib_links), FALSE);
}

/*
 * _srt_get_modules_from_path:
 * @sysroot_fd: A file descriptor opened on the root directory, usually '/'
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ` was this array
 * @helpers_path: (nullable): An optional path to find "inspect-library" helper, PATH
 *  is used if %NULL
 * @multiarch_tuple: (not nullable) (type filename): A Debian-style multiarch tuple
 *  such as %SRT_ABI_X86_64
 * @check_flags: Flags affecting how we do the search
 * @module_directory_path: (not nullable) (type filename): Path, in @sysroot namespace,
 *  to search for driver modules
 * @is_extra: If this path should be considered an extra or not
 * @module: Which graphic module to search
 * @drivers_out: (inout): Prepend the found drivers to this list.
 *  If @module is #SRT_GRAPHICS_DRI_MODULE, the element-type will be #SrtDriDriver.
 *  Otherwise if @module is #SRT_GRAPHICS_VAAPI_MODULE, the element-type will be #SrtVaApiDriver.
 *
 * @drivers_out will be prepended only with modules that are of the same ELF class that
 * corresponds to @multiarch_tuple.
 *
 * Drivers are added to `drivers_out` in reverse lexicographic order
 * (`r600_dri.so` is before `r200_dri.so`, which is before `i965_dri.so`).
 */
static void
_srt_get_modules_from_path (int sysroot_fd,
                            gchar **envp,
                            const char *helpers_path,
                            const char *multiarch_tuple,
                            SrtCheckFlags check_flags,
                            const char *module_directory_path,
                            gboolean is_extra,
                            SrtGraphicsModule module,
                            GList **drivers_out)
{
  /* We have up to 2 suffixes that we want to list */
  const gchar *module_suffix[3];
  const gchar *module_prefix = NULL;
  SrtLibraryIssues issues;
  struct dirent *dent;
  glnx_autofd int module_dirfd = -1;
  g_auto(GLnxDirFdIterator) dfd_iter = { 0, };
  g_autoptr(GPtrArray) in_this_dir = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(GError) error = NULL;
  gsize i;

  g_return_if_fail (sysroot_fd >= 0);
  g_return_if_fail (envp != NULL);
  g_return_if_fail (module_directory_path != NULL);
  g_return_if_fail (drivers_out != NULL);

  switch (module)
    {
      case SRT_GRAPHICS_DRI_MODULE:
        module_suffix[0] = "_dri.so";
        module_suffix[1] = NULL;
        break;

      case SRT_GRAPHICS_VAAPI_MODULE:
        module_suffix[0] = "_drv_video.so";
        module_suffix[1] = NULL;
        break;

      case SRT_GRAPHICS_VDPAU_MODULE:
        module_prefix = "libvdpau_";
        module_suffix[0] = ".so";
        module_suffix[1] = ".so.1";
        module_suffix[2] = NULL;
        break;

      case SRT_GRAPHICS_GLX_MODULE:
      case NUM_SRT_GRAPHICS_MODULES:
      default:
        g_return_if_reached ();
    }

  g_debug ("Looking for %sdrivers in (sysroot)/%s",
           is_extra ? "extra " : "",
           module_directory_path);

  module_dirfd = _srt_resolve_in_sysroot (sysroot_fd, module_directory_path,
                                          (SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY
                                           | SRT_RESOLVE_FLAGS_READABLE),
                                          NULL, &error);

  if (module_dirfd < 0)
    {
      g_debug ("An error occurred trying to resolve \"%s\" in sysroot: %s",
               module_directory_path, error->message);
      return;
    }

  if (!glnx_dirfd_iterator_init_take_fd (&module_dirfd, &dfd_iter, &error))
    {
      g_debug ("Unable to start iterating \"%s\": %s",
               module_directory_path, error->message);
      return;
    }

  while (glnx_dirfd_iterator_next_dent (&dfd_iter, &dent, NULL, NULL) && dent != NULL)
    {
      for (i = 0; module_suffix[i] != NULL; i++)
        {
          if (g_str_has_suffix (dent->d_name, module_suffix[i]) &&
              (module_prefix == NULL || g_str_has_prefix (dent->d_name, module_prefix)))
            {
              g_ptr_array_add (in_this_dir, g_strdup (dent->d_name));
            }
        }
    }

  g_ptr_array_sort (in_this_dir, _srt_indirect_strcmp0);

  for (i = 0; i < in_this_dir->len; i++)
    {
      SrtVaApiVersion libva_version = SRT_VA_API_VERSION_UNKNOWN;
      gchar *this_driver_link = NULL;
      const gchar *library_multiarch = NULL;
      const gchar *this_driver_name = g_ptr_array_index (in_this_dir, i);
      g_autofree gchar *this_driver_in_provider = g_build_filename (module_directory_path,
                                                                    this_driver_name,
                                                                    NULL);

      library_multiarch = _srt_architecture_guess_from_elf (dfd_iter.fd,
                                                            this_driver_name, &error);

      if (library_multiarch == NULL)
        {
          /* We were not able to guess the multiarch, fallback to inspect-library */
          g_autofree gchar *driver_proc_path = NULL;
          g_debug ("%s", error->message);
          g_clear_error (&error);

          g_debug ("Falling back to inspect-library...");
          driver_proc_path = g_strdup_printf ("/proc/%ld/fd/%d/%s", (long) getpid (),
                                              dfd_iter.fd, this_driver_name);
          issues = _srt_check_library_presence (helpers_path, driver_proc_path,
                                                multiarch_tuple, NULL, NULL, envp,
                                                SRT_LIBRARY_SYMBOLS_FORMAT_PLAIN, NULL);

          /* If "${multiarch}-inspect-library" was unable to load the driver, it's safe to assume that
          * its ELF class was not what we were searching for. */
          if (issues & SRT_LIBRARY_ISSUES_CANNOT_LOAD)
            continue;
        }
      else if (g_strcmp0 (library_multiarch, multiarch_tuple) != 0)
        {
          g_debug ("The library \"%s\" has a multiarch %s, but we were looking for %s. Skipping...",
                    this_driver_in_provider, library_multiarch, multiarch_tuple);
          continue;
        }

      switch (module)
        {
          case SRT_GRAPHICS_DRI_MODULE:
            *drivers_out = g_list_prepend (*drivers_out,
                                            srt_dri_driver_new (this_driver_in_provider,
                                                                is_extra));
            break;

          case SRT_GRAPHICS_VAAPI_MODULE:
            if (!(check_flags & SRT_CHECK_FLAGS_SKIP_SLOW_CHECKS))
              libva_version = _srt_va_api_driver_version (dfd_iter.fd, this_driver_name);

            *drivers_out = g_list_prepend (*drivers_out,
                                           srt_va_api_driver_new (this_driver_in_provider,
                                                                  libva_version,
                                                                  is_extra));
            break;

          case SRT_GRAPHICS_VDPAU_MODULE:
            this_driver_link = glnx_readlinkat_malloc (dfd_iter.fd, this_driver_name,
                                                       NULL, NULL);
            *drivers_out = g_list_prepend (*drivers_out,
                                            srt_vdpau_driver_new (this_driver_in_provider,
                                                                  this_driver_link,
                                                                  is_extra));
            g_free (this_driver_link);
            break;

          case SRT_GRAPHICS_GLX_MODULE:
          case NUM_SRT_GRAPHICS_MODULES:
          default:
            g_return_if_reached ();
        }
    }
}

/*
 * _srt_get_extra_modules_directory:
 * @library_search_path: (not nullable) (type filename): The absolute path to a directory that
 *  is in the library search path (e.g. /usr/lib/x86_64-linux-gnu)
 * @multiarch_tuple: (not nullable) (type filename): A Debian-style multiarch tuple
 *  such as %SRT_ABI_X86_64
 * @driver_class: Get the extra directories based on this ELF class, like
 *  ELFCLASS64.
 *
 * Given a loader path, this function tries to create a list of extra directories where it
 * might be possible to find driver modules.
 * E.g. given /usr/lib/x86_64-linux-gnu, return /usr/lib64 and /usr/lib
 *
 * Returns: (transfer full) (element-type gchar *) (nullable): A list of absolute
 *  paths in descending alphabetical order, or %NULL if an error occurred.
 */
static GList *
_srt_get_extra_modules_directory (const gchar *library_search_path,
                                  const gchar *multiarch_tuple,
                                  int driver_class)
{
  GList *ret = NULL;
  const gchar *libqual = NULL;
  gchar *lib_multiarch;
  gchar *dir;

  g_return_val_if_fail (library_search_path != NULL, NULL);
  g_return_val_if_fail (multiarch_tuple != NULL, NULL);

  dir = g_strdup (library_search_path);

  /* If the loader path ends with "/mesa" we try to look one directory above.
   * For example this is how Ubuntu 16.04 works, the loaders are in ${libdir}/mesa
   * and the DRI modules in ${libdir}/dri */
  if (g_str_has_suffix (dir, "/mesa"))
    {
      dir[strlen (dir) - strlen ("/mesa") + 1] = '\0';
      /* Remove the trailing slash */
      if (g_strcmp0 (dir, "/") != 0)
        dir[strlen (dir) - 1] = '\0';
    }

  ret = g_list_prepend (ret, g_build_filename (dir, "dri", NULL));
  g_debug ("Looking in lib directory: %s", (const char *) ret->data);

  lib_multiarch = g_strdup_printf ("/lib/%s", multiarch_tuple);

  if (!g_str_has_suffix (dir, lib_multiarch))
    {
      g_debug ("%s is not in the loader path: %s", lib_multiarch, library_search_path);
      goto out;
    }

  dir[strlen (dir) - strlen (lib_multiarch) + 1] = '\0';

  switch (driver_class)
    {
      case ELFCLASS32:
        libqual = "lib32";
        break;

      case ELFCLASS64:
        libqual = "lib64";
        break;

      case ELFCLASSNONE:
      default:
        g_free (lib_multiarch);
        g_free (dir);
        g_return_val_if_reached (NULL);
    }

  ret = g_list_prepend (ret, g_build_filename (dir, "lib", "dri", NULL));
  g_debug ("Looking in lib directory: %s", (const char *) ret->data);
  ret = g_list_prepend (ret, g_build_filename (dir, libqual, "dri", NULL));
  g_debug ("Looking in libQUAL directory: %s", (const char *) ret->data);

  ret = g_list_sort (ret, (GCompareFunc) strcmp);

out:
  g_free (lib_multiarch);
  g_free (dir);
  return ret;
}

/*
 * _srt_get_library_class:
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ`
 *  was this array
 * @library: (not nullable) (type filename): The library path to use
 *
 * Return the class of the specified library.
 * If it fails, %ELFCLASSNONE will be returned.
 *
 * Returns: the library class.
 */
static int
_srt_get_library_class (gchar **envp,
                        const gchar *library)
{
  g_autoptr(Elf) elf = NULL;
  glnx_autofd int fd = -1;
  g_autoptr(GError) error = NULL;

  g_return_val_if_fail (envp != NULL, ELFCLASSNONE);
  g_return_val_if_fail (library != NULL, ELFCLASSNONE);

  if (g_environ_getenv (envp, "SRT_TEST_ELF_CLASS_FROM_PATH") != NULL)
    {
      /* In the automated tests we use stub libraries, so we can't infer the
       * class using gelf_getclass(). Instead we use its path for hints. */
      if (strstr (library, "/x86_64-") != NULL
          || strstr (library, "/lib64") != NULL
          || strstr (library, "64/") != NULL)
        {
          return ELFCLASS64;
        }
      else
        {
          return ELFCLASS32;
        }
    }

  if (!_srt_open_elf (-1, library, &fd, &elf, &error))
    {
      g_debug ("%s", error->message);
      return ELFCLASSNONE;
    }

  return gelf_getclass (elf);
}

/*
 * @sysroot_fd: A file descriptor opened on the root directory, usually '/'
 * @loader_path: absolute and canonicalized path to a loader
 *  such as libva.so.2
 * @is_extra: If true, any modules found will be marked as "extra"
 * @module: Which kind of loader this is, currently DRI, VA-API or VDPAU
 * @drivers_set: Set of directories we already checked for @module.
 *  If the @loader_path suggests looking in one of these directories, it
 *  will not be checked again.
 *  When this function looks in a new directory, it is added to this set.
 * @drivers_out: (inout): Results are prepended to this list.
 *  The type depends on @module, the same as for _srt_get_modules_from_path().
 */
static void
_srt_get_modules_from_loader_library (int sysroot_fd,
                                      const gchar *loader_path,
                                      gchar **envp,
                                      const char *helpers_path,
                                      const char *multiarch_tuple,
                                      SrtCheckFlags check_flags,
                                      gboolean is_extra,
                                      SrtGraphicsModule module,
                                      GHashTable *drivers_set,
                                      GList **drivers_out)
{
  g_autofree gchar *libdir = NULL;
  g_autofree gchar *libdir_driver = NULL;

  g_return_if_fail (sysroot_fd >= 0);
  g_return_if_fail (loader_path != NULL);
  g_return_if_fail (loader_path[0] == '/');
  g_return_if_fail (drivers_set != NULL);

  libdir = g_path_get_dirname (loader_path);

  if (module == SRT_GRAPHICS_VDPAU_MODULE)
    libdir_driver = g_build_filename (libdir, "vdpau", NULL);
  else
    libdir_driver = g_build_filename (libdir, "dri", NULL);

  if (!g_hash_table_contains (drivers_set, libdir_driver))
    {
      g_hash_table_add (drivers_set, g_strdup (libdir_driver));
      _srt_get_modules_from_path (sysroot_fd, envp, helpers_path, multiarch_tuple,
                                  check_flags, libdir_driver, is_extra, module,
                                  drivers_out);
    }

  if (module == SRT_GRAPHICS_DRI_MODULE)
    {
      /* Used on Slackware according to
        * https://github.com/ValveSoftware/steam-runtime/issues/318 */
      g_autofree gchar *slackware = g_build_filename (libdir, "xorg",
                                                      "modules", "dri",
                                                      NULL);

      if (!g_hash_table_contains (drivers_set, slackware))
        {
          _srt_get_modules_from_path (sysroot_fd, envp, helpers_path,
                                      multiarch_tuple, check_flags, slackware,
                                      is_extra, module, drivers_out);
          g_hash_table_add (drivers_set, g_steal_pointer (&slackware));
        }
    }

  if (!(check_flags & SRT_CHECK_FLAGS_SKIP_EXTRAS))
    {
      const GList *this_extra_path;
      int driver_class = _srt_get_library_class (envp, loader_path);

      if (driver_class != ELFCLASSNONE)
        {
          GList *extras;

          extras = _srt_get_extra_modules_directory (libdir, multiarch_tuple, driver_class);
          for (this_extra_path = extras; this_extra_path != NULL; this_extra_path = this_extra_path->next)
            {
              if (!g_hash_table_contains (drivers_set, this_extra_path->data))
                {
                  g_debug ("Checking extra modules in directory \"%s\"",
                           (gchar *)this_extra_path->data);
                  g_hash_table_add (drivers_set, g_strdup (this_extra_path->data));
                  _srt_get_modules_from_path (sysroot_fd, envp, helpers_path,
                                              multiarch_tuple, check_flags,
                                              this_extra_path->data, TRUE,
                                              module, drivers_out);
                }
            }

          g_list_free_full (extras, g_free);
        }
    }
}

/*
 * _srt_list_glx_icds:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ` was this array
 * @helpers_path: (nullable): An optional path to find "capsule-capture-libs" helper,
 *  PATH is used if %NULL
 * @multiarch_tuple: (not nullable) (type filename): A Debian-style multiarch tuple
 *  such as %SRT_ABI_X86_64
 * @drivers_out: (inout): Prepend the found drivers to this list as opaque
 *  #SrtGlxIcd objects. There is no guarantee about the order of the list
 *
 * Implementation of srt_system_info_list_glx_icds().
 */
static void
_srt_list_glx_icds (const char *sysroot,
                    gchar **envp,
                    const char *helpers_path,
                    const char *multiarch_tuple,
                    GList **drivers_out)
{
  GPtrArray *by_soname_argv = NULL;
  GPtrArray *overrides_argv = NULL;
  GError *error = NULL;
  gchar *by_soname_tmp_dir = NULL;
  gchar *overrides_tmp_dir = NULL;
  gchar *overrides_path = NULL;
  GHashTable *known_libs = NULL;

  g_return_if_fail (sysroot != NULL);
  g_return_if_fail (multiarch_tuple != NULL);
  g_return_if_fail (drivers_out != NULL);
  g_return_if_fail (_srt_check_not_setuid ());

  known_libs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  by_soname_tmp_dir = g_dir_make_tmp ("glx-icds-XXXXXX", &error);
  if (by_soname_tmp_dir == NULL)
    {
      g_debug ("An error occurred trying to create a temporary folder: %s", error->message);
      goto out;
    }

  by_soname_argv = _argv_for_list_glx_icds (sysroot, helpers_path, multiarch_tuple,
                                            by_soname_tmp_dir, &error);

  if (by_soname_argv == NULL)
    {
      g_debug ("An error occurred trying to capture glx ICDs: %s", error->message);
      goto out;
    }

  _srt_list_modules_from_directory (envp, by_soname_argv, by_soname_tmp_dir, known_libs,
                                    SRT_GRAPHICS_GLX_MODULE, FALSE, drivers_out);

  /* When in a container we might miss valid GLX drivers because the `ld.so.cache` in
   * use doesn't have a reference about them. To fix that we also include every
   * "libGLX_*.so.*" libraries that we find in the "/overrides/lib/${multiarch}" folder */
  overrides_path = g_build_filename (sysroot, "/overrides", NULL);
  if (g_file_test (overrides_path, G_FILE_TEST_IS_DIR))
    {
      overrides_tmp_dir = g_dir_make_tmp ("glx-icds-XXXXXX", &error);
      if (overrides_tmp_dir == NULL)
        {
          g_debug ("An error occurred trying to create a temporary folder: %s", error->message);
          goto out;
        }

      overrides_argv = _argv_for_list_glx_icds_in_path (sysroot, helpers_path, multiarch_tuple,
                                                        overrides_tmp_dir, overrides_path, &error);

      if (overrides_argv == NULL)
        {
          g_debug ("An error occurred trying to capture glx ICDs: %s", error->message);
          goto out;
        }

      _srt_list_modules_from_directory (envp, overrides_argv, overrides_tmp_dir, known_libs,
                                        SRT_GRAPHICS_GLX_MODULE, FALSE, drivers_out);
    }

out:
  g_clear_pointer (&by_soname_argv, g_ptr_array_unref);
  g_clear_pointer (&overrides_argv, g_ptr_array_unref);
  if (by_soname_tmp_dir)
    {
      if (!_srt_rm_rf (by_soname_tmp_dir))
        g_debug ("Unable to remove the temporary directory: %s", by_soname_tmp_dir);
    }
  if (overrides_tmp_dir)
    {
      if (!_srt_rm_rf (overrides_tmp_dir))
        g_debug ("Unable to remove the temporary directory: %s", overrides_tmp_dir);
    }
  g_free (by_soname_tmp_dir);
  g_free (overrides_tmp_dir);
  g_free (overrides_path);
  g_hash_table_unref (known_libs);
  g_clear_error (&error);
}

/*
 * _srt_get_modules_full:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @sysroot_fd: A file descriptor opened on @sysroot
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ` was this array
 * @helpers_path: (nullable): An optional path to find "inspect-library" helper, PATH is used if %NULL
 * @multiarch_tuple: (not nullable) (type filename): A Debian-style multiarch tuple
 *  such as %SRT_ABI_X86_64
 * @check_flags: Flags affecting how we do the search
 * @module: Which graphic module to search
 * @drivers_out: (inout): Prepend the found drivers to this list.
 *  If @module is #SRT_GRAPHICS_DRI_MODULE or #SRT_GRAPHICS_VAAPI_MODULE or
 *  #SRT_GRAPHICS_VDPAU_MODULE, the element-type will be #SrtDriDriver, or
 *  #SrtVaApiDriver or #SrtVdpauDriver, respectively.
 *
 * On exit, `drivers_out` will have the least-preferred directories first and the
 * most-preferred directories last. Within a directory, the drivers will be
 * in reverse lexicographic order: `r600_dri.so` before `r200_dri.so`, which in turn
 * is before `nouveau_dri.so`.
 */
static void
_srt_get_modules_full (const char *sysroot,
                       int sysroot_fd,
                       gchar **envp,
                       const char *helpers_path,
                       const char *multiarch_tuple,
                       SrtCheckFlags check_flags,
                       SrtGraphicsModule module,
                       GList **drivers_out)
{
  const char * const *loader_libraries;
  static const char *const dri_loaders[] = { "libGLX_mesa.so.0", "libEGL_mesa.so.0",
                                             "libGL.so.1", NULL };
  static const char *const va_api_loaders[] = { "libva.so.2", "libva.so.1", NULL };
  static const char *const vdpau_loaders[] = { "libvdpau.so.1", NULL };
  const gchar *env_override;
  const gchar *drivers_path;
  const gchar *ld_library_path = NULL;
  g_autofree gchar *flatpak_info = NULL;
  g_autofree gchar *tmp_dir = NULL;
  g_autofree gchar *capture_libs_output_dir = NULL;
  g_autoptr(GHashTable) drivers_set = NULL;
  gboolean is_extra = FALSE;
  g_autoptr(GPtrArray) vdpau_argv = NULL;
  g_autoptr(GError) error = NULL;
  gsize i;

  g_return_if_fail (sysroot != NULL);
  g_return_if_fail (sysroot_fd >= 0);
  g_return_if_fail (envp != NULL);
  g_return_if_fail (multiarch_tuple != NULL);
  g_return_if_fail (drivers_out != NULL);
  g_return_if_fail (_srt_check_not_setuid ());

  switch (module)
    {
      case SRT_GRAPHICS_DRI_MODULE:
        loader_libraries = dri_loaders;
        env_override = "LIBGL_DRIVERS_PATH";
        break;

      case SRT_GRAPHICS_VAAPI_MODULE:
        loader_libraries = va_api_loaders;
        env_override = "LIBVA_DRIVERS_PATH";
        break;

      case SRT_GRAPHICS_VDPAU_MODULE:
        loader_libraries = vdpau_loaders;
        env_override = "VDPAU_DRIVER_PATH";
        break;

      case SRT_GRAPHICS_GLX_MODULE:
      case NUM_SRT_GRAPHICS_MODULES:
      default:
        g_return_if_reached ();
    }

  drivers_path = g_environ_getenv (envp, env_override);
  ld_library_path = g_environ_getenv (envp, "LD_LIBRARY_PATH");

  flatpak_info = g_build_filename (sysroot, ".flatpak-info", NULL);
  drivers_set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  if (drivers_path)
    {
      /* If the graphics environment variable for this module is set, we make
       * the assumption that it is intended to be interpreted as if the
       * sysroot was the real root directory: for example
       * LIBGL_DRIVERS_PATH=/foo might really mean /sysroot/foo. */

      g_auto(GStrv) entries = NULL;
      char **entry;

      g_debug ("A driver path environment is available: %s", drivers_path);
      /* VDPAU_DRIVER_PATH holds just a single path and not a colon separeted
       * list of paths. Because of that we handle the VDPAU case separately to
       * avoid splitting a theoretically valid path like "/usr/lib/custom_d:r/" */
      if (module == SRT_GRAPHICS_VDPAU_MODULE)
        {
          entries = g_new (gchar*, 2);
          entries[0] = g_strdup (drivers_path);
          entries[1] = NULL;
        }
      else
        {
          entries = g_strsplit (drivers_path, ":", 0);
        }

      for (entry = entries; entry != NULL && *entry != NULL; entry++)
        {
          if (*entry[0] == '\0')
            continue;

          if (!g_hash_table_contains (drivers_set, *entry))
            {
              g_hash_table_add (drivers_set, g_strdup (*entry));
              _srt_get_modules_from_path (sysroot_fd, envp, helpers_path, multiarch_tuple,
                                          check_flags, *entry, FALSE, module, drivers_out);
            }
        }

      /* We continue to search for libraries but we mark them all as "extra" because the
       * loader wouldn't have picked them up. */
      if (check_flags & SRT_CHECK_FLAGS_SKIP_EXTRAS)
        goto out;
      else
        is_extra = TRUE;
    }

  /* If we are in a Flatpak environment we search in the same paths that Flatpak uses,
   * keeping also the same search order.
   *
   * For VA-API these are the paths used:
   * "%{libdir}/dri:%{libdir}/dri/intel-vaapi-driver:%{libdir}/GL/lib/dri"
   * (reference:
   * <https://gitlab.com/freedesktop-sdk/freedesktop-sdk/blob/master/elements/components/libva.bst>)
   *
   * For Mesa there is just a single path:
   * "%{libdir}/GL/lib/dri"
   * (really `GL/default/lib/dri` or `GL/mesa-git/lib/dri`, but `GL/lib/dri` is
   * populated with symbolic links; reference:
   * <https://gitlab.com/freedesktop-sdk/freedesktop-sdk/blob/master/elements/extensions/mesa/mesa.bst>
   * and
   * <https://gitlab.com/freedesktop-sdk/freedesktop-sdk/blob/master/elements/flatpak-images/platform.bst>)
   *
   * For VDPAU there is just a single path:
   * "%{libdir}/vdpau"
   * (reference:
   * <https://gitlab.com/freedesktop-sdk/freedesktop-sdk/blob/master/elements/components/libvdpau.bst>)
   * */
  if (g_file_test (flatpak_info, G_FILE_TEST_EXISTS))
    {
      g_autofree gchar *libdir = g_build_filename ("/usr", "lib", multiarch_tuple, NULL);
      if (module == SRT_GRAPHICS_VAAPI_MODULE)
        {
          g_autofree gchar *libdir_dri = g_build_filename (libdir, "dri", NULL);
          g_autofree gchar *intel_vaapi = g_build_filename (libdir_dri, "intel-vaapi-driver", NULL);
          if (!g_hash_table_contains (drivers_set, libdir_dri))
            {
              g_hash_table_add (drivers_set, g_strdup (libdir_dri));
              _srt_get_modules_from_path (sysroot_fd, envp, helpers_path,
                                          multiarch_tuple, check_flags, libdir_dri,
                                          is_extra, module, drivers_out);
            }
          if (!g_hash_table_contains (drivers_set, intel_vaapi))
            {
              g_hash_table_add (drivers_set, g_strdup (intel_vaapi));
              _srt_get_modules_from_path (sysroot_fd, envp, helpers_path,
                                          multiarch_tuple, check_flags, intel_vaapi,
                                          is_extra, module, drivers_out);
            }
        }

      if (module == SRT_GRAPHICS_VAAPI_MODULE || module == SRT_GRAPHICS_DRI_MODULE)
        {
          g_autofree gchar *gl_lib_dri = g_build_filename (libdir, "GL", "lib", "dri", NULL);

          if (!g_hash_table_contains (drivers_set, gl_lib_dri))
            {
              g_hash_table_add (drivers_set, g_strdup (gl_lib_dri));
              _srt_get_modules_from_path (sysroot_fd, envp, helpers_path,
                                          multiarch_tuple, check_flags, gl_lib_dri,
                                          is_extra, module, drivers_out);
            }
        }

      /* We continue to search for libraries but we mark them all as "extra" because the
       * loader wouldn't have picked them up.
       * The only exception is for VDPAU, becuase in a Flatpak environment the search path
       * is the same as in a non container environment. */
      if (module != SRT_GRAPHICS_VDPAU_MODULE)
        {
          if (check_flags & SRT_CHECK_FLAGS_SKIP_EXTRAS)
            goto out;
          else
            is_extra = TRUE;
        }
    }

  if (g_strcmp0 (sysroot, "/") != 0)
    {
      /* If the sysroot is not "/", we can't use _srt_check_library_presence()
       * to locate the loader libraries because it doesn't take into
       * consideration our custom sysroot, and dlopening a library in the host
       * system that has unmet dependencies may fail.
       * Instead we use capsule-capture-libs, and check the symlinks that it
       * creates. */
      g_autoptr(GPtrArray) gfx_argv = NULL;
      g_auto(GStrv) loader_lib_link = NULL;
      capture_libs_output_dir = g_dir_make_tmp ("graphics-drivers-XXXXXX", &error);
      if (capture_libs_output_dir == NULL)
        {
          g_debug ("An error occurred trying to create a temporary folder: %s",
                   error->message);
          goto out;
        }

      gfx_argv = _argv_for_list_loader_libraries (envp, sysroot, helpers_path,
                                                  multiarch_tuple, capture_libs_output_dir,
                                                  loader_libraries, &error);
      if (gfx_argv == NULL)
        {
          g_debug ("An error occurred trying to locate graphics drivers: %s",
                   error->message);
          goto out;
        }

      loader_lib_link = _srt_list_links_from_directory (envp, gfx_argv, capture_libs_output_dir);
      for (i = 0; loader_lib_link != NULL && loader_lib_link[i] != NULL; i++)
        {
          g_debug ("Searching modules using the loader path \"%s\"", loader_lib_link[i]);
          _srt_get_modules_from_loader_library (sysroot_fd, loader_lib_link[i],
                                                envp, helpers_path, multiarch_tuple,
                                                check_flags, is_extra, module,
                                                drivers_set, drivers_out);
        }
    }
  else
    {
      for (i = 0; loader_libraries[i] != NULL; i++)
        {
          g_autoptr(SrtLibrary) library_details = NULL;
          g_autofree gchar *driver_canonical_path = NULL;
          SrtLibraryIssues issues;

          issues = _srt_check_library_presence (helpers_path,
                                                loader_libraries[i],
                                                multiarch_tuple,
                                                NULL,   /* symbols path */
                                                NULL,   /* hidden dependencies */
                                                envp,
                                                SRT_LIBRARY_SYMBOLS_FORMAT_PLAIN,
                                                &library_details);

          if (issues & (SRT_LIBRARY_ISSUES_CANNOT_LOAD |
                        SRT_LIBRARY_ISSUES_UNKNOWN |
                        SRT_LIBRARY_ISSUES_TIMEOUT))
            {
              const char *messages = srt_library_get_messages (library_details);

              if (messages == NULL || messages[0] == '\0')
                messages = "(no diagnostic output)";

              g_debug ("Unable to load library %s: %s",
                       loader_libraries[i],
                       messages);
            }

          const gchar *loader_path = srt_library_get_absolute_path (library_details);
          if (loader_path == NULL)
            {
              g_debug ("loader path for %s is NULL", loader_libraries[i]);
              continue;
            }

          /* The path may be a symbolic link or it can contain ./ or ../
           * The sysroot is "/", so we don't have to worry about symlinks that
           * could escape from the sysroot. */
          driver_canonical_path = realpath (loader_path, NULL);
          if (driver_canonical_path == NULL)
            {
              g_debug ("realpath(%s): %s", loader_path, g_strerror (errno));
              continue;
            }

          _srt_get_modules_from_loader_library (sysroot_fd, driver_canonical_path,
                                                envp, helpers_path, multiarch_tuple,
                                                check_flags, is_extra, module,
                                                drivers_set, drivers_out);
        }
    }


  if (module == SRT_GRAPHICS_VDPAU_MODULE)
    {
      /* VDPAU modules are also loaded by just dlopening the bare filename
       * libvdpau_${VDPAU_DRIVER}.so
       * To cover that we search in all directories listed in LD_LIBRARY_PATH.
       * LD_LIBRARY_PATH entries are assumed to be interpreted as if the
       * sysroot was the real root directory. */
      if (ld_library_path != NULL)
        {
          g_auto(GStrv) entries = g_strsplit (ld_library_path, ":", 0);
          gchar **entry;

          for (entry = entries; entry != NULL && *entry != NULL; entry++)
            {
              glnx_autofd int file_fd = -1;
              g_autofree gchar *entry_realpath_in_sysroot = NULL;
              g_autofree gchar *absolute_path_in_sysroot = NULL;

              /* Scripts that manipulate LD_LIBRARY_PATH have a habit of
               * adding empty entries */
              if (*entry[0] == '\0')
                continue;

              file_fd = _srt_resolve_in_sysroot (sysroot_fd,
                                                 *entry, SRT_RESOLVE_FLAGS_NONE,
                                                 &entry_realpath_in_sysroot, &error);

              if (file_fd < 0)
                {
                  /* Skip it if the path doesn't exist or is not reachable */
                  g_debug ("An error occurred while resolving \"%s\": %s",
                           *entry, error->message);
                  g_clear_error (&error);
                  continue;
                }

              /* Convert the resolved realpath to an absolute path */
              absolute_path_in_sysroot = g_build_filename ("/", entry_realpath_in_sysroot,
                                                           NULL);

              if (!g_hash_table_contains (drivers_set, absolute_path_in_sysroot))
                {
                  g_hash_table_add (drivers_set, g_strdup (absolute_path_in_sysroot));
                  _srt_get_modules_from_path (sysroot_fd, envp, helpers_path,
                                              multiarch_tuple, check_flags,
                                              absolute_path_in_sysroot, is_extra,
                                              module, drivers_out);
                }
            }
        }

      /* Also use "capsule-capture-libs" to search for VDPAU drivers that we might have
       * missed */
      tmp_dir = g_dir_make_tmp ("vdpau-drivers-XXXXXX", &error);
      if (tmp_dir == NULL)
        {
          g_debug ("An error occurred trying to create a temporary folder: %s", error->message);
          goto out;
        }
      vdpau_argv = _argv_for_list_vdpau_drivers (envp, sysroot, helpers_path,
                                                 multiarch_tuple, tmp_dir, &error);
      if (vdpau_argv == NULL)
        {
          g_debug ("An error occurred trying to capture VDPAU drivers: %s", error->message);
          goto out;
        }
      _srt_list_modules_from_directory (envp, vdpau_argv, tmp_dir, drivers_set,
                                        SRT_GRAPHICS_VDPAU_MODULE, is_extra, drivers_out);

      if (!(check_flags & SRT_CHECK_FLAGS_SKIP_EXTRAS))
        {
          /* Debian used to hardcode "/usr/lib/vdpau" as an additional search path for VDPAU.
           * However since libvdpau 1.3-1 it has been removed; reference:
           * <https://salsa.debian.org/nvidia-team/libvdpau/commit/11a3cd84>
           * Just to be sure to not miss a potentially valid library path we search on it
           * unconditionally, flagging it as extra. */
          g_autofree gchar *debian_additional = g_build_filename ("/usr", "lib", "vdpau", NULL);

          if (!g_hash_table_contains (drivers_set, debian_additional))
            {
              _srt_get_modules_from_path (sysroot_fd, envp, helpers_path,
                                          multiarch_tuple, check_flags, debian_additional,
                                          TRUE, module, drivers_out);
            }
        }
    }

out:
  if (tmp_dir)
    {
      if (!_srt_rm_rf (tmp_dir))
        g_debug ("Unable to remove the temporary directory: %s", tmp_dir);
    }
  if (capture_libs_output_dir != NULL)
    {
      if (!_srt_rm_rf (capture_libs_output_dir))
        g_debug ("Unable to remove the temporary directory: %s", capture_libs_output_dir);
    }

}

/*
 * _srt_list_graphics_modules:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @sysroot_fd: A file descriptor opened on @sysroot, or negative to
 *  reopen it
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ` was this array
 * @helpers_path: (nullable): An optional path to find "inspect-library" helper, PATH is used if %NULL
 * @multiarch_tuple: (not nullable) (type filename): A Debian-style multiarch tuple
 *  such as %SRT_ABI_X86_64
 * @check_flags: Flags affecting how we do the search
 * @which: Graphics modules to look for
 *
 * Implementation of srt_system_info_list_dri_drivers() etc.
 *
 * The returned list for GLX modules is in an unspecified order.
 *
 * Instead the returned list for all the other graphics modules will have the
 * most-preferred directories first and the least-preferred directories last.
 * Within a directory, the drivers will be in lexicographic order, for example
 * `nouveau_dri.so`, `r200_dri.so`, `r600_dri.so` in that order.
 *
 * Returns: (transfer full) (element-type GObject) (nullable): A list of
 *  opaque #SrtDriDriver, etc. objects, or %NULL if nothing was found. Free with
 *  `g_list_free_full(list, g_object_unref)`.
 */
GList *
_srt_list_graphics_modules (const char *sysroot,
                            int sysroot_fd,
                            gchar **envp,
                            const char *helpers_path,
                            const char *multiarch_tuple,
                            SrtCheckFlags check_flags,
                            SrtGraphicsModule which)
{
  GList *drivers = NULL;

  g_return_val_if_fail (sysroot != NULL, NULL);
  g_return_val_if_fail (multiarch_tuple != NULL, NULL);

  if (which == SRT_GRAPHICS_GLX_MODULE)
    _srt_list_glx_icds (sysroot, envp, helpers_path, multiarch_tuple, &drivers);
  else
    _srt_get_modules_full (sysroot, sysroot_fd, envp, helpers_path, multiarch_tuple,
                           check_flags, which, &drivers);

  return g_list_reverse (drivers);
}
