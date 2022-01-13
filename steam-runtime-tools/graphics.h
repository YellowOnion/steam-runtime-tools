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

#if !defined(_SRT_IN_SINGLE_HEADER) && !defined(_SRT_COMPILATION)
#error "Do not include directly, use <steam-runtime-tools/steam-runtime-tools.h>"
#endif

#include <glib.h>
#include <glib-object.h>

#include <steam-runtime-tools/macros.h>

typedef struct _SrtGraphics SrtGraphics;
typedef struct _SrtGraphicsClass SrtGraphicsClass;

#define SRT_TYPE_GRAPHICS srt_graphics_get_type ()
#define SRT_GRAPHICS(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_GRAPHICS, SrtGraphics))
#define SRT_GRAPHICS_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_GRAPHICS, SrtGraphicsClass))
#define SRT_IS_GRAPHICS(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_GRAPHICS))
#define SRT_IS_GRAPHICS_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_GRAPHICS))
#define SRT_GRAPHICS_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_GRAPHICS, SrtGraphicsClass)

_SRT_PUBLIC
GType srt_graphics_get_type (void);

typedef struct _SrtGraphicsDevice SrtGraphicsDevice;
typedef struct _SrtGraphicsDeviceClass SrtGraphicsDeviceClass;

#define SRT_TYPE_GRAPHICS_DEVICE (srt_graphics_device_get_type ())
#define SRT_GRAPHICS_DEVICE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_GRAPHICS_DEVICE, SrtGraphicsDevice))
#define SRT_GRAPHICS_DEVICE_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_GRAPHICS_DEVICE, SrtGraphicsDeviceClass))
#define SRT_IS_GRAPHICS_DEVICE(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_GRAPHICS_DEVICE))
#define SRT_IS_GRAPHICS_DEVICE_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_GRAPHICS_DEVICE))
#define SRT_GRAPHICS_DEVICE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_GRAPHICS_DEVICE, SrtGraphicsDeviceClass)
_SRT_PUBLIC
GType srt_graphics_device_get_type (void);

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
 * @SRT_GRAPHICS_LIBRARY_VENDOR_PRIMUS: The graphics driver is the Primus non-GLVND
 */
typedef enum
{
  SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN,
  SRT_GRAPHICS_LIBRARY_VENDOR_GLVND,
  SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN_NON_GLVND,
  SRT_GRAPHICS_LIBRARY_VENDOR_MESA,
  SRT_GRAPHICS_LIBRARY_VENDOR_NVIDIA,
  SRT_GRAPHICS_LIBRARY_VENDOR_PRIMUS,
} SrtGraphicsLibraryVendor;

/**
 * SrtLoadableIssues:
 * @SRT_LOADABLE_ISSUES_NONE: There are no problems
 * @SRT_LOADABLE_ISSUES_UNKNOWN: An internal error occurred while checking the
 *  loadable, or an unknown issue flag was encountered while reading a report
 * @SRT_LOADABLE_ISSUES_UNSUPPORTED: The API version of the JSON file is not
 *  supported yet
 * @SRT_LOADABLE_ISSUES_CANNOT_LOAD: Unable to parse the JSON file describing
 *  the loadable or unable to load the library
 * @SRT_LOADABLE_ISSUES_DUPLICATED: This loadable, and another one, have a
 *  library path that points to the same library, and, if available, also the
 *  same name
 *
 * A bitfield with flags representing problems with the loadables, or
 * %SRT_LOADABLE_ISSUES_NONE (which is numerically zero) if no problems
 * were detected.
 *
 * In general, more bits set means more problems.
 */
typedef enum
{
  SRT_LOADABLE_ISSUES_NONE = 0,
  SRT_LOADABLE_ISSUES_UNKNOWN = (1 << 0),
  SRT_LOADABLE_ISSUES_UNSUPPORTED = (1 << 1),
  SRT_LOADABLE_ISSUES_CANNOT_LOAD = (1 << 2),
  SRT_LOADABLE_ISSUES_DUPLICATED = (1 << 3),
} SrtLoadableIssues;

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
 * @SRT_RENDERING_INTERFACE_VAAPI: VA-API rendering interface
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

