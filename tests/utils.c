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
#include <glib-object.h>

#include "steam-runtime-tools/utils-internal.h"
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
 * Test _srt_filter_gameoverlayrenderer function.
 */
static void
filter_gameoverlayrenderer (Fixture *f,
                            gconstpointer context)
{
  const char *ld_preload1 = "/home/me/.local/share/Steam/ubuntu12_32/gameoverlayrenderer.so:"
                            "/home/me/.local/share/Steam/ubuntu12_64/gameoverlayrenderer.so";

  const char *ld_preload2 = ":/home/me/my/lib.so:"
                            "/home/me/.local/share/Steam/ubuntu12_32/gameoverlayrenderer.so:"
                            "/home/me/.local/share/Steam/ubuntu12_64/gameoverlayrenderer.so:"
                            "/home/me/my/second.lib.so:";

  const char *ld_preload3 = "/home/me/my/lib.so:"
                            "/home/me/my/second.lib.so";

  gchar *filtered_preload = NULL;

  filtered_preload = _srt_filter_gameoverlayrenderer (ld_preload1);
  g_assert_cmpstr (filtered_preload, ==, "");
  g_free (filtered_preload);

  filtered_preload = _srt_filter_gameoverlayrenderer (ld_preload2);
  g_assert_cmpstr (filtered_preload, ==,
                   ":/home/me/my/lib.so:/home/me/my/second.lib.so:");
  g_free (filtered_preload);

  filtered_preload = _srt_filter_gameoverlayrenderer (ld_preload3);
  g_assert_cmpstr (filtered_preload, ==,
                   "/home/me/my/lib.so:/home/me/my/second.lib.so");
  g_free (filtered_preload);
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add ("/utils/filter_gameoverlayrenderer", Fixture, NULL, setup,
              filter_gameoverlayrenderer, teardown);

  return g_test_run ();
}
