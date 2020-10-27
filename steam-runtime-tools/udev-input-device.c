/*
 * libudev-based input device monitor, loosely based on SDL code.
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

#include "steam-runtime-tools/udev-input-device-internal.h"

#include <dlfcn.h>
#include <linux/input.h>

#include <glib-unix.h>
#include <libglnx.h>

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/input-device.h"
#include "steam-runtime-tools/input-device-internal.h"
#include "steam-runtime-tools/utils-internal.h"

static void srt_udev_input_device_iface_init (SrtInputDeviceInterface *iface);
static void srt_udev_input_device_monitor_iface_init (SrtInputDeviceMonitorInterface *iface);
static void srt_udev_input_device_monitor_initable_iface_init (GInitableIface *iface);

struct udev;
struct udev_list_entry;
struct udev_device;
struct udev_monitor;
struct udev_enumerate;

static struct
{
  void *handle;

  struct udev *(*udev_new) (void);
  struct udev *(*udev_unref) (struct udev *);

  const char *(*udev_list_entry_get_name) (struct udev_list_entry *);
  struct udev_list_entry *(*udev_list_entry_get_next) (struct udev_list_entry *);

  struct udev_device *(*udev_device_new_from_syspath) (struct udev *,
                                                       const char *);
  const char *(*udev_device_get_action) (struct udev_device *);
  const char *(*udev_device_get_devnode) (struct udev_device *);
  const char *(*udev_device_get_subsystem) (struct udev_device *);
  const char *(*udev_device_get_syspath) (struct udev_device *);
  struct udev_list_entry *(*udev_device_get_properties_list_entry) (struct udev_device *);
  const char *(*udev_device_get_property_value) (struct udev_device *, const char *);
  struct udev_device *(*udev_device_ref) (struct udev_device *);
  struct udev_device *(*udev_device_unref) (struct udev_device *);

  struct udev_monitor *(*udev_monitor_new_from_netlink) (struct udev *,
                                                         const char *);
  int (*udev_monitor_filter_add_match_subsystem_devtype) (struct udev_monitor *,
                                                          const char *,
                                                          const char *);
  int (*udev_monitor_enable_receiving) (struct udev_monitor *);
  struct udev_device *(*udev_monitor_receive_device) (struct udev_monitor *);
  int (*udev_monitor_get_fd) (struct udev_monitor *);
  struct udev_monitor *(*udev_monitor_unref) (struct udev_monitor *);

  struct udev_enumerate *(*udev_enumerate_new) (struct udev *);
  int (*udev_enumerate_add_match_subsystem) (struct udev_enumerate *,
                                             const char *);
  int (*udev_enumerate_scan_devices) (struct udev_enumerate *);
  struct udev_list_entry *(*udev_enumerate_get_list_entry) (struct udev_enumerate *);
  struct udev_enumerate *(*udev_enumerate_unref) (struct udev_enumerate *);
} symbols;

struct _SrtUdevInputDevice
{
  GObject parent;

  struct udev_device *dev;
};

struct _SrtUdevInputDeviceClass
{
  GObjectClass parent;
};

struct _SrtUdevInputDeviceMonitor
{
  GObject parent;

  struct udev *context;
  struct udev_monitor *monitor;
  GHashTable *devices;
  GMainContext *monitor_context;
  GSource *monitor_source;

  gboolean want_evdev;
  gboolean want_hidraw;
  SrtInputDeviceMonitorFlags flags;
  enum
  {
    NOT_STARTED = 0,
    STARTED,
    STOPPED
  } state;
};

struct _SrtUdevInputDeviceMonitorClass
{
  GObjectClass parent;
};

G_DEFINE_TYPE_WITH_CODE (SrtUdevInputDevice,
                         _srt_udev_input_device,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SRT_TYPE_INPUT_DEVICE,
                                                srt_udev_input_device_iface_init))

G_DEFINE_TYPE_WITH_CODE (SrtUdevInputDeviceMonitor,
                         _srt_udev_input_device_monitor,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                srt_udev_input_device_monitor_initable_iface_init)
                         G_IMPLEMENT_INTERFACE (SRT_TYPE_INPUT_DEVICE_MONITOR,
                                                srt_udev_input_device_monitor_iface_init))

static void
_srt_udev_input_device_init (SrtUdevInputDevice *self)
{
}

static void
srt_udev_input_device_finalize (GObject *object)
{
  SrtUdevInputDevice *self = SRT_UDEV_INPUT_DEVICE (object);

  g_clear_pointer (&self->dev, symbols.udev_device_unref);

  G_OBJECT_CLASS (_srt_udev_input_device_parent_class)->finalize (object);
}

static void
_srt_udev_input_device_class_init (SrtUdevInputDeviceClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->finalize = srt_udev_input_device_finalize;
}

static const char *
srt_udev_input_device_get_dev_node (SrtInputDevice *device)
{
  SrtUdevInputDevice *self = SRT_UDEV_INPUT_DEVICE (device);

  return symbols.udev_device_get_devnode (self->dev);
}

static const char *
srt_udev_input_device_get_subsystem (SrtInputDevice *device)
{
  SrtUdevInputDevice *self = SRT_UDEV_INPUT_DEVICE (device);

  return symbols.udev_device_get_subsystem (self->dev);
}

static const char *
srt_udev_input_device_get_sys_path (SrtInputDevice *device)
{
  SrtUdevInputDevice *self = SRT_UDEV_INPUT_DEVICE (device);

  return symbols.udev_device_get_syspath (self->dev);
}

static gchar **
srt_udev_input_device_dup_udev_properties (SrtInputDevice *device)
{
  SrtUdevInputDevice *self = SRT_UDEV_INPUT_DEVICE (device);
  struct udev_list_entry *entries;
  struct udev_list_entry *entry;
  g_autoptr(GPtrArray) arr = g_ptr_array_new_with_free_func (g_free);

  entries = symbols.udev_device_get_properties_list_entry (self->dev);

  for (entry = entries;
       entry != NULL;
       entry = symbols.udev_list_entry_get_next (entry))
    {
      const char *name = symbols.udev_list_entry_get_name (entry);
      const char *value = symbols.udev_device_get_property_value (self->dev, name);

      g_ptr_array_add (arr, g_strdup_printf ("%s=%s", name, value));
    }

  g_ptr_array_add (arr, NULL);
  return (gchar **) g_ptr_array_free (g_steal_pointer (&arr), FALSE);
}

static void
srt_udev_input_device_iface_init (SrtInputDeviceInterface *iface)
{
#define IMPLEMENT(x) iface->x = srt_udev_input_device_ ## x

  IMPLEMENT (get_dev_node);
  IMPLEMENT (get_sys_path);
  IMPLEMENT (get_subsystem);
  IMPLEMENT (dup_udev_properties);

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
_srt_udev_input_device_monitor_init (SrtUdevInputDeviceMonitor *self)
{
  self->monitor_context = g_main_context_ref_thread_default ();
  self->state = NOT_STARTED;
  self->devices = g_hash_table_new_full (g_str_hash,
                                         g_str_equal,
                                         NULL,
                                         g_object_unref);
}

static void
srt_udev_input_device_monitor_get_property (GObject *object,
                                            guint prop_id,
                                            GValue *value,
                                            GParamSpec *pspec)
{
  SrtUdevInputDeviceMonitor *self = SRT_UDEV_INPUT_DEVICE_MONITOR (object);

  switch ((Property) prop_id)
    {
      case PROP_FLAGS:
        g_value_set_flags (value, self->flags);
        break;

      case PROP_IS_ACTIVE:
        g_value_set_boolean (value, (self->state == STARTED));
        break;

      case PROP_0:
      case N_PROPERTIES:
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_udev_input_device_monitor_set_property (GObject *object,
                                            guint prop_id,
                                            const GValue *value,
                                            GParamSpec *pspec)
{
  SrtUdevInputDeviceMonitor *self = SRT_UDEV_INPUT_DEVICE_MONITOR (object);

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
srt_udev_input_device_monitor_dispose (GObject *object)
{
  srt_input_device_monitor_stop (SRT_INPUT_DEVICE_MONITOR (object));
  G_OBJECT_CLASS (_srt_udev_input_device_monitor_parent_class)->dispose (object);
}

static void
srt_udev_input_device_monitor_finalize (GObject *object)
{
  srt_input_device_monitor_stop (SRT_INPUT_DEVICE_MONITOR (object));
  G_OBJECT_CLASS (_srt_udev_input_device_monitor_parent_class)->finalize (object);
}

static void
_srt_udev_input_device_monitor_class_init (SrtUdevInputDeviceMonitorClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_udev_input_device_monitor_get_property;
  object_class->set_property = srt_udev_input_device_monitor_set_property;
  object_class->dispose = srt_udev_input_device_monitor_dispose;
  object_class->finalize = srt_udev_input_device_monitor_finalize;

  g_object_class_override_property (object_class, PROP_FLAGS, "flags");
  g_object_class_override_property (object_class, PROP_IS_ACTIVE, "is-active");
}

static gboolean
srt_udev_input_device_monitor_initable_init (GInitable *initable,
                                             GCancellable *cancellable G_GNUC_UNUSED,
                                             GError **error)
{
  static const char * const libs[] = { "libudev.so.1", "libudev.so.0" };
  void *handle = NULL;
  const char *lib = NULL;
  const char *which = NULL;
  gsize i;

  /* Only initialize once per process */
  if (symbols.handle != NULL)
    return TRUE;

  for (i = 0; i < G_N_ELEMENTS (libs); i++)
    {
      lib = libs[i];
      handle = dlopen (lib, RTLD_NOW|RTLD_LOCAL|RTLD_NODELETE);

      if (handle != NULL)
        break;
    }

  if (handle == NULL)
    {
      /* Try the first library again, for the side-effect of getting the
       * error message */
      lib = libs[0];
      handle = dlopen (lib, RTLD_NOW|RTLD_LOCAL|RTLD_NODELETE);
    }

  if (handle == NULL)
    return glnx_throw (error, "Unable to load %s: %s", lib, dlerror ());

