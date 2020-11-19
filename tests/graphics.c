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

/* Include these before steam-runtime-tools.h so that its backport of
 * G_DEFINE_AUTOPTR_CLEANUP_FUNC will be visible to it */
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <libelf.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "steam-runtime-tools/graphics-internal.h"
#include "steam-runtime-tools/graphics-test-defines.h"
#include "steam-runtime-tools/system-info.h"
#include "test-utils.h"

static const char *argv0;
static gchar *global_sysroots;

typedef struct
{
  gchar *srcdir;
  gchar *builddir;
  const gchar *sysroots;
  gchar *sysroot;
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

  f->srcdir = g_strdup (g_getenv ("G_TEST_SRCDIR"));
  f->builddir = g_strdup (g_getenv ("G_TEST_BUILDDIR"));

  if (f->srcdir == NULL)
    f->srcdir = g_path_get_dirname (argv0);

  if (f->builddir == NULL)
    f->builddir = g_path_get_dirname (argv0);

  f->sysroots = global_sysroots;

  if (g_chdir (f->srcdir) != 0)
    g_error ("chdir %s: %s", f->srcdir, g_strerror (errno));

  f->fake_icds_envp = g_get_environ ();

  if (config == NULL || config->icd_mode != ICD_MODE_RELATIVE_FILENAMES)
    {
      f->sysroot = g_build_filename (f->srcdir, "fake-icds", NULL);
      /* Some of the mock helper programs rely on this, so we set it
       * even though SrtSystemInfo doesn't use it any more */
      f->fake_icds_envp = g_environ_setenv (f->fake_icds_envp,
                                            "SRT_TEST_SYSROOT", f->sysroot, TRUE);
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
  g_free (f->sysroot);
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
  srt_system_info_set_sysroot (info, f->sysroot);

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
  srt_system_info_set_sysroot (info, f->sysroot);

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

typedef struct
{
  const gchar *name;
  const gchar *description;
  const gchar *library_path;
  const gchar *api_version;
  const gchar *component_layers[5];
  /* This needs to be an explicit value because, if in input we had a single
   * JSON with multiple layers, we write it to the filesystem as separated JSON
   * files. So the output is not always exactly the same as the input JSON */
  const gchar *json_to_compare;
  const gchar *error_message_suffix;
  const gchar *error_domain;
  gint error_code;
} VulkanLayerTest;

typedef struct
{
  const gchar *description;
  const gchar *sysroot;
  const gchar *vk_layer_path;
  const gchar *home;
  const gchar *xdg_config_dirs;
  const gchar *xdg_data_dirs;
  VulkanLayerTest explicit_layers[5];
  VulkanLayerTest implicit_layers[5];
} VulkanLayersTest;

static const VulkanLayersTest vulkan_layers_test[] =
{
  {
    .description = "Good single VK_LAYER_PATH dir",
    .sysroot = "debian10",
    .vk_layer_path = "/custom_path",
    .explicit_layers =
    {
      {
        .name = "VK_LAYER_MANGOHUD_overlay",
        .description = "Vulkan Hud Overlay",
        .library_path = "/usr/$LIB/libMangoHud.so",
        .api_version = "1.2.135",
        .json_to_compare = "expectations/MangoHud.json",
      },
      {
        .name = "VK_LAYER_LUNARG_overlay",
        .description = "LunarG HUD layer",
        .library_path = "vkOverlayLayer.so",
        .api_version = "1.1.5",
        .json_to_compare = "custom_path/Single-good-layer.json",
      },
    },
    /* Implicit layers are not affected by VK_LAYER_PATH env */
    .implicit_layers =
    {
      {
        .name = "VK_LAYER_first",
        .description = "Vulkan first layer",
        .library_path = "libFirst.so",
        .api_version = "1.0.13",
        .json_to_compare = "expectations/MultiLayers_part1.json",
      },
      {
        .name = "VK_LAYER_second",
        .description = "Vulkan second layer",
        .library_path = "libSecond.so",
        .api_version = "1.0.13",
        .json_to_compare = "expectations/MultiLayers_part2.json",
      },
    },
  },

  {
    .description = "Good implicit dirs",
    .sysroot = "debian10",
    .home = "/home/debian",
    .xdg_config_dirs = "/usr/local/etc:::",
    .explicit_layers =
    {
      {
        .name = "VK_LAYER_MESA_overlay",
        .description = "Mesa Overlay layer",
        .library_path = "libVkLayer_MESA_overlay.so",
        .api_version = "1.1.73",
        .json_to_compare = "usr/local/etc/vulkan/explicit_layer.d/VkLayer_MESA_overlay.json",
      },
    },
    .implicit_layers =
    {
      {
        .name = "VK_LAYER_first",
        .description = "Vulkan first layer",
        .library_path = "libFirst.so",
        .api_version = "1.0.13",
        .json_to_compare = "expectations/MultiLayers_part1.json",
      },
      {
        .name = "VK_LAYER_second",
        .description = "Vulkan second layer",
        .library_path = "libSecond.so",
        .api_version = "1.0.13",
        .json_to_compare = "expectations/MultiLayers_part2.json",
      },
      {
        .name = "VK_LAYER_VALVE_steam_overlay_64",
        .description = "Steam Overlay Layer",
        .library_path = "/home/debian/.local/share/Steam/ubuntu12_64/steamoverlayvulkanlayer.so",
        .api_version = "1.2.136",
        .json_to_compare = "home/debian/.local/share/vulkan/implicit_layer.d/steamoverlay_x86_64.json",
      },
    },
  },

  {
    .description = "Layers with missing required fields and unsupported version",
    .sysroot = "fedora",
    .implicit_layers =
    {
      // incomplete_layer.json
      {
        .error_message_suffix = "cannot be parsed because it is missing a required field",
        .error_domain = "g-io-error-quark",
        .error_code = G_IO_ERROR_FAILED,
      },
      // newer_layer.json
      {
        .error_message_suffix = "is not supported",
        .error_domain = "g-io-error-quark",
        .error_code = G_IO_ERROR_FAILED,
      },
    },
  },

  {
    .description = "Meta layer",
    .sysroot = "fedora",
    .vk_layer_path = "/custom_path",
    .explicit_layers =
    {
      {
        .name = "VK_LAYER_META_layer",
        .description = "Meta-layer example",
        .api_version = "1.0.9000",
        .component_layers =
        {
          "VK_LAYER_KHRONOS_validation",
          "VK_LAYER_LUNARG_api_dump",
        },
        .json_to_compare = "custom_path/meta_layer.json",
      },
    },
  },

};

static void
_test_layer_values (SrtVulkanLayer *layer,
                    const VulkanLayerTest *test,
                    const gchar *test_dir,
                    const gchar *sysroot)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(SrtVulkanLayer) layer_dup = NULL;

  if (test->error_message_suffix != NULL)
    {
      g_assert_false (srt_vulkan_layer_check_error (layer, &error));
      g_assert_true (g_str_has_suffix (error->message, test->error_message_suffix));
      g_assert_cmpstr (g_quark_to_string (error->domain), ==, test->error_domain);
      g_assert_cmpint (error->code, ==, test->error_code);
      return;
    }

  g_assert_cmpstr (test->name, ==, srt_vulkan_layer_get_name (layer));
  g_assert_cmpstr (test->description, ==,
                    srt_vulkan_layer_get_description (layer));
  g_assert_cmpstr (test->library_path, ==,
                    srt_vulkan_layer_get_library_path (layer));
  g_assert_cmpstr (test->api_version, ==,
                    srt_vulkan_layer_get_api_version (layer));

  layer_dup = srt_vulkan_layer_new_replace_library_path (layer,
                                                         "/run/host/vulkan_layer.json");

  g_assert_cmpstr (test->name, ==, srt_vulkan_layer_get_name (layer_dup));
  g_assert_cmpstr (test->description, ==,
                    srt_vulkan_layer_get_description (layer_dup));
  g_assert_cmpstr (test->api_version, ==,
                    srt_vulkan_layer_get_api_version (layer_dup));

  /* If library_path was NULL, this means we have a meta-layer. So even
   * after calling the replace function we still expect to have a
   * NULL library_path. */
  if (test->library_path == NULL)
    g_assert_cmpstr (NULL, ==, srt_vulkan_layer_get_library_path (layer_dup));
  else
    g_assert_cmpstr ("/run/host/vulkan_layer.json", ==,
                      srt_vulkan_layer_get_library_path (layer_dup));

  if (test->json_to_compare != NULL)
    {
      g_autofree gchar *input_contents = NULL;
      g_autofree gchar *output_contents = NULL;
      g_autofree gchar *input_json = NULL;
      g_autofree gchar *output_file = NULL;
      g_autoptr(JsonParser) parser = NULL;
      g_autoptr(JsonGenerator) generator = NULL;
      JsonNode *node = NULL;  /* not owned */

      output_file = g_build_filename (test_dir, test->name, NULL);

      srt_vulkan_layer_write_to_file (layer, output_file, &error);
      g_assert_no_error (error);
      g_file_get_contents (output_file, &output_contents, NULL, &error);
      g_assert_no_error (error);

      input_json = g_build_filename (sysroot, test->json_to_compare, NULL);
      parser = json_parser_new ();
      json_parser_load_from_file (parser, input_json, &error);
      g_assert_no_error (error);
      node = json_parser_get_root (parser);
      g_assert_nonnull (node);
      generator = json_generator_new ();
      json_generator_set_root (generator, node);
      json_generator_set_pretty (generator, TRUE);
      input_contents = json_generator_to_data (generator, NULL);

      g_assert_cmpstr (input_contents, ==, output_contents);
    }
}

static void
test_layer_vulkan (Fixture *f,
                   gconstpointer context)
{
  g_auto(GStrv) vulkan_layer_envp = g_get_environ ();
  g_autofree gchar *tmp_dir = NULL;
  g_autoptr(GError) error = NULL;
  gsize i;

  tmp_dir = g_dir_make_tmp ("layers-test-XXXXXX", &error);
  g_assert_no_error (error);

  for (i = 0; i < G_N_ELEMENTS (vulkan_layers_test); i++)
    {
      const VulkanLayersTest *test = &vulkan_layers_test[i];
      VulkanLayerTest layer_test;
      g_autoptr(SrtSystemInfo) info = NULL;
      g_autoptr(SrtObjectList) explicit_layers = NULL;
      g_autoptr(SrtObjectList) implicit_layers = NULL;
      g_autofree gchar *sysroot = NULL;
      /* Create a new empty temp sub directory for every test */
      g_autofree gchar *test_num = g_strdup_printf ("%" G_GSIZE_FORMAT, i);
      g_autofree gchar *this_test_dir = g_build_filename (tmp_dir, test_num, NULL);
      const GList *iter;
      gsize j;

      g_test_message ("%s: %s", test->sysroot, test->description);

      g_mkdir (this_test_dir, 0755);

      sysroot = g_build_filename (f->sysroots, test->sysroot, NULL);

      if (test->vk_layer_path == NULL)
        vulkan_layer_envp = g_environ_unsetenv (vulkan_layer_envp, "VK_LAYER_PATH");
      else
        vulkan_layer_envp = g_environ_setenv (vulkan_layer_envp,
                                              "VK_LAYER_PATH", test->vk_layer_path,
                                              TRUE);

      if (test->home == NULL)
        vulkan_layer_envp = g_environ_unsetenv (vulkan_layer_envp, "HOME");
      else
        vulkan_layer_envp = g_environ_setenv (vulkan_layer_envp,
                                              "HOME", test->home, TRUE);

      if (test->xdg_config_dirs == NULL)
        vulkan_layer_envp = g_environ_unsetenv (vulkan_layer_envp, "XDG_CONFIG_DIRS");
      else
        vulkan_layer_envp = g_environ_setenv (vulkan_layer_envp,
                                              "XDG_CONFIG_DIRS", test->xdg_config_dirs, TRUE);

      if (test->xdg_data_dirs == NULL)
        vulkan_layer_envp = g_environ_unsetenv (vulkan_layer_envp, "XDG_DATA_DIRS");
      else
        vulkan_layer_envp = g_environ_setenv (vulkan_layer_envp,
                                              "XDG_DATA_DIRS", test->xdg_data_dirs, TRUE);

      info = srt_system_info_new (NULL);
      g_assert_nonnull (info);
      srt_system_info_set_environ (info, vulkan_layer_envp);
      srt_system_info_set_sysroot (info, sysroot);

      explicit_layers = srt_system_info_list_explicit_vulkan_layers (info);

      for (iter = explicit_layers, j = 0; iter != NULL; iter = iter->next, j++)
        {
          layer_test = test->explicit_layers[j];
          _test_layer_values (iter->data, &layer_test, this_test_dir, sysroot);
        }
      g_assert_cmpstr (test->explicit_layers[j].name, ==, NULL);

      implicit_layers = srt_system_info_list_implicit_vulkan_layers (info);

      for (iter = implicit_layers, j = 0; iter != NULL; iter = iter->next, j++)
        {
          layer_test = test->implicit_layers[j];
          _test_layer_values (iter->data, &layer_test, this_test_dir, sysroot);
        }
      g_assert_cmpstr (test->implicit_layers[j].name, ==, NULL);

      /* No need to keep this around */
      if (!_srt_rm_rf (this_test_dir))
        g_debug ("Unable to remove the temp layers directory: %s", this_test_dir);
    }

  if (!_srt_rm_rf (tmp_dir))
    g_debug ("Unable to remove the temp layers directory: %s", tmp_dir);
}

static void
check_list_suffixes (const GList *list,
                     const gchar * const *suffixes,
                     SrtGraphicsModule module)
{
  const gchar *value = NULL;
  gsize i = 0;
  for (const GList *iter = list; iter != NULL; iter = iter->next, i++)
    {
      switch (module)
        {
          case SRT_GRAPHICS_DRI_MODULE:
            value = srt_dri_driver_get_library_path (iter->data);
            break;
          case SRT_GRAPHICS_VAAPI_MODULE:
            value = srt_va_api_driver_get_library_path (iter->data);
            break;
          case SRT_GRAPHICS_VDPAU_MODULE:
            value = srt_vdpau_driver_get_library_path (iter->data);
            break;
          case SRT_GRAPHICS_GLX_MODULE:
            value = srt_glx_icd_get_library_soname (iter->data);
            break;

          case NUM_SRT_GRAPHICS_MODULES:
          default:
            g_return_if_reached ();
        }
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

          case SRT_GRAPHICS_GLX_MODULE:
          case NUM_SRT_GRAPHICS_MODULES:
          default:
            g_return_if_reached ();
        }
    }
}

