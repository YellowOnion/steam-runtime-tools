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

#include <linux/input.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>

#include <glib-unix.h>
#include <libglnx.h>

#include "steam-runtime-tools/input-device.h"

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/input-device-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#define ALWAYS_OPEN_FLAGS (O_CLOEXEC | O_NOCTTY)

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
  } usb_device_ancestor;
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

  G_OBJECT_CLASS (_srt_direct_input_device_parent_class)->finalize (object);
}

static void
_srt_direct_input_device_class_init (SrtDirectInputDeviceClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->finalize = srt_direct_input_device_finalize;
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

static const char *
srt_direct_input_device_get_hid_sys_path (SrtInputDevice *device)
{
  SrtDirectInputDevice *self = SRT_DIRECT_INPUT_DEVICE (device);

  return self->hid_ancestor.sys_path;
}

static const char *
srt_direct_input_device_get_input_sys_path (SrtInputDevice *device)
{
  SrtDirectInputDevice *self = SRT_DIRECT_INPUT_DEVICE (device);

  return self->input_ancestor.sys_path;
}

static const char *
srt_direct_input_device_get_usb_device_sys_path (SrtInputDevice *device)
{
  SrtDirectInputDevice *self = SRT_DIRECT_INPUT_DEVICE (device);

  return self->usb_device_ancestor.sys_path;
}

static void
srt_direct_input_device_iface_init (SrtInputDeviceInterface *iface)
{
#define IMPLEMENT(x) iface->x = srt_direct_input_device_ ## x

  IMPLEMENT (get_dev_node);
  IMPLEMENT (get_sys_path);
  IMPLEMENT (get_subsystem);

  IMPLEMENT (get_hid_sys_path);
  IMPLEMENT (get_input_sys_path);
  IMPLEMENT (get_usb_device_sys_path);

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

  g_debug ("Trying to open %s", devnode);

  fd = open (devnode, O_RDONLY | O_NONBLOCK | ALWAYS_OPEN_FLAGS);

  if (fd >= 0)
    {
      /* The permissions are already OK for use */
      g_debug ("Opened %s", devnode);
      close (fd);
    }
  else
    {
      /* We'll get another chance after the permissions get updated */
      g_debug ("Unable to open %s to identify it: %s", devnode, g_strerror (errno));
      g_object_unref (device);
      return;
    }

  device->hid_ancestor.sys_path = get_ancestor_with_subsystem_devtype (device->sys_path,
                                                              "hid", NULL,
                                                              NULL);
  device->input_ancestor.sys_path = find_input_ancestor (device->sys_path);
  device->usb_device_ancestor.sys_path = get_ancestor_with_subsystem_devtype (device->sys_path,
                                                                     "usb",
                                                                     "usb_device",
                                                                     NULL);

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
      g_auto(GLnxDirFdIterator) iter = { FALSE };
      g_autoptr(GError) error = NULL;

      if (!glnx_dirfd_iterator_init_at (AT_FDCWD, "/dev", TRUE, &iter, &error))
        {
          g_debug ("Unable to open /dev/: %s", error->message);
        }

      while (error == NULL)
        {
          struct dirent *dent;

          if (!glnx_dirfd_iterator_next_dent (&iter, &dent, NULL, &error))
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
      g_auto(GLnxDirFdIterator) iter = { FALSE };
      g_autoptr(GError) error = NULL;

      if (!glnx_dirfd_iterator_init_at (AT_FDCWD, "/dev/input", TRUE, &iter,
                                        &error))
        {
          g_debug ("Unable to open /dev/input/: %s", error->message);
        }

      while (error == NULL)
        {
          struct dirent *dent;

          if (!glnx_dirfd_iterator_next_dent (&iter, &dent, NULL, &error))
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
