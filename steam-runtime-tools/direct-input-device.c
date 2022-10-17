/*
 * Direct /dev,/sys-based input device monitor, very loosely based on SDL code.
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

#include "steam-runtime-tools/direct-input-device-internal.h"

#include <dirent.h>
#include <linux/input.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>

#include <glib-unix.h>
#include <libglnx.h>

#include "steam-runtime-tools/input-device.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/input-device-internal.h"
#include "steam-runtime-tools/utils-internal.h"

static void srt_direct_input_device_iface_init (SrtInputDeviceInterface *iface);
static void srt_direct_input_device_monitor_iface_init (SrtInputDeviceMonitorInterface *iface);

static void
remove_last_component_in_place (char *path)
{
  char *slash = strrchr (path, '/');

  if (slash == NULL)
    *path = '\0';
  else
    *slash = '\0';
}

/*
 * @path: The path to a device directory in /sys
 *
 * Returns: (nullable) (transfer full): The closest ancestor of @path
 *  that is an input device with evdev capabilities, or %NULL if not found
 */
static gchar *
find_input_ancestor (const char *path)
{
  g_autofree char *ancestor = NULL;

  for (ancestor = g_strdup (path);
       g_str_has_prefix (ancestor, "/sys/");
       remove_last_component_in_place (ancestor))
    {
      g_autofree char *subsys = g_build_filename (ancestor, "subsystem", NULL);
      g_autofree char *caps = g_build_filename (ancestor, "capabilities", "ev", NULL);
      g_autofree char *target = glnx_readlinkat_malloc (AT_FDCWD, subsys,
                                                        NULL, NULL);
      const char *slash = NULL;

      if (!g_file_test (caps, G_FILE_TEST_IS_REGULAR))
        continue;

      if (target != NULL)
        slash = strrchr (target, '/');

      if (slash == NULL || strcmp (slash + 1, "input") != 0)
        continue;

      return g_steal_pointer (&ancestor);
    }

  return NULL;
}

/*
 * @path: The path to a device directory in /sys
 * @subsystem: A desired subsystem, such as "hid" or "usb", or %NULL to accept any
 * @devtype: A desired device type, such as "usb_device", or %NULL to accept any
 * @uevent_out: (out) (optional): Optionally return the text of /sys/.../uevent
 *
 * Returns: (nullable) (transfer full): The closest ancestor of @path
 *  that has a subsystem of @subsystem, or %NULL if not found.
 */
static gchar *
get_ancestor_with_subsystem_devtype (const char *path,
                                     const char *subsystem,
                                     const char *devtype,
                                     gchar **uevent_out)
{
  g_autofree char *ancestor = NULL;

  for (ancestor = g_strdup (path);
       g_str_has_prefix (ancestor, "/sys/");
       remove_last_component_in_place (ancestor))
    {
      g_autofree char *uevent = g_build_filename (ancestor, "uevent", NULL);
      g_autofree char *text = NULL;

      /* If it doesn't have a uevent file, it isn't a real device */
      if (!g_file_get_contents (uevent, &text, NULL, NULL))
        continue;

      if (subsystem != NULL)
        {
          g_autofree char *subsys = g_build_filename (ancestor, "subsystem", NULL);
          g_autofree char *target = glnx_readlinkat_malloc (AT_FDCWD, subsys,
                                                            NULL, NULL);
          const char *slash = NULL;

          if (target != NULL)
            slash = strrchr (target, '/');

          if (slash == NULL || strcmp (slash + 1, subsystem) != 0)
            continue;
        }

      if (devtype != NULL
          && !_srt_input_device_uevent_field_equals (text, "DEVTYPE", devtype))
        continue;

      if (uevent_out != NULL)
        *uevent_out = g_steal_pointer (&text);

      return g_steal_pointer (&ancestor);
    }

  return NULL;
}

static GQuark quark_hidraw = 0;
static GQuark quark_input = 0;

struct _SrtDirectInputDevice
{
  GObject parent;

  char *sys_path;
  gchar *dev_node;
  GQuark subsystem;

  struct
  {
    gchar *sys_path;
  } hid_ancestor;

  struct
  {
    gchar *sys_path;
  } input_ancestor;

