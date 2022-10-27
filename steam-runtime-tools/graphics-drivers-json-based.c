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

#include "steam-runtime-tools/glib-backports-internal.h"

#include "steam-runtime-tools/graphics-drivers-json-based-internal.h"
#include "steam-runtime-tools/graphics-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "steam-runtime-tools/json-utils-internal.h"
#include "steam-runtime-tools/library-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"

void
srt_loadable_clear (SrtLoadable *self)
{
  g_clear_error (&self->error);
  g_clear_pointer (&self->api_version, g_free);
  g_clear_pointer (&self->json_path, g_free);
  g_clear_pointer (&self->library_path, g_free);
  g_clear_pointer (&self->file_format_version, g_free);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->type, g_free);
  g_clear_pointer (&self->implementation_version, g_free);
  g_clear_pointer (&self->description, g_free);
  g_clear_pointer (&self->component_layers, g_strfreev);
  g_clear_pointer (&self->functions, g_hash_table_unref);
  g_list_free_full (g_steal_pointer (&self->instance_extensions), instance_extension_free);
  g_clear_pointer (&self->pre_instance_functions, g_hash_table_unref);
  g_list_free_full (g_steal_pointer (&self->device_extensions), device_extension_free);
  g_clear_pointer (&self->enable_env_var.name, g_free);
  g_clear_pointer (&self->enable_env_var.value, g_free);
  g_clear_pointer (&self->disable_env_var.name, g_free);
  g_clear_pointer (&self->disable_env_var.value, g_free);
}

/*
 * See srt_egl_icd_resolve_library_path(),
 * srt_vulkan_icd_resolve_library_path() or
 * srt_vulkan_layer_resolve_library_path()
 */
gchar *
srt_loadable_resolve_library_path (const SrtLoadable *self)
{
  gchar *dir;
  gchar *ret;

  /*
   * In Vulkan, this function behaves according to the specification:
   *
   * The "library_path" specifies either a filename, a relative pathname,
   * or a full pathname to an ICD shared library file. If "library_path"
   * specifies a relative pathname, it is relative to the path of the
   * JSON manifest file. If "library_path" specifies a filename, the
   * library must live in the system's shared object search path.
   * — https://github.com/KhronosGroup/Vulkan-Loader/blob/sdk-1.2.198.1/docs/LoaderDriverInterface.md#driver-manifest-file-format
   * — https://github.com/KhronosGroup/Vulkan-Loader/blob/sdk-1.2.198.1/docs/LoaderLayerInterface.md#layer-manifest-file-format
   *
   * In GLVND, EGL ICDs with relative pathnames are currently passed
   * directly to dlopen(), which will interpret them as relative to
   * the current working directory - but upstream acknowledge in
   * https://github.com/NVIDIA/libglvnd/issues/187 that this is not
   * actually very useful, and have indicated that they would consider
   * a patch to give it the same behaviour as Vulkan instead.
   */

  if (self->library_path == NULL)
    return NULL;

  if (self->library_path[0] == '/')
    return g_strdup (self->library_path);

  if (strchr (self->library_path, '/') == NULL)
    return g_strdup (self->library_path);

  dir = g_path_get_dirname (self->json_path);
  ret = g_build_filename (dir, self->library_path, NULL);
  g_free (dir);
  g_return_val_if_fail (g_path_is_absolute (ret), ret);
  return ret;
}

/* See srt_egl_icd_check_error(), srt_vulkan_icd_check_error() */
gboolean
srt_loadable_check_error (const SrtLoadable *self,
                          GError **error)
{
  if (self->error != NULL && error != NULL)
    *error = g_error_copy (self->error);

  return (self->error == NULL);
}

/* See srt_egl_icd_write_to_file(), srt_vulkan_icd_write_to_file() and
 * srt_vulkan_layer_write_to_file() */