#define SYMBOL(name) \
  do \
    { \
      void *symbol; \
      \
      which = #name; \
      symbol = dlsym (handle, which); \
      \
      if (symbol != NULL) \
        symbols.name = (__typeof__ (symbols.name)) G_CALLBACK (symbol); \
      else \
        goto fail; \
    } \
  while (0)

  SYMBOL (udev_new);
  SYMBOL (udev_unref);

  SYMBOL (udev_list_entry_get_name);
  SYMBOL (udev_list_entry_get_next);

  SYMBOL (udev_device_new_from_syspath);
  SYMBOL (udev_device_get_action);
  SYMBOL (udev_device_get_devnode);
  SYMBOL (udev_device_get_subsystem);
  SYMBOL (udev_device_get_syspath);
  SYMBOL (udev_device_get_properties_list_entry);
  SYMBOL (udev_device_get_property_value);
  SYMBOL (udev_device_ref);
  SYMBOL (udev_device_unref);

  SYMBOL (udev_monitor_new_from_netlink);
  SYMBOL (udev_monitor_filter_add_match_subsystem_devtype);
  SYMBOL (udev_monitor_enable_receiving);
  SYMBOL (udev_monitor_receive_device);
  SYMBOL (udev_monitor_get_fd);
  SYMBOL (udev_monitor_unref);

  SYMBOL (udev_enumerate_new);
  SYMBOL (udev_enumerate_add_match_subsystem);
  SYMBOL (udev_enumerate_scan_devices);
  SYMBOL (udev_enumerate_get_list_entry);
  SYMBOL (udev_enumerate_unref);