  struct
  {
    gchar *sys_path;
    gchar *manufacturer;
    gchar *product;
    gchar *serial;
    guint32 product_id;
    guint32 vendor_id;
    guint32 device_version;
  } usb_device_ancestor;

  struct
  {
    SrtEvdevCapabilities caps;
    gchar *name;
    gchar *phys;
    gchar *uniq;
    guint32 bus_type;
    guint32 product_id;
    guint32 vendor_id;
    guint32 version;
  } evdev;

  struct
  {
    gchar *name;
    gchar *phys;
    gchar *uniq;
    guint32 bus_type;
    guint32 product_id;
    guint32 vendor_id;
  } hid;

  SrtInputDeviceInterfaceFlags iface_flags;
};

struct _SrtDirectInputDeviceClass
{
  GObjectClass parent;
};

struct _SrtDirectInputDeviceMonitor
{
  GObject parent;
  GHashTable *devices;
  GMainContext *monitor_context;
  GSource *monitor_source;
  SrtInputDeviceMonitorFlags flags;
  gboolean want_evdev;
  gboolean want_hidraw;
  int inotify_fd;
  int dev_watch;
  int devinput_watch;
  enum
  {
    NOT_STARTED = 0,
    STARTED,
    STOPPED
  } state;
};

struct _SrtDirectInputDeviceMonitorClass
{
  GObjectClass parent;
};

G_DEFINE_TYPE_WITH_CODE (SrtDirectInputDevice,
                         _srt_direct_input_device,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SRT_TYPE_INPUT_DEVICE,
                                                srt_direct_input_device_iface_init))

G_DEFINE_TYPE_WITH_CODE (SrtDirectInputDeviceMonitor,
                         _srt_direct_input_device_monitor,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SRT_TYPE_INPUT_DEVICE_MONITOR,
                                                srt_direct_input_device_monitor_iface_init))

static void
_srt_direct_input_device_init (SrtDirectInputDevice *self)
{
}

static void
srt_direct_input_device_finalize (GObject *object)
{
  SrtDirectInputDevice *self = SRT_DIRECT_INPUT_DEVICE (object);

  g_clear_pointer (&self->sys_path, free);
  g_clear_pointer (&self->dev_node, g_free);
  g_clear_pointer (&self->hid_ancestor.sys_path, g_free);
  g_clear_pointer (&self->input_ancestor.sys_path, g_free);
  g_clear_pointer (&self->usb_device_ancestor.sys_path, g_free);
  g_clear_pointer (&self->usb_device_ancestor.manufacturer, g_free);
  g_clear_pointer (&self->usb_device_ancestor.product, g_free);
  g_clear_pointer (&self->usb_device_ancestor.serial, g_free);
  g_clear_pointer (&self->hid.name, g_free);
  g_clear_pointer (&self->hid.phys, g_free);
  g_clear_pointer (&self->hid.uniq, g_free);
  g_clear_pointer (&self->evdev.name, g_free);
  g_clear_pointer (&self->evdev.phys, g_free);
  g_clear_pointer (&self->evdev.uniq, g_free);

  G_OBJECT_CLASS (_srt_direct_input_device_parent_class)->finalize (object);
}

static void
_srt_direct_input_device_class_init (SrtDirectInputDeviceClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->finalize = srt_direct_input_device_finalize;
}

static SrtInputDeviceInterfaceFlags
srt_direct_input_device_get_interface_flags (SrtInputDevice *device)
{
  SrtDirectInputDevice *self = SRT_DIRECT_INPUT_DEVICE (device);

  return self->iface_flags;
}

static const char *
srt_direct_input_device_get_dev_node (SrtInputDevice *device)
{
  SrtDirectInputDevice *self = SRT_DIRECT_INPUT_DEVICE (device);

  return self->dev_node;
}

static const char *
srt_direct_input_device_get_subsystem (SrtInputDevice *device)
{
  SrtDirectInputDevice *self = SRT_DIRECT_INPUT_DEVICE (device);

  return g_quark_to_string (self->subsystem);
}

static const char *
srt_direct_input_device_get_sys_path (SrtInputDevice *device)
{
  SrtDirectInputDevice *self = SRT_DIRECT_INPUT_DEVICE (device);

  return self->sys_path;
}

