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

#include "steam-runtime-tools/libdl-internal.h"
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

#define ERROR SRT_LOADABLE_KIND_ERROR
#define PATH SRT_LOADABLE_KIND_PATH
#define BASE SRT_LOADABLE_KIND_BASENAME
#define DYN SRT_LOADABLE_FLAGS_DYNAMIC_TOKENS
#define ABI SRT_LOADABLE_FLAGS_ABI_DEPENDENT
#define ORIGIN SRT_LOADABLE_FLAGS_ORIGIN
#define UNKNOWN SRT_LOADABLE_FLAGS_UNKNOWN_TOKENS
#define NONE SRT_LOADABLE_FLAGS_NONE
static const struct
{
  const char *loadable;
  SrtLoadableKind kind;
  SrtLoadableFlags flags;
}
libdl_classify_tests[] =
{
    { "", ERROR, NONE },
    { "/usr/lib/libc.so.6", PATH, NONE },
    { "/usr/$LIB/libMangoHud.so", PATH, DYN|ABI },
    { "${LIB}/libfoo.so", PATH, DYN|ABI },
    { "/usr/plat-$PLATFORM/libc.so.6", PATH, DYN|ABI },
    { "${PLATFORM}/libc.so.6", PATH, DYN|ABI },
    { "${ORIGIN}/../${LIB}/libc.so.6", PATH, DYN|ABI|ORIGIN },
    { "/$ORIGIN/libfoo", PATH, DYN|ORIGIN },
    { "$ORIGIN/$FUTURE/${PLATFORM}-libfoo.so", PATH, DYN|ORIGIN|ABI|UNKNOWN },
    { "${FUTURE}/libfoo.so", PATH, DYN|UNKNOWN },
    { "libc.so.6", BASE, NONE },
    { "libMangoHud.so", BASE, NONE },
    { "looks-like-${TOKENS}-interpreted-but-not-really", BASE, NONE },
};
#undef ERROR
#undef PATH
#undef BASE
#undef DYN
#undef ABI
#undef ORIGIN
#undef UNKNOWN
#undef NONE

static void
test_libdl_classify (Fixture *f,
                     gconstpointer context)
{
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (libdl_classify_tests); i++)
    {
      SrtLoadableFlags flags = -1;
      SrtLoadableKind kind;

      kind = _srt_loadable_classify (libdl_classify_tests[i].loadable, &flags);

      g_assert_cmpuint (kind, ==, libdl_classify_tests[i].kind);
      g_assert_cmphex (flags, ==, libdl_classify_tests[i].flags);

      kind = _srt_loadable_classify (libdl_classify_tests[i].loadable, NULL);
      g_assert_cmpuint (kind, ==, libdl_classify_tests[i].kind);
    }
}

int
main (int argc,
      char **argv)
{
  _srt_tests_init (&argc, &argv, NULL);

  g_test_add ("/libdl/classify", Fixture, NULL, setup,
              test_libdl_classify, teardown);

  return g_test_run ();
}
