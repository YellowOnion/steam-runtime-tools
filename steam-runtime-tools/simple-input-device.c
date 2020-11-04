/*
 * An input device loaded from JSON or similar.
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

#include "steam-runtime-tools/simple-input-device-internal.h"

#include <libglnx.h>

#include "steam-runtime-tools/enums.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/input-device.h"
#include "steam-runtime-tools/input-device-internal.h"
#include "steam-runtime-tools/json-utils-internal.h"
#include "steam-runtime-tools/utils-internal.h"

static void srt_simple_input_device_iface_init (SrtInputDeviceInterface *iface);

G_DEFINE_TYPE_WITH_CODE (SrtSimpleInputDevice,
                         _srt_simple_input_device,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SRT_TYPE_INPUT_DEVICE,
                                                srt_simple_input_device_iface_init))

static void
_srt_simple_input_device_init (SrtSimpleInputDevice *self)
{
}

static void
srt_simple_input_device_finalize (GObject *object)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (object);

  g_free (self->dev_node);
  g_free (self->sys_path);
  g_free (self->subsystem);
  g_strfreev (self->udev_properties);
  g_free (self->uevent);

  g_free (self->hid_ancestor.sys_path);
  g_free (self->hid_ancestor.uevent);
  g_free (self->hid_ancestor.name);
  g_free (self->hid_ancestor.phys);
  g_free (self->hid_ancestor.uniq);

  g_free (self->input_ancestor.sys_path);
  g_free (self->input_ancestor.uevent);
  g_free (self->input_ancestor.name);
  g_free (self->input_ancestor.phys);
  g_free (self->input_ancestor.uniq);

  g_free (self->usb_device_ancestor.sys_path);
  g_free (self->usb_device_ancestor.uevent);
  g_free (self->usb_device_ancestor.manufacturer);
  g_free (self->usb_device_ancestor.product);
  g_free (self->usb_device_ancestor.serial);

  G_OBJECT_CLASS (_srt_simple_input_device_parent_class)->finalize (object);
}

static void
_srt_simple_input_device_class_init (SrtSimpleInputDeviceClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->finalize = srt_simple_input_device_finalize;
}

static SrtInputDeviceInterfaceFlags
srt_simple_input_device_get_interface_flags (SrtInputDevice *device)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

  return self->iface_flags;
}

static SrtInputDeviceTypeFlags
srt_simple_input_device_get_type_flags (SrtInputDevice *device)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

  return self->type_flags;
}

static const char *
srt_simple_input_device_get_dev_node (SrtInputDevice *device)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

  return self->dev_node;
}

static const char *
srt_simple_input_device_get_subsystem (SrtInputDevice *device)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

  return self->subsystem;
}

static const char *
srt_simple_input_device_get_sys_path (SrtInputDevice *device)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

  return self->sys_path;
}

static gboolean
srt_simple_input_device_get_identity (SrtInputDevice *device,
                                      unsigned int *bus_type,
                                      unsigned int *vendor_id,
                                      unsigned int *product_id,
                                      unsigned int *version)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

  if (self->bus_type == 0
      && self->vendor_id == 0
      && self->product_id == 0
      && self->version == 0)
    return FALSE;

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

static gchar **
srt_simple_input_device_dup_udev_properties (SrtInputDevice *device)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

  return g_strdupv (self->udev_properties);
}

static gchar *
srt_simple_input_device_dup_uevent (SrtInputDevice *device)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

  return g_strdup (self->uevent);
}

static const SrtEvdevCapabilities *
srt_simple_input_device_peek_event_capabilities (SrtInputDevice *device)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

  return &self->evdev_caps;
}

static const char *
srt_simple_input_device_get_hid_sys_path (SrtInputDevice *device)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

  return self->hid_ancestor.sys_path;
}

static gboolean
srt_simple_input_device_get_hid_identity (SrtInputDevice *device,
                                          unsigned int *bus_type,
                                          unsigned int *vendor_id,
                                          unsigned int *product_id,
                                          const char **name,
                                          const char **phys,
                                          const char **uniq)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

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

static gchar *
srt_simple_input_device_dup_hid_uevent (SrtInputDevice *device)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

  return g_strdup (self->hid_ancestor.uevent);
}

static const char *
srt_simple_input_device_get_input_sys_path (SrtInputDevice *device)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

  return self->input_ancestor.sys_path;
}

static gboolean
srt_simple_input_device_get_input_identity (SrtInputDevice *device,
                                            unsigned int *bus_type,
                                            unsigned int *vendor_id,
                                            unsigned int *product_id,
                                            unsigned int *version,
                                            const char **name,
                                            const char **phys,
                                            const char **uniq)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

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

static gchar *
srt_simple_input_device_dup_input_uevent (SrtInputDevice *device)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

  return g_strdup (self->input_ancestor.uevent);
}

static const char *
srt_simple_input_device_get_usb_device_sys_path (SrtInputDevice *device)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

  return self->usb_device_ancestor.sys_path;
}

static gboolean
srt_simple_input_device_get_usb_device_identity (SrtInputDevice *device,
                                                 unsigned int *vendor_id,
                                                 unsigned int *product_id,
                                                 unsigned int *version,
                                                 const char **manufacturer,
                                                 const char **product,
                                                 const char **serial)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

  if (self->usb_device_ancestor.sys_path == NULL)
    return FALSE;

  if (vendor_id != NULL)
    *vendor_id = self->usb_device_ancestor.vendor_id;

  if (product_id != NULL)
    *product_id = self->usb_device_ancestor.product_id;

  if (version != NULL)
    *version = self->usb_device_ancestor.device_version;

  if (manufacturer != NULL)
    *manufacturer = self->usb_device_ancestor.manufacturer;

  if (product != NULL)
    *product = self->usb_device_ancestor.product;

  if (serial != NULL)
    *serial = self->usb_device_ancestor.serial;

  return TRUE;
}

static gchar *
srt_simple_input_device_dup_usb_device_uevent (SrtInputDevice *device)
{
  SrtSimpleInputDevice *self = SRT_SIMPLE_INPUT_DEVICE (device);

  return g_strdup (self->usb_device_ancestor.uevent);
}

static void
srt_simple_input_device_iface_init (SrtInputDeviceInterface *iface)
{
#define IMPLEMENT(x) iface->x = srt_simple_input_device_ ## x

  IMPLEMENT (get_type_flags);
  IMPLEMENT (get_interface_flags);
  IMPLEMENT (get_dev_node);
  IMPLEMENT (get_sys_path);
  IMPLEMENT (get_subsystem);
  IMPLEMENT (get_identity);
  IMPLEMENT (dup_udev_properties);
  IMPLEMENT (dup_uevent);
  IMPLEMENT (peek_event_capabilities);

  IMPLEMENT (get_hid_sys_path);
  IMPLEMENT (get_hid_identity);
  IMPLEMENT (get_input_sys_path);
  IMPLEMENT (get_input_identity);
  IMPLEMENT (get_usb_device_sys_path);
  IMPLEMENT (get_usb_device_identity);

  IMPLEMENT (dup_hid_uevent);
  IMPLEMENT (dup_input_uevent);
  IMPLEMENT (dup_usb_device_uevent);

#undef IMPLEMENT
}

static gchar *
dup_json_string_member (JsonObject *obj,
                        const gchar *name)
{
  JsonNode *node = json_object_get_member (obj, name);

  if (node == NULL)
    return NULL;

  /* This returns NULL without error if it is a non-string */
  return json_node_dup_string (node);
}

