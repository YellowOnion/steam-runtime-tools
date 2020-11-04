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
