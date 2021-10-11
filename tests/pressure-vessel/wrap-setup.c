/*
 * Copyright © 2019-2021 Collabora Ltd.
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

#include <stdlib.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx/libglnx.h"

#include "tests/test-utils.h"

#include "supported-architectures.h"
#include "wrap-setup.h"
#include "utils.h"

/* These match the first entry in PvMultiArchdetails.platforms,
 * which is the easiest realistic thing for a mock implementation of
 * srt_system_info_check_library() to use. */
#define MOCK_PLATFORM_32 "i686"
#define MOCK_PLATFORM_64 "xeon_phi"

/* These match Debian multiarch, which is as good a thing as any for
 * a mock implementation of srt_system_info_check_library() to use. */
#define MOCK_LIB_32 "lib/" SRT_ABI_I386
#define MOCK_LIB_64 "lib/" SRT_ABI_X86_64

typedef struct
{
  TestsOpenFdSet old_fds;
  gchar *tmpdir;
  gchar *mock_host;
  gchar *mock_runtime;
  gchar *var;
  GStrv env;
} Fixture;

typedef struct
{
  int unused;
} Config;

static int
open_or_die (const char *path,
             int flags,
             int mode)
{
  glnx_autofd int fd = open (path, flags | O_CLOEXEC, mode);

  if (fd >= 0)
    return glnx_steal_fd (&fd);
  else
    g_error ("open(%s, 0x%x): %s", path, flags, g_strerror (errno));
}

static FlatpakExports *
fixture_create_exports (Fixture *f)
{
  g_autoptr(FlatpakExports) exports = flatpak_exports_new ();
  glnx_autofd int fd = open_or_die (f->mock_host, O_RDONLY | O_DIRECTORY, 0755);

  flatpak_exports_take_host_fd (exports, glnx_steal_fd (&fd));
  return g_steal_pointer (&exports);
}

static PvRuntime *
fixture_create_runtime (Fixture *f,
                        PvRuntimeFlags flags)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(PvGraphicsProvider) graphics_provider = NULL;
  g_autoptr(PvRuntime) runtime = NULL;
  const char *gfx_in_container;

  if (flags & PV_RUNTIME_FLAGS_FLATPAK_SUBSANDBOX)
    gfx_in_container = "/run/parent";
  else
    gfx_in_container = "/run/host";

  graphics_provider = pv_graphics_provider_new ("/", gfx_in_container,
                                                &local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (graphics_provider);

  runtime = pv_runtime_new (f->mock_runtime,
                            "mock_platform_1.0",
                            f->var,
                            NULL,
                            graphics_provider,
                            environ,
                            (flags
                             | PV_RUNTIME_FLAGS_VERBOSE
                             | PV_RUNTIME_FLAGS_SINGLE_THREAD),
                            &local_error);
  g_assert_no_error (local_error);
  g_assert_nonnull (runtime);
  return g_steal_pointer (&runtime);
}

