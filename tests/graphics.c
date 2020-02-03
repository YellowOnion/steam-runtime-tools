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
  int exit_status;
  int terminating_signal;

  graphics = _srt_graphics_new("mock-good",
                               SRT_WINDOW_SYSTEM_GLX,
                               SRT_RENDERING_INTERFACE_GL,
                               SRT_GRAPHICS_LIBRARY_VENDOR_GLVND,
                               SRT_TEST_GOOD_GRAPHICS_RENDERER,
                               SRT_TEST_GOOD_GRAPHICS_VERSION,
                               SRT_GRAPHICS_ISSUES_NONE,
                               "",
                               0,
                               0);
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
  g_assert_cmpint (srt_graphics_get_exit_status (graphics), ==, 0);
  g_assert_cmpint (srt_graphics_get_terminating_signal (graphics), ==, 0);
  g_assert_true (vendor_neutral);
  g_object_get (graphics,
                "messages", &messages,
                "multiarch-tuple", &tuple,
                "issues", &issues,
                "library-vendor", &library_vendor,
                "renderer-string", &renderer,
                "version-string", &version,
                "exit-status", &exit_status,
                "terminating-signal", &terminating_signal,
                NULL);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_NONE);
  g_assert_cmpint (library_vendor, ==, SRT_GRAPHICS_LIBRARY_VENDOR_GLVND);
  g_assert_cmpstr (messages, ==, NULL);
  g_assert_cmpstr (tuple, ==, "mock-good");
  g_assert_cmpstr (renderer, ==, SRT_TEST_GOOD_GRAPHICS_RENDERER);
  g_assert_cmpstr (version, ==, SRT_TEST_GOOD_GRAPHICS_VERSION);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_cmpint (terminating_signal, ==, 0);
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
  gchar *messages;
  gchar *tuple;
  gchar *renderer;
  gchar *version;
  int exit_status;
  int terminating_signal;

  SrtSystemInfo *info = srt_system_info_new (NULL);
  srt_system_info_set_helpers_path (info, f->builddir);

  issues = srt_system_info_check_graphics (info,
                                           "mock-good",
                                           SRT_WINDOW_SYSTEM_GLX,
                                           SRT_RENDERING_INTERFACE_GL,
                                           &graphics);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_NONE);
  g_assert_cmpstr (srt_graphics_get_messages (graphics), ==,
                   NULL);
  g_assert_cmpstr (srt_graphics_get_renderer_string (graphics), ==,
                   SRT_TEST_GOOD_GRAPHICS_RENDERER);
  g_assert_cmpstr (srt_graphics_get_version_string (graphics), ==,
                   SRT_TEST_GOOD_GRAPHICS_VERSION);
  g_assert_cmpint (srt_graphics_get_exit_status (graphics), ==, 0);
  g_assert_cmpint (srt_graphics_get_terminating_signal (graphics), ==, 0);
  g_object_get (graphics,
                "messages", &messages,
                "multiarch-tuple", &tuple,
                "issues", &issues,
                "renderer-string", &renderer,
                "version-string", &version,
                "exit-status", &exit_status,
                "terminating-signal", &terminating_signal,
                NULL);
  g_assert_cmpstr (messages, ==, NULL);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_NONE);
  g_assert_cmpstr (tuple, ==, "mock-good");
  g_assert_cmpstr (renderer, ==, SRT_TEST_GOOD_GRAPHICS_RENDERER);
  g_assert_cmpstr (version, ==, SRT_TEST_GOOD_GRAPHICS_VERSION);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_cmpint (terminating_signal, ==, 0);
  g_free (tuple);
  g_free (renderer);
  g_free (version);
  g_free (messages);

  g_object_unref (graphics);
  g_object_unref (info);
}

/*
 * Test that the X11 window system is normalized correctly.
 */
