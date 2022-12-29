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

#include <libglnx.h>

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
  gchar *messages;
  gchar *tuple;
  gchar *requested_name;
  gchar *compat_soname;
  gchar *real_soname;
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
                              "",
                              NULL,
                              NULL,
                              NULL,
                              "libz_special.so.1",
                              0,
                              0);
  g_assert_cmpint (srt_library_get_issues (library), ==,
                   SRT_LIBRARY_ISSUES_NONE);
  g_assert_cmpstr (srt_library_get_messages (library), ==, NULL);
  g_assert_cmpstr (srt_library_get_multiarch_tuple (library), ==,
                   "arm-linux-gnueabihf");
  g_assert_cmpstr (srt_library_get_requested_name (library), ==,
                   "libz.so.1");
  G_GNUC_BEGIN_IGNORE_DEPRECATIONS
  g_assert_cmpstr (srt_library_get_soname (library), ==,
                   srt_library_get_requested_name (library));
  G_GNUC_END_IGNORE_DEPRECATIONS
  g_assert_cmpstr (srt_library_get_real_soname (library), ==,
                   "libz_special.so.1");
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
                "messages", &messages,
                "multiarch-tuple", &tuple,
                "requested-name", &requested_name,
                "real-soname", &real_soname,
                "absolute-path", &absolute_path,
                "missing-symbols", &missing_mutable,
                "misversioned-symbols", &misversioned_mutable,
                "dependencies", &dependencies_mutable,
                "issues", &issues,
                "soname", &compat_soname,
                NULL);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_NONE);
  g_assert_cmpstr (messages, ==, NULL);
  g_assert_cmpstr (tuple, ==, "arm-linux-gnueabihf");
  g_assert_cmpstr (requested_name, ==, "libz.so.1");
  g_assert_cmpstr (compat_soname, ==, requested_name);
  g_assert_cmpstr (real_soname, ==, "libz_special.so.1");
  g_assert_cmpstr (absolute_path, ==, "/usr/lib/libz.so.1");
  g_assert_nonnull (missing_mutable);
  g_assert_cmpstr (missing_mutable[0], ==, NULL);
  g_assert_nonnull (misversioned_mutable);
  g_assert_cmpstr (misversioned_mutable[0], ==, NULL);
  g_assert_nonnull (dependencies_mutable);
  g_assert_cmpstr (dependencies_mutable[0], ==, NULL);
  g_free (messages);
  g_free (tuple);
  g_free (requested_name);
  g_free (compat_soname);
  g_free (real_soname);
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
                              "ld.so: libjpeg.so.62: nope\n",
                              one_missing,
                              one_misversioned,
                              two_deps,
                              NULL,
                              0,
                              0);
  g_assert_cmpint (srt_library_get_issues (library) &
                   SRT_LIBRARY_ISSUES_MISSING_SYMBOLS, !=, 0);
  g_assert_cmpint (srt_library_get_issues (library) &
                   SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS, !=, 0);
  g_assert_cmpstr (srt_library_get_messages (library), ==,
                   "ld.so: libjpeg.so.62: nope\n");
  g_assert_cmpstr (srt_library_get_multiarch_tuple (library), ==,
                   "s390x-linux-gnu");
  g_assert_cmpstr (srt_library_get_requested_name (library), ==,
                   "libjpeg.so.62");
  g_assert_cmpstr (srt_library_get_real_soname (library), ==, NULL);
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
                "messages", &messages,
                "multiarch-tuple", &tuple,
                "requested-name", &requested_name,
                "real-soname", &real_soname,
                "absolute-path", &absolute_path,
                "missing-symbols", &missing_mutable,
                "misversioned-symbols", &misversioned_mutable,
                "dependencies", &dependencies_mutable,
                "issues", &issues,
                NULL);
  g_assert_cmpint (issues & SRT_LIBRARY_ISSUES_MISSING_SYMBOLS, !=, 0);
  g_assert_cmpint (issues & SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS, !=, 0);
  g_assert_cmpstr (messages, ==, "ld.so: libjpeg.so.62: nope\n");
  g_assert_cmpstr (tuple, ==, "s390x-linux-gnu");
  g_assert_cmpstr (requested_name, ==, "libjpeg.so.62");
  g_assert_cmpstr (real_soname, ==, NULL);
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
  g_free (messages);
  g_free (tuple);
  g_free (real_soname);
  g_free (requested_name);
  g_free (absolute_path);
  g_strfreev (missing_mutable);
  g_strfreev (misversioned_mutable);
  g_strfreev (dependencies_mutable);
  g_object_unref (library);
}

