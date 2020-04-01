/*
 * Copyright © 2019 Collabora Ltd.
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

#if !defined(_SRT_IN_SINGLE_HEADER) && !defined(_SRT_COMPILATION)
#error "Do not include directly, use <steam-runtime-tools/steam-runtime-tools.h>"
#endif

#include <glib.h>
#include <glib-object.h>

#include <steam-runtime-tools/graphics.h>
#include <steam-runtime-tools/library.h>
#include <steam-runtime-tools/locale.h>
#include <steam-runtime-tools/runtime.h>
#include <steam-runtime-tools/steam.h>

/**
 * SrtTestFlags:
 * @SRT_TEST_FLAGS_NONE: Behave normally
 * @SRT_TEST_FLAGS_TIME_OUT_SOONER: Reduce arbitrary timeouts
 *
 * A bitfield with flags representing behaviour changes used in automated
 * tests, or %SRT_TEST_FLAGS_NONE (which is numerically zero) for normal
 * production behaviour.
 */
typedef enum
{
  SRT_TEST_FLAGS_TIME_OUT_SOONER = (1 << 0),
  SRT_TEST_FLAGS_NONE = 0
} SrtTestFlags;

/**
 * SrtDriverFlags:
 * @SRT_DRIVER_FLAGS_NONE: Get just the drivers found in the canonical folders
 * @SRT_DRIVER_FLAGS_INCLUDE_ALL: Get all the drivers that have been found,
 *  even if they are in directories that we do not believe will normally be
 *  loaded (srt_dri_driver_is_extra() or srt_va_api_driver_is_extra() will
 *  return %TRUE)
 *
 * A bitfield with flags representing filters, used when retrieving a
 * list of drivers. Use %SRT_DRIVER_FLAGS_NONE for a list of the drivers that
 * are believed to be on the search path that will be used in practice.
 */
typedef enum
{
  SRT_DRIVER_FLAGS_INCLUDE_ALL = (1 << 1),
  SRT_DRIVER_FLAGS_NONE = 0
} SrtDriverFlags;

/**
 * SrtContainerType:
 * @SRT_CONTAINER_TYPE_UNKNOWN: Unknown container type
 * @SRT_CONTAINER_TYPE_NONE: No container detected
 * @SRT_CONTAINER_TYPE_FLATPAK: Running in a Flatpak app
 * @SRT_CONTAINER_TYPE_PRESSURE_VESSEL: Running in a Steam Runtime container
 *  using pressure-vessel
 * @SRT_CONTAINER_TYPE_DOCKER: Running in a Docker container
 *
 * A type of container.
 */
typedef enum
{
  SRT_CONTAINER_TYPE_NONE = 0,
  SRT_CONTAINER_TYPE_FLATPAK,
  SRT_CONTAINER_TYPE_PRESSURE_VESSEL,
  SRT_CONTAINER_TYPE_DOCKER,
  SRT_CONTAINER_TYPE_UNKNOWN = -1
} SrtContainerType;

typedef struct _SrtSystemInfo SrtSystemInfo;
typedef struct _SrtSystemInfoClass SrtSystemInfoClass;

#define SRT_TYPE_SYSTEM_INFO srt_system_info_get_type ()
#define SRT_SYSTEM_INFO(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_SYSTEM_INFO, SrtSystemInfo))
#define SRT_SYSTEM_INFO_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_SYSTEM_INFO, SrtSystemInfoClass))
#define SRT_IS_SYSTEM_INFO(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_SYSTEM_INFO))
#define SRT_IS_SYSTEM_INFO_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_SYSTEM_INFO))
#define SRT_SYSTEM_INFO_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_SYSTEM_INFO, SrtSystemInfoClass)

GType srt_system_info_get_type (void);

SrtSystemInfo *srt_system_info_new (const char *expectations);

gboolean srt_system_info_can_run (SrtSystemInfo *self,
                                  const char *multiarch_tuple);
SrtContainerType srt_system_info_get_container_type (SrtSystemInfo *self);
gchar *srt_system_info_dup_container_host_directory (SrtSystemInfo *self);

gboolean srt_system_info_can_write_to_uinput (SrtSystemInfo *self);
SrtLibraryIssues srt_system_info_check_libraries (SrtSystemInfo *self,
                                                  const gchar *multiarch_tuple,
                                                  GList **libraries_out);