static void
check_list_links (const GList *list,
                  const gchar * const *suffixes,
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

          case SRT_GRAPHICS_GLX_MODULE:
            value = srt_glx_icd_get_library_path (iter->data);
            break;

          case SRT_GRAPHICS_VAAPI_MODULE:
          case SRT_GRAPHICS_DRI_MODULE:
          case NUM_SRT_GRAPHICS_MODULES:
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
check_paths_are_absolute (const GList *list,
                          SrtGraphicsModule module)
{
  const gchar *library_path = NULL;
  gchar *absolute_path = NULL;
  for (const GList *iter = list; iter != NULL; iter = iter->next)
    {
      switch (module)
        {
          case SRT_GRAPHICS_DRI_MODULE:
            library_path = srt_dri_driver_get_library_path (iter->data);
            absolute_path = srt_dri_driver_resolve_library_path (iter->data);
            break;

          case SRT_GRAPHICS_VDPAU_MODULE:
            library_path = srt_vdpau_driver_get_library_path (iter->data);
            absolute_path = srt_vdpau_driver_resolve_library_path (iter->data);
            break;

          case SRT_GRAPHICS_VAAPI_MODULE:
            library_path = srt_va_api_driver_get_library_path (iter->data);
            absolute_path = srt_va_api_driver_resolve_library_path (iter->data);
            break;

          case SRT_GRAPHICS_GLX_MODULE:
          case NUM_SRT_GRAPHICS_MODULES:
          default:
            g_return_if_reached ();
        }
      g_assert_cmpstr (library_path, ==, absolute_path);
      g_assert_nonnull (library_path);
      g_assert_cmpint (library_path[0], ==, '/');
      g_free (absolute_path);
    }
}

static void
check_paths_are_relative (const GList *list,
                          SrtGraphicsModule module)
{
  const gchar *library_path = NULL;
  gchar *absolute_path = NULL;
  gsize i = 0;
  for (const GList *iter = list; iter != NULL; iter = iter->next, i++)
    {
      switch (module)
        {
          case SRT_GRAPHICS_DRI_MODULE:
            library_path = srt_dri_driver_get_library_path (iter->data);
            absolute_path = srt_dri_driver_resolve_library_path (iter->data);
            break;

          case SRT_GRAPHICS_VDPAU_MODULE:
            library_path = srt_vdpau_driver_get_library_path (iter->data);
            absolute_path = srt_vdpau_driver_resolve_library_path (iter->data);
            break;

          case SRT_GRAPHICS_VAAPI_MODULE:
            library_path = srt_va_api_driver_get_library_path (iter->data);
            absolute_path = srt_va_api_driver_resolve_library_path (iter->data);
            break;

          case SRT_GRAPHICS_GLX_MODULE:
          case NUM_SRT_GRAPHICS_MODULES:
          default:
            g_return_if_reached ();
        }
      g_assert_cmpstr (library_path, !=, absolute_path);
      g_assert_nonnull (library_path);
      g_assert_nonnull (absolute_path);
      g_assert_cmpint (library_path[0], !=, '/');
      g_assert_cmpint (absolute_path[0], ==, '/');
      assert_same_file (library_path, absolute_path);
      g_free (absolute_path);
    }
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
  const gchar *multiarch_tuples[] = {"i386-mock-debian", "x86_64-mock-debian", NULL};
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

  sysroot = g_build_filename (f->sysroots, "debian10", NULL);
  envp = g_get_environ ();
  envp = g_environ_setenv (envp, "SRT_TEST_SYSROOT", sysroot, TRUE);
  envp = g_environ_unsetenv (envp, "LIBGL_DRIVERS_PATH");
  envp = g_environ_unsetenv (envp, "LIBVA_DRIVERS_PATH");

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, envp);
  srt_system_info_set_sysroot (info, sysroot);
  srt_system_info_set_helpers_path (info, f->builddir);

  /* The output is guaranteed to be in aphabetical order */
  dri = srt_system_info_list_dri_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (dri, dri_suffixes_i386, SRT_GRAPHICS_DRI_MODULE);
  check_paths_are_absolute (dri, SRT_GRAPHICS_DRI_MODULE);
  g_list_free_full (dri, g_object_unref);

  dri = srt_system_info_list_dri_drivers (info, multiarch_tuples[1], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (dri, dri_suffixes_x86_64, SRT_GRAPHICS_DRI_MODULE);
  check_paths_are_absolute (dri, SRT_GRAPHICS_DRI_MODULE);
  g_list_free_full (dri, g_object_unref);

  /* The output is guaranteed to be in aphabetical order */
  va_api = srt_system_info_list_va_api_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (va_api, va_api_suffixes_i386, SRT_GRAPHICS_VAAPI_MODULE);
  check_paths_are_absolute (va_api, SRT_GRAPHICS_VAAPI_MODULE);
  g_list_free_full (va_api, g_object_unref);

  va_api = srt_system_info_list_va_api_drivers (info, multiarch_tuples[1], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (va_api, va_api_suffixes_x86_64, SRT_GRAPHICS_VAAPI_MODULE);
  check_paths_are_absolute (va_api, SRT_GRAPHICS_VAAPI_MODULE);
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
  const gchar *multiarch_tuples[] = {"i386-mock-fedora", "x86_64-mock-fedora", NULL};
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

  sysroot = g_build_filename (f->sysroots, "fedora", NULL);
  envp = g_get_environ ();
  envp = g_environ_setenv (envp, "SRT_TEST_SYSROOT", sysroot, TRUE);
  envp = g_environ_unsetenv (envp, "LIBGL_DRIVERS_PATH");
  envp = g_environ_unsetenv (envp, "LIBVA_DRIVERS_PATH");

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, envp);
  srt_system_info_set_sysroot (info, sysroot);
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
test_dri_ubuntu16 (Fixture *f,
                   gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **envp;
  gchar *sysroot;
  GList *dri;
  GList *va_api;
  const gchar *multiarch_tuples[] = {"mock-ubuntu-64-bit", NULL};
  const gchar *dri_suffixes[] = {NULL};
  const gchar *dri_suffixes_extra[] = {"/lib/dri/radeonsi_dri.so",
                                       "/lib/mock-ubuntu-64-bit/dri/i965_dri.so",
                                       "/lib/mock-ubuntu-64-bit/dri/radeon_dri.so",
                                       NULL};
  const gchar *va_api_suffixes[] = {"/lib/mock-ubuntu-64-bit/dri/radeonsi_drv_video.so",
                                    NULL};

  sysroot = g_build_filename (f->sysroots, "ubuntu16", NULL);
  envp = g_get_environ ();
  envp = g_environ_setenv (envp, "SRT_TEST_SYSROOT", sysroot, TRUE);
  envp = g_environ_setenv (envp, "SRT_TEST_FORCE_ELF", "64", TRUE);
  envp = g_environ_unsetenv (envp, "LIBGL_DRIVERS_PATH");
  envp = g_environ_unsetenv (envp, "LIBVA_DRIVERS_PATH");

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, envp);
  srt_system_info_set_sysroot (info, sysroot);
  srt_system_info_set_helpers_path (info, f->builddir);

  dri = srt_system_info_list_dri_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (dri, dri_suffixes, SRT_GRAPHICS_DRI_MODULE);
  g_list_free_full (dri, g_object_unref);

  dri = srt_system_info_list_dri_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_INCLUDE_ALL);
  check_list_suffixes (dri, dri_suffixes_extra, SRT_GRAPHICS_DRI_MODULE);
  g_list_free_full (dri, g_object_unref);

  va_api = srt_system_info_list_va_api_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (va_api, va_api_suffixes, SRT_GRAPHICS_VAAPI_MODULE);
  g_list_free_full (va_api, g_object_unref);

  va_api = srt_system_info_list_va_api_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_INCLUDE_ALL);
  check_list_suffixes (va_api, va_api_suffixes, SRT_GRAPHICS_VAAPI_MODULE);
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
  const gchar *multiarch_tuples[] = {"i386-mock-fedora", NULL};
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

  sysroot = g_build_filename (f->sysroots, "no-os-release", NULL);

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
  libva_combined = g_strjoin (":", libva, libva2, libva3, NULL);

  envp = g_get_environ ();
  envp = g_environ_setenv (envp, "SRT_TEST_SYSROOT", sysroot, TRUE);
  envp = g_environ_setenv (envp, "LIBGL_DRIVERS_PATH", libgl_combined, TRUE);
  envp = g_environ_setenv (envp, "LIBVA_DRIVERS_PATH", libva_combined, TRUE);

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, envp);
  srt_system_info_set_sysroot (info, sysroot);
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

  /* Test relative path.
   * Move to the sysroots path because otherwise we can't use the
   * relative paths */
  if (g_chdir (global_sysroots) != 0)
    g_error ("chdir %s: %s", global_sysroots, g_strerror (errno));
  g_free (libgl);
  g_free (libgl2);
  g_free (libgl3);
  g_free (libva);
  g_free (libva2);
  g_free (libva3);
  g_free (libgl_combined);
  g_free (libva_combined);
  libgl = g_build_filename ("no-os-release", "custom_path32", "dri", NULL);
  libgl2 = g_build_filename ("no-os-release", "custom_path32_2", "dri", NULL);
  libgl3 = g_build_filename ("no-os-release", "custom_path64", "dri", NULL);
  libva = g_build_filename ("no-os-release", "custom_path32", "va", NULL);
  libva2 = g_build_filename ("no-os-release", "custom_path32_2", "va", NULL);
  libva3 = g_build_filename ("no-os-release", "custom_path64", "va", NULL);
  libgl_combined = g_strjoin (":", libgl, libgl2, libgl3, NULL);
  libva_combined = g_strjoin (":", libva, libva2, libva3, NULL);
  envp = g_environ_setenv (envp, "LIBGL_DRIVERS_PATH", libgl_combined, TRUE);
  envp = g_environ_setenv (envp, "LIBVA_DRIVERS_PATH", libva_combined, TRUE);
  srt_system_info_set_environ (info, envp);

  dri = srt_system_info_list_dri_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (dri, dri_suffixes, SRT_GRAPHICS_DRI_MODULE);
  check_paths_are_relative (dri, SRT_GRAPHICS_DRI_MODULE);
  g_list_free_full (dri, g_object_unref);

  va_api = srt_system_info_list_va_api_drivers (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  check_list_suffixes (va_api, va_api_suffixes, SRT_GRAPHICS_VAAPI_MODULE);
  check_paths_are_relative (va_api, SRT_GRAPHICS_VAAPI_MODULE);
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

  sysroot = g_build_filename (f->sysroots, "flatpak-example", NULL);
  envp = g_get_environ ();
  envp = g_environ_setenv (envp, "SRT_TEST_SYSROOT", sysroot, TRUE);
  envp = g_environ_unsetenv (envp, "LIBGL_DRIVERS_PATH");
  envp = g_environ_unsetenv (envp, "LIBVA_DRIVERS_PATH");

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, envp);
  srt_system_info_set_sysroot (info, sysroot);
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