static unsigned long
get_json_hex_member (JsonObject *obj,
                     const gchar *name)
{
  JsonNode *node = json_object_get_member (obj, name);
  const char *s;

  if (node == NULL)
    return 0;

  s = json_node_get_string (node);

  if (s == NULL)
    return 0;

  if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
    s += 2;

  return strtoul (s, NULL, 16);
}

static JsonObject *
get_json_object_member (JsonObject *obj,
                        const char *name)
{
  JsonNode *node = json_object_get_member (obj, name);

  if (node != NULL && JSON_NODE_HOLDS_OBJECT (node))
    return json_node_get_object (node);

  return NULL;
}

static gchar *
dup_json_uevent (JsonObject *obj)
{
  return _srt_json_object_dup_array_of_lines_member (obj, "uevent");
}

static void
get_json_evdev_caps (JsonObject *obj,
                     const char *name,
                     unsigned long *longs,
                     size_t n_longs)
{
  JsonNode *node = json_object_get_member (obj, name);
  /* The first pointer that is out of bounds for longs */
  unsigned char *limit = (unsigned char *) &longs[n_longs];
  /* The output position in longs */
  unsigned char *out = (unsigned char *) longs;
  /* The input position in the string we are parsing */
  const char *iter;
  size_t i;

  if (node == NULL)
    return;

  iter = json_node_get_string (node);

  if (iter == NULL)
    return;

  while (*iter != '\0')
    {
      unsigned int this_byte;
      int used;

      while (*iter == ' ')
        iter++;

      if (sscanf (iter, "%x%n", &this_byte, &used) == 1)
        iter += used;
      else
        break;

      if (out < limit)
        *(out++) = (unsigned char) this_byte;
      else
        break;
    }

  for (i = 0; i < n_longs; i++)
    longs[i] = GULONG_FROM_LE (longs[i]);
}

