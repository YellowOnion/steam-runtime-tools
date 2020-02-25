/*
 * Copyright © 2020 Collabora Ltd.
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
#include <glib/gstdio.h>
#include <glib-object.h>

#include "glib-backports.h"
#include "libglnx/libglnx.h"

#include "flatpak-bwrap-private.h"
#include "flatpak-utils-base-private.h"

typedef struct _PvRuntime PvRuntime;
typedef struct _PvRuntimeClass PvRuntimeClass;

#define PV_TYPE_RUNTIME (pv_runtime_get_type ())
#define PV_RUNTIME(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), PV_TYPE_RUNTIME, PvRuntime))
#define PV_RUNTIME_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), PV_TYPE_RUNTIME, PvRuntimeClass))
#define PV_IS_RUNTIME(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PV_TYPE_RUNTIME))
#define PV_IS_RUNTIME_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), PV_TYPE_RUNTIME))
#define PV_RUNTIME_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), PV_TYPE_RUNTIME, PvRuntimeClass)
GType pv_runtime_get_type (void);

PvRuntime *pv_runtime_new (const char *source_files,
                           const char *bubblewrap,
                           const char *tools_dir,
                           GError **error);

void pv_runtime_append_lock_adverb (PvRuntime *self,
                                    FlatpakBwrap *bwrap);
gboolean pv_runtime_bind (PvRuntime *self,
                          FlatpakBwrap *bwrap,
                          GError **error);
void pv_runtime_cleanup (PvRuntime *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PvRuntime, g_object_unref)
