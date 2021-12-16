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

/*
 * Set @error and return %FALSE if @mode_and_flags is unsupported.
 */
gboolean
_srt_input_device_check_open_flags (int mode_and_flags,
                                    GError **error)
{
  int mode;
  int unhandled_flags;

  mode = mode_and_flags & (O_RDONLY|O_WRONLY|O_RDWR);
  unhandled_flags = mode_and_flags & ~mode;

  if (unhandled_flags & O_NONBLOCK)
    unhandled_flags &= ~O_NONBLOCK;

  if (unhandled_flags != 0)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "Flags not supported: 0x%x", unhandled_flags);
      return FALSE;
    }

  switch (mode)
    {
      case O_RDONLY:
      case O_WRONLY:
      case O_RDWR:
        break;

      default:
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                     "Mode not supported: 0x%x", mode);
        return FALSE;
    }

  return TRUE;
}

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
 * srt_input_device_get_type_flags:
 * @device: An object implementing #SrtInputDeviceInterface
 *
 * Return flags describing what sort of device this is.
 * If possible, these will be taken from a data source such as udev's
 * input_id builtin, which will be treated as authoritative.
 *
 * Returns: Flags describing the input device.
 */
SrtInputDeviceTypeFlags
srt_input_device_get_type_flags (SrtInputDevice *device)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, SRT_INPUT_DEVICE_TYPE_FLAGS_NONE);

  return iface->get_type_flags (device);
}

/**
 * srt_input_device_gues_type_flags_from_event_capabilities:
 * @device: An object implementing #SrtInputDeviceInterface
 *
 * Return flags describing what sort of device this is.
 * Unlike srt_input_device_get_type_flags(), this function always
 * tries to guess the type flags from the event capabilities, which
 * can be used in diagnostic tools to highlight devices that might be
 * misidentified when only their event capabilities are available.
 *
 * Returns: Flags describing the input device.
 */
SrtInputDeviceTypeFlags
srt_input_device_guess_type_flags_from_event_capabilities (SrtInputDevice *device)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);
  const SrtEvdevCapabilities *caps;

  g_return_val_if_fail (iface != NULL, 0);

  caps = iface->peek_event_capabilities (device);

  if (caps == NULL)
    return SRT_INPUT_DEVICE_TYPE_FLAGS_NONE;

  return _srt_evdev_capabilities_guess_type (caps);
}

#define srt_input_device_default_get_type_flags \
  srt_input_device_guess_type_flags_from_event_capabilities

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

const unsigned long *
_srt_evdev_capabilities_get_bits (const SrtEvdevCapabilities *caps,
                                  unsigned int type,
                                  size_t *n_longs)
{
  const unsigned long *buf = NULL;
  size_t n = 0;

  switch (type)
    {
      case 0:
        buf = &caps->ev[0];
        n = G_N_ELEMENTS (caps->ev);
        break;

      case EV_KEY:
        buf = &caps->keys[0];
        n = G_N_ELEMENTS (caps->keys);
        break;

      case EV_ABS:
        buf = &caps->abs[0];
        n = G_N_ELEMENTS (caps->abs);
        break;

      case EV_REL:
        buf = &caps->rel[0];
        n = G_N_ELEMENTS (caps->rel);
        break;

      case EV_FF:
        buf = &caps->ff[0];
        n = G_N_ELEMENTS (caps->ff);
        break;

      case EV_MSC:
      case EV_SW:
      case EV_LED:
      case EV_SND:
      case EV_REP:
      case EV_PWR:
      case EV_FF_STATUS:
      case EV_MAX:
      default:
        break;
    }

  *n_longs = n;
  return buf;
}

/* Format to print an unsigned long in hex, zero-padded to full size
 * if possible. */
#if G_MAXULONG == 0xffffffffUL
# define HEX_LONG_FORMAT "08lx"
#elif defined(__LP64__)
# define HEX_LONG_FORMAT "016lx"
#else
# warning Unsupported architecture, assuming ILP32
# define HEX_LONG_FORMAT "08lx"
#endif