static void
setup (Fixture *f,
       gconstpointer context)
{
  static const char * const touch[] =
  {
    "app/lib/libpreloadA.so",
    "future/libs-post2038/.exists",
    "home/me/libpreloadH.so",
    "lib/libpreload-rootfs.so",
    "opt/" MOCK_LIB_32 "/libpreloadL.so",
    "opt/" MOCK_LIB_64 "/libpreloadL.so",
    "overlay/libs/usr/lib/libpreloadO.so",
    "platform/plat-" MOCK_PLATFORM_32 "/libpreloadP.so",
    "platform/plat-" MOCK_PLATFORM_64 "/libpreloadP.so",
    "steam/lib/gameoverlayrenderer.so",
    "usr/lib/libpreloadU.so",
    "usr/local/lib/libgtk3-nocsd.so.0",
    "in-root-plat-" MOCK_PLATFORM_32 "-only-32-bit.so",
  };
  g_autoptr(GError) local_error = NULL;
  gsize i;

  f->old_fds = tests_check_fd_leaks_enter ();

  f->tmpdir = g_dir_make_tmp ("pressure-vessel-tests.XXXXXX", &local_error);
  g_assert_no_error (local_error);
  f->mock_host = g_build_filename (f->tmpdir, "host", NULL);
  f->mock_runtime = g_build_filename (f->tmpdir, "runtime", NULL);
  f->var = g_build_filename (f->tmpdir, "var", NULL);
  g_assert_no_errno (g_mkdir (f->mock_host, 0755));
  g_assert_no_errno (g_mkdir (f->mock_runtime, 0755));
  g_assert_no_errno (g_mkdir (f->var, 0755));

  for (i = 0; i < G_N_ELEMENTS (touch); i++)
    {
      g_autofree char *dir = g_path_get_dirname (touch[i]);
      g_autofree char *full_dir = g_build_filename (f->mock_host, dir, NULL);
      g_autofree char *full_path = g_build_filename (f->mock_host, touch[i], NULL);

      g_test_message ("Creating %s", full_dir);
      g_assert_no_errno (g_mkdir_with_parents (full_dir, 0755));
      g_test_message ("Creating %s", full_path);
      g_file_set_contents (full_path, "", 0, &local_error);
      g_assert_no_error (local_error);
    }

  f->env = g_get_environ ();
  f->env = g_environ_setenv (f->env, "STEAM_COMPAT_CLIENT_INSTALL_PATH",
                             "/steam", TRUE);
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  g_autoptr(GError) local_error = NULL;

  if (f->tmpdir != NULL)
    {
      glnx_shutil_rm_rf_at (-1, f->tmpdir, NULL, &local_error);
      g_assert_no_error (local_error);
    }

  tests_check_fd_leaks_leave (f->old_fds);
  g_clear_pointer (&f->mock_host, g_free);
  g_clear_pointer (&f->mock_runtime, g_free);
  g_clear_pointer (&f->tmpdir, g_free);
  g_clear_pointer (&f->var, g_free);
  g_clear_pointer (&f->env, g_strfreev);
}

static void
populate_ld_preload (Fixture *f,
                     GPtrArray *argv,
                     PvAppendPreloadFlags flags,
                     PvRuntime *runtime,
                     FlatpakExports *exports)
{
  static const struct
  {
    const char *string;
    const char *warning;
  } preloads[] =
  {
    { "", .warning = "Ignoring invalid loadable module \"\"" },
    { "", .warning = "Ignoring invalid loadable module \"\"" },
    { "/app/lib/libpreloadA.so" },
    { "/platform/plat-$PLATFORM/libpreloadP.so" },
    { "/opt/${LIB}/libpreloadL.so" },
    { "/lib/libpreload-rootfs.so" },
    { "/usr/lib/libpreloadU.so" },
    { "/home/me/libpreloadH.so" },
    { "/steam/lib/gameoverlayrenderer.so" },
    { "/overlay/libs/${ORIGIN}/../lib/libpreloadO.so" },
    { "/future/libs-$FUTURE/libpreloadF.so" },
    { "/in-root-plat-${PLATFORM}-only-32-bit.so" },
    { "/in-root-${FUTURE}.so" },
    { "./${RELATIVE}.so" },
    { "./relative.so" },
    { "libfakeroot.so" },
    { "libpthread.so.0" },
    {
      "/usr/local/lib/libgtk3-nocsd.so.0",
      .warning = "Disabling gtk3-nocsd LD_PRELOAD: it is known to cause crashes.",
    },
    { "", .warning = "Ignoring invalid loadable module \"\"" },
  };
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (preloads); i++)
    {
      GLogLevelFlags old_fatal_mask = G_LOG_FATAL_MASK;

      /* We expect a warning for libgtk3-nocsd.so.0, but the test framework
       * makes warnings and critical warnings fatal, in addition to the
       * usual fatal errors. Temporarily relax that to just critical
       * warnings and fatal errors. */
      if (preloads[i].warning != NULL)
        {
          old_fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
#if GLIB_CHECK_VERSION(2, 34, 0)
          /* We can't check for the message during unit testing when
           * compiling with GLib 2.32 from scout, but we can check for it
           * in developer builds against a newer GLib. Note that this
           * assumes pressure-vessel doesn't define G_LOG_USE_STRUCTURED,
           * but that's a GLib 2.50 feature which we are unlikely to use
           * while we are still building against scout. */
          g_test_expect_message ("pressure-vessel",
                                 G_LOG_LEVEL_WARNING,
                                 preloads[i].warning);
#endif
        }

      pv_wrap_append_preload (argv,
                              "LD_PRELOAD",
                              "--ld-preload",
                              preloads[i].string,
                              f->env,
                              flags | PV_APPEND_PRELOAD_FLAGS_IN_UNIT_TESTS,
                              runtime,
                              exports);

      /* If we modified the fatal mask, put back the old value. */
      if (preloads[i].warning != NULL)
        {
#if GLIB_CHECK_VERSION(2, 34, 0)
          g_test_assert_expected_messages ();
#endif
          g_log_set_always_fatal (old_fatal_mask);
        }
    }

  for (i = 0; i < argv->len; i++)
    g_test_message ("argv[%" G_GSIZE_FORMAT "]: %s",
                    i, (const char *) g_ptr_array_index (argv, i));

  g_test_message ("argv->len: %" G_GSIZE_FORMAT, i);
}

