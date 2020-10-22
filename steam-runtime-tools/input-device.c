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

#include "steam-runtime-tools/input-device.h"
#include "steam-runtime-tools/input-device-internal.h"

#include <libglnx.h>

#include "steam-runtime-tools/enums.h"

#include "steam-runtime-tools/direct-input-device-internal.h"

/**
 * SrtInputDevice:
 *
 * An input device.
 *
 * Take additional references with g_object_ref(), release references
 * with g_object_unref().
 */
/**
 * SrtInputDeviceInterface:
 *
 * The interface implemented by each SrtInputDevice.
 */
G_DEFINE_INTERFACE (SrtInputDevice, srt_input_device, G_TYPE_OBJECT)

static const char *
srt_input_device_default_get_null_string (SrtInputDevice *device)
{
  return NULL;
}

/**
 * srt_input_device_get_dev_node:
 * @device: An object implementing #SrtInputDeviceInterface
 *
 * Return the path of the device node in /dev that is implemented by this
 * input device, or %NULL if not known or if the device does not have a
 * corresponding device node.
 *
 * For processes in a container, it is not guaranteed that this path will
 * exist in the container's /dev.
 *
 * The returned string will not be freed as long as @device is referenced,
 * but the device node in /dev might be deleted, or even reused for a different
 * device.
 *
 * Returns: (nullable) (transfer none): A path in /dev that will not be
 *  freed as long as a reference to @device is held, or %NULL
 */
const char *
srt_input_device_get_dev_node (SrtInputDevice *device)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->get_dev_node (device);
}

/**
 * srt_input_device_get_sys_path:
 * @device: An object implementing #SrtInputDeviceInterface
 *
 * Return the path of the device directory in /sys that represents this
 * input device.
 *
 * For processes in a container, it is not guaranteed that this path will
 * exist in the container's /sys.
 *
 * The returned string will not be freed as long as @device is referenced,
 * but the directory in /sys might be deleted, or even reused for a different
 * device.
 *
 * Returns: (nullable) (transfer none): A path in /sys that will not be
 *  freed as long as a reference to @device is held, or %NULL
 */
const char *
srt_input_device_get_sys_path (SrtInputDevice *device)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->get_sys_path (device);
}

/**
 * srt_input_device_get_subsystem:
 * @device: An object implementing #SrtInputDeviceInterface
 *
 * Return the subsystem in which this device exists, typically input
 * or hidraw, or %NULL if not known.
 *
 * Returns: (nullable) (transfer none): A short string that will not be
 *  freed as long as a reference to @device is held, or %NULL
 */
const char *
srt_input_device_get_subsystem (SrtInputDevice *device)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->get_subsystem (device);
}

static void
srt_input_device_default_init (SrtInputDeviceInterface *iface)
{
#define IMPLEMENT2(x, y) iface->x = srt_input_device_default_ ## y
#define IMPLEMENT(x) IMPLEMENT2 (x, x)

  IMPLEMENT2 (get_dev_node, get_null_string);
  IMPLEMENT2 (get_sys_path, get_null_string);
  IMPLEMENT2 (get_subsystem, get_null_string);

#undef IMPLEMENT
#undef IMPLEMENT2
}

/**
 * SrtInputDeviceMonitor:
 *
 * An object to enumerate and monitor input devices.
 *
 * Take additional references with g_object_ref(), release references
 * with g_object_unref().
 *
 * New input devices are signalled by the #SrtInputDeviceMonitor::added signal.
 * An input device being removed is signalled by the
 * #SrtInputDeviceMonitor::removed signal. The end of the initial device
 * enumeration is signalled by the #SrtInputDeviceMonitor::all-for-now
 * signal.
 *
 * All of these signals are emitted in whatever #GMainContext was
 * the thread-default main context at the time the #SrtInputDeviceMonitor
 * was created (in other words, their callbacks are called in the thread
 * that is iterating the chosen #GMainContext). If you are using threads
 * other than the main thread, use g_main_context_push_thread_default()
 * to ensure that an appropriate main context will be used.
 */
/**
 * SrtInputDeviceMonitorInterface:
 *
 * The interface implemented by each SrtInputDeviceMonitor.
 */
G_DEFINE_INTERFACE (SrtInputDeviceMonitor, srt_input_device_monitor, G_TYPE_OBJECT)

static guint monitor_signal_added = 0;
static guint monitor_signal_removed = 0;
static guint monitor_signal_all_for_now = 0;

