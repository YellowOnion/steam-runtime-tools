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

#include <errno.h>
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
  gchar *srcdir;
  gchar *builddir;
  gchar **fake_icds_envp;
} Fixture;

typedef struct
{
  enum
  {
    ICD_MODE_NORMAL,
    ICD_MODE_XDG_DIRS,
    ICD_MODE_FLATPAK,
    ICD_MODE_EXPLICIT_DIRS,
    ICD_MODE_EXPLICIT_FILENAMES,
    ICD_MODE_RELATIVE_FILENAMES,
  } icd_mode;
} Config;

static void
setup (Fixture *f,
       gconstpointer context)
{
  const Config *config = context;
  gchar *tmp;

  f->srcdir = g_strdup (g_getenv ("G_TEST_SRCDIR"));
  f->builddir = g_strdup (g_getenv ("G_TEST_BUILDDIR"));

  if (f->srcdir == NULL)
    f->srcdir = g_path_get_dirname (argv0);

  if (f->builddir == NULL)
    f->builddir = g_path_get_dirname (argv0);

  if (g_chdir (f->srcdir) != 0)
    g_error ("chdir %s: %s", f->srcdir, g_strerror (errno));

  f->fake_icds_envp = g_get_environ ();

  if (config == NULL || config->icd_mode != ICD_MODE_RELATIVE_FILENAMES)
    {
      tmp = g_build_filename (f->srcdir, "fake-icds", NULL);
      f->fake_icds_envp = g_environ_setenv (f->fake_icds_envp,
                                            "SRT_TEST_SYSROOT", tmp, TRUE);
      g_free (tmp);
    }

  f->fake_icds_envp = g_environ_setenv (f->fake_icds_envp,
                                        "HOME", "/home", TRUE);

  if (config != NULL && config->icd_mode == ICD_MODE_XDG_DIRS)
    {
      f->fake_icds_envp = g_environ_setenv (f->fake_icds_envp,
                                            "XDG_CONFIG_DIRS", "/confdir", TRUE);
      f->fake_icds_envp = g_environ_setenv (f->fake_icds_envp,
                                            "XDG_DATA_HOME", "/datahome", TRUE);
      f->fake_icds_envp = g_environ_setenv (f->fake_icds_envp,
                                            "XDG_DATA_DIRS", "/datadir", TRUE);
    }
  else
    {
      f->fake_icds_envp = g_environ_unsetenv (f->fake_icds_envp,
                                              "XDG_CONFIG_DIRS");
      f->fake_icds_envp = g_environ_unsetenv (f->fake_icds_envp,
                                              "XDG_DATA_HOME");
      f->fake_icds_envp = g_environ_unsetenv (f->fake_icds_envp,
                                              "XDG_DATA_DIRS");
    }

  if (config != NULL && config->icd_mode == ICD_MODE_EXPLICIT_FILENAMES)
    {
      f->fake_icds_envp = g_environ_setenv (f->fake_icds_envp,
                                            "__EGL_VENDOR_LIBRARY_FILENAMES",
                                            "/not-a-file:"
                                            "/null.json:"
                                            "/false.json:"
                                            "/str.json:"
                                            "/no-library.json",
                                            TRUE);
      f->fake_icds_envp = g_environ_setenv (f->fake_icds_envp,
                                            "VK_ICD_FILENAMES",
                                            "/not-a-file:"
                                            "/null.json:"
                                            "/false.json:"
                                            "/str.json:"
                                            "/no-library.json:"
                                            "/no-api-version.json",
                                            TRUE);
    }
  else if (config != NULL && config->icd_mode == ICD_MODE_RELATIVE_FILENAMES)
    {
      f->fake_icds_envp = g_environ_setenv (f->fake_icds_envp,
                                            "__EGL_VENDOR_LIBRARY_FILENAMES",
                                            "fake-icds/not-a-file:"
                                            "fake-icds/usr/share/glvnd/egl_vendor.d/50_mesa.json:"
                                            "fake-icds/null.json:"
                                            "fake-icds/false.json:"
                                            "fake-icds/str.json:"
                                            "fake-icds/no-library.json",
                                            TRUE);
      f->fake_icds_envp = g_environ_setenv (f->fake_icds_envp,
                                            "VK_ICD_FILENAMES",
                                            "fake-icds/not-a-file:"
                                            "fake-icds/usr/share/vulkan/icd.d/intel_icd.x86_64.json:"
                                            "fake-icds/null.json:"
                                            "fake-icds/false.json:"
                                            "fake-icds/str.json:"
                                            "fake-icds/no-library.json:"
                                            "fake-icds/no-api-version.json",
                                            TRUE);
    }
  else
    {
      f->fake_icds_envp = g_environ_unsetenv (f->fake_icds_envp,
                                              "__EGL_VENDOR_LIBRARY_FILENAMES");
      f->fake_icds_envp = g_environ_unsetenv (f->fake_icds_envp,
                                              "VK_ICD_FILENAMES");
    }

  if (config != NULL && config->icd_mode == ICD_MODE_EXPLICIT_DIRS)
    {
      f->fake_icds_envp = g_environ_setenv (f->fake_icds_envp,
                                            "__EGL_VENDOR_LIBRARY_DIRS",
                                            "/egl1:/egl2",
                                            TRUE);
    }
  else
    {
      f->fake_icds_envp = g_environ_unsetenv (f->fake_icds_envp,
                                              "__EGL_VENDOR_LIBRARY_DIRS");
    }
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_free (f->srcdir);
  g_free (f->builddir);
  g_strfreev (f->fake_icds_envp);
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
  SrtGraphicsLibraryVendor library_vendor;
  gboolean vendor_neutral;
  gchar *messages;
  gchar *tuple;
  gchar *renderer;
  gchar *version;

  graphics = _srt_graphics_new("mock-good",
                               SRT_WINDOW_SYSTEM_GLX,
                               SRT_RENDERING_INTERFACE_GL,
                               SRT_GRAPHICS_LIBRARY_VENDOR_GLVND,
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
  vendor_neutral = srt_graphics_library_is_vendor_neutral (graphics, &library_vendor);
  g_assert_cmpint (library_vendor, ==, SRT_GRAPHICS_LIBRARY_VENDOR_GLVND);
  g_assert_true (vendor_neutral);
  g_object_get (graphics,
                "messages", &messages,
                "multiarch-tuple", &tuple,
                "issues", &issues,
                "library-vendor", &library_vendor,
                "renderer-string", &renderer,
                "version-string", &version,
                NULL);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_NONE);
  g_assert_cmpint (library_vendor, ==, SRT_GRAPHICS_LIBRARY_VENDOR_GLVND);
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
  SrtGraphicsLibraryVendor library_vendor;
  gboolean vendor_neutral;
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
  /* We used "mock-bad" for the architecture so, when checking the library vendor,
   * we will not be able to call the helper `mock-bad-check-library`.
   * For this reason we expect %SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN */
  vendor_neutral = srt_graphics_library_is_vendor_neutral (graphics, &library_vendor);
  g_assert_cmpint (library_vendor, ==, SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN);
  g_assert_false (vendor_neutral);
  g_object_get (graphics,
                "messages", &messages,
                "multiarch-tuple", &tuple,
                "issues", &issues,
                "library-vendor", &library_vendor,
                "renderer-string", &renderer,
                "version-string", &version,
                NULL);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_CANNOT_LOAD);
  g_assert_cmpint (library_vendor, ==, SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN);
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