typedef struct
{
  const gchar *description;
  const gchar *multiarch_tuple;
  const gchar *sysroot;
  const gchar *vdpau_suffixes[5];
  const gchar *vdpau_links[5];
  const gchar *vdpau_suffixes_extra[6];
  const gchar *vdpau_path_env;
  const gchar *vdpau_driver_env;
  const gchar *ld_library_path_env;
} VdpauTest;

static const VdpauTest vdpau_test[] =
{
  {
    .description = "debian 10 i386",
    .multiarch_tuple = "i386-mock-debian",
    .sysroot = "debian10",
    .vdpau_suffixes =
    {
      "/lib/i386-linux-gnu/vdpau/libvdpau_r600.so",
      "/lib/i386-linux-gnu/vdpau/libvdpau_radeonsi.so",
      "/lib/i386-linux-gnu/vdpau/libvdpau_radeonsi.so.1",
      NULL
    },
    /* These symlinks are provided by "libvdpau_radeonsi.so" and "libvdpau_radeonsi.so.1" */
    .vdpau_links =
    {
      "libvdpau_radeonsi.so.1.0.0",
      "libvdpau_radeonsi.so.1.0.0",
      NULL
    },
  },

  {
    .description = "debian 10 x86_64",
    .multiarch_tuple = "x86_64-mock-debian",
    .sysroot = "debian10",
    .vdpau_suffixes =
    {
      "/lib/x86_64-linux-gnu/vdpau/libvdpau_r600.so.1",
      "/lib/x86_64-linux-gnu/vdpau/libvdpau_radeonsi.so",
      "/lib/x86_64-linux-gnu/vdpau/libvdpau_radeonsi.so.1",
      NULL
    },
    /* These symlinks are provided by "libvdpau_r600.so", "libvdpau_radeonsi.so"
     * and "libvdpau_radeonsi.so.1" */
    .vdpau_links =
    {
      "libvdpau_r600.so.1.0.0",
      "libvdpau_radeonsi.so.1.0.0",
      "libvdpau_radeonsi.so.1.0.0",
      NULL
    },
  },

  {
    .description = "fedora 32 bit",
    .multiarch_tuple = "i386-mock-fedora",
    .sysroot = "fedora",
    .vdpau_suffixes =
    {
      "/usr/lib/vdpau/libvdpau_nouveau.so.1",
      "/usr/lib/vdpau/libvdpau_r600.so",
      "/usr/lib/vdpau/libvdpau_radeonsi.so",
      "/usr/lib/vdpau/libvdpau_radeonsi.so.1",
      NULL
    },
    /* These symlinks are provided by "libvdpau_radeonsi.so" and "libvdpau_radeonsi.so.1" */
    .vdpau_links =
    {
      "libvdpau_radeonsi.so.1.0.0",
      "libvdpau_radeonsi.so.1.0.0",
      NULL
    },
  },

  {
    .description = "fedora 64 bit",
    .multiarch_tuple = "x86_64-mock-fedora",
    .sysroot = "fedora",
    .vdpau_suffixes =
    {
      "/usr/lib64/vdpau/libvdpau_r300.so",
      "/usr/lib64/vdpau/libvdpau_r300.so.1",
      "/usr/lib64/vdpau/libvdpau_radeonsi.so",
      "/usr/lib64/vdpau/libvdpau_radeonsi.so.1",
      NULL
    },
    /* These symlinks are provided by "libvdpau_r300.so.1" and "libvdpau_radeonsi.so.1" */
    .vdpau_links =
    {
      "libvdpau_r300.so",
      "libvdpau_radeonsi.so",
      NULL
    },
  },

  {
    .description = "vdpau with environment",
    .multiarch_tuple = "i386-mock-fedora",
    .sysroot = "no-os-release",
    .vdpau_suffixes =
    {
      "/custom_path32/vdpau/libvdpau_r600.so.1",
      "/custom_path32/vdpau/libvdpau_radeonsi.so.1",
      NULL
    },
    .vdpau_suffixes_extra =
    {
      "/custom_path32/vdpau/libvdpau_r600.so.1",
      "/custom_path32/vdpau/libvdpau_radeonsi.so.1",
      "/usr/lib/vdpau/libvdpau_nouveau.so.1",
      "/another_custom_path/libvdpau_custom.so",
      "/usr/lib/libvdpau_r9000.so",
      NULL
    },
    .vdpau_path_env = "custom_path32",
    .vdpau_driver_env = "r9000",
    .ld_library_path_env = "another_custom_path",
  },

  {
    .description = "flatpak",
    .multiarch_tuple = "mock-abi",
    .sysroot = "flatpak-example",
    .vdpau_suffixes =
    {
      "/usr/lib/mock-abi/vdpau/libvdpau_radeonsi.so.1",
      NULL
    },
  },
};

