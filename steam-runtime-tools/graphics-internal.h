/*< internal_header >*/
/*
 * Copyright © 2019-2021 Collabora Ltd.
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
#include "steam-runtime-tools/system-info-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include <json-glib/json-glib.h>

/*
 * _srt_graphics_new:
 * @multiarch_tuple: A multiarch tuple like %SRT_ABI_I386,
 *  representing an ABI
 * @window_system: The window system to check
 * @rendering_interface: The rendering interface to check
 * @graphics_devices: Array of #SrtGraphicsDevice
 * @issues: Problems found when checking @multiarch_tuple with
 *  the given @winsys and @renderer.
 * @messages: Any debug messages found when checking graphics.
 * @exit_status: exit status of helper, or -1 if it did not exit normally
 * @termination_signal: signal that terminated the helper, or 0
 *
 * Inline convenience function to create a new SrtGraphics.
 * This is not part of the public API.
 *
 * Returns: (transfer full): A new #SrtGraphics
 */
static inline SrtGraphics *_srt_graphics_new (const char *multiarch_tuple,
                                              SrtWindowSystem window_system,
                                              SrtRenderingInterface rendering_interface,
                                              SrtGraphicsLibraryVendor library_vendor,
                                              const gchar *renderer_string,
                                              const gchar *version_string,
                                              GPtrArray *graphics_devices,
                                              SrtGraphicsIssues issues,
                                              const gchar *messages,
                                              int exit_status,
                                              int termination_signal);

/*
 * _srt_graphics_device_new:
 * @name: Device name, also referred as renderer name
 * @api_version: API version used by this device
 * @driver_version: Driver version used by this device
 * @vendor_id: Device vendor ID
 * @device_id: Device ID
 * @type: The device type
 * @issues: Problems found when checking the drawing capabilities of
 *  the device
 *
 * Inline convenience function to create a new SrtGraphicsDevice.
 * This is not part of the public API.
 *
 * Returns: (transfer full): A new #SrtGraphicsDevice
 */
static inline SrtGraphicsDevice *_srt_graphics_device_new (const gchar *name,
                                                           const gchar *api_version,
                                                           const gchar *driver_version,
                                                           const gchar *vendor_id,
                                                           const gchar *device_id,
                                                           SrtVkPhysicalDeviceType type,
                                                           SrtGraphicsIssues issues);


/*
 * _srt_graphics_hash_key:
 * @window_system: The window system,
 * @rendering_interface: The rendering interface,
 *
 * Generate an int hash key from a window system and rendering interface, used
 * in SrtSystemInfo to cache SrtGraphics objects and results based on window
 * system and rendering interface used
 *
 * Returns: A unique integer for each combination of window system and renderer.
 */
static inline int _srt_graphics_hash_key(SrtWindowSystem winsys, SrtRenderingInterface renderer);

#ifndef __GTK_DOC_IGNORE__

G_GNUC_INTERNAL SrtGraphicsIssues _srt_check_graphics (gchar **envp,
                                                       const char *helpers_path,
                                                       SrtTestFlags test_flags,
                                                       const char *multiarch_tuple,
                                                       SrtWindowSystem window_system,
                                                       SrtRenderingInterface rendering_interface,
                                                       SrtGraphics **details_out);

static inline SrtGraphics *
_srt_graphics_new (const char *multiarch_tuple,
                   SrtWindowSystem window_system,
                   SrtRenderingInterface rendering_interface,
                   SrtGraphicsLibraryVendor library_vendor,
                   const gchar *renderer_string,
                   const gchar *version_string,
                   GPtrArray *graphics_devices,
                   SrtGraphicsIssues issues,
                   const gchar *messages,
                   int exit_status,
                   int terminating_signal)
{
  g_return_val_if_fail (multiarch_tuple != NULL, NULL);
  return g_object_new (SRT_TYPE_GRAPHICS,
                       "multiarch-tuple", multiarch_tuple,
                       "issues", issues,
                       "library-vendor", library_vendor,
                       "window-system", window_system,
                       "rendering-interface", rendering_interface,
                       "renderer-string", renderer_string,
                       "version-string", version_string,
                       "graphics-devices", graphics_devices,
                       "messages", messages,
                       "exit-status", exit_status,
                       "terminating-signal", terminating_signal,
                       NULL);
}