static void
test_normalize_window_system (Fixture *f,
                              gconstpointer context)
{
  static struct TestVector
  {
    const char *description;
    /* These nested structs are just to provide an obvious visual
     * distinction between input and output. */
    struct
    {
      SrtWindowSystem winsys;
      SrtRenderingInterface iface;
    } in;
    struct
    {
      SrtWindowSystem winsys;
      const char *parameters_string;
    } out;
  } test_vectors[] =
  {
    /*
     * winsys  iface->   GL    |    GLESv2        |  Vulkan        |
     * ----------+-------------+------------------+----------------+
     * X11       |   (glx/gl)  | (egl_x11/glesv2) | x11/vulkan     |
     * GLX       |    glx/gl   |      (!)         |    (!)         |
     * EGL_X11   |  egl_x11/gl | egl_x11/glesv2   |    (!)         |
     * Wayland   |  wayland/gl | wayland/glesv2   | wayland/vulkan |
     *
     * (We don't implement Wayland yet, but if we did, it would behave
     * like this.)
     *
     * Key: (x): alias for x; (!): invalid/makes no sense
     */

    { "X11/GL is shorthand for GLX/GL",
      { SRT_WINDOW_SYSTEM_X11, SRT_RENDERING_INTERFACE_GL },
      { SRT_WINDOW_SYSTEM_GLX, "glx/gl" } },
    { "X11/GLESv2 is shorthand for EGL_X11/GLESv2",
      { SRT_WINDOW_SYSTEM_X11, SRT_RENDERING_INTERFACE_GLESV2 },
      { SRT_WINDOW_SYSTEM_EGL_X11, "egl_x11/glesv2" } },
    { "X11/Vulkan is neither GLX nor EGL",
      { SRT_WINDOW_SYSTEM_X11, SRT_RENDERING_INTERFACE_VULKAN },
      { SRT_WINDOW_SYSTEM_X11, "x11/vulkan" } },

    { "GLX/GL can be selected explicitly",
      { SRT_WINDOW_SYSTEM_GLX, SRT_RENDERING_INTERFACE_GL },
      { SRT_WINDOW_SYSTEM_GLX, "glx/gl" } },
    /* GLX/GLESv2 omitted: doesn't work in practice */
    /* GLX/Vulkan omitted: makes no sense */

    { "EGL_X11/GLESv2 can be selected explicitly",
      { SRT_WINDOW_SYSTEM_EGL_X11, SRT_RENDERING_INTERFACE_GLESV2 },
      { SRT_WINDOW_SYSTEM_EGL_X11, "egl_x11/glesv2" } },
    { "EGL_X11/GL can be selected explicitly",
      { SRT_WINDOW_SYSTEM_EGL_X11, SRT_RENDERING_INTERFACE_GL },
      { SRT_WINDOW_SYSTEM_EGL_X11, "egl_x11/gl" } },
    /* EGL_X11/Vulkan omitted: makes no sense */

    /* Wayland row omitted: not implemented yet (scout's libwayland-*
     * are too old) */
  };
  gsize i;
  SrtSystemInfo *info = srt_system_info_new (NULL);

  srt_system_info_set_helpers_path (info, f->builddir);

  for (i = 0; i < G_N_ELEMENTS (test_vectors); i++)
    {
      const struct TestVector *test_vector = &test_vectors[i];
      SrtGraphics *graphics = NULL;
      SrtWindowSystem winsys;
      SrtRenderingInterface iface;
      gchar *tmp;

      g_test_message ("%s", test_vector->description);

      srt_system_info_check_graphics (info,
                                      "mock-good",
                                      test_vector->in.winsys,
                                      test_vector->in.iface,
                                      &graphics);
      g_assert_nonnull (graphics);
      g_assert_cmpint (srt_graphics_get_rendering_interface (graphics), ==,
                       test_vector->in.iface);
      g_assert_cmpint (srt_graphics_get_window_system (graphics), ==,
                       test_vector->out.winsys);
      tmp = srt_graphics_dup_parameters_string (graphics);
      g_assert_cmpstr (tmp, ==, test_vector->out.parameters_string);
      g_free (tmp);
      g_object_get (graphics,
                    "rendering-interface", &iface,
                    "window-system", &winsys,
                    NULL);
      g_assert_cmpint (iface, ==, test_vector->in.iface);
      g_assert_cmpint (winsys, ==, test_vector->out.winsys);
      g_clear_object (&graphics);

      srt_system_info_check_graphics (info,
                                      "mock-bad",
                                      test_vector->in.winsys,
                                      test_vector->in.iface,
                                      &graphics);
      g_assert_nonnull (graphics);
      g_assert_cmpint (srt_graphics_get_rendering_interface (graphics), ==,
                       test_vector->in.iface);
      g_assert_cmpint (srt_graphics_get_window_system (graphics), ==,
                       test_vector->out.winsys);
      tmp = srt_graphics_dup_parameters_string (graphics);
      g_assert_cmpstr (tmp, ==, test_vector->out.parameters_string);
      g_free (tmp);
      g_object_get (graphics,
                    "rendering-interface", &iface,
                    "window-system", &winsys,
                    NULL);
      g_assert_cmpint (iface, ==, test_vector->in.iface);
      g_assert_cmpint (winsys, ==, test_vector->out.winsys);
      g_clear_object (&graphics);
    }

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
  int exit_status;
  int terminating_signal;

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
                   "warning: this warning should always be logged\n"
                   "Waffle error: 0x2 WAFFLE_ERROR_UNKNOWN: XOpenDisplay failed\n"
                  "info: you used LIBGL_DEBUG=verbose\n");
  g_assert_cmpint (srt_graphics_get_exit_status (graphics), ==, 1);
  g_assert_cmpint (srt_graphics_get_terminating_signal (graphics), ==, 0);

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
                "exit-status", &exit_status,
                "terminating-signal", &terminating_signal,
                NULL);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_CANNOT_LOAD);
  g_assert_cmpint (library_vendor, ==, SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN);
  g_assert_cmpstr (messages, ==,
                   "warning: this warning should always be logged\n"
                   "Waffle error: 0x2 WAFFLE_ERROR_UNKNOWN: XOpenDisplay failed\n"
                  "info: you used LIBGL_DEBUG=verbose\n");
  g_assert_cmpstr (tuple, ==, "mock-bad");
  g_assert_cmpstr (renderer, ==, NULL);
  g_assert_cmpstr (version, ==, NULL);
  g_assert_cmpint (exit_status, ==, 1);
  g_assert_cmpint (terminating_signal, ==, 0);
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
  int exit_status;
  int terminating_signal;

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
  // Timeout has exit code 124
  g_assert_cmpint (srt_graphics_get_exit_status (graphics), ==, 124);
  g_assert_cmpint (srt_graphics_get_terminating_signal (graphics), ==, 0);
  g_object_get (graphics,
                "multiarch-tuple", &tuple,
                "issues", &issues,
                "renderer-string", &renderer,
                "version-string", &version,
                "exit-status", &exit_status,
                "terminating-signal", &terminating_signal,
                NULL);
  g_assert_cmphex ((issues & SRT_GRAPHICS_ISSUES_CANNOT_LOAD), ==, SRT_GRAPHICS_ISSUES_CANNOT_LOAD);
  g_assert_cmphex ((issues & SRT_GRAPHICS_ISSUES_TIMEOUT), ==, SRT_GRAPHICS_ISSUES_TIMEOUT);
  g_assert_cmpstr (tuple, ==, "mock-hanging");
  g_assert_cmpstr (renderer, ==, NULL);
  g_assert_cmpstr (version, ==, NULL);
  g_assert_cmpint (exit_status, ==, 124);
  g_assert_cmpint (terminating_signal, ==, 0);
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
  gchar *messages;
  gchar *tuple;
  gchar *renderer;
  gchar *version;
  int exit_status;
  int terminating_signal;

  SrtSystemInfo *info = srt_system_info_new (NULL);
  srt_system_info_set_helpers_path (info, f->builddir);

  issues = srt_system_info_check_graphics (info,
                                           "mock-software",
                                           SRT_WINDOW_SYSTEM_GLX,
                                           SRT_RENDERING_INTERFACE_GL,
                                           &graphics);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_SOFTWARE_RENDERING);
  g_assert_cmpstr (srt_graphics_get_messages (graphics), ==,
                   "warning: this warning should always be logged\n"
                   "info: you used LIBGL_DEBUG=verbose\n");
  g_assert_cmpstr (srt_graphics_get_renderer_string (graphics), ==,
                   SRT_TEST_SOFTWARE_GRAPHICS_RENDERER);
  g_assert_cmpstr (srt_graphics_get_version_string (graphics), ==,
                   SRT_TEST_SOFTWARE_GRAPHICS_VERSION);
  g_assert_cmpint (srt_graphics_get_exit_status (graphics), ==, 0);
  g_assert_cmpint (srt_graphics_get_terminating_signal (graphics), ==, 0);

  g_object_get (graphics,
                "messages", &messages,
                "multiarch-tuple", &tuple,
                "issues", &issues,
                "renderer-string", &renderer,
                "version-string", &version,
                "exit-status", &exit_status,
                "terminating-signal", &terminating_signal,
                NULL);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_SOFTWARE_RENDERING);
  g_assert_cmpstr (tuple, ==, "mock-software");
  g_assert_cmpstr (renderer, ==, SRT_TEST_SOFTWARE_GRAPHICS_RENDERER);
  g_assert_cmpstr (version, ==, SRT_TEST_SOFTWARE_GRAPHICS_VERSION);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_cmpint (terminating_signal, ==, 0);
  g_assert_cmpstr (messages, ==,
                   "warning: this warning should always be logged\n"
                   "info: you used LIBGL_DEBUG=verbose\n");

  g_free (tuple);
  g_free (renderer);
  g_free (version);
  g_free (messages);

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
  int exit_status;
  int terminating_signal;

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
  g_assert_cmpint (srt_graphics_get_exit_status (graphics), ==, 0);
  g_assert_cmpint (srt_graphics_get_terminating_signal (graphics), ==, 0);

  g_object_get (graphics,
                "multiarch-tuple", &tuple,
                "issues", &issues,
                "renderer-string", &renderer,
                "version-string", &version,
                "exit-status", &exit_status,
                "terminating-signal", &terminating_signal,
                NULL);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_NONE);
  g_assert_cmpstr (tuple, ==, "mock-good");
  g_assert_cmpstr (renderer, ==, SRT_TEST_GOOD_GRAPHICS_RENDERER);
  g_assert_cmpstr (version, ==, SRT_TEST_GOOD_VULKAN_VERSION);
  g_assert_cmpint (exit_status, ==, 0);
  g_assert_cmpint (terminating_signal, ==, 0);
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
  int exit_status;
  int terminating_signal;

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
  g_assert_cmpint (srt_graphics_get_exit_status (graphics), ==, 1);
  g_assert_cmpint (srt_graphics_get_terminating_signal (graphics), ==, 0);

  g_object_get (graphics,
                "multiarch-tuple", &tuple,
                "issues", &issues,
                "renderer-string", &renderer,
                "version-string", &version,
                "exit-status", &exit_status,
                "terminating-signal", &terminating_signal,
                NULL);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_CANNOT_LOAD);
  g_assert_cmpstr (tuple, ==, "mock-bad");
  g_assert_cmpstr (renderer, ==, NULL);
  g_assert_cmpstr (version, ==, NULL);
  g_assert_cmpint (exit_status, ==, 1);
  g_assert_cmpint (terminating_signal, ==, 0);
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
  int exit_status;
  int terminating_signal;

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
  g_assert_cmpint (srt_graphics_get_exit_status (graphics), ==, 1);
  g_assert_cmpint (srt_graphics_get_terminating_signal (graphics), ==, 0);

  g_object_get (graphics,
                "multiarch-tuple", &tuple,
                "issues", &issues,
                "renderer-string", &renderer,
                "version-string", &version,
                "exit-status", &exit_status,
                "terminating-signal", &terminating_signal,
                NULL);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_CANNOT_DRAW);
  g_assert_cmpstr (tuple, ==, "mock-mixed");
  g_assert_cmpstr (renderer, ==, SRT_TEST_GOOD_GRAPHICS_RENDERER);
  g_assert_cmpstr (version, ==, SRT_TEST_GOOD_VULKAN_VERSION);
  g_assert_cmpint (exit_status, ==, 1);
  g_assert_cmpint (terminating_signal, ==, 0);

  g_free (tuple);
  g_free (renderer);
  g_free (version);

  g_object_unref (graphics);
  g_object_unref (info);
}

