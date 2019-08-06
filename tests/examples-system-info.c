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

#include <json-glib/json-glib.h>

#include "test-utils.h"

static const char *argv0;
static const char * const multiarch_tuples[] = { SRT_ABI_I386, SRT_ABI_X86_64 };

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
 * Test if the expected libraries are available in the
 * running system.
 */
static void
libraries_presence (Fixture *f,
                    gconstpointer context)
{
  int exit_status = -1;
  JsonParser *parser = NULL;
  JsonNode *node = NULL;
  JsonObject *json;
  JsonObject *json_arch;
  GError *error = NULL;
  gchar *output = NULL;
  gchar *examples = NULL;
  SrtSystemInfo *info = srt_system_info_new (NULL);
  gchar *expectations_in = g_build_filename (f->srcdir, "expectations", NULL);
  const gchar *argv[] =
    {
      "steam-runtime-system-info", "--expectations", expectations_in, NULL
    };

  g_assert_true (g_spawn_sync (NULL,    /* working directory */
                               (gchar **) argv,
                               NULL,    /* envp */
                               G_SPAWN_SEARCH_PATH,
                               NULL,    /* child setup */
                               NULL,    /* user data */
                               &output, /* stdout */
                               NULL,    /* stderr */
                               &exit_status,
                               &error));
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_null (error);
  g_assert_nonnull (output);

  /* We can't use `json_from_string()` directly because we are targeting an
   * older json-glib version */
  parser = json_parser_new ();
  g_assert_true (json_parser_load_from_data (parser, output, -1, NULL));
  node = json_parser_get_root (parser);
  json = json_node_get_object (node);

  g_assert_true (json_object_has_member (json, "can-write-uinput"));

  g_assert_true (json_object_has_member (json, "architectures"));
  json = json_object_get_object_member (json, "architectures");

  for (gsize i = 0; i < G_N_ELEMENTS (multiarch_tuples); i++)
    {
      g_assert_true (json_object_has_member (json, multiarch_tuples[i]));
      json_arch = json_object_get_object_member (json, multiarch_tuples[i]);
      g_assert_true (json_object_has_member (json_arch, "can-run"));
      g_assert_cmpint (json_object_get_boolean_member (json_arch, "can-run"),
                      ==, srt_system_info_can_run (info, multiarch_tuples[i]));
      g_assert_cmpint (json_object_has_member (json_arch, "library-issues-summary"),
                      ==, srt_system_info_can_run (info, multiarch_tuples[i]));
      if (json_object_has_member (json_arch, "library-issues-summary"))
        {
          JsonArray *array;
          array = json_object_get_array_member (json_arch, "library-issues-summary");
          g_assert_cmpint (json_array_get_length (array), ==, 0);
        }
    }
  
  g_object_unref (parser);
  g_object_unref (info);
  g_free (examples);
  g_free (expectations_in);
  g_free (output);
  g_clear_error (&error);
}

static void
check_libraries_missing (JsonObject *json_arch)
{
  JsonArray *array;
  JsonObject *json_details;
  JsonObject *json_lib;
  if (json_object_has_member (json_arch, "library-issues-summary"))
    {
      array = json_object_get_array_member (json_arch, "library-issues-summary");
      g_assert_cmpint (json_array_get_length (array), ==, 3);
      g_assert_cmpstr (json_array_get_string_element (array, 0), ==, "cannot-load");
      g_assert_cmpstr (json_array_get_string_element (array, 1), ==, "missing-symbols");
      g_assert_cmpstr (json_array_get_string_element (array, 2), ==, "misversioned-symbols");

      g_assert_true (json_object_has_member (json_arch, "library-details"));
      json_details = json_object_get_object_member (json_arch, "library-details");

      g_assert_true (json_object_has_member (json_details, "libgio-MISSING-2.0.so.0"));
      json_lib = json_object_get_object_member (json_details, "libgio-MISSING-2.0.so.0");
      g_assert_true (json_object_has_member (json_lib, "path"));
      g_assert_cmpstr (json_object_get_string_member (json_lib, "path"), ==, NULL);
      g_assert_true (json_object_has_member (json_lib, "issues"));
      array = json_object_get_array_member (json_lib, "issues");
      g_assert_cmpint (json_array_get_length (array), ==, 1);
      g_assert_cmpstr (json_array_get_string_element (array, 0), ==, "cannot-load");
      g_assert_false (json_object_has_member (json_lib, "missing-symbols"));
      g_assert_false (json_object_has_member (json_lib, "misversioned-symbols"));

      g_assert_true (json_object_has_member (json_details, "libz.so.1"));
      json_lib = json_object_get_object_member (json_details, "libz.so.1");
      g_assert_true (json_object_has_member (json_lib, "path"));
      g_assert_cmpstr (json_object_get_string_member (json_lib, "path"), !=, NULL);
      g_assert_true (json_object_has_member (json_lib, "issues"));
      array = json_object_get_array_member (json_lib, "issues");
      g_assert_cmpint (json_array_get_length (array), ==, 2);
      g_assert_cmpstr (json_array_get_string_element (array, 0), ==, "missing-symbols");
      g_assert_cmpstr (json_array_get_string_element (array, 1), ==, "misversioned-symbols");
      array = json_object_get_array_member (json_lib, "missing-symbols");
      g_assert_cmpint (json_array_get_length (array), ==, 1);
      g_assert_cmpstr (json_array_get_string_element (array, 0), ==, "missing@NotAvailable");
      array = json_object_get_array_member (json_lib, "misversioned-symbols");
      g_assert_cmpint (json_array_get_length (array), ==, 1);
      g_assert_cmpstr (json_array_get_string_element (array, 0), ==, "crc32@WRONG_VERSION");
    }
}

