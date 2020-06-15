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

typedef struct _SrtGraphics SrtGraphics;
typedef struct _SrtGraphicsClass SrtGraphicsClass;

#define SRT_TYPE_GRAPHICS srt_graphics_get_type ()
#define SRT_GRAPHICS(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_GRAPHICS, SrtGraphics))
#define SRT_GRAPHICS_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_GRAPHICS, SrtGraphicsClass))
#define SRT_IS_GRAPHICS(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_GRAPHICS))
#define SRT_IS_GRAPHICS_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_GRAPHICS))
#define SRT_GRAPHICS_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_GRAPHICS, SrtGraphicsClass)

GType srt_graphics_get_type (void);

/* Backward compatibility with previous steam-runtime-tools naming */
#define SRT_GRAPHICS_ISSUES_INTERNAL_ERROR SRT_GRAPHICS_ISSUES_UNKNOWN

/**
 * SrtGraphicsIssues:
 * @SRT_GRAPHICS_ISSUES_NONE: There are no problems
 * @SRT_GRAPHICS_ISSUES_UNKNOWN: An internal error occurred while checking
 *  graphics, or an unknown issue flag was encountered while reading a report
 * @SRT_GRAPHICS_ISSUES_CANNOT_LOAD: Unable to load the necessary libraries and create rendering context
 * @SRT_GRAPHICS_ISSUES_SOFTWARE_RENDERING: The graphics renderer is software based
 * @SRT_GRAPHICS_ISSUES_TIMEOUT: The check for this graphics stack took
 *  too long to run and was terminated. This is likely to indicate that
 *  the graphics stack causes the process using it to hang.
 * @SRT_GRAPHICS_ISSUES_CANNOT_DRAW: The drawing test failed
 *
 * A bitfield with flags representing problems with the graphics stack, or
 * %SRT_GRAPHICS_ISSUES_NONE (which is numerically zero) if no problems
 * were detected.
 *
 * In general, more bits set means more problems.
 */
typedef enum
{
  SRT_GRAPHICS_ISSUES_NONE = 0,
  SRT_GRAPHICS_ISSUES_UNKNOWN = (1 << 0),
  SRT_GRAPHICS_ISSUES_CANNOT_LOAD = (1 << 1),
  SRT_GRAPHICS_ISSUES_SOFTWARE_RENDERING = (1 << 2),
  SRT_GRAPHICS_ISSUES_TIMEOUT = (1 << 3),
  SRT_GRAPHICS_ISSUES_CANNOT_DRAW = (1 << 4),
} SrtGraphicsIssues;

/**
 * SrtGraphicsLibraryVendor:
 * @SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN: Unable to check the graphics driver vendor
 * @SRT_GRAPHICS_LIBRARY_VENDOR_GLVND: The graphics driver is the vendor-neutral GLVND
 * @SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN_NON_GLVND: The graphics driver is non-GLVND,
 *  but the exact vendor is unknown
 * @SRT_GRAPHICS_LIBRARY_VENDOR_MESA: The graphics driver is the mesa non-GLVND
 * @SRT_GRAPHICS_LIBRARY_VENDOR_NVIDIA: The graphics driver is the Nvidia non-GLVND
 */
typedef enum
{
  SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN,
  SRT_GRAPHICS_LIBRARY_VENDOR_GLVND,
  SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN_NON_GLVND,
  SRT_GRAPHICS_LIBRARY_VENDOR_MESA,
  SRT_GRAPHICS_LIBRARY_VENDOR_NVIDIA,
} SrtGraphicsLibraryVendor;

/**
 * SrtWindowSystem:
 * @SRT_WINDOW_SYSTEM_X11: X11 window system, with GL: equivalent to GLX; with GLES: equivalent to EGL_X11; with Vulkan: use X11
 * @SRT_WINDOW_SYSTEM_GLX: GLX window system, only possible with GL
 * @SRT_WINDOW_SYSTEM_EGL_X11: EGL_X11 window system, only possible with GL/GLES
 */
typedef enum
{
  SRT_WINDOW_SYSTEM_X11,
  SRT_WINDOW_SYSTEM_GLX,
  SRT_WINDOW_SYSTEM_EGL_X11,
} SrtWindowSystem;

#define SRT_N_WINDOW_SYSTEMS (SRT_WINDOW_SYSTEM_EGL_X11 + 1)

