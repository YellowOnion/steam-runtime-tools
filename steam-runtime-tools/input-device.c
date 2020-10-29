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

#include <linux/hidraw.h>
#include <linux/input.h>

#include <libglnx.h>

#include "steam-runtime-tools/enums.h"

#include "steam-runtime-tools/direct-input-device-internal.h"
#include "steam-runtime-tools/udev-input-device-internal.h"

#ifndef HIDIOCGRAWUNIQ
/* added in Linux 5.6, will fail on older kernels; this should
 * be fine, we'll just report a NULL unique ID */
#define HIDIOCGRAWUNIQ(len)     _IOC(_IOC_READ, 'H', 0x08, len)
#endif

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

static gchar **
srt_input_device_default_get_null_strv (SrtInputDevice *device)
{
  return NULL;
}

/**
 * srt_input_device_get_interface_flags:
 * @device: An object implementing #SrtInputDeviceInterface
 *
 * Return flags describing how the input device can be used.
 *
 * Returns: Flags describing the input device.
 */
SrtInputDeviceInterfaceFlags
srt_input_device_get_interface_flags (SrtInputDevice *device)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, SRT_INPUT_DEVICE_INTERFACE_FLAGS_NONE);

  return iface->get_interface_flags (device);
}

static SrtInputDeviceInterfaceFlags
srt_input_device_default_get_interface_flags (SrtInputDevice *device)
{
  return SRT_INPUT_DEVICE_INTERFACE_FLAGS_NONE;
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

/**
 * srt_input_device_dup_uevent:
 * @device: An object implementing #SrtInputDeviceInterface
 *
 * Return the `uevent` data from the kernel.
 *
 * Returns: (nullable) (transfer full): A multi-line string, or %NULL.
 *  Free with g_free().
 */
gchar *
srt_input_device_dup_uevent (SrtInputDevice *device)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->dup_uevent (device);
}

static gchar *
read_uevent (const char *sys_path)
{
  g_autofree gchar *uevent_path = NULL;
  g_autofree gchar *contents = NULL;

  if (sys_path == NULL)
    return NULL;

  uevent_path = g_build_filename (sys_path, "uevent", NULL);

  if (g_file_get_contents (uevent_path, &contents, NULL, NULL))
    return g_steal_pointer (&contents);

  return NULL;
}

/*
 * Default implementation: read the file in /sys. The kernel provides this,
 * so it should always be present, except in containers.
 */
static gchar *
srt_input_device_default_dup_uevent (SrtInputDevice *device)
{
  return read_uevent (srt_input_device_get_sys_path (device));
}

static gchar *
srt_input_device_default_dup_hid_uevent (SrtInputDevice *device)
{
  return read_uevent (srt_input_device_get_hid_sys_path (device));
}

static gchar *
srt_input_device_default_dup_input_uevent (SrtInputDevice *device)
{
  return read_uevent (srt_input_device_get_input_sys_path (device));
}

static gchar *
srt_input_device_default_dup_usb_device_uevent (SrtInputDevice *device)
{
  return read_uevent (srt_input_device_get_usb_device_sys_path (device));
}

/**
 * srt_input_device_dup_udev_properties:
 * @device: An object implementing #SrtInputDeviceInterface
 *
 * Return the udev properties of this input device, if available, in
 * the same format as `environ`. g_environ_getenv() can be used to
 * process them.
 *
 * Returns: (nullable) (transfer full): udev properties, or %NULL.
 *  Free with g_strfreev().
 */
gchar **
srt_input_device_dup_udev_properties (SrtInputDevice *device)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->dup_udev_properties (device);
}