gboolean
srt_loadable_write_to_file (const SrtLoadable *self,
                            const char *path,
                            GType which,
                            GError **error)
{
  JsonBuilder *builder;
  JsonGenerator *generator;
  JsonNode *root;
  gchar *json_output;
  gboolean ret = FALSE;
  const gchar *member;
  gpointer key;
  gpointer value;
  const GList *l;

  /* EGL external platforms have { "ICD": ... } in their JSON file,
   * even though you might have expected a different string. */
  if (which == SRT_TYPE_EGL_ICD
      || which == SRT_TYPE_VULKAN_ICD
      || which == SRT_TYPE_EGL_EXTERNAL_PLATFORM)
    member = "ICD";
  else if (which == SRT_TYPE_VULKAN_LAYER)
    member = "layer";
  else
    g_return_val_if_reached (FALSE);

  if (!srt_loadable_check_error (self, error))
    {
      g_prefix_error (error,
                      "Cannot save %s metadata to file because it is invalid: ",
                      member);
      return FALSE;
    }

  builder = json_builder_new ();
  json_builder_begin_object (builder);
    {
      if (which == SRT_TYPE_VULKAN_ICD)
        {
          json_builder_set_member_name (builder, "file_format_version");

          /* We parse and store all the information defined in file format
           * version 1.0.0 and 1.0.1. We use the file format 1.0.1 only if
           * the field "is_portability_driver" is set, because that is the
           * only change that has been introduced with 1.0.1. */
          if (self->portability_driver)
            json_builder_add_string_value (builder, "1.0.1");
          else
            json_builder_add_string_value (builder, "1.0.0");

           json_builder_set_member_name (builder, member);
           json_builder_begin_object (builder);
            {
              json_builder_set_member_name (builder, "library_path");
              json_builder_add_string_value (builder, self->library_path);

              json_builder_set_member_name (builder, "api_version");
              json_builder_add_string_value (builder, self->api_version);

              if (self->portability_driver)
                {
                  json_builder_set_member_name (builder, "is_portability_driver");
                  json_builder_add_boolean_value (builder, self->portability_driver);
                }
            }
           json_builder_end_object (builder);
        }
      else if (which == SRT_TYPE_EGL_ICD
               || which == SRT_TYPE_EGL_EXTERNAL_PLATFORM)
        {
          /* We parse and store all the information defined in file format
           * version 1.0.0, but nothing beyond that, so we use this version
           * in our output instead of quoting whatever was in the input. */
          json_builder_set_member_name (builder, "file_format_version");
          json_builder_add_string_value (builder, "1.0.0");

           json_builder_set_member_name (builder, member);
           json_builder_begin_object (builder);
            {
              json_builder_set_member_name (builder, "library_path");
              json_builder_add_string_value (builder, self->library_path);
            }
           json_builder_end_object (builder);
        }
      else if (which == SRT_TYPE_VULKAN_LAYER)
        {
          json_builder_set_member_name (builder, "file_format_version");
          /* In the Vulkan layer specs the file format version is a required field.
           * However it might happen that we are not aware of its value, e.g. when we
           * parse an s-r-s-i report. Because of that, if the file format version info
           * is missing, we don't consider it a fatal error and we just set it to the
           * lowest version that is required, based on the fields we have. */
          if (self->file_format_version == NULL)
            {
              if (self->pre_instance_functions != NULL)
                json_builder_add_string_value (builder, "1.1.2");
              else if (self->component_layers != NULL && self->component_layers[0] != NULL)
                json_builder_add_string_value (builder, "1.1.1");
              else
                json_builder_add_string_value (builder, "1.1.0");
            }
          else
            {
              json_builder_add_string_value (builder, self->file_format_version);
            }

          json_builder_set_member_name (builder, "layer");
          json_builder_begin_object (builder);
            {
              json_builder_set_member_name (builder, "name");
              json_builder_add_string_value (builder, self->name);

              json_builder_set_member_name (builder, "type");
              json_builder_add_string_value (builder, self->type);

              if (self->library_path != NULL)
                {
                  json_builder_set_member_name (builder, "library_path");
                  json_builder_add_string_value (builder, self->library_path);
                }

              json_builder_set_member_name (builder, "api_version");
              json_builder_add_string_value (builder, self->api_version);

              json_builder_set_member_name (builder, "implementation_version");
              json_builder_add_string_value (builder, self->implementation_version);

              json_builder_set_member_name (builder, "description");
              json_builder_add_string_value (builder, self->description);

              _srt_json_builder_add_strv_value (builder, "component_layers",
                                                (const gchar * const *) self->component_layers,
                                                FALSE);

              if (self->functions != NULL)
                {
                  g_auto(SrtHashTableIter) iter = SRT_HASH_TABLE_ITER_CLEARED;

                  json_builder_set_member_name (builder, "functions");
                  json_builder_begin_object (builder);
                  _srt_hash_table_iter_init_sorted (&iter,
                                                    self->functions,
                                                    _srt_generic_strcmp0);
                  while (_srt_hash_table_iter_next (&iter, &key, &value))
                    {
                      json_builder_set_member_name (builder, key);
                      json_builder_add_string_value (builder, value);
                    }
                  json_builder_end_object (builder);
                }

              if (self->pre_instance_functions != NULL)
                {
                  g_auto(SrtHashTableIter) iter = SRT_HASH_TABLE_ITER_CLEARED;

                  json_builder_set_member_name (builder, "pre_instance_functions");
                  json_builder_begin_object (builder);
                  _srt_hash_table_iter_init_sorted (&iter,
                                                    self->pre_instance_functions,
                                                    _srt_generic_strcmp0);
                  while (_srt_hash_table_iter_next (&iter, &key, &value))
                    {
                      json_builder_set_member_name (builder, key);
                      json_builder_add_string_value (builder, value);
                    }
                  json_builder_end_object (builder);
                }

              if (self->instance_extensions != NULL)
                {
                  json_builder_set_member_name (builder, "instance_extensions");
                  json_builder_begin_array (builder);
                  for (l = self->instance_extensions; l != NULL; l = l->next)
                    {
                      InstanceExtension *ie = l->data;
                      json_builder_begin_object (builder);
                      json_builder_set_member_name (builder, "name");
                      json_builder_add_string_value (builder, ie->name);
                      json_builder_set_member_name (builder, "spec_version");
                      json_builder_add_string_value (builder, ie->spec_version);
                      json_builder_end_object (builder);
                    }
                  json_builder_end_array (builder);
                }

              if (self->device_extensions != NULL)
                {
                  json_builder_set_member_name (builder, "device_extensions");
                  json_builder_begin_array (builder);
                  for (l = self->device_extensions; l != NULL; l = l->next)
                    {
                      DeviceExtension *de = l->data;
                      json_builder_begin_object (builder);
                      json_builder_set_member_name (builder, "name");
                      json_builder_add_string_value (builder, de->name);
                      json_builder_set_member_name (builder, "spec_version");
                      json_builder_add_string_value (builder, de->spec_version);
                      _srt_json_builder_add_strv_value (builder, "entrypoints",
                                                        (const gchar * const *) de->entrypoints,
                                                        FALSE);
                      json_builder_end_object (builder);
                    }
                  json_builder_end_array (builder);
                }

              if (self->enable_env_var.name != NULL)
                {
                  json_builder_set_member_name (builder, "enable_environment");
                  json_builder_begin_object (builder);
                  json_builder_set_member_name (builder, self->enable_env_var.name);
                  json_builder_add_string_value (builder, self->enable_env_var.value);
                  json_builder_end_object (builder);
                }

              if (self->disable_env_var.name != NULL)
                {
                  json_builder_set_member_name (builder, "disable_environment");
                  json_builder_begin_object (builder);
                  json_builder_set_member_name (builder, self->disable_env_var.name);
                  json_builder_add_string_value (builder, self->disable_env_var.value);
                  json_builder_end_object (builder);
                }
            }
          json_builder_end_object (builder);
        }
    }
  json_builder_end_object (builder);

  root = json_builder_get_root (builder);
  generator = json_generator_new ();
  json_generator_set_root (generator, root);
  json_generator_set_pretty (generator, TRUE);
  json_output = json_generator_to_data (generator, NULL);

  ret = g_file_set_contents (path, json_output, -1, error);

  if (!ret)
    g_prefix_error (error, "Cannot save %s metadata to file :", member);

  g_free (json_output);
  g_object_unref (generator);
  json_node_free (root);
  g_object_unref (builder);
  return ret;
}

