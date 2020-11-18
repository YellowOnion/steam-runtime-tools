
/*
 * pressure-vessel-launcher — accept IPC requests to create child processes
 *
 * Copyright © 2020 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <sys/socket.h>
#include <sys/un.h>

#include <glib.h>
#include <gio/gio.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "libglnx/libglnx.h"

#include "launcher1.h"

#define LAUNCHER_IFACE "com.steampowered.PressureVessel.Launcher1"
#define LAUNCHER_PATH "/com/steampowered/PressureVessel/Launcher1"

typedef enum
{
  PV_LAUNCH_FLAGS_CLEAR_ENV = (1 << 0),
  PV_LAUNCH_FLAGS_NONE = 0,
  PV_LAUNCH_FLAGS_MASK = (
    PV_LAUNCH_FLAGS_CLEAR_ENV |
    PV_LAUNCH_FLAGS_NONE
  ),
} PvLaunchFlags;

#if !GLIB_CHECK_VERSION (2, 47, 92)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (PvLauncher1Skeleton, g_object_unref)
#endif

/* Format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx */
#define PV_UUID_STRLEN 36

#define PV_MAX_SOCKET_DIRECTORY_LEN 64

/* If ${socket_directory} is no longer than PV_MAX_SOCKET_DIRECTORY_LEN,
 * then struct sockaddr_un.sun_path is long enough to contain
 * "${socket_directory}/${uuid}\0" */
G_STATIC_ASSERT (sizeof (struct sockaddr_un) >=
                 (G_STRUCT_OFFSET (struct sockaddr_un, sun_path) +
                  PV_MAX_SOCKET_DIRECTORY_LEN +
                  PV_UUID_STRLEN +
                  2));

/* Chosen to be similar to env(1) */
enum
{
  LAUNCH_EX_USAGE = 125,
  LAUNCH_EX_FAILED = 125,
  LAUNCH_EX_CANNOT_INVOKE = 126,
  LAUNCH_EX_NOT_FOUND = 127,
  LAUNCH_EX_CANNOT_REPORT = 128
};
