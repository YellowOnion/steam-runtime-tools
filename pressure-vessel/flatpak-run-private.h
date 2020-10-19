/*
 * Cut-down version of common/flatpak-run-private.h from Flatpak
 * Last updated: Flatpak 1.8.2
 *
 * Copyright © 2017-2019 Collabora Ltd.
 * Copyright © 2014-2019 Red Hat, Inc
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
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#ifndef __FLATPAK_RUN_H__
#define __FLATPAK_RUN_H__

#include <glib.h>
#include <glib-object.h>

#include "libglnx/libglnx.h"

#include "flatpak-common-types-private.h"
#include "flatpak-context-private.h"
#include "flatpak-bwrap-private.h"
#include "flatpak-utils-private.h"
#include "steam-runtime-tools/glib-backports-internal.h"

void flatpak_run_add_x11_args (FlatpakBwrap *bwrap,
                               GHashTable   *extra_locked_vars_to_unset,
                               gboolean      allowed);
gboolean flatpak_run_add_wayland_args (FlatpakBwrap *bwrap);
void flatpak_run_add_pulseaudio_args (FlatpakBwrap *bwrap,
                                      GHashTable   *extra_locked_vars_to_unset);
gboolean flatpak_run_add_system_dbus_args (FlatpakBwrap *app_bwrap);
gboolean flatpak_run_add_session_dbus_args (FlatpakBwrap *app_bwrap);
void     flatpak_run_apply_env_appid (FlatpakBwrap *bwrap,
                                      GFile        *app_dir);
GFile *flatpak_get_data_dir (const char *app_id);

extern const char * const *flatpak_abs_usrmerged_dirs;
#endif /* __FLATPAK_RUN_H__ */
