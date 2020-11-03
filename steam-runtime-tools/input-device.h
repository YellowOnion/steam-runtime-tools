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

/**
 * SrtInputDeviceTypeFlags:
 * @SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK: A joystick, gamepad, steering wheel
 *  or other game controller (udev ID_INPUT_JOYSTICK)
 * @SRT_INPUT_DEVICE_TYPE_FLAGS_ACCELEROMETER: An accelerometer, either motion
 *  controls in a game controller such as Playstation 3 "sixaxis" controllers
 *  or in the computer itself (udev ID_INPUT_ACCELEROMETER).
 *  Note that unlike SDL, SrtInputDeviceMonitor always considers
 *  accelerometers to be their own device type distinct from joysticks;
 *  there is no equivalent of SDL_HINT_ACCELEROMETER_AS_JOYSTICK.
 * @SRT_INPUT_DEVICE_TYPE_FLAGS_KEYBOARD: Keyboards with a somewhat full set
 *  of keys (udev ID_INPUT_KEYBOARD)
 * @SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS: Any device with keyboard keys, however
 *  incomplete (udev ID_INPUT_KEY)
 * @SRT_INPUT_DEVICE_TYPE_FLAGS_MOUSE: A mouse or mouse-like pointer
 *  controller (udev ID_INPUT_MOUSE)
 * @SRT_INPUT_DEVICE_TYPE_FLAGS_TOUCHPAD: A touchpad, perhaps built in to a
 *  game controller like the Playstation 4 controller, or perhaps used
 *  as a mouse replacement as in most laptops (udev ID_INPUT_TOUCHPAD)
 * @SRT_INPUT_DEVICE_TYPE_FLAGS_TOUCHSCREEN: A touchscreen (udev ID_INPUT_TOUCHSCREEN)
 * @SRT_INPUT_DEVICE_TYPE_FLAGS_TABLET: A graphics tablet (udev ID_INPUT_TABLET)
 * @SRT_INPUT_DEVICE_TYPE_FLAGS_TABLET_PAD: A graphics tablet with buttons
 *  (udev ID_INPUT_TABLET_PAD)
 * @SRT_INPUT_DEVICE_TYPE_FLAGS_POINTING_STICK: A mouse-like control similar to
 *  the IBM/Lenovo Trackpoint (udev ID_INPUT_POINTINGSTICK)
 * @SRT_INPUT_DEVICE_TYPE_FLAGS_SWITCH: A switch, such as a laptop lid being
 *  opened (udev ID_INPUT_SWITCH)
 *
 * Flags describing a type of input device. An input device can fall into
 * one or more of these categories.
 */
typedef enum
{
  SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK = (1 << 0),
  SRT_INPUT_DEVICE_TYPE_FLAGS_ACCELEROMETER = (1 << 1),
  SRT_INPUT_DEVICE_TYPE_FLAGS_KEYBOARD = (1 << 2),
  SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS = (1 << 3),
  SRT_INPUT_DEVICE_TYPE_FLAGS_MOUSE = (1 << 4),
  SRT_INPUT_DEVICE_TYPE_FLAGS_TOUCHPAD = (1 << 5),
  SRT_INPUT_DEVICE_TYPE_FLAGS_TOUCHSCREEN = (1 << 6),
  SRT_INPUT_DEVICE_TYPE_FLAGS_TABLET = (1 << 7),
  SRT_INPUT_DEVICE_TYPE_FLAGS_TABLET_PAD = (1 << 8),
  SRT_INPUT_DEVICE_TYPE_FLAGS_POINTING_STICK = (1 << 9),
  SRT_INPUT_DEVICE_TYPE_FLAGS_SWITCH = (1 << 10),
  SRT_INPUT_DEVICE_TYPE_FLAGS_NONE = 0
} SrtInputDeviceTypeFlags;

/**
 * SrtInputDeviceInterfaceFlags:
 * @SRT_INPUT_DEVICE_INTERFACE_FLAGS_EVENT: evdev event device nodes,
 *  typically /dev/input/event*
 * @SRT_INPUT_DEVICE_INTERFACE_FLAGS_RAW_HID: Raw USB or Bluetooth HID
 *  device nodes, typically /dev/hidraw*
 * @SRT_INPUT_DEVICE_INTERFACE_FLAGS_READABLE: Only report device nodes that appear to be
 *  openable in read-only mode
 * @SRT_INPUT_DEVICE_INTERFACE_FLAGS_READ_WRITE: Only report device nodes that appear to be
 *  openable in read/write mode
 *
 * Flags describing the interface offered by an input device.
 */
typedef enum
{
  SRT_INPUT_DEVICE_INTERFACE_FLAGS_EVENT = (1 << 0),
  SRT_INPUT_DEVICE_INTERFACE_FLAGS_RAW_HID = (1 << 1),
  SRT_INPUT_DEVICE_INTERFACE_FLAGS_READABLE = (1 << 2),
  SRT_INPUT_DEVICE_INTERFACE_FLAGS_READ_WRITE = (1 << 3),
  SRT_INPUT_DEVICE_INTERFACE_FLAGS_NONE = 0
} SrtInputDeviceInterfaceFlags;