void
_srt_evdev_capabilities_dump (const SrtEvdevCapabilities *caps)
{
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (caps->ev); i++)
    g_debug ("ev[%zu]: %" HEX_LONG_FORMAT, i, caps->ev[i]);

  for (i = 0; i < G_N_ELEMENTS (caps->keys); i++)
    g_debug ("keys[%zu]: %" HEX_LONG_FORMAT, i, caps->keys[i]);

  for (i = 0; i < G_N_ELEMENTS (caps->abs); i++)
    g_debug ("abs[%zu]: %" HEX_LONG_FORMAT, i, caps->abs[i]);

  for (i = 0; i < G_N_ELEMENTS (caps->rel); i++)
    g_debug ("rel[%zu]: %" HEX_LONG_FORMAT, i, caps->rel[i]);

  for (i = 0; i < G_N_ELEMENTS (caps->props); i++)
    g_debug ("props[%zu]: %" HEX_LONG_FORMAT, i, caps->props[i]);
}

/**
 * srt_input_device_get_event_capabilities:
 * @device: An object implementing #SrtInputDeviceInterface
 * @type: An event type from `<linux/input.h>` such as `EV_KEY`,
 *  or 0 to get the supported event types
 * @storage: (optional) (out caller-allocates) (array length=n_longs): An
 *  array of @n_longs unsigned long values that will receive the bitfield,
 *  allocated by the caller. Bit number 0 is the least significant bit
 *  of storage[0] and so on up to the most significant bit of storage[0],
 *  which is one place less significant than the least significant bit
 *  of storage[1] if present.
 * @n_longs: The length of @storage, or 0 if @storage is %NULL
 *
 * Fill a buffer with the event capabilities in the same encoding used
 * for the EVIOCGBIT ioctl, or query how large that buffer would have to be.
 *
 * Bits in @storage above the highest known event value will be zeroed.
 *
 * If @storage is too small, high event values will not be represented.
 * For example, if @type is `EV_KEY` and @n_longs is 1, then @storage
 * will only indicate whether the first 32 or 64 key event codes are
 * supported, and will not indicate anything about the level of support
 * for `KEY_RIGHTALT` (event code 100).
 *
 * If @type is not a supported type, all of @storage will be zeroed and
 * 0 will be returned.
 *
 * Returns: The number of unsigned long values that would have been
 *  required for the highest possible event of type @type, which might
 *  be greater than @n_longs
 */
size_t
srt_input_device_get_event_capabilities (SrtInputDevice *device,
                                         unsigned int type,
                                         unsigned long *storage,
                                         size_t n_longs)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);
  const SrtEvdevCapabilities *caps;
  const unsigned long *buf = NULL;
  size_t n = 0;

  g_return_val_if_fail (iface != NULL, 0);
  g_return_val_if_fail (storage != NULL || n_longs == 0, 0);

  if (n_longs != 0)
    memset (storage, '\0', n_longs * sizeof (long));

  caps = iface->peek_event_capabilities (device);

  if (caps != NULL)
    buf = _srt_evdev_capabilities_get_bits (caps, type, &n);

  if (storage != NULL && buf != NULL && n_longs != 0 && n != 0)
    memcpy (storage, buf, MIN (n_longs, n) * sizeof (long));

  return n;
}

static const SrtEvdevCapabilities *
srt_input_device_default_peek_event_capabilities (SrtInputDevice *device)
{
  return NULL;
}

/**
 * srt_input_device_has_event_type:
 * @device: An object implementing #SrtInputDeviceInterface
 * @type: `EV_KEY`, `EV_ABS`, `EV_REL`, `EV_FF` or another evdev event type
 *
 * If the @device is an evdev device implementing the given event
 * type, return %TRUE. Otherwise return %FALSE.
 *
 * Returns: %TRUE if the object implements the given event type.
 */
gboolean
srt_input_device_has_event_type (SrtInputDevice *device,
                                 unsigned int type)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);
  const SrtEvdevCapabilities *caps;

  g_return_val_if_fail (iface != NULL, 0);

  caps = iface->peek_event_capabilities (device);

  return (caps != NULL
          && type <= EV_MAX
          && test_bit_checked (type, caps->ev, G_N_ELEMENTS (caps->ev)));
}

