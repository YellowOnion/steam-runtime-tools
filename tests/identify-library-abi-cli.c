/*
 * Copyright © 2021 Collabora Ltd.
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
#include <steam-runtime-tools/glib-backports-internal.h>
#include <steam-runtime-tools/utils-internal.h>

#include <glib.h>

#include <fcntl.h>
#include <string.h>
#include <sysexits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "test-utils.h"

static const char *argv0;
static gchar *empty_temp_dir;

typedef struct
{
  gchar *srcdir;
  gchar *builddir;
} Fixture;

typedef struct
{
  int unused;
} Config;

typedef struct
{
  const gchar *path;
  const gchar *abi;
} LibInfo;

typedef struct
{
  const gchar *argv[5];
  int exit_status;
  const gchar *stdout_contains;
  const gchar *stderr_contains;
} IdentifyLibraryAbi;

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
_spawn_and_check_output (const IdentifyLibraryAbi *t)
{
  g_autofree gchar *child_stdout = NULL;
  g_autofree gchar *child_stderr = NULL;
  g_autoptr(GError) error = NULL;
  gboolean ret;
  int wait_status = -1;

  ret = g_spawn_sync (NULL,    /* working directory */
                      (gchar **) t->argv,
                      NULL,    /* envp */
                      G_SPAWN_SEARCH_PATH,
                      NULL,    /* child setup */
                      NULL,    /* user data */
                      &child_stdout,
                      &child_stderr,
                      &wait_status,
                      &error);
  g_assert_no_error (error);
  g_assert_true (ret);
  g_assert_true (WIFEXITED (wait_status));
  g_assert_cmpint (WEXITSTATUS (wait_status), ==, t->exit_status);
  g_assert_nonnull (child_stdout);
  g_assert_true (g_utf8_validate (child_stdout, -1, NULL));
  g_assert_nonnull (child_stderr);
  g_assert_true (g_utf8_validate (child_stderr, -1, NULL));
  if (t->stdout_contains != NULL)
    g_assert_cmpstr (strstr (child_stdout, t->stdout_contains), !=, NULL);
  if (t->stderr_contains != NULL)
    g_assert_cmpstr (strstr (child_stderr, t->stderr_contains), !=, NULL);
}

static void
test_arguments_validation (Fixture *f,
                           gconstpointer context)
{
  const IdentifyLibraryAbi identify_lib_abi[] =
  {
    {
      .argv =
      {
        "steam-runtime-identify-library-abi",
        "--ldconfig",
        NULL,
      },
      .exit_status = 0,
    },
    {
      .argv =
      {
        "steam-runtime-identify-library-abi",
        "--ldconfig",
        "--print0",
        NULL,
      },
      .exit_status = 0,
    },
    {
      .argv =
      {
        "steam-runtime-identify-library-abi",
        "--directory",
        empty_temp_dir,
        NULL,
      },
      .exit_status = 0,
    },
    {
      .argv =
      {
        "steam-runtime-identify-library-abi",
        "--this-option-is-unsupported",
        NULL,
      },
      .exit_status = EX_USAGE,
      .stderr_contains = "Unknown option",
    },
    {
      .argv =
      {
        "steam-runtime-identify-library-abi",
        "this-argument-is-unsupported",
        NULL,
      },
      .exit_status = EX_USAGE,
      .stderr_contains = "Either --ldconfig or --directory are required",
    },
    {
      .argv =
      {
        "steam-runtime-identify-library-abi",
        "--ldconfig",
        "--directory",
        empty_temp_dir,
        NULL,
      },
      .exit_status = EX_USAGE,
      .stderr_contains = "cannot be used at the same time",
    },
    {
      .argv =
      {
        "steam-runtime-identify-library-abi",
        NULL,
      },
      .exit_status = EX_USAGE,
      .stderr_contains = "Either --ldconfig or --directory are required",
    },
    {
      .argv =
      {
        "steam-runtime-identify-library-abi",
        "--directory",
        "/this_directory_does_not_exist",
        NULL,
      },
      .exit_status = 1,
      .stderr_contains = "Unable to realpath",
    },
  };

  for (gsize i = 0; i < G_N_ELEMENTS (identify_lib_abi); i++)
    _spawn_and_check_output (&identify_lib_abi[i]);
}

/*
 * Test `steam-runtime-identify-library-abi --help` and `--version`.
 */
static void
test_help_and_version (Fixture *f,
                       gconstpointer context)
{
  const IdentifyLibraryAbi identify_lib_abi[] =
  {
    {
      .argv =
      {
        "env",
        "LC_ALL=C",
        "steam-runtime-identify-library-abi",
        "--version",
        NULL,
      },
      .exit_status = 0,
      .stdout_contains = VERSION,
    },
    {
      .argv =
      {
        "env",
        "LC_ALL=C",
        "steam-runtime-identify-library-abi",
        "--help",
        NULL,
      },
      .exit_status = 0,
      .stdout_contains = "OPTION",
    },
  };

  for (gsize i = 0; i < G_N_ELEMENTS (identify_lib_abi); i++)
    _spawn_and_check_output (&identify_lib_abi[i]);
}

