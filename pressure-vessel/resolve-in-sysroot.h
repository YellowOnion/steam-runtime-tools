/*
 * Copyright © 2020 Collabora Ltd.
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

/*
 * PvResolveFlags:
 * @PV_RESOLVE_FLAGS_MKDIR_P: Create the filename to be resolved and
 *  all of its ancestors as directories. If any already exist, they
 *  must be directories or symlinks to directories.
 * @PV_RESOLVE_FLAGS_KEEP_FINAL_SYMLINK: If the last component of
 *  the path is a symlink, return a fd pointing to the symlink itself.
 * @PV_RESOLVE_FLAGS_REJECT_SYMLINKS: If any component of
 *  the path is a symlink, fail with %G_IO_ERROR_TOO_MANY_LINKS.
 * @PV_RESOLVE_FLAGS_READABLE: Open the last component of the path
 *  for reading, instead of just as `O_PATH`.
 * @PV_RESOLVE_FLAGS_NONE: No special behaviour.
 *
 * Flags affecting how pv_resolve_in_sysroot() behaves.
 */
typedef enum
{
  PV_RESOLVE_FLAGS_MKDIR_P = (1 << 0),
  PV_RESOLVE_FLAGS_KEEP_FINAL_SYMLINK = (1 << 1),
  PV_RESOLVE_FLAGS_REJECT_SYMLINKS = (1 << 2),
  PV_RESOLVE_FLAGS_READABLE = (1 << 3),
  PV_RESOLVE_FLAGS_NONE = 0
} PvResolveFlags;

int pv_resolve_in_sysroot (int sysroot,
                           const char *descendant,
                           PvResolveFlags flags,
                           gchar **real_path_out,
                           GError **error) G_GNUC_WARN_UNUSED_RESULT;