/*
 * Use 'inspect-library' to get the absolute path of @library_path,
 * resolving also its eventual symbolic links.
 */
static gchar *
_get_library_canonical_path (gchar **envp,
                             const char *helpers_path,
                             const char *multiarch,
                             const gchar *library_path)
{
  g_autoptr(SrtLibrary) library = NULL;
  _srt_check_library_presence (helpers_path, library_path, multiarch, NULL,
                               NULL, envp,
                               SRT_LIBRARY_SYMBOLS_FORMAT_PLAIN, &library);

  /* Use realpath() because the path might still be a symbolic link or it can
   * contains ./ or ../
   * The absolute path is gathered using 'inspect-library', so we don't have
   * to worry about still having special tokens, like ${LIB}, in the path. */
  return realpath (srt_library_get_absolute_path (library), NULL);
}

static void
_update_duplicated_value (GType which,
                          GHashTable *loadable_seen,
                          const gchar *key,
                          void *loadable_to_check)
{
  if (key == NULL)
    return;

  if (g_hash_table_contains (loadable_seen, key))
    {
      if (which == SRT_TYPE_VULKAN_ICD)
        {
          SrtVulkanIcd *vulkan_icd = g_hash_table_lookup (loadable_seen, key);
          _srt_vulkan_icd_set_is_duplicated (vulkan_icd, TRUE);
          _srt_vulkan_icd_set_is_duplicated (loadable_to_check, TRUE);
        }
      else if (which == SRT_TYPE_EGL_ICD)
        {
          SrtEglIcd *egl_icd = g_hash_table_lookup (loadable_seen, key);
          _srt_egl_icd_set_is_duplicated (egl_icd, TRUE);
          _srt_egl_icd_set_is_duplicated (loadable_to_check, TRUE);
        }
      else if (which == SRT_TYPE_EGL_EXTERNAL_PLATFORM)
        {
          SrtEglExternalPlatform *egl_ext = g_hash_table_lookup (loadable_seen, key);
          _srt_egl_external_platform_set_is_duplicated (egl_ext, TRUE);
          _srt_egl_external_platform_set_is_duplicated (loadable_to_check, TRUE);
        }
      else if (which == SRT_TYPE_VULKAN_LAYER)
        {
          SrtVulkanLayer *vulkan_layer = g_hash_table_lookup (loadable_seen, key);
          _srt_vulkan_layer_set_is_duplicated (vulkan_layer, TRUE);
          _srt_vulkan_layer_set_is_duplicated (loadable_to_check, TRUE);
        }
      else
        {
          g_return_if_reached ();
        }
    }
  else
    {
      g_hash_table_replace (loadable_seen,
                            g_strdup (key),
                            loadable_to_check);
    }
}