static const char * const expected_preload_paths[] =
{
  "/app/lib/libpreloadA.so",
  "/platform/plat-" MOCK_PLATFORM_64 "/libpreloadP.so:abi=" SRT_ABI_X86_64,
  "/platform/plat-" MOCK_PLATFORM_32 "/libpreloadP.so:abi=" SRT_ABI_I386,
  "/opt/" MOCK_LIB_64 "/libpreloadL.so:abi=" SRT_ABI_X86_64,
  "/opt/" MOCK_LIB_32 "/libpreloadL.so:abi=" SRT_ABI_I386,
  "/lib/libpreload-rootfs.so",
  "/usr/lib/libpreloadU.so",
  "/home/me/libpreloadH.so",
  "/steam/lib/gameoverlayrenderer.so",
  "/overlay/libs/${ORIGIN}/../lib/libpreloadO.so",
  "/future/libs-$FUTURE/libpreloadF.so",
  "/in-root-plat-i686-only-32-bit.so:abi=i386-linux-gnu",
  "/in-root-${FUTURE}.so",
  "./${RELATIVE}.so",
  "./relative.so",
  /* Our mock implementation of pv_runtime_has_library() behaves as though
   * libfakeroot is not in the runtime or graphics stack provider, only
   * the current namespace */
  "/path/to/" MOCK_LIB_64 "/libfakeroot.so:abi=" SRT_ABI_X86_64,
  "/path/to/" MOCK_LIB_32 "/libfakeroot.so:abi=" SRT_ABI_I386,
  /* Our mock implementation of pv_runtime_has_library() behaves as though
   * libpthread.so.0 *is* in the runtime, as we would expect */
  "libpthread.so.0",
};