/**
 * srt_input_device_monitor_new:
 * @flags: Flags affecting the behaviour of the input device monitor.
 *
 * Return an object that can be used to enumerate and monitor input
 * devices.
 *
 * Take additional references with g_object_ref(), release references
 * with g_object_unref().
 *
 * Returns: the input device monitor
 */
SrtInputDeviceMonitor *
srt_input_device_monitor_new (SrtInputDeviceMonitorFlags flags)
{
  /* For now we only have one implementation. */
  return g_object_new (SRT_TYPE_DIRECT_INPUT_DEVICE_MONITOR,
                       "flags", flags,
                       NULL);
}

static void
srt_input_device_monitor_default_do_nothing (SrtInputDeviceMonitor *monitor)
{
}

/**
 * srt_input_device_monitor_get_flags:
 * @device: An object implementing #SrtInputDeviceMonitorInterface
 *
 * Return flags describing the input device monitor.
 *
 * Returns: Flags describing the input device monitor.
 */
SrtInputDeviceMonitorFlags
srt_input_device_monitor_get_flags (SrtInputDeviceMonitor *monitor)
{
  SrtInputDeviceMonitorFlags flags = SRT_INPUT_DEVICE_MONITOR_FLAGS_NONE;
  SrtInputDeviceMonitorInterface *iface = SRT_INPUT_DEVICE_MONITOR_GET_INTERFACE (monitor);

  g_return_val_if_fail (iface != NULL, flags);

  g_object_get (monitor,
                "flags", &flags,
                NULL);
  return flags;
}

/**
 * srt_input_device_monitor_is_active:
 * @monitor: The input device monitor
 *
 * Return %TRUE if srt_input_device_monitor_start() has been called
 * successfully, and srt_input_device_monitor_stop() has not subsequently
 * been called.
 *
 * Returns: %TRUE if active
 */
gboolean
srt_input_device_monitor_is_active (SrtInputDeviceMonitor *monitor)
{
  gboolean ret = FALSE;
  SrtInputDeviceMonitorInterface *iface = SRT_INPUT_DEVICE_MONITOR_GET_INTERFACE (monitor);

  g_return_val_if_fail (iface != NULL, FALSE);

  g_object_get (monitor,
                "is-active", &ret,
                NULL);
  return ret;
}

/**
 * srt_input_device_monitor_request_raw_hid:
 * @monitor: The input device monitor
 *
 * Tell the input device monitor to return all raw HID devices.
 *
 * If neither this method nor srt_input_device_monitor_request_evdev() is
 * called, no devices will be found.
 *
 * This function cannot be called if srt_input_device_monitor_start()
 * or srt_input_device_monitor_stop() have already been called.
 */
void
srt_input_device_monitor_request_raw_hid (SrtInputDeviceMonitor *monitor)
{
  SrtInputDeviceMonitorInterface *iface = SRT_INPUT_DEVICE_MONITOR_GET_INTERFACE (monitor);

  g_return_if_fail (iface != NULL);

  iface->request_raw_hid (monitor);
}

/**
 * srt_input_device_monitor_request_evdev:
 * @monitor: The input device monitor
 *
 * Tell the input device monitor to return all evdev (event) devices.
 *
 * If neither this method nor srt_input_device_monitor_request_raw_hid() is
 * called, no devices will be found.
 *
 * This function cannot be called if srt_input_device_monitor_start()
 * or srt_input_device_monitor_stop() have already been called.
 */
void
srt_input_device_monitor_request_evdev (SrtInputDeviceMonitor *monitor)
{
  SrtInputDeviceMonitorInterface *iface = SRT_INPUT_DEVICE_MONITOR_GET_INTERFACE (monitor);

  g_return_if_fail (iface != NULL);

  iface->request_evdev (monitor);
}

/**
 * srt_input_device_monitor_start:
 * @monitor: The input device monitor
 *
 * Start to watch for input devices.
 *
 * The device monitor will emit signals in the thread-default main
 * context of the thread where this function was called
 * (see g_main_context_push_thread_default() for details).
 *
 * The SrtInputDeviceMonitor::added signal will be emitted when
 * a matching input device is detected.
 * If the monitor is watching for both %SRT_INPUT_DEVICE_FLAGS_EVENT
 * and %SRT_INPUT_DEVICE_FLAGS_RAW_HID devices, one signal will be
 * emitted for each one.
 *
 * The SrtInputDeviceMonitor::removed signal will be emitted when
 * a matching input device is removed.
 *
 * This function cannot be called if srt_input_device_monitor_start()
 * or srt_input_device_monitor_stop() have already been called.
 */