/**
 * srt_input_device_get_identity:
 * @device: An object implementing #SrtInputDeviceInterface
 * @bus_type: (out): Used to return the bus type from `<linux/input.h>`,
 *  usually `BUS_USB` or `BUS_BLUETOOTH`
 * @vendor_id: (out): Used to return the vendor ID, namespaced by
 *  the @bus_type, or 0 if unavailable
 * @product_id: (out): Used to return the product ID, namespaced by
 *  the @vendor_id, or 0 if unavailable
 * @version: (out): Used to return the device version, or 0 if unavailable
 *
 * Attempt to identify the device. If available, return the bus type,
 * the vendor ID, the product ID and/or the device version via "out"
 * parameters.
 *
 * The source of the information is unspecified. Use
 * srt_input_device_get_hid_identity(),
 * srt_input_device_get_input_identity() and/or
 * srt_input_device_get_usb_device_identity() if a specific source
 * is desired.
 *
 * Returns: %TRUE if information was available
 */
gboolean
srt_input_device_get_identity (SrtInputDevice *device,
                               unsigned int *bus_type,
                               unsigned int *vendor_id,
                               unsigned int *product_id,
                               unsigned int *version)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, FALSE);

  return iface->get_identity (device, bus_type, vendor_id, product_id, version);
}

static gboolean
srt_input_device_default_get_identity (SrtInputDevice *device,
                                       unsigned int *bus_type,
                                       unsigned int *vendor_id,
                                       unsigned int *product_id,
                                       unsigned int *version)
{
  if (srt_input_device_get_input_identity (device,
                                           bus_type, vendor_id, product_id,
                                           version, NULL, NULL, NULL))
    return TRUE;

  if (srt_input_device_get_hid_identity (device,
                                         bus_type, vendor_id, product_id,
                                         NULL, NULL, NULL))
    {
      if (version != NULL)
        {
          *version = 0;

          srt_input_device_get_usb_device_identity (device, NULL, NULL, version,
                                                    NULL, NULL, NULL);
        }

      return TRUE;
    }

  if (srt_input_device_get_usb_device_identity (device,
                                                vendor_id, product_id, version,
                                                NULL, NULL, NULL))
    {
      if (bus_type != NULL)
        *bus_type = BUS_USB;

      return TRUE;
    }

  return FALSE;
}

/**
 * srt_input_device_get_hid_sys_path:
 * @device: An object implementing #SrtInputDeviceInterface
 *
 * Return the path of the device directory in /sys that represents this
 * input device's closest ancestor that is a Human Interface Device.
 * Many, but not all, input devices have a HID ancestor. If there is no
 * applicable HID device, return %NULL.
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
srt_input_device_get_hid_sys_path (SrtInputDevice *device)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->get_hid_sys_path (device);
}

/**
 * srt_input_device_dup_hid_uevent:
 * @device: An object implementing #SrtInputDeviceInterface
 *
 * Return the uevent data structure similar to
 * srt_input_device_dup_uevent(), but for the ancestor device returned
 * by srt_input_device_get_hid_sys_path().
 *
 * Returns: (nullable) (transfer full): A multi-line string, or %NULL.
 *  Free with g_free().
 */
gchar *
srt_input_device_dup_hid_uevent (SrtInputDevice *device)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->dup_hid_uevent (device);
}

/**
 * srt_input_device_get_hid_identity:
 * @device: An object implementing #SrtInputDeviceInterface
 * @bus_type: (out): Used to return the bus type from `<linux/input.h>`,
 *  usually `BUS_USB` or `BUS_BLUETOOTH`
 * @vendor_id: (out): Used to return the vendor ID, namespaced by
 *  the @bus_type, or 0 if unavailable
 * @product_id: (out): Used to return the product ID, namespaced by
 *  the @vendor_id, or 0 if unavailable
 * @name: (out) (transfer none): Used to return a human-readable name
 *  that usually combines the vendor and product, or empty or %NULL
 *  if unavailable
 * @phys: (out) (transfer none): Used to return how the device is
 *  physically attached, or empty or %NULL if unavailable
 * @uniq: (out) (transfer none): Used to return the device's serial number
 *  or other unique identifier, or empty or %NULL if unavailable
 *
 * Attempt to identify the device. If available, return details via
 * "out" parameters.
 *
 * Returns: %TRUE if this is an evdev device and information was available
 */
