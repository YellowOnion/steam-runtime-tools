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

#include "test-utils.h"

typedef struct
{
  int unused;
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
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;
}

/*
 * Test basic functionality of the architecture module.
 */
static void
test_architecture (Fixture *f,
                   gconstpointer context)
{
#if defined(__x86_64__) && defined(__LP64__)
  g_assert_true (srt_architecture_can_run_x86_64 ());
#endif

#if defined(__i386__)
  g_assert_true (srt_architecture_can_run_i386 ());
#endif

  g_assert_cmpstr (srt_architecture_get_expected_runtime_linker (SRT_ABI_X86_64),
                   ==, "/lib64/ld-linux-x86-64.so.2");
  g_assert_cmpstr (srt_architecture_get_expected_runtime_linker (SRT_ABI_I386),
                   ==, "/lib/ld-linux.so.2");
  g_assert_cmpstr (srt_architecture_get_expected_runtime_linker ("potato-glados"),
                   ==, NULL);
}

int
main (int argc,
      char **argv)
{
  _srt_tests_init (&argc, &argv, NULL);
  g_test_add ("/architecture", Fixture, NULL,
              setup, test_architecture, teardown);

  return g_test_run ();
}