typedef struct
{
  const gchar *description;
  SrtLibrarySymbolsFormat format;
  const gchar *library_name;  /* Defaults to `libz.so.1` */
  const gchar *expected_symbols;
  const gchar *missing_symbols[5];
  const gchar *misversioned_symbols[5];
  SrtLibraryIssues issues;
  gboolean is_library_missing;
} LibraryTest;

static const LibraryTest library_test[] =
{
  {
    .description = "presence of `libz.so.1` that should be available in the system",
    .expected_symbols = "inflateCopy@ZLIB_1.2.0\n"
                        "inflateBack@ZLIB_1.2.0\n"
                        "adler32\n"
                        "gzopen@Base",
  },

  {
    .description = "parsing a deb-symbols(5) file",
    .format = SRT_LIBRARY_SYMBOLS_FORMAT_DEB_SYMBOLS,
    .expected_symbols = "# A comment.\n"
                        /* Obviously this library doesn't really exist: it's here to check
                         * that we do the right thing for files that describe more than
                         * one SONAME, like libglib2.0-0:*.symbols. */
                        "libzextra.so.0 libzextra0 #MINVER#\n"
                        " some_fictitious_symbol@Base 1:2.0\n"
                        "libz.so.1 zlib1g #MINVER#\n"
                        "| libz1 #MINVER\n"
                        "* Build-Depends-Package: zlib1g-dev\n"
                        " adler32@Base 1:1.1.4\n"
                        " inflateBack@ZLIB_1.2.0 1:1.2.0\n"
                        /* These symbols don't exist: they're here to check that we don't
                         * miss checking for symbols in the correct section of the file. */
                        " nope@MISSING 1:1.2.0\n"
                        " also_nope@Base 1:1.2.0\n"
                        /* This library doesn't exist either. */
                        "libzmore.so.0 libzmore0 #MINVER#\n"
                        " some_other_fictitious_symbol@Base 1:2.0\n",
    .missing_symbols =
    {
      "nope@MISSING",
      "also_nope",
      NULL
    },
    .issues = SRT_LIBRARY_ISSUES_MISSING_SYMBOLS,
  },

  {
    .description = "presence of empty lines in expected symbols file",
    .expected_symbols = "\n"
                        "inflateCopy@ZLIB_1.2.0\n"
                        "\n"
                        "inflateBack@ZLIB_1.2.0\n"
                        "gzopen@Base"
                        "\n",
  },

  {
    .description = "library with wrong/missing symbols",
    .expected_symbols = "inflateCopy@ZLIB_1.2.0\n"
                        "inflateFooBar@ZLIB_1.2.0\n"
                        "jpeg_mem_src@LIBJPEGTURBO_6.2",
    .missing_symbols =
    {
      "inflateFooBar@ZLIB_1.2.0",
      "jpeg_mem_src@LIBJPEGTURBO_6.2",
      NULL
    },
    .issues = SRT_LIBRARY_ISSUES_MISSING_SYMBOLS,
  },

  {
    .description = "library with misversioned symbols",
    .expected_symbols = "inflateBack@MISSING",
    .misversioned_symbols =
    {
      "inflateBack@MISSING",
      NULL
    },
    .issues = SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS,
  },

  {
    .description = "library with wrong/missing symbols and misversioned symbols",
    .expected_symbols = "inflateCopy@ZLIB_1.2.0\n"
                        "inflateBack@MISSING\n"
                        "inflateFooBar@ZLIB_1.2.0\n"
                        "gzopen@ZLIB_1.2.0",
    .missing_symbols =
    {
      "inflateFooBar@ZLIB_1.2.0",
      NULL
    },
    .misversioned_symbols =
    {
      "inflateBack@MISSING",
      "gzopen@ZLIB_1.2.0",
      NULL
    },
    .issues = SRT_LIBRARY_ISSUES_MISSING_SYMBOLS | SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS,
  },

  {
    .description = "library that is missing",
    .library_name = "libMISSING.so.62",
    .issues = SRT_LIBRARY_ISSUES_CANNOT_LOAD | SRT_LIBRARY_ISSUES_UNKNOWN_EXPECTATIONS,
    .is_library_missing = TRUE,
  },
};

static void
check_list_elements (const gchar * const *list,
                     const gchar * const *expected)
{
  gsize i;

  g_assert_nonnull (list);
  g_assert_nonnull (expected);

  for (i = 0; list[i] != NULL; i++)
    g_assert_cmpstr (list[i], ==, expected[i]);

  g_assert_cmpstr (expected[i], ==, NULL);
}