static void
test_vdpau (Fixture *f,
            gconstpointer context)
{
  for (gsize i = 0; i < G_N_ELEMENTS (vdpau_test); i++)
    {
      const VdpauTest *test = &vdpau_test[i];
      SrtSystemInfo *info;
      gchar **envp;
      gchar *sysroot;
      gchar *ld_library_path = NULL;
      gchar *vdpau_path = NULL;
      gchar *vdpau_relative_path = NULL;
      GList *vdpau;

      g_test_message ("%s: %s", test->sysroot, test->description);

      sysroot = g_build_filename (f->sysroots, test->sysroot, NULL);
      envp = g_get_environ ();
      envp = g_environ_setenv (envp, "SRT_TEST_SYSROOT", sysroot, TRUE);
      if (test->vdpau_path_env == NULL)
        {
          envp = g_environ_unsetenv (envp, "VDPAU_DRIVER_PATH");
        }
      else
        {
          vdpau_path = g_build_filename (sysroot, test->vdpau_path_env, "vdpau", NULL);
          vdpau_relative_path = g_build_filename (test->sysroot, test->vdpau_path_env,
                                                  "vdpau", NULL);
          envp = g_environ_setenv (envp, "VDPAU_DRIVER_PATH", vdpau_path, TRUE);
        }

      if (test->vdpau_driver_env == NULL)
        envp = g_environ_unsetenv (envp, "VDPAU_DRIVER");
      else
        envp = g_environ_setenv (envp, "VDPAU_DRIVER", test->vdpau_driver_env, TRUE);

      if (test->ld_library_path_env == NULL)
        {
          envp = g_environ_unsetenv (envp, "LD_LIBRARY_PATH");
        }
      else
        {
          ld_library_path = g_build_filename (sysroot, test->ld_library_path_env, NULL);
          envp = g_environ_setenv (envp, "LD_LIBRARY_PATH", ld_library_path, TRUE);
        }

      info = srt_system_info_new (NULL);
      srt_system_info_set_environ (info, envp);
      srt_system_info_set_sysroot (info, sysroot);
      srt_system_info_set_helpers_path (info, f->builddir);

      /* The output is guaranteed to be in aphabetical order */
      vdpau = srt_system_info_list_vdpau_drivers (info, test->multiarch_tuple, SRT_DRIVER_FLAGS_NONE);
      check_list_suffixes (vdpau, test->vdpau_suffixes, SRT_GRAPHICS_VDPAU_MODULE);
      check_list_links (vdpau, test->vdpau_links, SRT_GRAPHICS_VDPAU_MODULE);
      check_paths_are_absolute (vdpau, SRT_GRAPHICS_VDPAU_MODULE);
      g_list_free_full (vdpau, g_object_unref);

      if (test->vdpau_suffixes_extra[0] != NULL)
        {
          /* Do it again, this time including the extras */
          vdpau = srt_system_info_list_vdpau_drivers (info, test->multiarch_tuple, SRT_DRIVER_FLAGS_INCLUDE_ALL);
          check_list_suffixes (vdpau, test->vdpau_suffixes_extra, SRT_GRAPHICS_VDPAU_MODULE);
          check_paths_are_absolute (vdpau, SRT_GRAPHICS_VDPAU_MODULE);
          gulong non_extras = 0;
          for (; test->vdpau_suffixes[non_extras] != NULL; non_extras++)
            continue;
          check_list_extra (vdpau, non_extras, SRT_GRAPHICS_VDPAU_MODULE);
          g_list_free_full (vdpau, g_object_unref);
        }

      if (vdpau_relative_path != NULL)
        {
          envp = g_environ_setenv (envp, "VDPAU_DRIVER_PATH", vdpau_relative_path, TRUE);
          /* Move to the build directory because otherwise we can't use the relative sysroots path */
          if (g_chdir (global_sysroots) != 0)
            g_error ("chdir %s: %s", global_sysroots, g_strerror (errno));

          srt_system_info_set_environ (info, envp);
          vdpau = srt_system_info_list_vdpau_drivers (info, test->multiarch_tuple, SRT_DRIVER_FLAGS_NONE);
          check_list_suffixes (vdpau, test->vdpau_suffixes, SRT_GRAPHICS_VDPAU_MODULE);
          check_list_links (vdpau, test->vdpau_links, SRT_GRAPHICS_VDPAU_MODULE);
          check_paths_are_relative (vdpau, SRT_GRAPHICS_VDPAU_MODULE);
          g_list_free_full (vdpau, g_object_unref);
        }

      g_object_unref (info);
      g_free (sysroot);
      g_free (ld_library_path);
      g_free (vdpau_path);
      g_free (vdpau_relative_path);
      g_strfreev (envp);
    }
}

