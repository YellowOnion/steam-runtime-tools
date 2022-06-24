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

/**
 * SrtVaApiVersion:
 * @SRT_VA_API_VERSION_UNKNOWN: Unknown VA-API version
 * @SRT_VA_API_VERSION_LIBVA1: libva version 1.x (VA-API version 0.x, SONAME `libva.so.1`)
 * @SRT_VA_API_VERSION_LIBVA2: libva version 2.x (VA-API version 1.x, SONAME `libva.so.2`)
 *
 * The libva version a particular driver is compatible with.
 */
typedef enum
{
  SRT_VA_API_VERSION_UNKNOWN = 0,
  SRT_VA_API_VERSION_LIBVA1 = 1,
  SRT_VA_API_VERSION_LIBVA2 = 2
} SrtVaApiVersion;

typedef struct _SrtVaApiDriver SrtVaApiDriver;
typedef struct _SrtVaApiDriverClass SrtVaApiDriverClass;

#define SRT_TYPE_VA_API_DRIVER (srt_va_api_driver_get_type ())
#define SRT_VA_API_DRIVER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_VA_API_DRIVER, SrtVaApiDriver))
#define SRT_VA_API_DRIVER_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_VA_API_DRIVER, SrtVaApiDriverClass))
#define SRT_IS_VA_API_DRIVER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_VA_API_DRIVER))
#define SRT_IS_VA_API_DRIVER_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_VA_API_DRIVER))
#define SRT_VA_API_DRIVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_VA_API_DRIVER, SrtVaApiDriverClass)
_SRT_PUBLIC
GType srt_va_api_driver_get_type (void);

_SRT_PUBLIC
const gchar *srt_va_api_driver_get_library_path (SrtVaApiDriver *self);
_SRT_PUBLIC
gboolean srt_va_api_driver_is_extra (SrtVaApiDriver *self);
_SRT_PUBLIC
gchar *srt_va_api_driver_resolve_library_path (SrtVaApiDriver *self);
_SRT_PUBLIC
SrtVaApiVersion srt_va_api_driver_get_version (SrtVaApiDriver *self);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtVaApiDriver, g_object_unref)
#endif
