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
#include <gio/gio.h>

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <ftw.h>

#include "test-utils.h"
#include "fake-home.h"

static const char *argv0;
static gchar *fake_home_path;

typedef struct
{
  gchar *srcdir;
  gchar *builddir;
  gchar *sysroots;
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

  f->sysroots = g_build_filename (f->builddir, "sysroots", NULL);
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_free (f->srcdir);
  g_free (f->builddir);
  g_free (f->sysroots);

  /* We expect that fake_home already cleaned this up, but just to be sure we
   * do it too */
  _srt_rm_rf (fake_home_path);
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

  info = srt_system_info_new (NULL);
  g_assert_nonnull (info);
  srt_system_info_set_helpers_path (info, f->builddir);
  g_assert_true (srt_system_info_can_run (info, "mock"));
  /* The real helpers are not present here */
  g_assert_false (srt_system_info_can_run (info, SRT_ABI_I386));
  g_assert_false (srt_system_info_can_run (info, SRT_ABI_X86_64));
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
  g_assert_cmpstr (srt_library_get_requested_name (library), ==, "libgio-2.0.so.0");
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
  g_assert_cmpstr (srt_library_get_requested_name (library), ==, "libglib-2.0.so.0");
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

  /* Test third library */
  l = g_list_next (l);
  library = l->data;
  g_assert_nonnull (library);
  g_assert_cmpstr (srt_library_get_requested_name (library), ==, "libtheoraenc.so.1");
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
      g_debug ("libtheoraenc.so.1 depends on %s", dependencies[i]);

      if (strstr (dependencies[i], "/libc.so.") != NULL)
        seen_libc = TRUE;
    }

  g_assert_true (seen_libc);

  /* Test last library */
  l = g_list_next (l);
  library = l->data;
  g_assert_nonnull (library);
  g_assert_cmpstr (srt_library_get_requested_name (library), ==, "libz.so.1");
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

/*
 * Check that the expectations can be auto-detected from the
 * `STEAM_RUNTIME` environment variable.
 */
static void
auto_expectations (Fixture *f,
                   gconstpointer context)
{
  SrtSystemInfo *info;
  gchar *steam_runtime = NULL;
  GList *libraries = NULL;
  SrtLibraryIssues issues;
  gchar **env;

  if (strcmp (_SRT_MULTIARCH, "") == 0)
    {
      g_test_skip ("Unsupported architecture");
      return;
    }

  env = g_get_environ ();
  steam_runtime = g_build_filename (f->srcdir, "fake-steam-runtime", NULL);
  env = g_environ_setenv (env, "STEAM_RUNTIME", steam_runtime, TRUE);

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, env);
  issues = srt_system_info_check_libraries (info,
                                            _SRT_MULTIARCH,
                                            &libraries);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_NONE);
  check_libraries_result (libraries);
  g_list_free_full (libraries, g_object_unref);

  g_object_unref (info);
  g_strfreev (env);
  g_free (steam_runtime);
}

static void
check_library_result (SrtLibrary *library)
{
  const char * const *missing_symbols;
  const char * const *misversioned_symbols;
  const char * const *dependencies;
  gboolean seen_libc;

  g_assert_cmpstr (srt_library_get_requested_name (library), ==, "libz.so.1");

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
  g_assert_cmpstr (srt_library_get_requested_name (library), ==, "libz.so.1");
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
  g_assert_cmpstr (srt_library_get_requested_name (library), ==, "libgio-MISSING-2.0.so.0");
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
  g_assert_cmpstr (srt_library_get_requested_name (library), ==, "libglib-2.0.so.0");
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
  g_assert_cmpstr (srt_library_get_requested_name (library), ==, "libMISSING.so.62");
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

  /* Set the expectations folder to one that does not contain the
   * necessary files. We expect the library checks to fail. */
  info = srt_system_info_new ("/dev");

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

static void
steam_runtime (Fixture *f,
               gconstpointer context)
{
  SrtSystemInfo *info;
  SrtRuntimeIssues runtime_issues;
  SrtSteamIssues steam_issues;
  SrtSteam *steam_details;
  gchar *runtime_path = NULL;
  gchar *installation_path = NULL;
  gchar *bin32_path = NULL;
  FakeHome *fake_home;

  fake_home = fake_home_new (fake_home_path);
  fake_home_create_structure (fake_home);

  info = srt_system_info_new (NULL);
  fake_home_apply_to_system_info (fake_home, info);

  /* Check for runtime issues */
  runtime_issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (runtime_issues, ==, SRT_RUNTIME_ISSUES_NONE);
  runtime_path = srt_system_info_dup_runtime_path (info);
  g_assert_cmpstr (runtime_path, ==, fake_home->runtime);
  installation_path = srt_system_info_dup_steam_installation_path (info);
  g_assert_cmpstr (installation_path, ==, fake_home->steam_install);
  bin32_path = srt_system_info_dup_steam_bin32_path (info);
  g_assert_cmpstr (bin32_path, ==, fake_home->ubuntu12_32);
  g_free (runtime_path);
  g_free (installation_path);
  g_free (bin32_path);

  /* Do the check again, this time using the cache */
  runtime_issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (runtime_issues, ==, SRT_RUNTIME_ISSUES_NONE);
  runtime_path = srt_system_info_dup_runtime_path (info);
  g_assert_cmpstr (runtime_path, ==, fake_home->runtime);
  installation_path = srt_system_info_dup_steam_installation_path (info);
  g_assert_cmpstr (installation_path, ==, fake_home->steam_install);
  bin32_path = srt_system_info_dup_steam_bin32_path (info);
  g_assert_cmpstr (bin32_path, ==, fake_home->ubuntu12_32);

  /* Check for Steam issues */
  steam_issues = srt_system_info_get_steam_issues (info);
  g_assert_cmpint (steam_issues, ==, SRT_STEAM_ISSUES_NONE);
  steam_details = srt_system_info_get_steam_details (info);
  steam_issues = srt_steam_get_issues (steam_details);
  g_assert_cmpint (steam_issues, ==, SRT_STEAM_ISSUES_NONE);
  g_object_unref (steam_details);

  /* Do the check again, this time using the cache */
  steam_issues = srt_system_info_get_steam_issues (info);
  g_assert_cmpint (steam_issues, ==, SRT_STEAM_ISSUES_NONE);
  steam_details = srt_system_info_get_steam_details (info);
  steam_issues = srt_steam_get_issues (steam_details);
  g_assert_cmpint (steam_issues, ==, SRT_STEAM_ISSUES_NONE);

  fake_home_clean_up (fake_home);
  g_object_unref (info);
  g_object_unref (steam_details);
  g_free (runtime_path);
  g_free (installation_path);
  g_free (bin32_path);
}

static void
steam_runtime_missing (Fixture *f,
                       gconstpointer context)
{
  SrtSystemInfo *info;
  SrtRuntimeIssues runtime_issues;
  SrtSteamIssues steam_issues;
  gchar *full_ld_path = NULL;
  FakeHome *fake_home;

  fake_home = fake_home_new (fake_home_path);
  fake_home_create_structure (fake_home);

  full_ld_path = g_strdup (g_environ_getenv (fake_home->env, "LD_LIBRARY_PATH"));

  info = srt_system_info_new (NULL);

  /* Unset LD_LIBRARY_PATH */
  fake_home->env = g_environ_unsetenv (fake_home->env, "LD_LIBRARY_PATH");
  fake_home_apply_to_system_info (fake_home, info);
  runtime_issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (runtime_issues, ==, SRT_RUNTIME_ISSUES_NOT_IN_LD_PATH);

  /* Re set LD_LIBRARY_PATH and remove a required folder from the runtime */
  fake_home->env = g_environ_setenv (fake_home->env, "LD_LIBRARY_PATH", full_ld_path, TRUE);
  fake_home_apply_to_system_info (fake_home, info);
  g_assert_cmpint (g_rmdir (fake_home->amd64_usr_lib_64), ==, 0);
  runtime_issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (runtime_issues & SRT_RUNTIME_ISSUES_NOT_RUNTIME, !=, 0);
  g_assert_cmpint (runtime_issues & SRT_RUNTIME_ISSUES_NOT_IN_LD_PATH, !=, 0);
  steam_issues = srt_system_info_get_steam_issues (info);
  g_assert_cmpint (steam_issues, ==, SRT_STEAM_ISSUES_NONE);

  /* Do the check again, this time using the cache */
  runtime_issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (runtime_issues & SRT_RUNTIME_ISSUES_NOT_RUNTIME, !=, 0);
  g_assert_cmpint (runtime_issues & SRT_RUNTIME_ISSUES_NOT_IN_LD_PATH, !=, 0);
  steam_issues = srt_system_info_get_steam_issues (info);
  g_assert_cmpint (steam_issues, ==, SRT_STEAM_ISSUES_NONE);

  fake_home_clean_up (fake_home);
  g_object_unref (info);
  g_free (full_ld_path);
}

static void
steam_runtime_pinned (Fixture *f,
                      gconstpointer context)
{
  SrtSystemInfo *info;
  SrtRuntimeIssues issues;
  gchar *full_ld_path = NULL;
  gchar *ld_path = NULL;
  FakeHome *fake_home;

  fake_home = fake_home_new (fake_home_path);
  fake_home_create_structure (fake_home);

  full_ld_path = g_strdup (g_environ_getenv (fake_home->env, "LD_LIBRARY_PATH"));

  info = srt_system_info_new (NULL);

  /* Move the pinned libraries at the end of LD_LIBRARY_PATH */
  ld_path = g_strjoin (":",
                       fake_home->i386_lib_i386,
                       fake_home->i386_lib,
                       fake_home->i386_usr_lib_i386,
                       fake_home->i386_usr_lib,
                       fake_home->amd64_lib_64,
                       fake_home->amd64_lib,
                       fake_home->amd64_usr_lib_64,
                       fake_home->amd64_usr_lib,
                       fake_home->pinned_32,
                       fake_home->pinned_64,
                       NULL);
  fake_home->env = g_environ_setenv (fake_home->env, "LD_LIBRARY_PATH", ld_path, TRUE);
  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (SRT_RUNTIME_ISSUES_NOT_USING_NEWER_HOST_LIBRARIES, ==, issues);
  g_free (ld_path);

  /* Remove the pinned library folder */
  g_assert_cmpint (g_rmdir (fake_home->pinned_32), ==, 0);
  g_assert_cmpint (g_rmdir (fake_home->pinned_64), ==, 0);
  fake_home->env = g_environ_setenv (fake_home->env, "LD_LIBRARY_PATH", full_ld_path, TRUE);
  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (SRT_RUNTIME_ISSUES_NOT_USING_NEWER_HOST_LIBRARIES, ==, issues);

  /* Remove pinned libraries from LD_LIBRARY_PATH */
  ld_path = g_strjoin (":",
                       fake_home->i386_lib_i386,
                       fake_home->i386_lib,
                       fake_home->i386_usr_lib_i386,
                       fake_home->i386_usr_lib,
                       fake_home->amd64_lib_64,
                       fake_home->amd64_lib,
                       fake_home->amd64_usr_lib_64,
                       fake_home->amd64_usr_lib,
                       NULL);
  fake_home->env = g_environ_setenv (fake_home->env, "LD_LIBRARY_PATH", ld_path, TRUE);
  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (SRT_RUNTIME_ISSUES_NOT_USING_NEWER_HOST_LIBRARIES, ==, issues);

  fake_home_clean_up (fake_home);
  g_object_unref (info);
  g_free (full_ld_path);
  g_free (ld_path);
}

static void
runtime_disabled_or_missing (Fixture *f,
                             gconstpointer context)
{
  SrtSystemInfo *info;
  SrtRuntimeIssues issues;
  gchar *runtime_path = NULL;
  FakeHome *fake_home;

  fake_home = fake_home_new (fake_home_path);
  fake_home->create_steamrt_files = FALSE;
  fake_home_create_structure (fake_home);

  info = srt_system_info_new (NULL);

  /* Completely disable the runtime */
  fake_home->env = g_environ_setenv (fake_home->env, "STEAM_RUNTIME", "0", TRUE);
  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_DISABLED);
  runtime_path = srt_system_info_dup_runtime_path (info);
  g_assert_cmpstr (runtime_path, ==, NULL);

  /* Set the runtime to a relative position.
   * Test if we can revover using the expected path.
   * We didn't create SteamRT files so expect to receive a "not_runtime"
   * issue. */
  fake_home->env = g_environ_setenv (fake_home->env, "STEAM_RUNTIME", "my/not/absolute/runtime/path", TRUE);
  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (SRT_RUNTIME_ISSUES_NOT_IN_ENVIRONMENT |
                   SRT_RUNTIME_ISSUES_NOT_RUNTIME, ==, issues);

  /* Remove the STEAM_RUNTIME environment. */
  fake_home->env = g_environ_unsetenv (fake_home->env, "STEAM_RUNTIME");
  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (SRT_RUNTIME_ISSUES_NOT_IN_ENVIRONMENT |
                   SRT_RUNTIME_ISSUES_NOT_RUNTIME, ==, issues);

  /* Disable prefer host libraries */
  fake_home->env = g_environ_setenv (fake_home->env, "STEAM_RUNTIME_PREFER_HOST_LIBRARIES", "0", TRUE);
  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (SRT_RUNTIME_ISSUES_NOT_USING_NEWER_HOST_LIBRARIES |
                   SRT_RUNTIME_ISSUES_NOT_IN_ENVIRONMENT |
                   SRT_RUNTIME_ISSUES_NOT_RUNTIME, ==, issues);

  fake_home_clean_up (fake_home);
  g_object_unref (info);
}