static const SrtEvdevCapabilities *
srt_direct_input_device_peek_event_capabilities (SrtInputDevice *device)
{
  SrtDirectInputDevice *self = SRT_DIRECT_INPUT_DEVICE (device);

  return &self->evdev.caps;
}

static const char *
srt_direct_input_device_get_hid_sys_path (SrtInputDevice *device)
{
  SrtDirectInputDevice *self = SRT_DIRECT_INPUT_DEVICE (device);

  return self->hid_ancestor.sys_path;
}

static gboolean
srt_direct_input_device_get_hid_identity (SrtInputDevice *device,
                                          unsigned int *bus_type,
                                          unsigned int *vendor_id,
                                          unsigned int *product_id,
                                          const char **name,
                                          const char **phys,
                                          const char **uniq)
{
  SrtDirectInputDevice *self = SRT_DIRECT_INPUT_DEVICE (device);

  if (self->hid_ancestor.sys_path == NULL
      && (self->iface_flags & SRT_INPUT_DEVICE_INTERFACE_FLAGS_RAW_HID) == 0)
    return FALSE;

  if (bus_type != NULL)
    *bus_type = self->hid.bus_type;

  if (vendor_id != NULL)
    *vendor_id = self->hid.vendor_id;

  if (product_id != NULL)
    *product_id = self->hid.product_id;

  if (name != NULL)
    *name = self->hid.name;

  if (phys != NULL)
    *phys = self->hid.phys;

  if (uniq != NULL)
    *uniq = self->hid.uniq;

  return TRUE;
}

static const char *
srt_direct_input_device_get_input_sys_path (SrtInputDevice *device)
{
  SrtDirectInputDevice *self = SRT_DIRECT_INPUT_DEVICE (device);

  return self->input_ancestor.sys_path;
}

static gboolean
srt_direct_input_device_get_input_identity (SrtInputDevice *device,
                                            unsigned int *bus_type,
                                            unsigned int *vendor_id,
                                            unsigned int *product_id,
                                            unsigned int *version,
                                            const char **name,
                                            const char **phys,
                                            const char **uniq)
{
  SrtDirectInputDevice *self = SRT_DIRECT_INPUT_DEVICE (device);

  if (self->input_ancestor.sys_path == NULL
      && (self->iface_flags & SRT_INPUT_DEVICE_INTERFACE_FLAGS_EVENT) == 0)
    return FALSE;

  if (bus_type != NULL)
    *bus_type = self->evdev.bus_type;

  if (vendor_id != NULL)
    *vendor_id = self->evdev.vendor_id;

  if (product_id != NULL)
    *product_id = self->evdev.product_id;

  if (version != NULL)
    *version = self->evdev.version;

  if (name != NULL)
    *name = self->evdev.name;

  if (phys != NULL)
    *phys = self->evdev.phys;

  if (uniq != NULL)
    *uniq = self->evdev.uniq;

  return TRUE;
}

static const char *
srt_direct_input_device_get_usb_device_sys_path (SrtInputDevice *device)
{
  SrtDirectInputDevice *self = SRT_DIRECT_INPUT_DEVICE (device);

  return self->usb_device_ancestor.sys_path;
}

