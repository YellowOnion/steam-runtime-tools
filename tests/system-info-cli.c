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
#include "fake-home.h"

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
  gboolean result;
  int exit_status = -1;
  JsonParser *parser = NULL;
  JsonNode *node = NULL;
  JsonObject *json;
  JsonObject *json_arch;
  GError *error = NULL;
  gchar *output = NULL;
  SrtSystemInfo *info = srt_system_info_new (NULL);
  gchar *expectations_in = g_build_filename (f->srcdir, "expectations", NULL);
  const gchar *argv[] =
    {
      "steam-runtime-system-info", "--expectations", expectations_in, NULL
    };

  result = g_spawn_sync (NULL,    /* working directory */
                         (gchar **) argv,
                         NULL,    /* envp */
                         G_SPAWN_SEARCH_PATH,
                         NULL,    /* child setup */
                         NULL,    /* user data */
                         &output, /* stdout */
                         NULL,    /* stderr */
                         &exit_status,
                         &error);
  g_assert_no_error (error);
  g_assert_true (result);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_nonnull (output);

  /* We can't use `json_from_string()` directly because we are targeting an
   * older json-glib version */
  parser = json_parser_new ();
  result = json_parser_load_from_data (parser, output, -1, &error);
  g_assert_no_error (error);
  g_assert_true (result);
  node = json_parser_get_root (parser);
  json = json_node_get_object (node);

  g_assert_true (json_object_has_member (json, "can-write-uinput"));

  g_assert_true (json_object_has_member (json, "driver_environment"));

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
      g_assert_true (json_object_has_member (json_arch, "dri_drivers"));
      g_assert_true (json_object_has_member (json_arch, "va-api_drivers"));
      g_assert_true (json_object_has_member (json_arch, "vdpau_drivers"));
      g_assert_true (json_object_has_member (json_arch, "glx_drivers"));
    }

  g_object_unref (parser);
  g_object_unref (info);
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
      /* We don't assert about the exact contents of stderr, just that there was some */
      g_assert_true (json_object_has_member (json_lib, "messages"));
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
  gboolean result;
  int exit_status = -1;
  JsonParser *parser = NULL;
  JsonNode *node = NULL;
  JsonObject *json;
  JsonObject *json_arch;
  GError *error = NULL;
  gchar *output = NULL;
  SrtSystemInfo *info = srt_system_info_new (NULL);
  gchar *expectations_in = g_build_filename (f->srcdir, "expectations_with_missings", NULL);
  const gchar *argv[] =
    {
      "steam-runtime-system-info", "--expectations", expectations_in, NULL
    };

  result = g_spawn_sync (NULL,    /* working directory */
                         (gchar **) argv,
                         NULL,    /* envp */
                         G_SPAWN_SEARCH_PATH,
                         NULL,    /* child setup */
                         NULL,    /* user data */
                         &output, /* stdout */
                         NULL,    /* stderr */
                         &exit_status,
                         &error);
  g_assert_no_error (error);
  g_assert_true (result);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_nonnull (output);

  /* We can't use `json_from_string()` directly because we are targeting an
   * older json-glib version */
  parser = json_parser_new ();
  result = json_parser_load_from_data (parser, output, -1, &error);
  g_assert_no_error (error);
  g_assert_true (result);
  node = json_parser_get_root (parser);
  json = json_node_get_object (node);

  g_assert_true (json_object_has_member (json, "can-write-uinput"));

  g_assert_true (json_object_has_member (json, "driver_environment"));

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
      g_assert_true (json_object_has_member (json_arch, "dri_drivers"));
      g_assert_true (json_object_has_member (json_arch, "va-api_drivers"));
      g_assert_true (json_object_has_member (json_arch, "vdpau_drivers"));
      g_assert_true (json_object_has_member (json_arch, "glx_drivers"));

      check_libraries_missing (json_arch);
    }

  g_object_unref (parser);
  g_object_unref (info);
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
  g_assert_false (json_object_has_member (json_lib, "messages"));
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
  gboolean result;
  int exit_status = -1;
  JsonParser *parser = NULL;
  JsonNode *node = NULL;
  JsonObject *json;
  JsonObject *json_arch;
  GError *error = NULL;
  gchar *output = NULL;
  SrtSystemInfo *info = srt_system_info_new (NULL);
  gchar *expectations_in = g_build_filename (f->srcdir, "expectations", NULL);
  const gchar *argv[] =
    {
      /* We assert that there was nothing on stderr, so don't let
       * g_debug() break that assumption */
      "env", "G_MESSAGES_DEBUG=",
      "steam-runtime-system-info", "--expectations", expectations_in, "--verbose", NULL
    };

  result = g_spawn_sync (NULL,    /* working directory */
                         (gchar **) argv,
                         NULL,    /* envp */
                         G_SPAWN_SEARCH_PATH,
                         NULL,    /* child setup */
                         NULL,    /* user data */
                         &output, /* stdout */
                         NULL,    /* stderr */
                         &exit_status,
                         &error);
  g_assert_no_error (error);
  g_assert_true (result);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_nonnull (output);

  /* We can't use `json_from_string()` directly because we are targeting an
   * older json-glib version */
  parser = json_parser_new ();
  result = json_parser_load_from_data (parser, output, -1, &error);
  g_assert_no_error (error);
  g_assert_true (result);
  node = json_parser_get_root (parser);
  json = json_node_get_object (node);

  g_assert_true (json_object_has_member (json, "can-write-uinput"));

  g_assert_true (json_object_has_member (json, "steam-installation"));

  g_assert_true (json_object_has_member (json, "runtime"));

  g_assert_true (json_object_has_member (json, "driver_environment"));

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
      g_assert_true (json_object_has_member (json_arch, "dri_drivers"));
      g_assert_true (json_object_has_member (json_arch, "va-api_drivers"));
      g_assert_true (json_object_has_member (json_arch, "vdpau_drivers"));
      g_assert_true (json_object_has_member (json_arch, "glx_drivers"));

      check_libraries_verbose (json_arch);
    }

  g_object_unref (parser);
  g_object_unref (info);
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
  gboolean result;
  int exit_status = -1;
  JsonParser *parser = NULL;
  JsonNode *node = NULL;
  JsonObject *json;
  JsonObject *json_arch;
  GError *error = NULL;
  gchar *output = NULL;
  SrtSystemInfo *info = srt_system_info_new (NULL);
  const gchar *argv[] = { "steam-runtime-system-info", NULL };

  result = g_spawn_sync (NULL,    /* working directory */
                         (gchar **) argv,
                         NULL,    /* envp */
                         G_SPAWN_SEARCH_PATH,
                         NULL,    /* child setup */
                         NULL,    /* user data */
                         &output, /* stdout */
                         NULL,    /* stderr */
                         &exit_status,
                         &error);
  g_assert_no_error (error);
  g_assert_true (result);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_nonnull (output);

  /* We can't use `json_from_string()` directly because we are targeting an
   * older json-glib version */
  parser = json_parser_new ();
  result = json_parser_load_from_data (parser, output, -1, &error);
  g_assert_no_error (error);
  g_assert_true (result);
  node = json_parser_get_root (parser);
  json = json_node_get_object (node);

  g_assert_true (json_object_has_member (json, "can-write-uinput"));

  g_assert_true (json_object_has_member (json, "driver_environment"));

  g_assert_true (json_object_has_member (json, "architectures"));
  json = json_object_get_object_member (json, "architectures");

  for (gsize i = 0; i < G_N_ELEMENTS (multiarch_tuples); i++)
    {
      g_assert_true (json_object_has_member (json, multiarch_tuples[i]));
      json_arch = json_object_get_object_member (json, multiarch_tuples[i]);
      g_assert_true (json_object_has_member (json_arch, "can-run"));
      g_assert_cmpint (json_object_get_boolean_member (json_arch, "can-run"),
                      ==, srt_system_info_can_run (info, multiarch_tuples[i]));
      g_assert_true (json_object_has_member (json_arch, "dri_drivers"));
      g_assert_true (json_object_has_member (json_arch, "va-api_drivers"));
      g_assert_true (json_object_has_member (json_arch, "vdpau_drivers"));
      g_assert_true (json_object_has_member (json_arch, "glx_drivers"));
    }

  g_object_unref (parser);
  g_object_unref (info);
  g_free (output);
  g_clear_error (&error);
}