gboolean
srt_input_device_get_hid_identity (SrtInputDevice *device,
                                   unsigned int *bus_type,
                                   unsigned int *vendor_id,
                                   unsigned int *product_id,
                                   const char **name,
                                   const char **phys,
                                   const char **uniq)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, FALSE);

  return iface->get_hid_identity (device, bus_type, vendor_id, product_id,
                                  name, phys, uniq);
}

static gboolean
srt_input_device_default_get_hid_identity (SrtInputDevice *device,
                                           unsigned int *bus_type,
                                           unsigned int *vendor_id,
                                           unsigned int *product_id,
                                           const char **name,
                                           const char **phys,
                                           const char **uniq)
{
  return FALSE;
}

/**
 * srt_input_device_get_usb_device_sys_path:
 * @device: An object implementing #SrtInputDeviceInterface
 *
 * If the @device is associated with a USB device, return the path in
 * /sys representing the Linux `usb_device`. If not, return %NULL.
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
srt_input_device_get_usb_device_sys_path (SrtInputDevice *device)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->get_usb_device_sys_path (device);
}

/**
 * srt_input_device_dup_usb_device_uevent:
 * @device: An object implementing #SrtInputDeviceInterface
 *
 * Return the uevent data structure similar to
 * srt_input_device_dup_uevent(), but for the ancestor device returned
 * by srt_input_device_get_usb_device_sys_path().
 *
 * Returns: (nullable) (transfer full): A multi-line string, or %NULL.
 *  Free with g_free().
 */
gchar *
srt_input_device_dup_usb_device_uevent (SrtInputDevice *device)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->dup_usb_device_uevent (device);
}

/**
 * srt_input_device_get_usb_device_identity:
 * @device: An object implementing #SrtInputDeviceInterface
 * @bus_type: (out): Used to return the bus type from `<linux/input.h>`,
 *  usually `BUS_USB` or `BUS_BLUETOOTH`
 * @vendor_id: (out): Used to return the vendor ID, namespaced by
 *  the @bus_type, or 0 if unavailable
 * @product_id: (out): Used to return the product ID, namespaced by
 *  the @vendor_id, or 0 if unavailable
 * @device_version: (out): Used to return the product version, or 0 if unavailable
 * @manufacturer: (out) (transfer none): Used to return the human-readable name
 *  of the manufacturer, or empty or %NULL if unavailable
 * @product: (out) (transfer none): Used to return the human-readable name
 *  of the product, or empty or %NULL if unavailable
 * @serial: (out) (transfer none): Used to return the device's serial number
 *  or other unique identifier, or empty or %NULL if unavailable
 *
 * Attempt to identify the device. If available, return details via
 * "out" parameters.
 *
 * Returns: %TRUE if this is a USB device and information was available
 */
gboolean
srt_input_device_get_usb_device_identity (SrtInputDevice *device,
                                          unsigned int *vendor_id,
                                          unsigned int *product_id,
                                          unsigned int *device_version,
                                          const char **manufacturer,
                                          const char **product,
                                          const char **serial)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, FALSE);

  return iface->get_usb_device_identity (device,
                                         vendor_id, product_id, device_version,
                                         manufacturer, product, serial);
}

static gboolean
srt_input_device_default_get_usb_device_identity (SrtInputDevice *device,
                                                  unsigned int *vendor_id,
                                                  unsigned int *product_id,
                                                  unsigned int *device_version,
                                                  const char **manufacturer,
                                                  const char **product,
                                                  const char **serial)
{
  return FALSE;
}

/**
 * srt_input_device_get_input_sys_path:
 * @device: An object implementing #SrtInputDeviceInterface
 *
 * If the @device has an ancestor device that advertises evdev input
 * capabilities, return the path in /sys for that device. Otherwise
 * return %NULL.
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
srt_input_device_get_input_sys_path (SrtInputDevice *device)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->get_input_sys_path (device);
}

/**
 * srt_input_device_dup_input_uevent:
 * @device: An object implementing #SrtInputDeviceInterface
 *
 * Return the uevent data structure similar to
 * srt_input_device_dup_uevent(), but for the ancestor device returned
 * by srt_input_device_get_input_sys_path().
 *
 * Returns: (nullable) (transfer full): A multi-line string, or %NULL.
 *  Free with g_free().
 */
