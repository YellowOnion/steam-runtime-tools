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

/* Include it before steam-runtime-tools.h so that its backport of
 * G_DEFINE_AUTOPTR_CLEANUP_FUNC will be visible to it */
#include "steam-runtime-tools/glib-backports-internal.h"

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "steam-runtime-tools/container-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "test-utils.h"

static const char *argv0;
static gchar *global_sysroots;

typedef struct
{
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
 * Test basic functionality of the SrtContainerInfo object.
 */
static void
test_object (Fixture *f,
             gconstpointer context)
{
  g_autoptr(SrtContainerInfo) container = NULL;
  SrtContainerType type;
  g_autofree gchar *flatpak_version = NULL;
  g_autofree gchar *host_directory = NULL;

  container = _srt_container_info_new (SRT_CONTAINER_TYPE_FLATPAK,
                                       "1.10.2",
                                       "/run/host");

  g_assert_cmpint (srt_container_info_get_container_type (container), ==,
                   SRT_CONTAINER_TYPE_FLATPAK);
  g_assert_cmpstr (srt_container_info_get_flatpak_version (container), ==, "1.10.2");
  g_assert_cmpstr (srt_container_info_get_container_host_directory (container), ==,
                   "/run/host");
  g_object_get (container,
                "type", &type,
                "flatpak-version", &flatpak_version,
                "host-directory", &host_directory,
                NULL);
  g_assert_cmpint (type, ==, SRT_CONTAINER_TYPE_FLATPAK);
  g_assert_cmpstr (flatpak_version, ==, "1.10.2");
  g_assert_cmpstr (host_directory, ==, "/run/host");
}

typedef struct
{
  const char *description;
  const char *sysroot;
  SrtContainerType type;
  const char *host_directory;
  const char *flatpak_version;
} ContainerTest;

static const ContainerTest container_tests[] =
{
  {
    .description = "Has /.dockerenv",
    .sysroot = "debian-unstable",
    .type = SRT_CONTAINER_TYPE_DOCKER,
  },
  {
    .description = "Has an unknown value in /run/systemd/container",
    .sysroot = "debian10",
    .type = SRT_CONTAINER_TYPE_UNKNOWN,
  },
  {
    .description = "Has 'docker' in /run/systemd/container",
    .sysroot = "fedora",
    .type = SRT_CONTAINER_TYPE_DOCKER,
  },
  {
    .description = "Has /.flatpak-info and /run/host",
    .sysroot = "flatpak-example",
    .type = SRT_CONTAINER_TYPE_FLATPAK,
    .host_directory = "/run/host",
    .flatpak_version = "1.10.2",
  },
  {
    .description = "Has /run/host",
    .sysroot = "invalid-os-release",
    .type = SRT_CONTAINER_TYPE_UNKNOWN,
    .host_directory = "/run/host",
  },
  {
    .description = "Has no evidence of being a container",
    .sysroot = "no-os-release",
    .type = SRT_CONTAINER_TYPE_NONE,
  },
  {
    .description = "Has /run/pressure-vessel",
    .sysroot = "steamrt",
    .type = SRT_CONTAINER_TYPE_PRESSURE_VESSEL,
  },
  {
    .description = "Has a Docker-looking /proc/1/cgroup",
    .sysroot = "steamrt-unofficial",
    .type = SRT_CONTAINER_TYPE_DOCKER,
  },
  {
    .description = "Has 'podman' in /run/host/container-manager",
    .sysroot = "podman-example",
    .type = SRT_CONTAINER_TYPE_PODMAN,
    .host_directory = "/run/host",
  },
};

static void
test_containers (Fixture *f,
                 gconstpointer context)
{
  gsize i, j;

  for (i = 0; i < G_N_ELEMENTS (container_tests); i++)
    {
      const ContainerTest *test = &container_tests[i];
      g_autoptr(SrtSystemInfo) info;
      g_autofree gchar *expected_host = NULL;
      g_autofree gchar *sysroot = NULL;

      g_test_message ("%s: %s", test->sysroot, test->description);

      sysroot = g_build_filename (global_sysroots, test->sysroot, NULL);

      info = srt_system_info_new (NULL);
      g_assert_nonnull (info);
      srt_system_info_set_sysroot (info, sysroot);

      if (test->host_directory == NULL)
        expected_host = NULL;
      else
        expected_host = g_build_filename (sysroot, test->host_directory, NULL);

      for (j = 0; j < 2; j++)
        {
          g_autoptr(SrtContainerInfo) container = NULL;
          g_autofree gchar *host_dir_dup = NULL;

          container = srt_system_info_check_container (info);
          g_assert_nonnull (container);

          g_assert_cmpint (srt_system_info_get_container_type (info), ==,
                           test->type);
          g_assert_cmpint (srt_container_info_get_container_type (container), ==,
                           test->type);

          host_dir_dup = srt_system_info_dup_container_host_directory (info);
          g_assert_cmpstr (host_dir_dup, ==, expected_host);
          g_assert_cmpstr (srt_container_info_get_container_host_directory (container),
                           ==, expected_host);

          g_assert_cmpstr (srt_container_info_get_flatpak_version (container),
                           ==, test->flatpak_version);
        }
    }
}

int
main (int argc,
      char **argv)
{
  argv0 = argv[0];
  global_sysroots = _srt_global_setup_sysroots (argv0);

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/container/object", Fixture, NULL,
              setup, test_object, teardown);
  g_test_add ("/container/containers", Fixture, NULL,
              setup, test_containers, teardown);

  return g_test_run ();
}