static void
test_remap_ld_preload (Fixture *f,
                       gconstpointer context)
{
  g_autoptr(FlatpakExports) exports = fixture_create_exports (f);
  g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(PvRuntime) runtime = fixture_create_runtime (f, PV_RUNTIME_FLAGS_NONE);
  gsize i;

  populate_ld_preload (f, argv, PV_APPEND_PRELOAD_FLAGS_NONE, runtime, exports);

  g_assert_cmpuint (argv->len, ==, G_N_ELEMENTS (expected_preload_paths));

  for (i = 0; i < argv->len; i++)
    {
      char *argument = g_ptr_array_index (argv, i);
      g_assert_true (g_str_has_prefix (argument, "--ld-preload="));
      argument += strlen("--ld-preload=");

      if (g_str_has_prefix (expected_preload_paths[i], "/lib/")
          || g_str_has_prefix (expected_preload_paths[i], "/usr/lib/"))
        {
          g_assert_true (g_str_has_prefix (argument, "/run/host/"));
          argument += strlen("/run/host");
        }

      g_assert_cmpstr (argument, ==, expected_preload_paths[i]);
    }

  /* FlatpakExports never exports /app */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app/lib/libpreloadA.so"));

  /* We don't always export /home etc. so we have to explicitly export
   * this one */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/home"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/home/me"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/home/me/libpreloadH.so"));

  /* We don't always export /opt, so we have to explicitly export
   * these. */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/opt"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/opt/lib"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/opt/" MOCK_LIB_32 "/libpreloadL.so"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/opt/" MOCK_LIB_64 "/libpreloadL.so"));

  /* We don't always export /platform, so we have to explicitly export
   * these. */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/platform"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/platform/plat-" MOCK_PLATFORM_32 "/libpreloadP.so"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/platform/plat-" MOCK_PLATFORM_64 "/libpreloadP.so"));

  /* FlatpakExports never exports /lib as /lib */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/lib/libpreload-rootfs.so"));

  /* FlatpakExports never exports /usr as /usr */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr/lib/libpreloadU.so"));

  /* We assume STEAM_COMPAT_CLIENT_INSTALL_PATH is dealt with separately */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/steam"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/steam/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/steam/lib/gameoverlayrenderer.so"));

  /* We don't know what ${ORIGIN} will expand to, so we have to cut off at
   * /overlay/libs */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/overlay"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/overlay/libs"));

  /* We don't know what ${FUTURE} will expand to, so we have to cut off at
   * /future */
  g_assert_true (flatpak_exports_path_is_visible (exports, "/future"));

  /* We don't export the entire root directory just because it has a
   * module in it */
  g_assert_true (flatpak_exports_path_is_visible (exports, "/"));
}

static void
test_remap_ld_preload_flatpak (Fixture *f,
                               gconstpointer context)
{
  g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(PvRuntime) runtime = fixture_create_runtime (f, PV_RUNTIME_FLAGS_FLATPAK_SUBSANDBOX);
  gsize i;

  populate_ld_preload (f, argv, PV_APPEND_PRELOAD_FLAGS_FLATPAK_SUBSANDBOX,
                       runtime, NULL);

  g_assert_cmpuint (argv->len, ==, G_N_ELEMENTS (expected_preload_paths));

  for (i = 0; i < argv->len; i++)
    {
      char *argument = g_ptr_array_index (argv, i);
      g_assert_true (g_str_has_prefix (argument, "--ld-preload="));
      argument += strlen("--ld-preload=");

      if (g_str_has_prefix (expected_preload_paths[i], "/app/")
          || g_str_has_prefix (expected_preload_paths[i], "/lib/")
          || g_str_has_prefix (expected_preload_paths[i], "/usr/lib/"))
        {
          g_assert_true (g_str_has_prefix (argument, "/run/parent/"));
          argument += strlen("/run/parent");
        }

      g_assert_cmpstr (argument, ==, expected_preload_paths[i]);
    }
}

/*
 * In addition to testing the rare case where there's no runtime,
 * this one also exercises PV_APPEND_PRELOAD_FLAGS_REMOVE_GAME_OVERLAY,
 * which is the implementation of --remove-game-overlay.
 */