static void
runtime_version (Fixture *f,
                 gconstpointer context)
{
  SrtSystemInfo *info;
  SrtRuntimeIssues issues;
  gchar *version = NULL;
  gchar *dup_version = NULL;
  GError *error = NULL;
  FakeHome *fake_home;

  fake_home = fake_home_new (fake_home_path);
  fake_home_create_structure (fake_home);

  version = g_build_filename (fake_home->runtime, "version.txt", NULL);
  info = srt_system_info_new (NULL);

  /* Check version with a trailing new line */
  g_file_set_contents (version, "steam-runtime_0.20190711.3\n", -1, &error);
  g_assert_no_error (error);
  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_NONE);

  /* Check version with an empty number */
  g_file_set_contents (version, "steam-runtime_", -1, &error);
  g_assert_no_error (error);
  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_NOT_RUNTIME);

  /* Check version without underscore */
  g_file_set_contents (version, "steam-runtime", -1, &error);
  g_assert_no_error (error);
  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_NOT_RUNTIME);
  dup_version = srt_system_info_dup_runtime_version (info);
  g_assert_cmpstr (dup_version, ==, NULL);

  /* Check version with a custom prefix */
  g_file_set_contents (version, "custom-steam-runtime_0.20190711.3", -1, &error);
  g_assert_no_error (error);
  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_UNOFFICIAL);

  /* Check version with a custom prefix and multiple underscores */
  g_file_set_contents (version, "custom_steam_runtime_0.20190711.3", -1, &error);
  g_assert_no_error (error);
  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_UNOFFICIAL);
  dup_version = srt_system_info_dup_runtime_version (info);
  g_assert_cmpstr (dup_version, ==, "0.20190711.3");
  g_free (dup_version);

  /* Check an empty version file */
  g_file_set_contents (version, "", -1, &error);
  g_assert_no_error (error);
  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_NOT_RUNTIME);
  dup_version = srt_system_info_dup_runtime_version (info);
  g_assert_cmpstr (dup_version, ==, NULL);

  /* Check expected version */
  g_file_set_contents (version, "steam-runtime_0.20190711.3", -1, &error);
  g_assert_no_error (error);
  srt_system_info_set_expected_runtime_version (info, "0.20190711.3");
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_NONE);

  /* Check expected version with trailing new line */
  g_file_set_contents (version, "steam-runtime_0.20190711.3\n", -1, &error);
  g_assert_no_error (error);
  srt_system_info_set_expected_runtime_version (info, "0.20190711.3");
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_NONE);

  /* Check wrong expected version */
  g_file_set_contents (version, "steam-runtime_0.20190711.3", -1, &error);
  g_assert_no_error (error);
  srt_system_info_set_expected_runtime_version (info, "0.20210813.4");
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_UNEXPECTED_VERSION);

  /* Check wrong expected version */
  g_file_set_contents (version, "steam-runtime_", -1, &error);
  g_assert_no_error (error);
  srt_system_info_set_expected_runtime_version (info, "0.20180101.2");
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (SRT_RUNTIME_ISSUES_NOT_RUNTIME |
                   SRT_RUNTIME_ISSUES_UNEXPECTED_VERSION, ==, issues);
  dup_version = srt_system_info_dup_runtime_version (info);
  g_assert_cmpstr (dup_version, ==, "");

  /* Check expected version with custom prefix */
  g_file_set_contents (version, "my-custom_steam_runtime_0.20190711.3", -1, &error);
  g_assert_no_error (error);
  srt_system_info_set_expected_runtime_version (info, "0.20190711.3");
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_UNOFFICIAL);

  fake_home_clean_up (fake_home);
  g_object_unref (info);
  g_free (version);
  g_free (dup_version);
}

static void
runtime_unexpected_location (Fixture *f,
                             gconstpointer context)
{
  SrtSystemInfo *info;
  SrtRuntimeIssues issues;
  gchar *dot_steam_root = NULL;
  gchar *my_runtime = NULL;
  gchar *ld_path = NULL;
  gchar *env_path = NULL;
  gchar **parts = NULL;
  GFile *symlink_gfile = NULL;
  GError *error = NULL;
  FakeHome *fake_home;

  fake_home = fake_home_new (fake_home_path);
  fake_home->create_root_symlink = FALSE;
  fake_home_create_structure (fake_home);

  info = srt_system_info_new (NULL);
  dot_steam_root = g_build_filename (fake_home->home, ".steam", "root", NULL);

  my_runtime = g_build_filename (fake_home->steam_install, "ubuntu12_32",
                                 "my-runtime", NULL);


  /* Create a new homedir/.steam/steam symlink that doesn't point to
   * the expected steam runtime path. */
  symlink_gfile = g_file_new_for_path (dot_steam_root);
  g_file_make_symbolic_link (symlink_gfile, fake_home->pinned_64, NULL, &error);
  g_assert_no_error (error);
  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_UNEXPECTED_LOCATION);
  g_object_unref (symlink_gfile);

  /* Move the steam-runtime to another location called "my-runtime" and
   * adjust all the environment variables accordingly. */
  ld_path = g_strdup (g_environ_getenv (fake_home->env, "LD_LIBRARY_PATH"));
  parts = g_strsplit (ld_path, "steam-runtime", -1);
  g_free (ld_path);
  ld_path = g_strjoinv ("my-runtime", parts);
  g_strfreev (parts);

  env_path = g_strdup (g_environ_getenv (fake_home->env, "PATH"));
  parts = g_strsplit (env_path, "steam-runtime", -1);
  g_free (env_path);
  env_path = g_strjoinv ("my-runtime", parts);

  g_rename (fake_home->runtime, my_runtime);
  g_remove (dot_steam_root);
  symlink_gfile = g_file_new_for_path (dot_steam_root);
  g_file_make_symbolic_link (symlink_gfile, my_runtime, NULL, &error);
  g_assert_no_error (error);
  fake_home->env = g_environ_setenv (fake_home->env, "LD_LIBRARY_PATH", ld_path, TRUE);
  fake_home->env = g_environ_setenv (fake_home->env, "STEAM_RUNTIME", my_runtime, TRUE);
  fake_home->env = g_environ_setenv (fake_home->env, "PATH", env_path, TRUE);

  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_UNEXPECTED_LOCATION);

  fake_home_clean_up (fake_home);
  g_object_unref (symlink_gfile);
  g_object_unref (info);
  g_free (dot_steam_root);
  g_free (my_runtime);
  g_free (ld_path);
  g_free (env_path);
  g_strfreev (parts);
}

static void
steam_symlink (Fixture *f,
               gconstpointer context)
{
  SrtSystemInfo *info;
  SrtSteamIssues issues;
  gchar *data_home = NULL;
  gchar *dot_steam_steam = NULL;
  gchar *dot_steam_root = NULL;
  gchar *dot_steam_bin32 = NULL;
  gchar *installation_path = NULL;
  gchar *ubuntu12_32 = NULL;
  GFile *symlink_gfile = NULL;
  GError *error = NULL;
  FakeHome *fake_home;

  fake_home = fake_home_new (fake_home_path);
  fake_home->create_steam_symlink = FALSE;
  fake_home_create_structure (fake_home);

  info = srt_system_info_new (NULL);
  dot_steam_steam = g_build_filename (fake_home->home, ".steam", "steam", NULL);
  dot_steam_root = g_build_filename (fake_home->home, ".steam", "root", NULL);
  dot_steam_bin32 = g_build_filename (fake_home->home, ".steam", "bin32", NULL);
  ubuntu12_32 = g_build_filename (fake_home->steam_install, "ubuntu12_32", NULL);

  /* We don't have a homedir/.steam/steam symlink. */
  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_steam_issues (info);
  g_assert_cmpint ((SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_SYMLINK
                   | SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_DIRECTORY), ==, issues);

  /* Remove homedir/.steam/root symlink and create homedir/.steam/bin32 symlink. */
  g_remove (dot_steam_root);
  g_remove (dot_steam_bin32);
  symlink_gfile = g_file_new_for_path (dot_steam_bin32);
  g_file_make_symbolic_link (symlink_gfile, ubuntu12_32, NULL, &error);
  g_assert_no_error (error);
  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_steam_issues (info);
  g_assert_cmpint ((SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_SYMLINK
                   | SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_DIRECTORY
                   | SRT_STEAM_ISSUES_DOT_STEAM_ROOT_NOT_SYMLINK
                   | SRT_STEAM_ISSUES_DOT_STEAM_ROOT_NOT_DIRECTORY), ==, issues);

  /* Remove the homedir/.steam/bin32 symlink and set XDG_DATA_HOME env to a
   * folder that is not the expected homedir/.local/share */
  g_remove (dot_steam_bin32);
  data_home = g_build_filename (fake_home->home, "DataHome", NULL);
  fake_home->env = g_environ_setenv (fake_home->env, "XDG_DATA_HOME", data_home, TRUE);
  fake_home_apply_to_system_info (fake_home, info);
  issues = srt_system_info_get_steam_issues (info);
  g_assert_cmpint ((SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_SYMLINK
                   | SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_DIRECTORY
                   | SRT_STEAM_ISSUES_DOT_STEAM_ROOT_NOT_SYMLINK
                   | SRT_STEAM_ISSUES_DOT_STEAM_ROOT_NOT_DIRECTORY
                   | SRT_STEAM_ISSUES_CANNOT_FIND
                   | SRT_STEAM_ISSUES_CANNOT_FIND_DATA), ==, issues);
  installation_path = srt_system_info_dup_steam_installation_path (info);
  g_assert_cmpstr (installation_path, ==, NULL);

  fake_home_clean_up (fake_home);
  g_object_unref (symlink_gfile);
  g_object_unref (info);
  g_free (data_home);
  g_free (dot_steam_steam);
  g_free (dot_steam_root);
  g_free (dot_steam_bin32);
  g_free (ubuntu12_32);
}

/* Recreate the conditions that triggered the Debian bug 916303.
 * Steam was installed into "~/.steam", which meant that the "steam/"
 * directory inside the Steam installation collided with the "~/.steam/steam"
 * symlink, preventing the symlink from being created. */
static void
debian_bug_916303 (Fixture *f,
                   gconstpointer context)
{
  SrtSystemInfo *info;
  SrtSteamIssues issues;
  FakeHome *fake_home;
  gchar *installation_path;
  gchar *data_path;

  fake_home = fake_home_new (fake_home_path);
  fake_home->has_debian_bug_916303 = TRUE;
  fake_home_create_structure (fake_home);

  info = srt_system_info_new (NULL);
  fake_home_apply_to_system_info (fake_home, info);

  issues = srt_system_info_get_steam_issues (info);
  g_assert_cmpint (issues, ==, SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_SYMLINK);
  installation_path = srt_system_info_dup_steam_installation_path (info);
  g_assert_cmpstr (installation_path, ==, fake_home->steam_install);
  g_assert_true (g_str_has_suffix (installation_path, "/.steam"));
  data_path = srt_system_info_dup_steam_data_path (info);
  g_assert_cmpstr (data_path, ==, fake_home->steam_data);
  g_assert_true (g_str_has_suffix (data_path, "/.steam/steam"));

  fake_home_clean_up (fake_home);
  g_object_unref (info);
  g_free (installation_path);
  g_free (data_path);
}

