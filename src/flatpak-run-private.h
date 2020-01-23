/*
 * Cut-down version of common/flatpak-run-private.h from Flatpak
 *
 * Copyright © 2017-2019 Collabora Ltd.
 * Copyright © 2014-2019 Red Hat, Inc
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

void flatpak_run_add_x11_args (FlatpakBwrap *bwrap,
                               gboolean      allowed);
gboolean flatpak_run_add_wayland_args (FlatpakBwrap *bwrap);
void flatpak_run_add_pulseaudio_args (FlatpakBwrap *bwrap);
gboolean flatpak_run_add_system_dbus_args (FlatpakBwrap *app_bwrap);
gboolean flatpak_run_add_session_dbus_args (FlatpakBwrap *app_bwrap);
