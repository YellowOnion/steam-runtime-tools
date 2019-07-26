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

#include "steam-runtime-tools/library-internal.h"
#include "test-utils.h"

typedef struct
{
  int unused;
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
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;
}

/*
 * Test basic functionality of the SrtLibrary object.
 */
static void
test_object (Fixture *f,
             gconstpointer context)
{
  SrtLibrary *library;
  const char * const *missing;
  const char * const *misversioned;
  const char * const *dependencies;
  SrtLibraryIssues issues;
  gchar *tuple;
  gchar *soname;
  gchar *absolute_path;
  GStrv missing_mutable;
  GStrv misversioned_mutable;
  GStrv dependencies_mutable;
  const char *one_missing[] = { "jpeg_mem_src@LIBJPEGTURBO_6.2", NULL };
  const char *one_misversioned[] = { "jpeg_mem_dest@LIBJPEGTURBO_6.2", NULL };
  const char *two_deps[] = { "linux-vdso.so.1", "/usr/lib/libdl.so.2", NULL };

  library = _srt_library_new ("arm-linux-gnueabihf",
                              "/usr/lib/libz.so.1",
                              "libz.so.1",
                              SRT_LIBRARY_ISSUES_NONE,
                              NULL,
                              NULL,
                              NULL);
  g_assert_cmpint (srt_library_get_issues (library), ==,
                   SRT_LIBRARY_ISSUES_NONE);
  g_assert_cmpstr (srt_library_get_multiarch_tuple (library), ==,
                   "arm-linux-gnueabihf");
  g_assert_cmpstr (srt_library_get_soname (library), ==,
                   "libz.so.1");
  g_assert_cmpstr (srt_library_get_absolute_path (library), ==,
                   "/usr/lib/libz.so.1");
  missing = srt_library_get_missing_symbols (library);
  g_assert_nonnull (missing);
  g_assert_cmpstr (missing[0], ==, NULL);
  misversioned = srt_library_get_misversioned_symbols (library);
  g_assert_nonnull (misversioned);
  g_assert_cmpstr (misversioned[0], ==, NULL);
  dependencies = srt_library_get_dependencies (library);
  g_assert_nonnull (dependencies);
  g_assert_cmpstr (dependencies[0], ==, NULL);
  g_object_get (library,
                "multiarch-tuple", &tuple,
                "soname", &soname,
                "absolute-path", &absolute_path,
                "missing-symbols", &missing_mutable,
                "misversioned-symbols", &misversioned_mutable,
                "dependencies", &dependencies_mutable,
                "issues", &issues,
                NULL);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_NONE);
  g_assert_cmpstr (tuple, ==, "arm-linux-gnueabihf");
  g_assert_cmpstr (soname, ==, "libz.so.1");
  g_assert_cmpstr (absolute_path, ==, "/usr/lib/libz.so.1");
  g_assert_nonnull (missing_mutable);
  g_assert_cmpstr (missing_mutable[0], ==, NULL);
  g_assert_nonnull (misversioned_mutable);
  g_assert_cmpstr (misversioned_mutable[0], ==, NULL);
  g_assert_nonnull (dependencies_mutable);
  g_assert_cmpstr (dependencies_mutable[0], ==, NULL);
  g_free (tuple);
  g_free (soname);
  g_free (absolute_path);
  g_strfreev (missing_mutable);
  g_strfreev (misversioned_mutable);
  g_strfreev (dependencies_mutable);
  g_object_unref (library);

  library = _srt_library_new ("s390x-linux-gnu",
                              "/usr/lib/libjpeg.so.62",
                              "libjpeg.so.62",
                              SRT_LIBRARY_ISSUES_MISSING_SYMBOLS |
                              SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS,
                              one_missing,
                              one_misversioned,
                              two_deps);
  g_assert_cmpint (srt_library_get_issues (library) &
                   SRT_LIBRARY_ISSUES_MISSING_SYMBOLS, !=, 0);
  g_assert_cmpint (srt_library_get_issues (library) &
                   SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS, !=, 0);
  g_assert_cmpstr (srt_library_get_multiarch_tuple (library), ==,
                   "s390x-linux-gnu");
  g_assert_cmpstr (srt_library_get_soname (library), ==,
                   "libjpeg.so.62");
  g_assert_cmpstr (srt_library_get_absolute_path (library), ==,
                   "/usr/lib/libjpeg.so.62");
  missing = srt_library_get_missing_symbols (library);
  g_assert_nonnull (missing);
  g_assert_cmpstr (missing[0], ==, one_missing[0]);
  g_assert_cmpstr (missing[1], ==, NULL);
  misversioned = srt_library_get_misversioned_symbols (library);
  g_assert_nonnull (misversioned);
  g_assert_cmpstr (misversioned[0], ==, one_misversioned[0]);
  g_assert_cmpstr (misversioned[1], ==, NULL);
  dependencies = srt_library_get_dependencies (library);
  g_assert_nonnull (dependencies);
  g_assert_cmpstr (dependencies[0], ==, two_deps[0]);
  g_assert_cmpstr (dependencies[1], ==, two_deps[1]);
  g_assert_cmpstr (dependencies[2], ==, NULL);
  g_object_get (library,
                "multiarch-tuple", &tuple,
                "soname", &soname,
                "absolute-path", &absolute_path,
                "missing-symbols", &missing_mutable,
                "misversioned-symbols", &misversioned_mutable,
                "dependencies", &dependencies_mutable,
                "issues", &issues,
                NULL);
  g_assert_cmpint (issues & SRT_LIBRARY_ISSUES_MISSING_SYMBOLS, !=, 0);
  g_assert_cmpint (issues & SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS, !=, 0);
  g_assert_cmpstr (tuple, ==, "s390x-linux-gnu");
  g_assert_cmpstr (soname, ==, "libjpeg.so.62");
  g_assert_cmpstr (absolute_path, ==, "/usr/lib/libjpeg.so.62");
  g_assert_nonnull (missing_mutable);
  g_assert_cmpstr (missing_mutable[0], ==, one_missing[0]);
  g_assert_cmpstr (missing_mutable[1], ==, NULL);
  g_assert_nonnull (misversioned_mutable);
  g_assert_cmpstr (misversioned_mutable[0], ==, one_misversioned[0]);
  g_assert_cmpstr (misversioned_mutable[1], ==, NULL);
  g_assert_nonnull (dependencies_mutable);
  g_assert_cmpstr (dependencies_mutable[0], ==, two_deps[0]);
  g_assert_cmpstr (dependencies_mutable[1], ==, two_deps[1]);
  g_assert_cmpstr (dependencies_mutable[2], ==, NULL);
  g_free (tuple);
  g_free (soname);
  g_free (absolute_path);
  g_strfreev (missing_mutable);
  g_strfreev (misversioned_mutable);
  g_strfreev (dependencies_mutable);
  g_object_unref (library);
}