/*
 * Test a mock system with gl driver but check-gl failure
 */
static void
test_mixed_gl (Fixture *f,
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
                                           SRT_WINDOW_SYSTEM_GLX,
                                           SRT_RENDERING_INTERFACE_GL,
                                           &graphics);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_CANNOT_DRAW);
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
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_CANNOT_DRAW);
  g_assert_cmpstr (tuple, ==, "mock-mixed");
  g_assert_cmpstr (renderer, ==, SRT_TEST_GOOD_GRAPHICS_RENDERER);
  g_assert_cmpstr (version, ==, SRT_TEST_GOOD_GRAPHICS_VERSION);
  g_free (tuple);
  g_free (renderer);
  g_free (version);

  g_object_unref (graphics);
  g_object_unref (info);
}

/*
 * Test a mock system with sigusr terminating signal
 */
static void
test_sigusr (Fixture *f,
               gconstpointer context)
{
  SrtGraphics *graphics = NULL;
  SrtGraphicsIssues issues;
  gchar *tuple;
  int exit_status;
  int terminating_signal;

  SrtSystemInfo *info = srt_system_info_new (NULL);
  srt_system_info_set_helpers_path (info, f->builddir);

  issues = srt_system_info_check_graphics (info,
                                           "mock-sigusr",
                                           SRT_WINDOW_SYSTEM_GLX,
                                           SRT_RENDERING_INTERFACE_GL,
                                           &graphics);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_CANNOT_LOAD);
  g_object_get (graphics,
                "multiarch-tuple", &tuple,
                "issues", &issues,
                "exit-status", &exit_status,
                "terminating-signal", &terminating_signal,
                NULL);
  g_assert_cmpint (issues, ==, SRT_GRAPHICS_ISSUES_CANNOT_LOAD);
  g_assert_cmpstr (tuple, ==, "mock-sigusr");
  /* Depending on the version of timeout(1), it will have either
   * exited with status 128 + SIGUSR1, or killed itself with SIGUSR1 */
  if (exit_status != -1)
    {
      g_assert_cmpint (exit_status, ==, 128 + SIGUSR1);
    }
  g_assert_cmpint (terminating_signal, ==, SIGUSR1);
  g_free (tuple);

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
      srt_egl_icd_check_error (icd, &error);
      g_assert_no_error (error);
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
      srt_vulkan_icd_check_error (icd, &error);
      g_assert_no_error (error);
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

