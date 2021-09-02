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
#include "steam-runtime-tools/glib-backports-internal.h"

typedef enum
{
  PV_SHELL_NONE = 0,
  PV_SHELL_AFTER,
  PV_SHELL_FAIL,
  PV_SHELL_INSTEAD
} PvShell;

typedef enum
{
  PV_TERMINAL_NONE = 0,
  PV_TERMINAL_AUTO,
  PV_TERMINAL_TTY,
  PV_TERMINAL_XTERM,
} PvTerminal;

void pv_bwrap_wrap_in_xterm (FlatpakBwrap *wrapped_command,
                             const char *xcursor_path);
void pv_bwrap_wrap_interactive (FlatpakBwrap *wrapped_command,
                                PvShell shell);
gboolean pv_bwrap_wrap_tty (FlatpakBwrap *wrapped_command,
                            GError **error);
