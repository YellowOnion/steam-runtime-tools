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

#include "steam-runtime-tools/library-internal.h"
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
 * Test basic functionality of the SrtLibrary object.
 */
static void
test_object (Fixture *f,
             gconstpointer context)
{
  SrtLibrary *library;
  const char * const *missing;
  SrtLibraryIssues issues;
  gchar *tuple;
  gchar *soname;
  GStrv missing_mutable;
  const char * const one_missing[] = { "jpeg_mem_src@LIBJPEGTURBO_6.2", NULL };

  library = _srt_library_new ("arm-linux-gnueabihf",
                              "libz.so.1",
                              SRT_LIBRARY_ISSUES_NONE,
                              NULL);
  g_assert_cmpint (srt_library_get_issues (library), ==,
                   SRT_LIBRARY_ISSUES_NONE);
  g_assert_cmpstr (srt_library_get_multiarch_tuple (library), ==,
                   "arm-linux-gnueabihf");
  g_assert_cmpstr (srt_library_get_soname (library), ==,
                   "libz.so.1");
  missing = srt_library_get_missing_symbols (library);
  g_assert_nonnull (missing);
  g_assert_cmpstr (missing[0], ==, NULL);
  g_object_get (library,
                "multiarch-tuple", &tuple,
                "soname", &soname,
                "missing-symbols", &missing_mutable,
                "issues", &issues,
                NULL);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_NONE);
  g_assert_cmpstr (tuple, ==, "arm-linux-gnueabihf");
  g_assert_cmpstr (soname, ==, "libz.so.1");
  g_assert_nonnull (missing_mutable);
  g_assert_cmpstr (missing_mutable[0], ==, NULL);
  g_free (tuple);
  g_free (soname);
  g_strfreev (missing_mutable);
  g_object_unref (library);

  library = _srt_library_new ("s390x-linux-gnu",
                              "libjpeg.so.62",
                              SRT_LIBRARY_ISSUES_MISSING_SYMBOLS,
                              one_missing);
  g_assert_cmpint (srt_library_get_issues (library), ==,
                   SRT_LIBRARY_ISSUES_MISSING_SYMBOLS);
  g_assert_cmpstr (srt_library_get_multiarch_tuple (library), ==,
                   "s390x-linux-gnu");
  g_assert_cmpstr (srt_library_get_soname (library), ==,
                   "libjpeg.so.62");
  missing = srt_library_get_missing_symbols (library);
  g_assert_nonnull (missing);
  g_assert_cmpstr (missing[0], ==, one_missing[0]);
  g_assert_cmpstr (missing[1], ==, NULL);
  g_object_get (library,
                "multiarch-tuple", &tuple,
                "soname", &soname,
                "missing-symbols", &missing_mutable,
                "issues", &issues,
                NULL);
  g_assert_cmpint (issues, ==, SRT_LIBRARY_ISSUES_MISSING_SYMBOLS);
  g_assert_cmpstr (tuple, ==, "s390x-linux-gnu");
  g_assert_cmpstr (soname, ==, "libjpeg.so.62");
  g_assert_nonnull (missing_mutable);
  g_assert_cmpstr (missing_mutable[0], ==, one_missing[0]);
  g_assert_cmpstr (missing_mutable[1], ==, NULL);
  g_free (tuple);
  g_free (soname);
  g_strfreev (missing_mutable);
  g_object_unref (library);
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add ("/object", Fixture, NULL,
              setup, test_object, teardown);

  return g_test_run ();
}