/*
 * Test the presence of `libz.so.1` that should be availabe in the system.
 */
static void
test_presence (Fixture *f,
               gconstpointer context)
{
  gboolean result;
  char *tmp_file = NULL;
  GString *expected_symbols = NULL;
  SrtLibrary *library = NULL;
  SrtLibraryIssues issues;
  const char * const *missing_symbols;
  const char * const *misversioned_symbols;
  const char * const *dependencies;
  int fd;
  gboolean seen_libc;
  GError *error = NULL;

  if (strcmp (_SRT_MULTIARCH, "") == 0)
    {
      g_test_skip ("Unsupported architecture");
      return;
    }

  fd = g_file_open_tmp ("library-XXXXXX", &tmp_file, &error);
  g_assert_no_error (error);
  g_assert_cmpint (fd, !=, -1);
  close (fd);

  expected_symbols = g_string_new ("inflateCopy@ZLIB_1.2.0\n"
                                   "inflateBack@ZLIB_1.2.0\n"
                                   "gzopen@Base");

  result = g_file_set_contents (tmp_file, expected_symbols->str, -1, &error);
  g_assert_no_error (error);
  g_assert_true (result);

  issues = srt_check_library_presence ("libz.so.1",
                                       _SRT_MULTIARCH,
                                       tmp_file,
                                       &library);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_NONE);

  missing_symbols = srt_library_get_missing_symbols (library);
  g_assert_nonnull (missing_symbols);
  g_assert_cmpstr (missing_symbols[0], ==, NULL);

  misversioned_symbols = srt_library_get_misversioned_symbols (library);
  g_assert_nonnull (misversioned_symbols);
  g_assert_cmpstr (misversioned_symbols[0], ==, NULL);

  dependencies = srt_library_get_dependencies (library);
  g_assert_nonnull (dependencies);
  g_assert_cmpstr (dependencies[0], !=, NULL);
  seen_libc = FALSE;

  for (gsize i = 0; dependencies[i] != NULL; i++)
    {
      g_debug ("libz.so.1 depends on %s", dependencies[i]);

      if (strstr (dependencies[i], "/libc.so.") != NULL)
        seen_libc = TRUE;
    }

  g_assert_true (seen_libc);

  g_unlink (tmp_file);
  g_free (tmp_file);
  g_string_free (expected_symbols, TRUE);
  g_object_unref (library);
  g_clear_error (&error);
}