/**
 * srt_input_device_get_event_types:
 * @device: An object implementing #SrtInputDeviceInterface
 * @storage: (optional) (out caller-allocates) (array length=n_longs): An
 *  array of @n_longs unsigned long values that will receive the bitfield,
 *  allocated by the caller. Bit number 0 is the least significant bit
 *  of storage[0] and so on up to the most significant bit of storage[0],
 *  which is one place less significant than the least significant bit
 *  of storage[1] if present.
 * @n_longs: The length of @storage, or 0 if @storage is %NULL
 *
 * Fill a buffer with the supported event types in the same encoding used
 * for the EVIOCGBIT ioctl, or query how large that buffer would have to be.
 * This is the same as
 * `srt_input_device_get_event_capabilities (device, 0, ...)`,
 * except that bit numbers in @storage reflect event types, for example
 * bit number 3 (`storage[0] & (1 << 3)`) represents
 * event type 3 (`EV_ABS`).
 *
 * Returns: The number of unsigned long values that would have been
 *  required for the highest possible event of type @type, which might
 *  be greater than @n_longs
 */
size_t
srt_input_device_get_event_types (SrtInputDevice *device,
                                  unsigned long *storage,
                                  size_t n_longs)
{
  return srt_input_device_get_event_capabilities (device, 0, storage, n_longs);
}

/**
 * srt_input_device_has_input_property:
 * @device: An object implementing #SrtInputDeviceInterface
 * @input_prop: An input property such as `INPUT_PROP_POINTER`
 *
 * If the @device is an evdev device with the given input
 * property, return %TRUE. Otherwise return %FALSE.
 *
 * Returns: %TRUE if the object has the given input property.
 */
gboolean
srt_input_device_has_input_property (SrtInputDevice *device,
                                     unsigned int input_prop)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);
  const SrtEvdevCapabilities *caps;

  g_return_val_if_fail (iface != NULL, 0);

  caps = iface->peek_event_capabilities (device);

  return (caps != NULL
          && input_prop <= INPUT_PROP_MAX
          && test_bit_checked (input_prop, caps->props, G_N_ELEMENTS (caps->props)));
}

/**
 * srt_input_device_get_input_properties:
 * @device: An object implementing #SrtInputDeviceInterface
 * @storage: (optional) (out caller-allocates) (array length=n_longs): An
 *  array of @n_longs unsigned long values that will receive the bitfield,
 *  allocated by the caller. Bit number 0 is the least significant bit
 *  of storage[0] and so on up to the most significant bit of storage[0],
 *  which is one place less significant than the least significant bit
 *  of storage[1] if present.
 * @n_longs: The length of @storage, or 0 if @storage is %NULL
 *
 * Fill a buffer with the input device properties in the same encoding used
 * for the EVIOCGPROP ioctl, or query how large that buffer would have to be.
 *
 * Bit numbers in @storage reflect input properties, for example
 * bit number 6 (`storage[0] & (1 << 6)`) represents
 * input property 6 (`INPUT_PROP_ACCELEROMETER`).
 *
 * Returns: The number of unsigned long values that would have been
 *  required for the highest possible event of type @type, which might
 *  be greater than @n_longs
 */
size_t
srt_input_device_get_input_properties (SrtInputDevice *device,
                                       unsigned long *storage,
                                       size_t n_longs)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);
  const SrtEvdevCapabilities *caps;

  g_return_val_if_fail (iface != NULL, 0);
  g_return_val_if_fail (storage != NULL || n_longs == 0, 0);

  if (n_longs != 0)
    memset (storage, '\0', n_longs * sizeof (long));

  caps = iface->peek_event_capabilities (device);

  if (caps == NULL)
    return 0;

  if (storage != NULL && n_longs != 0)
    memcpy (storage, caps->props,
            MIN (n_longs, G_N_ELEMENTS (caps->props)) * sizeof (long));

  return G_N_ELEMENTS (caps->props);
}