static inline SrtGraphicsDevice *
_srt_graphics_device_new (const gchar *name,
                          const gchar *api_version,
                          const gchar *driver_version,
                          const gchar *vendor_id,
                          const gchar *device_id,
                          SrtVkPhysicalDeviceType type,
                          SrtGraphicsIssues issues)
{
  return g_object_new (SRT_TYPE_GRAPHICS_DEVICE,
                       "name", name,
                       "api-version", api_version,
                       "driver-version", driver_version,
                       "vendor-id", vendor_id,
                       "device-id", device_id,
                       "type", type,
                       "issues", issues,
                       NULL);
}

static inline int _srt_graphics_hash_key(SrtWindowSystem window_system,
                                         SrtRenderingInterface rendering_interface)
{
  G_STATIC_ASSERT (SRT_N_RENDERING_INTERFACES < 100);
  /* This allows us to have up to 100 unique renderers, we won't need nearly that
     many, but setting to 100 just to allow room to grow */
  return (int)window_system * 100 + (int)rendering_interface;
}

static inline const gchar *
_srt_graphics_window_system_string (SrtWindowSystem window_system)
{
  const gchar *result = srt_enum_value_to_nick (SRT_TYPE_WINDOW_SYSTEM, window_system);

  if (window_system == SRT_WINDOW_SYSTEM_EGL_X11)
    return "egl_x11";

  if (result != NULL)
    return result;

  return "unknown window system";
}

/**
 * _srt_graphics_window_system_nick_to_enum:
 * @nick: (not nullable): Nick of the window system enum
 * @window_system_out: (not nullable): Used to return the #SrtWindowSystem
 *  associated with the provided @nick
 * @error: Used to return an error on failure
 *
 * Returns: %TRUE on success
 */
