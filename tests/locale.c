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

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "steam-runtime-tools/locale-internal.h"
#include "test-utils.h"

#define MOCK_DEFAULT_RESULTING_NAME \
  "LC_CTYPE=en_GB.UTF-8;" \
  "LC_NUMERIC=en_GB.utf8;" \
  "LC_TIME=en_GB.utf8;" \
  "LC_COLLATE=en_GB.UTF-8;" \
  "LC_MONETARY=en_GB.utf8;" \
  "LC_MESSAGES=en_GB.UTF-8;" \
  "LC_PAPER=en_GB.utf8;" \
  "LC_NAME=en_GB.UTF-8;" \
  "LC_ADDRESS=en_GB.UTF-8;" \
  "LC_TELEPHONE=en_GB.UTF-8;" \
  "LC_MEASUREMENT=en_GB.utf8;" \
  "LC_IDENTIFICATION=en_GB.UTF-8"

static const char *argv0;

typedef struct
{
  int unused;
  gchar *builddir;
} Fixture;

typedef struct
{
  int unused;
} Config;

static void
setup (Fixture *f,
       gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;
  f->builddir = g_strdup (g_getenv ("G_TEST_BUILDDIR"));

  if (f->builddir == NULL)
    f->builddir = g_path_get_dirname (argv0);
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_free (f->builddir);
}

/*
 * Test basic functionality of the SrtLocale object.
 */
static void
test_object (Fixture *f,
             gconstpointer context)
{
  SrtLocale *locale;
  gchar *req = NULL;
  gchar *res = NULL;
  gchar *charset = NULL;
  gboolean is_utf8 = FALSE;

  locale = _srt_locale_new ("",
                            "fr_CA.UTF-8",
                            "UTF-8",
                            TRUE);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "fr_CA.UTF-8");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "UTF-8");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, TRUE);
  g_object_get (locale,
                "requested-name", &req,
                "resulting-name", &res,
                "charset", &charset,
                "is-utf8", &is_utf8,
                NULL);
  g_assert_cmpstr (req, ==, "");
  g_assert_cmpstr (res, ==, "fr_CA.UTF-8");
  g_assert_cmpstr (charset, ==, "UTF-8");
  g_assert_cmpint (is_utf8, ==, TRUE);
  g_free (req);
  g_free (res);
  g_free (charset);
  g_object_unref (locale);

  locale = _srt_locale_new ("en_US",
                            "en_US",
                            "ISO-8859-1",
                            FALSE);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "en_US");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "en_US");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "ISO-8859-1");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, FALSE);
  g_object_unref (locale);
}

static gboolean
is_host_os_like (SrtSystemInfo *info,
                 const gchar *host)
{
  g_auto(GStrv) os_like = NULL;
  gsize i;

  os_like = srt_system_info_dup_os_id_like (info, TRUE);
  for (i = 0; os_like != NULL && os_like[i] != NULL; i++)
    {
      if (g_strcmp0 (os_like[i], host) == 0)
        return TRUE;
    }
  return FALSE;
}

static void
test_complete (Fixture *f,
               gconstpointer context)
{
  SrtLocale *locale = NULL;
  SrtSystemInfo *info = srt_system_info_new (NULL);
  SrtLocaleIssues issues;
  SrtLocaleIssues additional_issues = SRT_LOCALE_ISSUES_NONE;
  GError *error = NULL;

  /* With Arch Linux it is expected to not have the SUPPORTED locale file */
  if (is_host_os_like (info, "arch"))
    additional_issues = SRT_LOCALE_ISSUES_I18N_SUPPORTED_MISSING;

  srt_system_info_set_primary_multiarch_tuple (info, "mock");
  srt_system_info_set_helpers_path (info, f->builddir);

  issues = srt_system_info_get_locale_issues (info);
  g_assert_cmpint (issues, ==,
                   (SRT_LOCALE_ISSUES_NONE
                    | additional_issues));

  locale = srt_system_info_check_locale (info, "C", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "C");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "C");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "ANSI_X3.4-1968");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, FALSE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "POSIX", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "POSIX");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "C");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "ANSI_X3.4-1968");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, FALSE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "C.UTF-8", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "C.UTF-8");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "C.UTF-8");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "UTF-8");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, TRUE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "en_US", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "en_US");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "en_US");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "ISO-8859-1");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, FALSE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "en_US.UTF-8", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "en_US.UTF-8");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "en_US.UTF-8");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "UTF-8");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, TRUE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "en_GB.UTF-8", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "en_GB.UTF-8");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "en_GB.UTF-8");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "UTF-8");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, TRUE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==,
                   MOCK_DEFAULT_RESULTING_NAME);
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "UTF-8");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, TRUE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "fr_CA", &error);
  g_assert_error (error, SRT_LOCALE_ERROR, SRT_LOCALE_ERROR_FAILED);
  g_assert_null (locale);
  g_clear_error (&error);

  g_clear_object (&info);
}

