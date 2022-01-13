/*<private_header>*/
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

#pragma once

#include "steam-runtime-tools/steam-runtime-tools.h"
#include "steam-runtime-tools/graphics-internal.h"
#include "steam-runtime-tools/utils-internal.h"

typedef struct
{
  gchar *name;
  gchar *spec_version;
  gchar **entrypoints;
} DeviceExtension;

typedef struct
{
  gchar *name;
  gchar *spec_version;
} InstanceExtension;

typedef struct
{
  gchar *name;
  gchar *value;
} EnvironmentVariable;

/* EGL and Vulkan ICDs are actually basically the same, but we don't
 * hard-code that in the API.
 * Vulkan Layers have the same structure too but with some extra fields. */
typedef struct
{
  GError *error;
  SrtLoadableIssues issues;
  gchar *api_version;   /* Always NULL when found in a SrtEglIcd */
  gchar *json_path;
  /* Either a filename, or a relative/absolute path in the sysroot */
  gchar *library_path;
  gchar *file_format_version;
  gchar *name;
  gchar *type;
  gchar *implementation_version;
  gchar *description;
  GStrv component_layers;
  /* Standard name => dlsym() name to call instead
   * (element-type utf8 utf8) */
  GHashTable *functions;
  /* (element-type InstanceExtension) */
  GList *instance_extensions;
  /* Standard name to intercept => dlsym() name to call instead
   * (element-type utf8 utf8) */
  GHashTable *pre_instance_functions;
  /* (element-type DeviceExtension) */
  GList *device_extensions;
  EnvironmentVariable enable_env_var;
  EnvironmentVariable disable_env_var;
} SrtLoadable;

static inline void
device_extension_free (gpointer p)
{
  DeviceExtension *self = p;

  g_free (self->name);
  g_free (self->spec_version);
  g_strfreev (self->entrypoints);
  g_slice_free (DeviceExtension, self);
}

static inline void
instance_extension_free (gpointer p)
{
  InstanceExtension *self = p;

  g_free (self->name);
  g_free (self->spec_version);
  g_slice_free (InstanceExtension, self);
}

void srt_loadable_clear (SrtLoadable *self);
gchar *srt_loadable_resolve_library_path (const SrtLoadable *self);
gboolean srt_loadable_check_error (const SrtLoadable *self,
                                   GError **error);
gboolean srt_loadable_write_to_file (const SrtLoadable *self,
                                     const char *path,
                                     GType which,
                                     GError **error);
void _srt_loadable_flag_duplicates (GType which,
                                    gchar **envp,
                                    const char *helpers_path,
                                    const char * const *multiarch_tuples,
                                    GList *loadable);

/*
 * A #GCompareFunc that does not sort the members of the directory.
 */
#define READDIR_ORDER ((GCompareFunc) NULL)

void load_json_dir (const char *sysroot,
                    const char *dir,
                    const char *suffix,
                    GCompareFunc sort,
                    void (*load_json_cb) (const char *, const char *, void *),
                    void *user_data);
void load_json_dirs (const char *sysroot,
                     GStrv search_paths,
                     const char *suffix,
                     GCompareFunc sort,
                     void (*load_json_cb) (const char *, const char *, void *),
                     void *user_data);
gboolean load_json (GType type,
                    const char *path,
                    gchar **api_version_out,
                    gchar **library_path_out,
                    SrtLoadableIssues *issues_out,
                    GError **error);

void _srt_egl_external_platform_set_is_duplicated (SrtEglExternalPlatform *self,
                                                   gboolean is_duplicated);
void _srt_egl_icd_set_is_duplicated (SrtEglIcd *self,
                                     gboolean is_duplicated);
void _srt_vulkan_icd_set_is_duplicated (SrtVulkanIcd *self,
                                        gboolean is_duplicated);
void _srt_vulkan_layer_set_is_duplicated (SrtVulkanLayer *self,
                                          gboolean is_duplicated);