static void
check_list_suffixes (const GList *list,
                     const gchar *suffixes[],
                     SrtGraphicsModule module)
{
  const gchar *value = NULL;
  gsize i = 0;
  for (const GList *iter = list; iter != NULL; iter = iter->next, i++)
    {
      if (module == SRT_GRAPHICS_DRI_MODULE)
        value = srt_dri_driver_get_library_path (iter->data);
      else if (module == SRT_GRAPHICS_VAAPI_MODULE)
        value = srt_va_api_driver_get_library_path (iter->data);
      else if (module == SRT_GRAPHICS_VDPAU_MODULE)
        value = srt_vdpau_driver_get_library_path (iter->data);
      g_assert_nonnull (suffixes[i]);
      g_assert_true (g_str_has_suffix (value, suffixes[i]));
    }
  g_assert_cmpstr (suffixes[i], ==, NULL);
}

static void
check_list_extra (const GList *list,
                  gsize non_extra,
                  SrtGraphicsModule module)
{
  gsize i = 0;
  for (const GList *iter = list; iter != NULL; iter = iter->next, i++)
    {
      gboolean is_extra = (i >= non_extra);
      switch (module)
        {
          case SRT_GRAPHICS_DRI_MODULE:
            g_assert_cmpint (is_extra, ==, srt_dri_driver_is_extra (iter->data));
            break;

          case SRT_GRAPHICS_VAAPI_MODULE:
            g_assert_cmpint (is_extra, ==, srt_va_api_driver_is_extra (iter->data));
            break;

          case SRT_GRAPHICS_VDPAU_MODULE:
            g_assert_cmpint (is_extra, ==, srt_vdpau_driver_is_extra (iter->data));
            break;

          default:
            g_return_if_reached ();
        }
    }
}

static void
check_list_links (const GList *list,
                  const gchar *suffixes[],
                  SrtGraphicsModule module)
{
  const gchar *value = NULL;
  gsize i = 0;
  for (const GList *iter = list; iter != NULL; iter = iter->next)
    {
      switch (module)
        {
          case SRT_GRAPHICS_VDPAU_MODULE:
            value = srt_vdpau_driver_get_library_link (iter->data);
            break;

          case SRT_GRAPHICS_VAAPI_MODULE:
          case SRT_GRAPHICS_DRI_MODULE:
          default:
            g_return_if_reached ();
        }
      /* If we don't expect any more symlinks, then "value" should be NULL too */
      if (suffixes[i] == NULL)
        g_assert_cmpstr (value, ==, NULL);
      if (value == NULL)
        continue;
      g_assert_true (g_str_has_suffix (value, suffixes[i]));
      i++;
    }
  g_assert_cmpstr (suffixes[i], ==, NULL);
}

