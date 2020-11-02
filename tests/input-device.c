/*
 * Copyright © 2020 Collabora Ltd.
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

#include <libglnx.h>

#include <steam-runtime-tools/steam-runtime-tools.h>

#include <linux/input.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>

#include "steam-runtime-tools/utils-internal.h"
#include "mock-input-device.h"
#include "test-utils.h"

typedef struct
{
  enum { MOCK, DIRECT, UDEV } type;
} Config;

static const Config defconfig =
{
  .type = MOCK,
};

static const Config direct_config =
{
  .type = DIRECT,
};

static const Config udev_config =
{
  .type = UDEV,
};

typedef struct
{
  const Config *config;
  GMainContext *monitor_context;
  GPtrArray *log;
  gboolean skipped;
} Fixture;

static void
setup (Fixture *f,
       gconstpointer context)
{
  const Config *config = context;
  GStatBuf sb;

  if (config == NULL)
    f->config = &defconfig;
  else
    f->config = config;

  if (f->config->type == DIRECT && g_stat ("/dev/input", &sb) != 0)
    {
      g_test_skip ("/dev/input not available");
      f->skipped = TRUE;
    }

  f->log = g_ptr_array_new_with_free_func (g_free);
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_clear_pointer (&f->log, g_ptr_array_unref);
  g_clear_pointer (&f->monitor_context, g_main_context_unref);
}

static void
test_input_device_identity_from_hid_uevent (Fixture *f,
                                            gconstpointer context)
{
  static const char text[] =
    "DRIVER=hid-steam\n"
    "HID_ID=0003:000028DE:00001142\n"
    "HID_NAME=Valve Software Steam Controller\n"
    "HID_PHYS=usb-0000:00:14.0-1.1/input0\n"
    "HID_UNIQ=serialnumber\n"
    "MODALIAS=hid:b0003g0001v000028DEp00001142\n";
  guint32 bus_type, vendor_id, product_id;
  g_autofree gchar *name = NULL;
  g_autofree gchar *phys = NULL;
  g_autofree gchar *uniq = NULL;

  g_assert_true (_srt_get_identity_from_hid_uevent (text,
                                                    &bus_type,
                                                    &vendor_id,
                                                    &product_id,
                                                    &name,
                                                    &phys,
                                                    &uniq));
  g_assert_cmphex (bus_type, ==, 0x0003);
  g_assert_cmphex (vendor_id, ==, 0x28de);
  g_assert_cmphex (product_id, ==, 0x1142);
  g_assert_cmpstr (name, ==, "Valve Software Steam Controller");
  g_assert_cmpstr (phys, ==, "usb-0000:00:14.0-1.1/input0");
  /* Real Steam Controllers don't expose a serial number here, but it's
   * a better test if we include one */
  g_assert_cmpstr (uniq, ==, "serialnumber");
}

#define VENDOR_SONY 0x0268
#define PRODUCT_SONY_PS3 0x054c

#define VENDOR_VALVE 0x28de
#define PRODUCT_VALVE_STEAM_CONTROLLER 0x1142

/* These aren't in the real vendor/product IDs, but we add them here
 * to make the test able to distinguish. They look a bit like HID,
 * EVDE(v) and USB, if you squint. */
#define HID_MARKER 0x41D00000
#define EVDEV_MARKER 0xE7DE0000
#define USB_MARKER 0x05B00000

/* The test below assumes EV_MAX doesn't increase its value */
G_STATIC_ASSERT (EV_MAX <= 31);

