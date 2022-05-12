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

#include <steam-runtime-tools/container.h>
#include <steam-runtime-tools/cpu-feature.h>
#include <steam-runtime-tools/graphics.h>
#include <steam-runtime-tools/library.h>
#include <steam-runtime-tools/locale.h>
#include <steam-runtime-tools/macros.h>
#include <steam-runtime-tools/runtime.h>
#include <steam-runtime-tools/steam.h>
#include <steam-runtime-tools/virtualization.h>
#include <steam-runtime-tools/xdg-portal.h>

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

typedef struct _SrtSystemInfo SrtSystemInfo;
typedef struct _SrtSystemInfoClass SrtSystemInfoClass;

#define SRT_TYPE_SYSTEM_INFO srt_system_info_get_type ()
#define SRT_SYSTEM_INFO(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_SYSTEM_INFO, SrtSystemInfo))
#define SRT_SYSTEM_INFO_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_SYSTEM_INFO, SrtSystemInfoClass))
#define SRT_IS_SYSTEM_INFO(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_SYSTEM_INFO))
#define SRT_IS_SYSTEM_INFO_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_SYSTEM_INFO))
#define SRT_SYSTEM_INFO_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_SYSTEM_INFO, SrtSystemInfoClass)

_SRT_PUBLIC
GType srt_system_info_get_type (void);

_SRT_PUBLIC
SrtSystemInfo *srt_system_info_new (const char *expectations);

_SRT_PUBLIC
SrtSystemInfo *srt_system_info_new_from_json (const char *path,
                                              GError **error);

_SRT_PUBLIC
gboolean srt_system_info_can_run (SrtSystemInfo *self,
                                  const char *multiarch_tuple);
_SRT_PUBLIC
SrtContainerInfo *srt_system_info_check_container (SrtSystemInfo *self);
_SRT_PUBLIC
SrtContainerType srt_system_info_get_container_type (SrtSystemInfo *self);
_SRT_PUBLIC
gchar *srt_system_info_dup_container_host_directory (SrtSystemInfo *self);
_SRT_PUBLIC
SrtVirtualizationInfo *srt_system_info_check_virtualization (SrtSystemInfo *self);

_SRT_PUBLIC
gboolean srt_system_info_can_write_to_uinput (SrtSystemInfo *self);
_SRT_PUBLIC
SrtLibraryIssues srt_system_info_check_libraries (SrtSystemInfo *self,
                                                  const gchar *multiarch_tuple,
                                                  GList **libraries_out);
_SRT_PUBLIC
SrtLibraryIssues srt_system_info_check_library (SrtSystemInfo *self,
                                                const gchar *multiarch_tuple,
                                                const gchar *requested_name,
                                                SrtLibrary **more_details_out);

_SRT_PUBLIC
SrtGraphicsIssues srt_system_info_check_graphics (SrtSystemInfo *self,
                                                  const char *multiarch_tuple,
                                                  SrtWindowSystem window_system,
                                                  SrtRenderingInterface rendering_interface,
                                                  SrtGraphics **details_out);

_SRT_PUBLIC
GList * srt_system_info_check_all_graphics (SrtSystemInfo *self,
                                            const char *multiarch_tuple);

_SRT_PUBLIC
GList *srt_system_info_list_egl_icds (SrtSystemInfo *self,
                                      const char * const *multiarch_tuples);
_SRT_PUBLIC
GList *srt_system_info_list_egl_external_platforms (SrtSystemInfo *self,
                                                    const char * const *multiarch_tuples);
_SRT_PUBLIC
GList *srt_system_info_list_vulkan_icds (SrtSystemInfo *self,
                                         const char * const *multiarch_tuples);
_SRT_PUBLIC
GList *srt_system_info_list_explicit_vulkan_layers (SrtSystemInfo *self);
_SRT_PUBLIC
GList *srt_system_info_list_implicit_vulkan_layers (SrtSystemInfo *self);
_SRT_PUBLIC
GList *srt_system_info_list_dri_drivers (SrtSystemInfo *self,
                                         const char *multiarch_tuple,
                                         SrtDriverFlags flags);
_SRT_PUBLIC
GList *srt_system_info_list_va_api_drivers (SrtSystemInfo *self,
                                            const char *multiarch_tuple,
                                            SrtDriverFlags flags);
_SRT_PUBLIC
GList *srt_system_info_list_vdpau_drivers (SrtSystemInfo *self,
                                           const char *multiarch_tuple,
                                           SrtDriverFlags flags);
_SRT_PUBLIC
GList *srt_system_info_list_glx_icds (SrtSystemInfo *self,
                                      const char *multiarch_tuple,
                                      SrtDriverFlags flags);

_SRT_PUBLIC
void srt_system_info_set_environ (SrtSystemInfo *self,
                                  gchar * const *env);
_SRT_PUBLIC
void srt_system_info_set_sysroot (SrtSystemInfo *self,
                                  const char *root);
_SRT_PUBLIC
void srt_system_info_set_helpers_path (SrtSystemInfo *self,
                                       const gchar *path);
_SRT_PUBLIC
const char *srt_system_info_get_primary_multiarch_tuple (SrtSystemInfo *self);
_SRT_PUBLIC
void srt_system_info_set_primary_multiarch_tuple (SrtSystemInfo *self,
                                                  const gchar *tuple);