static void
test_dri_debian10 (Fixture *f,
                   gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **envp;
  gchar *sysroot;
  GList *dri;
  GList *va_api;
  const gchar *multiarch_tuples[] = {"mock-debian-i386", "mock-debian-x86_64", NULL};
  const gchar *dri_suffixes_i386[] = {"/lib/i386-linux-gnu/dri/i965_dri.so",
                                      "/lib/i386-linux-gnu/dri/r300_dri.so",
                                      "/lib/i386-linux-gnu/dri/radeonsi_dri.so",
                                      NULL};
  const gchar *dri_suffixes_x86_64[] = {"/lib/x86_64-linux-gnu/dri/i965_dri.so",
                                        "/lib/x86_64-linux-gnu/dri/r600_dri.so",
                                        "/lib/x86_64-linux-gnu/dri/radeon_dri.so",
                                        NULL};
  const gchar *va_api_suffixes_i386[] = {"/lib/i386-linux-gnu/dri/r600_drv_video.so",
                                         NULL};
  const gchar *va_api_suffixes_x86_64[] = {"/lib/x86_64-linux-gnu/dri/r600_drv_video.so",
                                           "/lib/x86_64-linux-gnu/dri/radeonsi_drv_video.so",
                                           NULL};

  sysroot = g_build_filename (f->srcdir, "sysroots", "debian10", NULL);
  envp = g_get_environ ();
  envp = g_environ_setenv (envp, "SRT_TEST_SYSROOT", sysroot, TRUE);
  envp = g_environ_unsetenv (envp, "LIBGL_DRIVERS_PATH");
  envp = g_environ_unsetenv (envp, "LIBVA_DRIVERS_PATH");

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, envp);
  srt_system_info_set_helpers_path (info, f->builddir);

  /* The output is guaranteed to be in aphabetical order */
  dri = srt_system_info_list_dri_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (dri, dri_suffixes_i386, SRT_GRAPHICS_DRI_MODULE);
  g_list_free_full (dri, g_object_unref);

  dri = srt_system_info_list_dri_drivers (info, multiarch_tuples[1], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (dri, dri_suffixes_x86_64, SRT_GRAPHICS_DRI_MODULE);
  g_list_free_full (dri, g_object_unref);

  /* The output is guaranteed to be in aphabetical order */
  va_api = srt_system_info_list_va_api_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (va_api, va_api_suffixes_i386, SRT_GRAPHICS_VAAPI_MODULE);
  g_list_free_full (va_api, g_object_unref);

  va_api = srt_system_info_list_va_api_drivers (info, multiarch_tuples[1], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (va_api, va_api_suffixes_x86_64, SRT_GRAPHICS_VAAPI_MODULE);
  g_list_free_full (va_api, g_object_unref);

  /* Do it again, this time using the cached result.
   * While doing it we also try to get the "extra" drivers.
   * We expect to receive the same drivers list as before because we are using
   * a multiarch tuple that is different from what we have in debian10/usr/lib
   * so _srt_get_extra_modules_folder will fail to split the path.
   * Anyway, even if the folder had the same name as the multiarch tuple,
   * we still would be unable to get extras because the drivers that we are
   * using (e.g. libGL.so.1) are just empty files, so `elf_begin` would fail. */
  dri = srt_system_info_list_dri_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_INCLUDE_ALL);
  check_list_suffixes (dri, dri_suffixes_i386, SRT_GRAPHICS_DRI_MODULE);
  g_list_free_full (dri, g_object_unref);

  dri = srt_system_info_list_dri_drivers (info, multiarch_tuples[1], SRT_DRIVER_FLAGS_INCLUDE_ALL);
  check_list_suffixes (dri, dri_suffixes_x86_64, SRT_GRAPHICS_DRI_MODULE);
  g_list_free_full (dri, g_object_unref);

  va_api = srt_system_info_list_va_api_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_INCLUDE_ALL);
  check_list_suffixes (va_api, va_api_suffixes_i386, SRT_GRAPHICS_VAAPI_MODULE);
  g_list_free_full (va_api, g_object_unref);

  va_api = srt_system_info_list_va_api_drivers (info, multiarch_tuples[1], SRT_DRIVER_FLAGS_INCLUDE_ALL);
  check_list_suffixes (va_api, va_api_suffixes_x86_64, SRT_GRAPHICS_VAAPI_MODULE);
  g_list_free_full (va_api, g_object_unref);

  g_object_unref (info);
  g_free (sysroot);
  g_strfreev (envp);
}

