/*
 * Copyright © 2019-2020 Collabora Ltd.
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

#include <libglnx.h>

#include <steam-runtime-tools/glib-backports-internal.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <fcntl.h>
#include <string.h>
#include <sysexits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
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

  /* For the tests we currently have they are not used yet */
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

static void
test_arguments_validation (Fixture *f,
                           gconstpointer context)
{
  gboolean ret;
  int exit_status = -1;
  GError *error = NULL;
  gchar *output = NULL;
  gchar *diagnostics = NULL;
  const gchar *argv[] = { "steam-runtime-check-requirements", NULL, NULL };

  ret = g_spawn_sync (NULL,    /* working directory */
                      (gchar **) argv,
                      NULL,    /* envp */
                      G_SPAWN_SEARCH_PATH,
                      NULL,    /* child setup */
                      NULL,    /* user data */
                      &output,
                      &diagnostics,
                      &exit_status,
                      &error);
  g_assert_no_error (error);
  g_assert_true (ret);
  /* Do not assume the CI workers hardware. So we expect either a success or
   * an EX_OSERR status */
  if (exit_status != 0 && WIFEXITED (exit_status))
    g_assert_cmpint (WEXITSTATUS (exit_status), ==, EX_OSERR);
  else
    g_assert_cmpint (exit_status, ==, 0);
  g_assert_nonnull (output);
  g_assert_true (g_utf8_validate (output, -1, NULL));

  if (exit_status != 0)
    g_assert_cmpstr (output, !=, "");

  g_free (output);
  g_free (diagnostics);
  argv[1] = "--this-option-is-unsupported";
  ret = g_spawn_sync (NULL,    /* working directory */
                      (gchar **) argv,
                      NULL,    /* envp */
                      G_SPAWN_SEARCH_PATH,
                      NULL,    /* child setup */
                      NULL,    /* user data */
                      &output,
                      &diagnostics,
                      &exit_status,
                      &error);
  g_assert_no_error (error);
  g_assert_true (ret);
  g_assert_true (WIFEXITED (exit_status));
  g_assert_cmpint (WEXITSTATUS (exit_status), ==, EX_USAGE);
  g_assert_nonnull (output);
  g_assert_cmpstr (output, ==, "");
  g_assert_true (g_utf8_validate (output, -1, NULL));
  g_assert_nonnull (diagnostics);
  g_assert_cmpstr (diagnostics, !=, "");
  g_assert_true (g_utf8_validate (diagnostics, -1, NULL));

  g_free (output);
  g_free (diagnostics);
  argv[1] = "this-argument-is-unsupported";
  ret = g_spawn_sync (NULL,    /* working directory */
                      (gchar **) argv,
                      NULL,    /* envp */
                      G_SPAWN_SEARCH_PATH,
                      NULL,    /* child setup */
                      NULL,    /* user data */
                      &output,
                      &diagnostics,
                      &exit_status,
                      &error);
  g_assert_no_error (error);
  g_assert_true (ret);
  g_assert_true (WIFEXITED (exit_status));
  g_assert_cmpint (WEXITSTATUS (exit_status), ==, EX_USAGE);
  g_assert_nonnull (output);
  g_assert_cmpstr (output, ==, "");
  g_assert_true (g_utf8_validate (output, -1, NULL));
  g_assert_nonnull (diagnostics);
  g_assert_cmpstr (diagnostics, !=, "");
  g_assert_true (g_utf8_validate (diagnostics, -1, NULL));

  g_free (output);
  g_free (diagnostics);
  g_clear_error (&error);
}

/*
 * Test `steam-runtime-check-requirements --help` and `--version`.
 */
static void
test_help_and_version (Fixture *f,
                       gconstpointer context)
{
  gboolean ret;
  int exit_status = -1;
  GError *error = NULL;
  gchar *output = NULL;
  gchar *diagnostics = NULL;
  const gchar *argv[] = {
      "env",
      "LC_ALL=C",
      "steam-runtime-check-requirements",
      "--version",
      NULL
  };

  ret = g_spawn_sync (NULL,    /* working directory */
                      (gchar **) argv,
                      NULL,    /* envp */
                      G_SPAWN_SEARCH_PATH,
                      NULL,    /* child setup */
                      NULL,    /* user data */
                      &output,
                      &diagnostics,
                      &exit_status,
                      &error);
  g_assert_no_error (error);
  g_assert_true (ret);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_nonnull (output);
  g_assert_cmpstr (output, !=, "");
  g_assert_true (g_utf8_validate (output, -1, NULL));
  g_assert_nonnull (diagnostics);

  g_assert_nonnull (strstr (output, VERSION));

  g_free (output);
  g_free (diagnostics);
  g_clear_error (&error);

  argv[3] = "--help";

  ret = g_spawn_sync (NULL,    /* working directory */
                      (gchar **) argv,
                      NULL,    /* envp */
                      G_SPAWN_SEARCH_PATH,
                      NULL,    /* child setup */
                      NULL,    /* user data */
                      &output,
                      &diagnostics,
                      &exit_status,
                      &error);
  g_assert_no_error (error);
  g_assert_true (ret);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_nonnull (output);
  g_assert_cmpstr (output, !=, "");
  g_assert_true (g_utf8_validate (output, -1, NULL));
  g_assert_nonnull (diagnostics);

  g_assert_nonnull (strstr (output, "OPTIONS"));

  g_free (output);
  g_free (diagnostics);
  g_clear_error (&error);
}

int
main (int argc,
      char **argv)
{
  argv0 = argv[0];

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/check-requirements-cli/arguments_validation", Fixture, NULL,
              setup, test_arguments_validation, teardown);
  g_test_add ("/check-requirements-cli/help-and-version", Fixture, NULL,
              setup, test_help_and_version, teardown);

  return g_test_run ();
}