/*
 * @helpers_path: (nullable): An optional path to find "inspect-library"
 *  helper, PATH is used if %NULL
 * @loadable: (inout) (element-type SrtVulkanLayer):
 *
 * Iterate the provided @loadable list and update their "issues" property
 * to include the SRT_LOADABLE_ISSUES_DUPLICATED bit if they are duplicated.
 * Two ICDs are considered to be duplicated if they have the same absolute
 * library path.
 * Two Vulkan layers are considered to be duplicated if they have the same
 * name and absolute library path.
 */
void
_srt_loadable_flag_duplicates (GType which,
                               gchar **envp,
                               const char *helpers_path,
                               const char * const *multiarch_tuples,
                               GList *loadable)
{
  g_autoptr(GHashTable) loadable_seen = NULL;
  gsize i;
  GList *l;

  g_return_if_fail (which == SRT_TYPE_VULKAN_ICD
                    || which == SRT_TYPE_EGL_ICD
                    || which == SRT_TYPE_EGL_EXTERNAL_PLATFORM
                    || which == SRT_TYPE_VULKAN_LAYER);

  loadable_seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  for (l = loadable; l != NULL; l = l->next)
    {
      g_autofree gchar *resolved_path = NULL;
      const gchar *name = NULL;

      if (which == SRT_TYPE_VULKAN_ICD
          || which == SRT_TYPE_EGL_ICD
          || which == SRT_TYPE_EGL_EXTERNAL_PLATFORM)
        {
          if (which == SRT_TYPE_VULKAN_ICD)
            resolved_path = srt_vulkan_icd_resolve_library_path (l->data);
          else if (which == SRT_TYPE_EGL_ICD)
            resolved_path = srt_egl_icd_resolve_library_path (l->data);
          else
            resolved_path = srt_egl_external_platform_resolve_library_path (l->data);

          if (resolved_path == NULL)
            continue;

          if (multiarch_tuples == NULL)
            {
              /* If we don't have the multiarch_tuples just use the
               * resolved_path as is */
              _update_duplicated_value (which, loadable_seen, resolved_path, l->data);
            }
          else
            {
              for (i = 0; multiarch_tuples[i] != NULL; i++)
                {
                  g_autofree gchar *canonical_path = NULL;
                  canonical_path = _get_library_canonical_path (envp, helpers_path,
                                                                multiarch_tuples[i],
                                                                resolved_path);

                  if (canonical_path == NULL)
                    {
                      /* Either the library is of a different ELF class or it is missing */
                      g_debug ("Unable to get the absolute path of \"%s\" via inspect-library",
                               resolved_path);
                      continue;
                    }

                  _update_duplicated_value (which, loadable_seen, canonical_path, l->data);
                }
            }
        }
      else if (which == SRT_TYPE_VULKAN_LAYER)
        {
          resolved_path = srt_vulkan_layer_resolve_library_path (l->data);
          name = srt_vulkan_layer_get_name (l->data);

          if (resolved_path == NULL && name == NULL)
            continue;

          if (multiarch_tuples == NULL || resolved_path == NULL)
            {
              g_autofree gchar *hash_key = NULL;
              /* We need a key for the hashtable that includes the name and
               * the path in it. We use '//' as a separator between the two
               * values, because we don't expect to have '//' in the
               * path, nor in the name. In the very unlikely event where
               * a collision happens, we will just consider two layers
               * as duplicated when in reality they weren't. */
              hash_key = g_strdup_printf ("%s//%s", name, resolved_path);
              _update_duplicated_value (which, loadable_seen, hash_key, l->data);
            }
          else
            {
              for (i = 0; multiarch_tuples[i] != NULL; i++)
                {
                  g_autofree gchar *canonical_path = NULL;
                  g_autofree gchar *hash_key = NULL;
                  canonical_path = _get_library_canonical_path (envp, helpers_path,
                                                                multiarch_tuples[i],
                                                                resolved_path);

                  if (canonical_path == NULL)
                    {
                      /* Either the library is of a different ELF class or it is missing */
                      g_debug ("Unable to get the absolute path of \"%s\" via inspect-library",
                               resolved_path);
                      continue;
                    }

                  /* We need a key for the hashtable that includes the name and
                   * the canonical path in it. We use '//' as a separator
                   * between the two values, because we don't expect to have
                   * '//' in the path, nor in the name. In the very unlikely
                   * event where a collision happens, we will just consider
                   * two layers as duplicated when in reality they weren't. */
                  hash_key = g_strdup_printf ("%s//%s", name, canonical_path);
                  _update_duplicated_value (which, loadable_seen, hash_key, l->data);
                }
            }
        }
      else
        {
          g_return_if_reached ();
        }
    }
}