/*
 * Test the presence of empty lines in expected symbols file.
 */
static void
test_empty_line (Fixture *f,
                 gconstpointer context)
{
  gboolean result;
  char *tmp_file = NULL;
  GString *expected_symbols = NULL;
  SrtLibrary *library = NULL;
  SrtLibraryIssues issues;
  const char * const *missing_symbols;
  const char * const *misversioned_symbols;
  int fd;
  GError *error = NULL;

  if (strcmp (_SRT_MULTIARCH, "") == 0)
    {
      g_test_skip ("Unsupported architecture");
      return;
    }

  fd = g_file_open_tmp ("library-XXXXXX", &tmp_file, &error);
  g_assert_no_error (error);
  g_assert_cmpint (fd, !=, -1);
  close (fd);

  expected_symbols = g_string_new ("\n"
                                   "inflateCopy@ZLIB_1.2.0\n"
                                   "\n"
                                   "inflateBack@ZLIB_1.2.0\n"
                                   "gzopen@Base"
                                   "\n");

  result = g_file_set_contents (tmp_file, expected_symbols->str, -1, &error);
  g_assert_no_error (error);
  g_assert_true (result);

  issues = srt_check_library_presence ("libz.so.1",
                                       _SRT_MULTIARCH,
                                       tmp_file,
                                       &library);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_NONE);

  missing_symbols = srt_library_get_missing_symbols (library);
  g_assert_nonnull (missing_symbols);
  g_assert_cmpstr (missing_symbols[0], ==, NULL);

  misversioned_symbols = srt_library_get_misversioned_symbols (library);
  g_assert_nonnull (misversioned_symbols);
  g_assert_cmpstr (misversioned_symbols[0], ==, NULL);

  g_unlink (tmp_file);
  g_free (tmp_file);
  g_string_free (expected_symbols, TRUE);
  g_object_unref (library);
  g_clear_error (&error);
}

/*
 * Test a library with wrong/missing symbols.
 */
