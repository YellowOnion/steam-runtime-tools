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

#include <glib.h>
#include <glib/gstdio.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "libglnx/libglnx.h"

#include "bwrap-lock.h"
#include "tests/test-utils.h"
#include "utils.h"

typedef struct
{
  TestsOpenFdSet old_fds;
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

  f->old_fds = tests_check_fd_leaks_enter ();
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  tests_check_fd_leaks_leave (f->old_fds);
}

static void
test_locks (Fixture *f,
            gconstpointer context)
{
  g_autoptr(PvBwrapLock) read_lock1 = NULL;
  g_autoptr(PvBwrapLock) read_lock2 = NULL;
  g_autoptr(PvBwrapLock) write_lock1 = NULL;
  g_autoptr(PvBwrapLock) write_lock2 = NULL;
  g_autoptr(GError) error = NULL;
  g_auto(GLnxTmpDir) tmpdir = { FALSE };
  g_autofree gchar *lock = NULL;
  glnx_autofd int fd = -1;
  gboolean is_ofd;

  glnx_mkdtemp ("test-XXXXXX", 0700, &tmpdir, &error);
  g_assert_no_error (error);
  lock = g_build_filename (tmpdir.path, "lockfile", NULL);

  /* Take a shared (read) lock */
  read_lock1 = pv_bwrap_lock_new (AT_FDCWD, lock, PV_BWRAP_LOCK_FLAGS_CREATE,
                                  &error);
  g_assert_no_error (error);
  g_assert_nonnull (read_lock1);

  /* We cannot take an exclusive (write) lock at the same time */
  write_lock1 = pv_bwrap_lock_new (AT_FDCWD, lock, PV_BWRAP_LOCK_FLAGS_WRITE,
                                   &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
  g_assert_null (write_lock1);
  g_clear_error (&error);

  /* We can steal the fd, and still cannot take an exclusive (write) lock */
  is_ofd = pv_bwrap_lock_is_ofd (read_lock1);
  fd = pv_bwrap_lock_steal_fd (read_lock1);
  g_assert_cmpint (fd, >=, 0);
  write_lock1 = pv_bwrap_lock_new (AT_FDCWD, lock, PV_BWRAP_LOCK_FLAGS_WRITE,
                                   &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
  g_assert_null (write_lock1);
  g_clear_error (&error);
  /* We cannot steal it again */
  g_assert_cmpint (pv_bwrap_lock_steal_fd (read_lock1), ==, -1);

  /* The lock is held even after we free the original lock abstraction */
  g_clear_pointer (&read_lock1, pv_bwrap_lock_free);
  write_lock1 = pv_bwrap_lock_new (AT_FDCWD, lock, PV_BWRAP_LOCK_FLAGS_WRITE,
                                   &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
  g_assert_null (write_lock1);
  g_clear_error (&error);

  /* We can make a new lock from an existing one */
  read_lock1 = pv_bwrap_lock_new_take (glnx_steal_fd (&fd), is_ofd);
  g_assert_nonnull (read_lock1);
  write_lock1 = pv_bwrap_lock_new (AT_FDCWD, lock, PV_BWRAP_LOCK_FLAGS_WRITE,
                                   &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
  g_assert_null (write_lock1);
  g_clear_error (&error);

  /* We can take a second read lock at the same time */
  read_lock2 = pv_bwrap_lock_new (AT_FDCWD, lock, PV_BWRAP_LOCK_FLAGS_CREATE,
                                  &error);
  g_assert_no_error (error);
  g_assert_nonnull (read_lock2);

  /* Releasing one read lock is not enough */
  g_clear_pointer (&read_lock1, pv_bwrap_lock_free);
  write_lock1 = pv_bwrap_lock_new (AT_FDCWD, lock, PV_BWRAP_LOCK_FLAGS_WRITE,
                                   &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
  g_assert_null (write_lock1);
  g_clear_error (&error);

  /* Releasing both read locks is enough to allow a write lock. This
   * incidentally also tests the normalization of -1 to AT_FDCWD. */
  g_clear_pointer (&read_lock2, pv_bwrap_lock_free);
  write_lock1 = pv_bwrap_lock_new (-1, lock, PV_BWRAP_LOCK_FLAGS_WRITE, &error);
  g_assert_no_error (error);
  g_assert_nonnull (write_lock1);

  /* We cannot take read or write locks while this lock is held.
   * The second part here also exercises a non-trivial at_fd. */
  write_lock2 = pv_bwrap_lock_new (-1, lock, PV_BWRAP_LOCK_FLAGS_WRITE, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
  g_assert_null (write_lock2);
  g_clear_error (&error);
  read_lock1 = pv_bwrap_lock_new (tmpdir.fd, "lockfile",
                                  PV_BWRAP_LOCK_FLAGS_NONE, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_BUSY);
  g_assert_null (read_lock1);
  g_clear_error (&error);
}

int
main (int argc,
      char **argv)
{
  pv_avoid_gvfs ();

  g_test_init (&argc, &argv, NULL);
  g_test_add ("/locks", Fixture, NULL,
              setup, test_locks, teardown);

  return g_test_run ();
}
