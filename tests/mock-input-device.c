/*
 * Mock input device monitor, loosely based on SDL code.
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

#include "mock-input-device.h"

#include <linux/input.h>

#include <glib-unix.h>
#include <libglnx.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/input-device.h"
#include "steam-runtime-tools/input-device-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#define VENDOR_VALVE 0x28de
#define PRODUCT_VALVE_STEAM_CONTROLLER 0x1142

/* These aren't in the real vendor/product IDs, but we add them here
 * to make the test able to distinguish. They look a bit like HID,
 * EVDE(v) and USB, if you squint. */
#define HID_MARKER 0x41D00000
#define EVDEV_MARKER 0xE7DE0000
#define USB_MARKER 0x05B00000

static void mock_input_device_iface_init (SrtInputDeviceInterface *iface);
static void mock_input_device_monitor_iface_init (SrtInputDeviceMonitorInterface *iface);

G_DEFINE_TYPE_WITH_CODE (MockInputDevice,
                         mock_input_device,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SRT_TYPE_INPUT_DEVICE,
                                                mock_input_device_iface_init))

G_DEFINE_TYPE_WITH_CODE (MockInputDeviceMonitor,
                         mock_input_device_monitor,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SRT_TYPE_INPUT_DEVICE_MONITOR,
                                                mock_input_device_monitor_iface_init))

static void
mock_input_device_init (MockInputDevice *self)
{
}

static void
mock_input_device_finalize (GObject *object)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (object);

  g_clear_pointer (&self->sys_path, g_free);
  g_clear_pointer (&self->dev_node, g_free);
  g_clear_pointer (&self->subsystem, g_free);
  g_clear_pointer (&self->udev_properties, g_strfreev);
  g_clear_pointer (&self->uevent, g_free);

  g_clear_pointer (&self->hid_ancestor.sys_path, g_free);
  g_clear_pointer (&self->hid_ancestor.uevent, g_free);

  g_clear_pointer (&self->input_ancestor.sys_path, g_free);
  g_clear_pointer (&self->input_ancestor.uevent, g_free);

  g_clear_pointer (&self->usb_device_ancestor.sys_path, g_free);
  g_clear_pointer (&self->usb_device_ancestor.uevent, g_free);

  G_OBJECT_CLASS (mock_input_device_parent_class)->finalize (object);
}

static void
mock_input_device_class_init (MockInputDeviceClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->finalize = mock_input_device_finalize;
}

static SrtInputDeviceInterfaceFlags
mock_input_device_get_interface_flags (SrtInputDevice *device)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (device);

  return self->iface_flags;
}

static const char *
mock_input_device_get_dev_node (SrtInputDevice *device)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (device);

  return self->dev_node;
}

static const char *
mock_input_device_get_subsystem (SrtInputDevice *device)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (device);

  return self->subsystem;
}

static const char *
mock_input_device_get_sys_path (SrtInputDevice *device)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (device);

  return self->sys_path;
}

static gboolean
mock_input_device_get_identity (SrtInputDevice *device,
                                unsigned int *bus_type,
                                unsigned int *vendor_id,
                                unsigned int *product_id,
                                unsigned int *version)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (device);

  if (bus_type != NULL)
    *bus_type = self->bus_type;

  if (vendor_id != NULL)
    *vendor_id = self->vendor_id;

  if (product_id != NULL)
    *product_id = self->product_id;

  if (version != NULL)
    *version = self->version;

  return TRUE;
}

static gboolean
mock_input_device_get_hid_identity (SrtInputDevice *device,
                                        unsigned int *bus_type,
                                        unsigned int *vendor_id,
                                        unsigned int *product_id,
                                        const char **name,
                                        const char **phys,
                                        const char **uniq)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (device);

  if (self->hid_ancestor.sys_path == NULL)
    return FALSE;

  if (bus_type != NULL)
    *bus_type = self->hid_ancestor.bus_type;

  if (vendor_id != NULL)
    *vendor_id = self->hid_ancestor.vendor_id;

  if (product_id != NULL)
    *product_id = self->hid_ancestor.product_id;

  if (name != NULL)
    *name = self->hid_ancestor.name;

  if (phys != NULL)
    *phys = self->hid_ancestor.phys;

  if (uniq != NULL)
    *uniq = self->hid_ancestor.uniq;

  return TRUE;
}