static void
test_library_identification (Fixture *f,
                             gconstpointer context)
{
  gboolean ret;
  int exit_status = -1;
  GError *error = NULL;
  gchar *child_stdout = NULL;
  gchar *child_stderr = NULL;
  gsize i;
  const gchar *argv[] =
  {
    "steam-runtime-identify-library-abi",
    "--ldconfig",
    NULL,
    NULL,
  };

  ret = g_spawn_sync (NULL,    /* working directory */
                      (gchar **) argv,
                      NULL,    /* envp */
                      G_SPAWN_SEARCH_PATH,
                      NULL,    /* child setup */
                      NULL,    /* user data */
                      &child_stdout,
                      &child_stderr,
                      &exit_status,
                      &error);
  g_assert_no_error (error);
  g_assert_true (ret);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_nonnull (child_stdout);
  g_assert_cmpstr (child_stdout, !=, "");
  g_assert_true (g_utf8_validate (child_stdout, -1, NULL));
  g_assert_nonnull (child_stderr);

  const LibInfo libc_info[] =
  {
    {
      .path = "/usr/lib/x86_64-linux-gnu/libc.so.6",
      .abi = "x86_64-linux-gnu",
    },
    {
      .path = "/lib/x86_64-linux-gnu/libc.so.6",
      .abi = "x86_64-linux-gnu",
    },
    {
      .path = "/usr/lib/i386-linux-gnu/libc.so.6",
      .abi = "i386-linux-gnu",
    },
    {
      .path = "/lib/i386-linux-gnu/libc.so.6",
      .abi = "i386-linux-gnu",
    },
  };

  for (i = 0; i < G_N_ELEMENTS (libc_info); i++)
    {
      g_autofree gchar *expected_out_line = NULL;
      gchar *out_line = strstr (child_stdout, libc_info[i].path);
      if (out_line != NULL)
        {
          gchar *end_of_line = strstr (out_line, "\n");
          g_assert_nonnull (end_of_line);
          end_of_line[0] = '\0';
          expected_out_line = g_strdup_printf ("%s=%s", libc_info[i].path, libc_info[i].abi);
          g_assert_cmpstr (out_line, ==, expected_out_line);
          end_of_line[0] = '\n';
        }
      else
        {
          g_test_message ("\"%s\" seems to not be available in ldconfig output, "
                          "skipping this part of the test", libc_info[i].path);
        }
    }

  g_free (child_stdout);
  g_free (child_stderr);

  argv[1] = "--directory";
  for (i = 0; i < G_N_ELEMENTS (libc_info); i++)
    {
      g_autofree gchar *libc_dirname = NULL;
      g_autofree gchar *expected_out_line = NULL;
      const gchar *out_line;
      gchar *end_of_line;  /* not owned */

      if (!g_file_test (libc_info[i].path, G_FILE_TEST_EXISTS))
        {
          g_test_message ("\"%s\" is not available in the filesystem, skipping this "
                          "part of the test", libc_info[i].path);
          continue;
        }

      libc_dirname = g_path_get_dirname (libc_info[i].path);
      argv[2] = libc_dirname;

      ret = g_spawn_sync (NULL,    /* working directory */
                          (gchar **) argv,
                          NULL,    /* envp */
                          G_SPAWN_SEARCH_PATH,
                          NULL,    /* child setup */
                          NULL,    /* user data */
                          &child_stdout,
                          &child_stderr,
                          &exit_status,
                          &error);
      g_assert_no_error (error);
      g_assert_true (ret);
      g_assert_cmpint (exit_status, ==, 0);
      g_assert_nonnull (child_stdout);
      g_assert_cmpstr (child_stdout, !=, "");
      g_assert_true (g_utf8_validate (child_stdout, -1, NULL));
      g_assert_nonnull (child_stderr);

      out_line = strstr (child_stdout, libc_info[i].path);
      g_assert_nonnull (out_line);
      end_of_line = strstr (out_line, "\n");
      g_assert_nonnull (end_of_line);
      end_of_line[0] = '\0';
      expected_out_line = g_strdup_printf ("%s=%s", libc_info[i].path, libc_info[i].abi);
      g_assert_cmpstr (out_line, ==, expected_out_line);
      end_of_line[0] = '\n';

      g_free (child_stdout);
      g_free (child_stderr);
    }
}

int
main (int argc,
      char **argv)
{
  int status;
  GError *error = NULL;

  argv0 = argv[0];

  g_test_init (&argc, &argv, NULL);
  /* Creates an empty temporary directory to test the --directory option */
  empty_temp_dir = g_dir_make_tmp ("empty-dir-XXXXXX", &error);
  g_test_add ("/identify-library-abi-cli/arguments_validation", Fixture, NULL,
              setup, test_arguments_validation, teardown);
  g_test_add ("/identify-library-abi-cli/help-and-version", Fixture, NULL,
              setup, test_help_and_version, teardown);
  g_test_add ("/identify-library-abi-cli/library-identification", Fixture, NULL,
              setup, test_library_identification, teardown);

  status = g_test_run ();

  _srt_rm_rf (empty_temp_dir);
  g_free (empty_temp_dir);

  return status;
}