/*
 * load_json_dir:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @dir: A directory to search
 * @suffix: (nullable): A path to append to @dir, such as `"vulkan/icd.d"`
 * @sort: (nullable): If not %NULL, load ICDs sorted by filename in this order
 * @load_json_cb: Called for each potential ICD found
 * @user_data: Passed to @load_json_cb
 */
void
load_json_dir (const char *sysroot,
               const char *dir,
               const char *suffix,
               GCompareFunc sort,
               void (*load_json_cb) (const char *, const char *, void *),
               void *user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GDir) dir_iter = NULL;
  g_autofree gchar *canon = NULL;
  g_autofree gchar *sysrooted_dir = NULL;
  g_autofree gchar *suffixed_dir = NULL;
  const char *iter_dir;
  const char *member;
  g_autoptr(GPtrArray) members = NULL;
  gsize i;

  g_return_if_fail (sysroot != NULL);
  g_return_if_fail (load_json_cb != NULL);

  if (dir == NULL)
    return;

  if (!g_path_is_absolute (dir))
    {
      canon = g_canonicalize_filename (dir, NULL);
      dir = canon;
    }

  if (suffix != NULL)
    {
      suffixed_dir = g_build_filename (dir, suffix, NULL);
      dir = suffixed_dir;
    }

  sysrooted_dir = g_build_filename (sysroot, dir, NULL);
  iter_dir = sysrooted_dir;

  g_debug ("Looking for ICDs in %s (in sysroot %s)...", dir, sysroot);

  dir_iter = g_dir_open (iter_dir, 0, &error);

  if (dir_iter == NULL)
    {
      g_debug ("Failed to open \"%s\": %s", iter_dir, error->message);
      return;
    }

  members = g_ptr_array_new_with_free_func (g_free);

  while ((member = g_dir_read_name (dir_iter)) != NULL)
    {
      if (!g_str_has_suffix (member, ".json"))
        continue;

      g_ptr_array_add (members, g_strdup (member));
    }

  if (sort != READDIR_ORDER)
    g_ptr_array_sort (members, sort);

  for (i = 0; i < members->len; i++)
    {
      gchar *path;

      member = g_ptr_array_index (members, i);
      path = g_build_filename (dir, member, NULL);
      load_json_cb (sysroot, path, user_data);
      g_free (path);
    }
}