/**
 * SrtVkPhysicalDeviceType:
 * @SRT_VK_PHYSICAL_DEVICE_TYPE_OTHER: The GPU does not match any other available types
 * @SRT_VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: The GPU is typically one embedded in or
 *  tightly coupled with the host
 * @SRT_VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: The GPU is typically a separate processor
 *  connected to the host via an interlink.
 * @SRT_VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: The GPU is typically a virtual node in a
 *  virtualization environment
 * @SRT_VK_PHYSICAL_DEVICE_TYPE_CPU: The GPU is typically running on the same processors
 *  as the host (software rendering such as llvmpipe)
 *
 * These enums have been taken from the VkPhysicalDeviceType Vulkan specs.
 * Please keep them in sync.
 * https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VkPhysicalDeviceType.html
 */
typedef enum
{
  SRT_VK_PHYSICAL_DEVICE_TYPE_OTHER = 0,
  SRT_VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU = 1,
  SRT_VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU = 2,
  SRT_VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU = 3,
  SRT_VK_PHYSICAL_DEVICE_TYPE_CPU = 4,
} SrtVkPhysicalDeviceType;

_SRT_PUBLIC
SrtGraphicsIssues srt_graphics_device_get_issues (SrtGraphicsDevice *self);
_SRT_PUBLIC
const char *srt_graphics_device_get_name (SrtGraphicsDevice *self);
_SRT_PUBLIC
const char *srt_graphics_device_get_api_version (SrtGraphicsDevice *self);
_SRT_PUBLIC
guint32 srt_graphics_device_get_vulkan_driver_id (SrtGraphicsDevice *self);
_SRT_PUBLIC
const char *srt_graphics_device_get_driver_name (SrtGraphicsDevice *self);
_SRT_PUBLIC
const char *srt_graphics_device_get_driver_version (SrtGraphicsDevice *self);
_SRT_PUBLIC
const char *srt_graphics_device_get_vendor_id (SrtGraphicsDevice *self);
_SRT_PUBLIC
const char *srt_graphics_device_get_device_id (SrtGraphicsDevice *self);
_SRT_PUBLIC
const char *srt_graphics_device_get_messages (SrtGraphicsDevice *self);
_SRT_PUBLIC
SrtVkPhysicalDeviceType srt_graphics_device_get_device_type (SrtGraphicsDevice *self);

_SRT_PUBLIC
const char *srt_graphics_get_multiarch_tuple (SrtGraphics *self);
_SRT_PUBLIC
SrtGraphicsIssues srt_graphics_get_issues (SrtGraphics *self);
_SRT_PUBLIC
gboolean srt_graphics_library_is_vendor_neutral (SrtGraphics *self,
                                                 SrtGraphicsLibraryVendor *vendor_out);
_SRT_PUBLIC
SrtWindowSystem srt_graphics_get_window_system (SrtGraphics *self);
_SRT_PUBLIC
SrtRenderingInterface srt_graphics_get_rendering_interface (SrtGraphics *self);
_SRT_PUBLIC
const char *srt_graphics_get_version_string (SrtGraphics *self);
_SRT_PUBLIC
const char *srt_graphics_get_renderer_string (SrtGraphics *self);
_SRT_PUBLIC
const char *srt_graphics_get_messages (SrtGraphics *self);
_SRT_PUBLIC
GList *srt_graphics_get_devices (SrtGraphics *self);
_SRT_PUBLIC
gchar *srt_graphics_dup_parameters_string (SrtGraphics *self);
_SRT_PUBLIC
int srt_graphics_get_exit_status (SrtGraphics *self);
_SRT_PUBLIC
int srt_graphics_get_terminating_signal (SrtGraphics *self);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtGraphics, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtGraphicsDevice, g_object_unref)
#endif
