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

#include "steam-runtime-tools/glib-backports-internal.h"
#include "libglnx/libglnx.h"

#include "flatpak-bwrap-private.h"
#include "flatpak-utils-base-private.h"

/**
 * PvRuntimeFlags:
 * @PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK: Use provider graphics stack
 * @PV_RUNTIME_FLAGS_GENERATE_LOCALES: Generate missing locales
 * @PV_RUNTIME_FLAGS_GC_RUNTIMES: Garbage-collect old temporary runtimes
 * @PV_RUNTIME_FLAGS_VERBOSE: Be more verbose
 * @PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS: Include host Vulkan layers
 * @PV_RUNTIME_FLAGS_NONE: None of the above
 *
 * Flags affecting how we set up the runtime.
 */
typedef enum
{
  PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK = (1 << 0),
  PV_RUNTIME_FLAGS_GENERATE_LOCALES = (1 << 1),
  PV_RUNTIME_FLAGS_GC_RUNTIMES = (1 << 2),
  PV_RUNTIME_FLAGS_VERBOSE = (1 << 3),
  PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS = (1 << 4),
  PV_RUNTIME_FLAGS_NONE = 0
} PvRuntimeFlags;

#define PV_RUNTIME_FLAGS_MASK \
  (PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK \
   | PV_RUNTIME_FLAGS_GENERATE_LOCALES \
   | PV_RUNTIME_FLAGS_GC_RUNTIMES \
   | PV_RUNTIME_FLAGS_VERBOSE \
   | PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS \
   )

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
                           const char *mutable_parent,
                           const char *bubblewrap,
                           const char *tools_dir,
                           const char *provider_in_current_namespace,
                           const char *provider_in_container_namespace,
                           const GStrv original_environ,
                           PvRuntimeFlags flags,
                           GError **error);

gchar *pv_runtime_get_adverb (PvRuntime *self,
                              FlatpakBwrap *adverb_args);
gboolean pv_runtime_bind (PvRuntime *self,
                          FlatpakBwrap *bwrap,
                          GHashTable *extra_locked_vars_to_unset,
                          GHashTable *extra_locked_vars_to_inherit,
                          GError **error);
void pv_runtime_cleanup (PvRuntime *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PvRuntime, g_object_unref)