#define SRT_TYPE_INPUT_DEVICE (srt_input_device_get_type ())
#define SRT_INPUT_DEVICE(x) (G_TYPE_CHECK_INSTANCE_CAST ((x), SRT_TYPE_INPUT_DEVICE, SrtInputDevice))
#define SRT_IS_INPUT_DEVICE(x) (G_TYPE_CHECK_INSTANCE_TYPE ((x), SRT_TYPE_INPUT_DEVICE))
#define SRT_INPUT_DEVICE_GET_INTERFACE(x) (G_TYPE_INSTANCE_GET_INTERFACE ((x), SRT_TYPE_INPUT_DEVICE, SrtInputDeviceInterface))

typedef struct _SrtInputDevice SrtInputDevice;
typedef struct _SrtInputDeviceInterface SrtInputDeviceInterface;

_SRT_PUBLIC
GType srt_input_device_get_type (void);

_SRT_PUBLIC
SrtInputDeviceInterfaceFlags srt_input_device_get_interface_flags (SrtInputDevice *device);
_SRT_PUBLIC
SrtInputDeviceTypeFlags srt_input_device_get_type_flags (SrtInputDevice *device);
_SRT_PUBLIC
const char *srt_input_device_get_dev_node (SrtInputDevice *device);
_SRT_PUBLIC
const char *srt_input_device_get_sys_path (SrtInputDevice *device);
_SRT_PUBLIC
const char *srt_input_device_get_subsystem (SrtInputDevice *device);
_SRT_PUBLIC
gchar **srt_input_device_dup_udev_properties (SrtInputDevice *device);
_SRT_PUBLIC
gchar *srt_input_device_dup_uevent (SrtInputDevice *device);
_SRT_PUBLIC
gboolean srt_input_device_get_identity (SrtInputDevice *device,
                                        unsigned int *bus_type,
                                        unsigned int *vendor_id,
                                        unsigned int *product_id,
                                        unsigned int *version);
_SRT_PUBLIC
size_t srt_input_device_get_event_capabilities (SrtInputDevice *device,
                                                unsigned int type,
                                                unsigned long *storage,
                                                size_t n_longs);
_SRT_PUBLIC
size_t srt_input_device_get_event_types (SrtInputDevice *device,
                                         unsigned long *storage,
                                         size_t n_longs);
_SRT_PUBLIC
gboolean srt_input_device_has_event_type (SrtInputDevice *device,
                                          unsigned int type);
_SRT_PUBLIC
gboolean srt_input_device_has_event_capability (SrtInputDevice *device,
                                                unsigned int type,
                                                unsigned int code);
_SRT_PUBLIC
gboolean srt_input_device_has_input_property (SrtInputDevice *device,
                                              unsigned int input_prop);
_SRT_PUBLIC
size_t srt_input_device_get_input_properties (SrtInputDevice *device,
                                              unsigned long *storage,
                                              size_t n_longs);
_SRT_PUBLIC
SrtInputDeviceTypeFlags srt_input_device_guess_type_flags_from_event_capabilities (SrtInputDevice *device);

_SRT_PUBLIC
const char *srt_input_device_get_hid_sys_path (SrtInputDevice *device);
_SRT_PUBLIC
gchar *srt_input_device_dup_hid_uevent (SrtInputDevice *device);
_SRT_PUBLIC
gboolean srt_input_device_get_hid_identity (SrtInputDevice *device,
                                            unsigned int *bus_type,
                                            unsigned int *vendor_id,
                                            unsigned int *product_id,
                                            const char **name,
                                            const char **phys,
                                            const char **uniq);

_SRT_PUBLIC
const char *srt_input_device_get_input_sys_path (SrtInputDevice *device);
_SRT_PUBLIC
gchar *srt_input_device_dup_input_uevent (SrtInputDevice *device);
_SRT_PUBLIC
gboolean srt_input_device_get_input_identity (SrtInputDevice *device,
                                              unsigned int *bus_type,
                                              unsigned int *vendor_id,
                                              unsigned int *product_id,
                                              unsigned int *version,
                                              const char **name,
                                              const char **phys,
                                              const char **uniq);

_SRT_PUBLIC
const char *srt_input_device_get_usb_device_sys_path (SrtInputDevice *device);
_SRT_PUBLIC
gchar *srt_input_device_dup_usb_device_uevent (SrtInputDevice *device);
_SRT_PUBLIC
gboolean srt_input_device_get_usb_device_identity (SrtInputDevice *device,
                                                   unsigned int *vendor_id,
                                                   unsigned int *product_id,
                                                   unsigned int *device_version,
                                                   const char **manufacturer,
                                                   const char **product,
                                                   const char **serial);

/**
 * SrtInputDeviceMonitorFlags:
 * @SRT_INPUT_DEVICE_MONITOR_FLAGS_ONCE: Enumerate the devices that were
 *  available when monitoring starts, and then stop monitoring.
 * @SRT_INPUT_DEVICE_MONITOR_FLAGS_UDEV: Prefer to get devices from udev.
 * @SRT_INPUT_DEVICE_MONITOR_FLAGS_DIRECT: Prefer to get devices by
 *  monitoring /dev, /sys directly.
 * @SRT_INPUT_DEVICE_MONITOR_FLAGS_NONE: No special behaviour.
 *
 * Flags affecting the behaviour of the input device monitor.
 */
typedef enum
{
  SRT_INPUT_DEVICE_MONITOR_FLAGS_ONCE = (1 << 0),
  SRT_INPUT_DEVICE_MONITOR_FLAGS_UDEV = (1 << 1),
  SRT_INPUT_DEVICE_MONITOR_FLAGS_DIRECT = (1 << 2),
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