typedef struct
{
  const gchar *description;
  SrtWindowSystem window_system;
  SrtRenderingInterface rendering_interface;
  SrtGraphicsLibraryVendor library_vendor;
  SrtGraphicsIssues issues;
  SrtTestFlags test_flags;
  const gchar *multiarch_tuple;
  const gchar *renderer_string;
  const gchar *version_string;
  const gchar *messages;
  int exit_status;
  gboolean vendor_neutral;
} GraphicsTest;

static const GraphicsTest graphics_test[] =
{
  {
    .description = "good vdpau",
    .window_system = SRT_WINDOW_SYSTEM_X11,
    .rendering_interface = SRT_RENDERING_INTERFACE_VDPAU,
    .issues = SRT_GRAPHICS_ISSUES_NONE,
    .multiarch_tuple = "mock-good",
    .renderer_string = SRT_TEST_GOOD_VDPAU_RENDERER,
    .vendor_neutral = TRUE,
  },

  {
    .description = "bad vdpau",
    .window_system = SRT_WINDOW_SYSTEM_X11,
    .rendering_interface = SRT_RENDERING_INTERFACE_VDPAU,
    .issues = SRT_GRAPHICS_ISSUES_CANNOT_DRAW,
    .multiarch_tuple = "mock-bad",
    .messages = SRT_TEST_BAD_VDPAU_MESSAGES,
    .exit_status = 1,
    .vendor_neutral = TRUE,
  },

  {
    .description = "good gl",
    .window_system = SRT_WINDOW_SYSTEM_GLX,
    .rendering_interface = SRT_RENDERING_INTERFACE_GL,
    .issues = SRT_GRAPHICS_ISSUES_NONE,
    .multiarch_tuple = "mock-good",
    .renderer_string = SRT_TEST_GOOD_GRAPHICS_RENDERER,
    .version_string = SRT_TEST_GOOD_GRAPHICS_VERSION,
  },

  {
    .description = "no graphics stack",
    .window_system = SRT_WINDOW_SYSTEM_GLX,
    .rendering_interface = SRT_RENDERING_INTERFACE_GL,
    .issues = SRT_GRAPHICS_ISSUES_CANNOT_LOAD,
    .multiarch_tuple = "mock-bad",
    .messages = "warning: this warning should always be logged\n"
                "Waffle error: 0x2 WAFFLE_ERROR_UNKNOWN: XOpenDisplay failed\n"
                "info: you used LIBGL_DEBUG=verbose\n",
    /* We used "mock-bad" for the architecture so, when checking the library vendor,
     * we will not be able to call the helper `mock-bad-check-library`.
     * For this reason we expect %SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN */
    .library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN,
    .exit_status = 1,
  },

  {
    .description = "graphics timeout",
    .window_system = SRT_WINDOW_SYSTEM_GLX,
    .rendering_interface = SRT_RENDERING_INTERFACE_GL,
    .issues = (SRT_GRAPHICS_ISSUES_CANNOT_LOAD | SRT_GRAPHICS_ISSUES_TIMEOUT),
    .test_flags = SRT_TEST_FLAGS_TIME_OUT_SOONER,
    .multiarch_tuple = "mock-hanging",
    // Timeout has exit code 124
    .exit_status = 124,
  },

  {
    .description = "software rendering",
    .window_system = SRT_WINDOW_SYSTEM_GLX,
    .rendering_interface = SRT_RENDERING_INTERFACE_GL,
    .issues = SRT_GRAPHICS_ISSUES_SOFTWARE_RENDERING,
    .multiarch_tuple = "mock-software",
    .renderer_string = SRT_TEST_SOFTWARE_GRAPHICS_RENDERER,
    .version_string = SRT_TEST_SOFTWARE_GRAPHICS_VERSION,
    .messages = "warning: this warning should always be logged\n"
                "info: you used LIBGL_DEBUG=verbose\n",
  },

  {
    .description = "gl driver ok but check-gl fails",
    .window_system = SRT_WINDOW_SYSTEM_GLX,
    .rendering_interface = SRT_RENDERING_INTERFACE_GL,
    .issues = SRT_GRAPHICS_ISSUES_CANNOT_DRAW,
    .multiarch_tuple = "mock-mixed",
    .renderer_string = SRT_TEST_GOOD_GRAPHICS_RENDERER,
    .version_string = SRT_TEST_GOOD_GRAPHICS_VERSION,
    .messages = "warning: this warning should always be logged\n"
                "Waffle error: 0x2 WAFFLE_ERROR_UNKNOWN: XOpenDisplay failed\n"
                "info: you used LIBGL_DEBUG=verbose\n",
    .exit_status = 1,
  },

  {
    .description = "good vulkan",
    .window_system = SRT_WINDOW_SYSTEM_X11,
    .rendering_interface = SRT_RENDERING_INTERFACE_VULKAN,
    .issues = SRT_GRAPHICS_ISSUES_NONE,
    .multiarch_tuple = "mock-good",
    .renderer_string = SRT_TEST_GOOD_GRAPHICS_RENDERER,
    .version_string = SRT_TEST_GOOD_VULKAN_VERSION,
    .vendor_neutral = TRUE,
  },

  {
    .description = "bad vulkan",
    .window_system = SRT_WINDOW_SYSTEM_X11,
    .rendering_interface = SRT_RENDERING_INTERFACE_VULKAN,
    .issues = SRT_GRAPHICS_ISSUES_CANNOT_LOAD,
    .multiarch_tuple = "mock-bad",
    .messages = "/build/vulkan-tools/src/Vulkan-Tools-1.1.114/vulkaninfo/vulkaninfo.c:5884: failed with VK_ERROR_INITIALIZATION_FAILED\n",
    .exit_status = 1,
    .vendor_neutral = TRUE,
  },

  {
    .description = "good vulkan driver but check-vulkan failure",
    .window_system = SRT_WINDOW_SYSTEM_X11,
    .rendering_interface = SRT_RENDERING_INTERFACE_VULKAN,
    .issues = SRT_GRAPHICS_ISSUES_CANNOT_DRAW,
    .multiarch_tuple = "mock-mixed",
    .renderer_string = SRT_TEST_GOOD_GRAPHICS_RENDERER,
    .version_string = SRT_TEST_GOOD_VULKAN_VERSION,
    .messages = "failed to create window surface!\n",
    .exit_status = 1,
    .vendor_neutral = TRUE,
  },

  {
    .description = "good va-api",
    .window_system = SRT_WINDOW_SYSTEM_X11,
    .rendering_interface = SRT_RENDERING_INTERFACE_VAAPI,
    .issues = SRT_GRAPHICS_ISSUES_NONE,
    .multiarch_tuple = "mock-good",
    .renderer_string = SRT_TEST_GOOD_VAAPI_RENDERER,
    .vendor_neutral = TRUE,
  },

  {
    .description = "bad va-api",
    .window_system = SRT_WINDOW_SYSTEM_X11,
    .rendering_interface = SRT_RENDERING_INTERFACE_VAAPI,
    .issues = SRT_GRAPHICS_ISSUES_CANNOT_DRAW,
    .multiarch_tuple = "mock-bad",
    .messages = SRT_TEST_BAD_VAAPI_MESSAGES,
    .exit_status = 1,
    .vendor_neutral = TRUE,
  },
};