static void
test_missing_symbols (Fixture *f,
                      gconstpointer context)
{
  gboolean result;
  char *tmp_file = NULL;
  GString *expected_symbols = NULL;
  SrtLibrary *library = NULL;
  SrtLibraryIssues issues;
  const char * const *missing_symbols;
  const char * const *misversioned_symbols;
  const char * const *dependencies;
  int fd;
  gboolean seen_libc;
  GError *error = NULL;

  if (strcmp (_SRT_MULTIARCH, "") == 0)
    {
      g_test_skip ("Unsupported architecture");
      return;
    }

  fd = g_file_open_tmp ("library-XXXXXX", &tmp_file, &error);
  g_assert_no_error (error);
  g_assert_cmpint (fd, !=, -1);
  close (fd);

  expected_symbols = g_string_new ("inflateCopy@ZLIB_1.2.0\n"
                                   "inflateFooBar@ZLIB_1.2.0\n"
                                   "jpeg_mem_src@LIBJPEGTURBO_6.2");

  result = g_file_set_contents (tmp_file, expected_symbols->str, -1, &error);
  g_assert_no_error (error);
  g_assert_true (result);

  issues = srt_check_library_presence ("libz.so.1",
                                       _SRT_MULTIARCH,
                                       tmp_file,
                                       &library);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_MISSING_SYMBOLS);

  g_debug ("path to libz.so.1 is %s", srt_library_get_absolute_path (library));
  g_assert_true (srt_library_get_absolute_path (library)[0] == '/');
  g_assert_true (g_file_test (srt_library_get_absolute_path (library), G_FILE_TEST_EXISTS));

  missing_symbols = srt_library_get_missing_symbols (library);
  g_assert_nonnull (missing_symbols);
  g_assert_cmpstr (missing_symbols[0], ==, "inflateFooBar@ZLIB_1.2.0");
  g_assert_cmpstr (missing_symbols[1], ==, "jpeg_mem_src@LIBJPEGTURBO_6.2");
  g_assert_cmpstr (missing_symbols[2], ==, NULL);

  misversioned_symbols = srt_library_get_misversioned_symbols (library);
  g_assert_nonnull (misversioned_symbols);
  g_assert_cmpstr (misversioned_symbols[0], ==, NULL);

  dependencies = srt_library_get_dependencies (library);
  g_assert_nonnull (dependencies);
  g_assert_cmpstr (dependencies[0], !=, NULL);
  seen_libc = FALSE;

  for (gsize i = 0; dependencies[i] != NULL; i++)
    {
      g_debug ("libz.so.1 depends on %s", dependencies[i]);

      if (strstr (dependencies[i], "/libc.so.") != NULL)
        seen_libc = TRUE;
    }

  g_assert_true (seen_libc);

  g_unlink (tmp_file);
  g_free (tmp_file);
  g_string_free (expected_symbols, TRUE);
  g_object_unref (library);
  g_clear_error (&error);
}

/*
 * Test a library with wrong/missing symbols and misversioned symbols.
 */
static void
test_misversioned_symbols (Fixture *f,
                           gconstpointer context)
{
  gboolean result;
  char *tmp_file = NULL;
  GString *expected_symbols = NULL;
  SrtLibrary *library = NULL;
  SrtLibraryIssues issues;
  const char * const *missing_symbols;
  const char * const *misversioned_symbols;
  const char * const *dependencies;
  int fd;
  gboolean seen_libc;
  GError *error = NULL;

  if (strcmp (_SRT_MULTIARCH, "") == 0)
    {
      g_test_skip ("Unsupported architecture");
      return;
    }

  fd = g_file_open_tmp ("library-XXXXXX", &tmp_file, &error);
  g_assert_no_error (error);
  g_assert_cmpint (fd, !=, -1);
  close (fd);

  expected_symbols = g_string_new ("inflateBack@MISSING");

  result = g_file_set_contents (tmp_file, expected_symbols->str, -1, &error);
  g_assert_no_error (error);
  g_assert_true (result);

  issues = srt_check_library_presence ("libz.so.1",
                                       _SRT_MULTIARCH,
                                       tmp_file,
                                       &library);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS);

  g_debug ("path to libz.so.1 is %s", srt_library_get_absolute_path (library));
  g_assert_true (srt_library_get_absolute_path (library)[0] == '/');
  g_assert_true (g_file_test (srt_library_get_absolute_path (library), G_FILE_TEST_EXISTS));

  missing_symbols = srt_library_get_missing_symbols (library);
  g_assert_nonnull (missing_symbols);
  g_assert_cmpstr (missing_symbols[0], ==, NULL);

  misversioned_symbols = srt_library_get_misversioned_symbols (library);
  g_assert_nonnull (misversioned_symbols);
  g_assert_cmpstr (misversioned_symbols[0], ==, "inflateBack@MISSING");
  g_assert_cmpstr (misversioned_symbols[1], ==, NULL);

  dependencies = srt_library_get_dependencies (library);
  g_assert_nonnull (dependencies);
  g_assert_cmpstr (dependencies[0], !=, NULL);
  seen_libc = FALSE;

  for (gsize i = 0; dependencies[i] != NULL; i++)
    {
      g_debug ("libz.so.1 depends on %s", dependencies[i]);

      if (strstr (dependencies[i], "/libc.so.") != NULL)
        seen_libc = TRUE;
    }

  g_assert_true (seen_libc);

  g_unlink (tmp_file);
  g_free (tmp_file);
  g_string_free (expected_symbols, TRUE);
  g_object_unref (library);
  g_clear_error (&error);
}

