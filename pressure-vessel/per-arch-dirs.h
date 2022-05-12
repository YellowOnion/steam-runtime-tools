/*
 * Copyright © 2019-2022 Collabora Ltd.
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

#include "steam-runtime-tools/glib-backports-internal.h"
#include "libglnx.h"

#include "supported-architectures.h"

/*
 * PvPerArchDirs:
 *
 * A set of directories that are created on a one-per-architecture basis,
 * but can be referred to by a single path that uses special libdl tokens.
 */
typedef struct
{
  gchar *root_path;
  gchar *libdl_token_path;
  /* Same order as pv_multiarch_details */
  gchar *abi_paths[PV_N_SUPPORTED_ARCHITECTURES];
} PvPerArchDirs;

PvPerArchDirs *pv_per_arch_dirs_new (GError **error);
void pv_per_arch_dirs_free (PvPerArchDirs *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PvPerArchDirs, pv_per_arch_dirs_free)