static void
test_check_graphics (Fixture *f,
                     gconstpointer context)
{
  for (gsize i = 0; i < G_N_ELEMENTS (graphics_test); i++)
    {
      const GraphicsTest *test = &graphics_test[i];
      SrtGraphics *graphics = NULL;
      SrtGraphicsIssues issues;
      gchar *tuple;
      gchar *renderer;
      gchar *version;
      gchar *messages;
      int exit_status;
      int terminating_signal;
      gboolean vendor_neutral;
      SrtGraphicsLibraryVendor library_vendor;

      g_test_message ("%s", test->description);

      SrtSystemInfo *info = srt_system_info_new (NULL);
      srt_system_info_set_helpers_path (info, f->builddir);
      srt_system_info_set_test_flags (info, test->test_flags);

      issues = srt_system_info_check_graphics (info,
                                               test->multiarch_tuple,
                                               test->window_system,
                                               test->rendering_interface,
                                               &graphics);
      g_assert_cmpint (issues, ==, test->issues);
      g_assert_cmpstr (srt_graphics_get_renderer_string (graphics), ==, test->renderer_string);
      g_assert_cmpstr (srt_graphics_get_version_string (graphics), ==, test->version_string);
      g_assert_cmpstr (srt_graphics_get_messages (graphics), ==, test->messages);
      g_assert_cmpint (srt_graphics_get_exit_status (graphics), ==, test->exit_status);
      g_assert_cmpint (srt_graphics_get_terminating_signal (graphics), ==, 0);

      vendor_neutral = srt_graphics_library_is_vendor_neutral (graphics, &library_vendor);
      g_assert_cmpint (library_vendor, ==, test->library_vendor);
      g_assert_cmpint (vendor_neutral, ==, test->vendor_neutral);

      g_object_get (graphics,
                    "multiarch-tuple", &tuple,
                    "issues", &issues,
                    "renderer-string", &renderer,
                    "version-string", &version,
                    "messages", &messages,
                    "exit-status", &exit_status,
                    "terminating-signal", &terminating_signal,
                    NULL);
      g_assert_cmpint (issues, ==, test->issues);
      g_assert_cmpstr (tuple, ==, test->multiarch_tuple);
      g_assert_cmpstr (renderer, ==, test->renderer_string);
      g_assert_cmpstr (version, ==, test->version_string);
      g_assert_cmpstr (messages, ==, test->messages);
      g_assert_cmpint (exit_status, ==, test->exit_status);
      g_assert_cmpint (terminating_signal, ==, 0);
      g_free (tuple);
      g_free (renderer);
      g_free (version);
      g_free (messages);

      g_object_unref (graphics);
      g_object_unref (info);
    }
}