static void
test_dri_fedora (Fixture *f,
                 gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **envp;
  gchar *sysroot;
  GList *dri;
  GList *va_api;
  const gchar *multiarch_tuples[] = {"mock-fedora-32-bit", "mock-fedora-64-bit", NULL};
  const gchar *dri_suffixes_32[] = {"/usr/lib/dri/i965_dri.so",
                                    "/usr/lib/dri/r300_dri.so",
                                    "/usr/lib/dri/radeonsi_dri.so",
                                    NULL};
  const gchar *dri_suffixes_64[] = {"/usr/lib64/dri/i965_dri.so",
                                    "/usr/lib64/dri/r600_dri.so",
                                    "/usr/lib64/dri/radeon_dri.so",
                                    NULL};
  const gchar *va_api_suffixes_32[] = {"/usr/lib/dri/r600_drv_video.so",
                                       NULL};
  const gchar *va_api_suffixes_64[] = {"/usr/lib64/dri/r600_drv_video.so",
                                       "/usr/lib64/dri/radeonsi_drv_video.so",
                                       NULL};

  sysroot = g_build_filename (f->srcdir, "sysroots", "fedora", NULL);
  envp = g_get_environ ();
  envp = g_environ_setenv (envp, "SRT_TEST_SYSROOT", sysroot, TRUE);
  envp = g_environ_unsetenv (envp, "LIBGL_DRIVERS_PATH");
  envp = g_environ_unsetenv (envp, "LIBVA_DRIVERS_PATH");

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, envp);
  srt_system_info_set_helpers_path (info, f->builddir);

  dri = srt_system_info_list_dri_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (dri, dri_suffixes_32, SRT_GRAPHICS_DRI_MODULE);
  g_list_free_full (dri, g_object_unref);

  dri = srt_system_info_list_dri_drivers (info, multiarch_tuples[1], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (dri, dri_suffixes_64, SRT_GRAPHICS_DRI_MODULE);
  g_list_free_full (dri, g_object_unref);

  va_api = srt_system_info_list_va_api_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (va_api, va_api_suffixes_32, SRT_GRAPHICS_VAAPI_MODULE);
  g_list_free_full (va_api, g_object_unref);

  va_api = srt_system_info_list_va_api_drivers (info, multiarch_tuples[1], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (va_api, va_api_suffixes_64, SRT_GRAPHICS_VAAPI_MODULE);
  g_list_free_full (va_api, g_object_unref);

  g_object_unref (info);
  g_free (sysroot);
  g_strfreev (envp);
}

static void
test_dri_with_env (Fixture *f,
                   gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **envp;
  gchar *sysroot;
  gchar *libgl;
  gchar *libgl2;
  gchar *libgl3;
  gchar *libva;
  gchar *libva2;
  gchar *libva3;
  gchar *libgl_combined;
  gchar *libva_combined;
  GList *dri;
  GList *va_api;
  const gchar *multiarch_tuples[] = {"mock-fedora-32-bit", NULL};
  const gchar *dri_suffixes[] = {"/custom_path32/dri/r600_dri.so",
                                 "/custom_path32/dri/radeon_dri.so",
                                 "/custom_path32_2/dri/r300_dri.so",
                                 NULL};
  const gchar *dri_suffixes_with_extras[] = {"/custom_path32/dri/r600_dri.so",
                                             "/custom_path32/dri/radeon_dri.so",
                                             "/custom_path32_2/dri/r300_dri.so",
                                             "/usr/lib/dri/i965_dri.so",
                                             "/usr/lib/dri/radeonsi_dri.so",
                                             NULL};
  const gchar *va_api_suffixes[] = {"/custom_path32/va/r600_drv_video.so",
                                    "/custom_path32/va/radeonsi_drv_video.so",
                                    "/custom_path32_2/va/nouveau_drv_video.so",
                                    NULL};
  const gchar *va_api_suffixes_with_extras[] = {"/custom_path32/va/r600_drv_video.so",
                                                "/custom_path32/va/radeonsi_drv_video.so",
                                                "/custom_path32_2/va/nouveau_drv_video.so",
                                                "/usr/lib/dri/r600_drv_video.so",
                                                NULL};

  if (strcmp (_SRT_MULTIARCH, "") == 0)
    {
      g_test_skip ("Unsupported architecture");
      return;
    }

  sysroot = g_build_filename (f->srcdir, "sysroots", "no-os-release", NULL);

  libgl = g_build_filename (sysroot, "custom_path32", "dri", NULL);
  libva = g_build_filename (sysroot, "custom_path32", "va", NULL);
  libgl2 = g_build_filename (sysroot, "custom_path32_2", "dri", NULL);
  libva2 = g_build_filename (sysroot, "custom_path32_2", "va", NULL);
  /* We have these two 64bit directories but we are using only one mock 32bit executable.
   * So we expect to not receive the content of these directories because we should
   * find 32bit only libraries. */
  libgl3 = g_build_filename (sysroot, "custom_path64", "dri", NULL);
  libva3 = g_build_filename (sysroot, "custom_path64", "va", NULL);

  libgl_combined = g_strjoin (":", libgl, libgl2, libgl3, NULL);
  libva_combined = g_strjoin (":", libva, libva2, libgl3, NULL);

  envp = g_get_environ ();
  envp = g_environ_setenv (envp, "SRT_TEST_SYSROOT", sysroot, TRUE);
  envp = g_environ_setenv (envp, "LIBGL_DRIVERS_PATH", libgl_combined, TRUE);
  envp = g_environ_setenv (envp, "LIBVA_DRIVERS_PATH", libva_combined, TRUE);

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, envp);
  srt_system_info_set_helpers_path (info, f->builddir);

  /* The output is guaranteed to be in aphabetical order */
  dri = srt_system_info_list_dri_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (dri, dri_suffixes, SRT_GRAPHICS_DRI_MODULE);
  g_list_free_full (dri, g_object_unref);

  /* The output is guaranteed to be in aphabetical order */
  va_api = srt_system_info_list_va_api_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (va_api, va_api_suffixes, SRT_GRAPHICS_VAAPI_MODULE);
  g_list_free_full (va_api, g_object_unref);

  /* Do it again, this time including the extras */
  dri = srt_system_info_list_dri_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_INCLUDE_ALL);
  check_list_suffixes (dri, dri_suffixes_with_extras, SRT_GRAPHICS_DRI_MODULE);
  /* Minus one to not count the NULL terminator */
  check_list_extra (dri, G_N_ELEMENTS(dri_suffixes)-1, SRT_GRAPHICS_DRI_MODULE);
  g_list_free_full (dri, g_object_unref);

  va_api = srt_system_info_list_va_api_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_INCLUDE_ALL);
  check_list_suffixes (va_api, va_api_suffixes_with_extras, SRT_GRAPHICS_VAAPI_MODULE);
  /* Minus one to not count the NULL terminator */
  check_list_extra (va_api, G_N_ELEMENTS(va_api_suffixes)-1, SRT_GRAPHICS_VAAPI_MODULE);
  g_list_free_full (va_api, g_object_unref);

  g_object_unref (info);
  g_free (sysroot);
  g_free (libgl_combined);
  g_free (libva_combined);
  g_free (libgl);
  g_free (libgl2);
  g_free (libgl3);
  g_free (libva);
  g_free (libva2);
  g_free (libva3);
  g_strfreev (envp);
}

static void
test_dri_flatpak (Fixture *f,
                  gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **envp;
  gchar *sysroot;
  GList *dri;
  GList *va_api;
  const gchar *multiarch_tuples[] = { "mock-abi", NULL };
  const gchar *dri_suffixes[] = {"/usr/lib/mock-abi/GL/lib/dri/i965_dri.so",
                                  NULL};
  const gchar *va_api_suffixes[] = {"/usr/lib/mock-abi/dri/radeonsi_drv_video.so",
                                    "/usr/lib/mock-abi/dri/intel-vaapi-driver/i965_drv_video.so",
                                    "/usr/lib/mock-abi/GL/lib/dri/r600_drv_video.so",
                                    NULL};

  sysroot = g_build_filename (f->srcdir, "sysroots", "flatpak-example", NULL);
  envp = g_get_environ ();
  envp = g_environ_setenv (envp, "SRT_TEST_SYSROOT", sysroot, TRUE);
  envp = g_environ_unsetenv (envp, "LIBGL_DRIVERS_PATH");
  envp = g_environ_unsetenv (envp, "LIBVA_DRIVERS_PATH");

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, envp);
  srt_system_info_set_helpers_path (info, f->builddir);

  dri = srt_system_info_list_dri_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (dri, dri_suffixes, SRT_GRAPHICS_DRI_MODULE);
  g_list_free_full (dri, g_object_unref);

  va_api = srt_system_info_list_va_api_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (va_api, va_api_suffixes, SRT_GRAPHICS_VAAPI_MODULE);
  g_list_free_full (va_api, g_object_unref);

  g_object_unref (info);
  g_free (sysroot);
  g_strfreev (envp);
}

