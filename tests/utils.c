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
  _srt_setenv_disable_gio_modules ();

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/utils/avoid-gvfs", Fixture, NULL, setup, test_avoid_gvfs, teardown);
  g_test_add ("/utils/test-file-in-sysroot", Fixture, NULL,
              setup, test_file_in_sysroot, teardown);
  g_test_add ("/utils/filter_gameoverlayrenderer", Fixture, NULL, setup,
              filter_gameoverlayrenderer, teardown);

  return g_test_run ();
}