/*
 * Test a mock system with vulkan driver but check-vulkan failure
 */
static void
test_mixed_vulkan (Fixture *f,
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
                                           "mock-mixed",
                                           SRT_WINDOW_SYSTEM_X11,
                                           SRT_RENDERING_INTERFACE_VULKAN,
                                           &graphics);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_CANNOT_DRAW);
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
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_CANNOT_DRAW);
  g_assert_cmpstr (tuple, ==, "mock-mixed");
  g_assert_cmpstr (renderer, ==, SRT_TEST_GOOD_GRAPHICS_RENDERER);
  g_assert_cmpstr (version, ==, SRT_TEST_GOOD_VULKAN_VERSION);
  g_free (tuple);
  g_free (renderer);
  g_free (version);

  g_object_unref (graphics);
  g_object_unref (info);
}

/*
 * Assert that @icd is internally consistent.
 */
static void
assert_egl_icd (SrtEglIcd *icd)
{
  GError *error = NULL;
  GError *error_property = NULL;
  gchar *json_path = NULL;
  gchar *library_path = NULL;
  gchar *resolved = NULL;
  gchar *resolved_property = NULL;

  g_assert_true (SRT_IS_EGL_ICD (icd));

  g_object_get (icd,
                "error", &error_property,
                "json-path", &json_path,
                "library-path", &library_path,
                "resolved-library-path", &resolved_property,
                NULL);
  resolved = srt_egl_icd_resolve_library_path (icd);

  g_assert_cmpstr (json_path, !=, NULL);
  g_assert_cmpstr (json_path, ==, srt_egl_icd_get_json_path (icd));
  g_assert_true (g_path_is_absolute (json_path));

  /* These are invariants, even if they're NULL */
  g_assert_cmpstr (library_path, ==, srt_egl_icd_get_library_path (icd));
  g_assert_cmpstr (resolved_property, ==, resolved);

  if (error_property == NULL)
    {
      g_assert_true (srt_egl_icd_check_error (icd, NULL));
      g_assert_true (srt_egl_icd_check_error (icd, &error));
      g_assert_no_error (error);
      g_assert_no_error (error_property);
      g_assert_cmpstr (library_path, !=, NULL);
      g_assert_cmpstr (resolved, !=, NULL);
      g_assert_cmpstr (resolved_property, !=, NULL);

      if (strchr (resolved, '/') == NULL)
        {
          g_assert_cmpstr (resolved, ==, library_path);
        }
      else
        {
          g_assert_true (g_path_is_absolute (resolved));
        }
    }
  else
    {
      g_assert_false (srt_egl_icd_check_error (icd, NULL));
      g_assert_false (srt_egl_icd_check_error (icd, &error));
      g_assert_nonnull (error);
      g_assert_error (error, error_property->domain, error_property->code);
      g_assert_cmpstr (error->message, ==, error_property->message);
      g_assert_cmpstr (library_path, ==, NULL);
      g_assert_cmpstr (resolved, ==, NULL);
      g_assert_cmpstr (resolved_property, ==, NULL);
    }

  g_clear_error (&error);
  g_clear_error (&error_property);
  g_free (json_path);
  g_free (library_path);
  g_free (resolved);
  g_free (resolved_property);
}

