/*
 * Input device monitor, adapted from SDL code.
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

#include <libglnx.h>

#include <steam-runtime-tools/steam-runtime-tools.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/input-device-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "steam-runtime-tools/json-utils-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include <stdio.h>

#include <glib-unix.h>

#include <json-glib/json-glib.h>

#define RECORD_SEPARATOR "\x1e"

static FILE *original_stdout = NULL;
static gboolean opt_one_line = FALSE;
static gboolean opt_seq = FALSE;
static gboolean opt_verbose = FALSE;

static void
jsonify_flags (JsonBuilder *builder,
               GType flags_type,
               unsigned int values)
{
  GFlagsClass *class;
  GFlagsValue *flags_value;

  g_return_if_fail (G_TYPE_IS_FLAGS (flags_type));

  class = g_type_class_ref (flags_type);

  while (values != 0)
    {
      flags_value = g_flags_get_first_value (class, values);

      if (flags_value == NULL)
        break;

      json_builder_add_string_value (builder, flags_value->value_nick);
      values &= ~flags_value->value;
    }

  if (values)
    {
      gchar *rest = g_strdup_printf ("0x%x", values);

      json_builder_add_string_value (builder, rest);

      g_free (rest);
    }

  g_type_class_unref (class);
}

static void
print_json (JsonBuilder *builder)
{
  g_autoptr(JsonGenerator) generator = json_generator_new ();
  g_autofree gchar *text = NULL;
  JsonNode *root;

  root = json_builder_get_root (builder);
  generator = json_generator_new ();
  json_generator_set_root (generator, root);

  if (opt_seq)
    fputs (RECORD_SEPARATOR, original_stdout);

  if (!opt_one_line)
    json_generator_set_pretty (generator, TRUE);

  text = json_generator_to_data (generator, NULL);

  if (fputs (text, original_stdout) < 0)
    g_warning ("Unable to write output: %s", g_strerror (errno));

  if (fputs ("\n", original_stdout) < 0)
    g_warning ("Unable to write final newline: %s", g_strerror (errno));
}

static void
append_evdev_hex (GString *buf,
                  const unsigned long *bits,
                  size_t n_longs)
{
  size_t i, j;

  for (i = 0; i < n_longs; i++)
    {
      unsigned long word = bits[i];

      for (j = 0; j < sizeof (long); j++)
        {
          unsigned char byte = (word >> (CHAR_BIT * j)) & 0xff;
          g_string_append_printf (buf, "%02x ", byte);
        }
      g_string_append_c (buf, ' ');
    }
}

static void
added (SrtInputDeviceMonitor *monitor,
       SrtInputDevice *dev,
       void *user_data)
{
  g_autoptr(JsonBuilder) builder = json_builder_new ();

  json_builder_begin_object (builder);
    {
      json_builder_set_member_name (builder, "added");
      json_builder_begin_object (builder);
        {
          g_auto(GStrv) udev_properties = NULL;
          const char *sys_path;
          struct
          {
            const char *name;
            const char *phys;
            const char *uniq;
            const char *manufacturer;
            unsigned int bus_type, vendor_id, product_id, version;
          } id, blank_id = {};
          unsigned long bits[LONGS_FOR_BITS (HIGHEST_EVENT_CODE)];
          SrtInputDeviceTypeFlags type_flags;

          json_builder_set_member_name (builder, "interface_flags");
          json_builder_begin_array (builder);
          jsonify_flags (builder, SRT_TYPE_INPUT_DEVICE_INTERFACE_FLAGS,
                         srt_input_device_get_interface_flags (dev));
          json_builder_end_array (builder);

          type_flags = srt_input_device_get_type_flags (dev);
          json_builder_set_member_name (builder, "type_flags");
          json_builder_begin_array (builder);
          jsonify_flags (builder, SRT_TYPE_INPUT_DEVICE_TYPE_FLAGS,
                         type_flags);
          json_builder_end_array (builder);

          json_builder_set_member_name (builder, "dev_node");
          json_builder_add_string_value (builder,
                                         srt_input_device_get_dev_node (dev));
          json_builder_set_member_name (builder, "subsystem");
          json_builder_add_string_value (builder,
                                         srt_input_device_get_subsystem (dev));
          json_builder_set_member_name (builder, "sys_path");
          json_builder_add_string_value (builder,
                                         srt_input_device_get_sys_path (dev));

          id = blank_id;

          if (srt_input_device_get_identity (dev,
                                             &id.bus_type,
                                             &id.vendor_id,
                                             &id.product_id,
                                             &id.version))
            {
                {
                  g_autofree gchar *s = g_strdup_printf ("0x%04x", id.bus_type);
                  json_builder_set_member_name (builder, "bus_type");
                  json_builder_add_string_value (builder, s);
                }
                {
                  g_autofree gchar *s = g_strdup_printf ("0x%04x", id.vendor_id);
                  json_builder_set_member_name (builder, "vendor_id");
                  json_builder_add_string_value (builder, s);
                }
                {
                  g_autofree gchar *s = g_strdup_printf ("0x%04x", id.product_id);
                  json_builder_set_member_name (builder, "product_id");
                  json_builder_add_string_value (builder, s);
                }
                {
                  g_autofree gchar *s = g_strdup_printf ("0x%04x", id.version);
                  json_builder_set_member_name (builder, "version");
                  json_builder_add_string_value (builder, s);
                }
            }

          if (srt_input_device_get_interface_flags (dev)
              & SRT_INPUT_DEVICE_INTERFACE_FLAGS_EVENT)
            {
              SrtInputDeviceTypeFlags guessed_flags;

              json_builder_set_member_name (builder, "evdev");
              json_builder_begin_object (builder);

              if (srt_input_device_get_event_types (dev, bits, G_N_ELEMENTS (bits)) > 0)
                {
                  json_builder_set_member_name (builder, "types");
                  json_builder_begin_array (builder);
                    {
#define BIT(x) \
                      do \
                        { \
                          if (test_bit_checked (EV_ ## x, bits, G_N_ELEMENTS (bits))) \
                            { \
                              json_builder_add_string_value (builder, #x); \
                            } \
                        } \
                      while (0)

                      BIT (SYN);
                      BIT (KEY);
                      BIT (REL);
                      BIT (ABS);
                      BIT (MSC);
                      BIT (SW);
                      BIT (LED);
                      BIT (SND);
                      BIT (REP);
                      BIT (FF);
                      BIT (PWR);
                      BIT (FF_STATUS);

#undef BIT
                    }
                  json_builder_end_array (builder);

                  if (opt_verbose)
                    {
                      g_autoptr(GString) buf = g_string_new ("");

                      append_evdev_hex (buf, bits, LONGS_FOR_BITS (EV_MAX));
                      json_builder_set_member_name (builder, "raw_types");
                      json_builder_add_string_value (builder, buf->str);
                    }
                }

              if (srt_input_device_get_event_capabilities (dev, EV_ABS,
                                                           bits, G_N_ELEMENTS (bits)) > 0)
                {
                  json_builder_set_member_name (builder, "absolute_axes");
                  json_builder_begin_array (builder);
                    {
#define BIT(x) \
                      do \
                        { \
                          if (test_bit_checked (ABS_ ## x, bits, G_N_ELEMENTS (bits))) \
                            { \
                              json_builder_add_string_value (builder, #x); \
                            } \
                        } \
                      while (0)

                      BIT (X);
                      BIT (Y);
                      BIT (Z);
                      BIT (RX);
                      BIT (RY);
                      BIT (RZ);
                      BIT (THROTTLE);
                      BIT (RUDDER);
                      BIT (WHEEL);
                      BIT (GAS);
                      BIT (BRAKE);
                      BIT (HAT0X);
                      BIT (HAT0Y);
                      BIT (HAT1X);
                      BIT (HAT1Y);
                      BIT (HAT2X);
                      BIT (HAT2Y);
                      BIT (HAT3X);
                      BIT (HAT3Y);
                      BIT (PRESSURE);
                      BIT (DISTANCE);
                      BIT (TILT_X);
                      BIT (TILT_Y);
                      BIT (TOOL_WIDTH);
                      BIT (VOLUME);
                      BIT (MISC);
                      BIT (RESERVED);
                      BIT (MT_SLOT);

#undef BIT
                    }
                  json_builder_end_array (builder);

                  if (opt_verbose)
                    {
                      g_autoptr(GString) buf = g_string_new ("");

                      append_evdev_hex (buf, bits, LONGS_FOR_BITS (ABS_MAX));
                      json_builder_set_member_name (builder, "raw_abs");
                      json_builder_add_string_value (builder, buf->str);
                    }
                }

              if (srt_input_device_get_event_capabilities (dev, EV_REL,
                                                           bits, G_N_ELEMENTS (bits)) > 0)
                {
                  json_builder_set_member_name (builder, "relative_axes");
                  json_builder_begin_array (builder);
                    {
#define BIT(x) \
                      do \
                        { \
                          if (test_bit_checked (REL_ ## x, bits, G_N_ELEMENTS (bits))) \
                            { \
                              json_builder_add_string_value (builder, #x); \
                            } \
                        } \
                      while (0)

                      BIT (X);
                      BIT (Y);
                      BIT (Z);
                      BIT (RX);
                      BIT (RY);
                      BIT (RZ);
                      BIT (HWHEEL);
                      BIT (DIAL);
                      BIT (WHEEL);
                      BIT (MISC);
                      BIT (RESERVED);
                      BIT (WHEEL_HI_RES);
                      BIT (HWHEEL_HI_RES);

#undef BIT
                    }
                  json_builder_end_array (builder);

                  if (opt_verbose)
                    {
                      g_autoptr(GString) buf = g_string_new ("");

                      append_evdev_hex (buf, bits, LONGS_FOR_BITS (REL_MAX));
                      json_builder_set_member_name (builder, "raw_rel");
                      json_builder_add_string_value (builder, buf->str);
                    }
                }

              if (srt_input_device_get_event_capabilities (dev, EV_KEY,
                                                           bits, G_N_ELEMENTS (bits)) > 0)
                {
                  json_builder_set_member_name (builder, "keys");
                  json_builder_begin_array (builder);
                    {
#define BIT(x) \
                      do \
                        { \
                          if (test_bit_checked (x, bits, G_N_ELEMENTS (bits))) \
                            { \
                              json_builder_add_string_value (builder, #x); \
                            } \
                        } \
                      while (0)

                      /* We don't show all the keyboard keys here because
                       * that would be ridiculous, but we do show a
                       * selection that should be enough to tell the
                       * difference between keyboards, mice, joysticks
                       * and so on. We do show most joystick buttons. */

                      /* Gamepads */
                      BIT (BTN_A);    /* aka BTN_GAMEPAD, BTN_SOUTH */
                      BIT (BTN_B);
                      BIT (BTN_C);
                      BIT (BTN_X);
                      BIT (BTN_Y);
                      BIT (BTN_Z);
                      BIT (BTN_TL);
                      BIT (BTN_TR);
                      BIT (BTN_TL2);
                      BIT (BTN_TR2);
                      BIT (BTN_SELECT);
                      BIT (BTN_START);
                      BIT (BTN_MODE);
                      BIT (BTN_THUMBL);
                      BIT (BTN_THUMBR);
                      /* Not all gamepads have a digital d-pad, some only
                       * represent it as the hat0x and hat0y absolute axes;
                       * but some do have it */
                      BIT (BTN_DPAD_UP);
                      BIT (BTN_DPAD_DOWN);
                      BIT (BTN_DPAD_LEFT);
                      BIT (BTN_DPAD_RIGHT);

                      /* Flight sticks and similar joysticks */
                      BIT (BTN_TRIGGER);
                      BIT (BTN_THUMB);
                      BIT (BTN_THUMB2);
                      BIT (BTN_TOP);
                      BIT (BTN_TOP2);
                      BIT (BTN_PINKIE);
                      BIT (BTN_BASE);
                      BIT (BTN_BASE2);
                      BIT (BTN_BASE3);
                      BIT (BTN_BASE4);
                      BIT (BTN_BASE5);
                      BIT (BTN_BASE6);
                      BIT (BTN_DEAD);
                      BIT (BTN_TRIGGER_HAPPY);

                      /* Steering wheels */
                      BIT (BTN_GEAR_DOWN);
                      BIT (BTN_GEAR_UP);

                      /* Keyboards */
                      BIT (KEY_ESC);
                      BIT (KEY_0);
                      BIT (KEY_A);
                      BIT (KEY_KP0);
                      BIT (KEY_PLAY);

                      /* Mice and friends. BTN_LEFT is an alias for
                       * BTN_MOUSE, but we use BTN_MOUSE here as a
                       * hint that the rest are also mouse buttons. */
                      BIT (BTN_MOUSE);
                      BIT (BTN_RIGHT);
                      BIT (BTN_MIDDLE);
                      BIT (BTN_SIDE);
                      BIT (BTN_EXTRA);
                      BIT (BTN_FORWARD);
                      BIT (BTN_BACK);
                      BIT (BTN_TASK);
                      BIT (BTN_DIGI);
                      BIT (KEY_MACRO1);

                      /* Generic buttons that nobody knows what they do... */
                      BIT (BTN_0);

