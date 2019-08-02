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

#include <steam-runtime-tools/library.h>

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
gboolean srt_system_info_can_write_to_uinput (SrtSystemInfo *self);
SrtLibraryIssues srt_system_info_check_libraries (SrtSystemInfo *self,
                                                  const gchar *multiarch_tuple,
                                                  GList **libraries_out);
SrtLibraryIssues srt_system_info_check_library (SrtSystemInfo *self,
                                                const gchar *multiarch_tuple,
                                                const gchar *soname,
                                                SrtLibrary **more_details_out);
