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

#include <steam-runtime-tools/glib-compat.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "test-utils.h"

static const char *argv0;

typedef struct
{
  gchar *srcdir;
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

  f->srcdir = g_strdup (g_getenv ("G_TEST_SRCDIR"));
  f->builddir = g_strdup (g_getenv ("G_TEST_BUILDDIR"));

  if (f->srcdir == NULL)
    f->srcdir = g_path_get_dirname (argv0);

  if (f->builddir == NULL)
    f->builddir = g_path_get_dirname (argv0);
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_free (f->srcdir);
  g_free (f->builddir);
}

/*
 * Test basic functionality of the SrtSystemInfo object.
 */
static void
test_object (Fixture *f,
             gconstpointer context)
{
  SrtSystemInfo *info;
  gchar *expectations_in = NULL;
  gchar *expectations = NULL;
  int fd;

  info = srt_system_info_new (NULL);
  g_assert_nonnull (info);
  g_object_get (info,
                "expectations", &expectations,
                NULL);
  g_assert_cmpstr (expectations, ==, NULL);

  /* We try it twice, to exercise the cached and non-cached cases */
#if defined(__x86_64__) && defined(__LP64__)
  g_assert_true (srt_system_info_can_run (info, SRT_ABI_X86_64));
  g_assert_true (srt_system_info_can_run (info, SRT_ABI_X86_64));
#endif

#if defined(__i386__)
  g_assert_true (srt_system_info_can_run (info, SRT_ABI_I386));
  g_assert_true (srt_system_info_can_run (info, SRT_ABI_I386));
#endif

  g_assert_false (srt_system_info_can_run (info, "hal9000-linux-gnu"));
  g_assert_false (srt_system_info_can_run (info, "hal9000-linux-gnu"));

  /* This is a little bit tautologous - we're using the same check
   * that the production code does. */
  fd = open ("/dev/uinput", O_WRONLY | O_NONBLOCK);

  if (fd >= 0)
    {
      g_assert_true (srt_system_info_can_write_to_uinput (info));
      g_assert_true (srt_system_info_can_write_to_uinput (info));
      close (fd);
    }
  else
    {
      g_assert_false (srt_system_info_can_write_to_uinput (info));
      g_assert_false (srt_system_info_can_write_to_uinput (info));
    }

  g_object_unref (info);

  expectations_in = g_build_filename (f->srcdir, "expectations", NULL);
  info = srt_system_info_new (expectations_in);
  g_assert_nonnull (info);
  g_object_get (info,
                "expectations", &expectations,
                NULL);
  g_assert_cmpstr (expectations, ==, expectations_in);
  g_free (expectations_in);
  g_free (expectations);
  g_object_unref (info);
}

static void
check_libraries_result (GList *libraries)
{
  SrtLibrary *library = NULL;
  const char * const *missing_symbols;
  const char * const *misversioned_symbols;
  const char * const *dependencies;
  gboolean seen_libc;
  GList *l;

  g_assert_nonnull (libraries);
  l = libraries;

  /* Test first library. Alphabetical order is an API guarantee, so we know
   * which one it should be. */
  library = l->data;
  g_assert_nonnull (library);
  g_assert_cmpstr (srt_library_get_soname (library), ==, "libgio-2.0.so.0");
  missing_symbols = srt_library_get_missing_symbols (library);
  g_assert_nonnull (missing_symbols);
  g_assert_cmpstr (missing_symbols[0], ==, NULL);

  g_assert_cmpint (srt_library_get_issues (library), ==, SRT_LIBRARY_ISSUES_NONE);

  misversioned_symbols = srt_library_get_misversioned_symbols (library);
  g_assert_nonnull (misversioned_symbols);
  g_assert_cmpstr (misversioned_symbols[0], ==, NULL);

  dependencies = srt_library_get_dependencies (library);
  g_assert_nonnull (dependencies);
  g_assert_cmpstr (dependencies[0], !=, NULL);
  seen_libc = FALSE;

  for (gsize i = 0; dependencies[i] != NULL; i++)
    {
      g_debug ("libgio-2.0.so.0 depends on %s", dependencies[i]);

      if (strstr (dependencies[i], "/libc.so.") != NULL)
        seen_libc = TRUE;
    }

  g_assert_true (seen_libc);

  /* Test second library */
  l = g_list_next (l);
  library = l->data;
  g_assert_nonnull (library);
  g_assert_cmpstr (srt_library_get_soname (library), ==, "libglib-2.0.so.0");
  missing_symbols = srt_library_get_missing_symbols (library);
  g_assert_nonnull (missing_symbols);
  g_assert_cmpstr (missing_symbols[0], ==, NULL);

  g_assert_cmpint (srt_library_get_issues (library), ==, SRT_LIBRARY_ISSUES_NONE);

  misversioned_symbols = srt_library_get_misversioned_symbols (library);
  g_assert_nonnull (misversioned_symbols);
  g_assert_cmpstr (misversioned_symbols[0], ==, NULL);

  dependencies = srt_library_get_dependencies (library);
  g_assert_nonnull (dependencies);
  g_assert_cmpstr (dependencies[0], !=, NULL);
  seen_libc = FALSE;

  for (gsize i = 0; dependencies[i] != NULL; i++)
    {
      g_debug ("libglib-2.0.so.0 depends on %s", dependencies[i]);

      if (strstr (dependencies[i], "/libc.so.") != NULL)
        seen_libc = TRUE;
    }

  g_assert_true (seen_libc);

  /* Test last library */
  l = g_list_next (l);
  library = l->data;
  g_assert_nonnull (library);
  g_assert_cmpstr (srt_library_get_soname (library), ==, "libz.so.1");
  missing_symbols = srt_library_get_missing_symbols (library);
  g_assert_nonnull (missing_symbols);
  g_assert_cmpstr (missing_symbols[0], ==, NULL);

  g_assert_cmpint (srt_library_get_issues (library), ==, SRT_LIBRARY_ISSUES_NONE);

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
  g_assert_null (g_list_next (l));
}

