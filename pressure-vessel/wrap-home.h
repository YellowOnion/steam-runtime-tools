/*
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2022 Collabora Ltd.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib.h>

#include "environ.h"
#include "flatpak-bwrap-private.h"
#include "flatpak-exports-private.h"

/**
 * PvHomeMode:
 * @PV_HOME_MODE_TRANSIENT: The home directory in the container will be
 *  a tmpfs or otherwise expendable, like `flatpak run --nofilesystem=home`
 * @PV_HOME_MODE_PRIVATE: The home directory in the container will be
 *  a per-app directory, like `flatpak run --persist=.`
 * @PV_HOME_MODE_PRIVATE: The home directory in the container will be
 *  the real home directory, like `flatpak run --filesystem=home`
 */
typedef enum
{
  PV_HOME_MODE_TRANSIENT,
  PV_HOME_MODE_PRIVATE,
  PV_HOME_MODE_SHARED,
} PvHomeMode;

gboolean pv_wrap_use_home (PvHomeMode mode,
                           const char *real_home,
                           const char *private_home,
                           FlatpakExports *exports,
                           FlatpakBwrap *bwrap_home_arguments,
                           PvEnviron *container_env,
                           GError **error);