_SRT_PUBLIC
void srt_system_info_set_multiarch_tuples (SrtSystemInfo *self,
                                           const gchar * const *tuples);
_SRT_PUBLIC
GStrv srt_system_info_dup_multiarch_tuples (SrtSystemInfo *self);
_SRT_PUBLIC
void srt_system_info_set_test_flags (SrtSystemInfo *self,
                                     SrtTestFlags flags);

_SRT_PUBLIC
void srt_system_info_set_expected_runtime_version (SrtSystemInfo *self,
                                                   const char *version);
_SRT_PUBLIC
gchar *srt_system_info_dup_expected_runtime_version (SrtSystemInfo *self);
_SRT_PUBLIC
SrtRuntimeIssues srt_system_info_get_runtime_issues (SrtSystemInfo *self);
_SRT_PUBLIC
gchar *srt_system_info_dup_runtime_path (SrtSystemInfo *self);
_SRT_PUBLIC
gchar *srt_system_info_dup_runtime_version (SrtSystemInfo *self);

_SRT_PUBLIC
SrtSteamIssues srt_system_info_get_steam_issues (SrtSystemInfo *self);
_SRT_PUBLIC
SrtSteam *srt_system_info_get_steam_details (SrtSystemInfo *self);
_SRT_PUBLIC
gchar *srt_system_info_dup_steam_installation_path (SrtSystemInfo *self);
_SRT_PUBLIC
gchar *srt_system_info_dup_steam_data_path (SrtSystemInfo *self);
_SRT_PUBLIC
gchar *srt_system_info_dup_steam_bin32_path (SrtSystemInfo *self);

_SRT_PUBLIC
gchar ** srt_system_info_list_pressure_vessel_overrides (SrtSystemInfo *self,
                                                         gchar ***messages);

_SRT_PUBLIC
gchar ** srt_system_info_list_pinned_libs_32 (SrtSystemInfo *self,
                                              gchar ***messages);
_SRT_PUBLIC
gchar ** srt_system_info_list_pinned_libs_64 (SrtSystemInfo *self,
                                              gchar ***messages);

_SRT_PUBLIC
SrtLocaleIssues srt_system_info_get_locale_issues (SrtSystemInfo *self);
_SRT_PUBLIC
SrtLocale *srt_system_info_check_locale (SrtSystemInfo *self,
                                         const char *requested_name,
                                         GError **error);

_SRT_PUBLIC
gchar *srt_system_info_dup_os_build_id (SrtSystemInfo *self);
_SRT_PUBLIC
gchar *srt_system_info_dup_os_id (SrtSystemInfo *self);
_SRT_PUBLIC
gchar **srt_system_info_dup_os_id_like (SrtSystemInfo *self,
                                        gboolean include_self);
_SRT_PUBLIC
gchar *srt_system_info_dup_os_name (SrtSystemInfo *self);
_SRT_PUBLIC
gchar *srt_system_info_dup_os_pretty_name (SrtSystemInfo *self);
_SRT_PUBLIC
gchar *srt_system_info_dup_os_variant (SrtSystemInfo *self);
_SRT_PUBLIC
gchar *srt_system_info_dup_os_variant_id (SrtSystemInfo *self);
_SRT_PUBLIC
gchar *srt_system_info_dup_os_version_codename (SrtSystemInfo *self);
_SRT_PUBLIC
gchar *srt_system_info_dup_os_version_id (SrtSystemInfo *self);

_SRT_PUBLIC
gchar **srt_system_info_list_driver_environment (SrtSystemInfo *self);

_SRT_PUBLIC
GList *srt_system_info_list_desktop_entries (SrtSystemInfo *self);

_SRT_PUBLIC
SrtX86FeatureFlags srt_system_info_get_x86_features (SrtSystemInfo *self);
_SRT_PUBLIC
SrtX86FeatureFlags srt_system_info_get_known_x86_features (SrtSystemInfo *self);

_SRT_PUBLIC
gchar *srt_system_info_dup_steamscript_path (SrtSystemInfo *self);
_SRT_PUBLIC
gchar *srt_system_info_dup_steamscript_version (SrtSystemInfo *self);

_SRT_PUBLIC
GList *srt_system_info_list_xdg_portal_backends (SrtSystemInfo *self);
_SRT_PUBLIC
GList *srt_system_info_list_xdg_portal_interfaces (SrtSystemInfo *self);
_SRT_PUBLIC
SrtXdgPortalIssues srt_system_info_get_xdg_portal_issues (SrtSystemInfo *self,
                                                          gchar **messages);

_SRT_PUBLIC
gboolean srt_system_info_check_runtime_linker (SrtSystemInfo *self,
                                               const char *multiarch_tuple,
                                               gchar **resolved,
                                               GError **error);

_SRT_PUBLIC
gchar *srt_system_info_dup_libdl_lib (SrtSystemInfo *self,
                                      const char *multiarch_tuple,
                                      GError **error);
_SRT_PUBLIC
gchar *srt_system_info_dup_libdl_platform (SrtSystemInfo *self,
                                           const char *multiarch_tuple,
                                           GError **error);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtSystemInfo, g_object_unref)
#endif
