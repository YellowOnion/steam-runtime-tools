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

#include <sys/resource.h>
#include <sys/time.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/input-device-internal.h"
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

static void
test_avoid_gvfs (Fixture *f,
                 gconstpointer context)
{
  /* This doesn't actually call _srt_setenv_disable_gio_modules(),
   * because that's documented
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
test_bits_set (Fixture *f,
               gconstpointer context)
{
  g_assert_true (_srt_all_bits_set (0xff, 0x01 | 0x02 | 0x10));
  g_assert_false (_srt_all_bits_set (0x51, 0x01 | 0x02 | 0x10));
}

static void
test_dir_iter (Fixture *f,
               gconstpointer context)
{
  g_auto(SrtDirIter) iter = SRT_DIR_ITER_CLEARED;
  g_autoptr(GError) error = NULL;
  g_autofree char *prev = NULL;
  struct dirent *dent;

  _srt_dir_iter_init_at (&iter, -1, "/",
                         SRT_DIR_ITER_FLAGS_NONE,
                         NULL,
                         &error);
  g_assert_no_error (error);
  _srt_dir_iter_clear (&iter);

  _srt_dir_iter_init_at (&iter, -1, "/",
                         SRT_DIR_ITER_FLAGS_ENSURE_DTYPE,
                         _srt_dirent_strcmp,
                         &error);
  g_assert_no_error (error);
  _srt_dir_iter_clear (&iter);

  g_test_message ("Iterating over '/' in arbitrary order");
  _srt_dir_iter_init_at (&iter, -1, "/",
                         SRT_DIR_ITER_FLAGS_ENSURE_DTYPE,
                         NULL,
                         &error);
  g_assert_no_error (error);

  while (TRUE)
    {
      gboolean ok = _srt_dir_iter_next_dent (&iter, &dent, NULL, &error);

      g_assert_no_error (error);
      g_assert_true (ok);

      if (dent == NULL)
        break;

      g_assert_cmpstr (dent->d_name, !=, ".");
      g_assert_cmpstr (dent->d_name, !=, "..");
      g_assert_cmpint (dent->d_type, !=, DT_UNKNOWN);
      g_test_message ("%u ino#%lld %s",
                      dent->d_type, (long long) dent->d_ino, dent->d_name);
    }

  g_test_message ("And again");
  _srt_dir_iter_rewind (&iter);

  while (TRUE)
    {
      gboolean ok = _srt_dir_iter_next_dent (&iter, &dent, NULL, &error);

      g_assert_no_error (error);
      g_assert_true (ok);

      if (dent == NULL)
        break;

      g_assert_cmpstr (dent->d_name, !=, ".");
      g_assert_cmpstr (dent->d_name, !=, "..");
      g_assert_cmpint (dent->d_type, !=, DT_UNKNOWN);
      g_test_message ("%u ino#%lld %s",
                      dent->d_type, (long long) dent->d_ino, dent->d_name);
    }

  _srt_dir_iter_clear (&iter);

  g_test_message ("Iterating over '/' in sorted order");
  _srt_dir_iter_init_at (&iter, -1, "/",
                         SRT_DIR_ITER_FLAGS_NONE,
                         _srt_dirent_strcmp,
                         &error);
  g_assert_no_error (error);

  while (TRUE)
    {
      gboolean ok = _srt_dir_iter_next_dent (&iter, &dent, NULL, &error);

      g_assert_no_error (error);
      g_assert_true (ok);

      if (dent == NULL)
        break;

      g_assert_cmpstr (dent->d_name, !=, ".");
      g_assert_cmpstr (dent->d_name, !=, "..");
      g_test_message ("ino#%lld %s",
                      (long long) dent->d_ino, dent->d_name);

      if (prev != NULL)
        {
          g_assert_cmpstr (dent->d_name, >, prev);
          g_clear_pointer (&prev, g_free);
        }

      prev = g_strdup (dent->d_name);
    }

  g_test_message ("And again");
  _srt_dir_iter_rewind (&iter);

  while (TRUE)
    {
      gboolean ok = _srt_dir_iter_next_dent (&iter, &dent, NULL, &error);

      g_assert_no_error (error);
      g_assert_true (ok);

      if (dent == NULL)
        break;

      g_assert_cmpstr (dent->d_name, !=, ".");
      g_assert_cmpstr (dent->d_name, !=, "..");
      g_test_message ("ino#%lld %s",
                      (long long) dent->d_ino, dent->d_name);
    }
}

static void
test_evdev_bits (Fixture *f,
                 gconstpointer context)
{
  unsigned long words[] = { 0x00020001, 0x00080005 };

#ifdef __i386__
  g_assert_cmpuint (BITS_PER_LONG, ==, 32);
  g_assert_cmpuint (LONGS_FOR_BITS (1), ==, 1);
  g_assert_cmpuint (LONGS_FOR_BITS (32), ==, 1);
  g_assert_cmpuint (LONGS_FOR_BITS (33), ==, 2);
  g_assert_cmpuint (CHOOSE_BIT (0), ==, 0);
  g_assert_cmpuint (CHOOSE_BIT (31), ==, 31);
  g_assert_cmpuint (CHOOSE_BIT (32), ==, 0);
  g_assert_cmpuint (CHOOSE_BIT (33), ==, 1);
  g_assert_cmpuint (CHOOSE_BIT (63), ==, 31);
  g_assert_cmpuint (CHOOSE_BIT (64), ==, 0);
  g_assert_cmpuint (CHOOSE_LONG (0), ==, 0);
  g_assert_cmpuint (CHOOSE_LONG (31), ==, 0);
  g_assert_cmpuint (CHOOSE_LONG (32), ==, 1);
  g_assert_cmpuint (CHOOSE_LONG (33), ==, 1);
  g_assert_cmpuint (CHOOSE_LONG (63), ==, 1);
  g_assert_cmpuint (CHOOSE_LONG (64), ==, 2);
#elif defined(__LP64__)
  g_assert_cmpuint (BITS_PER_LONG, ==, 64);
  g_assert_cmpuint (LONGS_FOR_BITS (1), ==, 1);
  g_assert_cmpuint (LONGS_FOR_BITS (64), ==, 1);
  g_assert_cmpuint (LONGS_FOR_BITS (65), ==, 2);
  g_assert_cmpuint (CHOOSE_BIT (0), ==, 0);
  g_assert_cmpuint (CHOOSE_BIT (63), ==, 63);
  g_assert_cmpuint (CHOOSE_BIT (64), ==, 0);
  g_assert_cmpuint (CHOOSE_BIT (65), ==, 1);
  g_assert_cmpuint (CHOOSE_BIT (127), ==, 63);
  g_assert_cmpuint (CHOOSE_BIT (128), ==, 0);
  g_assert_cmpuint (CHOOSE_LONG (0), ==, 0);
  g_assert_cmpuint (CHOOSE_LONG (63), ==, 0);
  g_assert_cmpuint (CHOOSE_LONG (64), ==, 1);
  g_assert_cmpuint (CHOOSE_LONG (65), ==, 1);
  g_assert_cmpuint (CHOOSE_LONG (127), ==, 1);
  g_assert_cmpuint (CHOOSE_LONG (128), ==, 2);
#endif

  /* Among bits 0 to 15, only bit 0 (0x1) is set */
  g_assert_cmpuint (test_bit_checked (0, words, G_N_ELEMENTS (words)), ==, 1);
  g_assert_cmpuint (test_bit_checked (1, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (15, words, G_N_ELEMENTS (words)), ==, 0);

  /* Among bits 16 to 31, only bit 17 (0x2 << 16) is set */
  g_assert_cmpuint (test_bit_checked (16, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (17, words, G_N_ELEMENTS (words)), ==, 1);
  g_assert_cmpuint (test_bit_checked (18, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (31, words, G_N_ELEMENTS (words)), ==, 0);

#ifdef __i386__
  /* Among bits 32 to 63, only bits 32 (0x1 << 32), 34 (0x4 << 32)
   * and 51 (0x8 << 48) are set, and they don't count as set unless we
   * allow ourselves to look that far */
  g_assert_cmpuint (test_bit_checked (32, words, 1), ==, 0);
  g_assert_cmpuint (test_bit_checked (32, words, G_N_ELEMENTS (words)), ==, 1);
  g_assert_cmpuint (test_bit_checked (33, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (34, words, 1), ==, 0);
  g_assert_cmpuint (test_bit_checked (34, words, G_N_ELEMENTS (words)), ==, 1);
  g_assert_cmpuint (test_bit_checked (35, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (50, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (51, words, G_N_ELEMENTS (words)), ==, 1);
  g_assert_cmpuint (test_bit_checked (52, words, G_N_ELEMENTS (words)), ==, 0);
#elif defined(__LP64__)
  /* Among bits 64 to 127, only bits 64 (0x1 << 64), 66 (0x4 << 64)
   * and 83 (0x8 << 80) are set, and they don't count as set unless we
   * allow ourselves to look that far */
  g_assert_cmpuint (test_bit_checked (64, words, 1), ==, 0);
  g_assert_cmpuint (test_bit_checked (64, words, G_N_ELEMENTS (words)), ==, 1);
  g_assert_cmpuint (test_bit_checked (65, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (66, words, 1), ==, 0);
  g_assert_cmpuint (test_bit_checked (66, words, G_N_ELEMENTS (words)), ==, 1);
  g_assert_cmpuint (test_bit_checked (67, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (82, words, G_N_ELEMENTS (words)), ==, 0);
  g_assert_cmpuint (test_bit_checked (83, words, G_N_ELEMENTS (words)), ==, 1);
  g_assert_cmpuint (test_bit_checked (84, words, G_N_ELEMENTS (words)), ==, 0);
#endif
}

typedef struct
{
  const char *name;
  mode_t mode;
} File;

typedef struct
{
  const char *name;
  const char *target;
} Symlink;

typedef struct
{
  const char *path;
  GFileTest test;
  gboolean expected_result;
} InSysrootTest;

static void
test_file_in_sysroot (Fixture *f,
                      gconstpointer context)
{
  static const char * const prepare_dirs[] =
  {
    "dir1/dir2/dir3",
  };

  static const File prepare_files[] =
  {
    { "dir1/file1", 0600 },
    { "dir1/dir2/file2", 0600 },
    { "dir1/exec1", 0700 },
  };

  static const Symlink prepare_symlinks[] =
  {
    { "dir1/dir2/symlink_to_dir3", "dir3" },
    { "dir1/dir2/symlink_to_file2", "file2" },
    { "dir1/dir2/sym_to_sym_to_file2", "symlink_to_file2" },
    { "dir1/abs_symlink_to_run", "/run" },
  };

  static const InSysrootTest tests[] =
  {
    { "dir1", G_FILE_TEST_IS_DIR, TRUE },
    { "dir1", G_FILE_TEST_EXISTS, TRUE },
    { "/dir1", G_FILE_TEST_EXISTS, TRUE },
    { "dir1/dir2", G_FILE_TEST_IS_DIR, TRUE },
    /* These gets solved in sysroot, following symlinks too */
    { "dir1/dir2/symlink_to_dir3", G_FILE_TEST_IS_DIR, TRUE },
    { "dir1/dir2/sym_to_sym_to_file2", G_FILE_TEST_IS_REGULAR, TRUE },
    { "dir1/abs_symlink_to_run", G_FILE_TEST_IS_DIR, FALSE },
    { "dir1/missing", G_FILE_TEST_EXISTS, FALSE },
    { "dir1/file1", G_FILE_TEST_IS_REGULAR, TRUE },
    { "dir1/file1", (G_FILE_TEST_IS_DIR | G_FILE_TEST_IS_EXECUTABLE), FALSE },
    { "dir1/exec1", G_FILE_TEST_IS_REGULAR, TRUE },
    { "dir1/exec1", G_FILE_TEST_IS_EXECUTABLE, TRUE },
  };

  g_autoptr(GError) error = NULL;
  g_auto(GLnxTmpDir) tmpdir = { FALSE };
  gsize i;

  glnx_mkdtemp ("test-XXXXXX", 0700, &tmpdir, &error);
  g_assert_no_error (error);

  for (i = 0; i < G_N_ELEMENTS (prepare_dirs); i++)
    {
      const char *it = prepare_dirs[i];

      glnx_shutil_mkdir_p_at (tmpdir.fd, it, 0700, NULL, &error);
      g_assert_no_error (error);
    }

  for (i = 0; i < G_N_ELEMENTS (prepare_files); i++)
    {
      const File *it = &prepare_files[i];

      glnx_autofd int fd = openat (tmpdir.fd, it->name, O_WRONLY|O_CREAT, it->mode);
      if (fd == -1)
        g_error ("openat %s: %s", it->name, g_strerror (errno));
    }

  for (i = 0; i < G_N_ELEMENTS (prepare_symlinks); i++)
    {
      const Symlink *it = &prepare_symlinks[i];

      if (symlinkat (it->target, tmpdir.fd, it->name) != 0)
        g_error ("symlinkat %s: %s", it->name, g_strerror (errno));
    }

  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      const InSysrootTest *it = &tests[i];

      g_assert_cmpint (_srt_file_test_in_sysroot (tmpdir.path, -1,
                                                  it->path, it->test),
                       ==, it->expected_result);
    }
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

      g_assert_cmpstr (_srt_get_path_after (str, prefix), ==, expected);
    }
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

static void
test_gstring_replace (Fixture *f,
                      gconstpointer context)
{
  static const struct
  {
    const char *string;
    const char *original;
    const char *replacement;
    const char *expected;
  }
  tests[] =
  {
    { "/usr/$LIB/libMangoHud.so", "$LIB", "lib32", "/usr/lib32/libMangoHud.so" },
    { "food for foals", "o", "", "fd fr fals" },
    { "aaa", "a", "aaa", "aaaaaaaaa" },
    { "aaa", "a", "", "" },
    { "aaa", "aa", "bb", "bba" },
  };
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      g_autoptr(GString) buffer = g_string_new (tests[i].string);

      g_string_replace (buffer, tests[i].original, tests[i].replacement, 0);
      g_assert_cmpstr (buffer->str, ==, tests[i].expected);
      g_assert_cmpuint (buffer->len, ==, strlen (tests[i].expected));
      g_assert_cmpuint (buffer->allocated_len, >=, strlen (tests[i].expected) + 1);
    }
}

static void
test_hash_iter (Fixture *f,
                gconstpointer context)
{
  g_autoptr(GHashTable) table = g_hash_table_new_full (g_str_hash,
                                                       g_str_equal,
                                                       g_free,
                                                       g_free);
  g_auto(SrtHashTableIter) iter = SRT_HASH_TABLE_ITER_CLEARED;
  const char *prev = NULL;
  const char *k;
  const char *v;

  g_hash_table_replace (table, g_strdup ("1"), g_strdup ("one"));
  g_hash_table_replace (table, g_strdup ("2"), g_strdup ("two"));
  g_hash_table_replace (table, g_strdup ("3"), g_strdup ("three"));

  _srt_hash_table_iter_init (&iter, table);
  _srt_hash_table_iter_clear (&iter);

  _srt_hash_table_iter_init_sorted (&iter, table, NULL);
  _srt_hash_table_iter_clear (&iter);

  _srt_hash_table_iter_init_sorted (&iter, table, _srt_generic_strcmp0);
  _srt_hash_table_iter_clear (&iter);

  g_test_message ("Iterating in arbitrary order");
  _srt_hash_table_iter_init (&iter, table);

  while (_srt_hash_table_iter_next (&iter, &k, &v))
    g_test_message ("%s -> %s", k, v);

  _srt_hash_table_iter_clear (&iter);

  g_test_message ("Iterating in arbitrary order, keys only");
  _srt_hash_table_iter_init_sorted (&iter, table, NULL);

  while (_srt_hash_table_iter_next (&iter, &k, NULL))
    g_test_message ("%s -> (value)", k);

  _srt_hash_table_iter_clear (&iter);

  g_test_message ("Iterating in arbitrary order, values only");
  _srt_hash_table_iter_init_sorted (&iter, table, NULL);

  while (_srt_hash_table_iter_next (&iter, NULL, &v))
    g_test_message ("(key) -> %s", v);

  _srt_hash_table_iter_clear (&iter);

  g_test_message ("Iterating in sorted order");
  prev = NULL;
  _srt_hash_table_iter_init_sorted (&iter, table, _srt_generic_strcmp0);

  while (_srt_hash_table_iter_next (&iter, &k, &v))
    {
      g_test_message ("%s -> %s", k, v);

      if (prev != NULL)
        g_assert_cmpstr (k, >, prev);

      prev = k;
    }

  _srt_hash_table_iter_clear (&iter);

  g_test_message ("Iterating in sorted order, keys only");
  prev = NULL;
  _srt_hash_table_iter_init_sorted (&iter, table, _srt_generic_strcmp0);

  while (_srt_hash_table_iter_next (&iter, &k, NULL))
    {
      g_test_message ("%s -> (value)", k);

      if (prev != NULL)
        g_assert_cmpstr (k, >, prev);

      prev = k;
    }

  _srt_hash_table_iter_clear (&iter);

  g_test_message ("Iterating in sorted order, values only");
  _srt_hash_table_iter_init_sorted (&iter, table, _srt_generic_strcmp0);

  while (_srt_hash_table_iter_next (&iter, NULL, &v))
    g_test_message ("(key) -> %s", v);
}

G_STATIC_ASSERT (FD_SETSIZE == 1024);

static void
test_rlimit (Fixture *f,
             gconstpointer context)
{
  struct rlimit original;
  struct rlimit adjusted;

  if (getrlimit (RLIMIT_NOFILE, &original) < 0)
    {
      int saved_errno = errno;

      g_test_skip_printf ("getrlimit: %s", g_strerror (saved_errno));
      return;
    }

  if (original.rlim_max < 2048)
    {
      g_test_skip ("RLIMIT_NOFILE rlim_max is too small");
      return;
    }

  adjusted = original;
  adjusted.rlim_cur = 2048;
  g_assert_no_errno (setrlimit (RLIMIT_NOFILE, &adjusted));
  g_assert_cmpint (_srt_set_compatible_resource_limits (0), ==, 0);
  g_assert_no_errno (getrlimit (RLIMIT_NOFILE, &adjusted));
  g_assert_cmpint (adjusted.rlim_cur, ==, 1024);
  g_assert_cmpint (adjusted.rlim_max, ==, original.rlim_max);

  adjusted = original;
  adjusted.rlim_cur = 512;
  g_assert_no_errno (setrlimit (RLIMIT_NOFILE, &adjusted));
  g_assert_cmpint (_srt_set_compatible_resource_limits (getpid ()), ==, 0);
  g_assert_no_errno (getrlimit (RLIMIT_NOFILE, &adjusted));
  g_assert_cmpint (adjusted.rlim_cur, ==, 1024);
  g_assert_cmpint (adjusted.rlim_max, ==, original.rlim_max);

  adjusted = original;
  adjusted.rlim_cur = 1024;
  g_assert_no_errno (setrlimit (RLIMIT_NOFILE, &adjusted));
  g_assert_cmpint (_srt_set_compatible_resource_limits (0), ==, 0);
  g_assert_no_errno (getrlimit (RLIMIT_NOFILE, &adjusted));
  g_assert_cmpint (adjusted.rlim_cur, ==, 1024);
  g_assert_cmpint (adjusted.rlim_max, ==, original.rlim_max);
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

  g_assert_true (_srt_is_same_file ("/dev/null", "/dev/null"));
  g_assert_true (_srt_is_same_file ("/nonexistent", "/nonexistent"));
  g_assert_false (_srt_is_same_file ("/dev/null", "/dev/zero"));
  g_assert_false (_srt_is_same_file ("/dev/null", "/nonexistent"));
  g_assert_false (_srt_is_same_file ("/nonexistent", "/dev/null"));
  g_assert_false (_srt_is_same_file ("/nonexistent", "/nonexistent/also"));

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

  g_assert_true (_srt_is_same_file (hard_link_from, hard_link_to));
  g_assert_false (_srt_is_same_file (hard_link_from, "/dev/null"));

  if (symlink ("/dev/null", symlink_to_dev_null) != 0)
    g_error ("Could not create symlink \"%s\" -> /dev/null: %s",
             symlink_to_dev_null, g_strerror (errno));

  g_assert_true (_srt_is_same_file (symlink_to_dev_null, "/dev/null"));
  g_assert_false (_srt_is_same_file (symlink_to_dev_null, "/dev/zero"));

  glnx_shutil_rm_rf_at (-1, temp, NULL, &error);
  g_assert_no_error (error);
}

static void
test_str_is_integer (Fixture *f,
                     gconstpointer context)
{
  g_assert_false (_srt_str_is_integer (""));
  g_assert_false (_srt_str_is_integer ("no"));
  g_assert_true (_srt_str_is_integer ("1"));
  g_assert_true (_srt_str_is_integer ("123456789012345678901234567890"));
  g_assert_false (_srt_str_is_integer ("1.23"));
  g_assert_false (_srt_str_is_integer ("x23"));
  g_assert_false (_srt_str_is_integer ("23a"));
}

static const char uevent[] =
"DRIVER=lenovo\n"
"HID_ID=0003:000017EF:00006009\n"
"HID_NAME=Lite-On Technology Corp. ThinkPad USB Keyboard with TrackPoint\n"
"HID_PHYS=usb-0000:00:14.0-2/input0\n"
"HID_UNIQ=\n"
"MODALIAS=hid:b0003g0000v000017EFp00006009\n";

static struct
{
  const char *key;
  const char *value;
} uevent_parsed[] =
{
  { "DRIVER", "lenovo" },
  { "HID_ID", "0003:000017EF:00006009" },
  { "HID_NAME", "Lite-On Technology Corp. ThinkPad USB Keyboard with TrackPoint" },
  { "HID_PHYS", "usb-0000:00:14.0-2/input0" },
  { "HID_UNIQ", "" },
  { "MODALIAS", "hid:b0003g0000v000017EFp00006009" }
};

static const char no_newline[] = "DRIVER=lenovo";

static void
test_uevent_field (Fixture *f,
                   gconstpointer context)
{
  gsize i;

  g_assert_false (_srt_input_device_uevent_field_equals (no_newline, "DRIVER", ""));
  g_assert_false (_srt_input_device_uevent_field_equals (no_newline, "DRIVER", "lenov"));
  g_assert_true (_srt_input_device_uevent_field_equals (no_newline, "DRIVER", "lenovo"));
  g_assert_false (_srt_input_device_uevent_field_equals (no_newline, "DRIVER", "lenovoo"));

  g_assert_false (_srt_input_device_uevent_field_equals (uevent, "DRIVER", "lenov"));
  g_assert_false (_srt_input_device_uevent_field_equals (uevent, "DRIVER", "lenovoo"));
  g_assert_false (_srt_input_device_uevent_field_equals (uevent, "HID_ID", "0003:000017EF:0000600"));
  g_assert_false (_srt_input_device_uevent_field_equals (uevent, "HID_ID", "0003:000017EF:000060099"));
  g_assert_false (_srt_input_device_uevent_field_equals (uevent, "HID_UNIQ", "x"));
  g_assert_false (_srt_input_device_uevent_field_equals (uevent, "MODALIAS", "nope"));
  g_assert_false (_srt_input_device_uevent_field_equals (uevent, "NOPE", ""));
  g_assert_false (_srt_input_device_uevent_field_equals (uevent, "NOPE", "nope"));

  for (i = 0; i < G_N_ELEMENTS (uevent_parsed); i++)
    {
      g_autofree gchar *v = NULL;

      v = _srt_input_device_uevent_field (uevent, uevent_parsed[i].key);
      g_assert_cmpstr (v, ==, uevent_parsed[i].value);
      g_assert_true (_srt_input_device_uevent_field_equals (uevent,
                                                            uevent_parsed[i].key,
                                                            uevent_parsed[i].value));
    }

  g_assert_null (_srt_input_device_uevent_field (uevent, "NOPE"));
}

int
main (int argc,
      char **argv)
{
  _srt_setenv_disable_gio_modules ();

  _srt_tests_init (&argc, &argv, NULL);
  g_test_add ("/utils/avoid-gvfs", Fixture, NULL, setup, test_avoid_gvfs, teardown);
  g_test_add ("/utils/bits-set", Fixture, NULL,
              setup, test_bits_set, teardown);
  g_test_add ("/utils/dir-iter", Fixture, NULL,
              setup, test_dir_iter, teardown);
  g_test_add ("/utils/evdev-bits", Fixture, NULL,
              setup, test_evdev_bits, teardown);
  g_test_add ("/utils/test-file-in-sysroot", Fixture, NULL,
              setup, test_file_in_sysroot, teardown);
  g_test_add ("/utils/filter_gameoverlayrenderer", Fixture, NULL, setup,
              filter_gameoverlayrenderer, teardown);
  g_test_add ("/utils/get-path-after", Fixture, NULL,
              setup, test_get_path_after, teardown);
  g_test_add ("/utils/gstring-replace", Fixture, NULL,
              setup, test_gstring_replace, teardown);
  g_test_add ("/utils/hash-iter", Fixture, NULL,
              setup, test_hash_iter, teardown);
  g_test_add ("/utils/rlimit", Fixture, NULL,
              setup, test_rlimit, teardown);
  g_test_add ("/utils/same-file", Fixture, NULL,
              setup, test_same_file, teardown);
  g_test_add ("/utils/str_is_integer", Fixture, NULL,
              setup, test_str_is_integer, teardown);
  g_test_add ("/utils/uevent-field", Fixture, NULL,
              setup, test_uevent_field, teardown);

  return g_test_run ();
}