SrtSimpleInputDevice *
_srt_simple_input_device_new_from_json (JsonObject *obj)
{
  SrtSimpleInputDevice *self = g_object_new (SRT_TYPE_SIMPLE_INPUT_DEVICE,
                                             NULL);
  JsonObject *sub;

  self->sys_path = dup_json_string_member (obj, "sys_path");
  self->dev_node = dup_json_string_member (obj, "dev_node");
  self->subsystem = dup_json_string_member (obj, "subsystem");
  self->bus_type  = get_json_hex_member (obj, "bus_type");
  self->vendor_id  = get_json_hex_member (obj, "vendor_id");
  self->product_id  = get_json_hex_member (obj, "product_id");
  self->version  = get_json_hex_member (obj, "version");

  self->iface_flags = srt_get_flags_from_json_array (SRT_TYPE_INPUT_DEVICE_INTERFACE_FLAGS,
                                                     obj,
                                                     "interface_flags",
                                                     SRT_INPUT_DEVICE_INTERFACE_FLAGS_NONE);
  self->type_flags = srt_get_flags_from_json_array (SRT_TYPE_INPUT_DEVICE_TYPE_FLAGS,
                                                    obj,
                                                    "type_flags",
                                                    SRT_INPUT_DEVICE_TYPE_FLAGS_NONE);

  if ((sub = get_json_object_member (obj, "evdev")) != NULL)
    {
      get_json_evdev_caps (sub, "raw_types", self->evdev_caps.ev,
                           G_N_ELEMENTS (self->evdev_caps.ev));
      get_json_evdev_caps (sub, "raw_abs", self->evdev_caps.abs,
                           G_N_ELEMENTS (self->evdev_caps.abs));
      get_json_evdev_caps (sub, "raw_rel", self->evdev_caps.rel,
                           G_N_ELEMENTS (self->evdev_caps.rel));
      get_json_evdev_caps (sub, "raw_keys", self->evdev_caps.keys,
                           G_N_ELEMENTS (self->evdev_caps.keys));
      get_json_evdev_caps (sub, "raw_input_properties",
                           self->evdev_caps.props,
                           G_N_ELEMENTS (self->evdev_caps.props));
    }

  self->udev_properties = _srt_json_object_dup_strv_member (obj, "udev_properties", NULL);
  self->uevent = dup_json_uevent (obj);

  if ((sub = get_json_object_member (obj, "hid_ancestor")) != NULL)
    {
      self->hid_ancestor.sys_path = dup_json_string_member (sub, "sys_path");
      self->hid_ancestor.name = dup_json_string_member (sub, "name");
      self->hid_ancestor.bus_type = get_json_hex_member (sub, "bus_type");
      self->hid_ancestor.vendor_id = get_json_hex_member (sub, "vendor_id");
      self->hid_ancestor.product_id = get_json_hex_member (sub, "product_id");
      self->hid_ancestor.uniq = dup_json_string_member (sub, "uniq");
      self->hid_ancestor.phys = dup_json_string_member (sub, "phys");
      self->hid_ancestor.uevent = dup_json_uevent (sub);
    }

  if ((sub = get_json_object_member (obj, "input_ancestor")) != NULL)
    {
      self->input_ancestor.sys_path = dup_json_string_member (sub, "sys_path");
      self->input_ancestor.name = dup_json_string_member (sub, "name");
      self->input_ancestor.bus_type = get_json_hex_member (sub, "bus_type");
      self->input_ancestor.vendor_id = get_json_hex_member (sub, "vendor_id");
      self->input_ancestor.product_id = get_json_hex_member (sub, "product_id");
      self->input_ancestor.version = get_json_hex_member (sub, "version");
      self->input_ancestor.uniq = dup_json_string_member (sub, "uniq");
      self->input_ancestor.phys = dup_json_string_member (sub, "phys");
      self->input_ancestor.uevent = dup_json_uevent (sub);
    }

  if ((sub = get_json_object_member (obj, "usb_device_ancestor")) != NULL)
    {
      self->usb_device_ancestor.sys_path = dup_json_string_member (sub, "sys_path");
      self->usb_device_ancestor.vendor_id = get_json_hex_member (sub, "vendor_id");
      self->usb_device_ancestor.product_id = get_json_hex_member (sub, "product_id");
      self->usb_device_ancestor.device_version = get_json_hex_member (sub, "version");
      self->usb_device_ancestor.manufacturer = dup_json_string_member (sub, "manufacturer");
      self->usb_device_ancestor.product = dup_json_string_member (sub, "product");
      self->usb_device_ancestor.serial = dup_json_string_member (sub, "serial");
      self->usb_device_ancestor.uevent = dup_json_uevent (sub);
    }

  return self;
}
