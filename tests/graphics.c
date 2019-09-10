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

#include "steam-runtime-tools/graphics-internal.h"
#include "steam-runtime-tools/graphics-test-defines.h"
#include "steam-runtime-tools/system-info.h"
#include "test-utils.h"

static const char *argv0;

typedef struct
{
  int unused;
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
  f->builddir = g_strdup (g_getenv ("G_TEST_BUILDDIR"));

  if (f->builddir == NULL)
    f->builddir = g_path_get_dirname (argv0);
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_free (f->builddir);
}

/*
 * Test basic functionality of the SrtGraphics object.
 */
static void
test_object (Fixture *f,
             gconstpointer context)
{
  SrtGraphics *graphics;
  SrtGraphicsIssues issues;
  gchar *messages;
  gchar *tuple;
  gchar *renderer;
  gchar *version;

  graphics = _srt_graphics_new("mock-good",
                               SRT_WINDOW_SYSTEM_GLX,
                               SRT_RENDERING_INTERFACE_GL,
                               SRT_TEST_GOOD_GRAPHICS_RENDERER,
                               SRT_TEST_GOOD_GRAPHICS_VERSION,
                               SRT_GRAPHICS_ISSUES_NONE,
                               "");
  g_assert_cmpint (srt_graphics_get_issues (graphics), ==,
                   SRT_GRAPHICS_ISSUES_NONE);
  g_assert_cmpstr (srt_graphics_get_multiarch_tuple (graphics), ==,
                   "mock-good");
  g_assert_cmpstr (srt_graphics_get_renderer_string (graphics), ==,
                   SRT_TEST_GOOD_GRAPHICS_RENDERER);
  g_assert_cmpstr (srt_graphics_get_version_string (graphics), ==,
                   SRT_TEST_GOOD_GRAPHICS_VERSION);
  g_assert_cmpstr (srt_graphics_get_messages (graphics), ==, NULL);
  g_object_get (graphics,
                "messages", &messages,
                "multiarch-tuple", &tuple,
                "issues", &issues,
                "renderer-string", &renderer,
                "version-string", &version,
                NULL);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_NONE);
  g_assert_cmpstr (messages, ==, NULL);
  g_assert_cmpstr (tuple, ==, "mock-good");
  g_assert_cmpstr (renderer, ==, SRT_TEST_GOOD_GRAPHICS_RENDERER);
  g_assert_cmpstr (version, ==, SRT_TEST_GOOD_GRAPHICS_VERSION);
  g_free (messages);
  g_free (tuple);
  g_free (renderer);
  g_free (version);
  g_object_unref (graphics);
}

/*
 * Test a mock system with hardware graphics stack
 */
static void
test_good_graphics (Fixture *f,
                    gconstpointer context)
{
  SrtGraphics *graphics = NULL;
  SrtGraphicsIssues issues;
  gchar *tuple;
  gchar *renderer;
  gchar *version;

  SrtSystemInfo *info = srt_system_info_new (NULL);
  srt_system_info_set_helpers_path (info, f->builddir);

  issues = srt_system_info_check_graphics (info,
                                           "mock-good",
                                           SRT_WINDOW_SYSTEM_GLX,
                                           SRT_RENDERING_INTERFACE_GL,
                                           &graphics);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_NONE);
  g_assert_cmpstr (srt_graphics_get_renderer_string (graphics), ==,
                   SRT_TEST_GOOD_GRAPHICS_RENDERER);
  g_assert_cmpstr (srt_graphics_get_version_string (graphics), ==,
                   SRT_TEST_GOOD_GRAPHICS_VERSION);
  g_object_get (graphics,
                "multiarch-tuple", &tuple,
                "issues", &issues,
                "renderer-string", &renderer,
                "version-string", &version,
                NULL);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_NONE);
  g_assert_cmpstr (tuple, ==, "mock-good");
  g_assert_cmpstr (renderer, ==, SRT_TEST_GOOD_GRAPHICS_RENDERER);
  g_assert_cmpstr (version, ==, SRT_TEST_GOOD_GRAPHICS_VERSION);
  g_free (tuple);
  g_free (renderer);
  g_free (version);

  g_object_unref (graphics);
  g_object_unref (info);
}

/*
 * Test a mock system with no graphics stack
 */