/**
 * SrtRenderingInterface:
 * @SRT_RENDERING_INTERFACE_GL: GL rendering interface
 * @SRT_RENDERING_INTERFACE_GLESV2: GLESv2 rendering interfaces
 * @SRT_RENDERING_INTERFACE_VULKAN: Vulkan rendering interface
 * @SRT_RENDERING_INTERFACE_VDPAU: VDPAU rendering interface
 */
typedef enum
{
  SRT_RENDERING_INTERFACE_GL,
  SRT_RENDERING_INTERFACE_GLESV2,
  SRT_RENDERING_INTERFACE_VULKAN,
  SRT_RENDERING_INTERFACE_VDPAU,
  SRT_RENDERING_INTERFACE_VAAPI,
  /* ... possible future additions: GLESV1, GLESV3? */
} SrtRenderingInterface;

#define SRT_N_RENDERING_INTERFACES (SRT_RENDERING_INTERFACE_VAAPI + 1)

const char *srt_graphics_get_multiarch_tuple (SrtGraphics *self);
SrtGraphicsIssues srt_graphics_get_issues (SrtGraphics *self);
gboolean srt_graphics_library_is_vendor_neutral (SrtGraphics *self,
                                                 SrtGraphicsLibraryVendor *vendor_out);
SrtWindowSystem srt_graphics_get_window_system (SrtGraphics *self);
SrtRenderingInterface srt_graphics_get_rendering_interface (SrtGraphics *self);
const char *srt_graphics_get_version_string (SrtGraphics *self);
const char *srt_graphics_get_renderer_string (SrtGraphics *self);
const char *srt_graphics_get_messages (SrtGraphics *self);
gchar *srt_graphics_dup_parameters_string (SrtGraphics *self);
int srt_graphics_get_exit_status (SrtGraphics *self);
int srt_graphics_get_terminating_signal (SrtGraphics *self);

typedef struct _SrtEglIcd SrtEglIcd;
typedef struct _SrtEglIcdClass SrtEglIcdClass;

#define SRT_TYPE_EGL_ICD (srt_egl_icd_get_type ())
#define SRT_EGL_ICD(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_EGL_ICD, SrtEglIcd))
#define SRT_EGL_ICD_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_EGL_ICD, SrtEglIcdClass))
#define SRT_IS_EGL_ICD(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_EGL_ICD))
#define SRT_IS_EGL_ICD_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_EGL_ICD))
#define SRT_EGL_ICD_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_EGL_ICD, SrtEglIcdClass)
GType srt_egl_icd_get_type (void);

gboolean srt_egl_icd_check_error (SrtEglIcd *self,
                                  GError **error);
const gchar *srt_egl_icd_get_json_path (SrtEglIcd *self);
const gchar *srt_egl_icd_get_library_path (SrtEglIcd *self);
gchar *srt_egl_icd_resolve_library_path (SrtEglIcd *self);
SrtEglIcd *srt_egl_icd_new_replace_library_path (SrtEglIcd *self,
                                                 const char *path);
gboolean srt_egl_icd_write_to_file (SrtEglIcd *self,
                                    const char *path,
                                    GError **error);

typedef struct _SrtDriDriver SrtDriDriver;
typedef struct _SrtDriDriverClass SrtDriDriverClass;

#define SRT_TYPE_DRI_DRIVER (srt_dri_driver_get_type ())
#define SRT_DRI_DRIVER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_DRI_DRIVER, SrtDriDriver))
#define SRT_DRI_DRIVER_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_DRI_DRIVER, SrtDriDriverClass))
#define SRT_IS_DRI_DRIVER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_DRI_DRIVER))
#define SRT_IS_DRI_DRIVER_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_DRI_DRIVER))
#define SRT_DRI_DRIVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_DRI_DRIVER, SrtDriDriverClass)
GType srt_dri_driver_get_type (void);

const gchar *srt_dri_driver_get_library_path (SrtDriDriver *self);
gboolean srt_dri_driver_is_extra (SrtDriDriver *self);
gchar *srt_dri_driver_resolve_library_path (SrtDriDriver *self);

typedef struct _SrtVaApiDriver SrtVaApiDriver;
typedef struct _SrtVaApiDriverClass SrtVaApiDriverClass;