/*
 * Test libraries that are either not available or with missing and
 * misversioned symbols.
 */
static void
libraries_missing (Fixture *f,
                   gconstpointer context)
{
  int exit_status = -1;
  JsonParser *parser = NULL;
  JsonNode *node = NULL;
  JsonObject *json;
  JsonObject *json_arch;
  GError *error = NULL;
  gchar *output = NULL;
  gchar *examples = NULL;
  SrtSystemInfo *info = srt_system_info_new (NULL);
  gchar *expectations_in = g_build_filename (f->srcdir, "expectations_with_missings", NULL);
  const gchar *argv[] =
    {
      "steam-runtime-system-info", "--expectations", expectations_in, NULL
    };

  g_assert_true (g_spawn_sync (NULL,    /* working directory */
                               (gchar **) argv,
                               NULL,    /* envp */
                               G_SPAWN_SEARCH_PATH,
                               NULL,    /* child setup */
                               NULL,    /* user data */
                               &output, /* stdout */
                               NULL,    /* stderr */
                               &exit_status,
                               &error));
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_null (error);
  g_assert_nonnull (output);

  /* We can't use `json_from_string()` directly because we are targeting an
   * older json-glib version */
  parser = json_parser_new ();
  g_assert_true (json_parser_load_from_data (parser, output, -1, NULL));
  node = json_parser_get_root (parser);
  json = json_node_get_object (node);

  g_assert_true (json_object_has_member (json, "can-write-uinput"));
  
  g_assert_true (json_object_has_member (json, "architectures"));
  json = json_object_get_object_member (json, "architectures");

  for (gsize i = 0; i < G_N_ELEMENTS (multiarch_tuples); i++)
    {
      g_assert_true (json_object_has_member (json, multiarch_tuples[i]));
      json_arch = json_object_get_object_member (json, multiarch_tuples[i]);
      g_assert_true (json_object_has_member (json_arch, "can-run"));
      g_assert_cmpint (json_object_get_boolean_member (json_arch, "can-run"),
                      ==, srt_system_info_can_run (info, multiarch_tuples[i]));
      g_assert_cmpint (json_object_has_member (json_arch, "library-issues-summary"),
                      ==, srt_system_info_can_run (info, multiarch_tuples[i]));

      check_libraries_missing (json_arch);
    }
  
  g_object_unref (parser);
  g_object_unref (info);
  g_free (examples);
  g_free (expectations_in);
  g_free (output);
  g_clear_error (&error);
}

static void
check_library_no_errors (JsonObject *json_details,
                         const char *library_soname)
{
  JsonObject *json_lib;

  g_assert_true (json_object_has_member (json_details, library_soname));
  json_lib = json_object_get_object_member (json_details, library_soname);
  g_assert_true (json_object_has_member (json_lib, "path"));
  g_assert_cmpstr (json_object_get_string_member (json_lib, "path"), !=, NULL);
  g_assert_false (json_object_has_member (json_lib, "issues"));
  g_assert_false (json_object_has_member (json_lib, "missing-symbols"));
  g_assert_false (json_object_has_member (json_lib, "misversioned-symbols"));
}

static void
check_libraries_verbose (JsonObject *json_arch)
{
  JsonArray *array;
  JsonObject *json_details;
  if (json_object_has_member (json_arch, "library-issues-summary"))
    {
      array = json_object_get_array_member (json_arch, "library-issues-summary");
      g_assert_cmpint (json_array_get_length (array), ==, 0);

      g_assert_true (json_object_has_member (json_arch, "library-details"));
      json_details = json_object_get_object_member (json_arch, "library-details");

      check_library_no_errors (json_details, "libgio-2.0.so.0");

      check_library_no_errors (json_details, "libglib-2.0.so.0");

      check_library_no_errors (json_details, "libz.so.1");
    }
}

/*
 * Test the presence of libraries with the verbose option
 */