/*
 * Test if the expected libraries are available in the
 * running system.
 */
static void
libraries_presence (Fixture *f,
                    gconstpointer context)
{
  SrtSystemInfo *info;
  gchar *expectations_in = NULL;
  GList *libraries = NULL;
  SrtLibraryIssues issues;

  if (strcmp (_SRT_MULTIARCH, "") == 0)
    {
      g_test_skip ("Unsupported architecture");
      return;
    }

  expectations_in = g_build_filename (f->srcdir, "expectations", NULL);
  info = srt_system_info_new (expectations_in);
  issues = srt_system_info_check_libraries (info,
                                            _SRT_MULTIARCH,
                                            &libraries);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_NONE);
  check_libraries_result (libraries);
  g_list_free_full (libraries, g_object_unref);
  libraries = NULL;

  /* Do the check again, this time using the cache */
  issues = srt_system_info_check_libraries (info,
                                            _SRT_MULTIARCH,
                                            &libraries);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_NONE);
  check_libraries_result (libraries);

  g_list_free_full (libraries, g_object_unref);
  g_free (expectations_in);
  g_object_unref (info);
}

static void
check_library_result (SrtLibrary *library)
{
  const char * const *missing_symbols;
  const char * const *misversioned_symbols;
  const char * const *dependencies;
  gboolean seen_libc;

  g_assert_cmpstr (srt_library_get_soname (library), ==, "libz.so.1");

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
}

/*
 * Test if `libz.so.1` is available in the running system and
 * if it has the expected symbols.
 */
static void
library_presence (Fixture *f,
                  gconstpointer context)
{
  SrtSystemInfo *info;
  gchar *expectations_in = NULL;
  SrtLibrary *library = NULL;
  SrtLibraryIssues issues;

  if (strcmp (_SRT_MULTIARCH, "") == 0)
    {
      g_test_skip ("Unsupported architecture");
      return;
    }

  expectations_in = g_build_filename (f->srcdir, "expectations", NULL);
  info = srt_system_info_new (expectations_in);

  issues = srt_system_info_check_library (info,
                                          _SRT_MULTIARCH,
                                          "libz.so.1",
                                          &library);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_NONE);

  check_library_result (library);

  g_clear_pointer (&library, g_object_unref);
  /* Do the check again, this time using the cache */
  issues = srt_system_info_check_library (info,
                                          _SRT_MULTIARCH,
                                          "libz.so.1",
                                          &library);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_NONE);

  check_library_result (library);

  g_object_unref (library);
  g_free (expectations_in);
  g_object_unref (info);
}

static void
check_library_libz_missing_sym_result (SrtLibrary *library)
{
  const char * const *missing_symbols;
  const char * const *misversioned_symbols;
  const char * const *dependencies;
  gboolean seen_libc;

  g_assert_nonnull (library);
  g_assert_cmpstr (srt_library_get_soname (library), ==, "libz.so.1");
  g_debug ("path to libz.so.1 is %s", srt_library_get_absolute_path (library));
  g_assert_true (srt_library_get_absolute_path (library)[0] == '/');
  g_assert_true (g_file_test (srt_library_get_absolute_path (library), G_FILE_TEST_EXISTS));

  g_assert_cmpint (srt_library_get_issues (library) & SRT_LIBRARY_ISSUES_MISSING_SYMBOLS, !=, 0);
  g_assert_cmpint (srt_library_get_issues (library) & SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS, !=, 0);

  missing_symbols = srt_library_get_missing_symbols (library);
  g_assert_nonnull (missing_symbols);
  g_assert_cmpstr (missing_symbols[0], ==, "missing@NotAvailable");
  g_assert_cmpstr (missing_symbols[1], ==, NULL);

  misversioned_symbols = srt_library_get_misversioned_symbols (library);
  g_assert_nonnull (misversioned_symbols);
  g_assert_cmpstr (misversioned_symbols[0], ==, "crc32@WRONG_VERSION");
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
}

