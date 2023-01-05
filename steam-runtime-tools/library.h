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

#include <steam-runtime-tools/macros.h>

typedef struct _SrtLibrary SrtLibrary;
typedef struct _SrtLibraryClass SrtLibraryClass;

#define SRT_TYPE_LIBRARY srt_library_get_type ()
#define SRT_LIBRARY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_LIBRARY, SrtLibrary))
#define SRT_LIBRARY_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_LIBRARY, SrtLibraryClass))
#define SRT_IS_LIBRARY(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_LIBRARY))
#define SRT_IS_LIBRARY_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_LIBRARY))
#define SRT_LIBRARY_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_LIBRARY, SrtLibraryClass)

_SRT_PUBLIC
GType srt_library_get_type (void);

/* Backward compatibility with previous steam-runtime-tools naming */
#define SRT_LIBRARY_ISSUES_INTERNAL_ERROR SRT_LIBRARY_ISSUES_UNKNOWN

/**
 * SrtLibraryIssues:
 * @SRT_LIBRARY_ISSUES_NONE: There are no problems
 * @SRT_LIBRARY_ISSUES_CANNOT_LOAD: The library could not be loaded
 * @SRT_LIBRARY_ISSUES_MISSING_SYMBOLS: Some of the expected symbols
 *  were not present
 * @SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS: Some of the expected symbols
 *  were available with a different version
 * @SRT_LIBRARY_ISSUES_UNKNOWN: A generic internal error occurred, or an
 *  unknown issue flag was encountered while reading a report
 * @SRT_LIBRARY_ISSUES_UNKNOWN_EXPECTATIONS: No directory containing
 *  expected ABIs has been set, so we cannot know which libraries we are
 *  meant to have found
 * @SRT_LIBRARY_ISSUES_TIMEOUT: Helper timed out when executing
 * @SRT_LIBRARY_ISSUES_MISSING_VERSIONS: Some of the expected version
 *  definitions were not present
 * @SRT_LIBRARY_ISSUES_UNVERSIONED: The library was expected to have
 *  at least one version definition, but has no version definitions
 *  at all. Implies %SRT_LIBRARY_ISSUES_MISSING_VERSIONS.
 *
 * A bitfield with flags representing problems with a library, or
 * %SRT_LIBRARY_ISSUES_NONE (which is numerically zero) if no problems
 * were detected.
 *
 * In general, more bits set means more problems.
 */
typedef enum
{
  SRT_LIBRARY_ISSUES_CANNOT_LOAD = (1 << 0),
  SRT_LIBRARY_ISSUES_MISSING_SYMBOLS = (1 << 1),
  SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS = (1 << 2),
  SRT_LIBRARY_ISSUES_UNKNOWN = (1 << 3),
  SRT_LIBRARY_ISSUES_UNKNOWN_EXPECTATIONS = (1 << 4),
  SRT_LIBRARY_ISSUES_TIMEOUT = (1 << 5),
  SRT_LIBRARY_ISSUES_MISSING_VERSIONS = (1 << 6),
  SRT_LIBRARY_ISSUES_UNVERSIONED = (1 << 7),
  SRT_LIBRARY_ISSUES_NONE = 0
} SrtLibraryIssues;

/**
 * SrtLibrarySymbolsFormat:
 * @SRT_LIBRARY_SYMBOLS_FORMAT_PLAIN: One symbol per line
 * @SRT_LIBRARY_SYMBOLS_FORMAT_DEB_SYMBOLS: deb-symbols(5) format
 *
 * The format of a file listing expected symbols.
 */
typedef enum
{
  SRT_LIBRARY_SYMBOLS_FORMAT_PLAIN = 0,
  SRT_LIBRARY_SYMBOLS_FORMAT_DEB_SYMBOLS
} SrtLibrarySymbolsFormat;

_SRT_PUBLIC
const char *srt_library_get_requested_name (SrtLibrary *self);
_SRT_PUBLIC
const char *srt_library_get_absolute_path (SrtLibrary *self);
#ifndef __GTK_DOC_IGNORE__
_SRT_PUBLIC G_DEPRECATED_FOR(srt_library_get_requested_name)
#endif
const char *srt_library_get_soname (SrtLibrary *self);
_SRT_PUBLIC
const char *srt_library_get_messages (SrtLibrary *self);
_SRT_PUBLIC
const char *srt_library_get_multiarch_tuple (SrtLibrary *self);
_SRT_PUBLIC
SrtLibraryIssues srt_library_get_issues (SrtLibrary *self);
_SRT_PUBLIC
const char * const *srt_library_get_missing_symbols (SrtLibrary *self);
_SRT_PUBLIC
const char * const *srt_library_get_misversioned_symbols (SrtLibrary *self);
_SRT_PUBLIC
const char * const *srt_library_get_missing_versions (SrtLibrary *self);
_SRT_PUBLIC
const char * const *srt_library_get_dependencies (SrtLibrary *self);
_SRT_PUBLIC
const char *srt_library_get_real_soname (SrtLibrary *self);
_SRT_PUBLIC
SrtLibraryIssues srt_check_library_presence (const char *requested_name,
                                             const char *multiarch,
                                             const char *symbols_path,
                                             SrtLibrarySymbolsFormat symbols_format,
                                             SrtLibrary **more_details_out);
_SRT_PUBLIC
int srt_library_get_exit_status (SrtLibrary *self);
_SRT_PUBLIC
int srt_library_get_terminating_signal (SrtLibrary *self);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtLibrary, g_object_unref)
#endif