gchar *
srt_input_device_dup_input_uevent (SrtInputDevice *device)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, NULL);

  return iface->dup_input_uevent (device);
}

/**
 * srt_input_device_get_input_identity:
 * @device: An object implementing #SrtInputDeviceInterface
 * @bus_type: (out): Used to return the bus type from `<linux/input.h>`,
 *  usually `BUS_USB` or `BUS_BLUETOOTH`
 * @vendor_id: (out): Used to return the vendor ID, namespaced by
 *  the @bus_type, or 0 if unavailable
 * @product_id: (out): Used to return the product ID, namespaced by
 *  the @vendor_id, or 0 if unavailable
 * @version: (out): Used to return the product version, or 0 if unavailable
 * @name: (out) (transfer none): Used to return a human-readable name
 *  that usually combines the vendor and product, or empty or %NULL
 *  if unavailable
 * @phys: (out) (transfer none): Used to return how the device is
 *  physically attached, or empty or %NULL if unavailable
 * @uniq: (out) (transfer none): Used to return the device's serial number
 *  or other unique identifier, or empty or %NULL if unavailable
 *
 * Attempt to identify the device. If available, return details via
 * "out" parameters.
 *
 * Returns: %TRUE if this is an evdev device and information was available
 */
gboolean
srt_input_device_get_input_identity (SrtInputDevice *device,
                                     unsigned int *bus_type,
                                     unsigned int *vendor_id,
                                     unsigned int *product_id,
                                     unsigned int *version,
                                     const char **name,
                                     const char **phys,
                                     const char **uniq)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, FALSE);

  return iface->get_input_identity (device,
                                    bus_type, vendor_id, product_id, version,
                                    name, phys, uniq);
}

static gboolean
srt_input_device_default_get_input_identity (SrtInputDevice *device,
                                             unsigned int *bus_type,
                                             unsigned int *vendor_id,
                                             unsigned int *product_id,
                                             unsigned int *version,
                                             const char **name,
                                             const char **phys,
                                             const char **uniq)
{
  return FALSE;
}