#undef SYMBOL

  symbols.handle = handle;
  return TRUE;

fail:
  return glnx_throw (error, "Unable to find symbol %s in %s: %s",
                     which, lib, dlerror ());
}

static void
add_device (SrtUdevInputDeviceMonitor *self,
            struct udev_device *dev)
{
  SrtUdevInputDevice *device = NULL;
  const char *syspath;
  const char *devnode;
  gboolean want = FALSE;
  gboolean is_evdev = FALSE;
  gboolean is_hidraw = FALSE;
  const char *slash;

  syspath = symbols.udev_device_get_syspath (dev);
  devnode = symbols.udev_device_get_devnode (dev);

  if (G_UNLIKELY (syspath == NULL))
    {
      g_warning ("not adding udev_device with NULL syspath");
      return;
    }

  /* If we added a device at this /sys path already, don't add it again.
   * (This is for the "change" action, and the rarer "bind" action.) */
  if (g_hash_table_lookup_extended (self->devices, syspath, NULL, NULL))
    {
      g_debug ("ignoring device \"%s\" which we already have", syspath);
      return;
    }

  if (devnode == NULL)
    {
      /* We only care about the devices that could, in principle, be
       * opened */
      g_debug ("ignoring device \"%s\" with NULL device node", syspath);
      return;
    }