/*
 * Test a library with wrong/missing symbols and misversioned symbols.
 */
static void
test_missing_symbols_and_versions (Fixture *f,
                                   gconstpointer context)
{
  gboolean result;
  char *tmp_file = NULL;
  GString *expected_symbols = NULL;
  SrtLibrary *library = NULL;
  SrtLibraryIssues issues;
  const char * const *missing_symbols;
  const char * const *misversioned_symbols;
  const char * const *dependencies;
  int fd;
  gboolean seen_libc;
  GError *error = NULL;

  if (strcmp (_SRT_MULTIARCH, "") == 0)
    {
      g_test_skip ("Unsupported architecture");
      return;
    }

  fd = g_file_open_tmp ("library-XXXXXX", &tmp_file, &error);
  g_assert_no_error (error);
  g_assert_cmpint (fd, !=, -1);
  close (fd);

  expected_symbols = g_string_new ("inflateCopy@ZLIB_1.2.0\n"
                                   "inflateBack@MISSING\n"
                                   "inflateFooBar@ZLIB_1.2.0\n"
                                   "gzopen@ZLIB_1.2.0");

  result = g_file_set_contents (tmp_file, expected_symbols->str, -1, &error);
  g_assert_no_error (error);
  g_assert_true (result);

  issues = srt_check_library_presence ("libz.so.1",
                                       _SRT_MULTIARCH,
                                       tmp_file,
                                       &library);
  g_assert_cmpint (issues & SRT_LIBRARY_ISSUES_MISSING_SYMBOLS, !=, 0);
  g_assert_cmpint (issues & SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS, !=, 0);

  g_debug ("path to libz.so.1 is %s", srt_library_get_absolute_path (library));
  g_assert_true (srt_library_get_absolute_path (library)[0] == '/');
  g_assert_true (g_file_test (srt_library_get_absolute_path (library), G_FILE_TEST_EXISTS));

  missing_symbols = srt_library_get_missing_symbols (library);
  g_assert_nonnull (missing_symbols);
  g_assert_cmpstr (missing_symbols[0], ==, "inflateFooBar@ZLIB_1.2.0");
  g_assert_cmpstr (missing_symbols[1], ==, NULL);

  misversioned_symbols = srt_library_get_misversioned_symbols (library);
  g_assert_nonnull (misversioned_symbols);
  g_assert_cmpstr (misversioned_symbols[0], ==, "inflateBack@MISSING");
  g_assert_cmpstr (misversioned_symbols[1], ==, "gzopen@ZLIB_1.2.0");
  g_assert_cmpstr (misversioned_symbols[2], ==, NULL);

  dependencies = srt_library_get_dependencies (library);
  g_assert_nonnull (dependencies);
  g_assert_cmpstr (dependencies[0], !=, NULL);
  seen_libc = FALSE;

  for (gsize i = 0; dependencies[i] != NULL; i++)
    {
      g_debug ("libz.so.1 depends on %s", dependencies[i]);

      if (strstr (dependencies[i], "/libc.so.") != NULL)
        seen_libc = TRUE;
    }

  g_assert_true (seen_libc);

  g_unlink (tmp_file);
  g_free (tmp_file);
  g_string_free (expected_symbols, TRUE);
  g_object_unref (library);
  g_clear_error (&error);
}