/*
 * Assert that @icd is internally consistent and in a failed state.
 */
static void
assert_egl_icd_has_error (SrtEglIcd *icd)
{
  g_assert_false (srt_egl_icd_check_error (icd, NULL));
  assert_egl_icd (icd);
}

/*
 * Assert that @icd is internally consistent and in a successful state.
 */
static void
assert_egl_icd_no_error (SrtEglIcd *icd)
{
  GError *error = NULL;

  srt_egl_icd_check_error (icd, &error);
  g_assert_no_error (error);
  assert_egl_icd (icd);
}

static gboolean
same_stat (GStatBuf *left,
           GStatBuf *right)
{
  return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

/*
 * We don't assert that filenames are literally the same, because they
 * might canonicalize differently in the presence of symlinks: we just
 * assert that they are the same file.
 */
static void
assert_same_file (const char *expected,
                  const char *actual)
{
  GStatBuf expected_stat, actual_stat;

  if (g_stat (expected, &expected_stat) != 0)
    g_error ("stat %s: %s", expected, g_strerror (errno));

  if (g_stat (actual, &actual_stat) != 0)
    g_error ("stat %s: %s", actual, g_strerror (errno));

  if (!same_stat (&expected_stat, &actual_stat))
    g_error ("%s is not the same file as %s", expected, actual);
}

static void
test_icd_egl (Fixture *f,
              gconstpointer context)
{
  const Config *config = context;
  SrtSystemInfo *info = srt_system_info_new (NULL);
  GList *icds;
  const GList *iter;
  const char * const multiarchs[] = { "mock-abi", NULL };

  srt_system_info_set_environ (info, f->fake_icds_envp);

  if (config != NULL && config->icd_mode == ICD_MODE_FLATPAK)
    icds = srt_system_info_list_egl_icds (info, multiarchs);
  else
    icds = srt_system_info_list_egl_icds (info, NULL);

  for (iter = icds; iter != NULL; iter = iter->next)
    {
      GError *error = NULL;

      g_test_message ("ICD: %s", srt_egl_icd_get_json_path (iter->data));

      if (srt_egl_icd_check_error (iter->data, &error))
        {
          g_test_message ("\tlibrary: %s",
                          srt_egl_icd_get_library_path (iter->data));
        }
      else
        {
          g_test_message ("\terror: %s", error->message);
          g_clear_error (&error);
        }
    }

  if (config != NULL && config->icd_mode == ICD_MODE_EXPLICIT_DIRS)
    {
      SrtEglIcd *other;
      gchar *resolved;

      iter = icds;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_egl_icd_get_json_path (iter->data), ==,
                       "/egl1/AAA.json");
      assert_egl_icd_has_error (iter->data);

      other = srt_egl_icd_new_replace_library_path (iter->data,
                                                    "/run/host/libEGL_icd.so");
      /* Copying an invalid ICD yields another invalid ICD. */
      assert_egl_icd_has_error (iter->data);
      g_object_unref (other);

      iter = iter->next;
      /* We sort lexicographically with strcmp(), so BBB comes before a. */
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_egl_icd_get_json_path (iter->data), ==,
                       "/egl1/BBB.json");
      assert_egl_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_egl_icd_get_json_path (iter->data), ==,
                       "/egl1/a.json");
      assert_egl_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_egl_icd_get_json_path (iter->data), ==,
                       "/egl1/b.json");
      assert_egl_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_egl_icd_get_json_path (iter->data), ==,
                       "/egl1/z.json");
      assert_egl_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_egl_icd_get_json_path (iter->data), ==,
                       "/egl2/absolute.json");
      assert_egl_icd_no_error (iter->data);
      g_assert_cmpstr (srt_egl_icd_get_library_path (iter->data), ==,
                       "/opt/libEGL_myvendor.so");
      resolved = srt_egl_icd_resolve_library_path (iter->data);
      g_assert_cmpstr (resolved, ==, "/opt/libEGL_myvendor.so");
      g_free (resolved);

      iter = iter->next;
      g_assert_null (iter);
    }
  else if (config != NULL && config->icd_mode == ICD_MODE_EXPLICIT_FILENAMES)
    {
      iter = icds;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_egl_icd_get_json_path (iter->data), ==, "/not-a-file");
      assert_egl_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_egl_icd_get_json_path (iter->data), ==, "/null.json");
      assert_egl_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_egl_icd_get_json_path (iter->data), ==, "/false.json");
      assert_egl_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_egl_icd_get_json_path (iter->data), ==, "/str.json");
      assert_egl_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_egl_icd_get_json_path (iter->data), ==, "/no-library.json");
      assert_egl_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_null (iter);
    }
  else if (config != NULL && config->icd_mode == ICD_MODE_RELATIVE_FILENAMES)
    {
      const char *path;
      gchar *resolved;

      iter = icds;
      g_assert_nonnull (iter);
      path = srt_egl_icd_get_json_path (iter->data);
      g_assert_true (g_str_has_suffix (path, "/fake-icds/not-a-file"));
      g_assert_true (g_path_is_absolute (path));
      assert_egl_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      path = srt_egl_icd_get_json_path (iter->data);
      g_assert_true (g_str_has_suffix (path, "/fake-icds/usr/share/glvnd/egl_vendor.d/50_mesa.json"));
      g_assert_true (g_path_is_absolute (path));
      assert_same_file ("fake-icds/usr/share/glvnd/egl_vendor.d/50_mesa.json", path);
      assert_egl_icd_no_error (iter->data);
      g_assert_cmpstr (srt_egl_icd_get_library_path (iter->data), ==,
                       "libEGL_mesa.so.0");
      resolved = srt_egl_icd_resolve_library_path (iter->data);
      g_assert_cmpstr (resolved, ==, "libEGL_mesa.so.0");
      g_free (resolved);

      iter = iter->next;
      g_assert_nonnull (iter);
      path = srt_egl_icd_get_json_path (iter->data);
      g_assert_true (g_str_has_suffix (path, "/fake-icds/null.json"));
      g_assert_true (g_path_is_absolute (path));
      assert_same_file ("fake-icds/null.json", path);
      assert_egl_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      path = srt_egl_icd_get_json_path (iter->data);
      g_assert_true (g_str_has_suffix (path, "/fake-icds/false.json"));
      g_assert_true (g_path_is_absolute (path));
      assert_same_file ("fake-icds/false.json", path);
      assert_egl_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      path = srt_egl_icd_get_json_path (iter->data);
      g_assert_true (g_str_has_suffix (path, "/fake-icds/str.json"));
      g_assert_true (g_path_is_absolute (path));
      assert_same_file ("fake-icds/str.json", path);
      assert_egl_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      path = srt_egl_icd_get_json_path (iter->data);
      g_assert_true (g_str_has_suffix (path, "/fake-icds/no-library.json"));
      g_assert_true (g_path_is_absolute (path));
      assert_same_file ("fake-icds/no-library.json", path);
      assert_egl_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_null (iter);
    }
  else if (config != NULL && config->icd_mode == ICD_MODE_FLATPAK)
    {
      SrtEglIcd *other;
      gchar *resolved;

      iter = icds;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_egl_icd_get_json_path (iter->data), ==,
                       "/usr/lib/mock-abi/GL/glvnd/egl_vendor.d/relative.json");
      assert_egl_icd_no_error (iter->data);
      g_assert_cmpstr (srt_egl_icd_get_library_path (iter->data), ==,
                       "../libEGL_relative.so");
      resolved = srt_egl_icd_resolve_library_path (iter->data);
      g_assert_cmpstr (resolved, ==,
                       "/usr/lib/mock-abi/GL/glvnd/egl_vendor.d/../libEGL_relative.so");
      g_free (resolved);

      other = srt_egl_icd_new_replace_library_path (iter->data,
                                                    "/run/host/libEGL.so");
      assert_egl_icd_no_error (iter->data);
      g_assert_cmpstr (srt_egl_icd_get_json_path (other), ==,
                       srt_egl_icd_get_json_path (iter->data));
      /* The pointers are not equal */
      g_assert_true (srt_egl_icd_get_json_path (other) !=
                     srt_egl_icd_get_json_path (iter->data));
      g_assert_cmpstr (srt_egl_icd_get_library_path (other), ==,
                       "/run/host/libEGL.so");
      g_object_unref (other);

      iter = iter->next;
      g_assert_null (iter);
    }
  else
    {
      gchar *resolved;

      /* EGL ICDs don't respect the XDG variables, so XDG_DIRS is the same
       * as NORMAL. */
      iter = icds;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_egl_icd_get_json_path (iter->data), ==,
                       "/etc/glvnd/egl_vendor.d/invalid.json");
      /* This one is invalid. */
      assert_egl_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_egl_icd_get_json_path (iter->data), ==,
                       "/usr/share/glvnd/egl_vendor.d/50_mesa.json");
      assert_egl_icd_no_error (iter->data);
      g_assert_cmpstr (srt_egl_icd_get_library_path (iter->data), ==,
                       "libEGL_mesa.so.0");
      resolved = srt_egl_icd_resolve_library_path (iter->data);
      g_assert_cmpstr (resolved, ==, "libEGL_mesa.so.0");
      g_free (resolved);

      iter = iter->next;
      g_assert_null (iter);
    }

  g_list_free_full (icds, g_object_unref);
}