static void
check_missing_libraries_result (GList *libraries)
{
  SrtLibrary *library = NULL;
  const char * const *missing_symbols;
  const char * const *misversioned_symbols;
  const char * const *dependencies;
  GList *l;

  g_assert_nonnull (libraries);
  l = libraries;

  /* Test first library. Alphabetical order is an API guarantee, so we know
   * which one it should be. */
  library = l->data;
  g_assert_nonnull (library);
  g_assert_cmpstr (srt_library_get_soname (library), ==, "libgio-MISSING-2.0.so.0");
  g_assert_cmpstr (srt_library_get_absolute_path (library), ==, NULL);

  g_assert_cmpint (srt_library_get_issues (library), ==, SRT_LIBRARY_ISSUES_CANNOT_LOAD);

  missing_symbols = srt_library_get_missing_symbols (library);
  g_assert_nonnull (missing_symbols);
  g_assert_cmpstr (missing_symbols[0], ==, NULL);

  misversioned_symbols = srt_library_get_misversioned_symbols (library);
  g_assert_nonnull (misversioned_symbols);
  g_assert_cmpstr (misversioned_symbols[0], ==, NULL);

  dependencies = srt_library_get_dependencies (library);
  g_assert_nonnull (dependencies);
  g_assert_cmpstr (dependencies[0], ==, NULL);

  /* Test second library */
  l = g_list_next (l);
  library = l->data;
  g_assert_nonnull (library);
  g_assert_cmpstr (srt_library_get_soname (library), ==, "libglib-2.0.so.0");
  g_debug ("path to libglib-2.0.so.0 is %s", srt_library_get_absolute_path (library));
  g_assert_true (srt_library_get_absolute_path (library)[0] == '/');
  g_assert_true (g_file_test (srt_library_get_absolute_path (library), G_FILE_TEST_EXISTS));

  g_assert_cmpint (srt_library_get_issues (library), ==, SRT_LIBRARY_ISSUES_NONE);

  missing_symbols = srt_library_get_missing_symbols (library);
  g_assert_nonnull (missing_symbols);
  g_assert_cmpstr (missing_symbols[0], ==, NULL);

  misversioned_symbols = srt_library_get_misversioned_symbols (library);
  g_assert_nonnull (misversioned_symbols);
  g_assert_cmpstr (misversioned_symbols[0], ==, NULL);

  dependencies = srt_library_get_dependencies (library);
  g_assert_nonnull (dependencies);
  g_assert_cmpstr (dependencies[0], !=, NULL);

  /* Test last library */
  l = g_list_next (l);
  library = l->data;
  check_library_libz_missing_sym_result (library);
}

/*
 * Test libraries that are either not available or with missing and
 * misversioned symbols.
 */
static void
libraries_missing (Fixture *f,
                   gconstpointer context)
{
  SrtSystemInfo *info;
  gchar *expectations_in = NULL;
  GList *libraries = NULL;
  SrtLibraryIssues issues;

  if (strcmp (_SRT_MULTIARCH, "") == 0)
    {
      g_test_skip ("Unsupported architecture");
      return;
    }

  expectations_in = g_build_filename (f->srcdir, "expectations_with_missings", NULL);
  info = srt_system_info_new (expectations_in);
  issues = srt_system_info_check_libraries (info,
                                            _SRT_MULTIARCH,
                                            &libraries);
  g_assert_cmpint (issues & SRT_LIBRARY_ISSUES_MISSING_SYMBOLS, !=, 0);
  g_assert_cmpint (issues & SRT_LIBRARY_ISSUES_CANNOT_LOAD, !=, 0);
  g_assert_cmpint (issues & SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS, !=, 0);
  check_missing_libraries_result (libraries);
  g_list_free_full (libraries, g_object_unref);
  libraries = NULL;

  /* Do the check again, this time using the cache */
  issues = srt_system_info_check_libraries (info,
                                            _SRT_MULTIARCH,
                                            &libraries);
  g_assert_cmpint (issues & SRT_LIBRARY_ISSUES_MISSING_SYMBOLS, !=, 0);
  g_assert_cmpint (issues & SRT_LIBRARY_ISSUES_CANNOT_LOAD, !=, 0);
  g_assert_cmpint (issues & SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS, !=, 0);
  check_missing_libraries_result (libraries);

  g_list_free_full (libraries, g_object_unref);
  g_free (expectations_in);
  g_object_unref (info);
}