static gboolean
mock_input_device_get_input_identity (SrtInputDevice *device,
                                          unsigned int *bus_type,
                                          unsigned int *vendor_id,
                                          unsigned int *product_id,
                                          unsigned int *version,
                                          const char **name,
                                          const char **phys,
                                          const char **uniq)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (device);

  if (self->input_ancestor.sys_path == NULL)
    return FALSE;

  if (bus_type != NULL)
    *bus_type = self->input_ancestor.bus_type;

  if (vendor_id != NULL)
    *vendor_id = self->input_ancestor.vendor_id;

  if (product_id != NULL)
    *product_id = self->input_ancestor.product_id;

  if (version != NULL)
    *version = self->input_ancestor.version;

  if (name != NULL)
    *name = self->input_ancestor.name;

  if (phys != NULL)
    *phys = self->input_ancestor.phys;

  if (uniq != NULL)
    *uniq = self->input_ancestor.uniq;

  return TRUE;
}

static gboolean
mock_input_device_get_usb_device_identity (SrtInputDevice *device,
                                               unsigned int *vendor_id,
                                               unsigned int *product_id,
                                               unsigned int *device_version,
                                               const char **manufacturer,
                                               const char **product,
                                               const char **serial)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (device);

  if (self->usb_device_ancestor.sys_path == NULL)
    return FALSE;

  if (vendor_id != NULL)
    *vendor_id = self->usb_device_ancestor.vendor_id;

  if (product_id != NULL)
    *product_id = self->usb_device_ancestor.product_id;

  if (device_version != NULL)
    *device_version = self->usb_device_ancestor.device_version;

  if (manufacturer != NULL)
    *manufacturer = self->usb_device_ancestor.manufacturer;

  if (product != NULL)
    *product = self->usb_device_ancestor.product;

  if (serial != NULL)
    *serial = self->usb_device_ancestor.serial;

  return TRUE;
}

static gchar **
mock_input_device_dup_udev_properties (SrtInputDevice *device)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (device);

  return g_strdupv (self->udev_properties);
}

static gchar *
mock_input_device_dup_uevent (SrtInputDevice *device)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (device);

  return g_strdup (self->uevent);
}

static const SrtEvdevCapabilities *
mock_input_device_peek_event_capabilities (SrtInputDevice *device)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (device);

  return &self->evdev_caps;
}

static const char *
mock_input_device_get_hid_sys_path (SrtInputDevice *device)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (device);

  return self->hid_ancestor.sys_path;
}

static gchar *
mock_input_device_dup_hid_uevent (SrtInputDevice *device)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (device);

  return g_strdup (self->hid_ancestor.uevent);
}

static const char *
mock_input_device_get_input_sys_path (SrtInputDevice *device)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (device);

  return self->input_ancestor.sys_path;
}

static gchar *
mock_input_device_dup_input_uevent (SrtInputDevice *device)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (device);

  return g_strdup (self->input_ancestor.uevent);
}

static const char *
mock_input_device_get_usb_device_sys_path (SrtInputDevice *device)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (device);

  return self->usb_device_ancestor.sys_path;
}

static gchar *
mock_input_device_dup_usb_device_uevent (SrtInputDevice *device)
{
  MockInputDevice *self = MOCK_INPUT_DEVICE (device);

  return g_strdup (self->usb_device_ancestor.uevent);
}

