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

#include <libglnx.h>

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <steam-runtime-tools/glib-backports-internal.h>
#include <steam-runtime-tools/json-glib-backports-internal.h>
#include <steam-runtime-tools/utils-internal.h>

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

#if defined(__i386__) || defined(__x86_64__)
  static const char * const multiarch_tuples[] = { SRT_ABI_I386, SRT_ABI_X86_64 };
#elif defined(_SRT_MULTIARCH)
  static const char * const multiarch_tuples[] = { _SRT_MULTIARCH };
#else
#warning Unknown architecture, assuming x86
  static const char * const multiarch_tuples[] = { SRT_ABI_I386, SRT_ABI_X86_64 };
#endif

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
 * Test if the expected libraries are available in the
 * running system.
 */
static void
libraries_presence (Fixture *f,
                    gconstpointer context)
{
  gboolean result;
  int exit_status = -1;
  g_autoptr(JsonNode) node = NULL;
  JsonObject *json;
  JsonObject *json_arch;
  JsonObject *json_graphics;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *output = NULL;
  g_autoptr(SrtSystemInfo) info = srt_system_info_new (NULL);
  g_autofree gchar *expectations_in = g_build_filename (f->srcdir, "expectations", NULL);
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

  node = json_from_string (output, &error);
  g_assert_no_error (error);
  g_assert_nonnull (node);
  json = json_node_get_object (node);

  g_assert_true (json_object_has_member (json, "can-write-uinput"));

  g_assert_true (json_object_has_member (json, "driver_environment"));

  g_assert_true (json_object_has_member (json, "cpu-features"));

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

      g_assert_true (json_object_has_member (json_arch, "graphics-details"));
      json_graphics = json_object_get_object_member (json_arch, "graphics-details");
      g_assert_true (json_object_has_member (json_graphics, "x11/vulkan"));
      g_assert_true (json_object_has_member (json_graphics, "x11/vdpau"));
      g_assert_true (json_object_has_member (json_graphics, "x11/vaapi"));
      g_assert_true (json_object_has_member (json_graphics, "glx/gl"));
      g_assert_true (json_object_has_member (json_graphics, "egl_x11/gl"));
      g_assert_true (json_object_has_member (json_graphics, "egl_x11/glesv2"));
    }
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
  g_autoptr(JsonNode) node = NULL;
  JsonObject *json;
  JsonObject *json_arch;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *output = NULL;
  g_autoptr(SrtSystemInfo) info = srt_system_info_new (NULL);
  g_autofree gchar *expectations_in = g_build_filename (f->srcdir, "expectations_with_missings", NULL);
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

  node = json_from_string (output, &error);
  g_assert_no_error (error);
  g_assert_nonnull (node);
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
  g_autoptr(JsonNode) node = NULL;
  JsonObject *json;
  JsonObject *json_arch;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *output = NULL;
  g_autoptr(SrtSystemInfo) info = srt_system_info_new (NULL);
  g_autofree gchar *expectations_in = g_build_filename (f->srcdir, "expectations", NULL);
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

  node = json_from_string (output, &error);
  g_assert_no_error (error);
  g_assert_nonnull (node);
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
  g_autoptr(JsonNode) node = NULL;
  JsonObject *json;
  JsonObject *json_arch;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *output = NULL;
  g_autoptr(SrtSystemInfo) info = srt_system_info_new (NULL);
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

  node = json_from_string (output, &error);
  g_assert_no_error (error);
  g_assert_nonnull (node);
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
  g_autoptr(JsonNode) node = NULL;
  JsonObject *json;
  JsonObject *json_sub_object;
  JsonArray *array;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *output = NULL;
  const gchar *path = NULL;
  const gchar *script_path = NULL;
  const gchar *script_path_member = NULL;
  const gchar *script_version = NULL;
  const gchar *version = NULL;
  const gchar *argv[] = { "steam-runtime-system-info", NULL };
  FakeHome *fake_home;

  fake_home = fake_home_new (NULL);
  fake_home_create_structure (fake_home);

  /* We expect `fake_home_new` to already set 'STEAMSCRIPT' */
  script_path = g_environ_getenv (fake_home->env, "STEAMSCRIPT");
  g_assert_cmpstr (script_path, !=, NULL);

  fake_home->env = g_environ_setenv (fake_home->env, "STEAMSCRIPT_VERSION",
                                     "1.0.0.66", TRUE);

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

  node = json_from_string (output, &error);
  g_assert_no_error (error);
  g_assert_nonnull (node);
  json = json_node_get_object (node);

  g_assert_true (json_object_has_member (json, "can-write-uinput"));

  g_assert_true (json_object_has_member (json, "steam-installation"));
  json_sub_object = json_object_get_object_member (json, "steam-installation");

  g_assert_true (json_object_has_member (json_sub_object, "path"));
  path = json_object_get_string_member (json_sub_object, "path");
  g_assert_cmpstr (path, !=, NULL);
  g_assert_true (path[0] == '/');

  g_assert_true (json_object_has_member (json_sub_object, "steamscript_path"));
  script_path_member = json_object_get_string_member (json_sub_object, "steamscript_path");
  g_assert_cmpstr (script_path_member, ==, script_path);

  g_assert_true (json_object_has_member (json_sub_object, "steamscript_version"));
  script_version = json_object_get_string_member (json_sub_object, "steamscript_version");
  g_assert_cmpstr (script_version, ==, "1.0.0.66");

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
  g_autoptr(JsonNode) node = NULL;
  JsonObject *json;
  JsonObject *json_sub_object;
  JsonArray *array;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *output = NULL;
  const gchar *path = NULL;
  const gchar *script_path = NULL;
  const gchar *script_version = NULL;
  const gchar *version = NULL;
  const gchar *argv[] = { "steam-runtime-system-info", NULL };
  FakeHome *fake_home;

  fake_home = fake_home_new (NULL);
  fake_home->create_pinning_libs = FALSE;
  fake_home->create_steam_symlink = FALSE;
  fake_home->create_steamrt_files = FALSE;
  fake_home_create_structure (fake_home);

  fake_home->env = g_environ_unsetenv (fake_home->env, "STEAMSCRIPT");

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

  node = json_from_string (output, &error);
  g_assert_no_error (error);
  g_assert_nonnull (node);
  json = json_node_get_object (node);

  g_assert_true (json_object_has_member (json, "can-write-uinput"));


  g_assert_true (json_object_has_member (json, "steam-installation"));
  json_sub_object = json_object_get_object_member (json, "steam-installation");

  g_assert_true (json_object_has_member (json_sub_object, "path"));
  path = json_object_get_string_member (json_sub_object, "path");
  g_assert_cmpstr (path, !=, NULL);
  g_assert_true (path[0] == '/');

  g_assert_true (json_object_has_member (json_sub_object, "steamscript_path"));
  script_path = json_object_get_string_member (json_sub_object, "steamscript_path");
  g_assert_cmpstr (script_path, ==, NULL);

  g_assert_true (json_object_has_member (json_sub_object, "steamscript_version"));
  script_version = json_object_get_string_member (json_sub_object, "steamscript_version");
  g_assert_cmpstr (script_version, ==, NULL);

  g_assert_true (json_object_has_member (json_sub_object, "issues"));
  array = json_object_get_array_member (json_sub_object, "issues");
  g_assert_cmpint (json_array_get_length (array), ==, 4);
  g_assert_cmpstr (json_array_get_string_element (array, 0), ==,
                   "dot-steam-steam-not-symlink");
  g_assert_cmpstr (json_array_get_string_element (array, 1), ==,
                   "dot-steam-steam-not-directory");
  g_assert_cmpstr (json_array_get_string_element (array, 2), ==,
                   "steamscript-not-in-environment");
  /* This is caused by the missing steamscript */
  g_assert_cmpstr (json_array_get_string_element (array, 3), ==,
                   "unexpected-steam-uri-handler");

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
}