static gint
glx_icd_compare (SrtGlxIcd *a, SrtGlxIcd *b)
{
  return g_strcmp0 (srt_glx_icd_get_library_soname (a), srt_glx_icd_get_library_soname (b));
}

static void
test_glx_debian (Fixture *f,
                 gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **envp;
  gchar *sysroot;
  GList *glx;
  const gchar *multiarch_tuples[] = {"i386-mock-debian", "x86_64-mock-debian", NULL};
  const gchar *glx_suffixes_i386[] = {"libGLX_mesa.so.0",
                                      "libGLX_nvidia.so.0",
                                      NULL};
  const gchar *glx_paths_i386[] = {"/lib/i386-linux-gnu/libGLX_mesa.so.0",
                                   "/lib/i386-linux-gnu/libGLX_nvidia.so.0",
                                   NULL};
  const gchar *glx_suffixes_x86_64[] = {"libGLX_mesa.so.0",
                                        NULL};
  const gchar *glx_paths_x86_64[] = {"/lib/x86_64-linux-gnu/libGLX_mesa.so.0",
                                     NULL};

  sysroot = g_build_filename (f->sysroots, "debian10", NULL);
  envp = g_get_environ ();
  envp = g_environ_setenv (envp, "SRT_TEST_SYSROOT", sysroot, TRUE);

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, envp);
  srt_system_info_set_sysroot (info, sysroot);
  srt_system_info_set_helpers_path (info, f->builddir);

  glx = srt_system_info_list_glx_icds (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  /* The icds are not provided in a guaranteed order. Sort them before checking
   * with the expectations */
  glx = g_list_sort (glx, (GCompareFunc) glx_icd_compare);
  check_list_suffixes (glx, glx_suffixes_i386, SRT_GRAPHICS_GLX_MODULE);
  check_list_links (glx, glx_paths_i386, SRT_GRAPHICS_GLX_MODULE);
  g_list_free_full (glx, g_object_unref);

  glx = srt_system_info_list_glx_icds (info, multiarch_tuples[1], SRT_DRIVER_FLAGS_NONE);
  /* The icds are not provided in a guaranteed order. Sort them before checking
   * with the expectations */
  glx = g_list_sort (glx, (GCompareFunc) glx_icd_compare);
  check_list_suffixes (glx, glx_suffixes_x86_64, SRT_GRAPHICS_GLX_MODULE);
  check_list_links (glx, glx_paths_x86_64, SRT_GRAPHICS_GLX_MODULE);
  g_list_free_full (glx, g_object_unref);

  g_object_unref (info);
  g_free (sysroot);
  g_strfreev (envp);
}

