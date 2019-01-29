// Copyright © 2017 Collabora Ltd

// This file is part of libcapsule.

// libcapsule is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.

// libcapsule is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.

// You should have received a copy of the GNU Lesser General Public
// License along with libcapsule.  If not, see <http://www.gnu.org/licenses/>.

#include <string.h>

#include <glib.h>

#include "utils/utils.h"

typedef struct
{
  int dummy;
} Fixture;

static void
setup (Fixture *f,
       gconstpointer data)
{
}

static struct
{
  const char * const parts[3];
  const char *expected;
} filename_tests[] =
{
    { { "/host", "/usr/lib", "libc.so.6" }, "/host/usr/lib/libc.so.6" },
    { { "/usr/lib", "/libc.so.6", NULL }, "/usr/lib/libc.so.6" },
    { { "", "/usr/lib", "libc.so.6" }, "/usr/lib/libc.so.6" },
    { { "/", "usr/lib", "libc.so.6" }, "/usr/lib/libc.so.6" },
    { { "///host///", "///usr/lib///", "///libc.so.6" },
      "/host/usr/lib/libc.so.6" },
    { { NULL, "xxxxxxxxxxxxxxxx", NULL }, "" },
    { { "", NULL }, "" },
    { { "", "/etc/ld.so.cache", NULL }, "/etc/ld.so.cache" },
    { { "", "etc/ld.so.cache", NULL }, "/etc/ld.so.cache" },
    { { "/", "/etc/ld.so.cache", NULL }, "/etc/ld.so.cache" },
    { { "/", "etc/ld.so.cache", NULL }, "/etc/ld.so.cache" },
    { { "foo", "/bar", NULL }, "foo/bar" },
    { { "foo", "bar", NULL }, "foo/bar" },
};

static void
test_build_filename (Fixture *f,
                     gconstpointer data)
{
  unsigned i;

  for (i = 0; i < G_N_ELEMENTS (filename_tests); i++)
    {
      const char * const *parts = filename_tests[i].parts;
      const char *expected = filename_tests[i].expected;
      const size_t allocated = strlen (expected) + 5;
      gchar *buf = g_malloc (allocated);
      char *str = NULL;
      size_t used;
      size_t len = allocated;

      if (parts[1] == NULL)
        str = build_filename_alloc (parts[0], NULL);
      else if (parts[2] == NULL)
        str = build_filename_alloc (parts[0], parts[1], NULL);
      else
        str = build_filename_alloc (parts[0], parts[1], parts[2], NULL);

      g_assert_cmpstr (str, ==, expected);

      do
        {
          size_t j;

          memset (buf, '\xaa', allocated);

          if (parts[1] == NULL)
            {
              used = build_filename (buf, len, parts[0], NULL);
            }
          else if (parts[2] == NULL)
            {
              used = build_filename (buf, len, parts[0], parts[1], NULL);
            }
          else
            {
              used = build_filename (buf, len, parts[0], parts[1], parts[2],
                                     NULL);
            }

          g_test_message ("\"%s\", \"%s\", \"%s\" -> %zu \"%s\"",
                          parts[0], parts[1], parts[2], used,
                          len == 0 ? NULL : buf);

          g_assert_cmpuint (used, ==, strlen (expected));

          if (len == 0)
            {
              /* Stupid corner case: we can't write anything into the buffer */
            }
          else if (used >= len)
            {
              gchar *truncated = g_strndup (expected, len - 1);

              g_assert_cmpstr (buf, ==, truncated);
              g_free (truncated);
            }
          else
            {
              g_assert_cmpstr (buf, ==, expected);
            }

          /* The rest of the buffer is untouched (we didn't overflow) */
          for (j = len; j < allocated; j++)
            {
              g_assert_cmpint (buf[j], ==, '\xaa');
            }
        }
      while (--len > 0);

      g_free (buf);
      free (str);
    }
}

static void
teardown (Fixture *f,
          gconstpointer data)
{
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);
#if GLIB_CHECK_VERSION(2, 38, 0)
  g_test_set_nonfatal_assertions ();
#endif

  g_test_add ("/build-filename", Fixture, NULL, setup,
              test_build_filename, teardown);

  return g_test_run ();
}