static void
test_vdpau_debian10 (Fixture *f,
                     gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **envp;
  gchar *sysroot;
  GList *vdpau;
  const gchar *multiarch_tuples[] = {"mock-debian-i386", "mock-debian-x86_64", NULL};
  const gchar *vdpau_suffixes_i386[] = {"/lib/i386-linux-gnu/vdpau/libvdpau_r600.so",
                                        "/lib/i386-linux-gnu/vdpau/libvdpau_radeonsi.so",
                                        "/lib/i386-linux-gnu/vdpau/libvdpau_radeonsi.so.1",
                                        NULL};
  const gchar *vdpau_suffixes_x86_64[] = {"/lib/x86_64-linux-gnu/vdpau/libvdpau_r600.so.1",
                                          "/lib/x86_64-linux-gnu/vdpau/libvdpau_radeonsi.so",
                                          "/lib/x86_64-linux-gnu/vdpau/libvdpau_radeonsi.so.1",
                                          NULL};
  /* These symlinks are provided by "libvdpau_radeonsi.so" and "libvdpau_radeonsi.so.1" */
  const gchar *vdpau_links_i386[] = {"libvdpau_radeonsi.so.1.0.0",
                                     "libvdpau_radeonsi.so.1.0.0",
                                     NULL};
  /* These symlinks are provided by "libvdpau_r600.so", "libvdpau_radeonsi.so"
   * and "libvdpau_radeonsi.so.1" */
  const gchar *vdpau_links_x86_64[] = {"libvdpau_r600.so.1.0.0",
                                       "libvdpau_radeonsi.so.1.0.0",
                                       "libvdpau_radeonsi.so.1.0.0",
                                       NULL};

  sysroot = g_build_filename (f->srcdir, "sysroots", "debian10", NULL);
  envp = g_get_environ ();
  envp = g_environ_setenv (envp, "SRT_TEST_SYSROOT", sysroot, TRUE);
  envp = g_environ_unsetenv (envp, "VDPAU_DRIVER_PATH");

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, envp);
  srt_system_info_set_helpers_path (info, f->builddir);

  vdpau = srt_system_info_list_vdpau_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (vdpau, vdpau_suffixes_i386, SRT_GRAPHICS_VDPAU_MODULE);
  check_list_links (vdpau, vdpau_links_i386, SRT_GRAPHICS_VDPAU_MODULE);
  g_list_free_full (vdpau, g_object_unref);

  vdpau = srt_system_info_list_vdpau_drivers (info, multiarch_tuples[1], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (vdpau, vdpau_suffixes_x86_64, SRT_GRAPHICS_VDPAU_MODULE);
  check_list_links (vdpau, vdpau_links_x86_64, SRT_GRAPHICS_VDPAU_MODULE);
  g_list_free_full (vdpau, g_object_unref);

  g_object_unref (info);
  g_free (sysroot);
  g_strfreev (envp);
}

static void
test_vdpau_fedora (Fixture *f,
                   gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **envp;
  gchar *sysroot;
  GList *vdpau;
  const gchar *multiarch_tuples[] = {"mock-fedora-32-bit", "mock-fedora-64-bit", NULL};
  const gchar *vdpau_suffixes_32[] = {"/usr/lib/vdpau/libvdpau_nouveau.so.1",
                                      "/usr/lib/vdpau/libvdpau_r600.so",
                                      "/usr/lib/vdpau/libvdpau_radeonsi.so",
                                      "/usr/lib/vdpau/libvdpau_radeonsi.so.1",
                                       NULL};
  const gchar *vdpau_suffixes_64[] = {"/usr/lib64/vdpau/libvdpau_r300.so",
                                      "/usr/lib64/vdpau/libvdpau_r300.so.1",
                                      "/usr/lib64/vdpau/libvdpau_radeonsi.so",
                                      "/usr/lib64/vdpau/libvdpau_radeonsi.so.1",
                                       NULL};
  /* These symlinks are provided by "libvdpau_radeonsi.so" and "libvdpau_radeonsi.so.1" */
  const gchar *vdpau_links_32[] = {"libvdpau_radeonsi.so.1.0.0",
                                   "libvdpau_radeonsi.so.1.0.0",
                                   NULL};
  /* These symlinks are provided by "libvdpau_r300.so.1" and "libvdpau_radeonsi.so.1" */
  const gchar *vdpau_links_64[] = {"libvdpau_r300.so",
                                   "libvdpau_radeonsi.so",
                                   NULL};

  sysroot = g_build_filename (f->srcdir, "sysroots", "fedora", NULL);
  envp = g_get_environ ();
  envp = g_environ_setenv (envp, "SRT_TEST_SYSROOT", sysroot, TRUE);
  envp = g_environ_unsetenv (envp, "VDPAU_DRIVER_PATH");

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, envp);
  srt_system_info_set_helpers_path (info, f->builddir);

  vdpau = srt_system_info_list_vdpau_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (vdpau, vdpau_suffixes_32, SRT_GRAPHICS_VDPAU_MODULE);
  check_list_links (vdpau, vdpau_links_32, SRT_GRAPHICS_VDPAU_MODULE);
  g_list_free_full (vdpau, g_object_unref);

  vdpau = srt_system_info_list_vdpau_drivers (info, multiarch_tuples[1], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (vdpau, vdpau_suffixes_64, SRT_GRAPHICS_VDPAU_MODULE);
  check_list_links (vdpau, vdpau_links_64, SRT_GRAPHICS_VDPAU_MODULE);
  g_list_free_full (vdpau, g_object_unref);

  g_object_unref (info);
  g_free (sysroot);
  g_strfreev (envp);
}