static void
test_glx_container (Fixture *f,
                    gconstpointer context)
{
  SrtSystemInfo *info;
  gchar **envp;
  gchar *sysroot;
  GList *glx;
  const gchar *multiarch_tuples[] = {"i386-mock-container", "x86_64-mock-container", NULL};
  const gchar *glx_suffixes_i386[] = {"libGLX_nvidia.so.0",
                                      NULL};
  const gchar *glx_paths_i386[] = {"/lib/i386-linux-gnu/libGLX_nvidia.so.0",
                                   NULL};
  const gchar *glx_suffixes_x86_64[] = {"libGLX_custom.so.0",
                                        "libGLX_mesa.so.0",
                                        NULL};
  const gchar *glx_paths_x86_64[] = {"/lib/x86_64-linux-gnu/libGLX_custom.so.0",
                                     "/lib/x86_64-linux-gnu/libGLX_mesa.so.0",
                                     NULL};

  sysroot = g_build_filename (f->sysroots, "steamrt", NULL);
  envp = g_get_environ ();
  envp = g_environ_setenv (envp, "SRT_TEST_SYSROOT", sysroot, TRUE);

  info = srt_system_info_new (NULL);
  srt_system_info_set_environ (info, envp);
  srt_system_info_set_sysroot (info, sysroot);
  srt_system_info_set_helpers_path (info, f->builddir);

  glx = srt_system_info_list_glx_icds (info, multiarch_tuples[0], SRT_DRIVER_FLAGS_NONE);
  /* The icds are not provided in a guaranteed order. Sort them before checking
   * with the expectations */
  glx = g_list_sort (glx, (GCompareFunc) glx_icd_compare);
  check_list_suffixes (glx, glx_suffixes_i386, SRT_GRAPHICS_GLX_MODULE);
  check_list_links (glx, glx_paths_i386, SRT_GRAPHICS_GLX_MODULE);
  g_list_free_full (glx, g_object_unref);

  glx = srt_system_info_list_glx_icds (info, multiarch_tuples[1], SRT_DRIVER_FLAGS_NONE);
  /* The icds are not provided in a guaranteed order. Sort them before checking
   * with the expectations */
  glx = g_list_sort (glx, (GCompareFunc) glx_icd_compare);
  check_list_suffixes (glx, glx_suffixes_x86_64, SRT_GRAPHICS_GLX_MODULE);
  check_list_links (glx, glx_paths_x86_64, SRT_GRAPHICS_GLX_MODULE);
  g_list_free_full (glx, g_object_unref);

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
  int ret;

  argv0 = argv[0];
  global_sysroots = _srt_global_setup_sysroots (argv0);

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/graphics/object", Fixture, NULL,
              setup, test_object, teardown);
  g_test_add ("/graphics/normalize_window_system", Fixture, NULL,
              setup, test_normalize_window_system, teardown);
  g_test_add ("/graphics/sigusr", Fixture, NULL,
              setup, test_sigusr, teardown);

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

  g_test_add ("/graphics/layers/vulkan/xdg", Fixture, NULL,
              setup, test_layer_vulkan, teardown);

  g_test_add ("/graphics/dri/debian10", Fixture, NULL,
              setup, test_dri_debian10, teardown);
  g_test_add ("/graphics/dri/fedora", Fixture, NULL,
              setup, test_dri_fedora, teardown);
  g_test_add ("/graphics/dri/ubuntu16", Fixture, NULL,
              setup, test_dri_ubuntu16, teardown);
  g_test_add ("/graphics/dri/with_env", Fixture, NULL,
              setup, test_dri_with_env, teardown);
  g_test_add ("/graphics/dri/flatpak", Fixture, NULL,
              setup, test_dri_flatpak, teardown);

  g_test_add ("/graphics/vdpau/basic", Fixture, NULL,
              setup, test_vdpau, teardown);

  g_test_add ("/graphics/check", Fixture, NULL,
              setup, test_check_graphics, teardown);

  g_test_add ("/graphics/glx/debian", Fixture, NULL,
              setup, test_glx_debian, teardown);
  g_test_add ("/graphics/glx/container", Fixture, NULL,
              setup, test_glx_container, teardown);

  ret = g_test_run ();
  _srt_global_teardown_sysroots ();
  g_clear_pointer (&global_sysroots, g_free);
  return ret;
}
