/*
 * Copyright © 2019-2020 Collabora Ltd.
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

#pragma once

#include <glib.h>
#include <glib-object.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "libglnx/libglnx.h"

/**
 * PvBwrapLockFlags:
 * @PV_BWRAP_LOCK_FLAGS_CREATE: If the lock file doesn't exist, create it
 * @PV_BWRAP_LOCK_FLAGS_WAIT: If another process holds an incompatible lock,
 *  wait for it to be released; by default pv_bwrap_lock_new()
 *  raises %G_IO_ERROR_BUSY immediately
 * @PV_BWRAP_LOCK_FLAGS_WRITE: Take a write-lock instead of a read-lock;
 *  by default pv_bwrap_lock_new() takes a read-lock
 * @PV_BWRAP_LOCK_FLAGS_REQUIRE_OFD: Require an open file descriptor lock,
 *  which is not released on fork(). By default pv_bwrap_lock_new() tries
 *  an OFD lock first, then falls back to process-oriented locks if the
 *  kernel is older than Linux 3.15.
 * @PV_BWRAP_LOCK_FLAGS_PROCESS_ORIENTED: Require a process-oriented lock,
 *  which is released on fork(). By default pv_bwrap_lock_new() uses
 *  an OFD lock if available.
 * @PV_BWRAP_LOCK_FLAGS_NONE: None of the above
 *
 * Flags affecting how we take a lock on a runtime directory.
 */
typedef enum
{
  PV_BWRAP_LOCK_FLAGS_CREATE = (1 << 0),
  PV_BWRAP_LOCK_FLAGS_WAIT = (1 << 1),
  PV_BWRAP_LOCK_FLAGS_WRITE = (1 << 2),
  PV_BWRAP_LOCK_FLAGS_REQUIRE_OFD = (1 << 3),
  PV_BWRAP_LOCK_FLAGS_PROCESS_ORIENTED = (1 << 4),
  PV_BWRAP_LOCK_FLAGS_NONE = 0
} PvBwrapLockFlags;

typedef struct _PvBwrapLock PvBwrapLock;

PvBwrapLock *pv_bwrap_lock_new (int at_fd,
                                const gchar *path,
                                PvBwrapLockFlags flags,
                                GError **error);
PvBwrapLock *pv_bwrap_lock_new_take (int fd,
                                     gboolean is_ofd);
void pv_bwrap_lock_free (PvBwrapLock *self);
int pv_bwrap_lock_steal_fd (PvBwrapLock *self);
gboolean pv_bwrap_lock_is_ofd (PvBwrapLock *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PvBwrapLock, pv_bwrap_lock_free)