/*
 * Test a system with a good Steam installation.
 */
static void
steam_presence (Fixture *f,
                gconstpointer context)
{
  gboolean result;
  int exit_status = -1;
  JsonParser *parser = NULL;
  JsonNode *node = NULL;
  JsonObject *json;
  JsonObject *json_sub_object;
  JsonArray *array;
  GError *error = NULL;
  gchar *output = NULL;
  const gchar *path = NULL;
  const gchar *version = NULL;
  SrtSystemInfo *info = srt_system_info_new (NULL);
  const gchar *argv[] = { "steam-runtime-system-info", NULL };
  FakeHome *fake_home;

  fake_home = fake_home_new (NULL);
  fake_home_create_structure (fake_home);

  result = g_spawn_sync (NULL,    /* working directory */
                         (gchar **) argv,
                         fake_home->env, /* envp */
                         G_SPAWN_SEARCH_PATH,
                         NULL,    /* child setup */
                         NULL,    /* user data */
                         &output, /* stdout */
                         NULL,    /* stderr */
                         &exit_status,
                         &error);
  g_assert_no_error (error);
  g_assert_true (result);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_nonnull (output);

  /* We can't use `json_from_string()` directly because we are targeting an
   * older json-glib version */
  parser = json_parser_new ();
  result = json_parser_load_from_data (parser, output, -1, &error);
  g_assert_no_error (error);
  g_assert_true (result);
  node = json_parser_get_root (parser);
  json = json_node_get_object (node);

  g_assert_true (json_object_has_member (json, "can-write-uinput"));

  g_assert_true (json_object_has_member (json, "steam-installation"));
  json_sub_object = json_object_get_object_member (json, "steam-installation");

  g_assert_true (json_object_has_member (json_sub_object, "path"));
  path = json_object_get_string_member (json_sub_object, "path");
  g_assert_cmpstr (path, !=, NULL);
  g_assert_true (path[0] == '/');

  g_assert_true (json_object_has_member (json_sub_object, "issues"));
  array = json_object_get_array_member (json_sub_object, "issues");
  g_assert_cmpint (json_array_get_length (array), ==, 0);

  g_assert_true (json_object_has_member (json, "runtime"));
  json_sub_object = json_object_get_object_member (json, "runtime");

  g_assert_true (json_object_has_member (json_sub_object, "path"));
  path = json_object_get_string_member (json_sub_object, "path");
  g_assert_cmpstr (path, !=, NULL);
  g_assert_true (path[0] == '/');

  g_assert_true (json_object_has_member (json_sub_object, "version"));
  version = json_object_get_string_member (json_sub_object, "version");
  g_assert_cmpstr (version, !=, NULL);

  g_assert_true (json_object_has_member (json_sub_object, "issues"));
  array = json_object_get_array_member (json_sub_object, "issues");
  g_assert_cmpint (json_array_get_length (array), ==, 0);

  g_assert_false (json_object_has_member (json_sub_object, "overrides"));
  g_assert_true (json_object_has_member (json_sub_object, "pinned_libs_32"));
  g_assert_true (json_object_has_member (json_sub_object, "pinned_libs_64"));

  g_assert_true (json_object_has_member (json, "driver_environment"));

  g_assert_true (json_object_has_member (json, "architectures"));

  fake_home_clean_up (fake_home);
  g_object_unref (parser);
  g_object_unref (info);
  g_free (output);
  g_clear_error (&error);
}