SrtLibraryIssues srt_system_info_check_library (SrtSystemInfo *self,
                                                const gchar *multiarch_tuple,
                                                const gchar *soname,
                                                SrtLibrary **more_details_out);

SrtGraphicsIssues srt_system_info_check_graphics (SrtSystemInfo *self,
                                                  const char *multiarch_tuple,
                                                  SrtWindowSystem window_system,
                                                  SrtRenderingInterface rendering_interface,
                                                  SrtGraphics **details_out);

GList * srt_system_info_check_all_graphics (SrtSystemInfo *self,
                                         const char *multiarch_tuple);

GList *srt_system_info_list_egl_icds (SrtSystemInfo *self,
                                      const char * const *multiarch_tuples);
GList *srt_system_info_list_vulkan_icds (SrtSystemInfo *self,
                                         const char * const *multiarch_tuples);
GList *srt_system_info_list_dri_drivers (SrtSystemInfo *self,
                                         const char *multiarch_tuple,
                                         SrtDriverFlags flags);
GList *srt_system_info_list_va_api_drivers (SrtSystemInfo *self,
                                            const char *multiarch_tuple,
                                            SrtDriverFlags flags);
GList *srt_system_info_list_vdpau_drivers (SrtSystemInfo *self,
                                           const char *multiarch_tuple,
                                           SrtDriverFlags flags);
GList *srt_system_info_list_glx_icds (SrtSystemInfo *self,
                                      const char *multiarch_tuple,
                                      SrtDriverFlags flags);

void srt_system_info_set_environ (SrtSystemInfo *self,
                                  gchar * const *env);
void srt_system_info_set_sysroot (SrtSystemInfo *self,
                                  const char *root);
void srt_system_info_set_helpers_path (SrtSystemInfo *self,
                                       const gchar *path);
const char *srt_system_info_get_primary_multiarch_tuple (SrtSystemInfo *self);
void srt_system_info_set_primary_multiarch_tuple (SrtSystemInfo *self,
                                                  const gchar *tuple);
void srt_system_info_set_test_flags (SrtSystemInfo *self,
                                     SrtTestFlags flags);

void srt_system_info_set_expected_runtime_version (SrtSystemInfo *self,
                                                   const char *version);
gchar *srt_system_info_dup_expected_runtime_version (SrtSystemInfo *self);
SrtRuntimeIssues srt_system_info_get_runtime_issues (SrtSystemInfo *self);
gchar *srt_system_info_dup_runtime_path (SrtSystemInfo *self);
gchar *srt_system_info_dup_runtime_version (SrtSystemInfo *self);

SrtSteamIssues srt_system_info_get_steam_issues (SrtSystemInfo *self);
gchar *srt_system_info_dup_steam_installation_path (SrtSystemInfo *self);
gchar *srt_system_info_dup_steam_data_path (SrtSystemInfo *self);

gchar ** srt_system_info_list_pressure_vessel_overrides (SrtSystemInfo *self,
                                                         gchar ***messages);

gchar ** srt_system_info_list_pinned_libs_32 (SrtSystemInfo *self,
                                              gchar ***messages);
gchar ** srt_system_info_list_pinned_libs_64 (SrtSystemInfo *self,
                                              gchar ***messages);

SrtLocaleIssues srt_system_info_get_locale_issues (SrtSystemInfo *self);
SrtLocale *srt_system_info_check_locale (SrtSystemInfo *self,
                                         const char *requested_name,
                                         GError **error);

gchar *srt_system_info_dup_os_build_id (SrtSystemInfo *self);
gchar *srt_system_info_dup_os_id (SrtSystemInfo *self);
gchar **srt_system_info_dup_os_id_like (SrtSystemInfo *self,
                                        gboolean include_self);
gchar *srt_system_info_dup_os_name (SrtSystemInfo *self);
gchar *srt_system_info_dup_os_pretty_name (SrtSystemInfo *self);
gchar *srt_system_info_dup_os_variant (SrtSystemInfo *self);
gchar *srt_system_info_dup_os_variant_id (SrtSystemInfo *self);
gchar *srt_system_info_dup_os_version_codename (SrtSystemInfo *self);
gchar *srt_system_info_dup_os_version_id (SrtSystemInfo *self);

gchar **srt_system_info_list_driver_environment (SrtSystemInfo *self);

GList *srt_system_info_list_desktop_entries (SrtSystemInfo *self);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtSystemInfo, g_object_unref)
#endif