/*
 * Test the presence of a missing library.
 */
static void
test_missing_library (Fixture *f,
                      gconstpointer context)
{
  SrtLibrary *library = NULL;
  SrtLibraryIssues issues;
  const char * const *missing_symbols;
  const char * const *misversioned_symbols;
  const char * const *dependencies;

  if (strcmp (_SRT_MULTIARCH, "") == 0)
    {
      g_test_skip ("Unsupported architecture");
      return;
    }

  issues = srt_check_library_presence ("libMISSING.so.62",
                                       _SRT_MULTIARCH,
                                       NULL,
                                       NULL);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_CANNOT_LOAD);

  issues = srt_check_library_presence ("libMISSING.so.62",
                                       _SRT_MULTIARCH,
                                       NULL,
                                       &library);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_CANNOT_LOAD);
  g_assert_cmpstr (srt_library_get_absolute_path (library), ==, NULL);

  missing_symbols = srt_library_get_missing_symbols (library);
  g_assert_nonnull (missing_symbols);
  g_assert_cmpstr (missing_symbols[0], ==, NULL);

  misversioned_symbols = srt_library_get_misversioned_symbols (library);
  g_assert_nonnull (misversioned_symbols);
  g_assert_cmpstr (misversioned_symbols[0], ==, NULL);

  dependencies = srt_library_get_dependencies (library);
  g_assert_nonnull (dependencies);
  g_assert_cmpstr (dependencies[0], ==, NULL);

  g_object_unref (library);
}

/*
 * Test a not supported architecture.
 */
static void
test_missing_arch (Fixture *f,
                   gconstpointer context)
{
  SrtLibrary *library = NULL;
  SrtLibraryIssues issues;
  const char * const *missing_symbols;
  const char * const *misversioned_symbols;
  const char * const *dependencies;

  issues = srt_check_library_presence ("libz.so.1",
                                       "hal9000-linux-gnu",
                                       NULL,
                                       &library);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_CANNOT_LOAD);
  g_assert_cmpstr (srt_library_get_absolute_path (library), ==, NULL);

  missing_symbols = srt_library_get_missing_symbols (library);
  g_assert_nonnull (missing_symbols);
  g_assert_cmpstr (missing_symbols[0], ==, NULL);

  misversioned_symbols = srt_library_get_misversioned_symbols (library);
  g_assert_nonnull (misversioned_symbols);
  g_assert_cmpstr (misversioned_symbols[0], ==, NULL);

  dependencies = srt_library_get_dependencies (library);
  g_assert_nonnull (dependencies);
  g_assert_cmpstr (dependencies[0], ==, NULL);
  g_assert_cmpstr ("hal9000-linux-gnu", ==, srt_library_get_multiarch_tuple (library));

  g_object_unref (library);
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add ("/object", Fixture, NULL,
              setup, test_object, teardown);
  g_test_add ("/presence", Fixture, NULL, setup, test_presence,
              teardown);
  g_test_add ("/empty_line", Fixture, NULL, setup, test_empty_line,
              teardown);
  g_test_add ("/missing_symbol", Fixture, NULL, setup,
              test_missing_symbols, teardown);
  g_test_add ("/misversioned_symbol_version", Fixture, NULL, setup,
              test_misversioned_symbols, teardown);
  g_test_add ("/missing_symbol_and_version", Fixture, NULL, setup,
              test_missing_symbols_and_versions, teardown);
  g_test_add ("/missing_library", Fixture, NULL, setup,
              test_missing_library, teardown);
  g_test_add ("/missing_arch", Fixture, NULL, setup,
              test_missing_arch, teardown);

  return g_test_run ();
}
