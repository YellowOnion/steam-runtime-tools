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

typedef struct _SrtLocale SrtLocale;
typedef struct _SrtLocaleClass SrtLocaleClass;

#define SRT_TYPE_LOCALE srt_locale_get_type ()
#define SRT_LOCALE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_LOCALE, SrtLocale))
#define SRT_LOCALE_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_LOCALE, SrtLocaleClass))
#define SRT_IS_LOCALE(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_LOCALE))
#define SRT_IS_LOCALE_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_LOCALE))
#define SRT_LOCALE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_LOCALE, SrtLocaleClass)

GType srt_locale_get_type (void);

/**
 * SrtLocaleError:
 * @SRT_LOCALE_ERROR_FAILED: Unable to set the locale
 * @SRT_LOCALE_ERROR_INTERNAL_ERROR: Unable to check whether the locale
 *  could be set or not
 *
 * Errors in this domain indicate that problems were encountered when
 * setting or inspecting a locale.
 */
typedef enum
{
  SRT_LOCALE_ERROR_FAILED = 0,
  SRT_LOCALE_ERROR_INTERNAL_ERROR,
} SrtLocaleError;

#define SRT_LOCALE_ERROR (srt_locale_error_quark ())

GQuark srt_locale_error_quark (void);

/**
 * SrtLocaleIssues:
 * @SRT_LOCALE_ISSUES_NONE: There are no problems
 * @SRT_LOCALE_ISSUES_INTERNAL_ERROR: An internal error of some kind has occurred
 * @SRT_LOCALE_ISSUES_DEFAULT_MISSING: `setlocale (LC_ALL, "")` fails.
 *  This indicates that environment variables like LANGUAGE and LC_ALL
 *  are set to values that do not match the locales available on the
 *  filesystem.
 * @SRT_LOCALE_ISSUES_DEFAULT_NOT_UTF8: `setlocale (LC_ALL, "")` succeeds
 *  but results in a a non-UTF-8 locale. This often breaks program and
 *  library assumptions, particularly around interoperable filenames.
 * @SRT_LOCALE_ISSUES_C_UTF8_MISSING: `setlocale (LC_ALL, "C.UTF-8")`
 *  does not succeed, or succeeds but results in a non-UTF-8 locale.
 *  This locale is available in Debian and Fedora derivatives, and is a
 *  UTF-8 equivalent of the standard C/POSIX locale. It has been proposed
 *  for inclusion in upstream glibc, but as of 2019 it is not available on
 *  all Linux systems.
 * @SRT_LOCALE_ISSUES_EN_US_UTF8_MISSING: `setlocale (LC_ALL, "en_US.UTF-8")`
 *  does not succeed, or succeeds but results in a non-UTF-8 locale.
 *  This locale is not generally guaranteed to exist on Linux systems,
 *  but some games and software packages assume that it does.
 * @SRT_LOCALE_ISSUES_I18N_SUPPORTED_MISSING: The `SUPPORTED` file
 *  listing supported locales was not found in the expected location.
 *  This indicates that either locale data is not installed, or this
 *  operating system does not put it in the expected location.
 *  The Steam Runtime might be unable to generate extra locales if needed.
 * @SRT_LOCALE_ISSUES_I18N_LOCALES_EN_US_MISSING: The `locales/en_US` file
 *  describing the USA English locale was not found in the expected
 *  location.
 *  This indicates that either locale data is not installed, or this
 *  operating system does not put it in the expected location, or only
 *  a partial set of locale source data is available.
 *  The Steam Runtime will be unable to generate extra locales if needed.
 *
 * A bitfield with flags representing potential problems with locales, or
 * %SRT_LOCALE_ISSUES_NONE (which is numerically zero) if no problems
 * were detected.
 *
 * In general, more bits set means more problems.
 */
typedef enum
{
  SRT_LOCALE_ISSUES_NONE = 0,
  SRT_LOCALE_ISSUES_INTERNAL_ERROR = (1 << 0),
  SRT_LOCALE_ISSUES_DEFAULT_MISSING = (1 << 1),
  SRT_LOCALE_ISSUES_DEFAULT_NOT_UTF8 = (1 << 2),
  SRT_LOCALE_ISSUES_C_UTF8_MISSING = (1 << 3),
  SRT_LOCALE_ISSUES_EN_US_UTF8_MISSING = (1 << 4),
  SRT_LOCALE_ISSUES_I18N_SUPPORTED_MISSING = (1 << 5),
  SRT_LOCALE_ISSUES_I18N_LOCALES_EN_US_MISSING = (1 << 6),
} SrtLocaleIssues;

const char *srt_locale_get_requested_name (SrtLocale *self);
const char *srt_locale_get_resulting_name (SrtLocale *self);
const char *srt_locale_get_charset (SrtLocale *self);
gboolean srt_locale_is_utf8 (SrtLocale *self);