typedef struct
{
  const gchar *description;
  const gchar *input_name;
  const gchar *output_name;
} JsonTest;

static const JsonTest json_test[] =
{
  {
    .description = "full JSON parsing",
    .input_name = "full-good-report.json",
    .output_name = "full-good-report.json",
  },
};

static void
stdout_to_stderr_child_setup (gpointer nil)
{
  if (dup2 (STDERR_FILENO, STDOUT_FILENO) != STDOUT_FILENO)
    _srt_async_signal_safe_error ("Unable to redirect stdout to stderr", 1);
}

static void
json_parsing (Fixture *f,
              gconstpointer context)
{
  for (gsize i = 0; i < G_N_ELEMENTS (json_test); i++)
    {
      const JsonTest *test = &json_test[i];
      gboolean result;
      int exit_status = -1;
      g_autofree gchar *input_json = NULL;
      g_autofree gchar *output_json = NULL;
      g_autofree gchar *output = NULL;
      g_autofree gchar *expectation = NULL;
      g_autofree gchar *generated = NULL;
      g_auto(GStrv) envp = NULL;
      g_autoptr(GError) error = NULL;
      const gchar *argv[] = { "steam-runtime-system-info", NULL };
      g_autoptr(JsonParser) parser = NULL;
      JsonNode *node = NULL;  /* not owned */
      g_autoptr(JsonGenerator) generator = NULL;

      g_test_message ("%s: input=%s output=%s", test->description, test->input_name, test->output_name);

      input_json = g_build_filename (f->srcdir, "json-report", multiarch_tuples[0],
                                     test->input_name, NULL);
      output_json = g_build_filename (f->srcdir, "json-report", multiarch_tuples[0],
                                      test->output_name, NULL);

      parser = json_parser_new ();
      json_parser_load_from_file (parser, output_json, &error);
      g_assert_no_error (error);
      node = json_parser_get_root (parser);
      g_assert_nonnull (node);
      generator = json_generator_new ();
      json_generator_set_root (generator, node);
      json_generator_set_pretty (generator, TRUE);
      generated = json_generator_to_data (generator, NULL);
      expectation = g_strconcat (generated, "\n", NULL);

      envp = g_get_environ ();
      envp = g_environ_setenv (envp, "SRT_TEST_PARSE_JSON", input_json, TRUE);

      result = g_spawn_sync (NULL,    /* working directory */
                             (gchar **) argv,
                             envp,    /* envp */
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

      if (g_strcmp0 (output, expectation) != 0)
        {
          const char *artifacts = g_getenv ("AUTOPKGTEST_ARTIFACTS");
          g_autofree gchar *tmpdir = NULL;
          g_autofree gchar *expected_path = NULL;
          g_autofree gchar *output_path = NULL;
          const gchar *diff_argv[] = { "diff", "-u", "<expected>", "<output>", NULL };

          if (artifacts == NULL)
            {
              tmpdir = g_dir_make_tmp ("srt-tests-XXXXXX", &error);
              g_assert_no_error (error);
              artifacts = tmpdir;
            }

          expected_path = g_build_filename (artifacts, "expected.json", NULL);
          g_file_set_contents (expected_path, expectation, -1, &error);
          g_assert_no_error (error);

          output_path = g_build_filename (artifacts, "output.json", NULL);
          g_file_set_contents (output_path, output, -1, &error);
          g_assert_no_error (error);

          g_assert_cmpstr (diff_argv[2], ==, "<expected>");
          diff_argv[2] = expected_path;
          g_assert_cmpstr (diff_argv[3], ==, "<output>");
          diff_argv[3] = output_path;
          g_assert_null (diff_argv[4]);

          /* Ignore error from calling diff, if any: we're running it
           * for its side-effect of producing output on our stderr. */
          g_spawn_sync (NULL, (gchar **) diff_argv, NULL, G_SPAWN_SEARCH_PATH,
                        stdout_to_stderr_child_setup, NULL,
                        NULL, NULL, NULL, NULL);

          g_test_message ("Output for comparison: %s %s",
                          expected_path, output_path);

          if (tmpdir != NULL)
            _srt_rm_rf (tmpdir);
        }

      g_assert_cmpstr (output, ==, expectation);
    }
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
  g_autoptr(GError) error = NULL;
  g_autofree gchar *output = NULL;
  g_autofree gchar *diagnostics = NULL;
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

  if (g_getenv ("SRT_TEST_UNINSTALLED") != NULL)
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
  g_autoptr(JsonNode) node = NULL;
  JsonObject *json;
  JsonObject *json_arch;
  g_autoptr(GError) error = NULL;
  g_autofree gchar *output = NULL;
  g_autoptr(SrtSystemInfo) info = srt_system_info_new (NULL);
  g_autofree gchar *adverb = g_build_filename (f->builddir, "adverb", NULL);
  g_autofree gchar *expectations_in = g_build_filename (f->srcdir, "expectations", NULL);
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

  node = json_from_string (output, &error);
  g_assert_no_error (error);
  g_assert_nonnull (node);
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
}

int
main (int argc,
      char **argv)
{
  argv0 = argv[0];

  _srt_tests_init (&argc, &argv, NULL);
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
  g_test_add ("/system-info-cli/json_parsing", Fixture, NULL,
              setup, json_parsing, teardown);
  g_test_add ("/system-info-cli/help-and-version", Fixture, NULL,
              setup, test_help_and_version, teardown);
  g_test_add ("/system-info-cli/unblocks_sigchld", Fixture, NULL,
              setup, test_unblocks_sigchld, teardown);

  return g_test_run ();
}
