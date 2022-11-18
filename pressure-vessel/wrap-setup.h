/*
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2021 Collabora Ltd.
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

#include "steam-runtime-tools/utils-internal.h"

#include "flatpak-bwrap-private.h"
#include "flatpak-exports-private.h"
#include "runtime.h"
#include "wrap-pipewire.h"

typedef enum
{
  PV_BWRAP_FLAGS_SYSTEM = (1 << 0),
  PV_BWRAP_FLAGS_SETUID = (1 << 1),
  PV_BWRAP_FLAGS_HAS_PERMS = (1 << 2),
  PV_BWRAP_FLAGS_NONE = 0
} PvBwrapFlags;

gchar *pv_wrap_check_bwrap (const char *tools_dir,
                            gboolean only_prepare,
                            PvBwrapFlags *flags_out);

FlatpakBwrap *pv_wrap_share_sockets (PvEnviron *container_env,
                                     const GStrv original_environ,
                                     gboolean using_a_runtime,
                                     gboolean is_flatpak_env);

void pv_wrap_set_icons_env_vars (PvEnviron *container_env,
                                 const GStrv original_environ);

gboolean pv_wrap_use_host_os (int root_fd,
                              FlatpakExports *exports,
                              FlatpakBwrap *bwrap,
                              SrtDirentCompareFunc arbitrary_dirent_order,
                              GError **error);

gboolean pv_export_root_dirs_like_filesystem_host (int root_fd,
                                                   FlatpakExports *exports,
                                                   FlatpakFilesystemMode mode,
                                                   SrtDirentCompareFunc arbitrary_dirent_order,
                                                   GError **error);

void pv_wrap_move_into_scope (const char *steam_app_id);

/**
 * PvAppendPreloadFlags:
 * @PV_APPEND_PRELOAD_FLAGS_FLATPAK_SUBSANDBOX: The game will be run in
 *  a Flatpak subsandbox
 * @PV_APPEND_PRELOAD_FLAGS_REMOVE_GAME_OVERLAY: Disable the Steam Overlay
 * @PV_APPEND_PRELOAD_FLAGS_NONE: None of the above
 *
 * Flags affecting the behaviour of pv_wrap_append_preload().
 */
typedef enum
{
  PV_APPEND_PRELOAD_FLAGS_FLATPAK_SUBSANDBOX = (1 << 0),
  PV_APPEND_PRELOAD_FLAGS_REMOVE_GAME_OVERLAY = (1 << 1),
  PV_APPEND_PRELOAD_FLAGS_IN_UNIT_TESTS = (1 << 2),
  PV_APPEND_PRELOAD_FLAGS_NONE = 0
} PvAppendPreloadFlags;

void pv_wrap_append_preload (GPtrArray *argv,
                             const char *variable,
                             const char *option,
                             const char *preload,
                             GStrv env,
                             PvAppendPreloadFlags flags,
                             PvRuntime *runtime,
                             FlatpakExports *exports);

gboolean pv_wrap_maybe_load_nvidia_modules (GError **error);

void pv_wrap_detect_virtualization (gchar **interpreter_root_out,
                                    SrtMachineType *host_machine_out);