static void
test_input_device_usb (Fixture *f,
                       gconstpointer context)
{
  g_autoptr(MockInputDevice) mock_device = mock_input_device_new ();
  SrtInputDevice *device = SRT_INPUT_DEVICE (mock_device);
  SrtInputDeviceInterface *iface = SRT_INPUT_DEVICE_GET_INTERFACE (device);
  g_auto(GStrv) udev_properties = NULL;
  g_autofree gchar *uevent = NULL;
  g_autofree gchar *hid_uevent = NULL;
  g_autofree gchar *input_uevent = NULL;
  g_autofree gchar *usb_uevent = NULL;
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
  } identity = { 1, 1, 1, 1 };
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    const char *name;
    const char *phys;
    const char *uniq;
  } hid_identity = { 1, 1, 1, "x", "x", "x" };
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
    const char *name;
    const char *phys;
    const char *uniq;
  } input_identity = { 1, 1, 1, 1, "x", "x", "x" };
  struct
  {
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
    const char *manufacturer;
    const char *product;
    const char *serial;
  } usb_identity = { 1, 1, 1, "x", "x", "x" };
  unsigned long evbits;
  /* Initialize the first two to nonzero to check that they get zeroed */
  unsigned long bits[LONGS_FOR_BITS (HIGHEST_EVENT_CODE)] = { 0xa, 0xb };
  gsize i;

  mock_device->iface_flags = (SRT_INPUT_DEVICE_INTERFACE_FLAGS_EVENT
                              | SRT_INPUT_DEVICE_INTERFACE_FLAGS_READABLE);
  mock_device->dev_node = g_strdup ("/dev/input/event0");
  mock_device->sys_path = g_strdup ("/sys/devices/mock/usb/hid/input/input0/event0");
  mock_device->subsystem = g_strdup ("input");
  mock_device->udev_properties = g_new0 (gchar *, 2);
  mock_device->udev_properties[0] = g_strdup ("ID_INPUT_JOYSTICK=1");
  mock_device->udev_properties[1] = NULL;
  mock_device->uevent = g_strdup ("A=a\nB=b\n");
  /* This is a semi-realistic PS3 controller. */
  mock_device->bus_type = BUS_USB;
  mock_device->vendor_id = VENDOR_SONY;
  mock_device->product_id = PRODUCT_SONY_PS3;
  mock_device->version = 0x8111;

  /* We don't set all the bits, just enough to be vaguely realistic */
  set_bit (EV_KEY, mock_device->evdev_caps.ev);
  set_bit (EV_ABS, mock_device->evdev_caps.ev);
  set_bit (BTN_A, mock_device->evdev_caps.keys);
  set_bit (BTN_B, mock_device->evdev_caps.keys);
  set_bit (BTN_TL, mock_device->evdev_caps.keys);
  set_bit (BTN_TR, mock_device->evdev_caps.keys);
  set_bit (ABS_X, mock_device->evdev_caps.abs);
  set_bit (ABS_Y, mock_device->evdev_caps.abs);
  set_bit (ABS_RX, mock_device->evdev_caps.abs);
  set_bit (ABS_RY, mock_device->evdev_caps.abs);

  g_debug ("Mock device capabilities:");
  _srt_evdev_capabilities_dump (&mock_device->evdev_caps);

  mock_device->hid_ancestor.sys_path = g_strdup ("/sys/devices/mock/usb/hid");
  mock_device->hid_ancestor.uevent = g_strdup ("HID=yes\n");
  /* The part in square brackets isn't present on the real device, but
   * makes this test more thorough by letting us distinguish. */
  mock_device->hid_ancestor.name = g_strdup ("Sony PLAYSTATION(R)3 Controller [hid]");
  mock_device->hid_ancestor.phys = g_strdup ("usb-0000:00:14.0-1/input0");
  mock_device->hid_ancestor.uniq = g_strdup ("12:34:56:78:9a:bc");
  mock_device->hid_ancestor.bus_type = HID_MARKER | BUS_USB;
  mock_device->hid_ancestor.vendor_id = HID_MARKER | VENDOR_SONY;
  mock_device->hid_ancestor.product_id = HID_MARKER | PRODUCT_SONY_PS3;

  mock_device->input_ancestor.sys_path = g_strdup ("/sys/devices/mock/usb/hid/input");
  mock_device->input_ancestor.uevent = g_strdup ("INPUT=yes\n");
  mock_device->input_ancestor.name = g_strdup ("Sony PLAYSTATION(R)3 Controller [input]");
  mock_device->input_ancestor.phys = NULL;
  mock_device->input_ancestor.uniq = NULL;
  mock_device->input_ancestor.bus_type = EVDEV_MARKER | BUS_USB;
  mock_device->input_ancestor.vendor_id = EVDEV_MARKER | VENDOR_SONY;
  mock_device->input_ancestor.product_id = EVDEV_MARKER | PRODUCT_SONY_PS3;
  mock_device->input_ancestor.version = EVDEV_MARKER | 0x8111;

  mock_device->usb_device_ancestor.sys_path = g_strdup ("/sys/devices/mock/usb");
  mock_device->usb_device_ancestor.uevent = g_strdup ("USB=usb_device\n");
  mock_device->usb_device_ancestor.vendor_id = USB_MARKER | VENDOR_SONY;
  mock_device->usb_device_ancestor.product_id = USB_MARKER | PRODUCT_SONY_PS3;
  mock_device->usb_device_ancestor.device_version = USB_MARKER | 0x0100;
  mock_device->usb_device_ancestor.manufacturer = g_strdup ("Sony");
  mock_device->usb_device_ancestor.product = g_strdup ("PLAYSTATION(R)3 Controller");
  mock_device->usb_device_ancestor.serial = NULL;

  g_assert_cmpuint (srt_input_device_get_interface_flags (device), ==,
                    SRT_INPUT_DEVICE_INTERFACE_FLAGS_EVENT
                    | SRT_INPUT_DEVICE_INTERFACE_FLAGS_READABLE);
  g_assert_cmpstr (srt_input_device_get_dev_node (device), ==,
                   "/dev/input/event0");
  g_assert_cmpstr (srt_input_device_get_sys_path (device), ==,
                   "/sys/devices/mock/usb/hid/input/input0/event0");
  g_assert_cmpstr (srt_input_device_get_subsystem (device), ==, "input");

  uevent = srt_input_device_dup_uevent (device);
  g_assert_cmpstr (uevent, ==, "A=a\nB=b\n");

  g_assert_cmpstr (srt_input_device_get_hid_sys_path (device), ==,
                   "/sys/devices/mock/usb/hid");
  hid_uevent = srt_input_device_dup_hid_uevent (device);
  g_assert_cmpstr (hid_uevent, ==, "HID=yes\n");

  g_assert_cmpstr (srt_input_device_get_input_sys_path (device), ==,
                   "/sys/devices/mock/usb/hid/input");
  input_uevent = srt_input_device_dup_input_uevent (device);
  g_assert_cmpstr (input_uevent, ==, "INPUT=yes\n");

  g_assert_cmpstr (srt_input_device_get_usb_device_sys_path (device), ==,
                   "/sys/devices/mock/usb");
  usb_uevent = srt_input_device_dup_usb_device_uevent (device);
  g_assert_cmpstr (usb_uevent, ==, "USB=usb_device\n");

  udev_properties = srt_input_device_dup_udev_properties (device);
  g_assert_nonnull (udev_properties);
  g_assert_cmpstr (udev_properties[0], ==, "ID_INPUT_JOYSTICK=1");
  g_assert_cmpstr (udev_properties[1], ==, NULL);

  g_assert_true (srt_input_device_get_identity (device,
                                                NULL, NULL, NULL, NULL));
  g_assert_true (srt_input_device_get_identity (device,
                                                &identity.bus_type,
                                                &identity.vendor_id,
                                                &identity.product_id,
                                                &identity.version));
  g_assert_cmphex (identity.bus_type, ==, BUS_USB);
  g_assert_cmphex (identity.vendor_id, ==, VENDOR_SONY);
  g_assert_cmphex (identity.product_id, ==, PRODUCT_SONY_PS3);
  g_assert_cmphex (identity.version, ==, 0x8111);

  g_assert_true (srt_input_device_get_hid_identity (device,
                                                    NULL, NULL, NULL,
                                                    NULL, NULL, NULL));
  g_assert_true (srt_input_device_get_hid_identity (device,
                                                    &hid_identity.bus_type,
                                                    &hid_identity.vendor_id,
                                                    &hid_identity.product_id,
                                                    &hid_identity.name,
                                                    &hid_identity.phys,
                                                    &hid_identity.uniq));
  g_assert_cmphex (hid_identity.bus_type, ==, HID_MARKER | BUS_USB);
  g_assert_cmphex (hid_identity.vendor_id, ==, HID_MARKER | VENDOR_SONY);
  g_assert_cmphex (hid_identity.product_id, ==, HID_MARKER | PRODUCT_SONY_PS3);
  g_assert_cmpstr (hid_identity.name, ==, "Sony PLAYSTATION(R)3 Controller [hid]");
  g_assert_cmpstr (hid_identity.phys, ==, "usb-0000:00:14.0-1/input0");
  g_assert_cmpstr (hid_identity.uniq, ==, "12:34:56:78:9a:bc");

  g_assert_true (srt_input_device_get_input_identity (device,
                                                      NULL, NULL, NULL, NULL,
                                                      NULL, NULL, NULL));
  g_assert_true (srt_input_device_get_input_identity (device,
                                                      &input_identity.bus_type,
                                                      &input_identity.vendor_id,
                                                      &input_identity.product_id,
                                                      &input_identity.version,
                                                      &input_identity.name,
                                                      &input_identity.phys,
                                                      &input_identity.uniq));
  g_assert_cmphex (input_identity.bus_type, ==, EVDEV_MARKER | BUS_USB);
  g_assert_cmphex (input_identity.vendor_id, ==, EVDEV_MARKER | VENDOR_SONY);
  g_assert_cmphex (input_identity.product_id, ==, EVDEV_MARKER | PRODUCT_SONY_PS3);
  g_assert_cmphex (input_identity.version, ==, EVDEV_MARKER | 0x8111);
  g_assert_cmpstr (input_identity.name, ==, "Sony PLAYSTATION(R)3 Controller [input]");
  g_assert_cmpstr (input_identity.phys, ==, NULL);
  g_assert_cmpstr (input_identity.uniq, ==, NULL);

  g_assert_true (srt_input_device_get_usb_device_identity (device,
                                                           NULL, NULL, NULL,
                                                           NULL, NULL, NULL));
  g_assert_true (srt_input_device_get_usb_device_identity (device,
                                                           &usb_identity.vendor_id,
                                                           &usb_identity.product_id,
                                                           &usb_identity.version,
                                                           &usb_identity.manufacturer,
                                                           &usb_identity.product,
                                                           &usb_identity.serial));
  g_assert_cmphex (usb_identity.vendor_id, ==, USB_MARKER | VENDOR_SONY);
  g_assert_cmphex (usb_identity.product_id, ==, USB_MARKER | PRODUCT_SONY_PS3);
  g_assert_cmpstr (usb_identity.manufacturer, ==, "Sony");
  g_assert_cmpstr (usb_identity.product, ==, "PLAYSTATION(R)3 Controller");
  g_assert_cmpstr (usb_identity.serial, ==, NULL);

  g_debug ("Capabilities internally:");
  _srt_evdev_capabilities_dump (iface->peek_event_capabilities (device));

  /* This assumes EV_MAX doesn't increase its value */
  g_assert_cmpuint (srt_input_device_get_event_types (device, NULL, 0),
                    ==, 1);
  g_assert_cmpuint (srt_input_device_get_event_types (device, &evbits, 1),
                    ==, 1);
  g_assert_cmphex (evbits, ==, mock_device->evdev_caps.ev[0]);
  g_assert_cmphex (evbits & (1 << EV_KEY), ==, 1 << EV_KEY);
  g_assert_cmphex (evbits & (1 << EV_ABS), ==, 1 << EV_ABS);
  g_assert_cmphex (evbits & (1 << EV_SW), ==, 0);
  g_assert_cmphex (evbits & (1 << EV_MSC), ==, 0);
  g_assert_cmpint (srt_input_device_has_event_type (device, EV_KEY), ==, TRUE);
  g_assert_cmpint (srt_input_device_has_event_type (device, EV_SW), ==, FALSE);
  g_assert_cmpint (srt_input_device_has_event_capability (device, 0, EV_KEY),
                   ==, TRUE);
  g_assert_cmpint (srt_input_device_has_event_capability (device, 0, EV_SW),
                   ==, FALSE);

  g_assert_cmpuint (srt_input_device_get_event_capabilities (device, 0,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    ==, 1);
  g_assert_cmphex (bits[0], ==, evbits);

  for (i = 1; i < G_N_ELEMENTS (bits); i++)
    g_assert_cmphex (bits[i], ==, 0);

  g_assert_cmpuint (srt_input_device_get_event_capabilities (device, EV_KEY,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    >, 1);
  /* Low KEY_ codes are keyboard keys, which we don't have */
  g_assert_cmphex (bits[0], ==, 0);
  g_assert_cmpint (test_bit (BTN_A, bits), ==, 1);
  g_assert_cmpint (test_bit (BTN_STYLUS, bits), ==, 0);
  g_assert_cmpint (test_bit (KEY_SEMICOLON, bits), ==, 0);
  g_assert_cmpmem (bits,
                   MIN (sizeof (bits), sizeof (mock_device->evdev_caps.keys)),
                   mock_device->evdev_caps.keys,
                   MIN (sizeof (bits), sizeof (mock_device->evdev_caps.keys)));

  /* ABS axes also match */
  g_assert_cmpuint (srt_input_device_get_event_capabilities (device, EV_ABS,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    >=, 1);
  g_assert_cmpint (test_bit (ABS_X, bits), ==, 1);
  g_assert_cmpint (test_bit (ABS_Z, bits), ==, 0);
  g_assert_cmpmem (bits,
                   MIN (sizeof (bits), sizeof (mock_device->evdev_caps.abs)),
                   mock_device->evdev_caps.abs,
                   MIN (sizeof (bits), sizeof (mock_device->evdev_caps.abs)));

  /* REL axes also match (in fact we don't have any, but we still store
   * the bitfield) */
  g_assert_cmpuint (srt_input_device_get_event_capabilities (device, EV_REL,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    >=, 1);
  g_assert_cmpmem (bits,
                   MIN (sizeof (bits), sizeof (mock_device->evdev_caps.rel)),
                   mock_device->evdev_caps.rel,
                   MIN (sizeof (bits), sizeof (mock_device->evdev_caps.rel)));

  /* We don't support EV_SW */
  g_assert_cmpuint (srt_input_device_get_event_capabilities (device, EV_SW,
                                                             bits,
                                                             G_N_ELEMENTS (bits)),
                    ==, 0);

  for (i = 1; i < G_N_ELEMENTS (bits); i++)
    g_assert_cmphex (bits[i], ==, 0);
}

static gboolean
in_monitor_main_context (Fixture *f)
{
  if (f->monitor_context == NULL)
    return g_main_context_is_owner (g_main_context_default ());

  return g_main_context_is_owner (f->monitor_context);
}

static void
device_added_cb (SrtInputDeviceMonitor *monitor,
                 SrtInputDevice *device,
                 gpointer user_data)
{
  Fixture *f = user_data;
  g_autofree gchar *message = NULL;
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
  } identity = { 1, 1, 1, 1 };
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    const char *name;
    const char *phys;
    const char *uniq;
  } hid_identity = { 1, 1, 1, "x", "x", "x" };
  struct
  {
    unsigned int bus_type;
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
    const char *name;
    const char *phys;
    const char *uniq;
  } input_identity = { 1, 1, 1, 1, "x", "x", "x" };
  struct
  {
    unsigned int vendor_id;
    unsigned int product_id;
    unsigned int version;
    const char *manufacturer;
    const char *product;
    const char *serial;
  } usb_identity = { 1, 1, 1, "x", "x", "x" };

  g_assert_true (SRT_IS_INPUT_DEVICE_MONITOR (monitor));
  g_assert_true (SRT_IS_INPUT_DEVICE (device));

  message = g_strdup_printf ("added device: %s",
                             srt_input_device_get_dev_node (device));
  g_debug ("%s: %s", G_OBJECT_TYPE_NAME (monitor), message);

  if (srt_input_device_get_identity (device,
                                     &identity.bus_type,
                                     &identity.vendor_id,
                                     &identity.product_id,
                                     &identity.version))
    {
      g_assert_true (srt_input_device_get_identity (device,
                                                    NULL, NULL, NULL, NULL));
    }
  else
    {
      g_assert_false (srt_input_device_get_identity (device,
                                                     NULL, NULL, NULL, NULL));
      /* previous contents are untouched */
      g_assert_cmphex (identity.bus_type, ==, 1);
      g_assert_cmphex (identity.vendor_id, ==, 1);
      g_assert_cmphex (identity.product_id, ==, 1);
      g_assert_cmphex (identity.version, ==, 1);
    }

  if (srt_input_device_get_hid_identity (device,
                                         &hid_identity.bus_type,
                                         &hid_identity.vendor_id,
                                         &hid_identity.product_id,
                                         &hid_identity.name,
                                         &hid_identity.phys,
                                         &hid_identity.uniq))
    {
      g_assert_true (srt_input_device_get_hid_identity (device,
                                                        NULL, NULL, NULL,
                                                        NULL, NULL, NULL));
    }
  else
    {
      g_assert_false (srt_input_device_get_hid_identity (device,
                                                         NULL, NULL, NULL,
                                                         NULL, NULL, NULL));
      /* previous contents are untouched */
      g_assert_cmphex (hid_identity.bus_type, ==, 1);
      g_assert_cmphex (hid_identity.vendor_id, ==, 1);
      g_assert_cmphex (hid_identity.product_id, ==, 1);
      g_assert_cmpstr (hid_identity.name, ==, "x");
      g_assert_cmpstr (hid_identity.phys, ==, "x");
      g_assert_cmpstr (hid_identity.uniq, ==, "x");
    }

  if (srt_input_device_get_input_identity (device,
                                           &input_identity.bus_type,
                                           &input_identity.vendor_id,
                                           &input_identity.product_id,
                                           &input_identity.version,
                                           &input_identity.name,
                                           &input_identity.phys,
                                           &input_identity.uniq))
    {
      g_assert_true (srt_input_device_get_input_identity (device,
                                                          NULL, NULL, NULL, NULL,
                                                          NULL, NULL, NULL));
    }
  else
    {
      g_assert_false (srt_input_device_get_input_identity (device,
                                                           NULL, NULL, NULL, NULL,
                                                           NULL, NULL, NULL));
      /* previous contents are untouched */
      g_assert_cmphex (input_identity.bus_type, ==, 1);
      g_assert_cmphex (input_identity.vendor_id, ==, 1);
      g_assert_cmphex (input_identity.product_id, ==, 1);
      g_assert_cmphex (input_identity.version, ==, 1);
      g_assert_cmpstr (input_identity.name, ==, "x");
      g_assert_cmpstr (input_identity.phys, ==, "x");
      g_assert_cmpstr (input_identity.uniq, ==, "x");
    }

  if (srt_input_device_get_usb_device_identity (device,
                                                &usb_identity.vendor_id,
                                                &usb_identity.product_id,
                                                &usb_identity.version,
                                                &usb_identity.manufacturer,
                                                &usb_identity.product,
                                                &usb_identity.serial))
    {
      g_assert_true (srt_input_device_get_usb_device_identity (device,
                                                               NULL, NULL, NULL,
                                                               NULL, NULL, NULL));
    }
  else
    {
      g_assert_false (srt_input_device_get_usb_device_identity (device,
                                                                NULL, NULL, NULL,
                                                                NULL, NULL, NULL));
      /* previous contents are untouched */
      g_assert_cmphex (usb_identity.vendor_id, ==, 1);
      g_assert_cmphex (usb_identity.product_id, ==, 1);
      g_assert_cmphex (usb_identity.version, ==, 1);
      g_assert_cmpstr (usb_identity.manufacturer, ==, "x");
      g_assert_cmpstr (usb_identity.product, ==, "x");
      g_assert_cmpstr (usb_identity.serial, ==, "x");
    }

  /* For the mock device monitor, we know exactly what to expect, so
   * we can compare the expected log with what actually happened. For
   * real device monitors, we don't know what's physically present,
   * so we have to just emit debug messages. */
  if (f->config->type == MOCK)
    {
      g_autofree gchar *uevent = NULL;
      g_autofree gchar *hid_uevent = NULL;
      g_autofree gchar *input_uevent = NULL;
      g_autofree gchar *usb_uevent = NULL;
      g_auto(GStrv) udev_properties = NULL;
      unsigned long evbits;
      unsigned long bits[LONGS_FOR_BITS (HIGHEST_EVENT_CODE)];
      gsize i;

      g_assert_cmpuint (srt_input_device_get_interface_flags (device), ==,
                        SRT_INPUT_DEVICE_INTERFACE_FLAGS_EVENT
                        | SRT_INPUT_DEVICE_INTERFACE_FLAGS_READABLE
                        | SRT_INPUT_DEVICE_INTERFACE_FLAGS_READ_WRITE);

      g_assert_cmphex (identity.bus_type, ==, BUS_USB);
      g_assert_cmphex (identity.vendor_id, ==, VENDOR_VALVE);
      g_assert_cmphex (identity.product_id, ==, PRODUCT_VALVE_STEAM_CONTROLLER);
      g_assert_cmphex (identity.version, ==, 0x0111);

      g_assert_cmphex (hid_identity.bus_type, ==, HID_MARKER | BUS_USB);
      g_assert_cmphex (hid_identity.vendor_id, ==, HID_MARKER | VENDOR_VALVE);
      g_assert_cmphex (hid_identity.product_id, ==, HID_MARKER | PRODUCT_VALVE_STEAM_CONTROLLER);
      g_assert_cmpstr (hid_identity.name, ==, "Valve Software Steam Controller");
      g_assert_cmpstr (hid_identity.phys, ==, "[hid]usb-0000:00:14.0-1.2/input1");
      g_assert_cmpstr (hid_identity.uniq, ==, "");

      g_assert_cmphex (input_identity.bus_type, ==, EVDEV_MARKER | BUS_USB);
      g_assert_cmphex (input_identity.vendor_id, ==, EVDEV_MARKER | VENDOR_VALVE);
      g_assert_cmphex (input_identity.product_id, ==, EVDEV_MARKER | PRODUCT_VALVE_STEAM_CONTROLLER);
      g_assert_cmphex (input_identity.version, ==, EVDEV_MARKER | 0x0111);
      g_assert_cmpstr (input_identity.name, ==, "Wireless Steam Controller");
      g_assert_cmpstr (input_identity.phys, ==, "[input]usb-0000:00:14.0-1.2/input1");
      g_assert_cmpstr (input_identity.uniq, ==, "12345678");

      g_assert_cmphex (usb_identity.vendor_id, ==, USB_MARKER | VENDOR_VALVE);
      g_assert_cmphex (usb_identity.product_id, ==, USB_MARKER | PRODUCT_VALVE_STEAM_CONTROLLER);
      g_assert_cmphex (usb_identity.version, ==, USB_MARKER | 0x0001);
      g_assert_cmpstr (usb_identity.manufacturer, ==, "Valve Software");
      g_assert_cmpstr (usb_identity.product, ==, "Steam Controller");
      g_assert_cmpstr (usb_identity.serial, ==, NULL);

      uevent = srt_input_device_dup_uevent (device);
      g_assert_cmpstr (uevent, ==, "ONE=1\nTWO=2\n");

      udev_properties = srt_input_device_dup_udev_properties (device);
      g_assert_nonnull (udev_properties);
      g_assert_cmpstr (udev_properties[0], ==, "ID_INPUT_JOYSTICK=1");
      g_assert_cmpstr (udev_properties[1], ==, NULL);

      g_assert_cmpstr (srt_input_device_get_hid_sys_path (device), ==,
                       "/sys/devices/mock/usb/hid");
      hid_uevent = srt_input_device_dup_hid_uevent (device);
      g_assert_cmpstr (hid_uevent, ==, "HID=yes\n");

      g_assert_cmpstr (srt_input_device_get_input_sys_path (device), ==,
                       "/sys/devices/mock/usb/hid/input");
      input_uevent = srt_input_device_dup_input_uevent (device);
      g_assert_cmpstr (input_uevent, ==, "INPUT=yes\n");

      g_assert_cmpstr (srt_input_device_get_usb_device_sys_path (device), ==,
                       "/sys/devices/mock/usb");
      usb_uevent = srt_input_device_dup_usb_device_uevent (device);
      g_assert_cmpstr (usb_uevent, ==, "USB=usb_device\n");

      /* This assumes EV_MAX doesn't increase its value */
      g_assert_cmpuint (srt_input_device_get_event_types (device, NULL, 0),
                        ==, 1);
      g_assert_cmpuint (srt_input_device_get_event_types (device, &evbits, 1),
                        ==, 1);
      g_assert_cmphex (evbits & (1 << EV_KEY), ==, 1 << EV_KEY);
      g_assert_cmphex (evbits & (1 << EV_ABS), ==, 1 << EV_ABS);
      g_assert_cmphex (evbits & (1 << EV_SW), ==, 0);
      g_assert_cmphex (evbits & (1 << EV_MSC), ==, 0);
      g_assert_cmpint (srt_input_device_has_event_type (device, EV_KEY), ==, TRUE);
      g_assert_cmpint (srt_input_device_has_event_type (device, EV_SW), ==, FALSE);
      g_assert_cmpint (srt_input_device_has_event_capability (device, 0, EV_KEY),
                       ==, TRUE);
      g_assert_cmpint (srt_input_device_has_event_capability (device, 0, EV_SW),
                       ==, FALSE);

      g_assert_cmpuint (srt_input_device_get_event_capabilities (device, 0,
                                                                 bits,
                                                                 G_N_ELEMENTS (bits)),
                        ==, 1);
      g_assert_cmphex (bits[0], ==, evbits);

      for (i = 1; i < G_N_ELEMENTS (bits); i++)
        g_assert_cmphex (bits[i], ==, 0);

      g_assert_cmpuint (srt_input_device_get_event_capabilities (device, EV_KEY,
                                                                 bits,
                                                                 G_N_ELEMENTS (bits)),
                        >, 1);
      /* Low KEY_ codes are keyboard keys, which we don't have */
      g_assert_cmphex (bits[0], ==, 0);
      g_assert_cmpint (test_bit (BTN_A, bits), ==, 1);
      g_assert_cmpint (test_bit (BTN_STYLUS, bits), ==, 0);
      g_assert_cmpint (test_bit (KEY_SEMICOLON, bits), ==, 0);

      /* ABS axes also match */
      g_assert_cmpuint (srt_input_device_get_event_capabilities (device, EV_ABS,
                                                                 bits,
                                                                 G_N_ELEMENTS (bits)),
                        >=, 1);
      g_assert_cmpint (test_bit (ABS_X, bits), ==, 1);
      g_assert_cmpint (test_bit (ABS_Z, bits), ==, 0);

      /* REL axes also match (in fact we don't have any, but we still store
       * the bitfield) */
      g_assert_cmpuint (srt_input_device_get_event_capabilities (device, EV_REL,
                                                                 bits,
                                                                 G_N_ELEMENTS (bits)),
                        >=, 1);

      for (i = 1; i < G_N_ELEMENTS (bits); i++)
        g_assert_cmphex (bits[i], ==, 0);

      /* We don't support EV_SW */
      g_assert_cmpuint (srt_input_device_get_event_capabilities (device, EV_SW,
                                                                 bits,
                                                                 G_N_ELEMENTS (bits)),
                        ==, 0);

      for (i = 1; i < G_N_ELEMENTS (bits); i++)
        g_assert_cmphex (bits[i], ==, 0);

      g_ptr_array_add (f->log, g_steal_pointer (&message));
    }

  g_assert_true (in_monitor_main_context (f));
}

static void
device_removed_cb (SrtInputDeviceMonitor *monitor,
                   SrtInputDevice *device,
                   gpointer user_data)
{
  Fixture *f = user_data;
  g_autofree gchar *message = NULL;

  g_assert_true (SRT_IS_INPUT_DEVICE_MONITOR (monitor));
  g_assert_true (SRT_IS_INPUT_DEVICE (device));

  message = g_strdup_printf ("removed device: %s",
                             srt_input_device_get_dev_node (device));
  g_debug ("%s: %s", G_OBJECT_TYPE_NAME (monitor), message);

  if (f->config->type == MOCK)
    g_ptr_array_add (f->log, g_steal_pointer (&message));

  g_assert_true (in_monitor_main_context (f));
}

static void
all_for_now_cb (SrtInputDeviceMonitor *monitor,
                gpointer user_data)
{
  Fixture *f = user_data;

  g_assert_true (SRT_IS_INPUT_DEVICE_MONITOR (monitor));

  g_ptr_array_add (f->log, g_strdup ("all for now"));
  g_debug ("%s: %s",
           G_OBJECT_TYPE_NAME (monitor),
           (const char *) g_ptr_array_index (f->log, f->log->len - 1));

  g_assert_true (in_monitor_main_context (f));
}

static gboolean
done_cb (gpointer user_data)
{
  gboolean *done = user_data;

  *done = TRUE;
  return G_SOURCE_REMOVE;
}

/* This is the equivalent of g_idle_add() for a non-default main context */
static guint
idle_add_in_context (GSourceFunc function,
                     gpointer data,
                     GMainContext *context)
{
  g_autoptr(GSource) idler = g_idle_source_new ();

  g_source_set_callback (idler, function, data, NULL);
  return g_source_attach (idler, context);
}

static SrtInputDeviceMonitor *
input_device_monitor_new (Fixture *f,
                          SrtInputDeviceMonitorFlags flags)
{
  switch (f->config->type)
    {
      case DIRECT:
        flags |= SRT_INPUT_DEVICE_MONITOR_FLAGS_DIRECT;
        return srt_input_device_monitor_new (flags);

      case UDEV:
        flags |= SRT_INPUT_DEVICE_MONITOR_FLAGS_UDEV;
        return srt_input_device_monitor_new (flags);

      case MOCK:
      default:
        return SRT_INPUT_DEVICE_MONITOR (mock_input_device_monitor_new (flags));
    }
}

/*
 * Test the basic behaviour of an input device monitor:
 * - start
 * - do initial enumeration
 * - watch for new devices
 * - emit signals in the correct main context
 * - stop
 */
static void
test_input_device_monitor (Fixture *f,
                           gconstpointer context)
{
  g_autoptr(SrtInputDeviceMonitor) monitor = NULL;
  g_autoptr(GError) error = NULL;
  gboolean ok;
  gboolean did_default_idle = FALSE;
  gboolean did_context_idle = FALSE;
  guint i;

  if (f->skipped)
    return;

  f->monitor_context = g_main_context_new ();

  /* To check that the signals get emitted in the correct main-context,
   * temporarily set a new thread-default main-context while we create
   * the monitor. */
  g_main_context_push_thread_default (f->monitor_context);
    {
      monitor = input_device_monitor_new (f, SRT_INPUT_DEVICE_MONITOR_FLAGS_NONE);
    }
  g_main_context_pop_thread_default (f->monitor_context);

  g_assert_nonnull (monitor);

  srt_input_device_monitor_request_evdev (monitor);
  srt_input_device_monitor_request_raw_hid (monitor);

  g_signal_connect (monitor, "added", G_CALLBACK (device_added_cb), f);
  g_signal_connect (monitor, "removed", G_CALLBACK (device_removed_cb), f);
  g_signal_connect (monitor, "all-for-now", G_CALLBACK (all_for_now_cb), f);

  /* Note that the signals are emitted in the main-context that was
   * thread-default at the time we created the object, not the
   * main-context that called start(). */
  ok = srt_input_device_monitor_start (monitor, &error);
  g_debug ("start() returned");
  g_assert_no_error (error);
  g_assert_true (ok);
  g_ptr_array_add (f->log, g_strdup ("start() returned"));

  g_idle_add (done_cb, &did_default_idle);
  idle_add_in_context (done_cb, &did_context_idle, f->monitor_context);

  i = 0;

  g_assert_cmpuint (f->log->len, >, i);
  g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                   "start() returned");
  /* There's nothing else in the log yet */
  g_assert_cmpuint (f->log->len, ==, i);

  /* Iterating the default main context does not deliver signals */
  while (!did_default_idle)
    g_main_context_iteration (NULL, TRUE);

  g_assert_cmpuint (f->log->len, ==, i);

  /* Iterating the main context that was thread-default at the time we
   * constructed the monitor *does* deliver signals */
  while (!did_context_idle)
    g_main_context_iteration (f->monitor_context, TRUE);

  /* For the mock device monitor, we can predict which devices will be added,
   * so we log them and assert about them. For real device monitors we
   * can't reliably do this. */
  if (f->config->type == MOCK)
    {
      g_assert_cmpuint (f->log->len, >, i);
      g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                       "added device: /dev/input/event0");
    }

  g_assert_cmpuint (f->log->len, >, i);
  g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                   "all for now");

  if (f->config->type == MOCK)
    {
      g_assert_cmpuint (f->log->len, >, i);
      g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                       "added device: /dev/input/event-connected-briefly");
      g_assert_cmpuint (f->log->len, >, i);
      g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                       "removed device: /dev/input/event-connected-briefly");
    }

  g_assert_cmpuint (f->log->len, ==, i);

  /* Explicitly stop it here. We test not explicitly stopping in the
   * other test-case */
  srt_input_device_monitor_stop (monitor);

  /* It's possible that not all the memory used is freed until we have
   * iterated the main-context one last time */
  did_context_idle = FALSE;
  idle_add_in_context (done_cb, &did_context_idle, f->monitor_context);

  while (!did_context_idle)
    g_main_context_iteration (f->monitor_context, TRUE);
}