static void
srt_input_device_default_init (SrtInputDeviceInterface *iface)
{
#define IMPLEMENT2(x, y) iface->x = srt_input_device_default_ ## y
#define IMPLEMENT(x) IMPLEMENT2 (x, x)

  IMPLEMENT (get_interface_flags);
  IMPLEMENT2 (get_dev_node, get_null_string);
  IMPLEMENT2 (get_sys_path, get_null_string);
  IMPLEMENT2 (get_subsystem, get_null_string);
  IMPLEMENT (get_identity);

  IMPLEMENT2 (get_hid_sys_path, get_null_string);
  IMPLEMENT (get_hid_identity);
  IMPLEMENT2 (get_input_sys_path, get_null_string);
  IMPLEMENT (get_input_identity);
  IMPLEMENT2 (get_usb_device_sys_path, get_null_string);
  IMPLEMENT (get_usb_device_identity);

  IMPLEMENT2 (dup_udev_properties, get_null_strv);

  IMPLEMENT (dup_uevent);
  IMPLEMENT (dup_hid_uevent);
  IMPLEMENT (dup_input_uevent);
  IMPLEMENT (dup_usb_device_uevent);

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

static const SrtInputDeviceMonitorFlags mode_flags = (SRT_INPUT_DEVICE_MONITOR_FLAGS_UDEV
                                                      | SRT_INPUT_DEVICE_MONITOR_FLAGS_DIRECT);

/*
 * @monitor_out: (out) (not optional):
 * @error: (inout) (not optional):
 */
static gboolean
try_udev (SrtInputDeviceMonitorFlags flags,
          SrtInputDeviceMonitor **monitor_out,
          GError **error)
{
  /* Don't try it again if we already failed, to avoid another warning */
  if (*error != NULL)
    return FALSE;

  *monitor_out = srt_udev_input_device_monitor_new (flags, error);

  if (*monitor_out != NULL)
    return TRUE;

  /* We usually expect this to succeed, so log a warning if it fails */
  g_warning ("Unable to initialize udev input device monitor: %s",
             (*error)->message);
  return FALSE;
}

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
  SrtInputDeviceMonitor *udev = NULL;
  g_autoptr(GError) udev_error = NULL;

  if (__builtin_popcount (flags & mode_flags) > 1)
    g_warning ("Requesting more than one of UDEV and DIRECT "
               "monitoring has undefined results: 0x%x", flags);

  /* First see whether the caller expressed a preference */

  if (flags & SRT_INPUT_DEVICE_MONITOR_FLAGS_UDEV)
    {
      if (try_udev (flags, &udev, &udev_error))
        return udev;
    }

  if (flags & SRT_INPUT_DEVICE_MONITOR_FLAGS_DIRECT)
    return srt_direct_input_device_monitor_new (flags);

  /* Prefer a direct monitor if we're in a container */
  if (g_file_test ("/.flatpak-info", G_FILE_TEST_EXISTS)
      || g_file_test ("/run/pressure-vessel", G_FILE_TEST_EXISTS)
      || g_file_test ("/run/host", G_FILE_TEST_EXISTS))
    return srt_direct_input_device_monitor_new (flags);

  if (try_udev (flags, &udev, &udev_error))
    return udev;

  /* Fall back to direct monitoring if we don't have libudev */
  return srt_direct_input_device_monitor_new (flags);
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

gboolean
_srt_get_identity_from_evdev (int fd,
                              guint32 *bus_type,
                              guint32 *vendor,
                              guint32 *product,
                              guint32 *version,
                              gchar **name,
                              gchar **phys,
                              gchar **uniq)
{
  struct input_id iid = {};
  char buf[256] = { '\0' };

  if (bus_type != NULL)
    *bus_type = 0;

  if (vendor != NULL)
    *vendor = 0;

  if (product != NULL)
    *product = 0;

  if (ioctl (fd, EVIOCGID, &iid) < 0)
    {
      g_debug ("EVIOCGID: %s", g_strerror (errno));
      return FALSE;
    }

  if (name != NULL
      && ioctl (fd, EVIOCGNAME (sizeof (buf) - 1), buf) == 0)
    *name = g_strdup (buf);

  memset (buf, '\0', sizeof (buf));

  if (phys != NULL
      && ioctl (fd, EVIOCGPHYS (sizeof (buf) - 1), buf) == 0)
    *phys = g_strdup (buf);

  memset (buf, '\0', sizeof (buf));

  if (uniq != NULL
      && ioctl (fd, EVIOCGUNIQ (sizeof (buf) - 1), buf) == 0)
    *uniq = g_strdup (buf);

  if (bus_type != NULL)
    *bus_type = iid.bustype;

  if (vendor != NULL)
    *vendor = iid.vendor;

  if (product != NULL)
    *product = iid.product;

  if (version != NULL)
    *version = iid.version;

  return TRUE;
}

gboolean
_srt_get_identity_from_raw_hid (int fd,
                                guint32 *bus_type,
                                guint32 *vendor,
                                guint32 *product,
                                gchar **name,
                                gchar **phys,
                                gchar **uniq)
{
  char buf[256] = { '\0' };
  struct hidraw_devinfo devinfo = {};

  if (bus_type != NULL)
    *bus_type = 0;

  if (vendor != NULL)
    *vendor = 0;

  if (product != NULL)
    *product = 0;

  if (ioctl (fd, HIDIOCGRAWINFO, &devinfo) < 0)
    {
      g_debug ("HIDIOCGRAWINFO: %s", g_strerror (errno));
      return FALSE;
    }

  if (name != NULL
      && ioctl (fd, HIDIOCGRAWNAME (sizeof (buf) - 1), buf) == 0)
    *name = g_strdup (buf);

  memset (buf, '\0', sizeof (buf));

  if (phys != NULL
      && ioctl (fd, HIDIOCGRAWPHYS (sizeof (buf) - 1), buf) == 0)
    *phys = g_strdup (buf);

  memset (buf, '\0', sizeof (buf));

  if (uniq != NULL
      && ioctl (fd, HIDIOCGRAWUNIQ (sizeof (buf) - 1), buf) == 0)
    *uniq = g_strdup (buf);

  if (bus_type != NULL)
    *bus_type = devinfo.bustype;

  if (vendor != NULL)
    *vendor = devinfo.vendor;

  if (product != NULL)
    *product = devinfo.product;

  return TRUE;
}

/*
 * Returns: The field @key from @text, or %NULL if not found.
 */
gchar *
_srt_input_device_uevent_field (const char *text,
                                const char *key)
{
  const char *line = text;
  size_t key_len = strlen (key);

  for (line = text;
       line != NULL && *line != '\0';
       line = strchr (line, '\n'))
    {
      while (*line == '\n')
        line++;

      /* If the line starts with KEY=, we're looking at the right field */
      if (strncmp (line, key, key_len) == 0
          && line[key_len] == '=')
        {
          const char *value = line + key_len + 1;
          const char *after = strchrnul (line, '\n');

          g_assert (after != NULL);
          g_assert (after >= value);
          g_assert (*after == '\n' || *after == '\0');
          return g_strndup (value, after - value);
        }
    }

  return NULL;
}

/*
 * Returns: %TRUE if @text contains KEY=WANT_VALUE, preceded by
 *  beginning-of-string or a newline, and followed by a newline
 *  or end-of-string.
 */
gboolean
_srt_input_device_uevent_field_equals (const char *text,
                                       const char *key,
                                       const char *want_value)
{
  const char *line = text;
  size_t key_len = strlen (key);
  size_t val_len = strlen (want_value);

  for (line = text;
       line != NULL && *line != '\0';
       line = strchr (line, '\n'))
    {
      while (*line == '\n')
        line++;

      /* If the line starts with KEY=, we're looking at the right field */
      if (strncmp (line, key, key_len) == 0
          && line[key_len] == '=')
        {
          const char *real_value = line + key_len + 1;
          char after;

          if (strncmp (real_value, want_value, val_len) != 0)
            return FALSE;

          after = real_value[val_len];

          return (after == '\0' || after == '\n');
        }
    }

  return FALSE;
}

gboolean
_srt_get_identity_from_hid_uevent (const char *text,
                                   guint32 *bus_type,
                                   guint32 *vendor_id,
                                   guint32 *product_id,
                                   gchar **name,
                                   gchar **phys,
                                   gchar **uniq)
{
  g_autofree gchar *id = _srt_input_device_uevent_field (text, "HID_ID");
  struct
  {
    guint bus_type;
    guint vendor_id;
    guint product_id;
  } tmp;
  int used;

  if (id == NULL)
    return FALSE;

  if (sscanf (id, "%x:%x:%x%n",
              &tmp.bus_type, &tmp.vendor_id, &tmp.product_id, &used) != 3
      || ((size_t) used) != strlen (id))
    return FALSE;

  if (name != NULL)
    *name = _srt_input_device_uevent_field (text, "HID_NAME");

  if (phys != NULL)
    *phys = _srt_input_device_uevent_field (text, "HID_PHYS");

  if (uniq != NULL)
    *uniq = _srt_input_device_uevent_field (text, "HID_UNIQ");

  if (bus_type != NULL)
    *bus_type = tmp.bus_type;

  if (vendor_id != NULL)
    *vendor_id = tmp.vendor_id;

  if (product_id != NULL)
    *product_id = tmp.product_id;

  return TRUE;
}
