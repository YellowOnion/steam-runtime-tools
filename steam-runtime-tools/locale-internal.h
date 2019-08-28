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

#ifndef __GTK_DOC_IGNORE__
static inline SrtLocale *_srt_locale_new (const char *requested_name,
                                          const char *resulting_name,
                                          const char *charset,
                                          gboolean is_utf8);

static inline SrtLocale *
_srt_locale_new (const char *requested_name,
                 const char *resulting_name,
                 const char *charset,
                 gboolean is_utf8)
{
  g_return_val_if_fail (requested_name != NULL, NULL);
  g_return_val_if_fail (resulting_name != NULL, NULL);
  g_return_val_if_fail (charset != NULL, NULL);

  return g_object_new (SRT_TYPE_LOCALE,
                       "requested-name", requested_name,
                       "resulting-name", resulting_name,
                       "charset", charset,
                       "is-utf8", is_utf8,
                       NULL);
}

G_GNUC_INTERNAL
SrtLocale *_srt_check_locale (const char *helpers_path,
                              const char *multiarch_tuple,
                              const char *requested_name,
                              GError **error);
#endif