/* Behave as though we're testing a beta client. */
static void
testing_beta_client (Fixture *f,
                     gconstpointer context)
{
  SrtSystemInfo *info;
  SrtSteamIssues issues;
  FakeHome *fake_home;
  gchar *installation_path;
  gchar *data_path;

  fake_home = fake_home_new (fake_home_path);
  fake_home->testing_beta_client = TRUE;
  fake_home_create_structure (fake_home);

  info = srt_system_info_new (NULL);
  fake_home_apply_to_system_info (fake_home, info);

  issues = srt_system_info_get_steam_issues (info);
  g_assert_cmpint (issues, ==, 0);
  installation_path = srt_system_info_dup_steam_installation_path (info);
  g_assert_cmpstr (installation_path, ==, fake_home->steam_install);
  g_assert_true (g_str_has_suffix (installation_path, "/beta-client"));
  data_path = srt_system_info_dup_steam_data_path (info);
  g_assert_cmpstr (data_path, ==, fake_home->steam_data);
  g_assert_true (g_str_has_suffix (data_path, "/.local/share/Steam"));

  fake_home_clean_up (fake_home);
  g_object_unref (info);
  g_free (installation_path);
  g_free (data_path);
}

static void
os_debian10 (Fixture *f,
             gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **strv;
  gchar *sysroot;
  gchar *s;

  sysroot = g_build_filename (f->sysroots, "debian10", NULL);

  info = srt_system_info_new (NULL);
  srt_system_info_set_sysroot (info, sysroot);

  s = srt_system_info_dup_os_build_id (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  s = srt_system_info_dup_os_id (info);
  g_assert_cmpstr (s, ==, "debian");
  g_free (s);

  strv = srt_system_info_dup_os_id_like (info, FALSE);
  g_assert_null (strv);
  g_strfreev (strv);

  strv = srt_system_info_dup_os_id_like (info, TRUE);
  g_assert_nonnull (strv);
  g_assert_cmpstr (strv[0], ==, "debian");
  g_assert_cmpstr (strv[1], ==, NULL);
  g_strfreev (strv);

  s = srt_system_info_dup_os_name (info);
  g_assert_cmpstr (s, ==, "Debian GNU/Linux");
  g_free (s);

  s = srt_system_info_dup_os_pretty_name (info);
  g_assert_cmpstr (s, ==, "Debian GNU/Linux 10 (buster)");
  g_free (s);

  s = srt_system_info_dup_os_variant (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  s = srt_system_info_dup_os_variant_id (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  s = srt_system_info_dup_os_version_codename (info);
  g_assert_cmpstr (s, ==, "buster");
  g_free (s);

  s = srt_system_info_dup_os_version_id (info);
  g_assert_cmpstr (s, ==, "10");
  g_free (s);

  g_object_unref (info);
  g_free (sysroot);
}

static void
os_debian_unstable (Fixture *f,
                    gconstpointer context)
{
  const char *sysroot_name = context;
  SrtSystemInfo *info;
  gchar **strv;
  gchar *sysroot;
  gchar *s;

  sysroot = g_build_filename (f->sysroots, sysroot_name, NULL);

  info = srt_system_info_new (NULL);
  srt_system_info_set_sysroot (info, sysroot);

  s = srt_system_info_dup_os_build_id (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  s = srt_system_info_dup_os_id (info);
  g_assert_cmpstr (s, ==, "debian");
  g_free (s);

  strv = srt_system_info_dup_os_id_like (info, FALSE);
  g_assert_null (strv);
  g_strfreev (strv);

  strv = srt_system_info_dup_os_id_like (info, TRUE);
  g_assert_nonnull (strv);
  g_assert_cmpstr (strv[0], ==, "debian");
  g_assert_cmpstr (strv[1], ==, NULL);
  g_strfreev (strv);

  s = srt_system_info_dup_os_name (info);
  g_assert_cmpstr (s, ==, "Debian GNU/Linux");
  g_free (s);

  s = srt_system_info_dup_os_pretty_name (info);
  g_assert_cmpstr (s, ==, "Debian GNU/Linux bullseye/sid");
  g_free (s);

  s = srt_system_info_dup_os_variant (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  s = srt_system_info_dup_os_variant_id (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  s = srt_system_info_dup_os_version_codename (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  s = srt_system_info_dup_os_version_id (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  g_object_unref (info);
  g_free (sysroot);
}

static void
os_steamrt (Fixture *f,
            gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **strv;
  gchar *sysroot;
  gchar *s;
  SrtRuntimeIssues runtime_issues;

  sysroot = g_build_filename (f->sysroots, "steamrt", NULL);

  info = srt_system_info_new (NULL);
  srt_system_info_set_sysroot (info, sysroot);

  s = srt_system_info_dup_os_build_id (info);
  g_assert_cmpstr (s, ==, "0.20190924.0");
  g_free (s);

  s = srt_system_info_dup_os_id (info);
  g_assert_cmpstr (s, ==, "steamrt");
  g_free (s);

  strv = srt_system_info_dup_os_id_like (info, FALSE);
  g_assert_nonnull (strv);
  g_assert_cmpstr (strv[0], ==, "ubuntu");
  g_assert_cmpstr (strv[1], ==, "debian");
  g_assert_cmpstr (strv[2], ==, NULL);
  g_strfreev (strv);

  strv = srt_system_info_dup_os_id_like (info, TRUE);
  g_assert_nonnull (strv);
  g_assert_cmpstr (strv[0], ==, "steamrt");
  g_assert_cmpstr (strv[1], ==, "ubuntu");
  g_assert_cmpstr (strv[2], ==, "debian");
  g_assert_cmpstr (strv[3], ==, NULL);
  g_strfreev (strv);

  s = srt_system_info_dup_os_name (info);
  g_assert_cmpstr (s, ==, "Steam Runtime");
  g_free (s);

  s = srt_system_info_dup_os_pretty_name (info);
  g_assert_cmpstr (s, ==, "Steam Runtime 1 (scout)");
  g_free (s);

  s = srt_system_info_dup_os_variant (info);
  g_assert_cmpstr (s, ==, "Platform");
  g_free (s);

  s = srt_system_info_dup_os_variant_id (info);
  g_assert_cmpstr (s, ==, "com.valvesoftware.steamruntime.platform-amd64_i386-scout");
  g_free (s);

  s = srt_system_info_dup_os_version_codename (info);
  /* It isn't in os-release(5), but we infer it from the ID and VERSION_ID */
  g_assert_cmpstr (s, ==, "scout");
  g_free (s);

  s = srt_system_info_dup_os_version_id (info);
  g_assert_cmpstr (s, ==, "1");
  g_free (s);

  runtime_issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (runtime_issues, ==, SRT_RUNTIME_ISSUES_NONE);

  s = srt_system_info_dup_runtime_path (info);
  g_assert_cmpstr (s, ==, "/");
  g_free (s);

  s = srt_system_info_dup_runtime_version (info);
  g_assert_cmpstr (s, ==, "0.20190924.0");
  g_free (s);

  g_object_unref (info);
  g_free (sysroot);
}

static void
os_steamrt_unofficial (Fixture *f,
                       gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **strv;
  gchar *sysroot;
  gchar *s;
  SrtRuntimeIssues runtime_issues;

  sysroot = g_build_filename (f->sysroots, "steamrt-unofficial", NULL);

  info = srt_system_info_new (NULL);
  srt_system_info_set_sysroot (info, sysroot);
  srt_system_info_set_expected_runtime_version (info, "0.20190711.3");

  s = srt_system_info_dup_os_build_id (info);
  g_assert_cmpstr (s, ==, "unofficial-0.20190924.0");
  g_free (s);

  s = srt_system_info_dup_os_id (info);
  g_assert_cmpstr (s, ==, "steamrt");
  g_free (s);

  strv = srt_system_info_dup_os_id_like (info, FALSE);
  g_assert_nonnull (strv);
  g_assert_cmpstr (strv[0], ==, "ubuntu");
  g_assert_cmpstr (strv[1], ==, "debian");
  g_assert_cmpstr (strv[2], ==, NULL);
  g_strfreev (strv);

  strv = srt_system_info_dup_os_id_like (info, TRUE);
  g_assert_nonnull (strv);
  g_assert_cmpstr (strv[0], ==, "steamrt");
  g_assert_cmpstr (strv[1], ==, "ubuntu");
  g_assert_cmpstr (strv[2], ==, "debian");
  g_assert_cmpstr (strv[3], ==, NULL);
  g_strfreev (strv);

  s = srt_system_info_dup_os_name (info);
  g_assert_cmpstr (s, ==, "Steam Runtime");
  g_free (s);

  s = srt_system_info_dup_os_pretty_name (info);
  g_assert_cmpstr (s, ==, "Steam Runtime 1 (scout)");
  g_free (s);

  s = srt_system_info_dup_os_variant (info);
  g_assert_cmpstr (s, ==, "Platform");
  g_free (s);

  s = srt_system_info_dup_os_variant_id (info);
  g_assert_cmpstr (s, ==, "com.valvesoftware.steamruntime.platform-amd64_i386-scout");
  g_free (s);

  s = srt_system_info_dup_os_version_codename (info);
  /* It isn't in os-release(5), but we infer it from the ID and VERSION_ID */
  g_assert_cmpstr (s, ==, "scout");
  g_free (s);

  s = srt_system_info_dup_os_version_id (info);
  g_assert_cmpstr (s, ==, "1");
  g_free (s);

  runtime_issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (runtime_issues, ==,
                   (SRT_RUNTIME_ISSUES_UNOFFICIAL
                    | SRT_RUNTIME_ISSUES_UNEXPECTED_VERSION));

  s = srt_system_info_dup_runtime_path (info);
  g_assert_cmpstr (s, ==, "/");
  g_free (s);

  s = srt_system_info_dup_runtime_version (info);
  g_assert_cmpstr (s, ==, "unofficial-0.20190924.0");
  g_free (s);

  g_object_unref (info);
  g_free (sysroot);
}

static void
os_invalid_os_release (Fixture *f,
                       gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **strv;
  gchar *sysroot;
  gchar *s;
  SrtRuntimeIssues runtime_issues;

  sysroot = g_build_filename (f->sysroots, "invalid-os-release", NULL);

  info = srt_system_info_new (NULL);
  srt_system_info_set_sysroot (info, sysroot);
  srt_system_info_set_expected_runtime_version (info, "0.20190711.3");

  s = srt_system_info_dup_os_build_id (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  s = srt_system_info_dup_os_id (info);
  g_assert_cmpstr (s, ==, "steamrt");
  g_free (s);

  strv = srt_system_info_dup_os_id_like (info, FALSE);
  g_assert_null (strv);
  g_strfreev (strv);

  strv = srt_system_info_dup_os_id_like (info, TRUE);
  g_assert_nonnull (strv);
  g_assert_cmpstr (strv[0], ==, "steamrt");
  g_assert_cmpstr (strv[1], ==, NULL);
  g_strfreev (strv);

  s = srt_system_info_dup_os_name (info);
  g_assert_cmpstr (s, ==, "This file does not end with a newline");
  g_free (s);

  s = srt_system_info_dup_os_pretty_name (info);
  g_assert_cmpstr (s, ==, "The second name");
  g_free (s);

  s = srt_system_info_dup_os_variant (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  s = srt_system_info_dup_os_variant_id (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  s = srt_system_info_dup_os_version_codename (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  s = srt_system_info_dup_os_version_id (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  runtime_issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (runtime_issues, ==,
                   (SRT_RUNTIME_ISSUES_UNEXPECTED_VERSION
                    | SRT_RUNTIME_ISSUES_NOT_RUNTIME));

  s = srt_system_info_dup_runtime_path (info);
  g_assert_cmpstr (s, ==, "/");
  g_free (s);

  s = srt_system_info_dup_runtime_version (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  g_object_unref (info);
  g_free (sysroot);
}

static void
os_no_os_release (Fixture *f,
                  gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **strv;
  gchar *sysroot;
  gchar *s;

  sysroot = g_build_filename (f->sysroots, "no-os-release", NULL);

  info = srt_system_info_new (NULL);
  srt_system_info_set_sysroot (info, sysroot);

  s = srt_system_info_dup_os_build_id (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  s = srt_system_info_dup_os_id (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  strv = srt_system_info_dup_os_id_like (info, FALSE);
  g_assert_null (strv);
  g_strfreev (strv);

  strv = srt_system_info_dup_os_id_like (info, TRUE);
  g_assert_null (strv);
  g_strfreev (strv);

  s = srt_system_info_dup_os_name (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  s = srt_system_info_dup_os_pretty_name (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  s = srt_system_info_dup_os_variant (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  s = srt_system_info_dup_os_variant_id (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  s = srt_system_info_dup_os_version_codename (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  s = srt_system_info_dup_os_version_id (info);
  g_assert_cmpstr (s, ==, NULL);
  g_free (s);

  g_object_unref (info);
  g_free (sysroot);
}

static void
overrides (Fixture *f,
           gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **output;
  gchar **issues;
  gchar *sysroot;
  gchar *s;
  gsize i;
  gboolean seen_link;

  sysroot = g_build_filename (f->sysroots, "steamrt", NULL);

  info = srt_system_info_new (NULL);
  srt_system_info_set_sysroot (info, sysroot);

  s = srt_system_info_dup_os_id (info);
  g_assert_cmpstr (s, ==, "steamrt");
  g_free (s);

  output = srt_system_info_list_pressure_vessel_overrides (info, &issues);

  /* In the steamrt test overrides folder we expect to have a symbolic
   * link to "/run/host/usr/lib/libgcc_s.so.1" */
  seen_link = FALSE;
  /* The output is not guaranteed to be ordered */
  g_debug ("overrides content:");
  for (i = 0; output[i] != NULL; i++)
    {
      g_debug ("%s", output[i]);

      if (strstr (output[i], "/run/host/usr/lib/libgcc_s.so.1") != NULL)
        seen_link = TRUE;
    }
  /* The overrides folder contains 4 folders plus the root folder, plus 4 files,
   * plus one ".keep" file */
  g_assert_cmpint (i, ==, 10);
  g_assert_true (seen_link);
  g_strfreev (output);

  g_assert_nonnull (issues);
  g_assert_cmpstr (issues[0], ==, NULL);
  g_strfreev (issues);

  /* Repeat the same check, this time using the cached result */
  output = srt_system_info_list_pressure_vessel_overrides (info, &issues);

  seen_link = FALSE;
  for (i = 0; output[i] != NULL; i++)
    {
      if (strstr (output[i], "/run/host/usr/lib/libgcc_s.so.1") != NULL)
        seen_link = TRUE;
    }
  g_assert_cmpint (i, ==, 10);
  g_assert_true (seen_link);
  g_strfreev (output);

  g_assert_nonnull (issues);
  g_assert_cmpstr (issues[0], ==, NULL);
  g_strfreev (issues);

  g_object_unref (info);
  g_free (sysroot);
}

static void
overrides_issues (Fixture *f,
                  gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **output;
  gchar **issues;
  gchar *sysroot;
  gchar *lib_folder;
  gsize i;
  gboolean seen_link;

  sysroot = g_build_filename (f->sysroots, "steamrt-overrides-issues", NULL);

  info = srt_system_info_new (NULL);
  srt_system_info_set_sysroot (info, sysroot);

  lib_folder = g_build_filename (sysroot, "overrides", "lib", NULL);

  /* Remove the read permission for the "lib" folder */
  g_assert_cmpint (g_chmod (lib_folder, 0200), ==, 0);

  if (g_access (lib_folder, R_OK) == 0)
    {
      g_test_skip ("This test can't be executed with elevated privileges");
      goto out;
    }

  output = srt_system_info_list_pressure_vessel_overrides (info, &issues);

  /* In the steamrt test overrides folder we expect to have a symbolic
   * link to "/run/host/usr/lib/libgcc_s.so.1" */
  seen_link = FALSE;
  /* The output is not guaranteed to be ordered */
  g_debug ("overrides content:");
  for (i = 0; output[i] != NULL; i++)
    {
      g_debug ("%s", output[i]);

      if (strstr (output[i], "/run/host/usr/lib/libgcc_s.so.1") != NULL)
        seen_link = TRUE;
    }
  /* The overrides folder contains 4 folders plus the root folder, plus one symlink,
   * plus 2 ".keep" files.
   * We expect to not be able to open the "lib" folder, so we should have 4 less items than
   * a "normal" scenario */
  g_assert_cmpint (i, ==, 4);
  /* We expect not to be able to reach the symlink */
  g_assert_false (seen_link);
  g_strfreev (output);

  g_assert_nonnull (issues);
  g_assert_cmpstr (strstr (issues[0], "overrides/lib"), !=, NULL);
  g_strfreev (issues);

  out:
    /* Re set the permissions for "lib" to the default 755 */
    g_chmod (lib_folder, 0755);
    g_object_unref (info);
    g_free (sysroot);
    g_free (lib_folder);
}

static void
overrides_not_available (Fixture *f,
                         gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **output;
  gchar **issues;
  gchar *sysroot;

  sysroot = g_build_filename (f->sysroots, "debian10", NULL);

  info = srt_system_info_new (NULL);
  srt_system_info_set_sysroot (info, sysroot);

  output = srt_system_info_list_pressure_vessel_overrides (info, &issues);

  g_assert_null (output);

  g_assert_null (issues);

  g_object_unref (info);
  g_free (sysroot);
}

static void
pinned_libraries (Fixture *f,
                  gconstpointer context)
{
  SrtSystemInfo *info;
  FakeHome *fake_home;
  gchar *target1 = NULL;
  gchar *target2 = NULL;
  gchar *start = NULL;
  gchar *has_pins = NULL;
  gchar **values = NULL;
  gchar **messages = NULL;
  GFile *symlink_gfile = NULL;
  gboolean seen_pins;
  gsize i;
  GError *error = NULL;

  fake_home = fake_home_new (fake_home_path);
  fake_home_create_structure (fake_home);

  info = srt_system_info_new (NULL);
  fake_home_apply_to_system_info (fake_home, info);

  start = g_build_filename (fake_home->pinned_32, "libcurl.so.3", NULL);
  target1 = g_build_filename (fake_home->pinned_32, "libcurl.so.4", NULL);
  symlink_gfile = g_file_new_for_path (start);

  g_file_make_symbolic_link (symlink_gfile, target1, NULL, &error);
  g_object_unref (symlink_gfile);
  g_assert_no_error (error);

  target2 = g_build_filename (fake_home->i386_usr_lib_i386, "libcurl.so.4.2.0", NULL);
  g_assert_cmpint (g_creat (target2, 0755), >, -1);
  symlink_gfile = g_file_new_for_path (target1);

  g_file_make_symbolic_link (symlink_gfile, target2, NULL, &error);
  g_object_unref (symlink_gfile);
  g_assert_no_error (error);

  has_pins = g_build_filename (fake_home->pinned_32, "has_pins", NULL);
  g_assert_cmpint (g_creat (has_pins, 0755), >, -1);

  values = srt_system_info_list_pinned_libs_32 (info, &messages);
  g_assert_nonnull (values);
  seen_pins = FALSE;
  /* The output is not guaranteed to be ordered */
  g_debug ("pinned_libs_32 content:");
  for (i = 0; values[i] != NULL; i++)
    {
      g_debug ("%s", values[i]);

      if (strstr (values[i], "has_pins") != NULL)
        seen_pins = TRUE;
    }
  /* We placed 3 files in `pinned_libs_32`. We expect to have 3 files plus the folder
   * itself */
  g_assert_cmpint (i, ==, 4);
  g_assert_true (seen_pins);
  g_strfreev (values);

  g_assert_nonnull (messages);
  g_assert_cmpstr (messages[0], ==, NULL);
  g_strfreev (messages);

  /* Repeat the same check, this time using the cached values */
  values = srt_system_info_list_pinned_libs_32 (info, &messages);
  g_assert_nonnull (values);
  seen_pins = FALSE;
  for (i = 0; values[i] != NULL; i++)
    {
      if (strstr (values[i], "has_pins") != NULL)
        seen_pins = TRUE;
    }
  g_assert_cmpint (i, ==, 4);
  g_assert_true (seen_pins);
  g_strfreev (values);

  g_assert_nonnull (messages);
  g_assert_cmpstr (messages[0], ==, NULL);
  g_strfreev (messages);

  g_free (target1);
  g_free (target2);
  g_free (start);
  g_free (has_pins);

  /* Check pinned_libs_64.
   * Set again the environ to flush the cached values */
  fake_home_apply_to_system_info (fake_home, info);
  start = g_build_filename (fake_home->pinned_64, "libcurl.so.3", NULL);
  target1 = g_build_filename (fake_home->pinned_64, "libcurl.so.4", NULL);
  symlink_gfile = g_file_new_for_path (start);

  g_file_make_symbolic_link (symlink_gfile, target1, NULL, &error);
  g_object_unref (symlink_gfile);
  g_assert_no_error (error);

  target2 = g_build_filename (fake_home->amd64_usr_lib_64, "libcurl.so.4.2.0", NULL);
  g_assert_cmpint (g_creat (target2, 0755), >, -1);
  symlink_gfile = g_file_new_for_path (target1);

  g_file_make_symbolic_link (symlink_gfile, target2, NULL, &error);
  g_object_unref (symlink_gfile);
  g_assert_no_error (error);

  has_pins = g_build_filename (fake_home->pinned_64, "has_pins", NULL);
  g_assert_cmpint (g_creat (has_pins, 0755), >, -1);

  values = srt_system_info_list_pinned_libs_64 (info, &messages);
  g_assert_nonnull (values);
  seen_pins = FALSE;
  g_debug ("pinned_libs_64 content:");
  for (i = 0; values[i] != NULL; i++)
    {
      g_debug ("%s", values[i]);

      if (strstr (values[i], "has_pins") != NULL)
        seen_pins = TRUE;
    }
  g_assert_cmpint (i, ==, 4);
  g_assert_true (seen_pins);
  g_strfreev (values);

  g_assert_nonnull (messages);
  g_assert_cmpstr (messages[0], ==, NULL);
  g_strfreev (messages);

  /* Repeat the same check, this time using the cached values */
  values = srt_system_info_list_pinned_libs_64 (info, &messages);
  g_assert_nonnull (values);
  seen_pins = FALSE;
  for (i = 0; values[i] != NULL; i++)
    {
      if (strstr (values[i], "has_pins") != NULL)
        seen_pins = TRUE;
    }
  g_assert_cmpint (i, ==, 4);
  g_assert_true (seen_pins);
  g_strfreev (values);

  g_assert_nonnull (messages);
  g_assert_cmpstr (messages[0], ==, NULL);
  g_strfreev (messages);

  g_free (target1);
  g_free (target2);
  g_free (start);
  g_free (has_pins);

  fake_home_clean_up (fake_home);
  g_object_unref (info);
}

static void
pinned_libraries_permission (Fixture *f,
                             gconstpointer context)
{
  SrtSystemInfo *info;
  FakeHome *fake_home;
  gchar *no_access = NULL;
  gchar **values = NULL;
  gchar **messages = NULL;
  gboolean seen_no_access;
  gsize i;

  fake_home = fake_home_new (fake_home_path);
  fake_home_create_structure (fake_home);

  info = srt_system_info_new (NULL);
  fake_home_apply_to_system_info (fake_home, info);

  no_access = g_build_filename (fake_home->pinned_32, "no_access", NULL);
  /* Creates a folder without read permissions */
  g_assert_cmpint (g_mkdir_with_parents (no_access, 0200), ==, 0);

  if (g_access (no_access, R_OK) == 0)
    {
      g_test_skip ("This test can't be executed with elevated privileges");
      goto out;
    }

  values = srt_system_info_list_pinned_libs_32 (info, &messages);
  g_assert_nonnull (values);
  seen_no_access = FALSE;
  /* The output is not guaranteed to be ordered */
  g_debug ("pinned_libs_32 content:");
  for (i = 0; values[i] != NULL; i++)
    {
      g_debug ("%s", values[i]);

      if (strstr (values[i], "no_access") != NULL)
        seen_no_access = TRUE;
    }
  /* We placed 1 folder in `pinned_libs_32`. We expect to have 1 folder plus the
   * parent folder itself */
  g_assert_cmpint (i, ==, 2);
  g_assert_true (seen_no_access);
  g_strfreev (values);

  g_assert_nonnull (messages);
  g_assert_cmpstr (strstr (messages[0], "no_access"), !=, NULL);
  g_strfreev (messages);

  g_free (no_access);

  /* Check pinned_libs_64.
   * Set again the environ to flush the cached values */
  fake_home_apply_to_system_info (fake_home, info);

  no_access = g_build_filename (fake_home->pinned_64, "no_access", NULL);
  g_assert_cmpint (g_mkdir_with_parents (no_access, 0311), ==, 0);

  values = srt_system_info_list_pinned_libs_64 (info, &messages);
  g_assert_nonnull (values);
  seen_no_access = FALSE;
  /* The output is not guaranteed to be ordered */
  g_debug ("pinned_libs_64 content:");
  for (i = 0; values[i] != NULL; i++)
    {
      g_debug ("%s", values[i]);

      if (strstr (values[i], "no_access") != NULL)
        seen_no_access = TRUE;
    }
  /* We placed 1 folder in `pinned_libs_32`. We expect to have 1 folder plus the
   * parent folder itself */
  g_assert_cmpint (i, ==, 2);
  g_assert_true (seen_no_access);
  g_strfreev (values);

  g_assert_nonnull (messages);
  g_assert_cmpstr (strstr (messages[0], "no_access"), !=, NULL);
  g_strfreev (messages);

  out:
    g_free (no_access);
    fake_home_clean_up (fake_home);
    g_object_unref (info);
}

static void
pinned_libraries_missing (Fixture *f,
                          gconstpointer context)
{
  SrtSystemInfo *info;
  FakeHome *fake_home;
  gchar **values = NULL;
  gchar **messages = NULL;

  fake_home = fake_home_new (fake_home_path);
  fake_home_create_structure (fake_home);

  info = srt_system_info_new (NULL);
  fake_home_apply_to_system_info (fake_home, info);

  g_assert_cmpint (g_rmdir (fake_home->pinned_32), ==, 0);

  values = srt_system_info_list_pinned_libs_32 (info, &messages);
  g_assert_nonnull (values);
  g_assert_cmpstr (values[0], ==, NULL);
  g_strfreev (values);

  g_assert_nonnull (messages);
  g_assert_cmpstr (strstr (messages[0], "pinned_libs_32"), !=, NULL);
  g_strfreev (messages);

  /* Check pinned_libs_64.
   * Set again the environ to flush the cached values */
  fake_home_apply_to_system_info (fake_home, info);

  g_assert_cmpint (g_rmdir (fake_home->pinned_64), ==, 0);

  values = srt_system_info_list_pinned_libs_64 (info, &messages);
  g_assert_nonnull (values);
  g_assert_cmpstr (values[0], ==, NULL);
  g_strfreev (values);

  g_assert_nonnull (messages);
  g_assert_cmpstr (strstr (messages[0], "pinned_libs_64"), !=, NULL);
  g_strfreev (messages);

  fake_home_clean_up (fake_home);
  g_object_unref (info);
}

static void
driver_environment (Fixture *f,
                    gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **envp;
  gchar **output;
  gsize i;
  const gchar *environment[][2] = { {"LIBVA_DRIVER_NAME", "radeonsi"},
                                    {"MESA_LOADER_DRIVER_OVERRIDE", "i965"},
                                    {"VDPAU_DRIVER", "secret_2"},
                                    {"__GLX_FORCE_VENDOR_LIBRARY_0", "driver_display_zero"},
                                    {"__GLX_FORCE_VENDOR_LIBRARY_12", "display_twelve"},
                                    {"__GLX_VENDOR_LIBRARY_NAME", "my_custom_driver"},
                                    {NULL, NULL} };

  envp = g_get_environ ();

  for (i = 0; environment[i][0] != NULL; i++)
    envp = g_environ_setenv (envp, environment[i][0], environment[i][1], TRUE);

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, envp);

  output = srt_system_info_list_driver_environment (info);
  g_assert_nonnull (output);
  g_assert_nonnull (output[0]);
  for (i = 0; output[i] != NULL; i++)
    {
      gchar *key_value = g_strjoin ("=", environment[i][0], environment[i][1], NULL);
      g_assert_cmpstr (key_value, ==, output[i]);
      g_free (key_value);
    }
  /* plus one for the last NULL item */
  g_assert_cmpint (G_N_ELEMENTS (environment), ==, i+1);
  g_strfreev (output);

  /* Do it again using the cached values */
  output = srt_system_info_list_driver_environment (info);
  g_assert_nonnull (output);
  g_assert_nonnull (output[0]);
  for (i = 0; output[i] != NULL; i++)
    {
      gchar *key_value = g_strjoin ("=", environment[i][0], environment[i][1], NULL);
      g_assert_cmpstr (key_value, ==, output[i]);
      g_free (key_value);
    }
  /* plus one for the last NULL item */
  g_assert_cmpint (G_N_ELEMENTS (environment), ==, i+1);
  g_strfreev (output);

  /* Test when no custom graphics environment variables are available */
  for (i = 0; environment[i][0] != NULL; i++)
    envp = g_environ_unsetenv (envp, environment[i][0]);

  srt_system_info_set_environ (info, envp);
  output = srt_system_info_list_driver_environment (info);
  g_assert_null (output);

  /* Test that variations from the canonical __GLX_FORCE_VENDOR_LIBRARY_[0-9]+
   * are not picked up */
  envp = g_environ_setenv (envp, "__GLX_FORCE_VENDOR_LIBRARY_0_EXTRA", "test", TRUE);
  envp = g_environ_setenv (envp, "__GLX_FORCE_VENDOR_LIBRARY", "test", TRUE);
  envp = g_environ_setenv (envp, "A__GLX_FORCE_VENDOR_LIBRARY_0", "test", TRUE);
  envp = g_environ_setenv (envp, "__GLX_FORCE_VENDOR_LIBRARY_", "test", TRUE);
  envp = g_environ_setenv (envp, "__GLX_FORCE_VENDOR_LIBRARY0", "test", TRUE);

  srt_system_info_set_environ (info, envp);
  output = srt_system_info_list_driver_environment (info);
  /* We expect an empty list because the environment variables are not following the
   * expected pattern */
  g_assert_null (output);

  g_object_unref (info);
  g_strfreev (envp);
}

typedef struct
{
  const char *description;
  const char *sysroot;
  SrtContainerType type;
  const char *host_directory;
} ContainerTest;

static const ContainerTest container_tests[] =
{
  { "Has /.dockerenv",
    "debian-unstable", SRT_CONTAINER_TYPE_DOCKER, NULL },
  { "Has an unknown value in /run/systemd/container",
    "debian10", SRT_CONTAINER_TYPE_UNKNOWN, NULL },
  { "Has 'docker' in /run/systemd/container",
    "fedora", SRT_CONTAINER_TYPE_DOCKER, NULL },
  { "Has /.flatpak-info",
    "flatpak-example", SRT_CONTAINER_TYPE_FLATPAK, "/run/host" },
  { "Has /run/host",
    "invalid-os-release", SRT_CONTAINER_TYPE_UNKNOWN, "/run/host" },
  { "Has no evidence of being a container",
    "no-os-release", SRT_CONTAINER_TYPE_NONE, NULL },
  { "Has /run/pressure-vessel",
    "steamrt", SRT_CONTAINER_TYPE_PRESSURE_VESSEL, "/run/host" },
  { "Has a Docker-looking /proc/1/cgroup",
    "steamrt-unofficial", SRT_CONTAINER_TYPE_DOCKER, NULL },
};

static void
test_containers (Fixture *f,
                 gconstpointer context)
{
  gsize i, j;

  for (i = 0; i < G_N_ELEMENTS (container_tests); i++)
    {
      const ContainerTest *test = &container_tests[i];
      SrtSystemInfo *info;
      gchar *sysroot, *got, *expected;

      g_test_message ("%s: %s", test->sysroot, test->description);

      sysroot = g_build_filename (f->sysroots, test->sysroot, NULL);

      info = srt_system_info_new (NULL);
      g_assert_nonnull (info);
      srt_system_info_set_sysroot (info, sysroot);

      for (j = 0; j < 2; j++)
        {
          g_assert_cmpint (srt_system_info_get_container_type (info), ==,
                           test->type);
          got = srt_system_info_dup_container_host_directory (info);

          if (test->host_directory == NULL)
            expected = NULL;
          else
            expected = g_build_filename (sysroot, test->host_directory, NULL);

          g_assert_cmpstr (got, ==, expected);
          g_free (got);
          g_free (expected);
        }

      g_object_unref (info);
      g_free (sysroot);
    }
}

/* For the purpose of this test an array that is NULL, and one with just one
 * NULL element, are considered to be equal */
static void
assert_equal_strings_arrays (const gchar * const *array1,
                             const gchar * const *array2)
{
  gsize i = 0;

  if (array1 == NULL)
    {
      if (array2 != NULL)
        g_assert_cmpstr (array2[0], ==, NULL);
      return;
    }

  if (array2 == NULL)
    {
      if (array1 != NULL)
        g_assert_cmpstr (array1[0], ==, NULL);
      return;
    }

  for (i = 0; array1[i] != NULL; i++)
    g_assert_cmpstr (array1[i], ==, array2[i]);

  g_assert_cmpstr (array2[i], ==, NULL);
}

static const char * const multiarch_tuples[] = { SRT_ABI_I386, SRT_ABI_X86_64 };

typedef struct
{
  const gchar *path;
  const gchar *data_path;
  SrtSteamIssues issues;
} SteamInstallationTest;

typedef struct
{
  const gchar *path;
  const gchar *version;
  SrtRuntimeIssues issues;
  const gchar *pinned_libs_32[5];
  const gchar *pinned_libs_64[5];
  const gchar *messages_32[5];
  const gchar *messages_64[5];
} RuntimeTest;

typedef struct
{
  const gchar *build_id;
  const gchar *id;
  const gchar *id_like[5];
  const gchar *name;
  const gchar *pretty_name;
} OsReleaseTest;

typedef struct
{
  SrtContainerType type;
  const gchar *host_path;
} ContTest;

typedef struct
{
  const gchar *library_path;
  const gchar *library_link;
  const gchar *library_soname;
  gboolean is_extra;
} DriverTest;

typedef struct
{
  SrtWindowSystem window_system;
  SrtRenderingInterface rendering_interface;
  const gchar *renderer;
  const gchar *version;
  SrtGraphicsLibraryVendor library_vendor;
  SrtGraphicsIssues issues;
  const gchar *messages;
  int exit_status;
  int terminating_signal;
  gboolean is_available;
} GraphicsTest;

typedef struct
{
  gboolean can_run;
  SrtLibraryIssues issues;
  DriverTest dri_drivers[5];
  DriverTest va_api_drivers[5];
  DriverTest vdpau_drivers[5];
  DriverTest glx_drivers[5];
  GraphicsTest graphics[10];
} ArchitectureTest;

typedef struct
{
  const gchar *name;
  const gchar *resulting_name;
  const gchar *charset;
  gboolean is_utf8;
  const gchar *error_domain;
  const gchar *error_message;
  int error_code;
} LocaleTest;

typedef struct
{
  const gchar *json_path;
  const gchar *library_path;
  const gchar *api_version;
  const gchar *error_domain;
  const gchar *error_message;
  int error_code;
} IcdTest;

typedef struct
{
  const gchar *id;
  const gchar *commandline;
  const gchar *filename;
  gboolean default_handler;
  gboolean steam_handler;
} DesktopEntryTest;

typedef struct
{
  const gchar *description;
  const gchar *input_name;
  gboolean can_write_uinput;
  SteamInstallationTest steam_installation;
  RuntimeTest runtime;
  OsReleaseTest os_release;
  ContTest container;
  const gchar *driver_environment[5];
  ArchitectureTest architecture[G_N_ELEMENTS (multiarch_tuples)];
  SrtLocaleIssues locale_issues;
  LocaleTest locale[5];
  IcdTest egl_icd[3];
  IcdTest vulkan_icd[3];
  DesktopEntryTest desktop_entry[3];
  SrtX86FeatureFlags x86_features;
  SrtX86FeatureFlags x86_known;
} JsonTest;

static const JsonTest json_test[] =
{
  { /* Begin Full JSON report */
    .description = "full JSON parsing",
    .input_name = "full-good-report.json",
    .can_write_uinput = TRUE,
    .steam_installation =
    {
      .path = "/home/me/.local/share/Steam",
      .data_path = "/home/me/.local/share/Steam",
      .issues = SRT_STEAM_ISSUES_STEAMSCRIPT_NOT_IN_ENVIRONMENT,
    },

    .runtime =
    {
      .path = "/home/me/.steam/root/ubuntu12_32/steam-runtime",
      .version = "0.20200123.4",
      .issues = SRT_RUNTIME_ISSUES_NONE,
      .pinned_libs_32 =
      {
        " 13902790      4 drwxr-xr-x   2 me  me      4096 Apr 24 21:32 pinned_libs_32",
        " 13902813      4 -rw-r--r--   1 me  me        50 Apr 24 21:32 pinned_libs_32/system_libGLU.so.1",
        " 13902814      4 lrwxrwxrwx   1 me  me       107 Apr 24 21:32 pinned_libs_32/libdbusmenu-gtk.so.4 -> /home/me/.local/share/Steam/ubuntu12_32/steam-runtime/usr/lib/i386-linux-gnu/libdbusmenu-gtk.so.4.0.13",
        NULL,
      },
      .pinned_libs_64 =
      {
        " 13902791      4 drwxr-xr-x   2 me  me      4096 Apr 24 21:32 pinned_libs_64",
        " 13902805      4 -rw-r--r--   1 me  me        46 Apr 24 21:32 pinned_libs_64/system_libGLU.so.1",
        " 13902795      4 lrwxrwxrwx   1 me  me       100 Apr 24 21:32 pinned_libs_64/libjack.so.0 -> /home/me/.local/share/Steam/ubuntu12_32/steam-runtime/usr/lib/x86_64-linux-gnu/libjack.so.0.1.0",
        NULL,
      },
    },

    .os_release =
    {
      .id = "arch",
      .id_like = {"ubuntu", "debian", NULL},
      .name = "Arch Linux",
      .pretty_name = "Arch Linux",
      .build_id = "rolling",
    },

    .container =
    {
      .type = SRT_CONTAINER_TYPE_DOCKER,
      .host_path = "/the/host/path",
    },

    .architecture =
    {
      {
        .can_run = FALSE,
        .issues = SRT_LIBRARY_ISSUES_UNKNOWN,
        .graphics =
        {
          {
            .window_system = SRT_WINDOW_SYSTEM_X11,
            .rendering_interface = SRT_RENDERING_INTERFACE_VULKAN,
            .messages = "ERROR: [Loader Message] Code 0 : /usr/lib/amdvlk64.so: wrong ELF class: ELFCLASS64\nCannot create Vulkan instance.\n",
            .issues = SRT_GRAPHICS_ISSUES_CANNOT_LOAD | SRT_GRAPHICS_ISSUES_CANNOT_DRAW,
            .exit_status = 1,
            .is_available = TRUE,
          },
          {
            .window_system = SRT_WINDOW_SYSTEM_X11,
            .rendering_interface = SRT_RENDERING_INTERFACE_VDPAU,
            .renderer = "G3DVL VDPAU Driver Shared Library version 1.0\n",
            .is_available = TRUE,
          },
          {
            .window_system = SRT_WINDOW_SYSTEM_X11,
            .rendering_interface = SRT_RENDERING_INTERFACE_VAAPI,
            .renderer = "Mesa Gallium driver 20.0.5 for AMD Radeon RX 5700 XT (NAVI10, DRM 3.36.0, 5.6.7-arch1-1, LLVM 10.0.0)\n",
            .is_available = TRUE,
          },
          {
            .window_system = SRT_WINDOW_SYSTEM_GLX,
            .rendering_interface = SRT_RENDERING_INTERFACE_GL,
            .renderer = "AMD Radeon RX 5700 XT (NAVI10, DRM 3.36.0, 5.6.7-arch1-1, LLVM 10.0.0)",
            .version = "4.6 (Compatibility Profile) Mesa 20.0.5",
            .library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_GLVND,
            .is_available = TRUE,
          },
          {
            .window_system = SRT_WINDOW_SYSTEM_EGL_X11,
            .rendering_interface = SRT_RENDERING_INTERFACE_GL,
            .renderer = "AMD Radeon RX 5700 XT (NAVI10, DRM 3.36.0, 5.6.7-arch1-1, LLVM 10.0.0)",
            .version = "4.6 (Compatibility Profile) Mesa 20.0.5",
            .library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_GLVND,
            .is_available = TRUE,
          },
          {
            .window_system = SRT_WINDOW_SYSTEM_EGL_X11,
            .rendering_interface = SRT_RENDERING_INTERFACE_GLESV2,
            .renderer = "AMD Radeon RX 5700 XT (NAVI10, DRM 3.36.0, 5.6.7-arch1-1, LLVM 10.0.0)",
            .version = "OpenGL ES 3.2 Mesa 20.0.5",
            .library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_GLVND,
            .is_available = TRUE,
          },
        },
        .dri_drivers =
        {
          {
            .library_path = "/usr/lib32/dri/radeonsi_dri.so",
          },
          {
            .library_path = "/usr/lib32/dri/vmwgfx_dri.so",
          },
        },
        .va_api_drivers =
        {
          {
            .library_path = "/home/me/.local/share/Steam/ubuntu12_32/steam-runtime/usr/lib/i386-linux-gnu/dri/dummy_drv_video.so",
          },
        },
        .vdpau_drivers =
        {
          {
            .library_path = "/home/me/.local/share/Steam/ubuntu12_32/steam-runtime/usr/lib/i386-linux-gnu/vdpau/libvdpau_trace.so.1",
            .library_link = "libvdpau_trace.so.1.0.0",
          },
        },
        .glx_drivers =
        {
          {
            .library_soname = "libGLX_indirect.so.0",
            .library_path = "/usr/lib32/libGLX_mesa.so.0.0.0",
          },
          {
            .library_soname = "libGLX_mesa.so.0",
            .library_path = "/usr/lib32/libGLX_mesa.so.0.0.0",
          },
        },
      },

      {
        .can_run = TRUE,
        .graphics =
        {
          {
            .window_system = SRT_WINDOW_SYSTEM_X11,
            .rendering_interface = SRT_RENDERING_INTERFACE_VULKAN,
            .renderer = "AMD Radeon RX 5700 XT",
            .version = "1.2.136 (device 1002:731f) (driver 2.0.140)",
            .is_available = TRUE,
          },
          {
            .window_system = SRT_WINDOW_SYSTEM_X11,
            .rendering_interface = SRT_RENDERING_INTERFACE_VDPAU,
            .renderer = "G3DVL VDPAU Driver Shared Library version 1.0\n",
            .is_available = TRUE,
          },
          {
            .window_system = SRT_WINDOW_SYSTEM_X11,
            .rendering_interface = SRT_RENDERING_INTERFACE_VAAPI,
            .renderer = "Mesa Gallium driver 20.0.5 for AMD Radeon RX 5700 XT (NAVI10, DRM 3.36.0, 5.6.7-arch1-1, LLVM 10.0.0)\n",
            .is_available = TRUE,
          },
          {
            .window_system = SRT_WINDOW_SYSTEM_GLX,
            .rendering_interface = SRT_RENDERING_INTERFACE_GL,
            .messages = "libGL: Can't open configuration file /etc/drirc: No such file or directory.\n/usr/share/libdrm/amdgpu.ids version: 1.0.0\nlibGL: Using DRI3 for screen 0\n",
            .renderer = "AMD Radeon RX 5700 XT (NAVI10, DRM 3.36.0, 5.6.7-arch1-1, LLVM 10.0.0)",
            .version = "4.6 (Compatibility Profile) Mesa 20.0.5",
            .library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_GLVND,
            .issues = SRT_GRAPHICS_ISSUES_CANNOT_DRAW,
            .terminating_signal = 6,
            .is_available = TRUE,
          },
          {
            .window_system = SRT_WINDOW_SYSTEM_EGL_X11,
            .rendering_interface = SRT_RENDERING_INTERFACE_GL,
            .renderer = "AMD Radeon RX 5700 XT (NAVI10, DRM 3.36.0, 5.6.7-arch1-1, LLVM 10.0.0)",
            .version = "4.6 (Compatibility Profile) Mesa 20.0.5",
            .library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_GLVND,
            .is_available = TRUE,
          },
          {
            .window_system = SRT_WINDOW_SYSTEM_EGL_X11,
            .rendering_interface = SRT_RENDERING_INTERFACE_GLESV2,
            .renderer = "AMD Radeon RX 5700 XT (NAVI10, DRM 3.36.0, 5.6.7-arch1-1, LLVM 10.0.0)",
            .version = "OpenGL ES 3.2 Mesa 20.0.5",
            .library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_GLVND,
            .is_available = TRUE,
          },
        },
        .dri_drivers =
        {
          {
            .library_path = "/usr/lib/dri/i915_dri.so",
          },
          {
            .library_path = "/usr/lib/dri/radeonsi_dri.so",
          },
        },
        .va_api_drivers =
        {
          {
            .library_path = "/usr/lib/dri/vdpau_drv_video.so",
          },
          {
            .library_path = "/home/me/.local/share/Steam/ubuntu12_32/steam-runtime/usr/lib/x86_64-linux-gnu/dri/dummy_drv_video.so",
          },
        },
        .vdpau_drivers =
        {
          {
            .library_path = "/usr/lib/vdpau/libvdpau_radeonsi.so",
            .library_link = "libvdpau_radeonsi.so.1.0.0",
          },
          {
            .library_path = "/usr/lib/vdpau/libvdpau_radeonsi.so.1",
            .library_link = "libvdpau_radeonsi.so.1.0.0",
          },
        },
        .glx_drivers =
        {
          {
            .library_soname = "libGLX_indirect.so.0",
            .library_path = "/usr/lib/libGLX_mesa.so.0.0.0",
          },
          {
            .library_soname = "libGLX_mesa.so.0",
            .library_path = "/usr/lib/libGLX_mesa.so.0.0.0",
          },
        },
      },
    },

    .locale_issues = SRT_LOCALE_ISSUES_C_UTF8_MISSING | SRT_LOCALE_ISSUES_I18N_SUPPORTED_MISSING,
    .locale =
    {
      {
        .name = "", // <default>
        .resulting_name = "en_US.UTF-8",
        .charset = "UTF-8",
        .is_utf8 = TRUE,
      },
      {
        .name = "C",
        .resulting_name = "C",
        .charset = "ANSI_X3.4-1968",
        .is_utf8 = FALSE,
      },
      {
        .name = "C.UTF-8",
        .error_domain = "srt-locale-error-quark",
        .error_code = 0,
        .error_message = "No such file or directory",
      },
      {
        .name = "en_US.UTF-8",
        .resulting_name = "en_US.UTF-8",
        .charset = "UTF-8",
        .is_utf8 = TRUE,
      },
    },

    .egl_icd =
    {
      {
        .json_path = "/usr/share/glvnd/egl_vendor.d/51_mesa.json",
        .library_path = "libEGL_mesa.so.0",
      },
    },

    .vulkan_icd =
    {
      {
        .json_path = "/usr/share/vulkan/icd.d/amd_icd64.json",
        .library_path = "/usr/lib/amdvlk64.so",
        .api_version = "1.2.136",
      },
    },

    .desktop_entry =
    {
      {
        .id = "steam.desktop",
        .commandline = "/usr/bin/steam-runtime %U",
        .filename = "/usr/share/applications/steam.desktop",
        .default_handler = TRUE,
        .steam_handler = TRUE,
      },
    },

    .x86_features = SRT_X86_FEATURE_X86_64 | SRT_X86_FEATURE_CMPXCHG16B,
    .x86_known = SRT_X86_FEATURE_X86_64 | SRT_X86_FEATURE_SSE3 | SRT_X86_FEATURE_CMPXCHG16B,

  }, /* End Full JSON report */

  { /* Begin Partial JSON report */
    .description = "partial JSON parsing",
    .input_name = "partial-report.json",
    .steam_installation =
    {
      .issues = SRT_STEAM_ISSUES_UNKNOWN,
    },
    .runtime =
    {
      .issues = SRT_RUNTIME_ISSUES_UNKNOWN,
    },
    .container =
    {
      .type = SRT_CONTAINER_TYPE_FLATPAK,
    },
    .driver_environment =
    {
      "LIBVA_DRIVER_NAME=vava",
      "<invalid>",
      "MESA_LOADER_DRIVER_OVERRIDE=radeonsi",
    },
    .architecture =
    {
      {
        .can_run = FALSE,
        .issues = SRT_LIBRARY_ISSUES_UNKNOWN,
        .graphics =
        {
          {
            .window_system = SRT_WINDOW_SYSTEM_X11,
            .rendering_interface = SRT_RENDERING_INTERFACE_VDPAU,
            .renderer = "G3DVL VDPAU Driver Shared Library version 1.0\n",
            .is_available = TRUE,
          },
        },
      },
      {
        .can_run = TRUE,
        .issues = SRT_LIBRARY_ISSUES_UNKNOWN,
      },
    },
    .locale_issues = SRT_LOCALE_ISSUES_UNKNOWN,
    .locale =
    {
      {
        .name = "", // <default>
        .resulting_name = "en_US.UTF-8",
        .charset = "UTF-8",
        .is_utf8 = TRUE,
      },
      {
        .name = "C",
        .error_domain = "srt-locale-error-quark",
        .error_code = 1,
        .error_message = "Information about the requested locale is missing",
      },
    },
    .vulkan_icd =
    {
      {
        .json_path = "/usr/share/vulkan/icd.d/amd_icd64.json",
        .error_domain = "g-io-error-quark", /* Default domain */
        .error_code = G_IO_ERROR_FAILED, /* Default error code */
        .error_message = "Something went wrong",
      },
    },
    .x86_features = SRT_X86_FEATURE_NONE,
    .x86_known = SRT_X86_FEATURE_NONE,
  }, /* End Partial JSON report */

  { /* Begin Partial-2 JSON report */
    .description = "partial-2 JSON parsing",
    .input_name = "partial-report-2.json",
    .steam_installation =
    {
      .path = "/home/me/.local/share/Steam",
      .issues = SRT_STEAM_ISSUES_UNKNOWN,
    },
    .runtime =
    {
      .path = "/home/me/.steam/root/ubuntu12_32/steam-runtime",
    },
    .os_release =
    {
      .id = "arch",
    },
    .container =
    {
      .type = SRT_CONTAINER_TYPE_DOCKER,
    },
    .architecture =
    {
      {
        /* i386 */
        .issues = SRT_LIBRARY_ISSUES_CANNOT_LOAD,
      },
      {
        .can_run = TRUE,
        .issues = SRT_LIBRARY_ISSUES_UNKNOWN,
      },
    },
    .locale_issues = SRT_LOCALE_ISSUES_UNKNOWN,
    .locale =
    {
      {
        .name = "", // <default>
        /* Missing the required "charset" */
        .error_domain = "g-io-error-quark", /* Default domain */
        .error_code = G_IO_ERROR_FAILED, /* Default error code */
        .error_message = "(missing error message)",
      },
    },
    .x86_features = SRT_X86_FEATURE_NONE,
    .x86_known = SRT_X86_FEATURE_NONE,
  }, /* End Partial-2 JSON report */

  { /* Begin Empty JSON report */
    .description = "empty JSON parsing",
    .input_name = "empty-report.json",
    .steam_installation =
    {
      .issues = SRT_STEAM_ISSUES_UNKNOWN,
    },
    .runtime =
    {
      .issues = SRT_RUNTIME_ISSUES_UNKNOWN,
    },
    .container =
    {
      .type = SRT_CONTAINER_TYPE_UNKNOWN,
    },
    .architecture =
    {
      {
        .issues = SRT_LIBRARY_ISSUES_CANNOT_LOAD,
      },
      {
        .issues = SRT_LIBRARY_ISSUES_CANNOT_LOAD,
      },
    },
    .locale_issues = SRT_LOCALE_ISSUES_UNKNOWN,
    .x86_features = SRT_X86_FEATURE_NONE,
    .x86_known = SRT_X86_FEATURE_NONE,
  }, /* End Empty JSON report */

  { /* Begin Newer JSON report */
    .description = "newer JSON parsing",
    .input_name = "newer-report.json",
    .steam_installation =
    {
      .issues = SRT_STEAM_ISSUES_UNKNOWN,
    },
    .runtime =
    {
      .issues = SRT_RUNTIME_ISSUES_UNKNOWN,
    },
    .container =
    {
      .type = SRT_CONTAINER_TYPE_UNKNOWN,
    },
    .architecture =
    {
      {
        .can_run = FALSE,
        .issues = SRT_LIBRARY_ISSUES_CANNOT_LOAD | SRT_LIBRARY_ISSUES_UNKNOWN,
        .graphics =
        {
          {
            .window_system = SRT_WINDOW_SYSTEM_X11,
            .rendering_interface = SRT_RENDERING_INTERFACE_VDPAU,
            .renderer = "G3DVL VDPAU Driver Shared Library version 1.0\n",
            .library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN,
            .issues = SRT_GRAPHICS_ISSUES_CANNOT_DRAW | SRT_GRAPHICS_ISSUES_UNKNOWN,
            .is_available = TRUE,
          },
        },
      },
      {
        /* x86_64 */
        .issues = SRT_LIBRARY_ISSUES_CANNOT_LOAD,
      }
    },
    .locale_issues = SRT_LOCALE_ISSUES_C_UTF8_MISSING | SRT_LOCALE_ISSUES_UNKNOWN,
    .x86_features = SRT_X86_FEATURE_X86_64 | SRT_X86_FEATURE_SSE3 | SRT_X86_FEATURE_UNKNOWN,
    .x86_known = SRT_X86_FEATURE_X86_64 | SRT_X86_FEATURE_SSE3 | SRT_X86_FEATURE_CMPXCHG16B | SRT_X86_FEATURE_UNKNOWN,
  }, /* End Newer JSON report */
};

static void
json_parsing (Fixture *f,
              gconstpointer context)
{
  for (gsize i = 0; i < G_N_ELEMENTS (json_test); i++)
    {
      const JsonTest *t = &json_test[i];
      SrtSystemInfo *info;
      gchar *input_json;
      gchar **driver_environment_list;
      gsize j;
      gsize jj;
      GList *icds;
      GList *iter;
      GList *desktop_entries;
      gchar *steam_path = NULL;
      gchar *steam_data_path = NULL;
      gchar *runtime_path = NULL;
      gchar *runtime_v = NULL;
      gchar *build_id;
      gchar *id;
      gchar *name;
      gchar *pretty_name;
      gchar *host_directory;
      gchar **pinned_32 = NULL;
      gchar **messages_32 = NULL;
      gchar **pinned_64 = NULL;
      gchar **messages_64 = NULL;
      gchar **id_like = NULL;
      GError *error = NULL;

      g_test_message ("%s: %s", t->input_name, t->description);

      input_json = g_build_filename (f->srcdir, "json-report", t->input_name, NULL);

      info = srt_system_info_new_from_json (input_json, &error);

      g_assert_no_error (error);

      g_assert_cmpint (t->can_write_uinput, ==, srt_system_info_can_write_to_uinput (info));

      steam_path = srt_system_info_dup_steam_installation_path (info);
      steam_data_path = srt_system_info_dup_steam_data_path (info);
      g_assert_cmpstr (t->steam_installation.path, ==, steam_path);
      g_assert_cmpstr (t->steam_installation.data_path, ==, steam_data_path);
      g_assert_cmpint (t->steam_installation.issues, ==, srt_system_info_get_steam_issues (info));

      runtime_path = srt_system_info_dup_runtime_path (info);
      runtime_v = srt_system_info_dup_runtime_version (info);
      pinned_32 = srt_system_info_list_pinned_libs_32 (info, &messages_32);
      pinned_64 = srt_system_info_list_pinned_libs_64 (info, &messages_64);
      g_assert_cmpstr (t->runtime.path, ==, runtime_path);
      g_assert_cmpstr (t->runtime.version, ==, runtime_v);
      g_assert_cmpint (t->runtime.issues, ==, srt_system_info_get_runtime_issues (info));
      assert_equal_strings_arrays (t->runtime.pinned_libs_32, (const gchar * const *)pinned_32);
      assert_equal_strings_arrays (t->runtime.pinned_libs_64, (const gchar * const *)pinned_64);
      assert_equal_strings_arrays (t->runtime.messages_32, (const gchar * const *)messages_32);
      assert_equal_strings_arrays (t->runtime.messages_64, (const gchar * const *)messages_64);

      build_id = srt_system_info_dup_os_build_id (info);
      id = srt_system_info_dup_os_id (info);
      id_like = srt_system_info_dup_os_id_like (info, FALSE);
      name = srt_system_info_dup_os_name (info);
      pretty_name = srt_system_info_dup_os_pretty_name (info);
      g_assert_cmpstr (t->os_release.build_id, ==, build_id);
      g_assert_cmpstr (t->os_release.id, ==, id);
      assert_equal_strings_arrays (t->os_release.id_like, (const gchar * const *)id_like);
      g_assert_cmpstr (t->os_release.name, ==, name);
      g_assert_cmpstr (t->os_release.pretty_name, ==, pretty_name);

      host_directory = srt_system_info_dup_container_host_directory (info);
      g_assert_cmpint (t->container.type, ==, srt_system_info_get_container_type (info));
      g_assert_cmpstr (t->container.host_path, ==, host_directory);

      driver_environment_list = srt_system_info_list_driver_environment (info);
      assert_equal_strings_arrays (t->driver_environment,
                                   (const gchar * const *)driver_environment_list);

      for (j = 0; j < G_N_ELEMENTS (multiarch_tuples); j++)
        {
          ArchitectureTest this_arch = t->architecture[j];
          GList *dri_list;
          GList *va_api_list;
          GList *vdpau_list;
          GList *glx_list;

          g_assert_cmpint (this_arch.can_run, ==, srt_system_info_can_run (info, multiarch_tuples[j]));
          if (this_arch.can_run)
            {
              g_assert_cmpint (this_arch.issues, ==,
                               srt_system_info_check_libraries (info, multiarch_tuples[j], NULL));
            }

          g_assert_cmpint (this_arch.issues, ==,
                           srt_system_info_check_libraries (info, multiarch_tuples[j], NULL));

          for (jj = 0; this_arch.graphics[jj].is_available; jj++)
            {
              SrtGraphics *graphics = NULL;
              SrtGraphicsLibraryVendor library_vendor;
              srt_system_info_check_graphics (info,
                                              multiarch_tuples[j],
                                              this_arch.graphics[jj].window_system,
                                              this_arch.graphics[jj].rendering_interface,
                                              &graphics);
              g_assert_cmpstr (this_arch.graphics[jj].messages, ==,
                               srt_graphics_get_messages (graphics));
              g_assert_cmpstr (this_arch.graphics[jj].renderer, ==,
                               srt_graphics_get_renderer_string (graphics));
              g_assert_cmpstr (this_arch.graphics[jj].version, ==,
                               srt_graphics_get_version_string (graphics));
              g_assert_cmpint (this_arch.graphics[jj].issues, ==,
                               srt_graphics_get_issues (graphics));
              srt_graphics_library_is_vendor_neutral (graphics, &library_vendor);
              g_assert_cmpint (this_arch.graphics[jj].library_vendor, ==, library_vendor);
              g_assert_cmpint (this_arch.graphics[jj].exit_status, ==,
                               srt_graphics_get_exit_status (graphics));
              g_assert_cmpint (this_arch.graphics[jj].terminating_signal, ==,
                               srt_graphics_get_terminating_signal (graphics));
              g_object_unref (graphics);
            }

          dri_list = srt_system_info_list_dri_drivers (info, multiarch_tuples[j],
                                                       SRT_DRIVER_FLAGS_INCLUDE_ALL);
          for (jj = 0, iter = dri_list; iter != NULL; iter = iter->next, jj++)
            {
              g_assert_cmpstr (this_arch.dri_drivers[jj].library_path, ==,
                               srt_dri_driver_get_library_path (iter->data));
              g_assert_cmpint (this_arch.dri_drivers[jj].is_extra, ==,
                               srt_dri_driver_is_extra (iter->data));
            }
          g_assert_cmpstr (this_arch.dri_drivers[jj].library_path, ==, NULL);

          va_api_list = srt_system_info_list_va_api_drivers (info, multiarch_tuples[j],
                                                             SRT_DRIVER_FLAGS_INCLUDE_ALL);
          for (jj = 0, iter = va_api_list; iter != NULL; iter = iter->next, jj++)
            {
              g_assert_cmpstr (this_arch.va_api_drivers[jj].library_path, ==,
                               srt_va_api_driver_get_library_path (iter->data));
              g_assert_cmpint (this_arch.va_api_drivers[jj].is_extra, ==,
                               srt_va_api_driver_is_extra (iter->data));
            }
          g_assert_cmpstr (this_arch.va_api_drivers[jj].library_path, ==, NULL);

          vdpau_list = srt_system_info_list_vdpau_drivers (info, multiarch_tuples[j],
                                                           SRT_DRIVER_FLAGS_INCLUDE_ALL);
          for (jj = 0, iter = vdpau_list; iter != NULL; iter = iter->next, jj++)
            {
              g_assert_cmpstr (this_arch.vdpau_drivers[jj].library_path, ==,
                               srt_vdpau_driver_get_library_path (iter->data));
              g_assert_cmpstr (this_arch.vdpau_drivers[jj].library_link, ==,
                               srt_vdpau_driver_get_library_link (iter->data));
              g_assert_cmpint (this_arch.va_api_drivers[jj].is_extra, ==,
                               srt_vdpau_driver_is_extra (iter->data));
            }
          g_assert_cmpstr (this_arch.vdpau_drivers[jj].library_path, ==, NULL);
          g_assert_cmpstr (this_arch.vdpau_drivers[jj].library_link, ==, NULL);

          glx_list = srt_system_info_list_glx_icds (info, multiarch_tuples[j], SRT_DRIVER_FLAGS_INCLUDE_ALL);
          for (jj = 0, iter = glx_list; iter != NULL; iter = iter->next, jj++)
            {
              g_assert_cmpstr (this_arch.glx_drivers[jj].library_path, ==,
                               srt_glx_icd_get_library_path (iter->data));
              g_assert_cmpstr (this_arch.glx_drivers[jj].library_soname, ==,
                               srt_glx_icd_get_library_soname (iter->data));
            }
          g_assert_cmpstr (this_arch.glx_drivers[jj].library_path, ==, NULL);
          g_assert_cmpstr (this_arch.glx_drivers[jj].library_soname, ==, NULL);

          g_list_free_full (dri_list, g_object_unref);
          g_list_free_full (va_api_list, g_object_unref);
          g_list_free_full (vdpau_list, g_object_unref);
          g_list_free_full (glx_list, g_object_unref);
        }

      g_assert_cmpint (t->locale_issues, ==, srt_system_info_get_locale_issues (info));
      for (j = 0; t->locale[j].name != NULL; j++)
        {
          error = NULL;
          SrtLocale *this_locale = srt_system_info_check_locale (info, t->locale[j].name, &error);
          if (this_locale != NULL)
            {
              g_assert_cmpstr (t->locale[j].resulting_name, ==,
                               srt_locale_get_resulting_name (this_locale));
              g_assert_cmpstr (t->locale[j].charset, ==,
                               srt_locale_get_charset (this_locale));
              g_assert_cmpint (t->locale[j].is_utf8, ==,
                               srt_locale_is_utf8 (this_locale));
            }
          else
            {
              g_assert_cmpstr (t->locale[j].error_domain, ==,
                               g_quark_to_string (error->domain));
              g_assert_cmpint (t->locale[j].error_code, ==, error->code);
              g_assert_cmpstr (t->locale[j].error_message, ==, error->message);
            }
          g_clear_object (&this_locale);
          g_clear_error (&error);
        }

      icds = srt_system_info_list_egl_icds (info, multiarch_tuples);
      for (j = 0, iter = icds; iter != NULL; iter = iter->next, j++)
        {
          error = NULL;
          g_assert_cmpstr (t->egl_icd[j].json_path, ==, srt_egl_icd_get_json_path (iter->data));
          if (srt_egl_icd_check_error (iter->data, &error))
            {
              g_assert_cmpstr (t->egl_icd[j].library_path, ==, srt_egl_icd_get_library_path (iter->data));
            }
          else
            {
              g_assert_cmpstr (t->egl_icd[j].error_domain, ==,
                               g_quark_to_string (error->domain));
              g_assert_cmpint (t->egl_icd[j].error_code, ==, error->code);
              g_assert_cmpstr (t->egl_icd[j].error_message, ==, error->message);
            }
          g_clear_error (&error);
        }
      g_list_free_full (icds, g_object_unref);

      icds = srt_system_info_list_vulkan_icds (info, multiarch_tuples);
      for (j = 0, iter = icds; iter != NULL; iter = iter->next, j++)
        {
          error = NULL;
          g_assert_cmpstr (t->vulkan_icd[j].json_path, ==, srt_vulkan_icd_get_json_path (iter->data));
          if (srt_vulkan_icd_check_error (iter->data, &error))
            {
              g_assert_cmpstr (t->vulkan_icd[j].library_path, ==,
                               srt_vulkan_icd_get_library_path (iter->data));
              g_assert_cmpstr (t->vulkan_icd[j].api_version, ==,
                               srt_vulkan_icd_get_api_version (iter->data));
            }
          else
            {
              g_assert_cmpstr (t->vulkan_icd[j].error_domain, ==,
                               g_quark_to_string (error->domain));
              g_assert_cmpint (t->vulkan_icd[j].error_code, ==, error->code);
              g_assert_cmpstr (t->vulkan_icd[j].error_message, ==, error->message);
            }
        }

      desktop_entries = srt_system_info_list_desktop_entries (info);
      for (j = 0, iter = desktop_entries; iter != NULL; iter = iter->next, j++)
        {
          g_assert_cmpstr (t->desktop_entry[j].id, ==, srt_desktop_entry_get_id (iter->data));
          g_assert_cmpstr (t->desktop_entry[j].commandline, ==,
                           srt_desktop_entry_get_commandline (iter->data));
          g_assert_cmpstr (t->desktop_entry[j].filename, ==, srt_desktop_entry_get_filename (iter->data));
          g_assert_cmpint (t->desktop_entry[j].default_handler, ==,
                           srt_desktop_entry_is_default_handler (iter->data));
          g_assert_cmpint (t->desktop_entry[j].steam_handler, ==,
                           srt_desktop_entry_is_steam_handler (iter->data));
        }

      g_assert_cmpint (t->x86_features, ==, srt_system_info_get_x86_features (info));
      g_assert_cmpint (t->x86_known, ==, srt_system_info_get_known_x86_features (info));

      g_list_free_full (icds, g_object_unref);
      g_free (steam_path);
      g_free (steam_data_path);
      g_free (runtime_path);
      g_free (runtime_v);
      g_free (build_id);
      g_free (id);
      g_free (name);
      g_free (pretty_name);
      g_free (host_directory);
      g_strfreev (pinned_32);
      g_strfreev (pinned_64);
      g_strfreev (messages_32);
      g_strfreev (messages_64);
      g_strfreev (id_like);
      g_strfreev (driver_environment_list);
      g_free (input_json);
      g_clear_error (&error);
      g_object_unref (info);
    }
}

int
main (int argc,
      char **argv)
{
  int status;
  argv0 = argv[0];

  /* We can't use %G_TEST_OPTION_ISOLATE_DIRS because we are targeting an older glib verion.
   * As a workaround we use our function `setup_env()`.
   * We can't use an environ because in `_srt_steam_check()` we call `g_app_info_get_*` that
   * doesn't support a custom environ. */
  g_test_init (&argc, &argv, NULL);
  fake_home_path = _srt_global_setup_private_xdg_dirs ();

  g_test_add ("/system-info/object", Fixture, NULL,
              setup, test_object, teardown);
  g_test_add ("/system-info/libraries_presence", Fixture, NULL,
              setup, libraries_presence, teardown);
  g_test_add ("/system-info/auto_expectations", Fixture, NULL,
              setup, auto_expectations, teardown);
  g_test_add ("/system-info/library_presence", Fixture, NULL,
              setup, library_presence, teardown);
  g_test_add ("/system-info/libraries_missing", Fixture, NULL,
              setup, libraries_missing, teardown);
  g_test_add ("/system-info/library_missing", Fixture, NULL,
              setup, library_missing, teardown);
  g_test_add ("/system-info/wrong_expectations", Fixture, NULL,
              setup, wrong_expectations, teardown);

  g_test_add ("/system-info/steam_runtime", Fixture, NULL,
              setup, steam_runtime, teardown);
  g_test_add ("/system-info/steam_runtime_missing", Fixture, NULL,
              setup, steam_runtime_missing, teardown);
  g_test_add ("/system-info/steam_runtime_pinned", Fixture, NULL,
              setup, steam_runtime_pinned, teardown);
  g_test_add ("/system-info/runtime_disabled_or_missing", Fixture, NULL,
              setup, runtime_disabled_or_missing, teardown);
  g_test_add ("/system-info/runtime_version", Fixture, NULL,
              setup, runtime_version, teardown);
  g_test_add ("/system-info/runtime_unexpected_location", Fixture, NULL,
              setup, runtime_unexpected_location, teardown);
  g_test_add ("/system-info/steam_symlink", Fixture, NULL,
              setup, steam_symlink, teardown);
  g_test_add ("/system-info/debian_bug_916303", Fixture, NULL,
              setup, debian_bug_916303, teardown);
  g_test_add ("/system-info/testing_beta_client", Fixture, NULL,
              setup, testing_beta_client, teardown);

  g_test_add ("/system-info/os/debian10", Fixture, NULL,
              setup, os_debian10, teardown);
  g_test_add ("/system-info/os/debian-unstable", Fixture,
              "debian-unstable",
              setup, os_debian_unstable, teardown);
  g_test_add ("/system-info/os/flatpak-on-debian-unstable", Fixture,
              "flatpak-example/run/host",
              setup, os_debian_unstable, teardown);
  g_test_add ("/system-info/os/steamrt", Fixture, NULL,
              setup, os_steamrt, teardown);
  g_test_add ("/system-info/os/steamrt-unofficial", Fixture, NULL,
              setup, os_steamrt_unofficial, teardown);
  g_test_add ("/system-info/os/invalid-os-release", Fixture, NULL,
              setup, os_invalid_os_release, teardown);
  g_test_add ("/system-info/os/no-os-release", Fixture, NULL,
              setup, os_no_os_release, teardown);

  g_test_add ("/system-info/libs/overrides", Fixture, NULL,
              setup, overrides, teardown);
  g_test_add ("/system-info/libs/overrides_issues", Fixture, NULL,
              setup, overrides_issues, teardown);
  g_test_add ("/system-info/libs/overrides_not_available", Fixture, NULL,
              setup, overrides_not_available, teardown);
  g_test_add ("/system-info/libs/pinned_libraries", Fixture, NULL,
              setup, pinned_libraries, teardown);
  g_test_add ("/system-info/libs/pinned_libraries_permission", Fixture, NULL,
              setup, pinned_libraries_permission, teardown);
  g_test_add ("/system-info/libs/pinned_libraries_missing", Fixture, NULL,
              setup, pinned_libraries_missing, teardown);

  g_test_add ("/system-info/driver_environment", Fixture, NULL,
              setup, driver_environment, teardown);

  g_test_add ("/system-info/containers", Fixture, NULL,
              setup, test_containers, teardown);

  g_test_add ("/system-info/json_parsing", Fixture, NULL,
              setup, json_parsing, teardown);

  status = g_test_run ();

  if (!_srt_global_teardown_private_xdg_dirs ())
    g_debug ("Unable to remove the fake home parent directory of: %s", fake_home_path);

  return status;
}