#undef BIT
                    }
                  json_builder_end_array (builder);

                  if (opt_verbose)
                    {
                      g_autoptr(GString) buf = g_string_new ("");

                      append_evdev_hex (buf, bits, LONGS_FOR_BITS (KEY_MAX));
                      json_builder_set_member_name (builder, "raw_keys");
                      json_builder_add_string_value (builder, buf->str);
                    }
                }

              if (srt_input_device_get_input_properties (dev, bits,
                                                         G_N_ELEMENTS (bits)) > 0)
                {
                  json_builder_set_member_name (builder, "input_properties");
                  json_builder_begin_array (builder);
                    {
#define BIT(x) \
                      do \
                        { \
                          if (test_bit_checked (INPUT_PROP_ ## x, bits, G_N_ELEMENTS (bits))) \
                            { \
                              json_builder_add_string_value (builder, #x); \
                            } \
                        } \
                      while (0)

                      BIT (POINTER);
                      BIT (DIRECT);
                      BIT (BUTTONPAD);
                      BIT (SEMI_MT);
                      BIT (TOPBUTTONPAD);
                      BIT (POINTING_STICK);
                      BIT (ACCELEROMETER);

#undef BIT
                    }
                  json_builder_end_array (builder);

                  if (opt_verbose)
                    {
                      g_autoptr(GString) buf = g_string_new ("");

                      append_evdev_hex (buf, bits, LONGS_FOR_BITS (REL_MAX));
                      json_builder_set_member_name (builder, "raw_input_properties");
                      json_builder_add_string_value (builder, buf->str);
                    }
                }

              guessed_flags = srt_input_device_guess_type_flags_from_event_capabilities (dev);

              if (opt_verbose || guessed_flags != type_flags)
                {
                  json_builder_set_member_name (builder, "guessed_type_flags");
                  json_builder_begin_array (builder);
                  jsonify_flags (builder, SRT_TYPE_INPUT_DEVICE_TYPE_FLAGS,
                                 guessed_flags);
                  json_builder_end_array (builder);
                }

              json_builder_end_object (builder);
            }

          udev_properties = srt_input_device_dup_udev_properties (dev);

          if (udev_properties != NULL)
            {
              gsize i;

              json_builder_set_member_name (builder, "udev_properties");
              json_builder_begin_array (builder);

              for (i = 0; udev_properties[i] != NULL; i++)
                {
                  g_autofree gchar *valid = NULL;

                  valid = g_utf8_make_valid (udev_properties[i], -1);
                  json_builder_add_string_value (builder, valid);
                }

              json_builder_end_array (builder);
            }

          if (opt_verbose)
            {
              g_autofree gchar *uevent = srt_input_device_dup_uevent (dev);

              if (uevent != NULL)
                _srt_json_builder_add_array_of_lines (builder, "uevent", uevent);
            }

          sys_path = srt_input_device_get_hid_sys_path (dev);

          id = blank_id;

          if (srt_input_device_get_hid_identity (dev,
                                                 &id.bus_type,
                                                 &id.vendor_id,
                                                 &id.product_id,
                                                 &id.name,
                                                 &id.phys,
                                                 &id.uniq)
              || sys_path != NULL)
            {
              json_builder_set_member_name (builder, "hid_ancestor");
              json_builder_begin_object (builder);

              json_builder_set_member_name (builder, "sys_path");
              json_builder_add_string_value (builder, sys_path);

              json_builder_set_member_name (builder, "name");
              json_builder_add_string_value (builder, id.name);

                {
                  g_autofree gchar *s = g_strdup_printf ("0x%04x", id.bus_type);
                  json_builder_set_member_name (builder, "bus_type");
                  json_builder_add_string_value (builder, s);
                }
                {
                  g_autofree gchar *s = g_strdup_printf ("0x%04x", id.vendor_id);
                  json_builder_set_member_name (builder, "vendor_id");
                  json_builder_add_string_value (builder, s);
                }
                {
                  g_autofree gchar *s = g_strdup_printf ("0x%04x", id.product_id);
                  json_builder_set_member_name (builder, "product_id");
                  json_builder_add_string_value (builder, s);
                }

              json_builder_set_member_name (builder, "uniq");
              json_builder_add_string_value (builder, id.uniq);

              if (opt_verbose)
                {
                  g_autofree gchar *uevent = srt_input_device_dup_hid_uevent (dev);

                  json_builder_set_member_name (builder, "phys");
                  json_builder_add_string_value (builder, id.phys);

                  if (uevent != NULL)
                    _srt_json_builder_add_array_of_lines (builder, "uevent", uevent);
                }

              json_builder_end_object (builder);
            }

          sys_path = srt_input_device_get_input_sys_path (dev);

          id = blank_id;

          if (srt_input_device_get_input_identity (dev,
                                                   &id.bus_type,
                                                   &id.vendor_id,
                                                   &id.product_id,
                                                   &id.version,
                                                   &id.name,
                                                   &id.phys,
                                                   &id.uniq)
              || sys_path != NULL)
            {
              json_builder_set_member_name (builder, "input_ancestor");
              json_builder_begin_object (builder);

              json_builder_set_member_name (builder, "sys_path");
              json_builder_add_string_value (builder, sys_path);

              json_builder_set_member_name (builder, "name");
              json_builder_add_string_value (builder, id.name);

                {
                  g_autofree gchar *s = g_strdup_printf ("0x%04x", id.bus_type);
                  json_builder_set_member_name (builder, "bus_type");
                  json_builder_add_string_value (builder, s);
                }
                {
                  g_autofree gchar *s = g_strdup_printf ("0x%04x", id.vendor_id);
                  json_builder_set_member_name (builder, "vendor_id");
                  json_builder_add_string_value (builder, s);
                }
                {
                  g_autofree gchar *s = g_strdup_printf ("0x%04x", id.product_id);
                  json_builder_set_member_name (builder, "product_id");
                  json_builder_add_string_value (builder, s);
                }
                {
                  g_autofree gchar *s = g_strdup_printf ("0x%04x", id.version);
                  json_builder_set_member_name (builder, "version");
                  json_builder_add_string_value (builder, s);
                }

              if (opt_verbose)
                {
                  g_autofree gchar *uevent = srt_input_device_dup_input_uevent (dev);

                  json_builder_set_member_name (builder, "phys");
                  json_builder_add_string_value (builder, id.phys);

                  if (uevent != NULL)
                    _srt_json_builder_add_array_of_lines (builder, "uevent", uevent);
                }

              json_builder_end_object (builder);
            }

          sys_path = srt_input_device_get_usb_device_sys_path (dev);

          id = blank_id;

          if (srt_input_device_get_usb_device_identity (dev,
                                                        &id.vendor_id,
                                                        &id.product_id,
                                                        &id.version,
                                                        &id.manufacturer,
                                                        &id.name,
                                                        &id.uniq)
              || sys_path != NULL)
            {
              json_builder_set_member_name (builder, "usb_device_ancestor");
              json_builder_begin_object (builder);

              json_builder_set_member_name (builder, "sys_path");
              json_builder_add_string_value (builder, sys_path);

                {
                  g_autofree gchar *s = g_strdup_printf ("0x%04x", id.vendor_id);
                  json_builder_set_member_name (builder, "vendor_id");
                  json_builder_add_string_value (builder, s);
                }
                {
                  g_autofree gchar *s = g_strdup_printf ("0x%04x", id.product_id);
                  json_builder_set_member_name (builder, "product_id");
                  json_builder_add_string_value (builder, s);
                }
                {
                  g_autofree gchar *s = g_strdup_printf ("0x%04x", id.version);
                  json_builder_set_member_name (builder, "version");
                  json_builder_add_string_value (builder, s);
                }

              json_builder_set_member_name (builder, "manufacturer");
              json_builder_add_string_value (builder, id.manufacturer);
              json_builder_set_member_name (builder, "product");
              json_builder_add_string_value (builder, id.name);
              json_builder_set_member_name (builder, "serial");
              json_builder_add_string_value (builder, id.uniq);

              if (opt_verbose)
                {
                  g_autofree gchar *uevent = srt_input_device_dup_usb_device_uevent (dev);

                  if (uevent != NULL)
                    _srt_json_builder_add_array_of_lines (builder, "uevent", uevent);
                }

              json_builder_end_object (builder);
            }
        }
      json_builder_end_object (builder);
    }
  json_builder_end_object (builder);

  print_json (builder);
}