static void
mock_input_device_iface_init (SrtInputDeviceInterface *iface)
{
#define IMPLEMENT(x) iface->x = mock_input_device_ ## x

  IMPLEMENT (get_interface_flags);
  IMPLEMENT (get_dev_node);
  IMPLEMENT (get_sys_path);
  IMPLEMENT (get_subsystem);
  IMPLEMENT (dup_udev_properties);
  IMPLEMENT (dup_uevent);
  IMPLEMENT (get_identity);
  IMPLEMENT (peek_event_capabilities);

  IMPLEMENT (get_hid_sys_path);
  IMPLEMENT (dup_hid_uevent);
  IMPLEMENT (get_hid_identity);
  IMPLEMENT (get_input_sys_path);
  IMPLEMENT (dup_input_uevent);
  IMPLEMENT (get_input_identity);
  IMPLEMENT (get_usb_device_sys_path);
  IMPLEMENT (dup_usb_device_uevent);
  IMPLEMENT (get_usb_device_identity);

#undef IMPLEMENT
}

MockInputDevice *
mock_input_device_new (void)
{
  return g_object_new (MOCK_TYPE_INPUT_DEVICE,
                       NULL);
}

typedef enum
{
  PROP_0,
  PROP_FLAGS,
  PROP_IS_ACTIVE,
  N_PROPERTIES
} Property;

static void
mock_input_device_monitor_init (MockInputDeviceMonitor *self)
{
  self->monitor_context = g_main_context_ref_thread_default ();
  self->state = NOT_STARTED;
  self->devices = g_hash_table_new_full (NULL,
                                         NULL,
                                         g_object_unref,
                                         NULL);
}

