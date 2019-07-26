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

#include "steam-runtime-tools/library.h"

/*
 * _srt_library_new:
 * @multiarch_tuple: A multiarch tuple like %SRT_ABI_I386,
 *  representing an ABI
 * @absolute_path: (nullable) Absolute path of @soname
 * @soname: A SONAME like libz.so.1
 * @issues: Problems found when loading a @multiarch_tuple copy
 *  of @soname
 * @missing_symbols: (nullable) (array zero-terminated=1) (element-type utf8):
 *  Symbols we expected to find in @soname but did not
 * @misversioned_symbols: (nullable) (array zero-terminated=1) (element-type utf8):
 *  Symbols we expected to find in @soname but were available with a different
 *  version
 * @dependencies: (nullable) (array zero-terminated=1) (element-type utf8):
 *  Dependencies of @soname
 *
 * Inline convenience function to create a new SrtLibrary.
 * This is not part of the public API.
 *
 * Returns: (transfer full): A new #SrtLibrary
 */
static inline SrtLibrary *_srt_library_new (const char *multiarch_tuple,
                                            const char *absolute_path,
                                            const char *soname,
                                            SrtLibraryIssues issues,
                                            const char * const *missing_symbols,
                                            const char * const *misversioned_symbols,
                                            const char * const *dependencies);

#ifndef __GTK_DOC_IGNORE__
static inline SrtLibrary *
_srt_library_new (const char *multiarch_tuple,
                  const char *absolute_path,
                  const char *soname,
                  SrtLibraryIssues issues,
                  const char * const *missing_symbols,
                  const char * const *misversioned_symbols,
                  const char * const *dependencies)
{
  g_return_val_if_fail (multiarch_tuple != NULL, NULL);
  g_return_val_if_fail (soname != NULL, NULL);
  return g_object_new (SRT_TYPE_LIBRARY,
                       "absolute-path", absolute_path,
                       "dependencies", dependencies,
                       "issues", issues,
                       "missing-symbols", missing_symbols,
                       "multiarch-tuple", multiarch_tuple,
                       "soname", soname,
                       "misversioned-symbols", misversioned_symbols,
                       NULL);
}
#endif
