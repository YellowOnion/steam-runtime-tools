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

#pragma once

#include <linux/input.h>

#include <glib-unix.h>
#include <libglnx.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/input-device.h"
#include "steam-runtime-tools/input-device-internal.h"

typedef struct _MockInputDevice MockInputDevice;
typedef struct _MockInputDeviceClass MockInputDeviceClass;

GType mock_input_device_get_type (void);
#define MOCK_TYPE_INPUT_DEVICE (mock_input_device_get_type ())
#define MOCK_INPUT_DEVICE(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), MOCK_TYPE_INPUT_DEVICE, MockInputDevice))
#define MOCK_IS_INPUT_DEVICE(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), MOCK_TYPE_INPUT_DEVICE))
#define MOCK_INPUT_DEVICE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MOCK_TYPE_INPUT_DEVICE, MockInputDeviceClass))
#define MOCK_INPUT_DEVICE_CLASS(c) (G_TYPE_CHECK_CLASS_CAST ((c), MOCK_TYPE_INPUT_DEVICE, MockInputDeviceClass))
#define MOCK_IS_INPUT_DEVICE_CLASS(c) (G_TYPE_CHECK_CLASS_TYPE ((c), MOCK_TYPE_INPUT_DEVICE))

struct _MockInputDevice
{
  GObject parent;

  gchar *sys_path;
  gchar *dev_node;
  gchar *subsystem;
  gchar **udev_properties;
  gchar *uevent;
};

struct _MockInputDeviceClass
{
  GObjectClass parent;
};

MockInputDevice *mock_input_device_new (void);

typedef struct _MockInputDeviceMonitor MockInputDeviceMonitor;
typedef struct _MockInputDeviceMonitorClass MockInputDeviceMonitorClass;

GType mock_input_device_monitor_get_type (void);
#define MOCK_TYPE_INPUT_DEVICE_MONITOR (mock_input_device_monitor_get_type ())
#define MOCK_INPUT_DEVICE_MONITOR(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), MOCK_TYPE_INPUT_DEVICE_MONITOR, MockInputDeviceMonitor))
#define MOCK_IS_INPUT_DEVICE_MONITOR(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), MOCK_TYPE_INPUT_DEVICE_MONITOR))
#define MOCK_INPUT_DEVICE_MONITOR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), MOCK_TYPE_INPUT_DEVICE_MONITOR, MockInputDeviceMonitorClass))
#define MOCK_INPUT_DEVICE_MONITOR_CLASS(c) (G_TYPE_CHECK_CLASS_CAST ((c), MOCK_TYPE_INPUT_DEVICE_MONITOR, MockInputDeviceMonitorClass))
#define MOCK_IS_INPUT_DEVICE_MONITOR_CLASS(c) (G_TYPE_CHECK_CLASS_TYPE ((c), MOCK_TYPE_INPUT_DEVICE_MONITOR))

struct _MockInputDeviceMonitor
{
  GObject parent;

  GMainContext *monitor_context;
  GHashTable *devices;
  GList *sources;

  SrtInputDeviceMonitorFlags flags;
  enum
  {
    NOT_STARTED = 0,
    STARTED,
    STOPPED
  } state;
};

struct _MockInputDeviceMonitorClass
{
  GObjectClass parent;
};

MockInputDeviceMonitor *mock_input_device_monitor_new (SrtInputDeviceMonitorFlags flags);

void mock_input_device_monitor_add (MockInputDeviceMonitor *self,
                                    MockInputDevice *device);
void mock_input_device_monitor_remove (MockInputDeviceMonitor *self,
                                       MockInputDevice *device);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (MockInputDevice, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (MockInputDeviceMonitor, g_object_unref)