static void
test_bad_graphics (Fixture *f,
                   gconstpointer context)
{
  SrtGraphics *graphics = NULL;
  SrtGraphicsIssues issues;
  gchar *messages;
  gchar *tuple;
  gchar *renderer;
  gchar *version;

  SrtSystemInfo *info = srt_system_info_new (NULL);
  srt_system_info_set_helpers_path (info, f->builddir);

  issues = srt_system_info_check_graphics (info,
                                           "mock-bad",
                                           SRT_WINDOW_SYSTEM_GLX,
                                           SRT_RENDERING_INTERFACE_GL,
                                           &graphics);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_CANNOT_LOAD);
  g_assert_cmpstr (srt_graphics_get_renderer_string (graphics), ==,
                   NULL);
  g_assert_cmpstr (srt_graphics_get_version_string (graphics), ==,
                   NULL);
  g_assert_cmpstr (srt_graphics_get_messages (graphics), ==,
                   "Waffle error: 0x2 WAFFLE_ERROR_UNKNOWN: XOpenDisplay failed\n");
  g_object_get (graphics,
                "messages", &messages,
                "multiarch-tuple", &tuple,
                "issues", &issues,
                "renderer-string", &renderer,
                "version-string", &version,
                NULL);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_CANNOT_LOAD);
  g_assert_cmpstr (messages, ==,
                   "Waffle error: 0x2 WAFFLE_ERROR_UNKNOWN: XOpenDisplay failed\n");
  g_assert_cmpstr (tuple, ==, "mock-bad");
  g_assert_cmpstr (renderer, ==, NULL);
  g_assert_cmpstr (version, ==, NULL);
  g_free (messages);
  g_free (tuple);
  g_free (renderer);
  g_free (version);

  g_object_unref (graphics);
  g_object_unref (info);
}

/*
 * Test a mock system with timeout
 */
static void
test_timeout_graphics (Fixture *f,
                   gconstpointer context)
{
  SrtGraphics *graphics = NULL;
  SrtGraphicsIssues issues;
  gchar *tuple;
  gchar *renderer;
  gchar *version;

  SrtSystemInfo *info = srt_system_info_new (NULL);
  srt_system_info_set_helpers_path (info, f->builddir);
  srt_system_info_set_test_flags (info, SRT_TEST_FLAGS_TIME_OUT_SOONER);

  issues = srt_system_info_check_graphics (info,
                                           "mock-hanging",
                                           SRT_WINDOW_SYSTEM_GLX,
                                           SRT_RENDERING_INTERFACE_GL,
                                           &graphics);
  g_debug ("issues is 0x%x", issues);
  g_assert_cmphex ((issues & SRT_GRAPHICS_ISSUES_CANNOT_LOAD), ==, SRT_GRAPHICS_ISSUES_CANNOT_LOAD);
  g_assert_cmphex ((issues & SRT_GRAPHICS_ISSUES_TIMEOUT), ==, SRT_GRAPHICS_ISSUES_TIMEOUT);
  g_assert_cmpstr (srt_graphics_get_renderer_string (graphics), ==,
                   NULL);
  g_assert_cmpstr (srt_graphics_get_version_string (graphics), ==,
                   NULL);
  g_object_get (graphics,
                "multiarch-tuple", &tuple,
                "issues", &issues,
                "renderer-string", &renderer,
                "version-string", &version,
                NULL);
  g_assert_cmphex ((issues & SRT_GRAPHICS_ISSUES_CANNOT_LOAD), ==, SRT_GRAPHICS_ISSUES_CANNOT_LOAD);
  g_assert_cmphex ((issues & SRT_GRAPHICS_ISSUES_TIMEOUT), ==, SRT_GRAPHICS_ISSUES_TIMEOUT);
  g_assert_cmpstr (tuple, ==, "mock-hanging");
  g_assert_cmpstr (renderer, ==, NULL);
  g_assert_cmpstr (version, ==, NULL);
  g_free (tuple);
  g_free (renderer);
  g_free (version);

  g_object_unref (graphics);
  g_object_unref (info);
}

/*
 * Test a mock system with software rendering
 */
static void
test_software_rendering (Fixture *f,
                         gconstpointer context)
{
  SrtGraphics *graphics = NULL;
  SrtGraphicsIssues issues;
  gchar *tuple;
  gchar *renderer;
  gchar *version;

  SrtSystemInfo *info = srt_system_info_new (NULL);
  srt_system_info_set_helpers_path (info, f->builddir);

  issues = srt_system_info_check_graphics (info,
                                           "mock-software",
                                           SRT_WINDOW_SYSTEM_GLX,
                                           SRT_RENDERING_INTERFACE_GL,
                                           &graphics);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_SOFTWARE_RENDERING);
  g_assert_cmpstr (srt_graphics_get_renderer_string (graphics), ==,
                   SRT_TEST_SOFTWARE_GRAPHICS_RENDERER);
  g_assert_cmpstr (srt_graphics_get_version_string (graphics), ==,
                   SRT_TEST_SOFTWARE_GRAPHICS_VERSION);
  g_object_get (graphics,
                "multiarch-tuple", &tuple,
                "issues", &issues,
                "renderer-string", &renderer,
                "version-string", &version,
                NULL);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_SOFTWARE_RENDERING);
  g_assert_cmpstr (tuple, ==, "mock-software");
  g_assert_cmpstr (renderer, ==, SRT_TEST_SOFTWARE_GRAPHICS_RENDERER);
  g_assert_cmpstr (version, ==, SRT_TEST_SOFTWARE_GRAPHICS_VERSION);
  g_free (tuple);
  g_free (renderer);
  g_free (version);

  g_object_unref (graphics);
  g_object_unref (info);
}

