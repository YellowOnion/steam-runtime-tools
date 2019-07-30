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

#include <glib.h>
#include <glib/gstdio.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "test-utils.h"

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
 * Test basic functionality of the SrtSystemInfo object.
 */
static void
test_object (Fixture *f,
             gconstpointer context)
{
  SrtSystemInfo *info;
  gchar *expectations_in = NULL;
  gchar *expectations = NULL;
  int fd;

  info = srt_system_info_new (NULL);
  g_assert_nonnull (info);
  g_object_get (info,
                "expectations", &expectations,
                NULL);
  g_assert_cmpstr (expectations, ==, NULL);

  /* We try it twice, to exercise the cached and non-cached cases */
#if defined(__x86_64__) && defined(__LP64__)
  g_assert_true (srt_system_info_can_run (info, SRT_ABI_X86_64));
  g_assert_true (srt_system_info_can_run (info, SRT_ABI_X86_64));
#endif

#if defined(__i386__)
  g_assert_true (srt_system_info_can_run (info, SRT_ABI_I386));
  g_assert_true (srt_system_info_can_run (info, SRT_ABI_I386));
#endif

  g_assert_false (srt_system_info_can_run (info, "hal9000-linux-gnu"));
  g_assert_false (srt_system_info_can_run (info, "hal9000-linux-gnu"));

  /* This is a little bit tautologous - we're using the same check
   * that the production code does. */
  fd = open ("/dev/uinput", O_WRONLY | O_NONBLOCK);

  if (fd >= 0)
    {
      g_assert_true (srt_system_info_can_write_to_uinput (info));
      g_assert_true (srt_system_info_can_write_to_uinput (info));
      close (fd);
    }
  else
    {
      g_assert_false (srt_system_info_can_write_to_uinput (info));
      g_assert_false (srt_system_info_can_write_to_uinput (info));
    }

  g_object_unref (info);

  expectations_in = g_build_filename (f->srcdir, "expectations", NULL);
  info = srt_system_info_new (expectations_in);
  g_assert_nonnull (info);
  g_object_get (info,
                "expectations", &expectations,
                NULL);
  g_assert_cmpstr (expectations, ==, expectations_in);
  g_free (expectations_in);
  g_free (expectations);
  g_object_unref (info);
}

int
main (int argc,
      char **argv)
{
  argv0 = argv[0];

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/system-info/object", Fixture, NULL,
              setup, test_object, teardown);

  return g_test_run ();
}