static void
test_remap_ld_preload_no_runtime (Fixture *f,
                                  gconstpointer context)
{
  g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func (g_free);
  g_autoptr(FlatpakExports) exports = fixture_create_exports (f);
  gsize i, j;

  populate_ld_preload (f, argv, PV_APPEND_PRELOAD_FLAGS_REMOVE_GAME_OVERLAY,
                       NULL, exports);

  g_assert_cmpuint (argv->len, ==, G_N_ELEMENTS (expected_preload_paths) - 1);

  for (i = 0, j = 0; i < argv->len; i++, j++)
    {
      char *argument = g_ptr_array_index (argv, i);
      g_assert_true (g_str_has_prefix (argument, "--ld-preload="));
      argument += strlen("--ld-preload=");

      /* /steam/lib/gameoverlayrenderer.so is missing because we used the
       * REMOVE_GAME_OVERLAY flag */
      if (g_str_has_suffix (expected_preload_paths[j], "/gameoverlayrenderer.so"))
        {
          /* We expect to skip only one element */
          g_assert_cmpint (i, ==, j);
          j++;
        }

      g_assert_cmpstr (argument, ==, expected_preload_paths[j]);
    }

  /* FlatpakExports never exports /app */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/app/lib/libpreloadA.so"));

  /* We don't always export /home etc. so we have to explicitly export
   * this one */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/home"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/home/me"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/home/me/libpreloadH.so"));

  /* We don't always export /opt, so we have to explicitly export
   * these. */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/opt"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/opt/lib"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/opt/" MOCK_LIB_32 "/libpreloadL.so"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/opt/" MOCK_LIB_64 "/libpreloadL.so"));

  /* We don't always export /platform, so we have to explicitly export
   * these. */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/platform"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/platform/plat-" MOCK_PLATFORM_32 "/libpreloadP.so"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/platform/plat-" MOCK_PLATFORM_64 "/libpreloadP.so"));

  /* FlatpakExports never exports /lib as /lib */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/lib/libpreload-rootfs.so"));

  /* FlatpakExports never exports /usr as /usr */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr/lib"));
  g_assert_false (flatpak_exports_path_is_visible (exports, "/usr/lib/libpreloadU.so"));

  /* We don't know what ${ORIGIN} will expand to, so we have to cut off at
   * /overlay/libs */
  g_assert_false (flatpak_exports_path_is_visible (exports, "/overlay"));
  g_assert_true (flatpak_exports_path_is_visible (exports, "/overlay/libs"));

  /* We don't know what ${FUTURE} will expand to, so we have to cut off at
   * /future */
  g_assert_true (flatpak_exports_path_is_visible (exports, "/future"));

  /* We don't export the entire root directory just because it has a
   * module in it */
  g_assert_true (flatpak_exports_path_is_visible (exports, "/"));
}

static void
test_remap_ld_preload_flatpak_no_runtime (Fixture *f,
                                          gconstpointer context)
{
  g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func (g_free);
  gsize i;

  populate_ld_preload (f, argv, PV_APPEND_PRELOAD_FLAGS_FLATPAK_SUBSANDBOX,
                       NULL, NULL);

  g_assert_cmpuint (argv->len, ==, G_N_ELEMENTS (expected_preload_paths));

  for (i = 0; i < argv->len; i++)
    {
      char *argument = g_ptr_array_index (argv, i);
      g_assert_true (g_str_has_prefix (argument, "--ld-preload="));
      argument += strlen("--ld-preload=");

      g_assert_cmpstr (argument, ==, expected_preload_paths[i]);
    }
}

int
main (int argc,
      char **argv)
{
  _srt_setenv_disable_gio_modules ();

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/remap-ld-preload", Fixture, NULL,
              setup, test_remap_ld_preload, teardown);
  g_test_add ("/remap-ld-preload-flatpak", Fixture, NULL,
              setup, test_remap_ld_preload_flatpak, teardown);
  g_test_add ("/remap-ld-preload-no-runtime", Fixture, NULL,
              setup, test_remap_ld_preload_no_runtime, teardown);
  g_test_add ("/remap-ld-preload-flatpak-no-runtime", Fixture, NULL,
              setup, test_remap_ld_preload_flatpak_no_runtime, teardown);

  return g_test_run ();
}
