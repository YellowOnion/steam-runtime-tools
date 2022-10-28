/*
 * Copyright © 2020-2022 Collabora Ltd.
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
#include "steam-runtime-tools/utils-internal.h"
#include "libglnx.h"

#include "environ.h"
#include "flatpak-bwrap-private.h"
#include "flatpak-exports-private.h"
#include "flatpak-utils-base-private.h"
#include "graphics-provider.h"

/**
 * PvRuntimeFlags:
 * @PV_RUNTIME_FLAGS_SINGLE_THREAD: Run in a single thread, for easier
 *  debugging
 * @PV_RUNTIME_FLAGS_GENERATE_LOCALES: Generate missing locales
 * @PV_RUNTIME_FLAGS_GC_RUNTIMES: Garbage-collect old temporary runtimes
 * @PV_RUNTIME_FLAGS_VERBOSE: Be more verbose
 * @PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS: Include host Vulkan layers
 * @PV_RUNTIME_FLAGS_COPY_RUNTIME: Copy the runtime and modify the copy
 * @PV_RUNTIME_FLAGS_UNPACK_ARCHIVE: Source is an archive, not a deployment
 * @PV_RUNTIME_FLAGS_FLATPAK_SUBSANDBOX: The runtime will be used in a
 *  Flatpak subsandbox
 * @PV_RUNTIME_FLAGS_INTERPRETER_ROOT: The runtime is being set up as a
 *  root filesystem overlay for an interpreter like FEX-Emu
 * @PV_RUNTIME_FLAGS_DETERMINISTIC: Try harder to achieve deterministic
 *  order, even where it shouldn't matter functionally
 * @PV_RUNTIME_FLAGS_NONE: None of the above
 *
 * Flags affecting how we set up the runtime.
 */
typedef enum
{
  PV_RUNTIME_FLAGS_SINGLE_THREAD = (1 << 0),
  PV_RUNTIME_FLAGS_GENERATE_LOCALES = (1 << 1),
  PV_RUNTIME_FLAGS_GC_RUNTIMES = (1 << 2),
  PV_RUNTIME_FLAGS_VERBOSE = (1 << 3),
  PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS = (1 << 4),
  PV_RUNTIME_FLAGS_COPY_RUNTIME = (1 << 5),
  PV_RUNTIME_FLAGS_UNPACK_ARCHIVE = (1 << 6),
  PV_RUNTIME_FLAGS_FLATPAK_SUBSANDBOX = (1 << 7),
  PV_RUNTIME_FLAGS_INTERPRETER_ROOT = (1 << 8),
  PV_RUNTIME_FLAGS_DETERMINISTIC = (1 << 9),
  PV_RUNTIME_FLAGS_NONE = 0
} PvRuntimeFlags;

/* If we're in an emulator like FEX-Emu, we need to use the host
 * OS's /usr as our real root directory, and set the runtime up
 * in a different directory. We use /run/p-v/interpreter-root
 * for the latter. */
#define PV_RUNTIME_PATH_INTERPRETER_ROOT "/run/pressure-vessel/interpreter-root"

#define PV_RUNTIME_FLAGS_MASK \
  (PV_RUNTIME_FLAGS_SINGLE_THREAD \
   | PV_RUNTIME_FLAGS_GENERATE_LOCALES \
   | PV_RUNTIME_FLAGS_GC_RUNTIMES \
   | PV_RUNTIME_FLAGS_VERBOSE \
   | PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS \
   | PV_RUNTIME_FLAGS_COPY_RUNTIME \
   | PV_RUNTIME_FLAGS_UNPACK_ARCHIVE \
   | PV_RUNTIME_FLAGS_FLATPAK_SUBSANDBOX \
   | PV_RUNTIME_FLAGS_INTERPRETER_ROOT \
   | PV_RUNTIME_FLAGS_DETERMINISTIC \
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

PvRuntime *pv_runtime_new (const char *source,
                           const char *id,
                           const char *variable_dir,
                           const char *bubblewrap,
                           PvGraphicsProvider *provider,
                           PvGraphicsProvider *interpreter_host_provider,
                           const GStrv original_environ,
                           PvRuntimeFlags flags,
                           GError **error);

gboolean pv_runtime_get_adverb (PvRuntime *self,
                                FlatpakBwrap *adverb_args);
gboolean pv_runtime_bind (PvRuntime *self,
                          FlatpakExports *exports,
                          FlatpakBwrap *bwrap,
                          PvEnviron *container_env,
                          GError **error);
const char *pv_runtime_get_modified_usr (PvRuntime *self);
const char *pv_runtime_get_modified_app (PvRuntime *self);
const char *pv_runtime_get_overrides (PvRuntime *self);
void pv_runtime_cleanup (PvRuntime *self);

gboolean pv_runtime_garbage_collect_legacy (const char *variable_dir,
                                            const char *runtime_base,
                                            SrtDirentCompareFunc arbitrary_dirent_order,
                                            GError **error);

gboolean pv_runtime_use_shared_sockets (PvRuntime *self,
                                        FlatpakBwrap *bwrap,
                                        PvEnviron *container_env,
                                        GError **error);

gboolean pv_runtime_has_library (PvRuntime *self,
                                 const char *library);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (PvRuntime, g_object_unref)