/*
 * Test a mock system with good vulkan drivers
 */
static void
test_good_vulkan (Fixture *f,
                  gconstpointer context)
{
  SrtGraphics *graphics = NULL;
  SrtGraphicsIssues issues;
  gchar *tuple;
  gchar *renderer;
  gchar *version;

  SrtSystemInfo *info = srt_system_info_new (NULL);
  srt_system_info_set_helpers_path (info, f->builddir);

  issues = srt_system_info_check_graphics (info,
                                           "mock-good",
                                           SRT_WINDOW_SYSTEM_X11,
                                           SRT_RENDERING_INTERFACE_VULKAN,
                                           &graphics);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_NONE);
  g_assert_cmpstr (srt_graphics_get_renderer_string (graphics), ==,
                   SRT_TEST_GOOD_GRAPHICS_RENDERER);
  g_assert_cmpstr (srt_graphics_get_version_string (graphics), ==,
                   SRT_TEST_GOOD_VULKAN_VERSION);
  g_object_get (graphics,
                "multiarch-tuple", &tuple,
                "issues", &issues,
                "renderer-string", &renderer,
                "version-string", &version,
                NULL);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_NONE);
  g_assert_cmpstr (tuple, ==, "mock-good");
  g_assert_cmpstr (renderer, ==, SRT_TEST_GOOD_GRAPHICS_RENDERER);
  g_assert_cmpstr (version, ==, SRT_TEST_GOOD_VULKAN_VERSION);
  g_free (tuple);
  g_free (renderer);
  g_free (version);

  g_object_unref (graphics);
  g_object_unref (info);
}

/*
 * Test a mock system with no vulkan graphics driver
 */
static void
test_bad_vulkan (Fixture *f,
                 gconstpointer context)
{
  SrtGraphics *graphics = NULL;
  SrtGraphicsIssues issues;
  gchar *tuple;
  gchar *renderer;
  gchar *version;

  SrtSystemInfo *info = srt_system_info_new (NULL);
  srt_system_info_set_helpers_path (info, f->builddir);

  issues = srt_system_info_check_graphics (info,
                                           "mock-bad",
                                           SRT_WINDOW_SYSTEM_X11,
                                           SRT_RENDERING_INTERFACE_VULKAN,
                                           &graphics);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_CANNOT_LOAD);
  g_assert_cmpstr (srt_graphics_get_renderer_string (graphics), ==,
                   NULL);
  g_assert_cmpstr (srt_graphics_get_version_string (graphics), ==,
                   NULL);
  g_object_get (graphics,
                "multiarch-tuple", &tuple,
                "issues", &issues,
                "renderer-string", &renderer,
                "version-string", &version,
                NULL);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_CANNOT_LOAD);
  g_assert_cmpstr (tuple, ==, "mock-bad");
  g_assert_cmpstr (renderer, ==, NULL);
  g_assert_cmpstr (version, ==, NULL);
  g_free (tuple);
  g_free (renderer);
  g_free (version);

  g_object_unref (graphics);
  g_object_unref (info);
}

int
main (int argc,
      char **argv)
{
  argv0 = argv[0];

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/object", Fixture, NULL,
              setup, test_object, teardown);
  g_test_add ("/good", Fixture, NULL,
              setup, test_good_graphics, teardown);
  g_test_add ("/bad", Fixture, NULL,
              setup, test_bad_graphics, teardown);
  g_test_add ("/hanging", Fixture, NULL,
              setup, test_timeout_graphics, teardown);
  g_test_add ("/software", Fixture, NULL,
              setup, test_software_rendering, teardown);

  g_test_add ("/vulkan", Fixture, NULL,
              setup, test_good_vulkan, teardown);
  g_test_add ("/vulkan-bad", Fixture, NULL,
              setup, test_bad_vulkan, teardown);

  return g_test_run ();
}
