/*<private_header>*/
/*
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

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/input-device-internal.h"

#define SRT_TYPE_SIMPLE_INPUT_DEVICE (_srt_simple_input_device_get_type ())

typedef struct _SrtSimpleInputDevice SrtSimpleInputDevice;
typedef struct _SrtSimpleInputDeviceClass SrtSimpleInputDeviceClass;

#define SRT_TYPE_SIMPLE_INPUT_DEVICE (_srt_simple_input_device_get_type ())
#define SRT_SIMPLE_INPUT_DEVICE(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), SRT_TYPE_SIMPLE_INPUT_DEVICE, SrtSimpleInputDevice))
#define SRT_IS_SIMPLE_INPUT_DEVICE(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), SRT_TYPE_SIMPLE_INPUT_DEVICE))
#define SRT_SIMPLE_INPUT_DEVICE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), SRT_TYPE_SIMPLE_INPUT_DEVICE, SrtSimpleInputDeviceClass))
#define SRT_SIMPLE_INPUT_DEVICE_CLASS(c) (G_TYPE_CHECK_CLASS_CAST ((c), SRT_TYPE_SIMPLE_INPUT_DEVICE, SrtSimpleInputDeviceClass))
#define SRT_IS_SIMPLE_INPUT_DEVICE_CLASS(c) (G_TYPE_CHECK_CLASS_TYPE ((c), SRT_TYPE_SIMPLE_INPUT_DEVICE))

G_GNUC_INTERNAL
GType _srt_simple_input_device_get_type (void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtSimpleInputDevice, g_object_unref)

struct _SrtSimpleInputDevice
{
  GObject parent;

  gchar *dev_node;
  gchar *sys_path;
  gchar *subsystem;
  gchar **udev_properties;
  gchar *uevent;

  struct
  {
    gchar *sys_path;
    gchar *uevent;
    gchar *name;
    gchar *phys;
    gchar *uniq;
    guint32 bus_type;
    guint32 product_id;
    guint32 vendor_id;
  } hid_ancestor;

  struct
  {
    gchar *sys_path;
    gchar *uevent;
    gchar *name;
    gchar *phys;
    gchar *uniq;
    guint32 bus_type;
    guint32 product_id;
    guint32 vendor_id;
    guint32 version;
  } input_ancestor;

  struct
  {
    gchar *sys_path;
    gchar *uevent;
    gchar *manufacturer;
    gchar *product;
    gchar *serial;
    guint32 product_id;
    guint32 vendor_id;
    guint32 device_version;
  } usb_device_ancestor;

  SrtEvdevCapabilities evdev_caps;
  SrtInputDeviceInterfaceFlags iface_flags;
  SrtInputDeviceTypeFlags type_flags;
  guint32 bus_type;
  guint32 vendor_id;
  guint32 product_id;
  guint32 version;
};

struct _SrtSimpleInputDeviceClass
{
  GObjectClass parent;
};
