/*
 * libudev-based input device monitor, loosely based on SDL code.
 *
 * Copyright © 1997-2020 Sam Lantinga <slouken@libsdl.org>
 * Copyright © 2020 Collabora Ltd.
 *
 * SPDX-License-Identifier: Zlib
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 */

#pragma once

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/input-device.h"

typedef struct _SrtUdevInputDevice SrtUdevInputDevice;
typedef struct _SrtUdevInputDeviceClass SrtUdevInputDeviceClass;

#define SRT_TYPE_UDEV_INPUT_DEVICE (_srt_udev_input_device_get_type ())
#define SRT_UDEV_INPUT_DEVICE(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), SRT_TYPE_UDEV_INPUT_DEVICE, SrtUdevInputDevice))
#define SRT_IS_UDEV_INPUT_DEVICE(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), SRT_TYPE_UDEV_INPUT_DEVICE))
#define SRT_UDEV_INPUT_DEVICE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), SRT_TYPE_UDEV_INPUT_DEVICE, SrtUdevInputDeviceClass))
#define SRT_UDEV_INPUT_DEVICE_CLASS(c) (G_TYPE_CHECK_CLASS_CAST ((c), SRT_TYPE_UDEV_INPUT_DEVICE, SrtUdevInputDeviceClass))
#define SRT_IS_UDEV_INPUT_DEVICE_CLASS(c) (G_TYPE_CHECK_CLASS_TYPE ((c), SRT_TYPE_UDEV_INPUT_DEVICE))

G_GNUC_INTERNAL
GType _srt_udev_input_device_get_type (void);

#define SRT_TYPE_UDEV_INPUT_DEVICE_MONITOR (_srt_udev_input_device_monitor_get_type ())
#define SRT_UDEV_INPUT_DEVICE_MONITOR(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), SRT_TYPE_UDEV_INPUT_DEVICE_MONITOR, SrtUdevInputDeviceMonitor))
#define SRT_IS_UDEV_INPUT_DEVICE_MONITOR(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), SRT_TYPE_UDEV_INPUT_DEVICE_MONITOR))
#define SRT_UDEV_INPUT_DEVICE_MONITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), SRT_TYPE_UDEV_INPUT_DEVICE_MONITOR, SrtUdevInputDeviceMonitorClass))
#define SRT_UDEV_INPUT_DEVICE_MONITOR_CLASS(c) (G_TYPE_CHECK_CLASS_CAST ((c), SRT_TYPE_UDEV_INPUT_DEVICE_MONITOR, SrtUdevInputDeviceMonitorClass))
#define SRT_IS_UDEV_INPUT_DEVICE_MONITOR_CLASS(c) (G_TYPE_CHECK_CLASS_TYPE ((c), SRT_TYPE_UDEV_INPUT_DEVICE_MONITOR))

typedef struct _SrtUdevInputDeviceMonitor SrtUdevInputDeviceMonitor;
typedef struct _SrtUdevInputDeviceMonitorClass SrtUdevInputDeviceMonitorClass;

G_GNUC_INTERNAL
GType _srt_udev_input_device_monitor_get_type (void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtUdevInputDevice, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtUdevInputDeviceMonitor, g_object_unref)

static inline SrtInputDeviceMonitor *
srt_udev_input_device_monitor_new (SrtInputDeviceMonitorFlags flags,
                                   GError **error)
{
  return g_initable_new (SRT_TYPE_UDEV_INPUT_DEVICE_MONITOR,
                         NULL /* cancellable */, error,
                         "flags", flags,
                         NULL);
}