static inline gboolean
_srt_graphics_window_system_nick_to_enum (const gchar *nick,
                                          SrtWindowSystem *window_system_out,
                                          GError **error)
{
  g_return_val_if_fail (nick != NULL, FALSE);
  g_return_val_if_fail (window_system_out != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (g_strcmp0 (nick, "egl_x11") == 0)
    {
      *window_system_out = SRT_WINDOW_SYSTEM_EGL_X11;
      return TRUE;
    }

  G_STATIC_ASSERT (sizeof (SrtWindowSystem) == sizeof (gint));

  return srt_enum_from_nick (SRT_TYPE_WINDOW_SYSTEM,
                             nick,
                             (gint *) window_system_out,
                             error);
}

/**
 * _srt_graphics_rendering_interface_string:
 * @rendering_interface: A #SrtRenderingInterface
 *
 * Returns: The string (nick) associated with the provided
 *  @rendering_interface, or "unknown rendering interface" if nothing was
 *  found.
 */
static inline const gchar *
_srt_graphics_rendering_interface_string (SrtRenderingInterface rendering_interface)
{
  const gchar *result = srt_enum_value_to_nick (SRT_TYPE_RENDERING_INTERFACE, rendering_interface);

  if (result != NULL)
    return result;

  return "unknown rendering interface";
}

/**
 * _srt_graphics_rendering_interface_nick_to_enum:
 * @nick: (not nullable): Nick of the rendering interface enum
 * @rendering_interface_out: (not nullable): Used to return the #SrtRenderingInterface
 *  associated with the provided @nick
 * @error: Used to return an error on failure
 *
 * Returns: %TRUE on success
 */
static inline gboolean
_srt_graphics_rendering_interface_nick_to_enum (const gchar *nick,
                                                SrtRenderingInterface *rendering_interface_out,
                                                GError **error)
{
  g_return_val_if_fail (nick != NULL, FALSE);
  g_return_val_if_fail (rendering_interface_out != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  G_STATIC_ASSERT (sizeof (SrtRenderingInterface) == sizeof (gint));

  return srt_enum_from_nick (SRT_TYPE_RENDERING_INTERFACE,
                             nick,
                             (gint *) rendering_interface_out,
                             error);
}

#endif

/**
 * SrtGraphicsModule:
 * @SRT_GRAPHICS_DRI_MODULE: Mesa DRI driver module
 * @SRT_GRAPHICS_VAAPI_MODULE: VA-API driver module
 * @SRT_GRAPHICS_VDPAU_MODULE: VDPAU driver module
 * @SRT_GRAPHICS_GLX_MODULE: GLX ICD module
 * @NUM_SRT_GRAPHICS_MODULES: 1 more than the last valid enum value
 */
typedef enum
{
  SRT_GRAPHICS_DRI_MODULE = 0,
  SRT_GRAPHICS_VAAPI_MODULE,
  SRT_GRAPHICS_VDPAU_MODULE,
  SRT_GRAPHICS_GLX_MODULE,
  NUM_SRT_GRAPHICS_MODULES  /* Always add new values before this one */
} SrtGraphicsModule;

G_GNUC_INTERNAL
GList *_srt_load_egl_icds (const char *helpers_path,
                           const char *sysroot,
                           gchar **envp,
                           const char * const *multiarch_tuples,
                           SrtCheckFlags check_flags);
G_GNUC_INTERNAL
GList *_srt_load_vulkan_icds (const char *helpers_path,
                              const char *sysroot,
                              gchar **envp,
                              const char * const *multiarch_tuples,
                              SrtCheckFlags check_flags);

G_GNUC_INTERNAL
GList *_srt_list_graphics_modules (const gchar *sysroot,
                                   gchar **envp,
                                   const char *helpers_path,
                                   const char *multiarch_tuple,
                                   SrtCheckFlags check_flags,
                                   SrtGraphicsModule which);

G_GNUC_INTERNAL
GList *_srt_load_vulkan_layers_extended (const char *helpers_path,
                                         const char *sysroot,
                                         gchar **envp,
                                         const char * const *multiarch_tuples,
                                         gboolean explicit,
                                         SrtCheckFlags check_flags);

void _srt_graphics_get_from_report (JsonObject *json_obj,
                                    const gchar *multiarch_tuple,
                                    GHashTable **cached_graphics);

GList *_srt_get_egl_from_json_report (JsonObject *json_obj);
GList *_srt_get_vulkan_from_json_report (JsonObject *json_obj);
GList *_srt_get_explicit_vulkan_layers_from_json_report (JsonObject *json_obj);
GList *_srt_get_implicit_vulkan_layers_from_json_report (JsonObject *json_obj);
GList *_srt_dri_driver_get_from_report (JsonObject *json_obj);
GList *_srt_va_api_driver_get_from_report (JsonObject *json_obj);
GList *_srt_vdpau_driver_get_from_report (JsonObject *json_obj);
GList *_srt_glx_icd_get_from_report (JsonObject *json_obj);

G_GNUC_INTERNAL
gchar ** _srt_graphics_get_vulkan_search_paths (const char *sysroot,
                                                gchar **envp,
                                                const char * const *multiarch_tuples,
                                                const char *suffix);

#define _SRT_GRAPHICS_EXPLICIT_VULKAN_LAYER_SUFFIX "vulkan/explicit_layer.d"
#define _SRT_GRAPHICS_IMPLICIT_VULKAN_LAYER_SUFFIX "vulkan/implicit_layer.d"
#define _SRT_GRAPHICS_VULKAN_ICD_SUFFIX "vulkan/icd.d"
