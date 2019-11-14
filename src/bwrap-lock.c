/*
 * Copyright © 2019 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "subprojects/libglnx/config.h"

#include <gio/gio.h>

#include "bwrap-lock.h"

/**
 * PvBwrapLock:
 *
 * A read/write lock compatible with the locks taken out by
 * `bwrap --lock-file FILENAME` and Flatpak.
 */
struct _PvBwrapLock
{
  int fd;
};

/**
 * pv_bwrap_lock_new:
 * @path: Runtime directory to lock; the actual lock file will be `$path/.ref`
 * @flags: Flags affecting how we lock the directory
 * @error: Used to raise an error on failure
 *
 * Take out a lock on a directory.
 *
 * If %PV_BWRAP_LOCK_FLAGS_WRITE is in @flags, the lock is a write-lock,
 * which can be held by at most one process at a time. This is appropriate
 * when about to modify or delete the runtime. Otherwise it is a read-lock,
 * which excludes writers but does not exclude other readers. This is
 * appropriate when running an app or game using the runtime.
 *
 * If %PV_BWRAP_LOCK_FLAGS_WAIT is not in @flags, raise %G_IO_ERROR_BUSY
 * if the lock cannot be obtained immediately.
 *
 * Returns: (nullable): A lock (release and free with pv_bwrap_lock_free())
 *  or %NULL.
 */
PvBwrapLock *
pv_bwrap_lock_new (const gchar *path,
                   PvBwrapLockFlags flags,
                   GError **error)
{
  glnx_autofd int fd = -1;
  struct flock l = {
    .l_type = F_RDLCK,
    .l_whence = SEEK_SET,
    .l_start = 0,
    .l_len = 0
  };
  const char *type_str = "reading";
  int open_flags = O_CLOEXEC | O_NOCTTY;
  int cmd;

  g_return_val_if_fail (path != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  if (flags & PV_BWRAP_LOCK_FLAGS_CREATE)
    open_flags |= O_RDWR | O_CREAT;
  else if (flags & PV_BWRAP_LOCK_FLAGS_WRITE)
    open_flags |= O_RDWR;
  else
    open_flags |= O_RDONLY;

  fd = TEMP_FAILURE_RETRY (openat (AT_FDCWD, path, open_flags, 0644));

  if (fd < 0)
    {
      glnx_throw_errno_prefix (error, "openat(%s)", path);
      return NULL;
    }

  if (flags & PV_BWRAP_LOCK_FLAGS_WAIT)
    cmd = F_SETLKW;
  else
    cmd = F_SETLK;

  if (flags & PV_BWRAP_LOCK_FLAGS_WRITE)
    {
      l.l_type = F_WRLCK;
      type_str = "writing";
    }

  if (TEMP_FAILURE_RETRY (fcntl (fd, cmd, &l)) < 0)
    {
      int saved_errno = errno;

      if (saved_errno == EACCES || saved_errno == EAGAIN)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_BUSY,
                       "Unable to lock %s for %s: file is busy",
                       path, type_str);
          return NULL;
        }

      glnx_throw_errno_prefix (error, "Unable to lock %s for %s",
                               path, type_str);
      return NULL;
    }

  return pv_bwrap_lock_new_take (glnx_steal_fd (&fd));
}

/**
 * pv_bwrap_lock_new_take:
 * @fd: A file descriptor, already locked
 *
 * Convert a simple file descriptor into a #PvBwrapLock.
 *
 * Returns: (not nullable): A lock (release and free
 *  with pv_bwrap_lock_free())
 */
PvBwrapLock *
pv_bwrap_lock_new_take (int fd)
{
  PvBwrapLock *self = NULL;

  g_return_val_if_fail (fd >= 0, NULL);

  self = g_slice_new0 (PvBwrapLock);
  self->fd = glnx_steal_fd (&fd);
  return self;
}

void
pv_bwrap_lock_free (PvBwrapLock *self)
{
  glnx_autofd int fd = -1;

  g_return_if_fail (self != NULL);

  fd = glnx_steal_fd (&self->fd);
  g_slice_free (PvBwrapLock, self);
  /* fd is closed by glnx_autofd if necessary, and that releases the lock */
}

int
pv_bwrap_lock_steal_fd (PvBwrapLock *self)
{
  g_return_val_if_fail (self != NULL, -1);
  return glnx_steal_fd (&self->fd);
}