/*
 * Test a system with a Steam installation with issues.
 */
static void
steam_issues (Fixture *f,
                gconstpointer context)
{
  gboolean result;
  int exit_status = -1;
  JsonParser *parser = NULL;
  JsonNode *node = NULL;
  JsonObject *json;
  JsonObject *json_sub_object;
  JsonArray *array;
  GError *error = NULL;
  gchar *output = NULL;
  const gchar *path = NULL;
  const gchar *version = NULL;
  SrtSystemInfo *info = srt_system_info_new (NULL);
  const gchar *argv[] = { "steam-runtime-system-info", NULL };
  FakeHome *fake_home;

  fake_home = fake_home_new (NULL);
  fake_home->create_pinning_libs = FALSE;
  fake_home->create_steam_symlink = FALSE;
  fake_home->create_steamrt_files = FALSE;
  fake_home_create_structure (fake_home);

  result = g_spawn_sync (NULL,    /* working directory */
                         (gchar **) argv,
                         fake_home->env, /* envp */
                         G_SPAWN_SEARCH_PATH,
                         NULL,    /* child setup */
                         NULL,    /* user data */
                         &output, /* stdout */
                         NULL,    /* stderr */
                         &exit_status,
                         &error);
  g_assert_no_error (error);
  g_assert_true (result);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_nonnull (output);

  /* We can't use `json_from_string()` directly because we are targeting an
   * older json-glib version */
  parser = json_parser_new ();
  result = json_parser_load_from_data (parser, output, -1, &error);
  g_assert_no_error (error);
  g_assert_true (result);
  node = json_parser_get_root (parser);
  json = json_node_get_object (node);

  g_assert_true (json_object_has_member (json, "can-write-uinput"));


  g_assert_true (json_object_has_member (json, "steam-installation"));
  json_sub_object = json_object_get_object_member (json, "steam-installation");

  g_assert_true (json_object_has_member (json_sub_object, "path"));
  path = json_object_get_string_member (json_sub_object, "path");
  g_assert_cmpstr (path, !=, NULL);
  g_assert_true (path[0] == '/');

  g_assert_true (json_object_has_member (json_sub_object, "issues"));
  array = json_object_get_array_member (json_sub_object, "issues");
  g_assert_cmpint (json_array_get_length (array), ==, 2);
  g_assert_cmpstr (json_array_get_string_element (array, 0), ==,
                   "dot-steam-steam-not-symlink");
  g_assert_cmpstr (json_array_get_string_element (array, 1), ==,
                   "dot-steam-steam-not-directory");

  g_assert_true (json_object_has_member (json, "runtime"));
  json_sub_object = json_object_get_object_member (json, "runtime");

  g_assert_true (json_object_has_member (json_sub_object, "path"));
  path = json_object_get_string_member (json_sub_object, "path");
  g_assert_cmpstr (path, !=, NULL);
  g_assert_true (path[0] == '/');

  g_assert_true (json_object_has_member (json_sub_object, "version"));
  version = json_object_get_string_member (json_sub_object, "version");
  g_assert_cmpstr (version, ==, NULL);

  g_assert_true (json_object_has_member (json_sub_object, "issues"));
  array = json_object_get_array_member (json_sub_object, "issues");
  g_assert_cmpint (json_array_get_length (array), ==, 2);
  g_assert_cmpstr (json_array_get_string_element (array, 0), ==,
                   "not-runtime");
  g_assert_cmpstr (json_array_get_string_element (array, 1), ==,
                   "not-using-newer-host-libraries");

  g_assert_true (json_object_has_member (json, "driver_environment"));

  g_assert_true (json_object_has_member (json, "architectures"));

  fake_home_clean_up (fake_home);
  g_object_unref (parser);
  g_object_unref (info);
  g_free (output);
  g_clear_error (&error);
}

