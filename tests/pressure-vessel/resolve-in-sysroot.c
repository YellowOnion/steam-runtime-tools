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
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx/libglnx.h"

#include "tests/test-utils.h"
#include "utils.h"

typedef struct
{
  TestsOpenFdSet old_fds;
} Fixture;

static void
setup (Fixture *f,
       gconstpointer context)
{
  f->old_fds = tests_check_fd_leaks_enter ();
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  tests_check_fd_leaks_leave (f->old_fds);
}

static gboolean
fd_same_as_rel_path_nofollow (int fd,
                              int dfd,
                              const gchar *path)
{
  GStatBuf fd_buffer, path_buffer;

  return (fstat (fd, &fd_buffer) == 0
          && fstatat (dfd, path, &path_buffer, AT_SYMLINK_NOFOLLOW) == 0
          && fd_buffer.st_dev == path_buffer.st_dev
          && fd_buffer.st_ino == path_buffer.st_ino);
}

typedef struct
{
  const char *name;
  const char *target;
} Symlink;

typedef enum
{
  RESOLVE_ERROR_DOMAIN_NONE,
  RESOLVE_ERROR_DOMAIN_GIO,
} ResolveErrorDomain;

typedef enum
{
  RESOLVE_CALL_FLAGS_IGNORE_PATH = (1 << 0),
  RESOLVE_CALL_FLAGS_NONE = 0
} ResolveCallFlags;

typedef struct
{
  struct
  {
    const char *path;
    SrtResolveFlags flags;
    ResolveCallFlags test_flags;
  } call;
  struct
  {
    const char *path;
    int code;
  } expect;
} ResolveTest;

