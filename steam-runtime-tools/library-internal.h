/*<private_header>*/
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

#include "steam-runtime-tools/system-info-internal.h"

/*
 * _srt_library_new:
 * @multiarch_tuple: A multiarch tuple like %SRT_ABI_I386,
 *  representing an ABI
 * @absolute_path: (nullable) Absolute path of @requested_name
 * @requested_name: A SONAME like libz.so.1
 * @issues: Problems found when loading a @multiarch_tuple copy
 *  of @requested_name
 * @missing_symbols: (nullable) (array zero-terminated=1) (element-type utf8):
 *  Symbols we expected to find in @requested_name but did not
 * @misversioned_symbols: (nullable) (array zero-terminated=1) (element-type utf8):
 *  Symbols we expected to find in @requested_name but were available with a
 *  different version
 * @missing_versions: (nullable) (array zero-terminated=1) (element-type utf8):
 *  Versions we expected to find in @requested_name but did not
 * @dependencies: (nullable) (array zero-terminated=1) (element-type utf8):
 *  Dependencies of @requested_name
 * @exit_status: exit status of helper, or -1 if it did not exit normally
 * @terminating_signal: signal that terminated the helper, or 0
 *
 * Inline convenience function to create a new SrtLibrary.
 * This is not part of the public API.
 *
 * Returns: (transfer full): A new #SrtLibrary
 */
static inline SrtLibrary *_srt_library_new (const char *multiarch_tuple,
                                            const char *absolute_path,
                                            const char *requested_name,
                                            SrtLibraryIssues issues,
                                            const char *messages,
                                            const char * const *missing_symbols,
                                            const char * const *misversioned_symbols,
                                            const char * const *missing_versions,
                                            const char * const *dependencies,
                                            const char *real_soname,
                                            int exit_status,
                                            int terminating_signal);

#ifndef __GTK_DOC_IGNORE__
static inline SrtLibrary *
_srt_library_new (const char *multiarch_tuple,
                  const char *absolute_path,
                  const char *requested_name,
                  SrtLibraryIssues issues,
                  const char *messages,
                  const char * const *missing_symbols,
                  const char * const *misversioned_symbols,
                  const char * const *missing_versions,
                  const char * const *dependencies,
                  const char *real_soname,
                  int exit_status,
                  int terminating_signal)
{
  g_return_val_if_fail (multiarch_tuple != NULL, NULL);
  g_return_val_if_fail (requested_name != NULL, NULL);
  return g_object_new (SRT_TYPE_LIBRARY,
                       "absolute-path", absolute_path,
                       "dependencies", dependencies,
                       "issues", issues,
                       "messages", messages,
                       "missing-symbols", missing_symbols,
                       "multiarch-tuple", multiarch_tuple,
                       "misversioned-symbols", misversioned_symbols,
                       "missing-versions", missing_versions,
                       "exit-status", exit_status,
                       "real-soname", real_soname,
                       "requested-name", requested_name,
                       "terminating-signal", terminating_signal,
                       NULL);
}
#endif

G_GNUC_INTERNAL
SrtLibraryIssues _srt_check_library_presence (const char *helpers_path,
                                              const char *requested_name,
                                              const char *multiarch,
                                              const char *symbols_path,
                                              const char * const *hidden_deps,
                                              SrtCheckFlags check_flags,
                                              gchar **envp,
                                              SrtLibrarySymbolsFormat symbols_format,
                                              SrtLibrary **more_details_out);