static void
test_legacy (Fixture *f,
             gconstpointer context)
{
  SrtLocale *locale = NULL;
  SrtSystemInfo *info = srt_system_info_new (NULL);
  SrtLocaleIssues issues;
  SrtLocaleIssues additional_issues = SRT_LOCALE_ISSUES_NONE;
  GError *error = NULL;

  if (is_host_os_like (info, "arch"))
    additional_issues = SRT_LOCALE_ISSUES_I18N_SUPPORTED_MISSING;

  srt_system_info_set_primary_multiarch_tuple (info, "mock-legacy");
  srt_system_info_set_helpers_path (info, f->builddir);

  issues = srt_system_info_get_locale_issues (info);
  g_assert_cmpint (issues, ==,
                   (SRT_LOCALE_ISSUES_DEFAULT_NOT_UTF8
                    | SRT_LOCALE_ISSUES_C_UTF8_MISSING
                    | additional_issues));

  locale = srt_system_info_check_locale (info, "C", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "C");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "C");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "ANSI_X3.4-1968");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, FALSE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "POSIX", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "POSIX");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "C");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "ANSI_X3.4-1968");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, FALSE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "C.UTF-8", &error);
  g_assert_error (error, SRT_LOCALE_ERROR, SRT_LOCALE_ERROR_FAILED);
  g_assert_null (locale);
  g_clear_error (&error);

  locale = srt_system_info_check_locale (info, "en_US", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "en_US");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "en_US");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "ISO-8859-1");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, FALSE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "en_US.UTF-8", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "en_US.UTF-8");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "en_US.UTF-8");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "UTF-8");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, TRUE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "en_GB.UTF-8", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "en_GB.UTF-8");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "en_GB.UTF-8");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "UTF-8");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, TRUE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "en_US");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "ISO-8859-1");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, FALSE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "fr_CA", &error);
  g_assert_error (error, SRT_LOCALE_ERROR, SRT_LOCALE_ERROR_FAILED);
  g_assert_null (locale);
  g_clear_error (&error);

  g_clear_object (&info);
}

static void
test_unamerican (Fixture *f,
                 gconstpointer context)
{
  SrtLocale *locale = NULL;
  SrtSystemInfo *info = srt_system_info_new (NULL);
  SrtLocaleIssues issues;
  SrtLocaleIssues additional_issues = SRT_LOCALE_ISSUES_NONE;
  GError *error = NULL;

  if (is_host_os_like (info, "arch"))
    additional_issues = SRT_LOCALE_ISSUES_I18N_SUPPORTED_MISSING;

  srt_system_info_set_primary_multiarch_tuple (info, "mock-unamerican");
  srt_system_info_set_helpers_path (info, f->builddir);

  issues = srt_system_info_get_locale_issues (info);
  g_assert_cmpint (issues, ==,
                   (SRT_LOCALE_ISSUES_EN_US_UTF8_MISSING
                    | additional_issues));

  locale = srt_system_info_check_locale (info, "C", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "C");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "C");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "ANSI_X3.4-1968");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, FALSE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "POSIX", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "POSIX");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "C");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "ANSI_X3.4-1968");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, FALSE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "C.UTF-8", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "C.UTF-8");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "C.UTF-8");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "UTF-8");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, TRUE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "en_US", &error);
  g_assert_error (error, SRT_LOCALE_ERROR, SRT_LOCALE_ERROR_FAILED);
  g_assert_null (locale);
  g_clear_error (&error);

  locale = srt_system_info_check_locale (info, "en_US.UTF-8", &error);
  g_assert_error (error, SRT_LOCALE_ERROR, SRT_LOCALE_ERROR_FAILED);
  g_assert_null (locale);
  g_clear_error (&error);

  locale = srt_system_info_check_locale (info, "en_GB.UTF-8", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "en_GB.UTF-8");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==, "en_GB.UTF-8");
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "UTF-8");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, TRUE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "", &error);
  g_assert_no_error (error);
  g_assert_nonnull (locale);
  g_assert_cmpstr (srt_locale_get_requested_name (locale), ==, "");
  g_assert_cmpstr (srt_locale_get_resulting_name (locale), ==,
                   MOCK_DEFAULT_RESULTING_NAME);
  g_assert_cmpstr (srt_locale_get_charset (locale), ==, "UTF-8");
  g_assert_cmpint (srt_locale_is_utf8 (locale), ==, TRUE);
  g_clear_object (&locale);

  locale = srt_system_info_check_locale (info, "fr_CA", &error);
  g_assert_error (error, SRT_LOCALE_ERROR, SRT_LOCALE_ERROR_FAILED);
  g_assert_null (locale);
  g_clear_error (&error);

  g_clear_object (&info);
}

int
main (int argc,
      char **argv)
{
  _srt_tests_global_debug_log_to_stderr ();
  argv0 = argv[0];

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/locale/object", Fixture, NULL,
              setup, test_object, teardown);
  g_test_add ("/locale/complete", Fixture, NULL,
              setup, test_complete, teardown);
  g_test_add ("/locale/legacy", Fixture, NULL,
              setup, test_legacy, teardown);
  g_test_add ("/locale/unamerican", Fixture, NULL,
              setup, test_unamerican, teardown);

  return g_test_run ();
}