static gboolean
srt_direct_input_device_get_usb_device_identity (SrtInputDevice *device,
                                                 unsigned int *vendor_id,
                                                 unsigned int *product_id,
                                                 unsigned int *device_version,
                                                 const char **manufacturer,
                                                 const char **product,
                                                 const char **serial)
{
  SrtDirectInputDevice *self = SRT_DIRECT_INPUT_DEVICE (device);

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

static void
srt_direct_input_device_iface_init (SrtInputDeviceInterface *iface)
{
#define IMPLEMENT(x) iface->x = srt_direct_input_device_ ## x

  IMPLEMENT (get_interface_flags);
  IMPLEMENT (get_dev_node);
  IMPLEMENT (get_sys_path);
  IMPLEMENT (get_subsystem);
  IMPLEMENT (peek_event_capabilities);

  IMPLEMENT (get_hid_sys_path);
  IMPLEMENT (get_input_sys_path);
  IMPLEMENT (get_usb_device_sys_path);

  IMPLEMENT (get_hid_identity);
  IMPLEMENT (get_input_identity);
  IMPLEMENT (get_usb_device_identity);

#undef IMPLEMENT
}

typedef enum
{
  PROP_0,
  PROP_FLAGS,
  PROP_IS_ACTIVE,
  N_PROPERTIES
} Property;

static void
_srt_direct_input_device_monitor_init (SrtDirectInputDeviceMonitor *self)
{
  self->monitor_context = g_main_context_ref_thread_default ();
  self->state = NOT_STARTED;
  self->devices = g_hash_table_new_full (g_str_hash,
                                         g_str_equal,
                                         NULL,
                                         g_object_unref);
  self->inotify_fd = -1;
  self->dev_watch = -1;
  self->devinput_watch = -1;
}

static void
srt_direct_input_device_monitor_get_property (GObject *object,
                                            guint prop_id,
                                            GValue *value,
                                            GParamSpec *pspec)
{
  SrtDirectInputDeviceMonitor *self = SRT_DIRECT_INPUT_DEVICE_MONITOR (object);

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
srt_direct_input_device_monitor_set_property (GObject *object,
                                            guint prop_id,
                                            const GValue *value,
                                            GParamSpec *pspec)
{
  SrtDirectInputDeviceMonitor *self = SRT_DIRECT_INPUT_DEVICE_MONITOR (object);

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
srt_direct_input_device_monitor_dispose (GObject *object)
{
  srt_input_device_monitor_stop (SRT_INPUT_DEVICE_MONITOR (object));
  G_OBJECT_CLASS (_srt_direct_input_device_monitor_parent_class)->dispose (object);
}

static void
srt_direct_input_device_monitor_finalize (GObject *object)
{
  srt_input_device_monitor_stop (SRT_INPUT_DEVICE_MONITOR (object));
  G_OBJECT_CLASS (_srt_direct_input_device_monitor_parent_class)->finalize (object);
}

static void
_srt_direct_input_device_monitor_class_init (SrtDirectInputDeviceMonitorClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_direct_input_device_monitor_get_property;
  object_class->set_property = srt_direct_input_device_monitor_set_property;
  object_class->dispose = srt_direct_input_device_monitor_dispose;
  object_class->finalize = srt_direct_input_device_monitor_finalize;

  g_object_class_override_property (object_class, PROP_FLAGS, "flags");
  g_object_class_override_property (object_class, PROP_IS_ACTIVE, "is-active");

  quark_hidraw = g_quark_from_static_string ("hidraw");
  quark_input = g_quark_from_static_string ("input");
}

/*
 * Get a sysfs attribute that is a string.
 *
 * On success, set *out and return TRUE.
 * On failure, leave *out untouched and return FALSE.
 */
static gboolean
dup_string (const char *sys_path,
            const char *attribute,
            gchar **out)
{
  g_autofree gchar *text = NULL;
  g_autofree gchar *child = NULL;

  if (sys_path == NULL)
    return FALSE;

  child = g_build_filename (sys_path, attribute, NULL);

  if (!g_file_get_contents (child, &text, NULL, NULL))
    return FALSE;

  g_strchomp (text);

  if (out != NULL)
    *out = g_steal_pointer (&text);

  return TRUE;
}

/*
 * Get a sysfs attribute that is a uint32 (or smaller) in hexadecimal
 * (with or without 0x prefix).
 *
 * On success, set *out and return TRUE.
 * On failure, leave *out untouched and return FALSE.
 */
static gboolean
get_uint32_hex (const char *sys_path,
                const char *attribute,
                guint32 *out)
{
  g_autofree gchar *buf = NULL;
  const char *tmp;
  guint64 ret;
  gchar *endptr;

  if (!dup_string (sys_path, attribute, &buf))
    return FALSE;

  tmp = buf;

  if (tmp[0] == '0' && (tmp[1] == 'x' || tmp[1] == 'X'))
    tmp += 2;

  ret = g_ascii_strtoull (tmp, &endptr, 16);

  if (endptr == NULL
      || (*endptr != '\0' && *endptr != '\n')
      || ret > G_MAXUINT32)
    return FALSE;

  if (out != NULL)
    *out = (guint32) ret;

  return TRUE;
}

static void
read_hid_ancestor (SrtDirectInputDevice *device)
{
  g_autofree gchar *uevent = NULL;

  if (device->hid_ancestor.sys_path == NULL)
    return;

  if (!dup_string (device->hid_ancestor.sys_path, "uevent", &uevent))
    return;

  _srt_get_identity_from_hid_uevent (uevent,
                                     &device->hid.bus_type,
                                     &device->hid.vendor_id,
                                     &device->hid.product_id,
                                     &device->hid.name,
                                     &device->hid.phys,
                                     &device->hid.uniq);
}

static void
read_input_ancestor (SrtDirectInputDevice *device)
{
  if (device->input_ancestor.sys_path == NULL)
    return;

  get_uint32_hex (device->input_ancestor.sys_path, "id/bustype",
                  &device->evdev.bus_type);
  get_uint32_hex (device->input_ancestor.sys_path, "id/vendor",
                  &device->evdev.vendor_id);
  get_uint32_hex (device->input_ancestor.sys_path, "id/product",
                  &device->evdev.product_id);
  get_uint32_hex (device->input_ancestor.sys_path, "id/version",
                  &device->evdev.version);
  dup_string (device->input_ancestor.sys_path, "name", &device->evdev.name);
  dup_string (device->input_ancestor.sys_path, "phys", &device->evdev.phys);
  dup_string (device->input_ancestor.sys_path, "uniq", &device->evdev.uniq);
}

static void
read_usb_device_ancestor (SrtDirectInputDevice *device)
{
  if (device->usb_device_ancestor.sys_path == NULL)
    return;

  get_uint32_hex (device->usb_device_ancestor.sys_path, "idVendor",
                  &device->usb_device_ancestor.vendor_id);
  get_uint32_hex (device->usb_device_ancestor.sys_path, "idProduct",
                  &device->usb_device_ancestor.product_id);
  get_uint32_hex (device->usb_device_ancestor.sys_path, "bcdDevice",
                  &device->usb_device_ancestor.device_version);
  dup_string (device->usb_device_ancestor.sys_path, "manufacturer",
              &device->usb_device_ancestor.manufacturer);
  dup_string (device->usb_device_ancestor.sys_path, "product",
              &device->usb_device_ancestor.product);
}

static void
add_device (SrtDirectInputDeviceMonitor *self,
            const char *devnode,
            GQuark subsystem)
{
  SrtDirectInputDevice *device = NULL;
  const char *slash = strrchr (devnode, '/');
  g_autofree char *sys_symlink = NULL;
  int fd;

  /* Never add a device for a second time */
  if (g_hash_table_contains (self->devices, devnode))
    return;

  if (slash == NULL || slash[1] == '\0')
    return;

  device = g_object_new (SRT_TYPE_DIRECT_INPUT_DEVICE, NULL);
  device->dev_node = g_strdup (devnode);
  device->subsystem = subsystem;

  sys_symlink = g_build_filename ("/sys/class",
                                  g_quark_to_string (subsystem),
                                  slash + 1, NULL);
  device->sys_path = realpath (sys_symlink, NULL);

  if (device->sys_path == NULL)
    {
      g_debug ("unable to get real path of %s: %s",
               sys_symlink, g_strerror (errno));
      g_object_unref (device);
      return;
    }

  if (subsystem == quark_hidraw)
    device->iface_flags |= SRT_INPUT_DEVICE_INTERFACE_FLAGS_RAW_HID;

  g_debug ("Trying to open %s", devnode);

  fd = open (devnode, O_RDONLY | O_NONBLOCK | _SRT_INPUT_DEVICE_ALWAYS_OPEN_FLAGS);

  if (fd >= 0)
    {
      /* The permissions are already OK for use */
      g_debug ("Opened %s", devnode);
      device->iface_flags |= SRT_INPUT_DEVICE_INTERFACE_FLAGS_READABLE;

      if (_srt_get_identity_from_raw_hid (fd,
                                          &device->hid.bus_type,
                                          &device->hid.vendor_id,
                                          &device->hid.product_id,
                                          &device->hid.name,
                                          &device->hid.phys,
                                          &device->hid.uniq))
        {
          g_debug ("%s is raw HID: bus type 0x%04x, vendor 0x%04x, product 0x%04x, \"%s\"",
                   devnode,
                   device->hid.bus_type,
                   device->hid.vendor_id,
                   device->hid.product_id,
                   device->hid.name);
          device->iface_flags |= SRT_INPUT_DEVICE_INTERFACE_FLAGS_RAW_HID;
        }

      if (_srt_get_identity_from_evdev (fd,
                                        &device->evdev.bus_type,
                                        &device->evdev.vendor_id,
                                        &device->evdev.product_id,
                                        &device->evdev.version,
                                        &device->evdev.name,
                                        &device->evdev.phys,
                                        &device->evdev.uniq))
        {
          g_debug ("%s is evdev: bus type 0x%04x, vendor 0x%04x, product 0x%04x, version 0x%04x, \"%s\"",
                   devnode,
                   device->evdev.bus_type,
                   device->evdev.vendor_id,
                   device->evdev.product_id,
                   device->evdev.version,
                   device->evdev.name);
          device->iface_flags |= SRT_INPUT_DEVICE_INTERFACE_FLAGS_EVENT;
        }

      _srt_evdev_capabilities_set_from_evdev (&device->evdev.caps, fd);
      close (fd);
    }
  else
    {
      /* We'll get another chance after the permissions get updated */
      g_debug ("Unable to open %s to identify it: %s", devnode, g_strerror (errno));
      g_object_unref (device);
      return;
    }

  fd = open (devnode, O_RDWR | O_NONBLOCK | _SRT_INPUT_DEVICE_ALWAYS_OPEN_FLAGS);

  if (fd >= 0)
    {
      g_debug ("Opened %s rw", devnode);
      device->iface_flags |= SRT_INPUT_DEVICE_INTERFACE_FLAGS_READ_WRITE;
      close (fd);
    }

  if ((device->iface_flags & (SRT_INPUT_DEVICE_INTERFACE_FLAGS_EVENT
                              | SRT_INPUT_DEVICE_INTERFACE_FLAGS_RAW_HID)) == 0)
    {
      g_debug ("%s is neither evdev nor raw HID, ignoring", devnode);
      g_object_unref (device);
      return;
    }

  device->hid_ancestor.sys_path = get_ancestor_with_subsystem_devtype (device->sys_path,
                                                                       "hid", NULL,
                                                                       NULL);
  read_hid_ancestor (device);
  device->input_ancestor.sys_path = find_input_ancestor (device->sys_path);
  read_input_ancestor (device);

  if (device->hid.bus_type == BUS_USB || device->evdev.bus_type == BUS_USB)
    {
      device->usb_device_ancestor.sys_path = get_ancestor_with_subsystem_devtype (device->sys_path,
                                                                                  "usb",
                                                                                  "usb_device",
                                                                                  NULL);
      read_usb_device_ancestor (device);
    }

  g_hash_table_replace (self->devices, device->dev_node, device);
  _srt_input_device_monitor_emit_added (SRT_INPUT_DEVICE_MONITOR (self),
                                        SRT_INPUT_DEVICE (device));
}

static void
remove_device (SrtDirectInputDeviceMonitor *self,
               const char *devnode)
{
  void *device = NULL;

  g_debug ("Removing device %s", devnode);

  if (g_hash_table_lookup_extended (self->devices, devnode, NULL, &device))
    {
      g_hash_table_steal (self->devices, devnode);
      _srt_input_device_monitor_emit_removed (SRT_INPUT_DEVICE_MONITOR (self),
                                              device);
      g_object_unref (device);
    }
}

static gboolean
srt_direct_input_device_monitor_cb (int fd,
                                    GIOCondition condition,
                                    void *user_data)
{
  SrtDirectInputDeviceMonitor *self = SRT_DIRECT_INPUT_DEVICE_MONITOR (user_data);
  union
  {
    struct inotify_event event;
    char storage[4096];
    char enough_for_inotify[sizeof (struct inotify_event) + NAME_MAX + 1];
  } buf;
  ssize_t bytes;
  size_t remain = 0;
  size_t len;

  g_return_val_if_fail (SRT_IS_DIRECT_INPUT_DEVICE_MONITOR (self), G_SOURCE_REMOVE);

  bytes = read (self->inotify_fd, &buf, sizeof (buf));

  if (bytes > 0)
    remain = (size_t) bytes;

  while (remain > 0)
    {
      g_return_val_if_fail (remain >= sizeof (struct inotify_event), G_SOURCE_REMOVE);

      if (buf.event.len > 0)
        {
          if (buf.event.wd == self->dev_watch)
            {
              if (g_str_has_prefix (buf.event.name, "hidraw")
                  && _srt_str_is_integer (buf.event.name + strlen ("hidraw")))
                {
                  g_autofree gchar *path = g_build_filename ("/dev",
                                                             buf.event.name,
                                                             NULL);

                  if (buf.event.mask & (IN_CREATE | IN_MOVED_TO | IN_ATTRIB))
                    add_device (self, path, quark_hidraw);
                  else if (buf.event.mask & (IN_DELETE | IN_MOVED_FROM))
                    remove_device (self, path);
                }
            }
          else if (buf.event.wd == self->devinput_watch)
            {
              if (g_str_has_prefix (buf.event.name, "event")
                  && _srt_str_is_integer (buf.event.name + strlen ("event")))
                {
                  g_autofree gchar *path = g_build_filename ("/dev/input",
                                                             buf.event.name,
                                                             NULL);

                  if (buf.event.mask & (IN_CREATE | IN_MOVED_TO | IN_ATTRIB))
                    add_device (self, path, quark_input);
                  else if (buf.event.mask & (IN_DELETE | IN_MOVED_FROM))
                    remove_device (self, path);
                }
            }
        }

      len = sizeof (struct inotify_event) + buf.event.len;
      remain -= len;

      if (remain != 0)
        memmove (&buf.storage[0], &buf.storage[len], remain);
    }

  return G_SOURCE_CONTINUE;
}

static void
srt_direct_input_device_monitor_request_raw_hid (SrtInputDeviceMonitor *monitor)
{
  SrtDirectInputDeviceMonitor *self = SRT_DIRECT_INPUT_DEVICE_MONITOR (monitor);

  g_return_if_fail (SRT_IS_DIRECT_INPUT_DEVICE_MONITOR (monitor));
  g_return_if_fail (self->state == NOT_STARTED);

  self->want_hidraw = TRUE;
}

static void
srt_direct_input_device_monitor_request_evdev (SrtInputDeviceMonitor *monitor)
{
  SrtDirectInputDeviceMonitor *self = SRT_DIRECT_INPUT_DEVICE_MONITOR (monitor);

  g_return_if_fail (SRT_IS_DIRECT_INPUT_DEVICE_MONITOR (monitor));
  g_return_if_fail (self->state == NOT_STARTED);

  self->want_evdev = TRUE;
}

static gboolean
enumerate_cb (gpointer user_data)
{
  SrtDirectInputDeviceMonitor *self = SRT_DIRECT_INPUT_DEVICE_MONITOR (user_data);

  if (self->want_hidraw)
    {
      g_auto(SrtDirIter) iter = SRT_DIR_ITER_CLEARED;
      g_autoptr(GError) error = NULL;

      if (!_srt_dir_iter_init_at (&iter, AT_FDCWD, "/dev",
                                  SRT_DIR_ITER_FLAGS_FOLLOW,
                                  versionsort,
                                  &error))
        {
          g_debug ("Unable to open /dev/: %s", error->message);
        }

      while (error == NULL)
        {
          struct dirent *dent;

          if (!_srt_dir_iter_next_dent (&iter, &dent, NULL, &error))
            {
              g_debug ("Unable to iterate over /dev/: %s",
                       error->message);
              break;
            }

          if (dent == NULL)
            break;

          if (g_str_has_prefix (dent->d_name, "hidraw")
              && _srt_str_is_integer (dent->d_name + strlen ("hidraw")))
            {
              g_autofree gchar *path = g_build_filename ("/dev", dent->d_name, NULL);
              add_device (self, path, quark_hidraw);
            }
        }
    }

  if (self->want_evdev)
    {
      g_auto(SrtDirIter) iter = SRT_DIR_ITER_CLEARED;
      g_autoptr(GError) error = NULL;

      if (!_srt_dir_iter_init_at (&iter, AT_FDCWD, "/dev/input",
                                  SRT_DIR_ITER_FLAGS_FOLLOW,
                                  versionsort,
                                  &error))
        {
          g_debug ("Unable to open /dev/input/: %s", error->message);
        }

      while (error == NULL)
        {
          struct dirent *dent;

          if (!_srt_dir_iter_next_dent (&iter, &dent, NULL, &error))
            {
              g_debug ("Unable to iterate over /dev/input/: %s",
                       error->message);
              break;
            }

          if (dent == NULL)
            break;

          if (g_str_has_prefix (dent->d_name, "event")
              && _srt_str_is_integer (dent->d_name + strlen ("event")))
            {
              g_autofree gchar *path = g_build_filename ("/dev/input", dent->d_name, NULL);
              add_device (self, path, quark_input);
            }
        }
    }

  _srt_input_device_monitor_emit_all_for_now (SRT_INPUT_DEVICE_MONITOR (self));
  return G_SOURCE_REMOVE;
}

static gboolean
srt_direct_input_device_monitor_start (SrtInputDeviceMonitor *monitor,
                                       GError **error)
{
  SrtDirectInputDeviceMonitor *self = SRT_DIRECT_INPUT_DEVICE_MONITOR (monitor);

  g_return_val_if_fail (SRT_IS_DIRECT_INPUT_DEVICE_MONITOR (monitor), FALSE);
  g_return_val_if_fail (self->state == NOT_STARTED, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  self->state = STARTED;

  if ((self->flags & SRT_INPUT_DEVICE_MONITOR_FLAGS_ONCE) == 0
      && (self->want_hidraw || self->want_evdev))
    {
      self->inotify_fd = inotify_init1 (IN_NONBLOCK | IN_CLOEXEC);

      if (self->inotify_fd < 0)
        return glnx_throw_errno_prefix (error, "inotify_init1");

      if (self->want_hidraw)
        {
          self->dev_watch = inotify_add_watch (self->inotify_fd, "/dev",
                                               IN_CREATE | IN_DELETE | IN_MOVE | IN_ATTRIB);

          if (self->dev_watch < 0)
            return glnx_throw_errno_prefix (error, "inotify_add_watch");
        }

      if (self->want_evdev)
        {
          self->devinput_watch = inotify_add_watch (self->inotify_fd, "/dev/input",
                                                    IN_CREATE | IN_DELETE | IN_MOVE | IN_ATTRIB);

          if (self->devinput_watch < 0)
            return glnx_throw_errno_prefix (error, "inotify_add_watch");
        }

      self->monitor_source = g_unix_fd_source_new (self->inotify_fd, G_IO_IN);
      g_source_set_callback (self->monitor_source,
                             (GSourceFunc) G_CALLBACK (srt_direct_input_device_monitor_cb),
                             self, NULL);
      g_source_set_priority (self->monitor_source, G_PRIORITY_DEFAULT);
      g_source_attach (self->monitor_source, self->monitor_context);
    }

  /* Make sure the signals for the initial batch of devices are emitted in
   * the correct main-context */
  g_main_context_invoke_full (self->monitor_context, G_PRIORITY_DEFAULT,
                              enumerate_cb, g_object_ref (self),
                              g_object_unref);
  return TRUE;
}

static void
srt_direct_input_device_monitor_stop (SrtInputDeviceMonitor *monitor)
{
  SrtDirectInputDeviceMonitor *self = SRT_DIRECT_INPUT_DEVICE_MONITOR (monitor);

  self->state = STOPPED;

  if (self->monitor_source != NULL)
    g_source_destroy (self->monitor_source);

  g_clear_pointer (&self->monitor_source, g_source_unref);
  g_clear_pointer (&self->monitor_context, g_main_context_unref);
  g_clear_pointer (&self->devices, g_hash_table_unref);

  if (self->inotify_fd >= 0)
    {
      close (self->inotify_fd);
      self->dev_watch = -1;
      self->devinput_watch = -1;
      self->inotify_fd = -1;
    }
}

static void
srt_direct_input_device_monitor_iface_init (SrtInputDeviceMonitorInterface *iface)
{
#define IMPLEMENT(x) iface->x = srt_direct_input_device_monitor_ ## x
  IMPLEMENT (request_evdev);
  IMPLEMENT (request_raw_hid);
  IMPLEMENT (start);
  IMPLEMENT (stop);
#undef IMPLEMENT
}