/**
 * srt_input_device_has_event_capability:
 * @device: An object implementing #SrtInputDeviceInterface
 * @type: `EV_KEY`, `EV_ABS`, `EV_REL`, `EV_FF` or another evdev event type
 * @code: A bit appropriate for the given evdev event type;
 *  for example, if @type is `EV_KEY`, then @code might be `KEY_BACKSPACE`
 *  or `BTN_X`
 *
 * If the @device is an evdev device implementing the given event
 * type and code, return %TRUE. Otherwise return %FALSE.
 *
 * This is currently only implemented for `EV_KEY`, `EV_ABS`, `EV_REL`
 * and `EV_FF` (the interesting event types for game controllers), and
 * will return %FALSE for more exotic event types.
 *
 * Returns: %TRUE if the object implements the given event type.
 */
gboolean
srt_input_device_has_event_capability (SrtInputDevice *device,
                                       unsigned int type,
                                       unsigned int code)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);
  const SrtEvdevCapabilities *caps;
  const unsigned long *buf = NULL;
  size_t n = 0;

  g_return_val_if_fail (iface != NULL, 0);

  caps = iface->peek_event_capabilities (device);

  if (caps == NULL)
    return FALSE;

  buf = _srt_evdev_capabilities_get_bits (caps, type, &n);
  return test_bit_checked (code, buf, n);
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

/**
 * srt_input_device_open:
 * @device: An object implementing #SrtInputDeviceInterface
 * @flags: Flags affecting how the device is opened
 * @error: Used to report the error on failure
 *
 * @flags must include one of: O_RDONLY, O_WRONLY, or O_RDWR.
 *
 * @flags may include zero or more of: O_NONBLOCK.
 *
 * The file descriptor is always opened with O_CLOEXEC and O_NOCTTY.
 * Explicitly specifying those flags is not allowed.
 *
 * Returns: A file descriptor owned by the caller, which must be closed
 *  with close(2) after use, or a negative number on error.
 */
int
srt_input_device_open (SrtInputDevice *device,
                       int flags,
                       GError **error)
{
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);

  g_return_val_if_fail (iface != NULL, -1);
  g_return_val_if_fail (error == NULL || *error == NULL, -1);

  if (!_srt_input_device_check_open_flags (flags, error))
    return -1;

  return iface->open_device (device, flags, error);
}

static int
srt_input_device_default_open_device (SrtInputDevice *device,
                                      int flags,
                                      GError **error)
{
  const char *devnode = NULL;
  int fd = -1;

  if (!_srt_input_device_check_open_flags (flags, error))
    return -1;

  devnode = srt_input_device_get_dev_node (device);

  if (devnode == NULL)
    {
      glnx_throw (error, "Device has no device node");
      return -1;
    }

  fd = open (devnode, flags | _SRT_INPUT_DEVICE_ALWAYS_OPEN_FLAGS);

  if (fd < 0)
    {
      glnx_throw_errno_prefix (error,
                               "Unable to open device node \"%s\"",
                               devnode);
      return -1;
    }

  return fd;
}