static void
test_resolve_in_sysroot (Fixture *f,
                         gconstpointer context)
{
  static const char * const prepare_dirs[] =
  {
    "a/b/c/d/e",
    "a/b2/c2/d2/e2",
  };
  static const char * const prepare_files[] =
  {
    "a/b/c/file",
  };
  static const Symlink prepare_symlinks[] =
  {
    { "a/b/symlink_to_c", "c" },
    { "a/b/symlink_to_b2", "../b2" },
    { "a/b/symlink_to_c2", "../../a/b2/c2" },
    { "a/b/symlink_to_itself", "." },
    { "a/b/abs_symlink_to_run", "/run" },
    { "a/b/long_symlink_to_dev", "../../../../../../../../../../../dev" },
    { "x", "create_me" },
  };
  static const ResolveTest tests[] =
  {
    { { "a/b/c/d" }, { "a/b/c/d" } },
    { { "a/b/c/d/" }, { "a/b/c/d" } },
    {
      { "a/b/c/d", SRT_RESOLVE_FLAGS_NONE, RESOLVE_CALL_FLAGS_IGNORE_PATH },
      { "a/b/c/d" },
    },
    { { "a/b/c/d/", SRT_RESOLVE_FLAGS_MKDIR_P }, { "a/b/c/d" } },
    {
      { "a/b/c/d", SRT_RESOLVE_FLAGS_MKDIR_P, RESOLVE_CALL_FLAGS_IGNORE_PATH },
      { "a/b/c/d" },
    },
    { { "create_me" }, { NULL, G_IO_ERROR_NOT_FOUND } },
    {
      { "create_me", SRT_RESOLVE_FLAGS_NONE, RESOLVE_CALL_FLAGS_IGNORE_PATH },
      { NULL, G_IO_ERROR_NOT_FOUND }
    },
    { { "a/b/c/d", SRT_RESOLVE_FLAGS_MKDIR_P }, { "a/b/c/d" } },
    { { "a/b/c/d", SRT_RESOLVE_FLAGS_READABLE }, { "a/b/c/d" } },
    { { "a/b/c/d", SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY }, { "a/b/c/d" } },
    {
      { "a/b/c/d", SRT_RESOLVE_FLAGS_READABLE|SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY },
      { "a/b/c/d" }
    },
    { { "a/b/c/file", SRT_RESOLVE_FLAGS_READABLE }, { "a/b/c/file" } },
    { { "a/b/c/file/" }, { NULL, G_IO_ERROR_NOT_DIRECTORY }},
    {
      { "a/b/c/file", SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY },
      { NULL, G_IO_ERROR_NOT_DIRECTORY }
    },
    {
      { "a/b/c/file", SRT_RESOLVE_FLAGS_MKDIR_P },
      { NULL, G_IO_ERROR_NOT_DIRECTORY }
    },
    {
      { "a/b/c/file/", SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY },
      { NULL, G_IO_ERROR_NOT_DIRECTORY }
    },
    {
      { "a/b/c/file/", SRT_RESOLVE_FLAGS_READABLE },
      { NULL, G_IO_ERROR_NOT_DIRECTORY }
    },
    {
      { "a/b/c/file", SRT_RESOLVE_FLAGS_READABLE|SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY },
      { NULL, G_IO_ERROR_NOT_DIRECTORY }
    },
    { { "a/b///////.////./././///././c/d" }, { "a/b/c/d" } },
    { { "/a/b///////.////././../b2////././c2/d2" }, { "a/b2/c2/d2" } },
    { { "a/b/c/d/e/f" }, { NULL, G_IO_ERROR_NOT_FOUND } },
    { { "a/b/c/d/e/f/", SRT_RESOLVE_FLAGS_MKDIR_P }, { "a/b/c/d/e/f" } },
    { { "a/b/c/d/e/f", SRT_RESOLVE_FLAGS_MKDIR_P }, { "a/b/c/d/e/f" } },
    { { "a/b/c/d/e/f/" }, { "a/b/c/d/e/f" } },
    { { "a/b/c/d/e/f", SRT_RESOLVE_FLAGS_MKDIR_P }, { "a/b/c/d/e/f" } },
    { { "a3/b3/c3" }, { NULL, G_IO_ERROR_NOT_FOUND } },
    { { "a3/b3/c3", SRT_RESOLVE_FLAGS_MKDIR_P }, { "a3/b3/c3" } },
    { { "a/b/symlink_to_c" }, { "a/b/c" } },
    { { "a/b/symlink_to_c/d" }, { "a/b/c/d" } },
    {
      { "a/b/symlink_to_c/d", SRT_RESOLVE_FLAGS_KEEP_FINAL_SYMLINK },
      { "a/b/c/d" }
    },
    {
      { "a/b/symlink_to_c/d", SRT_RESOLVE_FLAGS_REJECT_SYMLINKS },
      { NULL, G_IO_ERROR_TOO_MANY_LINKS }
    },
    { { "a/b/symlink_to_b2" }, { "a/b2" } },
    { { "a/b/symlink_to_c2" }, { "a/b2/c2" } },
    { { "a/b/abs_symlink_to_run" }, { NULL, G_IO_ERROR_NOT_FOUND } },
    {
      { "a/b/symlink_to_itself", SRT_RESOLVE_FLAGS_KEEP_FINAL_SYMLINK },
      { "a/b/symlink_to_itself" },
    },
    {
      { "a/b/symlink_to_itself",
        SRT_RESOLVE_FLAGS_KEEP_FINAL_SYMLINK|SRT_RESOLVE_FLAGS_READABLE },
      { NULL, G_IO_ERROR_TOO_MANY_LINKS },
    },
    {
      { "a/b/abs_symlink_to_run", SRT_RESOLVE_FLAGS_KEEP_FINAL_SYMLINK },
      { "a/b/abs_symlink_to_run" }
    },
    { { "run" }, { NULL, G_IO_ERROR_NOT_FOUND } },    /* Wasn't created yet */
    { { "a/b/abs_symlink_to_run", SRT_RESOLVE_FLAGS_MKDIR_P }, { "run" } },
    { { "a/b/abs_symlink_to_run/host" }, { NULL, G_IO_ERROR_NOT_FOUND } },
    { { "a/b/abs_symlink_to_run/host", SRT_RESOLVE_FLAGS_MKDIR_P }, { "run/host" } },
    { { "a/b/long_symlink_to_dev" }, { NULL, G_IO_ERROR_NOT_FOUND } },
    { { "a/b/long_symlink_to_dev/shm" }, { NULL, G_IO_ERROR_NOT_FOUND } },
    { { "a/b/long_symlink_to_dev/shm", SRT_RESOLVE_FLAGS_MKDIR_P }, { "dev/shm" } },
    { { "a/b/../b2/c2/../c3", SRT_RESOLVE_FLAGS_MKDIR_P }, { "a/b2/c3" } },
    { { "x" }, { NULL, G_IO_ERROR_NOT_FOUND } },
    { { "x", SRT_RESOLVE_FLAGS_KEEP_FINAL_SYMLINK }, { "x" } },
    /* This is a bit odd: unlike mkdir -p, we create targets for dangling
     * symlinks. It's easier to do this than not, and for pressure-vessel's
     * use-case it probably even makes more sense than not. */
    { { "x/y" }, { NULL, G_IO_ERROR_NOT_FOUND } },
    { { "x/y", SRT_RESOLVE_FLAGS_MKDIR_P }, { "create_me/y" } },
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
      const char *it = prepare_files[i];

      glnx_file_replace_contents_at (tmpdir.fd, it,
                                     (guint8 *) "hello", 5,
                                     0, NULL, &error);
      g_assert_no_error (error);
    }

  for (i = 0; i < G_N_ELEMENTS (prepare_symlinks); i++)
    {
      const Symlink *it = &prepare_symlinks[i];

      if (symlinkat (it->target, tmpdir.fd, it->name) != 0)
        g_error ("symlinkat %s: %s", it->name, g_strerror (errno));
    }

  for (i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      const ResolveTest *it = &tests[i];
      glnx_autofd int fd = -1;
      g_autofree gchar *path = NULL;
      gchar **out_path;
      g_autoptr(GString) description = g_string_new ("");
      TestsOpenFdSet old_fds;

      old_fds = tests_check_fd_leaks_enter ();

      if (it->call.flags & SRT_RESOLVE_FLAGS_MKDIR_P)
        g_string_append (description, " (creating directories)");

      if (it->call.flags & SRT_RESOLVE_FLAGS_KEEP_FINAL_SYMLINK)
        g_string_append (description, " (not following final symlink)");

      if (it->call.flags & SRT_RESOLVE_FLAGS_REJECT_SYMLINKS)
        g_string_append (description, " (not following any symlink)");

      if (it->call.flags & SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY)
        g_string_append (description, " (must be a directory)");

      if (it->call.flags & SRT_RESOLVE_FLAGS_READABLE)
        g_string_append (description, " (open for reading)");

      g_test_message ("%" G_GSIZE_FORMAT ": Resolving %s%s",
                      i, it->call.path, description->str);

      if (it->call.test_flags & RESOLVE_CALL_FLAGS_IGNORE_PATH)
        out_path = NULL;
      else
        out_path = &path;

      fd = _srt_resolve_in_sysroot (tmpdir.fd, it->call.path,
                                    it->call.flags, out_path, &error);

      if (it->expect.path != NULL)
        {
          g_assert_no_error (error);
          g_assert_cmpint (fd, >=, 0);

          if (out_path != NULL)
            g_assert_cmpstr (*out_path, ==, it->expect.path);

          g_assert_true (fd_same_as_rel_path_nofollow (fd, tmpdir.fd,
                                                       it->expect.path));
        }
      else
        {
          g_assert_error (error, G_IO_ERROR, it->expect.code);
          g_test_message ("Got error as expected: %s", error->message);
          g_assert_cmpint (fd, ==, -1);

          if (out_path != NULL)
            g_assert_cmpstr (*out_path, ==, NULL);

          g_clear_error (&error);
        }

      glnx_close_fd (&fd);
      tests_check_fd_leaks_leave (old_fds);
    }
}

int
main (int argc,
      char **argv)
{
  _srt_setenv_disable_gio_modules ();

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/resolve-in-sysroot", Fixture, NULL,
              setup, test_resolve_in_sysroot, teardown);

  return g_test_run ();
}