/*
 * load_json_dir:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @sysroot_fd: A file descriptor opened on @sysroot, or negative to
 *  reopen it
 * @search_paths: Directories to search
 * @suffix: (nullable): A path to append to @dir, such as `"vulkan/icd.d"`
 * @sort: (nullable): If not %NULL, load ICDs sorted by filename in this order
 * @load_json_cb: Called for each potential ICD found
 * @user_data: Passed to @load_json_cb
 *
 * If @search_paths contains duplicated directories they'll be filtered out
 * to prevent loading the same JSONs multiple times.
 */
void
load_json_dirs (const char *sysroot,
                int sysroot_fd,
                GStrv search_paths,
                const char *suffix,
                GCompareFunc sort,
                void (*load_json_cb) (const char *, const char *, void *),
                void *user_data)
{
  gchar **iter;
  g_autoptr(GHashTable) searched_set = NULL;
  g_autoptr(GError) error = NULL;
  glnx_autofd int local_sysroot_fd = -1;

  g_return_if_fail (sysroot != NULL);
  g_return_if_fail (load_json_cb != NULL);

  searched_set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  if (sysroot_fd < 0)
    {
      if (!glnx_opendirat (-1, sysroot, FALSE, &local_sysroot_fd, &error))
        {
          g_warning ("An error occurred trying to open \"%s\": %s", sysroot,
                     error->message);
          return;
        }

      sysroot_fd = local_sysroot_fd;
    }

  for (iter = search_paths;
       iter != NULL && *iter != NULL;
       iter++)
    {
      glnx_autofd int file_fd = -1;
      g_autofree gchar *file_realpath_in_sysroot = NULL;

      file_fd = _srt_resolve_in_sysroot (sysroot_fd,
                                         *iter, SRT_RESOLVE_FLAGS_NONE,
                                         &file_realpath_in_sysroot, &error);

      if (file_realpath_in_sysroot == NULL)
        {
          /* Skip it if the path doesn't exist or is not reachable */
          g_debug ("An error occurred while resolving \"%s\": %s", *iter, error->message);
          g_clear_error (&error);
          continue;
        }

      if (!g_hash_table_contains (searched_set, file_realpath_in_sysroot))
        {
          g_hash_table_add (searched_set, g_steal_pointer (&file_realpath_in_sysroot));
          load_json_dir (sysroot, *iter, suffix, sort, load_json_cb, user_data);
        }
      else
        {
          g_debug ("Skipping \"%s\" because we already loaded the JSONs from it",
                   file_realpath_in_sysroot);
        }
    }
}

/*
 * load_icd_from_json:
 * @type: %SRT_TYPE_EGL_ICD or %SRT_TYPE_EGL_EXTERNAL_PLATFORM or %SRT_TYPE_VULKAN_ICD
 * @sysroot: (not nullable): The root directory, usually `/`
 * @filename: The filename of the metadata
 * @list: (element-type GObject) (transfer full) (inout): Prepend the
 *  resulting #SrtEglIcd or #SrtEglExternalPlatform or #SrtVulkanIcd to this list
 *
 * Load an EGL or Vulkan ICD from a JSON metadata file.
 */
