/*< internal_header >*/
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

#include "steam-runtime-tools/steam-runtime-tools.h"

/*
 * _srt_graphics_new:
 * @multiarch_tuple: A multiarch tuple like %SRT_ABI_I386,
 *  representing an ABI
 * @window_system: The window system to check,
 * @rendering_interface: The renderint interface to check,
 * @issues: Problems found when checking @multiarch_tuple with
 *  the given @winsys and @renderer.
 *
 * Inline convenience function to create a new SrtGraphics.
 * This is not part of the public API.
 *
 * Returns: (transfer full): A new #SrtGraphics
 */
static inline SrtGraphics *_srt_graphics_new (const char *multiarch_tuple,
                                              SrtWindowSystem window_system,
                                              SrtRenderingInterface rendering_interface,
                                              const gchar *renderer_string,
                                              const gchar *version_string,
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

G_GNUC_INTERNAL SrtGraphicsIssues _srt_check_graphics (const char *helpers_path,
                                                       const char *multiarch_tuple,
                                                       SrtWindowSystem window_system,
                                                       SrtRenderingInterface rendering_interface,
                                                       SrtGraphics **details_out);

static inline SrtGraphics *
_srt_graphics_new (const char *multiarch_tuple,
                   SrtWindowSystem window_system,
                   SrtRenderingInterface rendering_interface,
                   const gchar *renderer_string,
                   const gchar *version_string,
                   SrtGraphicsIssues issues)
{
  g_return_val_if_fail (multiarch_tuple != NULL, NULL);
  return g_object_new (SRT_TYPE_GRAPHICS,
                       "multiarch-tuple", multiarch_tuple,
                       "issues", issues,
                       "window-system", window_system,
                       "rendering-interface", rendering_interface,
                       "renderer-string", renderer_string,
                       "version-string", version_string,
                       NULL);
}

static inline int _srt_graphics_hash_key(SrtWindowSystem window_system,
                                         SrtRenderingInterface rendering_interface)
{
  /* This allows us to have up to 100 unique renderers, we won't need nearly that
     many, but setting to 100 just to allow room to grow */
  return (int)window_system * 100 + (int)rendering_interface;
}

static inline const gchar *
_srt_graphics_window_system_string (SrtWindowSystem window_system)
{
  if (window_system == SRT_WINDOW_SYSTEM_X11)
    return "x11";
  else if (window_system == SRT_WINDOW_SYSTEM_GLX)
    return "glx";
  else if (window_system == SRT_WINDOW_SYSTEM_EGL_X11)
    return "egl_x11";
  return "unknown window system";
}

static inline const gchar *
_srt_graphics_rendering_interface_string (SrtRenderingInterface rendering_interface)
{
  if (rendering_interface == SRT_RENDERING_INTERFACE_GL)
    return "gl";
  else if (rendering_interface == SRT_RENDERING_INTERFACE_GLESV2)
    return "glesv2";
  return "unknown rendering interface";
}

#endif

