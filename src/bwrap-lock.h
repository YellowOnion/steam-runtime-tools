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

#pragma once

#include <glib.h>
#include <glib-object.h>

#include "glib-backports.h"
#include "libglnx.h"

/**
 * PvBwrapLockFlags:
 * @PV_BWRAP_LOCK_FLAGS_CREATE: If the lock file doesn't exist, create it
 * @PV_BWRAP_LOCK_FLAGS_WAIT: If another process holds an incompatible lock,
 *  wait for it to be released; by default pv_bwrap_lock_new()
 *  raises %G_IO_ERROR_BUSY immediately
 * @PV_BWRAP_LOCK_FLAGS_WRITE: Take a write-lock instead of a read-lock;
 *  by default pv_bwrap_lock_new() takes a read-lock
 * @PV_BWRAP_LOCK_FLAGS_NONE: None of the above
 *
 * Flags affecting how we take a lock on a runtime directory.
 */
typedef enum
{
  PV_BWRAP_LOCK_FLAGS_CREATE = (1 << 0),
  PV_BWRAP_LOCK_FLAGS_WAIT = (1 << 1),
  PV_BWRAP_LOCK_FLAGS_WRITE = (1 << 2),
  PV_BWRAP_LOCK_FLAGS_NONE = 0
} PvBwrapLockFlags;

typedef struct _PvBwrapLock PvBwrapLock;

PvBwrapLock *pv_bwrap_lock_new (const gchar *path,
                                PvBwrapLockFlags flags,
                                GError **error);
PvBwrapLock *pv_bwrap_lock_new_take (int fd);
void pv_bwrap_lock_free (PvBwrapLock *self);
int pv_bwrap_lock_steal_fd (PvBwrapLock *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PvBwrapLock, pv_bwrap_lock_free)
