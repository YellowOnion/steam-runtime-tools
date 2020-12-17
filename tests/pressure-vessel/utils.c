/*
 * Copyright © 2019-2020 Collabora Ltd.
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
#include "utils.h"

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

static void
test_arbitrary_key (Fixture *f,
                    gconstpointer context)
{
  g_autoptr(GHashTable) table = g_hash_table_new (g_str_hash, g_str_equal);
  gpointer k;

  k = pv_hash_table_get_arbitrary_key (table);
  g_assert_null (k);

  g_hash_table_add (table, (char *) "hello");
  k = pv_hash_table_get_arbitrary_key (table);
  g_assert_cmpstr (k, ==, "hello");

  g_hash_table_add (table, (char *) "world");
  k = pv_hash_table_get_arbitrary_key (table);

  if (g_strcmp0 (k, "hello") != 0)
    g_assert_cmpstr (k, ==, "world");
}

static void
test_capture_output (Fixture *f,
                     gconstpointer context)
{
  const gchar * argv[] =
  {
    "printf", "hello\\n", NULL
  };
  g_autofree gchar *output = NULL;
  g_autoptr(GError) error = NULL;

  output = pv_capture_output (argv, &error);
  g_assert_cmpstr (output, ==, "hello");
  g_assert_no_error (error);
  g_clear_pointer (&output, g_free);

  argv[1] = "hello\\nworld";    /* deliberately no trailing newline */
  output = pv_capture_output (argv, &error);
  g_assert_no_error (error);
  g_assert_cmpstr (output, ==, "hello\nworld");
  g_clear_pointer (&output, g_free);

  argv[0] = "/nonexistent/doesnotexist";
  output = pv_capture_output (argv, &error);
  g_assert_nonnull (error);

  if (error->domain == G_SPAWN_ERROR
      && error->code == G_SPAWN_ERROR_NOENT)
    {
      /* OK: specific failure could be reported */
    }
  else if (error->domain == G_SPAWN_ERROR
           && error->code == G_SPAWN_ERROR_FAILED)
    {
      /* less specific, but also OK */
    }
  else
    {
      g_error ("Expected pv_capture_output() with nonexistent executable "
               "to fail, got %s #%d",
               g_quark_to_string (error->domain), error->code);
    }

  g_assert_cmpstr (output, ==, NULL);
  g_clear_error (&error);

  argv[0] = "false";
  argv[1] = NULL;
  output = pv_capture_output (argv, &error);
  g_assert_error (error, G_SPAWN_EXIT_ERROR, 1);
  g_assert_cmpstr (output, ==, NULL);
  g_clear_error (&error);
}

static void
test_envp_cmp (Fixture *f,
               gconstpointer context)
{
  static const char * const unsorted[] =
  {
    "SAME_NAME=2",
    "EARLY_NAME=a",
    "SAME_NAME=222",
    "Z_LATE_NAME=b",
    "SUFFIX_ADDED=23",
    "SAME_NAME=1",
    "SAME_NAME=",
    "SUFFIX=42",
    "SAME_NAME=3",
    "SAME_NAME",
  };
  static const char * const sorted[] =
  {
    "EARLY_NAME=a",
    "SAME_NAME",
    "SAME_NAME=",
    "SAME_NAME=1",
    "SAME_NAME=2",
    "SAME_NAME=222",
    "SAME_NAME=3",
    "SUFFIX=42",
    "SUFFIX_ADDED=23",
    "Z_LATE_NAME=b",
  };
  G_GNUC_UNUSED const Config *config = context;
  const char **sort_this = NULL;
  gsize i, j;

  G_STATIC_ASSERT (G_N_ELEMENTS (sorted) == G_N_ELEMENTS (unsorted));

  for (i = 0; i < G_N_ELEMENTS (sorted); i++)
    {
      g_autofree gchar *copy = g_strdup (sorted[i]);

      g_test_message ("%s == %s", copy, sorted[i]);
      g_assert_cmpint (pv_envp_cmp (&copy, &sorted[i]), ==, 0);
      g_assert_cmpint (pv_envp_cmp (&sorted[i], &copy), ==, 0);

      for (j = i + 1; j < G_N_ELEMENTS (sorted); j++)
        {
          g_test_message ("%s < %s", sorted[i], sorted[j]);
          g_assert_cmpint (pv_envp_cmp (&sorted[i], &sorted[j]), <, 0);
          g_assert_cmpint (pv_envp_cmp (&sorted[j], &sorted[i]), >, 0);
        }
    }

  sort_this = g_new0 (const char *, G_N_ELEMENTS (unsorted));

  for (i = 0; i < G_N_ELEMENTS (unsorted); i++)
    sort_this[i] = unsorted[i];

  qsort (sort_this, G_N_ELEMENTS (unsorted), sizeof (char *), pv_envp_cmp);

  for (i = 0; i < G_N_ELEMENTS (sorted); i++)
    g_assert_cmpstr (sorted[i], ==, sort_this[i]);

  g_free (sort_this);
}