static void
check_library_missing_lib_result (SrtLibrary *library)
{
  const char * const *missing_symbols;
  const char * const *misversioned_symbols;
  const char * const *dependencies;

  g_assert_nonnull (library);
  g_assert_cmpstr (srt_library_get_soname (library), ==, "libMISSING.so.62");
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
}

/*
 * Test `libz.so.1` expecting missing and misversioned symbols.
 * Then test the missing library `libMISSING.so.62`.
 */
static void
library_missing (Fixture *f,
                 gconstpointer context)
{
  SrtSystemInfo *info;
  gchar *expectations_in = NULL;
  SrtLibrary *library = NULL;
  SrtLibraryIssues issues;

  if (strcmp (_SRT_MULTIARCH, "") == 0)
    {
      g_test_skip ("Unsupported architecture");
      return;
    }

  expectations_in = g_build_filename (f->srcdir, "expectations_with_missings", NULL);
  info = srt_system_info_new (expectations_in);

  /* Check a present library that has a missing symbol */
  issues = srt_system_info_check_library (info,
                                          _SRT_MULTIARCH,
                                          "libz.so.1",
                                          &library);
  g_assert_cmpint (issues & SRT_LIBRARY_ISSUES_MISSING_SYMBOLS, !=, 0);
  g_assert_cmpint (issues & SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS, !=, 0);
  check_library_libz_missing_sym_result (library);

  g_clear_pointer (&library, g_object_unref);
  /* Do the check again, this time using the cache */
  issues = srt_system_info_check_library (info,
                                          _SRT_MULTIARCH,
                                          "libz.so.1",
                                          &library);
  g_assert_cmpint (issues & SRT_LIBRARY_ISSUES_MISSING_SYMBOLS, !=, 0);
  g_assert_cmpint (issues & SRT_LIBRARY_ISSUES_MISVERSIONED_SYMBOLS, !=, 0);
  check_library_libz_missing_sym_result (library);

  g_clear_pointer (&library, g_object_unref);
  /* Check for a library that isn't listed in any of the .symbols files */
  issues = srt_system_info_check_library (info,
                                          _SRT_MULTIARCH,
                                          "libMISSING.so.62",
                                          &library);
  g_assert_cmpint (issues, ==,
                   (SRT_LIBRARY_ISSUES_CANNOT_LOAD |
                    SRT_LIBRARY_ISSUES_UNKNOWN_EXPECTATIONS));
  check_library_missing_lib_result (library);

  g_clear_pointer (&library, g_object_unref);
  /* Do the check again, this time using the cache */
  issues = srt_system_info_check_library (info,
                                          _SRT_MULTIARCH,
                                          "libMISSING.so.62",
                                          &library);
  g_assert_cmpint (issues, ==,
                   (SRT_LIBRARY_ISSUES_CANNOT_LOAD |
                    SRT_LIBRARY_ISSUES_UNKNOWN_EXPECTATIONS));
  check_library_missing_lib_result (library);

  g_object_unref (library);
  g_free (expectations_in);
  g_object_unref (info);
}

/*
 * Test libraries with the expectations folder set to NULL.
 */
static void
wrong_expectations (Fixture *f,
                    gconstpointer context)
{
  SrtSystemInfo *info;
  SrtLibraryIssues issues;

  if (strcmp (_SRT_MULTIARCH, "") == 0)
    {
      g_test_skip ("Unsupported architecture");
      return;
    }

  /* Set the expectations folder to NULL.
   * We expect the library checks to fail. */
  info = srt_system_info_new (NULL);

  issues = srt_system_info_check_libraries (info,
                                            _SRT_MULTIARCH,
                                            NULL);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_UNKNOWN_EXPECTATIONS);

  issues = srt_system_info_check_library (info,
                                          _SRT_MULTIARCH,
                                          "libz.so.1",
                                          NULL);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_UNKNOWN_EXPECTATIONS);

  g_object_unref (info);
}

int
main (int argc,
      char **argv)
{
  argv0 = argv[0];

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/system-info/object", Fixture, NULL,
              setup, test_object, teardown);
  g_test_add ("/system-info/libraries_presence", Fixture, NULL,
              setup, libraries_presence, teardown);
  g_test_add ("/system-info/library_presence", Fixture, NULL,
              setup, library_presence, teardown);
  g_test_add ("/system-info/libraries_missing", Fixture, NULL,
              setup, libraries_missing, teardown);
  g_test_add ("/system-info/library_missing", Fixture, NULL,
              setup, library_missing, teardown);
  g_test_add ("/system-info/wrong_expectations", Fixture, NULL,
              setup, wrong_expectations, teardown);

  return g_test_run ();
}
