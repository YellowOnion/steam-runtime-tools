/*
 * Copyright © 2017-2019 Collabora Ltd.
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

#include "libglnx/libglnx.h"

#include "flatpak-bwrap-private.h"
#include "glib-backports.h"

gboolean pv_bwrap_run_sync (FlatpakBwrap *bwrap,
                            int *exit_status_out,
                            GError **error);
gboolean pv_bwrap_execve (FlatpakBwrap *bwrap,
                          int original_stdout,
                          GError **error);
gboolean pv_bwrap_bind_usr (FlatpakBwrap *bwrap,
                            const char *host_path,
                            const char *mount_point,
                            GError **error);
void pv_bwrap_copy_tree (FlatpakBwrap *bwrap,
                         const char *source,
                         const char *dest);
void pv_bwrap_add_api_filesystems (FlatpakBwrap *bwrap);

static inline gboolean
pv_bwrap_was_finished (FlatpakBwrap *bwrap)
{
  g_return_val_if_fail (bwrap != NULL, FALSE);

  return (bwrap->argv->len >= 1 &&
          bwrap->argv->pdata[bwrap->argv->len - 1] == NULL);
}

FlatpakBwrap *pv_bwrap_copy (FlatpakBwrap *bwrap);