  slash = strrchr (syspath, '/');

  if (slash != NULL)
    {
      if (self->want_hidraw &&
          g_str_has_prefix (slash + 1, "hidraw") &&
          _srt_str_is_integer (slash + 1 + strlen ("hidraw")))
        want = is_hidraw = TRUE;

      if (self->want_evdev &&
          g_str_has_prefix (slash + 1, "event") &&
          _srt_str_is_integer (slash + 1 + strlen ("event")))
        want = is_evdev = TRUE;
    }

  if (!want)
    {
      g_debug ("ignoring uninteresting udev_device \"%s\"", syspath);
      return;
    }

  g_debug ("Adding device %s", syspath);

  device = g_object_new (SRT_TYPE_UDEV_INPUT_DEVICE, NULL);
  device->dev = symbols.udev_device_ref (dev);

  g_hash_table_replace (self->devices, (char *) syspath, device);
  _srt_input_device_monitor_emit_added (SRT_INPUT_DEVICE_MONITOR (self),
                                        SRT_INPUT_DEVICE (device));
}

static void
remove_device (SrtUdevInputDeviceMonitor *self,
               struct udev_device *dev)
{
  void *device = NULL;
  const char *syspath;

  syspath = symbols.udev_device_get_syspath (dev);

  if (G_UNLIKELY (syspath == NULL))
    {
      g_warning ("trying to remove udev_device with NULL syspath");
      symbols.udev_device_unref (dev);
      return;
    }

  g_debug ("Removing device %s", syspath);

  if (g_hash_table_lookup_extended (self->devices, syspath, NULL, &device))
    {
      g_hash_table_steal (self->devices, syspath);
      _srt_input_device_monitor_emit_removed (SRT_INPUT_DEVICE_MONITOR (self),
                                              device);
      g_object_unref (device);
    }
}

static gboolean
srt_udev_input_device_monitor_cb (int fd,
                                  GIOCondition condition,
                                  void *user_data)
{
  SrtUdevInputDeviceMonitor *self = SRT_UDEV_INPUT_DEVICE_MONITOR (user_data);
  struct udev_device *dev;
  const char *action;

  g_return_val_if_fail (SRT_IS_UDEV_INPUT_DEVICE_MONITOR (self), G_SOURCE_REMOVE);

  dev = symbols.udev_monitor_receive_device (self->monitor);

  if (dev == NULL)
    return G_SOURCE_CONTINUE;

  action = symbols.udev_device_get_action (dev);

  if (g_strcmp0 (action, "remove") == 0)
    remove_device (self, dev);
  else
    add_device (self, dev);

  g_clear_pointer (&dev, symbols.udev_device_unref);
  return G_SOURCE_CONTINUE;
}

static void
srt_udev_input_device_monitor_request_raw_hid (SrtInputDeviceMonitor *monitor)
{
  SrtUdevInputDeviceMonitor *self = SRT_UDEV_INPUT_DEVICE_MONITOR (monitor);

  g_return_if_fail (SRT_IS_UDEV_INPUT_DEVICE_MONITOR (monitor));
  g_return_if_fail (self->state == NOT_STARTED);

  self->want_hidraw = TRUE;
}

static void
srt_udev_input_device_monitor_request_evdev (SrtInputDeviceMonitor *monitor)
{
  SrtUdevInputDeviceMonitor *self = SRT_UDEV_INPUT_DEVICE_MONITOR (monitor);

  g_return_if_fail (SRT_IS_UDEV_INPUT_DEVICE_MONITOR (monitor));
  g_return_if_fail (self->state == NOT_STARTED);

  self->want_evdev = TRUE;
}

