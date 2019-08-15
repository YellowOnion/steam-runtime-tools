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
  gchar *runtime_path = NULL;
  gchar *installation_path = NULL;
  FakeHome *fake_home;

  fake_home = fake_home_new ();
  fake_home_create_structure (fake_home);

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, fake_home->env);

  /* Check for runtime issues */
  runtime_issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (runtime_issues, ==, SRT_RUNTIME_ISSUES_NONE);
  runtime_path = srt_system_info_dup_runtime_path (info);
  g_assert_cmpstr (runtime_path, ==, fake_home->runtime);
  installation_path = srt_system_info_dup_steam_installation_path (info);
  g_assert_cmpstr (installation_path, ==, fake_home->steam_base_folder);
  g_free (runtime_path);
  g_free (installation_path);

  /* Do the check again, this time using the cache */
  runtime_issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (runtime_issues, ==, SRT_RUNTIME_ISSUES_NONE);
  runtime_path = srt_system_info_dup_runtime_path (info);
  g_assert_cmpstr (runtime_path, ==, fake_home->runtime);
  installation_path = srt_system_info_dup_steam_installation_path (info);
  g_assert_cmpstr (installation_path, ==, fake_home->steam_base_folder);

  /* Check for Steam issues */
  steam_issues = srt_system_info_get_steam_issues (info);
  g_assert_cmpint (steam_issues, ==, SRT_RUNTIME_ISSUES_NONE);

  /* Do the check again, this time using the cache */
  steam_issues = srt_system_info_get_steam_issues (info);
  g_assert_cmpint (steam_issues, ==, SRT_RUNTIME_ISSUES_NONE);

  fake_home_clean_up (fake_home);
  g_object_unref (info);
  g_free (runtime_path);
  g_free (installation_path);
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

  fake_home = fake_home_new ();
  fake_home_create_structure (fake_home);

  full_ld_path = g_strdup (g_environ_getenv (fake_home->env, "LD_LIBRARY_PATH"));

  info = srt_system_info_new (NULL);

  /* Unset LD_LIBRARY_PATH */
  fake_home->env = g_environ_unsetenv (fake_home->env, "LD_LIBRARY_PATH");
  srt_system_info_set_environ (info, fake_home->env);
  runtime_issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (runtime_issues, ==, SRT_RUNTIME_ISSUES_NOT_IN_LD_PATH);

  /* Re set LD_LIBRARY_PATH and remove a required folder from the runtime */
  fake_home->env = g_environ_setenv (fake_home->env, "LD_LIBRARY_PATH", full_ld_path, TRUE);
  srt_system_info_set_environ (info, fake_home->env);
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

  fake_home = fake_home_new ();
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
  srt_system_info_set_environ (info, fake_home->env);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (SRT_RUNTIME_ISSUES_NOT_USING_NEWER_HOST_LIBRARIES, ==, issues);
  g_free (ld_path);

  /* Remove the pinned library folder */
  g_assert_cmpint (g_rmdir (fake_home->pinned_32), ==, 0);
  g_assert_cmpint (g_rmdir (fake_home->pinned_64), ==, 0);
  fake_home->env = g_environ_setenv (fake_home->env, "LD_LIBRARY_PATH", full_ld_path, TRUE);
  srt_system_info_set_environ (info, fake_home->env);
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
  srt_system_info_set_environ (info, fake_home->env);
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

  fake_home = fake_home_new ();
  fake_home->create_steamrt_files = FALSE;
  fake_home_create_structure (fake_home);

  info = srt_system_info_new (NULL);

  /* Completely disable the runtime */
  fake_home->env = g_environ_setenv (fake_home->env, "STEAM_RUNTIME", "0", TRUE);
  srt_system_info_set_environ (info, fake_home->env);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_DISABLED);
  runtime_path = srt_system_info_dup_runtime_path (info);
  g_assert_cmpstr (runtime_path, ==, NULL);

  /* Set the runtime to a relative position.
   * Test if we can revover using the expected path.
   * We didn't create SteamRT files so expect to receive a "not_runtime"
   * issue. */
  fake_home->env = g_environ_setenv (fake_home->env, "STEAM_RUNTIME", "my/not/absolute/runtime/path", TRUE);
  srt_system_info_set_environ (info, fake_home->env);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (SRT_RUNTIME_ISSUES_NOT_IN_ENVIRONMENT |
                   SRT_RUNTIME_ISSUES_NOT_RUNTIME, ==, issues);

  /* Remove the STEAM_RUNTIME environment. */
  fake_home->env = g_environ_unsetenv (fake_home->env, "STEAM_RUNTIME");
  srt_system_info_set_environ (info, fake_home->env);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (SRT_RUNTIME_ISSUES_NOT_IN_ENVIRONMENT |
                   SRT_RUNTIME_ISSUES_NOT_RUNTIME, ==, issues);

  /* Disable prefer host libraries */
  fake_home->env = g_environ_setenv (fake_home->env, "STEAM_RUNTIME_PREFER_HOST_LIBRARIES", "0", TRUE);
  srt_system_info_set_environ (info, fake_home->env);
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

  fake_home = fake_home_new ();
  fake_home_create_structure (fake_home);

  version = g_build_filename (fake_home->runtime, "version.txt", NULL);
  info = srt_system_info_new (NULL);

  /* Check version with a trailing new line */
  g_file_set_contents (version, "steam-runtime_0.20190711.3\n", -1, &error);
  g_assert_no_error (error);
  srt_system_info_set_environ (info, fake_home->env);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_NONE);

  /* Check version with an empty number */
  g_file_set_contents (version, "steam-runtime_", -1, &error);
  g_assert_no_error (error);
  srt_system_info_set_environ (info, fake_home->env);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_NOT_RUNTIME);

  /* Check version without underscore */
  g_file_set_contents (version, "steam-runtime", -1, &error);
  g_assert_no_error (error);
  srt_system_info_set_environ (info, fake_home->env);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_NOT_RUNTIME);
  dup_version = srt_system_info_dup_runtime_version (info);
  g_assert_cmpstr (dup_version, ==, NULL);

  /* Check version with a custom prefix */
  g_file_set_contents (version, "custom-steam-runtime_0.20190711.3", -1, &error);
  g_assert_no_error (error);
  srt_system_info_set_environ (info, fake_home->env);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_UNOFFICIAL);

  /* Check version with a custom prefix and multiple underscores */
  g_file_set_contents (version, "custom_steam_runtime_0.20190711.3", -1, &error);
  g_assert_no_error (error);
  srt_system_info_set_environ (info, fake_home->env);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_UNOFFICIAL);
  dup_version = srt_system_info_dup_runtime_version (info);
  g_assert_cmpstr (dup_version, ==, "0.20190711.3");
  g_free (dup_version);

  /* Check an empty version file */
  g_file_set_contents (version, "", -1, &error);
  g_assert_no_error (error);
  srt_system_info_set_environ (info, fake_home->env);
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
  gchar *dot_steam_steam = NULL;
  gchar *my_runtime = NULL;
  gchar *ld_path = NULL;
  gchar *env_path = NULL;
  gchar **parts = NULL;
  GFile *symlink = NULL;
  GError *error = NULL;
  FakeHome *fake_home;

  fake_home = fake_home_new ();
  fake_home->create_steam_symlink = FALSE;
  fake_home_create_structure (fake_home);

  info = srt_system_info_new (NULL);
  dot_steam_steam = g_build_filename (fake_home->home, ".steam", "steam", NULL);

  my_runtime = g_build_filename (fake_home->steam_base_folder, "ubuntu12_32",
                                 "my-runtime", NULL);


  /* Create a new homedir/.steam/steam symlink that doesn't point to
   * the expected steam runtime path. */
  symlink = g_file_new_for_path (dot_steam_steam);
  g_file_make_symbolic_link (symlink, fake_home->pinned_64, NULL, &error);
  g_assert_no_error (error);
  srt_system_info_set_environ (info, fake_home->env);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_UNEXPECTED_LOCATION);
  g_object_unref (symlink);

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
  g_remove (dot_steam_steam);
  symlink = g_file_new_for_path (dot_steam_steam);
  g_file_make_symbolic_link (symlink, my_runtime, NULL, &error);
  g_assert_no_error (error);
  fake_home->env = g_environ_setenv (fake_home->env, "LD_LIBRARY_PATH", ld_path, TRUE);
  fake_home->env = g_environ_setenv (fake_home->env, "STEAM_RUNTIME", my_runtime, TRUE);
  fake_home->env = g_environ_setenv (fake_home->env, "PATH", env_path, TRUE);

  srt_system_info_set_environ (info, fake_home->env);
  issues = srt_system_info_get_runtime_issues (info);
  g_assert_cmpint (issues, ==, SRT_RUNTIME_ISSUES_UNEXPECTED_LOCATION);

  fake_home_clean_up (fake_home);
  g_object_unref (symlink);
  g_object_unref (info);
  g_free (dot_steam_steam);
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
  GFile *symlink = NULL;
  GError *error = NULL;
  FakeHome *fake_home;

  fake_home = fake_home_new ();
  fake_home->create_steam_symlink = FALSE;
  fake_home_create_structure (fake_home);

  info = srt_system_info_new (NULL);
  dot_steam_steam = g_build_filename (fake_home->home, ".steam", "steam", NULL);
  dot_steam_root = g_build_filename (fake_home->home, ".steam", "root", NULL);
  dot_steam_bin32 = g_build_filename (fake_home->home, ".steam", "bin32", NULL);
  ubuntu12_32 = g_build_filename (fake_home->steam_base_folder, "ubuntu12_32", NULL);

  /* We don't have a homedir/.steam/steam symlink. */
  srt_system_info_set_environ (info, fake_home->env);
  issues = srt_system_info_get_steam_issues (info);
  g_assert_cmpint (SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_SYMLINK, ==, issues);

  /* Create the homedir/.steam/root symlink. */
  symlink = g_file_new_for_path (dot_steam_root);
  g_file_make_symbolic_link (symlink, fake_home->steam_base_folder, NULL, &error);
  g_assert_no_error (error);
  srt_system_info_set_environ (info, fake_home->env);
  issues = srt_system_info_get_steam_issues (info);
  g_assert_cmpint (SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_SYMLINK, ==, issues);
  g_object_unref (symlink);

  /* Remove homedir/.steam/root symlink and create homedir/.steam/bin32 symlink. */
  g_remove (dot_steam_root);
  symlink = g_file_new_for_path (dot_steam_bin32);
  g_file_make_symbolic_link (symlink, ubuntu12_32, NULL, &error);
  g_assert_no_error (error);
  srt_system_info_set_environ (info, fake_home->env);
  issues = srt_system_info_get_steam_issues (info);
  g_assert_cmpint (SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_SYMLINK, ==, issues);

  /* Remove the homedir/.steam/bin32 symlink and set XDG_DATA_HOME env to a
   * folder that is not the expected homedir/.local/share */
  g_remove (dot_steam_bin32);
  data_home = g_build_filename (fake_home->home, "DataHome", NULL);
  fake_home->env = g_environ_setenv (fake_home->env, "XDG_DATA_HOME", data_home, TRUE);
  srt_system_info_set_environ (info, fake_home->env);
  issues = srt_system_info_get_steam_issues (info);
  g_assert_cmpint (SRT_STEAM_ISSUES_CANNOT_FIND |
                   SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_SYMLINK, ==, issues);
  installation_path = srt_system_info_dup_steam_installation_path (info);
  g_assert_cmpstr (installation_path, ==, NULL);

  fake_home_clean_up (fake_home);
  g_object_unref (symlink);
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

  fake_home = fake_home_new ();
  fake_home->has_debian_bug_916303 = TRUE;
  fake_home_create_structure (fake_home);

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, fake_home->env);

  issues = srt_system_info_get_steam_issues (info);
  g_assert_cmpint (issues, ==, SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_SYMLINK);

  fake_home_clean_up (fake_home);
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

  return g_test_run ();
}