void
load_icd_from_json (GType type,
                    const char *sysroot,
                    const char *filename,
                    GList **list)
{
  g_autoptr(JsonParser) parser = NULL;
  g_autofree gchar *canon = NULL;
  g_autofree gchar *path = NULL;
  g_autoptr(GError) error = NULL;
  /* These are all borrowed from the parser */
  JsonNode *node;
  JsonObject *object;
  JsonNode *subnode;
  JsonObject *icd_object;
  const char *file_format_version;
  const char *api_version = NULL;
  const char *library_path = NULL;
  gboolean portability_driver = FALSE;
  SrtLoadableIssues issues = SRT_LOADABLE_ISSUES_NONE;

  g_return_if_fail (type == SRT_TYPE_VULKAN_ICD
                    || type == SRT_TYPE_EGL_ICD
                    || type == SRT_TYPE_EGL_EXTERNAL_PLATFORM);
  g_return_if_fail (sysroot != NULL);
  g_return_if_fail (list != NULL);

  if (!g_path_is_absolute (filename))
    {
      canon = g_canonicalize_filename (filename, NULL);
      filename = canon;
    }

  path = g_build_filename (sysroot, filename, NULL);

  g_debug ("Attempting to load %s from %s", g_type_name (type), path);

  parser = json_parser_new ();

  if (!json_parser_load_from_file (parser, path, &error))
    {
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  node = json_parser_get_root (parser);

  if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Expected to find a JSON object in \"%s\"", path);
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  object = json_node_get_object (node);

  file_format_version = _srt_json_object_get_string_member (object,
                                                            "file_format_version");
  if (file_format_version == NULL)
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "file_format_version in \"%s\" is either missing or not a string",
                   path);
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  if (type == SRT_TYPE_VULKAN_ICD)
    {
      /*
       * The compatibility rules for Vulkan ICDs are not clear.
       * See https://github.com/KhronosGroup/Vulkan-Loader/issues/248
       *
       * The reference loader currently logs a warning, but carries on
       * anyway, if the file format version is not 1.0.0 or 1.0.1.
       * However, on #248 there's a suggestion that all the format versions
       * that are valid for layer JSON (1.0.x up to 1.0.1 and 1.1.x up
       * to 1.1.2) should also be considered valid for ICD JSON. For now
       * we assume that the rule is the same as for EGL, below.
       */
      if (!g_str_has_prefix (file_format_version, "1.0."))
        {
          g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Vulkan file_format_version in \"%s\" is not 1.0.x",
                       path);
          issues |= SRT_LOADABLE_ISSUES_UNSUPPORTED;
          goto out;
        }
    }
  else
    {
      g_assert (type == SRT_TYPE_EGL_ICD || type == SRT_TYPE_EGL_EXTERNAL_PLATFORM);
      /*
       * For EGL, all 1.0.x versions are officially backwards compatible
       * with 1.0.0.
       * https://github.com/NVIDIA/libglvnd/blob/HEAD/src/EGL/icd_enumeration.md
       * There's no specification or public loader for external platforms,
       * but we assume the same is true for those.
       */
      if (!g_str_has_prefix (file_format_version, "1.0."))
        {
          g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "EGL file_format_version in \"%s\" is not 1.0.x",
                       path);
          issues |= SRT_LOADABLE_ISSUES_UNSUPPORTED;
          goto out;
        }
    }

  subnode = json_object_get_member (object, "ICD");

  if (subnode == NULL
      || !JSON_NODE_HOLDS_OBJECT (subnode))
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No \"ICD\" object in \"%s\"", path);
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  icd_object = json_node_get_object (subnode);

  if (type == SRT_TYPE_VULKAN_ICD)
    {
      api_version = _srt_json_object_get_string_member (icd_object, "api_version");
      if (api_version == NULL)
        {
          g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "ICD.api_version in \"%s\" is either missing or not a string",
                       path);
          issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
          goto out;
        }
      portability_driver = json_object_get_boolean_member_with_default (icd_object,
                                                                        "is_portability_driver",
                                                                        FALSE);
      if (portability_driver)
        issues |= SRT_LOADABLE_ISSUES_API_SUBSET;
    }

  library_path = _srt_json_object_get_string_member (icd_object, "library_path");
  if (library_path == NULL)
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "ICD.library_path in \"%s\" is either missing or not a string",
                   path);
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

out:
  if (type == SRT_TYPE_VULKAN_ICD)
    {
      if (error == NULL)
        *list = g_list_prepend (*list, srt_vulkan_icd_new (filename, api_version,
                                                           library_path, portability_driver,
                                                           issues));
      else
        *list = g_list_prepend (*list, srt_vulkan_icd_new_error (filename, issues, error));
    }
  else if (type == SRT_TYPE_EGL_ICD)
    {
      if (error == NULL)
        *list = g_list_prepend (*list, srt_egl_icd_new (filename, library_path, issues));
      else
        *list = g_list_prepend (*list, srt_egl_icd_new_error (filename, issues, error));
    }
  else if (type == SRT_TYPE_EGL_EXTERNAL_PLATFORM)
    {
      if (error == NULL)
        *list = g_list_prepend (*list, srt_egl_external_platform_new (filename, library_path,
                                                                      issues));
      else
        *list = g_list_prepend (*list, srt_egl_external_platform_new_error (filename, issues,
                                                                            error));
    }
  else
    {
      g_return_if_reached ();
    }
}