static void
libraries_presence_verbose (Fixture *f,
                            gconstpointer context)
{
  int exit_status = -1;
  JsonParser *parser = NULL;
  JsonNode *node = NULL;
  JsonObject *json;
  JsonObject *json_arch;
  GError *error = NULL;
  gchar *output = NULL;
  gchar *examples = NULL;
  SrtSystemInfo *info = srt_system_info_new (NULL);
  gchar *expectations_in = g_build_filename (f->srcdir, "expectations", NULL);
  const gchar *argv[] =
    {
      "steam-runtime-system-info", "--expectations", expectations_in, "--verbose", NULL
    };

  g_assert_true (g_spawn_sync (NULL,    /* working directory */
                               (gchar **) argv,
                               NULL,    /* envp */
                               G_SPAWN_SEARCH_PATH,
                               NULL,    /* child setup */
                               NULL,    /* user data */
                               &output, /* stdout */
                               NULL,    /* stderr */
                               &exit_status,
                               &error));
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_null (error);
  g_assert_nonnull (output);

  /* We can't use `json_from_string()` directly because we are targeting an
   * older json-glib version */
  parser = json_parser_new ();
  g_assert_true (json_parser_load_from_data (parser, output, -1, NULL));
  node = json_parser_get_root (parser);
  json = json_node_get_object (node);

  g_assert_true (json_object_has_member (json, "can-write-uinput"));
  
  g_assert_true (json_object_has_member (json, "architectures"));
  json = json_object_get_object_member (json, "architectures");

  for (gsize i = 0; i < G_N_ELEMENTS (multiarch_tuples); i++)
    {
      g_assert_true (json_object_has_member (json, multiarch_tuples[i]));
      json_arch = json_object_get_object_member (json, multiarch_tuples[i]);
      g_assert_true (json_object_has_member (json_arch, "can-run"));
      g_assert_cmpint (json_object_get_boolean_member (json_arch, "can-run"),
                      ==, srt_system_info_can_run (info, multiarch_tuples[i]));
      g_assert_cmpint (json_object_has_member (json_arch, "library-issues-summary"),
                      ==, srt_system_info_can_run (info, multiarch_tuples[i]));

      check_libraries_verbose (json_arch);
    }
  
  g_object_unref (parser);
  g_object_unref (info);
  g_free (examples);
  g_free (expectations_in);
  g_free (output);
  g_clear_error (&error);
}

/*
 * Test `steam-runtime-system-info` with no additional arguments
 */
static void
no_arguments (Fixture *f,
              gconstpointer context)
{
  int exit_status = -1;
  JsonParser *parser = NULL;
  JsonNode *node = NULL;
  JsonObject *json;
  JsonObject *json_arch;
  GError *error = NULL;
  gchar *output = NULL;
  gchar *examples = NULL;
  SrtSystemInfo *info = srt_system_info_new (NULL);
  gchar *expectations_in = g_build_filename (f->srcdir, "expectations", NULL);
  const gchar *argv[] = { "steam-runtime-system-info", NULL };

  g_assert_true (g_spawn_sync (NULL,    /* working directory */
                               (gchar **) argv,
                               NULL,    /* envp */
                               G_SPAWN_SEARCH_PATH,
                               NULL,    /* child setup */
                               NULL,    /* user data */
                               &output, /* stdout */
                               NULL,    /* stderr */
                               &exit_status,
                               &error));
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_null (error);
  g_assert_nonnull (output);

  /* We can't use `json_from_string()` directly because we are targeting an
   * older json-glib version */
  parser = json_parser_new ();
  g_assert_true (json_parser_load_from_data (parser, output, -1, NULL));
  node = json_parser_get_root (parser);
  json = json_node_get_object (node);

  g_assert_true (json_object_has_member (json, "can-write-uinput"));
  
  g_assert_true (json_object_has_member (json, "architectures"));
  json = json_object_get_object_member (json, "architectures");

  for (gsize i = 0; i < G_N_ELEMENTS (multiarch_tuples); i++)
    {
      g_assert_true (json_object_has_member (json, multiarch_tuples[i]));
      json_arch = json_object_get_object_member (json, multiarch_tuples[i]);
      g_assert_true (json_object_has_member (json_arch, "can-run"));
      g_assert_cmpint (json_object_get_boolean_member (json_arch, "can-run"),
                      ==, srt_system_info_can_run (info, multiarch_tuples[i]));
    }
  
  g_object_unref (parser);
  g_object_unref (info);
  g_free (examples);
  g_free (expectations_in);
  g_free (output);
  g_clear_error (&error);
}

int
main (int argc,
      char **argv)
{
  argv0 = argv[0];

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/system-info/libraries_presence", Fixture, NULL,
              setup, libraries_presence, teardown);
  g_test_add ("/system-info/libraries_missing", Fixture, NULL,
              setup, libraries_missing, teardown);
  g_test_add ("/system-info/libraries_presence_verbose", Fixture, NULL,
              setup, libraries_presence_verbose, teardown);
  g_test_add ("/system-info/no_arguments", Fixture, NULL,
              setup, no_arguments, teardown);

  return g_test_run ();
}