static void
removed (SrtInputDeviceMonitor *monitor,
         SrtInputDevice *dev,
         void *user_data)
{
  g_autoptr(JsonBuilder) builder = json_builder_new ();

  json_builder_begin_object (builder);
    {
      json_builder_set_member_name (builder, "removed");
      json_builder_begin_object (builder);
        {
          /* Only print enough details to identify the object */
          json_builder_set_member_name (builder, "dev_node");
          json_builder_add_string_value (builder,
                                         srt_input_device_get_dev_node (dev));
          json_builder_set_member_name (builder, "sys_path");
          json_builder_add_string_value (builder,
                                         srt_input_device_get_sys_path (dev));
        }
      json_builder_end_object (builder);
    }
  json_builder_end_object (builder);

  print_json (builder);
}

static void
all_for_now (SrtInputDeviceMonitor *source,
             void *user_data)
{
  g_autoptr(SrtInputDeviceMonitor) monitor = NULL;
  SrtInputDeviceMonitor **monitor_p = user_data;

  if (srt_input_device_monitor_get_flags (source) & SRT_INPUT_DEVICE_MONITOR_FLAGS_ONCE)
    {
      monitor = g_steal_pointer (monitor_p);
      g_assert (monitor == NULL || monitor == source);
      g_clear_object (&monitor);
    }

  if (opt_seq)
    fputs (RECORD_SEPARATOR, original_stdout);

  fputs ("{\"all-for-now\": true}\n", original_stdout);
}

