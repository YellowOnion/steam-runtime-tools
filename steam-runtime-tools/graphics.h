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

/**
 * SrtGraphicsIssues:
 * @SRT_GRAPHICS_ISSUES_NONE: There are no problems
 * @SRT_GRAPHICS_ISSUES_INTERNAL_ERROR: An internal error of some kind has occurred
 * @SRT_GRAPHICS_ISSUES_CANNOT_LOAD: Unable to load the necessary libraries and create rendering context
 * @SRT_GRAPHICS_ISSUES_SOFTWARE_RENDERING: The graphics renderer is software based
 * @SRT_GRAPHICS_ISSUES_TIMEOUT: The check for this graphics stack took
 *  too long to run and was terminated. This is likely to indicate that
 *  the graphics stack causes the process using it to hang.
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
  SRT_GRAPHICS_ISSUES_INTERNAL_ERROR = (1 << 0),
  SRT_GRAPHICS_ISSUES_CANNOT_LOAD = (1 << 1),
  SRT_GRAPHICS_ISSUES_SOFTWARE_RENDERING = (1 << 2),
  SRT_GRAPHICS_ISSUES_TIMEOUT = (1 << 3),
} SrtGraphicsIssues;

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
 */
typedef enum
{
  SRT_RENDERING_INTERFACE_GL,
  SRT_RENDERING_INTERFACE_GLESV2,
  /* ... possible future additions: GLESV1, GLESV3? */
} SrtRenderingInterface;

#define SRT_N_RENDERING_INTERFACES (SRT_RENDERING_INTERFACE_GLESV2 + 1)

const char *srt_graphics_get_multiarch_tuple (SrtGraphics *self);
SrtGraphicsIssues srt_graphics_get_issues (SrtGraphics *self);
SrtWindowSystem srt_graphics_get_window_system (SrtGraphics *self);
SrtRenderingInterface srt_graphics_get_rendering_interface (SrtGraphics *self);
const char *srt_graphics_get_version_string (SrtGraphics *self);
const char *srt_graphics_get_renderer_string (SrtGraphics *self);
gchar *srt_graphics_dup_parameters_string (SrtGraphics *self);