/*
 * Assert that @icd is internally consistent.
 */
static void
assert_vulkan_icd (SrtVulkanIcd *icd)
{
  GError *error = NULL;
  GError *error_property = NULL;
  gchar *api_version = NULL;
  gchar *json_path = NULL;
  gchar *library_path = NULL;
  gchar *resolved = NULL;
  gchar *resolved_property = NULL;

  g_assert_true (SRT_IS_VULKAN_ICD (icd));

  g_object_get (icd,
                "api-version", &api_version,
                "error", &error_property,
                "json-path", &json_path,
                "library-path", &library_path,
                "resolved-library-path", &resolved_property,
                NULL);
  resolved = srt_vulkan_icd_resolve_library_path (icd);

  g_assert_cmpstr (json_path, !=, NULL);
  g_assert_cmpstr (json_path, ==, srt_vulkan_icd_get_json_path (icd));
  g_assert_true (g_path_is_absolute (json_path));

  /* These are invariants, even if they're NULL */
  g_assert_cmpstr (api_version, ==, srt_vulkan_icd_get_api_version (icd));
  g_assert_cmpstr (library_path, ==, srt_vulkan_icd_get_library_path (icd));
  g_assert_cmpstr (resolved_property, ==, resolved);

  if (error_property == NULL)
    {
      g_assert_true (srt_vulkan_icd_check_error (icd, NULL));
      g_assert_true (srt_vulkan_icd_check_error (icd, &error));
      g_assert_no_error (error);
      g_assert_no_error (error_property);
      g_assert_cmpstr (library_path, !=, NULL);
      g_assert_cmpstr (api_version, !=, NULL);
      g_assert_cmpstr (resolved, !=, NULL);
      g_assert_cmpstr (resolved_property, !=, NULL);

      if (strchr (resolved, '/') == NULL)
        {
          g_assert_cmpstr (resolved, ==, library_path);
        }
      else
        {
          g_assert_true (g_path_is_absolute (resolved));
        }
    }
  else
    {
      g_assert_false (srt_vulkan_icd_check_error (icd, NULL));
      g_assert_false (srt_vulkan_icd_check_error (icd, &error));
      g_assert_nonnull (error);
      g_assert_error (error, error_property->domain, error_property->code);
      g_assert_cmpstr (error->message, ==, error_property->message);
      g_assert_cmpstr (library_path, ==, NULL);
      g_assert_cmpstr (api_version, ==, NULL);
      g_assert_cmpstr (resolved, ==, NULL);
      g_assert_cmpstr (resolved_property, ==, NULL);
    }

  g_clear_error (&error);
  g_clear_error (&error_property);
  g_free (api_version);
  g_free (json_path);
  g_free (library_path);
  g_free (resolved);
  g_free (resolved_property);
}

