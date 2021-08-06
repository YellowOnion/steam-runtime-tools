/*
 * Copyright © 2021 Collabora Ltd.
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

#include <glib.h>
#include <glib-object.h>

G_GNUC_INTERNAL gchar *_srt_libdl_detect_platform (gchar **envp,
                                                   const char *helpers_path,
                                                   const char *multiarch_tuple,
                                                   GError **error);

G_GNUC_INTERNAL gchar *_srt_libdl_detect_lib (gchar **envp,
                                              const char *helpers_path,
                                              const char *multiarch_tuple,
                                              GError **error);

typedef enum
{
  SRT_LOADABLE_KIND_ERROR,
  SRT_LOADABLE_KIND_BASENAME,
  SRT_LOADABLE_KIND_PATH,
} SrtLoadableKind;

typedef enum
{
  SRT_LOADABLE_FLAGS_DYNAMIC_TOKENS = (1 << 0),
  SRT_LOADABLE_FLAGS_ABI_DEPENDENT = (1 << 1),
  SRT_LOADABLE_FLAGS_ORIGIN = (1 << 2),
  SRT_LOADABLE_FLAGS_UNKNOWN_TOKENS = (1 << 3),
  SRT_LOADABLE_FLAGS_NONE = 0
} SrtLoadableFlags;

G_GNUC_INTERNAL
SrtLoadableKind _srt_loadable_classify (const char *loadable,
                                        SrtLoadableFlags *flags_out);