static void
test_get_path_after (Fixture *f,
                     gconstpointer context)
{
  static const struct
  {
    const char *str;
    const char *prefix;
    const char *expected;
  } tests[] =
  {
    { "/run/host/usr", "/run/host", "usr" },
    { "/run/host/usr", "/run/host/", "usr" },
    { "/run/host", "/run/host", "" },
    { "////run///host////usr", "//run//host", "usr" },
    { "////run///host////usr", "//run//host////", "usr" },
    { "/run/hostage", "/run/host", NULL },
    /* Any number of leading slashes is ignored, even zero */
    { "foo/bar", "/foo", "bar" },
    { "/foo/bar", "foo", "bar" },
  };
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      const char *str = tests[i].str;
      const char *prefix = tests[i].prefix;
      const char *expected = tests[i].expected;

      if (expected == NULL)
        g_test_message ("%s should not have path prefix %s",
                        str, prefix);
      else
        g_test_message ("%s should have path prefix %s followed by %s",
                        str, prefix, expected);

      g_assert_cmpstr (pv_get_path_after (str, prefix), ==, expected);
    }
}

static void
test_search_path_append (Fixture *f,
                         gconstpointer context)
{
  g_autoptr(GString) str = g_string_new ("");

  pv_search_path_append (str, NULL);
  g_assert_cmpstr (str->str, ==, "");

  pv_search_path_append (str, "");
  g_assert_cmpstr (str->str, ==, "");

  pv_search_path_append (str, "/bin");
  g_assert_cmpstr (str->str, ==, "/bin");

  pv_search_path_append (str, NULL);
  g_assert_cmpstr (str->str, ==, "/bin");

  pv_search_path_append (str, "");
  g_assert_cmpstr (str->str, ==, "/bin");

  pv_search_path_append (str, "/usr/bin");
  g_assert_cmpstr (str->str, ==, "/bin:/usr/bin");

  /* Duplicates are not removed */
  pv_search_path_append (str, "/usr/bin");
  g_assert_cmpstr (str->str, ==, "/bin:/usr/bin:/usr/bin");
}

int
main (int argc,
      char **argv)
{
  _srt_setenv_disable_gio_modules ();

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/arbitrary-key", Fixture, NULL,
              setup, test_arbitrary_key, teardown);
  g_test_add ("/capture-output", Fixture, NULL,
              setup, test_capture_output, teardown);
  g_test_add ("/envp-cmp", Fixture, NULL, setup, test_envp_cmp, teardown);
  g_test_add ("/get-path-after", Fixture, NULL,
              setup, test_get_path_after, teardown);
  g_test_add ("/search-path-append", Fixture, NULL,
              setup, test_search_path_append, teardown);

  return g_test_run ();
}