gboolean
srt_input_device_monitor_start (SrtInputDeviceMonitor *monitor,
                                GError **error)
{
  SrtInputDeviceMonitorInterface *iface = SRT_INPUT_DEVICE_MONITOR_GET_INTERFACE (monitor);

  g_return_val_if_fail (iface != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  return iface->start (monitor, error);
}

static gboolean
srt_input_device_monitor_default_start (SrtInputDeviceMonitor *monitor,
                                        GError **error)
{
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Not implemented");
  return FALSE;
}

/**
 * srt_input_device_monitor_stop:
 * @monitor: The input device monitor
 *
 * Stop the input device monitor. It still exists in memory until all
 * references are released, but will stop signalling new events.
 */
void
srt_input_device_monitor_stop (SrtInputDeviceMonitor *monitor)
{
  SrtInputDeviceMonitorInterface *iface = SRT_INPUT_DEVICE_MONITOR_GET_INTERFACE (monitor);

  g_return_if_fail (iface != NULL);

  iface->stop (monitor);
}

static void
srt_input_device_monitor_default_init (SrtInputDeviceMonitorInterface *iface)
{
#define IMPLEMENT2(x, y) iface->x = srt_input_device_monitor_default_ ## y
#define IMPLEMENT(x) IMPLEMENT2 (x, x)
  IMPLEMENT2 (request_raw_hid, do_nothing);
  IMPLEMENT2 (request_evdev, do_nothing);
  IMPLEMENT (start);
  IMPLEMENT2 (stop, do_nothing);
#undef IMPLEMENT

  /**
   * SrtInputDeviceMonitor::added:
   * @monitor: The input device monitor
   * @device: The device
   *
   * Emitted when an input device is added.
   */
  monitor_signal_added =
    g_signal_new ("added",
                  SRT_TYPE_INPUT_DEVICE_MONITOR,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (SrtInputDeviceMonitorInterface, added),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  SRT_TYPE_INPUT_DEVICE);

  /**
   * SrtInputDeviceMonitor::removed:
   * @monitor: The input device monitor
   * @device: The device
   *
   * Emitted when an input device is removed.
   */
  monitor_signal_removed =
    g_signal_new ("removed",
                  SRT_TYPE_INPUT_DEVICE_MONITOR,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (SrtInputDeviceMonitorInterface, removed),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 1,
                  SRT_TYPE_INPUT_DEVICE);

  /**
   * SrtInputDeviceMonitor::all-for-now:
   * @monitor: The input device monitor
   *
   * Emitted when the initial batch of input devices has been discovered.
   */
  monitor_signal_all_for_now =
    g_signal_new ("all-for-now",
                  SRT_TYPE_INPUT_DEVICE_MONITOR,
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (SrtInputDeviceMonitorInterface, all_for_now),
                  NULL, NULL, NULL,
                  G_TYPE_NONE, 0);

  /**
   * SrtInputDeviceMonitor::flags:
   *
   * Flags affecting the input device monitor.
   */
  g_object_interface_install_property (iface,
      g_param_spec_flags ("flags",
                          "Flags",
                          "Flags affecting the input device monitor",
                          SRT_TYPE_INPUT_DEVICE_MONITOR_FLAGS,
                          SRT_INPUT_DEVICE_MONITOR_FLAGS_NONE,
                          G_PARAM_READWRITE
                          | G_PARAM_CONSTRUCT_ONLY
                          | G_PARAM_STATIC_STRINGS));

  /**
   * SrtInputDeviceMonitor::is-active:
   *
   * The value of srt_input_device_monitor_is_active().
   */
  g_object_interface_install_property (iface,
      g_param_spec_boolean ("is-active",
                            "Is active",
                            "TRUE if started and not stopped",
                            FALSE,
                            G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

void
_srt_input_device_monitor_emit_added (SrtInputDeviceMonitor *monitor,
                                      SrtInputDevice *device)
{
  g_debug ("Added %p", device);
  g_signal_emit (monitor, monitor_signal_added, 0, device);
}

void
_srt_input_device_monitor_emit_removed (SrtInputDeviceMonitor *monitor,
                                        SrtInputDevice *device)
{
  g_debug ("Removed %p", device);
  g_signal_emit (monitor, monitor_signal_removed, 0, device);
}

void
_srt_input_device_monitor_emit_all_for_now (SrtInputDeviceMonitor *monitor)
{
  g_debug ("All for now");
  g_signal_emit (monitor, monitor_signal_all_for_now, 0);
}
