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

#include "steam-runtime-tools/graphics.h"

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

#ifndef __GTK_DOC_IGNORE__
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
#endif

