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

#include "glib-backports.h"
#include "libglnx/libglnx.h"

#include "test-utils.h"
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
test_avoid_gvfs (Fixture *f,
                 gconstpointer context)
{
  /* This doesn't actually call pv_avoid_gvfs(), because that's documented
   * to have to happen as early as possible in main(). Instead, we do that
   * in main() as documented, and in this function we just assert that
   * we did. */
  GVfs *vfs = g_vfs_get_default ();
  GVfs *local = g_vfs_get_local ();

  g_test_message ("Default VFS: %s at %p", G_OBJECT_TYPE_NAME (vfs), vfs);
  g_test_message ("Local VFS: %s at %p", G_OBJECT_TYPE_NAME (local), local);
  /* We compare by string equality to have a better message if this
   * assertion fails. We can't assert that the pointers are the same,
   * because GLib currently uses two instances of the same class. */
  g_assert_cmpstr (G_OBJECT_TYPE_NAME (vfs), ==,
                   G_OBJECT_TYPE_NAME (local));
  g_assert_cmpuint (G_OBJECT_TYPE (vfs), ==,
                    G_OBJECT_TYPE (local));
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
  g_assert_error (error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT);
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
test_same_file (Fixture *f,
                gconstpointer context)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *temp = NULL;
  g_autofree gchar *hard_link_from = NULL;
  g_autofree gchar *hard_link_to = NULL;
  g_autofree gchar *symlink_to_dev_null = NULL;

  g_assert_true (pv_is_same_file ("/dev/null", "/dev/null"));
  g_assert_true (pv_is_same_file ("/nonexistent", "/nonexistent"));
  g_assert_false (pv_is_same_file ("/dev/null", "/dev/zero"));
  g_assert_false (pv_is_same_file ("/dev/null", "/nonexistent"));
  g_assert_false (pv_is_same_file ("/nonexistent", "/dev/null"));
  g_assert_false (pv_is_same_file ("/nonexistent", "/nonexistent/also"));

  temp = g_dir_make_tmp (NULL, &error);
  g_assert_no_error (error);
  g_assert_nonnull (temp);

  hard_link_from = g_build_filename (temp, "hard-link-from", NULL);
  hard_link_to = g_build_filename (temp, "hard-link-to", NULL);
  symlink_to_dev_null = g_build_filename (temp, "symlink", NULL);

  g_file_set_contents (hard_link_from, "hello", -1, NULL);
  g_assert_no_error (error);

  if (link (hard_link_from, hard_link_to) != 0)
    g_error ("Could not create hard link \"%s\" -> /dev/null: %s",
             symlink_to_dev_null, g_strerror (errno));

  g_assert_true (pv_is_same_file (hard_link_from, hard_link_to));
  g_assert_false (pv_is_same_file (hard_link_from, "/dev/null"));

  if (symlink ("/dev/null", symlink_to_dev_null) != 0)
    g_error ("Could not create symlink \"%s\" -> /dev/null: %s",
             symlink_to_dev_null, g_strerror (errno));

  g_assert_true (pv_is_same_file (symlink_to_dev_null, "/dev/null"));
  g_assert_false (pv_is_same_file (symlink_to_dev_null, "/dev/zero"));

  glnx_shutil_rm_rf_at (-1, temp, NULL, &error);
  g_assert_no_error (error);
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
  pv_avoid_gvfs ();

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/arbitrary-key", Fixture, NULL,
              setup, test_arbitrary_key, teardown);
  g_test_add ("/avoid-gvfs", Fixture, NULL, setup, test_avoid_gvfs, teardown);
  g_test_add ("/capture-output", Fixture, NULL,
              setup, test_capture_output, teardown);
  g_test_add ("/envp-cmp", Fixture, NULL, setup, test_envp_cmp, teardown);
  g_test_add ("/same-file", Fixture, NULL, setup, test_same_file, teardown);
  g_test_add ("/search-path-append", Fixture, NULL,
              setup, test_search_path_append, teardown);

  return g_test_run ();
}
