/*
 * Input device internals, with parts based on SDL code.
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

#if !defined(_SRT_IN_SINGLE_HEADER) && !defined(_SRT_COMPILATION)
#error "Do not include directly, use <steam-runtime-tools/steam-runtime-tools.h>"
#endif

#include <gio/gio.h>

#include "steam-runtime-tools/macros.h"

#define SRT_TYPE_INPUT_DEVICE (srt_input_device_get_type ())
#define SRT_INPUT_DEVICE(x) (G_TYPE_CHECK_INSTANCE_CAST ((x), SRT_TYPE_INPUT_DEVICE, SrtInputDevice))
#define SRT_IS_INPUT_DEVICE(x) (G_TYPE_CHECK_INSTANCE_TYPE ((x), SRT_TYPE_INPUT_DEVICE))
#define SRT_INPUT_DEVICE_GET_INTERFACE(x) (G_TYPE_INSTANCE_GET_INTERFACE ((x), SRT_TYPE_INPUT_DEVICE, SrtInputDeviceInterface))

typedef struct _SrtInputDevice SrtInputDevice;
typedef struct _SrtInputDeviceInterface SrtInputDeviceInterface;

_SRT_PUBLIC
GType srt_input_device_get_type (void);

_SRT_PUBLIC
const char *srt_input_device_get_dev_node (SrtInputDevice *device);
_SRT_PUBLIC
const char *srt_input_device_get_sys_path (SrtInputDevice *device);
_SRT_PUBLIC
const char *srt_input_device_get_subsystem (SrtInputDevice *device);

/**
 * SrtInputDeviceMonitorFlags:
 * @SRT_INPUT_DEVICE_MONITOR_FLAGS_ONCE: Enumerate the devices that were
 *  available when monitoring starts, and then stop monitoring.
 * @SRT_INPUT_DEVICE_MONITOR_FLAGS_NONE: No special behaviour.
 *
 * Flags affecting the behaviour of the input device monitor.
 */
typedef enum
{
  SRT_INPUT_DEVICE_MONITOR_FLAGS_ONCE = (1 << 0),
  SRT_INPUT_DEVICE_MONITOR_FLAGS_NONE = 0
} SrtInputDeviceMonitorFlags;

#define SRT_TYPE_INPUT_DEVICE_MONITOR (srt_input_device_monitor_get_type ())
#define SRT_INPUT_DEVICE_MONITOR(x) (G_TYPE_CHECK_INSTANCE_CAST ((x), SRT_TYPE_INPUT_DEVICE_MONITOR, SrtInputDeviceMonitor))
#define SRT_IS_INPUT_DEVICE_MONITOR(x) (G_TYPE_CHECK_INSTANCE_TYPE ((x), SRT_TYPE_INPUT_DEVICE_MONITOR))
#define SRT_INPUT_DEVICE_MONITOR_GET_INTERFACE(x) (G_TYPE_INSTANCE_GET_INTERFACE ((x), SRT_TYPE_INPUT_DEVICE_MONITOR, SrtInputDeviceMonitorInterface))

typedef struct _SrtInputDeviceMonitor SrtInputDeviceMonitor;
typedef struct _SrtInputDeviceMonitorInterface SrtInputDeviceMonitorInterface;

_SRT_PUBLIC
GType srt_input_device_monitor_get_type (void);

_SRT_PUBLIC
SrtInputDeviceMonitor *srt_input_device_monitor_new (SrtInputDeviceMonitorFlags flags);

_SRT_PUBLIC
SrtInputDeviceMonitorFlags srt_input_device_monitor_get_flags (SrtInputDeviceMonitor *monitor);
_SRT_PUBLIC
void srt_input_device_monitor_request_raw_hid (SrtInputDeviceMonitor *monitor);
_SRT_PUBLIC
void srt_input_device_monitor_request_evdev (SrtInputDeviceMonitor *monitor);

_SRT_PUBLIC
gboolean srt_input_device_monitor_is_active (SrtInputDeviceMonitor *monitor);
_SRT_PUBLIC
gboolean srt_input_device_monitor_start (SrtInputDeviceMonitor *monitor,
                                         GError **error);
_SRT_PUBLIC
void srt_input_device_monitor_stop (SrtInputDeviceMonitor *monitor);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtInputDevice, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtInputDeviceMonitor, g_object_unref)
#endif
