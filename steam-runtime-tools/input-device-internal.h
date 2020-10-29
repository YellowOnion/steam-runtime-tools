/*<private_header>*/
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

#include "steam-runtime-tools/input-device.h"

struct _SrtInputDeviceInterface
{
  GTypeInterface parent;

  SrtInputDeviceInterfaceFlags (*get_interface_flags) (SrtInputDevice *device);

  const char * (*get_dev_node) (SrtInputDevice *device);
  const char * (*get_sys_path) (SrtInputDevice *device);
  const char * (*get_subsystem) (SrtInputDevice *device);
  gchar ** (*dup_udev_properties) (SrtInputDevice *device);
  gchar * (*dup_uevent) (SrtInputDevice *device);
  gboolean (*get_identity) (SrtInputDevice *device,
                            unsigned int *bus_type,
                            unsigned int *vendor_id,
                            unsigned int *product_id,
                            unsigned int *version);

  const char * (*get_hid_sys_path) (SrtInputDevice *device);
  gchar * (*dup_hid_uevent) (SrtInputDevice *device);
  gboolean (*get_hid_identity) (SrtInputDevice *device,
                                unsigned int *bus_type,
                                unsigned int *vendor_id,
                                unsigned int *product_id,
                                const char **name,
                                const char **phys,
                                const char **uniq);

  const char * (*get_input_sys_path) (SrtInputDevice *device);
  gchar * (*dup_input_uevent) (SrtInputDevice *device);
  gboolean (*get_input_identity) (SrtInputDevice *device,
                                  unsigned int *bus_type,
                                  unsigned int *vendor_id,
                                  unsigned int *product_id,
                                  unsigned int *version,
                                  const char **name,
                                  const char **phys,
                                  const char **uniq);

  const char * (*get_usb_device_sys_path) (SrtInputDevice *device);
  gchar * (*dup_usb_device_uevent) (SrtInputDevice *device);
  gboolean (*get_usb_device_identity) (SrtInputDevice *device,
                                       unsigned int *vendor_id,
                                       unsigned int *product_id,
                                       unsigned int *device_version,
                                       const char **manufacturer,
                                       const char **product,
                                       const char **serial);
};

struct _SrtInputDeviceMonitorInterface
{
  GTypeInterface parent;

  /* Signals */
  void (*added) (SrtInputDeviceMonitor *monitor,
                 SrtInputDevice *device);
  void (*removed) (SrtInputDeviceMonitor *monitor,
                   SrtInputDevice *device);
  void (*all_for_now) (SrtInputDeviceMonitor *monitor);

  /* Virtual methods */
  void (*request_raw_hid) (SrtInputDeviceMonitor *monitor);
  void (*request_evdev) (SrtInputDeviceMonitor *monitor);
  gboolean (*start) (SrtInputDeviceMonitor *monitor,
                     GError **error);
  void (*stop) (SrtInputDeviceMonitor *monitor);
};

void _srt_input_device_monitor_emit_added (SrtInputDeviceMonitor *monitor,
                                           SrtInputDevice *device);
void _srt_input_device_monitor_emit_removed (SrtInputDeviceMonitor *monitor,
                                             SrtInputDevice *device);
void _srt_input_device_monitor_emit_all_for_now (SrtInputDeviceMonitor *monitor);


gboolean _srt_get_identity_from_evdev (int fd,
                                       guint32 *bus_type,
                                       guint32 *vendor,
                                       guint32 *product,
                                       guint32 *version,
                                       gchar **name,
                                       gchar **phys,
                                       gchar **uniq);
gboolean _srt_get_identity_from_raw_hid (int fd,
                                         guint32 *bus_type,
                                         guint32 *vendor,
                                         guint32 *product,
                                         gchar **name,
                                         gchar **phys,
                                         gchar **uniq);
gchar *_srt_input_device_uevent_field (const char *text,
                                       const char *key);
gboolean _srt_input_device_uevent_field_equals (const char *text,
                                                const char *key,
                                                const char *want_value);
gboolean _srt_get_identity_from_hid_uevent (const char *text,
                                            guint32 *bus_type,
                                            guint32 *product_id,
                                            guint32 *vendor_id,
                                            gchar **name,
                                            gchar **phys,
                                            gchar **uniq);
