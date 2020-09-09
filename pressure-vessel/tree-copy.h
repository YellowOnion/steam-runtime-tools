/*
 * Contains code taken from Flatpak.
 *
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2020 Collabora Ltd.
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
 * PvCopyFlags:
 * @PV_COPY_FLAGS_USRMERGE: Transform the copied tree by merging
 *  /bin, /sbin, /lib* into /usr, and replacing them with symbolic
 *  links /bin -> usr/bin and so on.
 * @PV_RESOLVE_FLAGS_NONE: No special behaviour.
 *
 * Flags affecting how pv_cheap_tree_copy() behaves.
 */
typedef enum
{
  PV_COPY_FLAGS_USRMERGE = (1 << 0),
  PV_COPY_FLAGS_NONE = 0
} PvCopyFlags;

gboolean pv_cheap_tree_copy (const char *source_root,
                             const char *dest_root,
                             PvCopyFlags flags,
                             GError **error);