static gboolean
enumerate_cb (gpointer user_data)
{
  SrtUdevInputDeviceMonitor *self = SRT_UDEV_INPUT_DEVICE_MONITOR (user_data);

  if (self->want_hidraw || self->want_evdev)
    {
      struct udev_enumerate *enumerator = NULL;
      struct udev_list_entry *devs;
      struct udev_list_entry *item;

      enumerator = symbols.udev_enumerate_new (self->context);

      if (enumerator == NULL)
        {
          g_warning ("udev_enumerate_new: %s", g_strerror (errno));
          goto out;
        }

      if (self->want_evdev)
        symbols.udev_enumerate_add_match_subsystem (enumerator, "input");

      if (self->want_hidraw)
        symbols.udev_enumerate_add_match_subsystem (enumerator, "hidraw");

      symbols.udev_enumerate_scan_devices (enumerator);
      devs = symbols.udev_enumerate_get_list_entry (enumerator);

      for (item = devs; item != NULL; item = symbols.udev_list_entry_get_next (item))
        {
          const char *syspath = symbols.udev_list_entry_get_name (item);
          struct udev_device *dev;

          dev = symbols.udev_device_new_from_syspath (self->context, syspath);

          if (dev != NULL)
            add_device (self, dev);
          else
            g_warning ("udev_device_new_from_syspath \"%s\": %s",
                       syspath, g_strerror (errno));

          symbols.udev_device_unref (dev);
        }

      symbols.udev_enumerate_unref (enumerator);
    }

out:
  _srt_input_device_monitor_emit_all_for_now (SRT_INPUT_DEVICE_MONITOR (self));

  return G_SOURCE_REMOVE;
}

static gboolean
srt_udev_input_device_monitor_start (SrtInputDeviceMonitor *monitor,
                                     GError **error)
{
  SrtUdevInputDeviceMonitor *self = SRT_UDEV_INPUT_DEVICE_MONITOR (monitor);

  g_return_val_if_fail (SRT_IS_UDEV_INPUT_DEVICE_MONITOR (monitor), FALSE);
  g_return_val_if_fail (self->state == NOT_STARTED, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  /* If this assertion fails, g_initable_init() failed or wasn't called */
  g_return_val_if_fail (symbols.handle != NULL, FALSE);

  self->state = STARTED;

  self->context = symbols.udev_new ();

  if (self->context == NULL)
    return glnx_throw_errno_prefix (error, "udev_new");

  if ((self->flags & SRT_INPUT_DEVICE_MONITOR_FLAGS_ONCE) == 0
      && (self->want_hidraw || self->want_evdev))
    {
      int fd;

      self->monitor = symbols.udev_monitor_new_from_netlink (self->context, "udev");

      if (self->monitor == NULL)
        return glnx_throw_errno_prefix (error, "udev_monitor_new_from_netlink");

      if (self->want_evdev)
        symbols.udev_monitor_filter_add_match_subsystem_devtype (self->monitor, "input", NULL);

      if (self->want_hidraw)
        symbols.udev_monitor_filter_add_match_subsystem_devtype (self->monitor, "hidraw", NULL);

      symbols.udev_monitor_enable_receiving (self->monitor);

      fd = symbols.udev_monitor_get_fd (self->monitor);

      if (fd < 0)
        {
          errno = -fd;
          return glnx_throw_errno_prefix (error, "udev_monitor_get_fd");
        }

      self->monitor_source = g_unix_fd_source_new (fd, G_IO_IN);
      g_source_set_callback (self->monitor_source,
                             (GSourceFunc) G_CALLBACK (srt_udev_input_device_monitor_cb),
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
srt_udev_input_device_monitor_stop (SrtInputDeviceMonitor *monitor)
{
  SrtUdevInputDeviceMonitor *self = SRT_UDEV_INPUT_DEVICE_MONITOR (monitor);

  self->state = STOPPED;

  if (self->monitor_source != NULL)
    g_source_destroy (self->monitor_source);

  g_clear_pointer (&self->monitor_source, g_source_unref);
  g_clear_pointer (&self->monitor_context, g_main_context_unref);
  g_clear_pointer (&self->monitor, symbols.udev_monitor_unref);
  g_clear_pointer (&self->context, symbols.udev_unref);
  g_clear_pointer (&self->devices, g_hash_table_unref);
}

static void
srt_udev_input_device_monitor_iface_init (SrtInputDeviceMonitorInterface *iface)
{
#define IMPLEMENT(x) iface->x = srt_udev_input_device_monitor_ ## x
  IMPLEMENT (request_raw_hid);
  IMPLEMENT (request_evdev);
  IMPLEMENT (start);
  IMPLEMENT (stop);
#undef IMPLEMENT
}

static void
srt_udev_input_device_monitor_initable_iface_init (GInitableIface *iface)
{
  iface->init = srt_udev_input_device_monitor_initable_init;
}