static void
test_libraries (Fixture *f,
                gconstpointer context)
{
  const char *multiarch_tuple = NULL;
  g_autofree gchar *tmp_file = NULL;
  int fd;
  gsize i;
  gsize j;
  g_autoptr(GError) error = NULL;

#ifndef _SRT_MULTIARCH
  g_test_skip ("Unsupported architecture");
  return;
#else
  multiarch_tuple = _SRT_MULTIARCH;
#endif

  fd = g_file_open_tmp ("library-XXXXXX", &tmp_file, &error);
  g_assert_no_error (error);
  g_assert_cmpint (fd, !=, -1);
  close (fd);

  for (i = 0; i < G_N_ELEMENTS (library_test); i++)
    {
      const LibraryTest *test = &library_test[i];
      const char * const *dependencies;
      gboolean result;
      SrtLibraryIssues issues;
      gboolean seen_libc = FALSE;
      g_autoptr(SrtLibrary) library = NULL;
      const gchar *library_name = test->library_name == NULL ? "libz.so.1" : test->library_name;

      g_test_message ("%s", test->description);

      if (test->expected_symbols != NULL)
        {
          result = g_file_set_contents (tmp_file, test->expected_symbols, -1, &error);
          g_assert_no_error (error);
          g_assert_true (result);
        }

      issues = srt_check_library_presence (library_name,
                                           multiarch_tuple,
                                           test->expected_symbols == NULL ? NULL : tmp_file,
                                           test->format,
                                           &library);
      g_assert_cmpint (issues, ==, test->issues);
      g_assert_cmpint (srt_library_get_terminating_signal (library), ==, 0);

      if (test->is_library_missing)
        {
          g_assert_cmpint (srt_library_get_exit_status (library), ==, 1);
          g_assert_cmpstr (srt_library_get_absolute_path (library), ==, NULL);
        }
      else
        {
          g_assert_cmpint (srt_library_get_exit_status (library), ==, 0);
          g_debug ("path to %s is %s", library_name, srt_library_get_absolute_path (library));
          g_assert_true (srt_library_get_absolute_path (library)[0] == '/');
          g_assert_true (g_file_test (srt_library_get_absolute_path (library), G_FILE_TEST_EXISTS));
        }

      check_list_elements (srt_library_get_missing_symbols (library),
                           test->missing_symbols);
      check_list_elements (srt_library_get_misversioned_symbols (library),
                          test->misversioned_symbols);

      dependencies = srt_library_get_dependencies (library);
      g_assert_nonnull (dependencies);

      if (test->is_library_missing)
        {
          g_assert_cmpstr (dependencies[0], ==, NULL);
        }
      else
        {
          for (j = 0; dependencies[j] != NULL && !seen_libc; j++)
            {
              g_debug ("%s depends on %s", test->library_name, dependencies[j]);

              if (strstr (dependencies[j], "/libc.so.") != NULL)
                seen_libc = TRUE;
            }

          g_assert_true (seen_libc);
        }
    }

  g_unlink (tmp_file);
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
  const char *messages;

  issues = srt_check_library_presence ("libz.so.1",
                                       "hal9000-linux-gnu",
                                       NULL,
                                       SRT_LIBRARY_SYMBOLS_FORMAT_PLAIN,
                                       &library);
  g_assert_cmpint (issues, ==,
                   (SRT_LIBRARY_ISSUES_CANNOT_LOAD |
                    SRT_LIBRARY_ISSUES_UNKNOWN_EXPECTATIONS));
  g_assert_cmpstr (srt_library_get_absolute_path (library), ==, NULL);
  /* Because we can't find the hal9000-linux-gnu-inspect-library helper,
   * we never even get as far as running a command, so no exit status or
   * terminating signal is reported. */
  g_assert_cmpint (srt_library_get_exit_status (library), ==, -1);
  g_assert_cmpint (srt_library_get_terminating_signal (library), ==, 0);
  /* We mention the missing helper in the diagnostic messages. */
  messages = srt_library_get_messages (library);
  g_assert_nonnull (messages);
  g_test_message ("%s", messages);
  g_assert_nonnull (strstr (messages, "hal9000-linux-gnu-inspect-library"));

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
  _srt_tests_init (&argc, &argv, NULL);
  g_test_add ("/library/object", Fixture, NULL,
              setup, test_object, teardown);
  g_test_add ("/library/expectations", Fixture, NULL, setup, test_libraries,
              teardown);
  g_test_add ("/library/missing_arch", Fixture, NULL, setup,
              test_missing_arch, teardown);

  return g_test_run ();
}
