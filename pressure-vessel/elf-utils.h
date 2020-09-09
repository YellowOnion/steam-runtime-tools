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

#include <gelf.h>
#include <glib.h>
#include <libelf.h>

#include "libglnx/libglnx.h"

#include "glib-backports.h"

G_DEFINE_AUTOPTR_CLEANUP_FUNC (Elf, elf_end);

Elf *pv_elf_open_fd (int fd,
                     GError **error);

gchar *pv_elf_get_soname (Elf *elf,
                          GError **error);