static void
srt_input_device_default_init (SrtInputDeviceInterface *iface)
{
#define IMPLEMENT2(x, y) iface->x = srt_input_device_default_ ## y
#define IMPLEMENT(x) IMPLEMENT2 (x, x)

  IMPLEMENT (get_type_flags);
  IMPLEMENT (get_interface_flags);
  IMPLEMENT2 (get_dev_node, get_null_string);
  IMPLEMENT2 (get_sys_path, get_null_string);
  IMPLEMENT2 (get_subsystem, get_null_string);
  IMPLEMENT (get_identity);
  IMPLEMENT (peek_event_capabilities);

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

  IMPLEMENT (open_device);

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
 * Returns: (transfer full): the input device monitor
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
 * @monitor: An object implementing #SrtInputDeviceMonitorInterface
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
 * @error: Used to report an error on failure
 *
 * Start to watch for input devices.
 *
 * The device monitor will emit signals in the thread-default main
 * context of the thread where this function was called
 * (see g_main_context_push_thread_default() for details).
 *
 * The SrtInputDeviceMonitor::added signal will be emitted when
 * a matching input device is detected.
 * If the monitor is watching for both %SRT_INPUT_DEVICE_INTERFACE_FLAGS_EVENT
 * and %SRT_INPUT_DEVICE_INTERFACE_FLAGS_RAW_HID devices, one signal will be
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

static gboolean
_srt_get_caps_from_evdev (int fd,
                          unsigned int type,
                          unsigned long *bitmask,
                          size_t bitmask_len_longs)
{
  size_t bitmask_len_bytes = bitmask_len_longs * sizeof (*bitmask);

  memset (bitmask, 0, bitmask_len_bytes);

  if (ioctl (fd, EVIOCGBIT (type, bitmask_len_bytes), bitmask) < 0)
    return FALSE;

  return TRUE;
}

gboolean
_srt_evdev_capabilities_set_from_evdev (SrtEvdevCapabilities *caps,
                                        int fd)
{
  if (_srt_get_caps_from_evdev (fd, 0, caps->ev, G_N_ELEMENTS (caps->ev)))
    {
      _srt_get_caps_from_evdev (fd, EV_KEY, caps->keys, G_N_ELEMENTS (caps->keys));
      _srt_get_caps_from_evdev (fd, EV_ABS, caps->abs, G_N_ELEMENTS (caps->abs));
      _srt_get_caps_from_evdev (fd, EV_REL, caps->rel, G_N_ELEMENTS (caps->rel));
      _srt_get_caps_from_evdev (fd, EV_FF, caps->ff, G_N_ELEMENTS (caps->ff));
      ioctl (fd, EVIOCGPROP (sizeof (caps->props)), caps->props);
      return TRUE;
    }

  memset (caps, 0, sizeof (*caps));
  return FALSE;
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

/* _srt_evdev_capabilities_guess_type relies on all the joystick axes
 * being in the first unsigned long. */
G_STATIC_ASSERT (ABS_HAT3Y < BITS_PER_LONG);
#define JOYSTICK_ABS_AXES \
  ((1 << ABS_X) | (1 << ABS_Y) \
   | (1 << ABS_RX) | (1 << ABS_RY) \
   | (1 << ABS_THROTTLE) | (1 << ABS_RUDDER) \
   | (1 << ABS_WHEEL) | (1 << ABS_GAS) | (1 << ABS_BRAKE) \
   | (1 << ABS_HAT0X) | (1 << ABS_HAT0Y) \
   | (1 << ABS_HAT1X) | (1 << ABS_HAT1Y) \
   | (1 << ABS_HAT2X) | (1 << ABS_HAT2Y) \
   | (1 << ABS_HAT3X) | (1 << ABS_HAT3Y))

static const unsigned int first_mouse_button = BTN_MOUSE;
static const unsigned int last_mouse_button = BTN_JOYSTICK - 1;

static const unsigned int first_joystick_button = BTN_JOYSTICK;
static const unsigned int last_joystick_button = BTN_GAMEPAD - 1;

static const unsigned int first_gamepad_button = BTN_GAMEPAD;
static const unsigned int last_gamepad_button = BTN_DIGI - 1;

static const unsigned int first_dpad_button = BTN_DPAD_UP;
static const unsigned int last_dpad_button = BTN_DPAD_RIGHT;

static const unsigned int first_extra_joystick_button = BTN_TRIGGER_HAPPY;
static const unsigned int last_extra_joystick_button = BTN_TRIGGER_HAPPY40;

/*
 * Guess the type of device from the input capabilities.
 *
 * We cannot copy what udev does for licensing reasons (it's GPL-licensed),
 * so this is a reimplementation, variously taking inspiration from:
 *
 * - kernel documentation (https://www.kernel.org/doc/Documentation/input/)
 * - libmanette
 * - SDL
 * - Wine dlls/winebus.sys
 * - udev
 */
SrtInputDeviceTypeFlags
_srt_evdev_capabilities_guess_type (const SrtEvdevCapabilities *caps)
{
  SrtInputDeviceTypeFlags flags = SRT_INPUT_DEVICE_TYPE_FLAGS_NONE;
  unsigned int i;
  gboolean has_joystick_axes = FALSE;
  gboolean has_joystick_buttons = FALSE;

  /* Some properties let us be fairly sure about a device */
  if (test_bit (INPUT_PROP_ACCELEROMETER, caps->props))
    {
      g_debug ("INPUT_PROP_ACCELEROMETER => is accelerometer");
      flags |= SRT_INPUT_DEVICE_TYPE_FLAGS_ACCELEROMETER;
    }

  if (test_bit (INPUT_PROP_POINTING_STICK, caps->props))
    {
      g_debug ("INPUT_PROP_POINTING_STICK => is pointing stick");
      flags |= SRT_INPUT_DEVICE_TYPE_FLAGS_POINTING_STICK;
    }

  if (test_bit (INPUT_PROP_BUTTONPAD, caps->props)
      || test_bit (INPUT_PROP_TOPBUTTONPAD, caps->props))
    {
      g_debug ("INPUT_PROP_[TOP]BUTTONPAD => is touchpad");
      flags |= SRT_INPUT_DEVICE_TYPE_FLAGS_TOUCHPAD;
    }

  /* Devices with a stylus or pen are assumed to be graphics tablets */
  if (test_bit (BTN_STYLUS, caps->keys)
      || test_bit (BTN_TOOL_PEN, caps->keys))
    {
      g_debug ("Stylus or pen => is tablet");
      flags |= SRT_INPUT_DEVICE_TYPE_FLAGS_TABLET;
    }

  /* Devices that accept a finger touch are assumed to be touchpads or
   * touchscreens.
   *
   * In Steam we mostly only care about these as a way to
   * reject non-joysticks, so we're not very precise here yet.
   *
   * SDL assumes that TOUCH means a touchscreen and FINGER
   * means a touchpad. */
  if (flags == SRT_INPUT_DEVICE_TYPE_FLAGS_NONE
      && (test_bit (BTN_TOOL_FINGER, caps->keys)
          || test_bit (BTN_TOUCH, caps->keys)
          || test_bit (INPUT_PROP_SEMI_MT, caps->props)))
    {
      g_debug ("Finger or touch or semi-MT => is touchpad or touchscreen");

      if (test_bit (INPUT_PROP_POINTER, caps->props))
        flags |= SRT_INPUT_DEVICE_TYPE_FLAGS_TOUCHPAD;
      else
        flags |= SRT_INPUT_DEVICE_TYPE_FLAGS_TOUCHSCREEN;
    }

  /* Devices with mouse buttons are ... probably mice? */
  if (flags == SRT_INPUT_DEVICE_TYPE_FLAGS_NONE)
    {
      for (i = first_mouse_button; i <= last_mouse_button; i++)
        {
          if (test_bit (i, caps->keys))
            {
              g_debug ("Mouse button => mouse");
              flags |= SRT_INPUT_DEVICE_TYPE_FLAGS_MOUSE;
            }
        }
    }

  if (flags == SRT_INPUT_DEVICE_TYPE_FLAGS_NONE)
    {
      for (i = ABS_X; i < ABS_Z; i++)
        {
          if (!test_bit (i, caps->abs))
            break;
        }

      /* If it has 3 axes and no buttons it's probably an accelerometer. */
      if (i == ABS_Z && !test_bit (EV_KEY, caps->ev))
        {
          g_debug ("3 left axes and no buttons => accelerometer");
          flags |= SRT_INPUT_DEVICE_TYPE_FLAGS_ACCELEROMETER;
        }

      /* Same for RX..RZ (e.g. Wiimote) */
      for (i = ABS_RX; i < ABS_RZ; i++)
        {
          if (!test_bit (i, caps->abs))
            break;
        }

      if (i == ABS_RZ && !test_bit (EV_KEY, caps->ev))
        {
          g_debug ("3 right axes and no buttons => accelerometer");
          flags |= SRT_INPUT_DEVICE_TYPE_FLAGS_ACCELEROMETER;
        }
    }

  /* Bits 1 to 31 are ESC, numbers and Q to D, which SDL and udev both
   * consider to be enough to count as a fully-functioned keyboard. */
  if ((caps->keys[0] & 0xfffffffe) == 0xfffffffe)
    {
      g_debug ("First few keys => keyboard");
      flags |= SRT_INPUT_DEVICE_TYPE_FLAGS_KEYBOARD;
    }

  /* If we have *any* keys, consider it to be something a bit
   * keyboard-like. Bits 0 to 63 are all keyboard keys.
   * Make sure we stop before reaching KEY_UP which is sometimes
   * used on game controller mappings, e.g. for the Wiimote. */
  for (i = 0; i < (64 / BITS_PER_LONG); i++)
    {
      if (caps->keys[i] != 0)
        flags |= SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS;
    }

  if (caps->abs[0] & JOYSTICK_ABS_AXES)
    has_joystick_axes = TRUE;

  /* Flight stick buttons */
  for (i = first_joystick_button; i <= last_joystick_button; i++)
    {
      if (test_bit (i, caps->keys))
        has_joystick_buttons = TRUE;
    }

  /* Gamepad buttons (Xbox, PS3, etc.) */
  for (i = first_gamepad_button; i <= last_gamepad_button; i++)
    {
      if (test_bit (i, caps->keys))
        has_joystick_buttons = TRUE;
    }

  /* Gamepad digital dpad */
  for (i = first_dpad_button; i <= last_dpad_button; i++)
    {
      if (test_bit (i, caps->keys))
        has_joystick_buttons = TRUE;
    }

  /* Steering wheel gear-change buttons */
  for (i = BTN_GEAR_DOWN; i <= BTN_GEAR_UP; i++)
    {
      if (test_bit (i, caps->keys))
        has_joystick_buttons = TRUE;
    }

  /* Reserved space for extra game-controller buttons, e.g. on Corsair
   * gaming keyboards */
  for (i = first_extra_joystick_button; i <= last_extra_joystick_button; i++)
    {
      if (test_bit (i, caps->keys))
        has_joystick_buttons = TRUE;
    }

  if (test_bit (last_mouse_button, caps->keys))
    {
      /* Mice with a very large number of buttons can apparently
       * overflow into the joystick-button space, but they're still not
       * joysticks. */
      has_joystick_buttons = FALSE;
    }

  /* TODO: Do we want to consider BTN_0 up to BTN_9 to be joystick buttons?
   * libmanette and SDL look for BTN_1, udev does not.
   *
   * They're used by some game controllers, like BTN_1 and BTN_2 for the
   * Wiimote, BTN_1..BTN_9 for the SpaceTec SpaceBall and BTN_0..BTN_3
   * for Playstation dance pads, but they're also used by
   * non-game-controllers like Logitech mice. For now we entirely ignore
   * these buttons: they are not evidence that it's a joystick, but
   * neither are they evidence that it *isn't* a joystick. */

  /* We consider it to be a joystick if there is some evidence that it is,
   * and no evidence that it's something else.
   *
   * Unlike SDL, we accept devices with only axes and no buttons as a
   * possible joystick, unless they have X/Y/Z axes in which case we
   * assume they're accelerometers. */
  if ((has_joystick_buttons || has_joystick_axes)
      && (flags == SRT_INPUT_DEVICE_TYPE_FLAGS_NONE))
    {
      g_debug ("Looks like a joystick");
      flags |= SRT_INPUT_DEVICE_TYPE_FLAGS_JOYSTICK;
    }

  /* If we have *any* keys below BTN_MISC, consider it to be something
   * a bit keyboard-like, but don't rule out *also* being considered
   * to be a joystick (again for e.g. the Wiimote). */
  for (i = 0; i < (BTN_MISC / BITS_PER_LONG); i++)
    {
      if (caps->keys[i] != 0)
        flags |= SRT_INPUT_DEVICE_TYPE_FLAGS_HAS_KEYS;
    }

  /* Also non-exclusive: don't rule out a device being a joystick and
   * having a switch */
  if (test_bit (EV_SW, caps->ev))
    flags |= SRT_INPUT_DEVICE_TYPE_FLAGS_SWITCH;

  return flags;
}