/*
 * Test things we couldn't test in the previous test-case:
 * - the ONCE flag, which disables monitoring
 * - using our thread-default main-context throughout
 */
static void
test_input_device_monitor_once (Fixture *f,
                                gconstpointer context)
{
  g_autoptr(SrtInputDeviceMonitor) monitor = NULL;
  g_autoptr(GError) error = NULL;
  gboolean ok;
  gboolean done = FALSE;
  guint i;

  if (f->skipped)
    return;

  monitor = input_device_monitor_new (f, SRT_INPUT_DEVICE_MONITOR_FLAGS_ONCE);
  g_assert_nonnull (monitor);

  srt_input_device_monitor_request_evdev (monitor);
  srt_input_device_monitor_request_raw_hid (monitor);

  g_signal_connect (monitor, "added", G_CALLBACK (device_added_cb), f);
  g_signal_connect (monitor, "removed", G_CALLBACK (device_removed_cb), f);
  g_signal_connect (monitor, "all-for-now", G_CALLBACK (all_for_now_cb), f);

  ok = srt_input_device_monitor_start (monitor, &error);
  g_debug ("start() returned");
  g_assert_no_error (error);
  g_assert_true (ok);
  g_ptr_array_add (f->log, g_strdup ("start() returned"));

  g_idle_add (done_cb, &done);

  while (!done)
    g_main_context_iteration (NULL, TRUE);

  i = 0;

  /* Because the same main context was the thread-default at the
   * time we created the object and at the time we called start(),
   * the first batch of signals arrive even before start() has returned. */
  if (f->config->type == MOCK)
    {
      g_assert_cmpuint (f->log->len, >, i);
      g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                       "added device: /dev/input/event0");
    }

  g_assert_cmpuint (f->log->len, >, i);
  g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                   "all for now");
  g_assert_cmpuint (f->log->len, >, i);
  g_assert_cmpstr (g_ptr_array_index (f->log, i++), ==,
                   "start() returned");
  g_assert_cmpuint (f->log->len, ==, i);

  /* Don't explicitly stop it here. We test explicitly stopping in the
   * other test-case */
  g_clear_object (&monitor);

  /* It's possible that not all the memory used is freed until we have
   * iterate the main-context one last time */
  done = FALSE;
  g_idle_add (done_cb, &done);

  while (!done)
    g_main_context_iteration (NULL, TRUE);
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add ("/input-device/identity-from-hid-uevent", Fixture, NULL,
              setup, test_input_device_identity_from_hid_uevent, teardown);
  g_test_add ("/input-device/usb", Fixture, NULL,
              setup, test_input_device_usb, teardown);
  g_test_add ("/input-device/monitor/mock", Fixture, NULL,
              setup, test_input_device_monitor, teardown);
  g_test_add ("/input-device/monitor-once/mock", Fixture, NULL,
              setup, test_input_device_monitor_once, teardown);
  g_test_add ("/input-device/monitor/direct", Fixture, &direct_config,
              setup, test_input_device_monitor, teardown);
  g_test_add ("/input-device/monitor-once/direct", Fixture, &direct_config,
              setup, test_input_device_monitor_once, teardown);
  g_test_add ("/input-device/monitor/udev", Fixture, &udev_config,
              setup, test_input_device_monitor, teardown);
  g_test_add ("/input-device/monitor-once/udev", Fixture, &udev_config,
              setup, test_input_device_monitor_once, teardown);

  return g_test_run ();
}