static gboolean
interrupt_cb (void *user_data)
{
  g_autoptr(SrtInputDeviceMonitor) monitor = NULL;
  SrtInputDeviceMonitor **monitor_p = user_data;

  monitor = g_steal_pointer (monitor_p);
  g_clear_object (&monitor);

  return G_SOURCE_CONTINUE;
}

static SrtInputDeviceMonitorFlags opt_mode = SRT_INPUT_DEVICE_MONITOR_FLAGS_NONE;
static SrtInputDeviceMonitorFlags opt_flags = SRT_INPUT_DEVICE_MONITOR_FLAGS_NONE;
static gboolean opt_evdev = FALSE;
static gboolean opt_hidraw = FALSE;
static gboolean opt_print_version = FALSE;

static gboolean
opt_once_cb (const gchar *option_name,
             const gchar *value,
             gpointer data,
             GError **error)
{
  opt_flags |= SRT_INPUT_DEVICE_MONITOR_FLAGS_ONCE;
  return TRUE;
}

static gboolean
opt_direct_cb (const gchar *option_name,
               const gchar *value,
               gpointer data,
               GError **error)
{
  opt_mode = SRT_INPUT_DEVICE_MONITOR_FLAGS_DIRECT;
  return TRUE;
}

static gboolean
opt_udev_cb (const gchar *option_name,
             const gchar *value,
             gpointer data,
             GError **error)
{
  opt_mode = SRT_INPUT_DEVICE_MONITOR_FLAGS_UDEV;
  return TRUE;
}

