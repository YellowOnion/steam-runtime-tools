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

#if !defined(_SRT_IN_SINGLE_HEADER) && !defined(_SRT_COMPILATION)
#error "Do not include directly, use <steam-runtime-tools/steam-runtime-tools.h>"
#endif

#include <glib.h>
#include <glib-object.h>

#include <steam-runtime-tools/macros.h>

typedef struct _SrtGlxIcd SrtGlxIcd;
typedef struct _SrtGlxIcdClass SrtGlxIcdClass;

#define SRT_TYPE_GLX_ICD (srt_glx_icd_get_type ())
#define SRT_GLX_ICD(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_GLX_ICD, SrtGlxIcd))
#define SRT_GLX_ICD_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_GLX_ICD, SrtGlxIcdClass))
#define SRT_IS_GLX_ICD(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_GLX_ICD))
#define SRT_IS_GLX_ICD_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_GLX_ICD))
#define SRT_GLX_ICD_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_GLX_ICD, SrtGlxIcdClass)
_SRT_PUBLIC
GType srt_glx_icd_get_type (void);

_SRT_PUBLIC
const gchar *srt_glx_icd_get_library_soname (SrtGlxIcd *self);
_SRT_PUBLIC
const gchar *srt_glx_icd_get_library_path (SrtGlxIcd *self);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtGlxIcd, g_object_unref)
#endif