static void
mock_input_device_monitor_get_property (GObject *object,
                                        guint prop_id,
                                        GValue *value,
                                        GParamSpec *pspec)
{
  MockInputDeviceMonitor *self = MOCK_INPUT_DEVICE_MONITOR (object);

  switch ((Property) prop_id)
    {
      case PROP_IS_ACTIVE:
        g_value_set_boolean (value, (self->state == STARTED));
        break;

      case PROP_FLAGS:
        g_value_set_flags (value, self->flags);
        break;

      case PROP_0:
      case N_PROPERTIES:
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
mock_input_device_monitor_set_property (GObject *object,
                                        guint prop_id,
                                        const GValue *value,
                                        GParamSpec *pspec)
{
  MockInputDeviceMonitor *self = MOCK_INPUT_DEVICE_MONITOR (object);

  switch ((Property) prop_id)
    {
      case PROP_FLAGS:
        /* Construct-only */
        g_return_if_fail (self->flags == 0);
        self->flags = g_value_get_flags (value);
        break;

      case PROP_IS_ACTIVE:
      case PROP_0:
      case N_PROPERTIES:
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
mock_input_device_monitor_dispose (GObject *object)
{
  srt_input_device_monitor_stop (SRT_INPUT_DEVICE_MONITOR (object));
  G_OBJECT_CLASS (mock_input_device_monitor_parent_class)->dispose (object);
}

static void
mock_input_device_monitor_finalize (GObject *object)
{
  srt_input_device_monitor_stop (SRT_INPUT_DEVICE_MONITOR (object));
  G_OBJECT_CLASS (mock_input_device_monitor_parent_class)->finalize (object);
}

static void
mock_input_device_monitor_class_init (MockInputDeviceMonitorClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = mock_input_device_monitor_get_property;
  object_class->set_property = mock_input_device_monitor_set_property;
  object_class->dispose = mock_input_device_monitor_dispose;
  object_class->finalize = mock_input_device_monitor_finalize;

  g_object_class_override_property (object_class, PROP_IS_ACTIVE, "is-active");
  g_object_class_override_property (object_class, PROP_FLAGS, "flags");
}

MockInputDeviceMonitor *
mock_input_device_monitor_new (SrtInputDeviceMonitorFlags flags)
{
  return g_object_new (MOCK_TYPE_INPUT_DEVICE_MONITOR,
                       "flags", flags,
                       NULL);
}

void
mock_input_device_monitor_add (MockInputDeviceMonitor *self,
                               MockInputDevice *device)
{
  /* Ref it first in case the last ref to it is in the hash table */
  g_autoptr(MockInputDevice) ref = g_object_ref (device);

  mock_input_device_monitor_remove (self, device);

  g_hash_table_add (self->devices, g_steal_pointer (&ref));

  _srt_input_device_monitor_emit_added (SRT_INPUT_DEVICE_MONITOR (self),
                                        SRT_INPUT_DEVICE (device));
}

void
mock_input_device_monitor_remove (MockInputDeviceMonitor *self,
                                  MockInputDevice *device)
{
  if (g_hash_table_steal (self->devices, device))
    {
      _srt_input_device_monitor_emit_removed (SRT_INPUT_DEVICE_MONITOR (self),
                                              SRT_INPUT_DEVICE (device));
      g_object_unref (device);
    }
}

static void
add_steam_controller (MockInputDeviceMonitor *self,
                      const char *tail,
                      MockInputDevice **dev_out)
{
  g_autoptr(MockInputDevice) device = mock_input_device_new ();
  g_autoptr(GPtrArray) arr = g_ptr_array_new_full (1, g_free);

  device->iface_flags = (SRT_INPUT_DEVICE_INTERFACE_FLAGS_EVENT
                         | SRT_INPUT_DEVICE_INTERFACE_FLAGS_READABLE
                         | SRT_INPUT_DEVICE_INTERFACE_FLAGS_READ_WRITE);
  device->dev_node = g_strdup_printf ("/dev/input/event%s", tail);
  device->sys_path = g_strdup_printf ("/sys/devices/mock/usb/hid/input/input0/event%s",
                                      tail);
  device->subsystem = g_strdup ("input");
  device->uevent = g_strdup ("ONE=1\nTWO=2\n");

  device->hid_ancestor.sys_path = g_strdup ("/sys/devices/mock/usb/hid");
  device->hid_ancestor.uevent = g_strdup ("HID=yes\n");

  device->input_ancestor.sys_path = g_strdup ("/sys/devices/mock/usb/hid/input");
  device->input_ancestor.uevent = g_strdup ("INPUT=yes\n");

  device->usb_device_ancestor.sys_path = g_strdup ("/sys/devices/mock/usb");
  device->usb_device_ancestor.uevent = g_strdup ("USB=usb_device\n");

  g_ptr_array_add (arr, g_strdup ("ID_INPUT_JOYSTICK=1"));
  g_ptr_array_add (arr, NULL);
  device->udev_properties = (gchar **) g_ptr_array_free (g_steal_pointer (&arr), FALSE);

  /* This is a semi-realistic Steam Controller. */
  device->bus_type = BUS_USB;
  device->vendor_id = VENDOR_VALVE;
  device->product_id = PRODUCT_VALVE_STEAM_CONTROLLER;
  device->version = 0x0111;
  /* We don't set all the bits, just enough to be vaguely realistic */
  set_bit (EV_KEY, device->evdev_caps.ev);
  set_bit (EV_ABS, device->evdev_caps.ev);
  set_bit (BTN_A, device->evdev_caps.keys);
  set_bit (BTN_B, device->evdev_caps.keys);
  set_bit (BTN_X, device->evdev_caps.keys);
  set_bit (BTN_Y, device->evdev_caps.keys);
  set_bit (ABS_X, device->evdev_caps.abs);
  set_bit (ABS_Y, device->evdev_caps.abs);
  set_bit (ABS_RX, device->evdev_caps.abs);
  set_bit (ABS_RY, device->evdev_caps.abs);

  /* The part in square brackets isn't present on the real device, but
   * makes this test more thorough by letting us distinguish. */
  device->hid_ancestor.name = g_strdup ("Valve Software Steam Controller");
  device->hid_ancestor.phys = g_strdup ("[hid]usb-0000:00:14.0-1.2/input1");
  device->hid_ancestor.uniq = g_strdup ("");
  device->hid_ancestor.bus_type = HID_MARKER | BUS_USB;
  device->hid_ancestor.vendor_id = HID_MARKER | VENDOR_VALVE;
  device->hid_ancestor.product_id = HID_MARKER | PRODUCT_VALVE_STEAM_CONTROLLER;

  device->input_ancestor.name = g_strdup ("Wireless Steam Controller");
  device->input_ancestor.phys = g_strdup ("[input]usb-0000:00:14.0-1.2/input1");
  device->input_ancestor.uniq = g_strdup ("12345678");
  device->input_ancestor.bus_type = EVDEV_MARKER | BUS_USB;
  device->input_ancestor.vendor_id = EVDEV_MARKER | VENDOR_VALVE;
  device->input_ancestor.product_id = EVDEV_MARKER | PRODUCT_VALVE_STEAM_CONTROLLER;
  device->input_ancestor.version = EVDEV_MARKER | 0x0111;

  device->usb_device_ancestor.vendor_id = USB_MARKER | VENDOR_VALVE;
  device->usb_device_ancestor.product_id = USB_MARKER | PRODUCT_VALVE_STEAM_CONTROLLER;
  device->usb_device_ancestor.device_version = USB_MARKER | 0x0001;
  device->usb_device_ancestor.manufacturer = g_strdup ("Valve Software");
  device->usb_device_ancestor.product = g_strdup ("Steam Controller");
  device->usb_device_ancestor.serial = NULL;

  mock_input_device_monitor_add (self, device);

  if (dev_out != NULL)
    *dev_out = g_steal_pointer (&device);
}

static gboolean
briefly_add_steam_controller_in_idle_cb (gpointer user_data)
{
  MockInputDeviceMonitor *self = MOCK_INPUT_DEVICE_MONITOR (user_data);
  g_autoptr(MockInputDevice) device = NULL;

  add_steam_controller (self, "-connected-briefly", &device);
  mock_input_device_monitor_remove (self, device);
  return G_SOURCE_REMOVE;
}

static gboolean
enumerate_cb (gpointer user_data)
{
  MockInputDeviceMonitor *self = MOCK_INPUT_DEVICE_MONITOR (user_data);

  add_steam_controller (self, "0", NULL);
  _srt_input_device_monitor_emit_all_for_now (SRT_INPUT_DEVICE_MONITOR (self));
  return G_SOURCE_REMOVE;
}

static gboolean
mock_input_device_monitor_start (SrtInputDeviceMonitor *monitor,
                                 GError **error)
{
  MockInputDeviceMonitor *self = MOCK_INPUT_DEVICE_MONITOR (monitor);

  g_return_val_if_fail (MOCK_IS_INPUT_DEVICE_MONITOR (monitor), FALSE);
  g_return_val_if_fail (self->state == NOT_STARTED, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  self->state = STARTED;

  /* Make sure the signals for the initial batch of devices are emitted in
   * the correct main-context */
  g_main_context_invoke_full (self->monitor_context, G_PRIORITY_DEFAULT,
                              enumerate_cb, g_object_ref (self),
                              g_object_unref);

  if ((self->flags & SRT_INPUT_DEVICE_MONITOR_FLAGS_ONCE) == 0)
    {
      GSource *source = g_idle_source_new ();

      g_source_set_callback (source,
                             briefly_add_steam_controller_in_idle_cb,
                             self, NULL);
      g_source_set_priority (source, G_PRIORITY_DEFAULT);
      g_source_attach (source, self->monitor_context);
      self->sources = g_list_prepend (self->sources, g_steal_pointer (&source));
    }

  return TRUE;
}

static void
mock_input_device_monitor_stop (SrtInputDeviceMonitor *monitor)
{
  MockInputDeviceMonitor *self = MOCK_INPUT_DEVICE_MONITOR (monitor);

  self->state = STOPPED;

  while (self->sources != NULL)
    {
      GSource *s = self->sources->data;

      self->sources = g_list_delete_link (self->sources, self->sources);
      g_source_destroy (s);
      g_source_unref (s);
    }

  g_clear_pointer (&self->monitor_context, g_main_context_unref);
  g_clear_pointer (&self->devices, g_hash_table_unref);
}

static void
mock_input_device_monitor_iface_init (SrtInputDeviceMonitorInterface *iface)
{
#define IMPLEMENT(x) iface->x = mock_input_device_monitor_ ## x
  IMPLEMENT (start);
  IMPLEMENT (stop);
#undef IMPLEMENT
}