/*
 * Assert that @icd is internally consistent and in a failed state.
 */
static void
assert_vulkan_icd_has_error (SrtVulkanIcd *icd)
{
  g_assert_false (srt_vulkan_icd_check_error (icd, NULL));
  assert_vulkan_icd (icd);
}

/*
 * Assert that @icd is internally consistent and in a successful state.
 */
static void
assert_vulkan_icd_no_error (SrtVulkanIcd *icd)
{
  GError *error = NULL;

  srt_vulkan_icd_check_error (icd, &error);
  g_assert_no_error (error);
  assert_vulkan_icd (icd);
}

static void
test_icd_vulkan (Fixture *f,
                 gconstpointer context)
{
  const Config *config = context;
  SrtSystemInfo *info = srt_system_info_new (NULL);
  GList *icds;
  const GList *iter;
  const char * const multiarchs[] = { "mock-abi", NULL };

  srt_system_info_set_environ (info, f->fake_icds_envp);

  if (config != NULL && config->icd_mode == ICD_MODE_FLATPAK)
    icds = srt_system_info_list_vulkan_icds (info, multiarchs);
  else
    icds = srt_system_info_list_vulkan_icds (info, NULL);

  for (iter = icds; iter != NULL; iter = iter->next)
    {
      GError *error = NULL;

      g_test_message ("ICD: %s", srt_vulkan_icd_get_json_path (iter->data));

      if (srt_vulkan_icd_check_error (iter->data, &error))
        {
          g_test_message ("\tlibrary: %s",
                          srt_vulkan_icd_get_library_path (iter->data));
        }
      else
        {
          g_test_message ("\terror: %s", error->message);
          g_clear_error (&error);
        }
    }

  if (config != NULL && config->icd_mode == ICD_MODE_EXPLICIT_FILENAMES)
    {
      SrtVulkanIcd *other;

      iter = icds;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==, "/not-a-file");
      assert_vulkan_icd_has_error (iter->data);

      other = srt_vulkan_icd_new_replace_library_path (iter->data,
                                                       "/run/host/vulkan_icd.so");
      /* Copying an invalid ICD yields another invalid ICD. */
      assert_vulkan_icd_has_error (iter->data);
      g_object_unref (other);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==, "/null.json");
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==, "/false.json");
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==, "/str.json");
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==, "/no-library.json");
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==, "/no-api-version.json");
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_null (iter);
    }
  else if (config != NULL && config->icd_mode == ICD_MODE_RELATIVE_FILENAMES)
    {
      const char *path;

      iter = icds;
      g_assert_nonnull (iter);
      path = srt_vulkan_icd_get_json_path (iter->data);
      g_assert_true (g_str_has_suffix (path, "/fake-icds/not-a-file"));
      g_assert_true (g_path_is_absolute (path));
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      path = srt_vulkan_icd_get_json_path (iter->data);
      g_assert_true (g_str_has_suffix (path, "/fake-icds/usr/share/vulkan/icd.d/intel_icd.x86_64.json"));
      g_assert_true (g_path_is_absolute (path));
      assert_same_file ("fake-icds/usr/share/vulkan/icd.d/intel_icd.x86_64.json", path);
      assert_vulkan_icd_no_error (iter->data);
      g_assert_cmpstr (srt_vulkan_icd_get_library_path (iter->data), ==,
                       "/usr/lib/x86_64-linux-gnu/libvulkan_intel.so");
      g_assert_cmpstr (srt_vulkan_icd_get_api_version (iter->data), ==, "1.1.102");

      iter = iter->next;
      g_assert_nonnull (iter);
      path = srt_vulkan_icd_get_json_path (iter->data);
      g_assert_true (g_str_has_suffix (path, "/fake-icds/null.json"));
      g_assert_true (g_path_is_absolute (path));
      assert_same_file ("fake-icds/null.json", path);
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      path = srt_vulkan_icd_get_json_path (iter->data);
      g_assert_true (g_str_has_suffix (path, "/fake-icds/false.json"));
      g_assert_true (g_path_is_absolute (path));
      assert_same_file ("fake-icds/false.json", path);
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      path = srt_vulkan_icd_get_json_path (iter->data);
      g_assert_true (g_str_has_suffix (path, "/fake-icds/str.json"));
      g_assert_true (g_path_is_absolute (path));
      assert_same_file ("fake-icds/str.json", path);
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      path = srt_vulkan_icd_get_json_path (iter->data);
      g_assert_true (g_str_has_suffix (path, "/fake-icds/no-library.json"));
      g_assert_true (g_path_is_absolute (path));
      assert_same_file ("fake-icds/no-library.json", path);
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      path = srt_vulkan_icd_get_json_path (iter->data);
      g_assert_true (g_str_has_suffix (path, "/fake-icds/no-api-version.json"));
      g_assert_true (g_path_is_absolute (path));
      assert_same_file ("fake-icds/no-api-version.json", path);
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_null (iter);
    }
  else if (config != NULL && config->icd_mode == ICD_MODE_FLATPAK)
    {
      gchar *resolved;
      SrtVulkanIcd *other;

      iter = icds;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==,
                       "/etc/xdg/vulkan/icd.d/invalid.json");
      /* This is not valid JSON (it's an empty file) so loading it fails */
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==,
                       "/etc/vulkan/icd.d/basename.json");
      assert_vulkan_icd_no_error (iter->data);
      g_assert_cmpstr (srt_vulkan_icd_get_library_path (iter->data), ==,
                       "libvulkan_basename.so");
      g_assert_cmpstr (srt_vulkan_icd_get_api_version (iter->data), ==, "1.2.3");
      resolved = srt_vulkan_icd_resolve_library_path (iter->data);
      g_assert_cmpstr (resolved, ==, "libvulkan_basename.so");
      g_free (resolved);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==,
                       "/usr/lib/mock-abi/GL/vulkan/icd.d/invalid.json");
      /* This has a JSON array, not an object, so loading it fails */
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==,
                       "/usr/lib/mock-abi/vulkan/icd.d/relative.json");
      assert_vulkan_icd_no_error (iter->data);
      resolved = srt_vulkan_icd_resolve_library_path (iter->data);
      g_assert_cmpstr (resolved, ==,
                       "/usr/lib/mock-abi/vulkan/icd.d/../libvulkan_relative.so");
      g_free (resolved);

      other = srt_vulkan_icd_new_replace_library_path (iter->data,
                                                       "/run/host/vulkan_icd.so");
      assert_vulkan_icd_no_error (iter->data);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (other), ==,
                       srt_vulkan_icd_get_json_path (iter->data));
      /* The pointers are not equal */
      g_assert_true (srt_vulkan_icd_get_json_path (other) !=
                     srt_vulkan_icd_get_json_path (iter->data));
      g_assert_cmpstr (srt_vulkan_icd_get_api_version (other), ==,
                       srt_vulkan_icd_get_api_version (iter->data));
      /* The pointers are not equal */
      g_assert_true (srt_vulkan_icd_get_api_version (other) !=
                     srt_vulkan_icd_get_api_version (iter->data));
      g_assert_cmpstr (srt_vulkan_icd_get_library_path (other), ==,
                       "/run/host/vulkan_icd.so");
      g_object_unref (other);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==,
                       "/usr/local/share/vulkan/icd.d/intel_icd.i686.json");
      assert_vulkan_icd_no_error (iter->data);
      g_assert_cmpstr (srt_vulkan_icd_get_library_path (iter->data), ==,
                       "/usr/lib/i386-linux-gnu/libvulkan_intel.so");
      g_assert_cmpstr (srt_vulkan_icd_get_api_version (iter->data), ==, "1.1.102");
      resolved = srt_vulkan_icd_resolve_library_path (iter->data);
      g_assert_cmpstr (resolved, ==, "/usr/lib/i386-linux-gnu/libvulkan_intel.so");
      g_free (resolved);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==,
                       "/usr/share/vulkan/icd.d/intel_icd.x86_64.json");
      assert_vulkan_icd_no_error (iter->data);
      g_assert_cmpstr (srt_vulkan_icd_get_library_path (iter->data), ==,
                       "/usr/lib/x86_64-linux-gnu/libvulkan_intel.so");
      g_assert_cmpstr (srt_vulkan_icd_get_api_version (iter->data), ==, "1.1.102");

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==,
                       "/home/.local/share/vulkan/icd.d/invalid.json");
      /* This one lacks the required format version */
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_null (iter);
    }
  else if (config != NULL && config->icd_mode == ICD_MODE_XDG_DIRS)
    {
      gchar *resolved;

      iter = icds;
      g_assert_nonnull (iter);
      /* We load $XDG_CONFIG_DIRS instead of /etc/xdg */
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==,
                       "/confdir/vulkan/icd.d/invalid.json");
      /* Not format 1.0.x, so we can't be confident that we're reading
       * it correctly */
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      /* /etc is unaffected by XDG variables */
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==,
                       "/etc/vulkan/icd.d/basename.json");
      assert_vulkan_icd_no_error (iter->data);
      g_assert_cmpstr (srt_vulkan_icd_get_library_path (iter->data), ==,
                       "libvulkan_basename.so");
      g_assert_cmpstr (srt_vulkan_icd_get_api_version (iter->data), ==, "1.2.3");
      resolved = srt_vulkan_icd_resolve_library_path (iter->data);
      g_assert_cmpstr (resolved, ==, "libvulkan_basename.so");
      g_free (resolved);

      iter = iter->next;
      g_assert_nonnull (iter);
      /* We load $XDG_DATA_DIRS instead of /usr/local/share:/usr/share.
       * In this case it only has one item. */
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==,
                       "/datadir/vulkan/icd.d/invalid.json");
      /* Not format 1.0.x, so we can't be confident that we're reading
       * it correctly */
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      /* We load $XDG_DATA_DIRS *before* $XDG_DATA_HOME for
       * some reason. This is weird, but it matches the reference
       * Vulkan loader. */
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==,
                       "/datahome/vulkan/icd.d/invalid.json");
      /* Missing API version */
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      /* We load $XDG_DATA_HOME *as well as* ~/.local/share for some
       * reason. This is weird, but it matches the reference Vulkan
       * loader. */
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==,
                       "/home/.local/share/vulkan/icd.d/invalid.json");
      /* This one lacks the required format version */
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_null (iter);
    }
  else
    {
      gchar *resolved;

      iter = icds;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==,
                       "/etc/xdg/vulkan/icd.d/invalid.json");
      /* This is not valid JSON (it's an empty file) so loading it fails */
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==,
                       "/etc/vulkan/icd.d/basename.json");
      assert_vulkan_icd_no_error (iter->data);
      g_assert_cmpstr (srt_vulkan_icd_get_library_path (iter->data), ==,
                       "libvulkan_basename.so");
      g_assert_cmpstr (srt_vulkan_icd_get_api_version (iter->data), ==, "1.2.3");
      resolved = srt_vulkan_icd_resolve_library_path (iter->data);
      g_assert_cmpstr (resolved, ==, "libvulkan_basename.so");
      g_free (resolved);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==,
                       "/usr/local/share/vulkan/icd.d/intel_icd.i686.json");
      assert_vulkan_icd_no_error (iter->data);
      g_assert_cmpstr (srt_vulkan_icd_get_library_path (iter->data), ==,
                       "/usr/lib/i386-linux-gnu/libvulkan_intel.so");
      g_assert_cmpstr (srt_vulkan_icd_get_api_version (iter->data), ==, "1.1.102");
      resolved = srt_vulkan_icd_resolve_library_path (iter->data);
      g_assert_cmpstr (resolved, ==, "/usr/lib/i386-linux-gnu/libvulkan_intel.so");
      g_free (resolved);

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==,
                       "/usr/share/vulkan/icd.d/intel_icd.x86_64.json");
      assert_vulkan_icd_no_error (iter->data);
      g_assert_cmpstr (srt_vulkan_icd_get_library_path (iter->data), ==,
                       "/usr/lib/x86_64-linux-gnu/libvulkan_intel.so");
      g_assert_cmpstr (srt_vulkan_icd_get_api_version (iter->data), ==, "1.1.102");

      iter = iter->next;
      g_assert_nonnull (iter);
      g_assert_cmpstr (srt_vulkan_icd_get_json_path (iter->data), ==,
                       "/home/.local/share/vulkan/icd.d/invalid.json");
      /* This one lacks the required format version */
      assert_vulkan_icd_has_error (iter->data);

      iter = iter->next;
      g_assert_null (iter);
    }

  g_list_free_full (icds, g_object_unref);
}