#define SRT_TYPE_VA_API_DRIVER (srt_va_api_driver_get_type ())
#define SRT_VA_API_DRIVER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_VA_API_DRIVER, SrtVaApiDriver))
#define SRT_VA_API_DRIVER_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_VA_API_DRIVER, SrtVaApiDriverClass))
#define SRT_IS_VA_API_DRIVER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_VA_API_DRIVER))
#define SRT_IS_VA_API_DRIVER_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_VA_API_DRIVER))
#define SRT_VA_API_DRIVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_VA_API_DRIVER, SrtVaApiDriverClass)
GType srt_va_api_driver_get_type (void);

const gchar *srt_va_api_driver_get_library_path (SrtVaApiDriver *self);
gboolean srt_va_api_driver_is_extra (SrtVaApiDriver *self);
gchar *srt_va_api_driver_resolve_library_path (SrtVaApiDriver *self);

typedef struct _SrtVdpauDriver SrtVdpauDriver;
typedef struct _SrtVdpauDriverClass SrtVdpauDriverClass;

#define SRT_TYPE_VDPAU_DRIVER (srt_vdpau_driver_get_type ())
#define SRT_VDPAU_DRIVER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_VDPAU_DRIVER, SrtVdpauDriver))
#define SRT_VDPAU_DRIVER_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_VDPAU_DRIVER, SrtVdpauDriverClass))
#define SRT_IS_VDPAU_DRIVER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_VDPAU_DRIVER))
#define SRT_IS_VDPAU_DRIVER_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_VDPAU_DRIVER))
#define SRT_VDPAU_DRIVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_VDPAU_DRIVER, SrtVdpauDriverClass)
GType srt_vdpau_driver_get_type (void);

const gchar *srt_vdpau_driver_get_library_path (SrtVdpauDriver *self);
const gchar *srt_vdpau_driver_get_library_link (SrtVdpauDriver *self);
gboolean srt_vdpau_driver_is_extra (SrtVdpauDriver *self);
gchar *srt_vdpau_driver_resolve_library_path (SrtVdpauDriver *self);

typedef struct _SrtVulkanIcd SrtVulkanIcd;
typedef struct _SrtVulkanIcdClass SrtVulkanIcdClass;

#define SRT_TYPE_VULKAN_ICD (srt_vulkan_icd_get_type ())
#define SRT_VULKAN_ICD(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_VULKAN_ICD, SrtVulkanIcd))
#define SRT_VULKAN_ICD_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_VULKAN_ICD, SrtVulkanIcdClass))
#define SRT_IS_VULKAN_ICD(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_VULKAN_ICD))
#define SRT_IS_VULKAN_ICD_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_VULKAN_ICD))
#define SRT_VULKAN_ICD_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_VULKAN_ICD, SrtVulkanIcdClass)
GType srt_vulkan_icd_get_type (void);

gboolean srt_vulkan_icd_check_error (SrtVulkanIcd *self,
                                     GError **error);
const gchar *srt_vulkan_icd_get_api_version (SrtVulkanIcd *self);
const gchar *srt_vulkan_icd_get_json_path (SrtVulkanIcd *self);
const gchar *srt_vulkan_icd_get_library_path (SrtVulkanIcd *self);
gchar *srt_vulkan_icd_resolve_library_path (SrtVulkanIcd *self);
SrtVulkanIcd *srt_vulkan_icd_new_replace_library_path (SrtVulkanIcd *self,
                                                       const char *path);
gboolean srt_vulkan_icd_write_to_file (SrtVulkanIcd *self,
                                       const char *path,
                                       GError **error);

typedef struct _SrtGlxIcd SrtGlxIcd;
typedef struct _SrtGlxIcdClass SrtGlxIcdClass;

#define SRT_TYPE_GLX_ICD (srt_glx_icd_get_type ())
#define SRT_GLX_ICD(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_GLX_ICD, SrtGlxIcd))
#define SRT_GLX_ICD_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_GLX_ICD, SrtGlxIcdClass))
#define SRT_IS_GLX_ICD(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_GLX_ICD))
#define SRT_IS_GLX_ICD_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_GLX_ICD))
#define SRT_GLX_ICD_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_GLX_ICD, SrtGlxIcdClass)
GType srt_glx_icd_get_type (void);

const gchar *srt_glx_icd_get_library_soname (SrtGlxIcd *self);
const gchar *srt_glx_icd_get_library_path (SrtGlxIcd *self);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtGraphics, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtEglIcd, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtDriDriver, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtVaApiDriver, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtVdpauDriver, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtVulkanIcd, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtGlxIcd, g_object_unref)
#endif
