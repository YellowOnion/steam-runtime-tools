/*
 * Copyright © 2021 Collabora Ltd.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#if !defined(_SRT_IN_SINGLE_HEADER) && !defined(_SRT_COMPILATION)
#error "Do not include directly, use <steam-runtime-tools/steam-runtime-tools.h>"
#endif

#include <glib.h>
#include <glib-object.h>

#include <steam-runtime-tools/macros.h>

typedef struct _SrtContainerInfo SrtContainerInfo;
typedef struct _SrtContainerInfoClass SrtContainerInfoClass;

#define SRT_TYPE_CONTAINER_INFO srt_container_info_get_type ()
#define SRT_CONTAINER_INFO(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_CONTAINER_INFO, SrtContainerInfo))
#define SRT_CONTAINER_INFO_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_CONTAINER_INFO, SrtContainerInfoClass))
#define SRT_IS_CONTAINER_INFO(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_CONTAINER_INFO))
#define SRT_IS_CONTAINER_INFO_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_CONTAINER_INFO))
#define SRT_CONTAINER_INFO_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_CONTAINER_INFO, SrtContainerInfoClass)
_SRT_PUBLIC
GType srt_container_info_get_type (void);

/**
 * SrtContainerType:
 * @SRT_CONTAINER_TYPE_UNKNOWN: Unknown container type
 * @SRT_CONTAINER_TYPE_NONE: No container detected
 * @SRT_CONTAINER_TYPE_FLATPAK: Running in a Flatpak app
 * @SRT_CONTAINER_TYPE_PRESSURE_VESSEL: Running in a Steam Runtime container
 *  using pressure-vessel
 * @SRT_CONTAINER_TYPE_DOCKER: Running in a Docker container
 * @SRT_CONTAINER_TYPE_PODMAN: Running in a Podman container
 *
 * A type of container.
 */
typedef enum
{
  SRT_CONTAINER_TYPE_NONE = 0,
  SRT_CONTAINER_TYPE_FLATPAK,
  SRT_CONTAINER_TYPE_PRESSURE_VESSEL,
  SRT_CONTAINER_TYPE_DOCKER,
  SRT_CONTAINER_TYPE_PODMAN,
  SRT_CONTAINER_TYPE_UNKNOWN = -1
} SrtContainerType;

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtContainerInfo, g_object_unref)
#endif

_SRT_PUBLIC
SrtContainerType srt_container_info_get_container_type (SrtContainerInfo *self);
_SRT_PUBLIC
const gchar *srt_container_info_get_container_host_directory (SrtContainerInfo *self);