static void
test_vdpau_with_env (Fixture *f,
                     gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **envp;
  gchar *sysroot;
  gchar *vdpau_path;
  GList *vdpau;
  const gchar *multiarch_tuples[] = {"mock-fedora-32-bit", NULL};
  const gchar *vdpau_suffixes[] = {"/custom_path32/vdpau/libvdpau_r600.so.1",
                                   "/custom_path32/vdpau/libvdpau_radeonsi.so.1",
                                   NULL};
  const gchar *vdpau_suffixes_with_extras[] = {"/custom_path32/vdpau/libvdpau_r600.so.1",
                                               "/custom_path32/vdpau/libvdpau_radeonsi.so.1",
                                               "/usr/lib/vdpau/libvdpau_nouveau.so.1",
                                               NULL};
  /* There are no symlinks */
  const gchar *vdpau_links[] = {NULL};

  if (strcmp (_SRT_MULTIARCH, "") == 0)
    {
      g_test_skip ("Unsupported architecture");
      return;
    }

  sysroot = g_build_filename (f->srcdir, "sysroots", "no-os-release", NULL);

  vdpau_path = g_build_filename (sysroot, "custom_path32", "vdpau", NULL);

  envp = g_get_environ ();
  envp = g_environ_setenv (envp, "SRT_TEST_SYSROOT", sysroot, TRUE);
  envp = g_environ_setenv (envp, "VDPAU_DRIVER_PATH", vdpau_path, TRUE);

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, envp);
  srt_system_info_set_helpers_path (info, f->builddir);

  /* The output is guaranteed to be in aphabetical order */
  vdpau = srt_system_info_list_vdpau_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (vdpau, vdpau_suffixes, SRT_GRAPHICS_VDPAU_MODULE);
  check_list_links (vdpau, vdpau_links, SRT_GRAPHICS_VDPAU_MODULE);
  g_list_free_full (vdpau, g_object_unref);

  /* Do it again, this time including the extras */
  vdpau = srt_system_info_list_vdpau_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_INCLUDE_ALL);
  check_list_suffixes (vdpau, vdpau_suffixes_with_extras, SRT_GRAPHICS_VDPAU_MODULE);
  check_list_links (vdpau, vdpau_links, SRT_GRAPHICS_VDPAU_MODULE);
  /* Minus one to not count the NULL terminator */
  check_list_extra (vdpau, G_N_ELEMENTS(vdpau_suffixes)-1, SRT_GRAPHICS_VDPAU_MODULE);
  g_list_free_full (vdpau, g_object_unref);

  g_object_unref (info);
  g_free (sysroot);
  g_free (vdpau_path);
  g_strfreev (envp);
}

static void
test_vdpau_flatpak (Fixture *f,
                  gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **envp;
  gchar *sysroot;
  GList *vdpau;
  const gchar *multiarch_tuples[] = { "mock-abi", NULL };
  const gchar *vdpau_suffixes[] = {"/usr/lib/mock-abi/vdpau/libvdpau_radeonsi.so.1",
                                   NULL};

  sysroot = g_build_filename (f->srcdir, "sysroots", "flatpak-example", NULL);
  envp = g_get_environ ();
  envp = g_environ_setenv (envp, "SRT_TEST_SYSROOT", sysroot, TRUE);
  envp = g_environ_unsetenv (envp, "VDPAU_DRIVER_PATH");

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, envp);
  srt_system_info_set_helpers_path (info, f->builddir);

  vdpau = srt_system_info_list_vdpau_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (vdpau, vdpau_suffixes, SRT_GRAPHICS_VDPAU_MODULE);
  g_list_free_full (vdpau, g_object_unref);

  g_object_unref (info);
  g_free (sysroot);
  g_strfreev (envp);
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
  g_test_add ("/graphics/normalize_window_system", Fixture, NULL,
              setup, test_normalize_window_system, teardown);
  g_test_add ("/graphics/gl-mixed", Fixture, NULL,
              setup, test_mixed_gl, teardown);
  g_test_add ("/graphics/sigusr", Fixture, NULL,
              setup, test_sigusr, teardown);

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

  g_test_add ("/graphics/dri/debian10", Fixture, NULL,
              setup, test_dri_debian10, teardown);
  g_test_add ("/graphics/dri/fedora", Fixture, NULL,
              setup, test_dri_fedora, teardown);
  g_test_add ("/graphics/dri/with_env", Fixture, NULL,
              setup, test_dri_with_env, teardown);
  g_test_add ("/graphics/dri/flatpak", Fixture, NULL,
              setup, test_dri_flatpak, teardown);

  g_test_add ("/graphics/vdpau/debian10", Fixture, NULL,
              setup, test_vdpau_debian10, teardown);
  g_test_add ("/graphics/vdpau/fedora", Fixture, NULL,
              setup, test_vdpau_fedora, teardown);
  g_test_add ("/graphics/vdpau/with_env", Fixture, NULL,
              setup, test_vdpau_with_env, teardown);
  g_test_add ("/graphics/vdpau/flatpak", Fixture, NULL,
              setup, test_vdpau_flatpak, teardown);

  return g_test_run ();
}