static const GOptionEntry option_entries[] =
{
  { "direct", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_direct_cb,
    "Find devices using /dev and /sys", NULL },
  { "evdev", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_evdev,
    "List evdev event devices", NULL },
  { "hidraw", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_hidraw,
    "List raw HID devices", NULL },
  { "once", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_once_cb,
    "Print devices that are initially discovered, then exit", NULL },
  { "one-line", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_one_line,
    "Print one device per line [default: pretty-print as concatenated JSON]",
    NULL },
  { "seq", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_seq,
    "Output application/json-seq [default: pretty-print as concatenated JSON]",
    NULL },
  { "udev", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_udev_cb,
    "Find devices using udev", NULL },
  { "verbose", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_verbose,
    "Be more verbose", NULL },
  { "version", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, &opt_print_version,
    "Print version number and exit", NULL },
  { NULL }
};

static gboolean
run (int argc,
     char **argv,
     GError **error)
{
  g_autoptr(GOptionContext) option_context = NULL;
  g_autoptr(SrtInputDeviceMonitor) monitor = NULL;
  guint int_handler;

  _srt_setenv_disable_gio_modules ();

  option_context = g_option_context_new ("");
  g_option_context_add_main_entries (option_context, option_entries, NULL);

  if (!g_option_context_parse (option_context, &argc, &argv, error))
    return FALSE;

  if (opt_print_version)
    {
      /* Output version number as YAML for machine-readability,
       * inspired by `ostree --version` and `docker version` */
      g_print (
          "%s:\n"
          " Package: steam-runtime-tools\n"
          " Version: %s\n",
          g_get_prgname (), VERSION);
      return TRUE;
    }

  /* stdout is reserved for machine-readable output, so avoid having
   * things like g_debug() pollute it. */
  original_stdout = _srt_divert_stdout_to_stderr (error);

  if (original_stdout == NULL)
    return FALSE;

  int_handler = g_unix_signal_add (SIGINT, interrupt_cb, &monitor);
  monitor = srt_input_device_monitor_new (opt_mode | opt_flags);

  if (opt_evdev)
    srt_input_device_monitor_request_evdev (monitor);

  if (opt_hidraw)
    srt_input_device_monitor_request_raw_hid (monitor);

  if (opt_evdev + opt_hidraw == 0)
    {
      /* Subscribe to everything by default */
      srt_input_device_monitor_request_evdev (monitor);
      srt_input_device_monitor_request_raw_hid (monitor);
    }

  g_signal_connect (monitor, "added", G_CALLBACK (added), NULL);
  g_signal_connect (monitor, "removed", G_CALLBACK (removed), NULL);
  g_signal_connect (monitor, "all-for-now", G_CALLBACK (all_for_now), &monitor);

  if (!srt_input_device_monitor_start (monitor, error))
    return FALSE;

  while (monitor != NULL)
    g_main_context_iteration (NULL, TRUE);

  if (fclose (original_stdout) != 0)
    g_warning ("Unable to close stdout: %s", g_strerror (errno));

  g_source_remove (int_handler);
  return TRUE;
}

int
main (int argc,
      char **argv)
{
  g_autoptr(GError) error = NULL;

  if (run (argc, argv, &error))
    return 0;

  if (error == NULL)
    g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED, "Assertion failure");

  g_printerr ("%s: %s\n", g_get_prgname (), error->message);

  if (error->domain == G_OPTION_ERROR)
    return 2;

  return 1;
}