/*
 * Test `steam-runtime-system-info --help` and `--version`.
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
      "steam-runtime-system-info",
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
  g_assert_nonnull (diagnostics);

  g_assert_nonnull (strstr (output, "OPTIONS"));

  g_free (output);
  g_free (diagnostics);
  g_clear_error (&error);
}

/*
 * Make sure it works when run by Steam.
 */
static void
test_unblocks_sigchld (Fixture *f,
                       gconstpointer context)
{
  gboolean result;
  int exit_status = -1;
  JsonParser *parser = NULL;
  JsonNode *node = NULL;
  JsonObject *json;
  JsonObject *json_arch;
  GError *error = NULL;
  gchar *output = NULL;
  SrtSystemInfo *info = srt_system_info_new (NULL);
  gchar *adverb = g_build_filename (f->builddir, "adverb", NULL);
  gchar *expectations_in = g_build_filename (f->srcdir, "expectations", NULL);
  const gchar *argv[] =
    {
      adverb,
      "--ignore-sigchld",
      "--block-sigchld",
      "--",
      "env",
      "G_DEBUG=fatal_criticals",
      "steam-runtime-system-info",
      "--expectations",
      expectations_in,
      NULL
    };

  result = g_spawn_sync (NULL,    /* working directory */
                         (gchar **) argv,
                         NULL,    /* envp */
                         G_SPAWN_SEARCH_PATH,
                         NULL,    /* child setup */
                         NULL,    /* user data */
                         &output, /* stdout */
                         NULL,    /* stderr */
                         &exit_status,
                         &error);
  g_assert_no_error (error);
  g_assert_true (result);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_nonnull (output);

  parser = json_parser_new ();
  result = json_parser_load_from_data (parser, output, -1, &error);
  g_assert_no_error (error);
  g_assert_true (result);
  node = json_parser_get_root (parser);
  json = json_node_get_object (node);

  g_assert_true (json_object_has_member (json, "can-write-uinput"));

  g_assert_true (json_object_has_member (json, "driver_environment"));

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
  g_free (output);
  g_clear_error (&error);
  g_free (adverb);
  g_free (expectations_in);
}

int
main (int argc,
      char **argv)
{
  argv0 = argv[0];

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/system-info-cli/libraries_presence", Fixture, NULL,
              setup, libraries_presence, teardown);
  g_test_add ("/system-info-cli/libraries_missing", Fixture, NULL,
              setup, libraries_missing, teardown);
  g_test_add ("/system-info-cli/libraries_presence_verbose", Fixture, NULL,
              setup, libraries_presence_verbose, teardown);
  g_test_add ("/system-info-cli/no_arguments", Fixture, NULL,
              setup, no_arguments, teardown);
  g_test_add ("/system-info-cli/steam_presence", Fixture, NULL,
              setup, steam_presence, teardown);
  g_test_add ("/system-info-cli/steam_issues", Fixture, NULL,
              setup, steam_issues, teardown);
  g_test_add ("/system-info-cli/help-and-version", Fixture, NULL,
              setup, test_help_and_version, teardown);
  g_test_add ("/system-info-cli/unblocks_sigchld", Fixture, NULL,
              setup, test_unblocks_sigchld, teardown);

  return g_test_run ();
}