static const Config dir_config = { ICD_MODE_EXPLICIT_DIRS };
static const Config filename_config = { ICD_MODE_EXPLICIT_FILENAMES };
static const Config flatpak_config = { ICD_MODE_FLATPAK };
static const Config relative_config = { ICD_MODE_RELATIVE_FILENAMES };
static const Config xdg_config = { ICD_MODE_XDG_DIRS };

int
main (int argc,
      char **argv)
{
  argv0 = argv[0];

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/graphics/object", Fixture, NULL,
              setup, test_object, teardown);
  g_test_add ("/graphics/good", Fixture, NULL,
              setup, test_good_graphics, teardown);
  g_test_add ("/graphics/bad", Fixture, NULL,
              setup, test_bad_graphics, teardown);
  g_test_add ("/graphics/hanging", Fixture, NULL,
              setup, test_timeout_graphics, teardown);
  g_test_add ("/graphics/software", Fixture, NULL,
              setup, test_software_rendering, teardown);

  g_test_add ("/graphics/vulkan", Fixture, NULL,
              setup, test_good_vulkan, teardown);
  g_test_add ("/graphics/vulkan-bad", Fixture, NULL,
              setup, test_bad_vulkan, teardown);
  g_test_add ("/graphics/vulkan-mixed", Fixture, NULL,
              setup, test_mixed_vulkan, teardown);

  g_test_add ("/graphics/icd/egl/basic", Fixture, NULL,
              setup, test_icd_egl, teardown);
  g_test_add ("/graphics/icd/egl/dirs", Fixture, &dir_config,
              setup, test_icd_egl, teardown);
  g_test_add ("/graphics/icd/egl/filenames", Fixture, &filename_config,
              setup, test_icd_egl, teardown);
  g_test_add ("/graphics/icd/egl/flatpak", Fixture, &flatpak_config,
              setup, test_icd_egl, teardown);
  g_test_add ("/graphics/icd/egl/relative", Fixture, &relative_config,
              setup, test_icd_egl, teardown);
  g_test_add ("/graphics/icd/egl/xdg", Fixture, &xdg_config,
              setup, test_icd_egl, teardown);
  g_test_add ("/graphics/icd/vulkan/basic", Fixture, NULL,
              setup, test_icd_vulkan, teardown);
  g_test_add ("/graphics/icd/vulkan/filenames", Fixture, &filename_config,
              setup, test_icd_vulkan, teardown);
  g_test_add ("/graphics/icd/vulkan/flatpak", Fixture, &flatpak_config,
              setup, test_icd_vulkan, teardown);
  g_test_add ("/graphics/icd/vulkan/relative", Fixture, &relative_config,
              setup, test_icd_vulkan, teardown);
  g_test_add ("/graphics/icd/vulkan/xdg", Fixture, &xdg_config,
              setup, test_icd_vulkan, teardown);

  return g_test_run ();
}
