/*
 * Copyright © 2019-2022 Collabora Ltd.
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

#include "steam-runtime-tools/graphics.h"

#include "steam-runtime-tools/architecture.h"
#include "steam-runtime-tools/architecture-internal.h"
#include "steam-runtime-tools/enums.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/graphics-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "steam-runtime-tools/json-utils-internal.h"
#include "steam-runtime-tools/library-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

#include <errno.h>
#include <fcntl.h>
#include <gelf.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <libelf.h>

#include <json-glib/json-glib.h>

#define VK_VERSION_MAJOR(version) ((uint32_t)(version) >> 22)
#define VK_VERSION_MINOR(version) (((uint32_t)(version) >> 12) & 0x3ff)
#define VK_VERSION_PATCH(version) ((uint32_t)(version) & 0xfff)

/**
 * SECTION:graphics
 * @title: Graphics compatibility check
 * @short_description: Get information about system's graphics capabilities
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtGraphics is an opaque object representing a graphics capabilities.
 * This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 *
 * #SrtGraphicsDevice is an opaque object representing a single, physical or
 * virtual, GPU.
 * This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 */

struct _SrtGraphicsDevice
{
  /*< private >*/
  GObject parent;
  SrtGraphicsIssues issues;
  gchar *name;
  gchar *api_version;
  gchar *driver_name;
  gchar *driver_version;
  gchar *vendor_id;
  gchar *device_id;
  gchar *messages;
  SrtVkPhysicalDeviceType type;
  guint32 vulkan_driver_id;
};

struct _SrtGraphicsDeviceClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum
{
  GRAPHICS_DEVICE_PROP_0,
  GRAPHICS_DEVICE_PROP_ISSUES,
  GRAPHICS_DEVICE_PROP_NAME,
  GRAPHICS_DEVICE_PROP_API_VERSION,
  GRAPHICS_DEVICE_PROP_VULKAN_DRIVER_ID,
  GRAPHICS_DEVICE_PROP_DRIVER_NAME,
  GRAPHICS_DEVICE_PROP_DRIVER_VERSION,
  GRAPHICS_DEVICE_PROP_VENDOR_ID,
  GRAPHICS_DEVICE_PROP_DEVICE_ID,
  GRAPHICS_DEVICE_PROP_TYPE,
  N_GRAPHICS_DEVICE_PROPERTIES,
};

G_DEFINE_TYPE (SrtGraphicsDevice, srt_graphics_device, G_TYPE_OBJECT)

static void
srt_graphics_device_init (SrtGraphicsDevice *self)
{
}

static void
srt_graphics_device_get_property (GObject *object,
                                  guint prop_id,
                                  GValue *value,
                                  GParamSpec *pspec)
{
  SrtGraphicsDevice *self = SRT_GRAPHICS_DEVICE (object);

  switch (prop_id)
    {
      case GRAPHICS_DEVICE_PROP_ISSUES:
        g_value_set_flags (value, self->issues);
        break;

      case GRAPHICS_DEVICE_PROP_NAME:
        g_value_set_string (value, self->name);
        break;

      case GRAPHICS_DEVICE_PROP_API_VERSION:
        g_value_set_string (value, self->api_version);
        break;

      case GRAPHICS_DEVICE_PROP_VULKAN_DRIVER_ID:
        g_value_set_uint (value, self->vulkan_driver_id);
        break;

      case GRAPHICS_DEVICE_PROP_DRIVER_NAME:
        g_value_set_string (value, self->driver_name);
        break;

      case GRAPHICS_DEVICE_PROP_DRIVER_VERSION:
        g_value_set_string (value, self->driver_version);
        break;

      case GRAPHICS_DEVICE_PROP_VENDOR_ID:
        g_value_set_string (value, self->vendor_id);
        break;

      case GRAPHICS_DEVICE_PROP_DEVICE_ID:
        g_value_set_string (value, self->device_id);
        break;

      case GRAPHICS_DEVICE_PROP_TYPE:
        g_value_set_enum (value, self->type);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_graphics_device_set_property (GObject *object,
                                  guint prop_id,
                                  const GValue *value,
                                  GParamSpec *pspec)
{
  SrtGraphicsDevice *self = SRT_GRAPHICS_DEVICE (object);

  switch (prop_id)
    {
      case GRAPHICS_DEVICE_PROP_ISSUES:
        /* Construct-only */
        g_return_if_fail (self->issues == 0);
        self->issues = g_value_get_flags (value);
        break;

      case GRAPHICS_DEVICE_PROP_NAME:
        /* Construct only */
        g_return_if_fail (self->name == NULL);
        self->name = g_value_dup_string (value);
        break;

      case GRAPHICS_DEVICE_PROP_API_VERSION:
        /* Construct only */
        g_return_if_fail (self->api_version == NULL);
        self->api_version = g_value_dup_string (value);
        break;

      case GRAPHICS_DEVICE_PROP_VULKAN_DRIVER_ID:
        /* Construct only */
        g_return_if_fail (self->vulkan_driver_id == 0);
        self->vulkan_driver_id = g_value_get_uint (value);
        break;

      case GRAPHICS_DEVICE_PROP_DRIVER_NAME:
        /* Construct only */
        g_return_if_fail (self->driver_name == NULL);
        self->driver_name = g_value_dup_string (value);
        break;

      case GRAPHICS_DEVICE_PROP_DRIVER_VERSION:
        /* Construct only */
        g_return_if_fail (self->driver_version == NULL);
        self->driver_version = g_value_dup_string (value);
        break;

      case GRAPHICS_DEVICE_PROP_VENDOR_ID:
        /* Construct only */
        g_return_if_fail (self->vendor_id == NULL);
        self->vendor_id = g_value_dup_string (value);
        break;

      case GRAPHICS_DEVICE_PROP_DEVICE_ID:
        /* Construct only */
        g_return_if_fail (self->device_id == NULL);
        self->device_id = g_value_dup_string (value);
        break;

      case GRAPHICS_DEVICE_PROP_TYPE:
        /* Construct-only */
        g_return_if_fail (self->type == 0);
        self->type = g_value_get_enum (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_graphics_device_finalize (GObject *object)
{
  SrtGraphicsDevice *self = SRT_GRAPHICS_DEVICE (object);

  g_free (self->name);
  g_free (self->api_version);
  g_free (self->driver_name);
  g_free (self->driver_version);
  g_free (self->vendor_id);
  g_free (self->device_id);
  g_free (self->messages);

  G_OBJECT_CLASS (srt_graphics_device_parent_class)->finalize (object);
}

static GParamSpec *graphics_device_properties[N_GRAPHICS_DEVICE_PROPERTIES] = { NULL };

static void
srt_graphics_device_class_init (SrtGraphicsDeviceClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_graphics_device_get_property;
  object_class->set_property = srt_graphics_device_set_property;
  object_class->finalize = srt_graphics_device_finalize;

  graphics_device_properties[GRAPHICS_DEVICE_PROP_ISSUES] =
    g_param_spec_flags ("issues", "Issues", "Problems with the graphics device card",
                        SRT_TYPE_GRAPHICS_ISSUES, SRT_GRAPHICS_ISSUES_NONE,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  graphics_device_properties[GRAPHICS_DEVICE_PROP_NAME] =
    g_param_spec_string ("name", "Device name",
                         "Which name the device has.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  graphics_device_properties[GRAPHICS_DEVICE_PROP_API_VERSION] =
    g_param_spec_string ("api-version", "API version",
                         "Which API version is in use.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  graphics_device_properties[GRAPHICS_DEVICE_PROP_VULKAN_DRIVER_ID] =
    g_param_spec_uint ("vulkan-driver-id", "Vulkan driver ID",
                         "Which device driver is in use, numerically equal to a VkDriverId.",
                         0, G_MAXUINT32, 0,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  graphics_device_properties[GRAPHICS_DEVICE_PROP_DRIVER_NAME] =
    g_param_spec_string ("driver-name", "Driver name",
                         "Which device driver is in use.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  graphics_device_properties[GRAPHICS_DEVICE_PROP_DRIVER_VERSION] =
    g_param_spec_string ("driver-version", "Driver version",
                         "Which driver version is in use.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  graphics_device_properties[GRAPHICS_DEVICE_PROP_VENDOR_ID] =
    g_param_spec_string ("vendor-id", "Vendor ID", "The vendor ID of the device.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  graphics_device_properties[GRAPHICS_DEVICE_PROP_DEVICE_ID] =
    g_param_spec_string ("device-id", "Device ID", "The device ID of the device.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  graphics_device_properties[GRAPHICS_DEVICE_PROP_TYPE] =
    g_param_spec_enum ("type", "Type", "Which type the device is.",
                       SRT_TYPE_VK_PHYSICAL_DEVICE_TYPE, SRT_VK_PHYSICAL_DEVICE_TYPE_OTHER,
                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_GRAPHICS_DEVICE_PROPERTIES,
                                     graphics_device_properties);
}

/**
 * srt_graphics_device_get_issues:
 * @self: a SrtGraphicsDevice object
 *
 * Return the problems found when testing @self.
 *
 * Returns: A bitfield containing problems, or %SRT_GRAPHICS_ISSUES_NONE
 *  if no problems were found
 */
SrtGraphicsIssues
srt_graphics_device_get_issues (SrtGraphicsDevice *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS_DEVICE (self), SRT_GRAPHICS_ISSUES_UNKNOWN);
  return self->issues;
}

/**
 * srt_graphics_device_get_name:
 * @self: a SrtGraphicsDevice object
 *
 * Return the device name of @self, or %NULL if it's not known.
 *
 * Returns (nullable): A string indicating the device name.
 */
const char *
srt_graphics_device_get_name (SrtGraphicsDevice *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS_DEVICE (self), NULL);
  return self->name;
}

/**
 * srt_graphics_device_get_api_version:
 * @self: a SrtGraphicsDevice object
 *
 * Return the API version used by @self, or %NULL if it's not known.
 *
 * Returns (nullable): A string indicating the API version.
 */
const char *
srt_graphics_device_get_api_version (SrtGraphicsDevice *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS_DEVICE (self), NULL);
  return self->api_version;
}

/**
 * srt_graphics_device_get_vulkan_driver_id:
 * @self: a SrtGraphicsDevice object
 *
 * Return the `VkDriverId` of the driver used by @self, or 0 if unknown.
 *
 * Returns: An integer numerically equal to a `VkDriverId`, indicating
 *  the driver in use, or 0 if unknown.
 */
guint32
srt_graphics_device_get_vulkan_driver_id (SrtGraphicsDevice *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS_DEVICE (self), 0);
  return self->vulkan_driver_id;
}

/**
 * srt_graphics_device_get_driver_name:
 * @self: a SrtGraphicsDevice object
 *
 * Return the name of the driver used by @self, or %NULL if it's not known.
 *
 * Returns (nullable): A string indicating the driver version.
 */
const char *
srt_graphics_device_get_driver_name (SrtGraphicsDevice *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS_DEVICE (self), NULL);
  return self->driver_name;
}

/**
 * srt_graphics_device_get_driver_version:
 * @self: a SrtGraphicsDevice object
 *
 * Return the driver version used by @self, or %NULL if it's not known.
 *
 * Returns (nullable): A string indicating the driver version.
 */
const char *
srt_graphics_device_get_driver_version (SrtGraphicsDevice *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS_DEVICE (self), NULL);
  return self->driver_version;
}

/**
 * srt_graphics_device_get_vendor_id:
 * @self: a SrtGraphicsDevice object
 *
 * Return the vendor ID of @self, or %NULL if it's not known.
 *
 * Returns (nullable): A string indicating the vendor ID.
 */
const char *
srt_graphics_device_get_vendor_id (SrtGraphicsDevice *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS_DEVICE (self), NULL);
  return self->vendor_id;
}

/**
 * srt_graphics_device_get_device_id:
 * @self: a SrtGraphicsDevice object
 *
 * Return the device ID of @self, or %NULL if it's not known.
 *
 * Returns (nullable): A string indicating the device ID.
 */
const char *
srt_graphics_device_get_device_id (SrtGraphicsDevice *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS_DEVICE (self), NULL);
  return self->device_id;
}

/**
 * srt_graphics_device_get_messages:
 * @self: a SrtGraphicsDevice object
 *
 * Return the diagnostic messages produced while checking @self device
 * drawing capabilities.
 *
 * Returns: (nullable) (transfer none): A string, which must not be freed,
 *  or %NULL if there were no diagnostic messages.
 */
const char *
srt_graphics_device_get_messages (SrtGraphicsDevice *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS_DEVICE (self), NULL);
  return self->messages;
}

/**
 * srt_graphics_device_get_device_type:
 * @self: a SrtGraphicsDevice object
 *
 * Return the @self device type
 *
 * Returns: An enumeration of #SrtVkPhysicalDeviceType indicating the type
 *  of this device.
 */
SrtVkPhysicalDeviceType
srt_graphics_device_get_device_type (SrtGraphicsDevice *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS_DEVICE (self),
                        SRT_VK_PHYSICAL_DEVICE_TYPE_OTHER);
  return self->type;
}

/*
 * @self: a SrtGraphicsDevice object
 * @can_draw: if %TRUE, this device is able to successfully draw a triangle
 *  test
 *
 * @self issues is adjusted accordingly to the @can_draw value.
 */
static void
_srt_graphics_device_set_can_draw (SrtGraphicsDevice *self,
                                   gboolean can_draw)
{
  g_return_if_fail (SRT_IS_GRAPHICS_DEVICE (self));
  if (can_draw)
    self->issues &= ~SRT_GRAPHICS_ISSUES_CANNOT_DRAW;
  else
    self->issues |= SRT_GRAPHICS_ISSUES_CANNOT_DRAW;
}

/*
 * @self: a SrtGraphicsDevice object
 * @messages (nullable): diagnostic messages
 *
 * Set @self diagnostic messages to the provided @messages.
 */
static void
_srt_graphics_device_set_messages (SrtGraphicsDevice *self,
                                   const gchar *messages)
{
  g_return_if_fail (SRT_IS_GRAPHICS_DEVICE (self));
  g_free (self->messages);
  self->messages = g_strdup (messages);
}

struct _SrtGraphics
{
  /*< private >*/
  GObject parent;
  GQuark multiarch_tuple;
  SrtWindowSystem window_system;
  SrtRenderingInterface rendering_interface;
  SrtGraphicsIssues issues;
  SrtGraphicsLibraryVendor library_vendor;
  gchar *messages;
  gchar *renderer_string;
  gchar *version_string;
  GPtrArray *devices;
  int exit_status;
  int terminating_signal;
};

struct _SrtGraphicsClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum {
  PROP_0,
  PROP_ISSUES,
  PROP_LIBRARY_VENDOR,
  PROP_MESSAGES,
  PROP_MULTIARCH_TUPLE,
  PROP_WINDOW_SYSTEM,
  PROP_RENDERING_INTERFACE,
  PROP_RENDERER_STRING,
  PROP_VERSION_STRING,
  PROP_GRAPHICS_DEVICES,
  PROP_EXIT_STATUS,
  PROP_TERMINATING_SIGNAL,
  N_PROPERTIES
};

G_DEFINE_TYPE (SrtGraphics, srt_graphics, G_TYPE_OBJECT)

static void
srt_graphics_init (SrtGraphics *self)
{
}

static void
srt_graphics_get_property (GObject *object,
                          guint prop_id,
                          GValue *value,
                          GParamSpec *pspec)
{
  SrtGraphics *self = SRT_GRAPHICS (object);

  switch (prop_id)
    {
      case PROP_ISSUES:
        g_value_set_flags (value, self->issues);
        break;

      case PROP_LIBRARY_VENDOR:
        g_value_set_enum (value, self->library_vendor);
        break;

      case PROP_MESSAGES:
        g_value_set_string (value, self->messages);
        break;

      case PROP_MULTIARCH_TUPLE:
        g_value_set_string (value, g_quark_to_string (self->multiarch_tuple));
        break;

      case PROP_WINDOW_SYSTEM:
        g_value_set_enum (value, self->window_system);
        break;

      case PROP_RENDERING_INTERFACE:
        g_value_set_enum (value, self->rendering_interface);
        break;

      case PROP_RENDERER_STRING:
        g_value_set_string (value, self->renderer_string);
        break;

      case PROP_VERSION_STRING:
        g_value_set_string (value, self->version_string);
        break;

      case PROP_GRAPHICS_DEVICES:
        g_value_set_boxed (value, self->devices);
        break;

      case PROP_EXIT_STATUS:
        g_value_set_int (value, self->exit_status);
        break;

      case PROP_TERMINATING_SIGNAL:
        g_value_set_int (value, self->terminating_signal);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_graphics_set_property (GObject *object,
                           guint prop_id,
                           const GValue *value,
                           GParamSpec *pspec)
{
  SrtGraphics *self = SRT_GRAPHICS (object);
  const char *tmp;

  switch (prop_id)
    {
      case PROP_ISSUES:
        /* Construct-only */
        g_return_if_fail (self->issues == 0);
        self->issues = g_value_get_flags (value);
        break;

      case PROP_LIBRARY_VENDOR:
        /* Construct-only */
        g_return_if_fail (self->library_vendor == 0);
        self->library_vendor = g_value_get_enum (value);
        break;

      case PROP_MESSAGES:
        /* Construct-only */
        g_return_if_fail (self->messages == NULL);
        tmp = g_value_get_string (value);

        /* Normalize the empty string (expected to be common) to NULL */
        if (tmp != NULL && tmp[0] == '\0')
          tmp = NULL;

        self->messages = g_strdup (tmp);
        break;

      case PROP_MULTIARCH_TUPLE:
        /* Construct-only */
        g_return_if_fail (self->multiarch_tuple == 0);
        /* Intern the string since we only expect to deal with a few values */
        self->multiarch_tuple = g_quark_from_string (g_value_get_string (value));
        break;

      case PROP_WINDOW_SYSTEM:
        /* Construct-only */
        g_return_if_fail (self->window_system == 0);
        self->window_system = g_value_get_enum (value);
        break;

      case PROP_RENDERING_INTERFACE:
        /* Construct-only */
        g_return_if_fail (self->rendering_interface == 0);
        self->rendering_interface = g_value_get_enum (value);
        break;

      case PROP_RENDERER_STRING:
        /* Construct only */
        g_return_if_fail (self->renderer_string == NULL);

        self->renderer_string = g_value_dup_string (value);
        break;

      case PROP_VERSION_STRING:
        /* Construct only */
        g_return_if_fail (self->version_string == NULL);

        self->version_string = g_value_dup_string (value);
        break;

      case PROP_GRAPHICS_DEVICES:
        /* Construct only */
        g_return_if_fail (self->devices == NULL);
        self->devices = g_value_dup_boxed (value);
        break;

      case PROP_EXIT_STATUS:
        self->exit_status = g_value_get_int (value);
        break;

      case PROP_TERMINATING_SIGNAL:
        self->terminating_signal = g_value_get_int (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_graphics_dispose (GObject *object)
{
  SrtGraphics *self = SRT_GRAPHICS (object);

  g_clear_pointer (&self->devices, g_ptr_array_unref);

  G_OBJECT_CLASS (srt_graphics_parent_class)->dispose (object);
}

static void
srt_graphics_finalize (GObject *object)
{
  SrtGraphics *self = SRT_GRAPHICS (object);

  g_free (self->messages);
  g_free (self->renderer_string);
  g_free (self->version_string);

  G_OBJECT_CLASS (srt_graphics_parent_class)->finalize (object);
}

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void
srt_graphics_class_init (SrtGraphicsClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_graphics_get_property;
  object_class->set_property = srt_graphics_set_property;
  object_class->dispose = srt_graphics_dispose;
  object_class->finalize = srt_graphics_finalize;

  properties[PROP_ISSUES] =
    g_param_spec_flags ("issues", "Issues", "Problems with the graphics stack",
                        SRT_TYPE_GRAPHICS_ISSUES, SRT_GRAPHICS_ISSUES_NONE,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  properties[PROP_LIBRARY_VENDOR] =
    g_param_spec_enum ("library-vendor", "Library vendor", "Which library vendor is currently in use.",
                        SRT_TYPE_GRAPHICS_LIBRARY_VENDOR, SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  properties[PROP_MESSAGES] =
    g_param_spec_string ("messages", "Messages",
                         "Diagnostic messages produced while checking this "
                         "graphics stack",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_MULTIARCH_TUPLE] =
    g_param_spec_string ("multiarch-tuple", "Multiarch tuple",
                         "Which multiarch tuple we are checking, for example "
                         "x86_64-linux-gnu",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_WINDOW_SYSTEM] =
    g_param_spec_enum ("window-system", "Window System", "Which window system we are checking.",
                        SRT_TYPE_WINDOW_SYSTEM, SRT_WINDOW_SYSTEM_GLX,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  properties[PROP_RENDERING_INTERFACE] =
    g_param_spec_enum ("rendering-interface", "Rendering Interface", "Which rendering interface we are checking.",
                        SRT_TYPE_RENDERING_INTERFACE, SRT_RENDERING_INTERFACE_GL,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  properties[PROP_RENDERER_STRING] =
    g_param_spec_string ("renderer-string", "Found Renderer", "Which renderer was found by checking.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_VERSION_STRING] =
    g_param_spec_string ("version-string", "Found version", "Which version of graphics renderer was found from check.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_GRAPHICS_DEVICES] =
    g_param_spec_boxed ("graphics-devices", "List of graphics devices", "List of #SrtGraphicsDevice, representing the graphical cards.",
                        G_TYPE_PTR_ARRAY,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  properties[PROP_EXIT_STATUS] =
    g_param_spec_int ("exit-status", "Exit status", "Exit status of helper(s) executed. 0 on success, positive on unsuccessful exit(), -1 if killed by a signal or not run at all",
                      -1,
                      G_MAXINT,
                      0,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                      G_PARAM_STATIC_STRINGS);

  properties[PROP_TERMINATING_SIGNAL] =
    g_param_spec_int ("terminating-signal", "Terminating signal", "Signal used to terminate helper process if any, 0 otherwise",
                      0,
                      G_MAXINT,
                      0,
                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                      G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

/*
 * @node: The JsonNode to process the wflinfo results from.
 * @version_string: (out) (transfer none) (not optional):
 * @renderer_string: (out) (transfer none) (not optional):
 */
static SrtGraphicsIssues
_srt_process_wflinfo (JsonNode *node,
                      gchar **version_string,
                      const gchar **renderer_string)
{
  g_return_val_if_fail (version_string != NULL, SRT_GRAPHICS_ISSUES_UNKNOWN);
  g_return_val_if_fail (renderer_string != NULL, SRT_GRAPHICS_ISSUES_UNKNOWN);

  SrtGraphicsIssues issues = SRT_GRAPHICS_ISSUES_NONE;

  if (node == NULL)
    {
      g_debug ("The json output is empty");
      issues |= SRT_GRAPHICS_ISSUES_CANNOT_LOAD;
      return issues;
    }

  JsonObject *object = json_node_get_object (node);
  JsonNode *sub_node = NULL;
  JsonObject *sub_object = NULL;

  if (!json_object_has_member (object, "OpenGL"))
    {
      g_debug ("The json output doesn't contain an OpenGL object");
      issues |= SRT_GRAPHICS_ISSUES_CANNOT_LOAD;
      return issues;
    }

  sub_node = json_object_get_member (object, "OpenGL");
  sub_object = json_node_get_object (sub_node);

  if (!json_object_has_member (sub_object, "version string") ||
      !json_object_has_member (sub_object, "renderer string"))
    {
      g_debug ("Json output is missing version or renderer");
      issues |= SRT_GRAPHICS_ISSUES_CANNOT_LOAD;
      return issues;
    }

  *version_string = g_strdup (json_object_get_string_member (sub_object, "version string"));
  *renderer_string = json_object_get_string_member (sub_object, "renderer string");

  /* Check renderer to see if we are using software rendering */
  if (strstr (*renderer_string, "llvmpipe") != NULL ||
      strstr (*renderer_string, "software rasterizer") != NULL ||
      strstr (*renderer_string, "softpipe") != NULL)
    {
      issues |= SRT_GRAPHICS_ISSUES_SOFTWARE_RENDERING;
    }
  return issues;
}

/*
 * @node (not nullable):
 * @version_string (not optional) (out):
 * @device (not optional) (out):
 */
static SrtGraphicsIssues
_srt_process_check_vulkan_info (JsonNode *node,
                                gchar **version_string,
                                SrtGraphicsDevice **device)
{
  g_autoptr(GString) buf = NULL;
  JsonObject *object = NULL;
  JsonObject *sub_object = NULL;
  JsonNode *sub_node = NULL;
  const gchar *device_name = NULL;
  const gchar *api_version = NULL;
  guint32 driver_version = 0;
  const gchar *vendor_id = NULL;
  const gchar *device_id = NULL;
  const char *driver_name;
  const char *driver_info;
  SrtVkPhysicalDeviceType device_type;
  gint64 driver_id;

  /* Until we parse the drawing test results, we assume that we are not
   * able to draw with this device */
  SrtGraphicsIssues issues = SRT_GRAPHICS_ISSUES_CANNOT_DRAW;

  object = json_node_get_object (node);

  /* We only parse the device information, not the drawing test result */
  if (!json_object_has_member (object, "device-info"))
    return issues;

  sub_node = json_object_get_member (object, "device-info");
  sub_object = json_node_get_object (sub_node);

  device_name = json_object_get_string_member_with_default (sub_object, "device-name",
                                                            NULL);
  device_type = json_object_get_int_member_with_default (sub_object, "device-type", 0);
  driver_id = json_object_get_int_member_with_default (sub_object, "driver-id", 0);
  api_version = json_object_get_string_member_with_default (sub_object, "api-version",
                                                            NULL);

  if (!_srt_json_object_get_hex_uint32_member (sub_object, "driver-version", &driver_version))
    driver_version = 0;

  /* Typical values: "NVIDIA", "radv", "Intel open-source Mesa driver" */
  driver_name = json_object_get_string_member_with_default (sub_object, "driver-name",
                                                            NULL);
  /* Typical values: "470.86" for Nvidia, "Mesa 21.0.3 (ACO)" for Mesa */
  driver_info = json_object_get_string_member_with_default (sub_object, "driver-info",
                                                            NULL);
  vendor_id = json_object_get_string_member_with_default (sub_object, "vendor-id",
                                                          NULL);
  device_id = json_object_get_string_member_with_default (sub_object, "device-id",
                                                          NULL);

  if (driver_id < 0 || driver_id > G_MAXUINT32)
    driver_id = 0;

  buf = g_string_new ("");

  if (driver_info != NULL)
    {
      /* Vulkan 1.2 drivers can report their real version number in the
       * driver info, and in practice Mesa and Nvidia both do. */
      g_string_append (buf, driver_info);
    }
  else
    {
      /* For older drivers, hope it's the same encoding as apiVersion,
       * as it is in Mesa */
      g_string_append_printf (buf, "%#x (%u.%u.%u?)",
                              driver_version,
                              VK_VERSION_MAJOR (driver_version),
                              VK_VERSION_MINOR (driver_version),
                              VK_VERSION_PATCH (driver_version));
    }

  if (device_type == SRT_VK_PHYSICAL_DEVICE_TYPE_CPU)
    issues |= SRT_GRAPHICS_ISSUES_SOFTWARE_RENDERING;

  *device = _srt_graphics_device_new (device_name, api_version,
                                      (guint32) driver_id, driver_name, buf->str,
                                      vendor_id, device_id, device_type, issues);
  *version_string = g_string_free (g_steal_pointer (&buf), FALSE);

  return issues;
}

/*
 * @window_system: (not optional) (inout):
 */
static GPtrArray *
_argv_for_graphics_test (const char *helpers_path,
                         SrtTestFlags test_flags,
                         const char *multiarch_tuple,
                         SrtWindowSystem *window_system,
                         SrtRenderingInterface rendering_interface,
                         GError **error)
{
  const char *api;
  GPtrArray *argv = NULL;
  gchar *platformstring = NULL;
  SrtHelperFlags flags = (SRT_HELPER_FLAGS_TIME_OUT
                          | SRT_HELPER_FLAGS_SEARCH_PATH);

  g_assert (window_system != NULL);

  if (test_flags & SRT_TEST_FLAGS_TIME_OUT_SOONER)
    flags |= SRT_HELPER_FLAGS_TIME_OUT_SOONER;

  if (*window_system == SRT_WINDOW_SYSTEM_GLX)
    {
      switch (rendering_interface)
        {
          case SRT_RENDERING_INTERFACE_GL:
            platformstring = g_strdup ("glx");
            break;

          case SRT_RENDERING_INTERFACE_GLESV2:
          case SRT_RENDERING_INTERFACE_VULKAN:
          case SRT_RENDERING_INTERFACE_VDPAU:
          case SRT_RENDERING_INTERFACE_VAAPI:
          default:
            g_critical ("GLX window system only makes sense with GL "
                        "rendering interface, not %d",
                        rendering_interface);
            g_return_val_if_reached (NULL);
            break;
        }
    }
  else if (*window_system == SRT_WINDOW_SYSTEM_X11)
    {
      switch (rendering_interface)
        {
          case SRT_RENDERING_INTERFACE_GL:
            platformstring = g_strdup ("glx");
            *window_system = SRT_WINDOW_SYSTEM_GLX;
            break;

          case SRT_RENDERING_INTERFACE_GLESV2:
            platformstring = g_strdup ("x11_egl");
            *window_system = SRT_WINDOW_SYSTEM_EGL_X11;
            break;

          case SRT_RENDERING_INTERFACE_VULKAN:
          case SRT_RENDERING_INTERFACE_VDPAU:
          case SRT_RENDERING_INTERFACE_VAAPI:
            /* They don't set platformstring, just set argv later. */
            break;

          default:
            g_return_val_if_reached (NULL);
        }
    }
  else if (*window_system == SRT_WINDOW_SYSTEM_EGL_X11)
    {
      switch (rendering_interface)
        {
          case SRT_RENDERING_INTERFACE_GL:
          case SRT_RENDERING_INTERFACE_GLESV2:
            platformstring = g_strdup ("x11_egl");
            break;

          case SRT_RENDERING_INTERFACE_VULKAN:
          case SRT_RENDERING_INTERFACE_VDPAU:
          case SRT_RENDERING_INTERFACE_VAAPI:
          default:
            g_critical ("EGL window system only makes sense with a GL-based "
                        "rendering interface, not %d",
                        rendering_interface);
            g_return_val_if_reached (NULL);
            break;
        }
    }
  else
    {
      /* should not be reached because the precondition checks should
       * have caught this */
      g_return_val_if_reached (NULL);
    }

  switch (rendering_interface)
    {
      case SRT_RENDERING_INTERFACE_GL:
      case SRT_RENDERING_INTERFACE_GLESV2:
        argv = _srt_get_helper (helpers_path, multiarch_tuple, "wflinfo", flags,
                                error);

        if (argv == NULL)
          goto out;

        if (rendering_interface == SRT_RENDERING_INTERFACE_GLESV2)
          api = "gles2";
        else
          api = "gl";

        g_ptr_array_add (argv, g_strdup_printf ("--platform=%s", platformstring));
        g_ptr_array_add (argv, g_strdup_printf ("--api=%s", api));
        g_ptr_array_add (argv, g_strdup ("--format=json"));
        break;

      case SRT_RENDERING_INTERFACE_VULKAN:
        argv = _srt_get_helper (helpers_path, multiarch_tuple, "check-vulkan",
                                flags, error);

        if (argv == NULL)
          goto out;

        break;

      case SRT_RENDERING_INTERFACE_VDPAU:
        argv = _srt_get_helper (helpers_path, multiarch_tuple, "check-vdpau",
                                flags, error);

        if (argv == NULL)
          goto out;

        g_ptr_array_add (argv, g_strdup ("--verbose"));
        break;

      case SRT_RENDERING_INTERFACE_VAAPI:
        argv = _srt_get_helper (helpers_path, multiarch_tuple, "check-va-api",
                                flags, error);

        if (argv == NULL)
          goto out;

        g_ptr_array_add (argv, g_strdup ("--verbose"));
        break;

      default:
        g_return_val_if_reached (NULL);
        break;
    }

  g_ptr_array_add (argv, NULL);

out:
  g_free (platformstring);
  return argv;
}

static GPtrArray *
_argv_for_check_gl (const char *helpers_path,
                    SrtTestFlags test_flags,
                    const char *multiarch_tuple,
                    GError **error)
{
  GPtrArray *argv;
  SrtHelperFlags flags = SRT_HELPER_FLAGS_TIME_OUT;

  if (test_flags & SRT_TEST_FLAGS_TIME_OUT_SOONER)
    flags |= SRT_HELPER_FLAGS_TIME_OUT_SOONER;

  argv = _srt_get_helper (helpers_path, multiarch_tuple, "check-gl",
                          flags, error);

  if (argv == NULL)
    return NULL;

  g_ptr_array_add (argv, NULL);
  return argv;
}

/**
 * _srt_check_library_vendor:
 * @envp: (not nullable): The environment
 * @multiarch_tuple: A multiarch tuple to check e.g. i386-linux-gnu
 * @window_system: The window system to check.
 * @rendering_interface: The graphics renderer to check.
 *
 * Return whether the entry-point library for this graphics stack is vendor-neutral or
 * vendor-specific, and if vendor-specific, attempt to guess the vendor.
 *
 * For newer GLX and EGL graphics stacks (since 2017-2018) the entry-point library is
 * provided by GLVND, a vendor-neutral dispatch library that loads vendor-specific ICDs.
 * This function returns %SRT_GRAPHICS_DRIVER_VENDOR_GLVND.
 *
 * For older GLX and EGL graphics stacks, the entry-point library libGL.so.1 or libEGL.so.1
 * is part of a particular vendor's graphics library, usually Mesa, NVIDIA or Primus. This
 * function attempts to determine which one.
 *
 * For Vulkan the entry-point library libvulkan.so.1 is always vendor-neutral
 * (similar to GLVND), so this function is not useful. It always returns
 * %SRT_GRAPHICS_DRIVER_VENDOR_UNKNOWN.
 *
 * Returns: the graphics library vendor currently in use, or
 *  %SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN if the rendering interface is
 *  %SRT_RENDERING_INTERFACE_VULKAN or if there was a problem loading the library.
 */
static SrtGraphicsLibraryVendor
_srt_check_library_vendor (gchar **envp,
                           const char *multiarch_tuple,
                           SrtWindowSystem window_system,
                           SrtRenderingInterface rendering_interface)
{
  SrtGraphicsLibraryVendor library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN;
  const gchar *soname = NULL;
  SrtLibrary *library = NULL;
  SrtLibraryIssues issues;
  const char * const *dependencies;
  gboolean have_libstdc_deps = FALSE;
  gboolean have_libxcb_deps = FALSE;

  g_return_val_if_fail (envp != NULL, SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN);

  /* Vulkan, VDPAU and VA-API are always vendor-neutral, so it doesn't make sense to check it.
   * We simply return SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN */
  if (rendering_interface == SRT_RENDERING_INTERFACE_VULKAN ||
      rendering_interface == SRT_RENDERING_INTERFACE_VDPAU ||
      rendering_interface == SRT_RENDERING_INTERFACE_VAAPI)
    goto out;

  switch (window_system)
    {
      case SRT_WINDOW_SYSTEM_GLX:
      case SRT_WINDOW_SYSTEM_X11:
        soname = "libGL.so.1";
        break;

      case SRT_WINDOW_SYSTEM_EGL_X11:
        soname = "libEGL.so.1";
        break;

      default:
        g_return_val_if_reached (SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN);
    }

  issues = srt_check_library_presence (soname, multiarch_tuple, NULL, SRT_LIBRARY_SYMBOLS_FORMAT_PLAIN, &library);

  if ((issues & SRT_LIBRARY_ISSUES_CANNOT_LOAD) != 0)
    goto out;

  dependencies = srt_library_get_dependencies (library);
  library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN_NON_GLVND;

  for (gsize i = 0; dependencies[i] != NULL; i++)
    {
      if (strstr (dependencies[i], "/libGLdispatch.so.") != NULL)
        {
          library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_GLVND;
          break;
        }
      if (strstr (dependencies[i], "/libglapi.so.") != NULL)
        {
          library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_MESA;
          break;
        }
      if (strstr (dependencies[i], "/libnvidia-") != NULL)
        {
          library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_NVIDIA;
          break;
        }
      if (strstr (dependencies[i], "/libstdc++.so.") != NULL)
        {
          have_libstdc_deps = TRUE;
        }
      if (strstr (dependencies[i], "/libxcb.so.") != NULL)
        {
          have_libxcb_deps = TRUE;
        }
    }

  if (library_vendor == SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN_NON_GLVND &&
      have_libstdc_deps &&
      !have_libxcb_deps)
    {
      library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_PRIMUS;
    }

out:
  g_clear_pointer (&library, g_object_unref);
  return library_vendor;
}

/*
 * Run the given helper and report any issues found
 *
 * @my_environ: (inout) (transfer full): The environment to modify and run the argv in
 * @output: (inout) (transfer full): The output collected from the helper
 * @child_stderr: (inout) (transfer full): The stderr collected from the helper
 * @argv: (transfer none): The helper and arguments to run
 * @wait_status: (out) (transfer full): The wait status of the helper,
 *   or -1 if it could not be run at all
 * @exit_status: (out) (transfer full): Exit status of helper(s) executed.
 *   0 on success, positive on unsuccessful exit(), -1 if killed by a signal or
 *   not run at all
 * @terminating_signal: (out) (transfer full): Signal used to terminate helper
 *   process if any, 0 otherwise
 * @verbose: If true set environment variables for debug output
 * @non_zero_waitstatus_issue: Which issue should be set if wait_status is non zero
 */
static SrtGraphicsIssues
_srt_run_helper (GStrv *my_environ,
                 gchar **output,
                 gchar **child_stderr,
                 const GPtrArray *argv,
                 int *wait_status,
                 int *exit_status,
                 int *terminating_signal,
                 gboolean verbose,
                 SrtGraphicsIssues non_zero_wait_status_issue)
{
  // non_zero_wait_status_issue needs to be something other than NONE, otherwise
  // we get no indication in caller that there was any error
  g_return_val_if_fail (non_zero_wait_status_issue != SRT_GRAPHICS_ISSUES_NONE,
                        SRT_GRAPHICS_ISSUES_UNKNOWN);

  g_return_val_if_fail (wait_status != NULL, SRT_GRAPHICS_ISSUES_UNKNOWN);
  g_return_val_if_fail (exit_status != NULL, SRT_GRAPHICS_ISSUES_UNKNOWN);
  g_return_val_if_fail (terminating_signal != NULL, SRT_GRAPHICS_ISSUES_UNKNOWN);

  SrtGraphicsIssues issues = SRT_GRAPHICS_ISSUES_NONE;
  GError *error = NULL;

  if (verbose)
    {
      // Issues found, so run again with LIBGL_DEBUG=verbose set in environment
      *my_environ = g_environ_setenv (*my_environ, "LIBGL_DEBUG", "verbose", TRUE);
    }

  // Ignore what came on stderr previously, use this run's error message
  g_free(*output);
  *output = NULL;
  g_free(*child_stderr);
  *child_stderr = NULL;
  *wait_status = -1;
  *exit_status = -1;
  *terminating_signal = 0;

  if (!g_spawn_sync (NULL,    /* working directory */
                     (gchar **) argv->pdata,
                     *my_environ,    /* envp */
                     G_SPAWN_SEARCH_PATH,       /* flags */
                     _srt_child_setup_unblock_signals,
                     NULL,    /* user data */
                     output, /* stdout */
                     child_stderr,
                     wait_status,
                     &error))
    {
      g_debug ("An error occurred calling the helper: %s", error->message);
      *child_stderr = g_strdup (error->message);
      g_clear_error (&error);
      issues |= non_zero_wait_status_issue;
    }
  else if (*wait_status != 0)
    {
      g_debug ("... wait status %d", *wait_status);
      issues |= non_zero_wait_status_issue;

      if (_srt_process_timeout_wait_status (*wait_status, exit_status, terminating_signal))
        {
          issues |= SRT_GRAPHICS_ISSUES_TIMEOUT;
        }
    }
  else
    {
      *exit_status = 0;
    }

  return issues;
}

/**
 * _srt_check_graphics:
 * @envp: (not nullable): Used instead of `environ`
 * @helpers_path: An optional path to find wflinfo helpers, PATH is used if null.
 * @test_flags: Flags used during automated testing
 * @multiarch_tuple: A multiarch tuple to check e.g. i386-linux-gnu
 * @winsys: The window system to check.
 * @renderer: The graphics renderer to check.
 * @details_out: The SrtGraphics object containing the details of the check.
 *
 * Return the problems found when checking the graphics stack given.
 *
 * Returns: A bitfield containing problems, or %SRT_GRAPHICS_ISSUES_NONE
 *  if no problems were found
 */
G_GNUC_INTERNAL SrtGraphicsIssues
_srt_check_graphics (gchar **envp,
                     const char *helpers_path,
                     SrtTestFlags test_flags,
                     const char *multiarch_tuple,
                     SrtWindowSystem window_system,
                     SrtRenderingInterface rendering_interface,
                     SrtGraphics **details_out)
{
  gchar *output = NULL;
  gchar *child_stderr = NULL;
  gchar *child_stderr2 = NULL;
  int wait_status = -1;
  int exit_status = -1;
  int terminating_signal = 0;
  g_autoptr(JsonNode) node = NULL;
  g_autofree gchar *version_string = NULL;
  const gchar *renderer_string = NULL;
  GError *error = NULL;
  SrtGraphicsIssues issues = SRT_GRAPHICS_ISSUES_NONE;
  SrtGraphicsIssues non_zero_wait_status_issue = SRT_GRAPHICS_ISSUES_CANNOT_LOAD;
  GStrv my_environ = NULL;
  SrtGraphicsLibraryVendor library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN;
  g_auto(GStrv) json_output = NULL;
  g_autoptr(GPtrArray) graphics_device = g_ptr_array_new_with_free_func (g_object_unref);
  gsize i;

  g_return_val_if_fail (details_out == NULL || *details_out == NULL, SRT_GRAPHICS_ISSUES_UNKNOWN);
  g_return_val_if_fail (((unsigned) window_system) < SRT_N_WINDOW_SYSTEMS, SRT_GRAPHICS_ISSUES_UNKNOWN);
  g_return_val_if_fail (((unsigned) rendering_interface) < SRT_N_RENDERING_INTERFACES, SRT_GRAPHICS_ISSUES_UNKNOWN);
  g_return_val_if_fail (_srt_check_not_setuid (), SRT_GRAPHICS_ISSUES_UNKNOWN);
  g_return_val_if_fail (envp != NULL, SRT_GRAPHICS_ISSUES_UNKNOWN);

  GPtrArray *argv = _argv_for_graphics_test (helpers_path,
                                             test_flags,
                                             multiarch_tuple,
                                             &window_system,
                                             rendering_interface,
                                             &error);

  if (argv == NULL)
    {
      issues |= SRT_GRAPHICS_ISSUES_CANNOT_LOAD;
      /* Put the error message in the 'messages' */
      child_stderr = g_strdup (error->message);
      goto out;
    }

  my_environ = _srt_filter_gameoverlayrenderer_from_envp (envp);

  library_vendor = _srt_check_library_vendor (envp, multiarch_tuple,
                                              window_system,
                                              rendering_interface);

  switch (rendering_interface)
    {
      case SRT_RENDERING_INTERFACE_GL:
      case SRT_RENDERING_INTERFACE_GLESV2:
        non_zero_wait_status_issue = SRT_GRAPHICS_ISSUES_CANNOT_LOAD;
        break;

      case SRT_RENDERING_INTERFACE_VULKAN:
      case SRT_RENDERING_INTERFACE_VDPAU:
      case SRT_RENDERING_INTERFACE_VAAPI:
        /* The test here tries to draw an offscreen X11 window */
        non_zero_wait_status_issue = SRT_GRAPHICS_ISSUES_CANNOT_DRAW;
        break;

      default:
        g_return_val_if_reached (SRT_GRAPHICS_ISSUES_UNKNOWN);
    }

  issues |= _srt_run_helper (&my_environ,
                             &output,
                             &child_stderr,
                             argv,
                             &wait_status,
                             &exit_status,
                             &terminating_signal,
                             FALSE,
                             non_zero_wait_status_issue);

  if (rendering_interface != SRT_RENDERING_INTERFACE_VULKAN
      && issues != SRT_GRAPHICS_ISSUES_NONE)
    {
      /* For Vulkan `LIBGL_DEBUG` has no effect. Also we try to continue even
       * if we faced some issues because we might still have a valid JSON in
       * output */

      // Issues found, so run again with LIBGL_DEBUG=verbose set in environment
      issues |= _srt_run_helper (&my_environ,
                                 &output,
                                 &child_stderr,
                                 argv,
                                 &wait_status,
                                 &exit_status,
                                 &terminating_signal,
                                 TRUE,
                                 non_zero_wait_status_issue);

      goto out;
    }

  switch (rendering_interface)
    {
      case SRT_RENDERING_INTERFACE_GL:
      case SRT_RENDERING_INTERFACE_GLESV2:
        node = json_from_string (output, &error);
        if (node == NULL)
          {
            if (error == NULL)
              g_debug ("The helper output is empty");
            else
              g_debug ("The helper output is not a valid JSON: %s", error->message);
            issues |= SRT_GRAPHICS_ISSUES_CANNOT_LOAD;

            // Issues found, so run again with LIBGL_DEBUG=verbose set in environment
            issues |= _srt_run_helper (&my_environ,
                                       &output,
                                       &child_stderr,
                                       argv,
                                       &wait_status,
                                       &exit_status,
                                       &terminating_signal,
                                       TRUE,
                                       SRT_GRAPHICS_ISSUES_CANNOT_LOAD);

            goto out;
          }

        issues |= _srt_process_wflinfo (node, &version_string, &renderer_string);

        if (issues != SRT_GRAPHICS_ISSUES_NONE)
          {
            // Issues found, so run again with LIBGL_DEBUG=verbose set in environment
            issues |= _srt_run_helper (&my_environ,
                                       &output,
                                       &child_stderr,
                                       argv,
                                       &wait_status,
                                       &exit_status,
                                       &terminating_signal,
                                       TRUE,
                                       SRT_GRAPHICS_ISSUES_CANNOT_LOAD);
          }

        if (rendering_interface == SRT_RENDERING_INTERFACE_GL &&
            window_system == SRT_WINDOW_SYSTEM_GLX)
          {
            /* Now perform *-check-gl drawing test */
            g_ptr_array_unref (argv);
            g_clear_pointer (&output, g_free);

            argv = _argv_for_check_gl (helpers_path,
                                       test_flags,
                                       multiarch_tuple,
                                       &error);

            if (argv == NULL)
              {
                issues |= SRT_GRAPHICS_ISSUES_CANNOT_DRAW;
                /* Put the error message in the 'messages' */
                child_stderr2 = g_strdup (error->message);
                goto out;
              }

            /* Now run and report exit code/messages if failure */
            issues |= _srt_run_helper (&my_environ,
                                       &output,
                                       &child_stderr2,
                                       argv,
                                       &wait_status,
                                       &exit_status,
                                       &terminating_signal,
                                       FALSE,
                                       SRT_GRAPHICS_ISSUES_CANNOT_DRAW);

            if (issues != SRT_GRAPHICS_ISSUES_NONE)
              {
                // Issues found, so run again with LIBGL_DEBUG=verbose set in environment
                issues |= _srt_run_helper (&my_environ,
                                           &output,
                                           &child_stderr2,
                                           argv,
                                           &wait_status,
                                           &exit_status,
                                           &terminating_signal,
                                           TRUE,
                                           SRT_GRAPHICS_ISSUES_CANNOT_DRAW);
              }
          }
        break;

      case SRT_RENDERING_INTERFACE_VULKAN:
        if (output == NULL || output[0] == '\0')
          {
            g_debug ("The helper output is empty");
            issues |= SRT_GRAPHICS_ISSUES_CANNOT_LOAD;
            goto out;
          }

        json_output = g_strsplit (output, "\n", -1);

        for (i = 0; json_output[i] != NULL; i++)
          {
            g_autofree gchar *new_version_string = NULL;
            g_autoptr(JsonNode) this_node = NULL;
            SrtGraphicsDevice *device = NULL;
            SrtGraphicsIssues device_issues = SRT_GRAPHICS_ISSUES_NONE;
            this_node = json_from_string (json_output[i], &error);
            if (this_node == NULL)
              {
                if (error != NULL)
                  {
                    g_debug ("The Vulkan helper output is not a valid JSON: %s", error->message);
                    g_clear_error (&error);
                  }
                break;
              }

            device_issues |= _srt_process_check_vulkan_info (this_node, &new_version_string,
                                                             &device);

            /* If we were unable to get the device info from this node, it
             * probably means that we already checked all the "device-info"
             * JSON objects. There is no need to continue because drawing
             * tests always follow the device info. */
            if (device == NULL)
              break;

            /* If the GPU 0 is a software rendering, we propagate this info to
             * the general Vulkan issues too. */
            if (i == 0 && device_issues & SRT_GRAPHICS_ISSUES_SOFTWARE_RENDERING)
              issues |= SRT_GRAPHICS_ISSUES_SOFTWARE_RENDERING;

            /* If this is the first device that we have, store its version and
             * renderer for the SrtGraphics object */
            if (version_string == NULL)
              version_string = g_strdup (new_version_string);

            if (renderer_string == NULL)
              renderer_string = srt_graphics_device_get_name (device);

            g_ptr_array_add (graphics_device, device);
          }

        for (i = 0; json_output[i] != NULL; i++)
          {
            JsonObject *object = NULL;
            JsonObject *sub_object = NULL;
            JsonNode *sub_node = NULL;
            gboolean can_draw;
            gsize index;
            const gchar *messages = NULL;
            g_autoptr(JsonNode) this_node = NULL;
            this_node = json_from_string (json_output[i], &error);
            if (this_node == NULL)
              {
                if (error != NULL)
                  {
                    g_debug ("The Vulkan helper output is not a valid JSON: %s", error->message);
                    g_clear_error (&error);
                  }
                /* Apparently we reached the final empty line */
                break;
              }

            object = json_node_get_object (this_node);
            /* We only parse the drawing test result */
            if (!json_object_has_member (object, "test"))
              continue;

            sub_node = json_object_get_member (object, "test");
            sub_object = json_node_get_object (sub_node);

            if (!json_object_has_member (sub_object, "index"))
              {
                g_debug ("The Vulkan helper output seems to be malformed");
                break;
              }

            index = json_object_get_int_member (sub_object, "index");
            can_draw = json_object_get_boolean_member_with_default (sub_object, "can-draw",
                                                                    FALSE);
            messages = json_object_get_string_member_with_default (sub_object, "error-message",
                                                                   NULL);

            if (graphics_device->len < index + 1)
              {
                g_debug ("Apparently the Vulkan helper returned more test results than devices info");
                break;
              }
            _srt_graphics_device_set_can_draw (g_ptr_array_index (graphics_device, index),
                                               can_draw);
            _srt_graphics_device_set_messages (g_ptr_array_index (graphics_device, index),
                                               messages);
          }
        break;

      case SRT_RENDERING_INTERFACE_VDPAU:
      case SRT_RENDERING_INTERFACE_VAAPI:
        if (output != NULL)
          renderer_string = output;
        break;

      default:
        g_return_val_if_reached (SRT_GRAPHICS_ISSUES_UNKNOWN);
    }

out:

  /* If we have stderr (or error messages) from both wflinfo and
   * check-gl, combine them */
  if (child_stderr2 != NULL && child_stderr2[0] != '\0')
    {
      gchar *tmp = g_strconcat (child_stderr, child_stderr2, NULL);

      g_free (child_stderr);
      child_stderr = tmp;
    }

  if (details_out != NULL)
    *details_out = _srt_graphics_new (multiarch_tuple,
                                      window_system,
                                      rendering_interface,
                                      library_vendor,
                                      renderer_string,
                                      version_string,
                                      graphics_device,
                                      issues,
                                      child_stderr,
                                      exit_status,
                                      terminating_signal);

  g_clear_pointer (&argv, g_ptr_array_unref);
  g_free (output);
  g_free (child_stderr);
  g_free (child_stderr2);
  g_clear_error (&error);
  g_strfreev (my_environ);
  return issues;
}

/**
 * srt_graphics_get_issues:
 * @self: a SrtGraphics object
 *
 * Return the problems found when loading @self.
 *
 * Returns: A bitfield containing problems, or %SRT_GRAPHICS_ISSUES_NONE
 *  if no problems were found
 */
SrtGraphicsIssues
srt_graphics_get_issues (SrtGraphics *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS (self), SRT_GRAPHICS_ISSUES_UNKNOWN);
  return self->issues;
}

/**
 * srt_graphics_library_is_vendor_neutral:
 * @self: A #SrtGraphics object
 * @vendor_out: (out) (optional): Used to return a #SrtGraphicsLibraryVendor object
 *  representing whether the entry-point library for this graphics stack is vendor-neutral
 *  or vendor-specific, and if vendor-specific, attempt to guess the vendor
 *
 * Return whether the entry-point library for this graphics stack is vendor-neutral or vendor-specific.
 * Vulkan, VDPAU and VA-API are always vendor-neutral, so this function will always return %TRUE for them.
 *
 * Returns: %TRUE if the graphics library is vendor-neutral, %FALSE otherwise.
 */
gboolean
srt_graphics_library_is_vendor_neutral (SrtGraphics *self,
                                        SrtGraphicsLibraryVendor *vendor_out)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS (self), FALSE);

  if (vendor_out != NULL)
    *vendor_out = self->library_vendor;

  return (self->rendering_interface == SRT_RENDERING_INTERFACE_VULKAN ||
          self->rendering_interface == SRT_RENDERING_INTERFACE_VDPAU ||
          self->rendering_interface == SRT_RENDERING_INTERFACE_VAAPI ||
          self->library_vendor == SRT_GRAPHICS_LIBRARY_VENDOR_GLVND);
}

/**
 * srt_graphics_get_multiarch_tuple:
 * @self: a graphics object
 *
 * Return the multiarch tuple representing the ABI of @self.
 *
 * Returns: A Debian-style multiarch tuple, usually %SRT_ABI_I386
 *  or %SRT_ABI_X86_64
 */
const char *
srt_graphics_get_multiarch_tuple (SrtGraphics *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS (self), NULL);
  return g_quark_to_string (self->multiarch_tuple);
}

/**
 * srt_graphics_get_window_system:
 * @self: a graphics object
 *
 * Return the window system tested on the given graphics object.
 *
 * Returns: An enumeration of #SrtWindowSystem which window system was tested.
 */
SrtWindowSystem
srt_graphics_get_window_system (SrtGraphics *self)
{
  // Not sure what to return if self is not a SrtGraphics object, maybe need
  // to add a SRT_WINDOW_SYSTEM_NONE ?
  g_return_val_if_fail (SRT_IS_GRAPHICS (self), 0);
  return self->window_system;
}

/**
 * srt_graphics_get_rendering_interface:
 * @self: a graphics object
 *
 * Return the rendering interface which was tested on the given graphics object.
 *
 * Returns: An enumeration of #SrtRenderingInterface indicating which rendering
 * interface was tested.
 */
SrtRenderingInterface
srt_graphics_get_rendering_interface (SrtGraphics *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS (self), 0);
  return self->rendering_interface;
}

/**
 * srt_graphics_get_version_string:
 * @self: a graphics object
 *
 * Return the version string found when testing the given graphics.
 *
 * Returns: A string indicating the version found when testing graphics.
 */
const char *
srt_graphics_get_version_string (SrtGraphics *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS (self), NULL);
  return self->version_string;
}

/**
 * srt_graphics_get_exit_status:
 * @self: a graphics object
 *
 * Return the exit status of helpers when testing the given graphics.
 *
 * Returns: 0 on success, positive on unsuccessful `exit()`,
 *          -1 if killed by a signal or not run at all
 */
int srt_graphics_get_exit_status (SrtGraphics *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS (self), -1);

  return self->exit_status;
}

/**
 * srt_graphics_get_terminating_signal:
 * @self: a graphics object
 *
 * Return the terminating signal used to terminate the helper if any.
 *
 * Returns: a signal number such as `SIGTERM`, or 0 if not killed by a signal or not run at all.
 */
int srt_graphics_get_terminating_signal (SrtGraphics *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS (self), -1);

  return self->terminating_signal;
}

/**
 * srt_graphics_get_renderer_string:
 * @self: a graphics object
 *
 * Return the renderer string found when testing the given graphics.
 *
 * Returns: A string indicating the renderer found when testing graphics.
 */
const char *
srt_graphics_get_renderer_string (SrtGraphics *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS (self), NULL);
  return self->renderer_string;
}

/**
 * srt_graphics_dup_parameters_string:
 * @self: a graphics object
 *
 * Return a string indicating which window system and rendering interface were
 * tested, for example "glx/gl" for "desktop" OpenGL on X11 via GLX, or
 * "egl_x11/glesv2" for OpenGLES v2 on X11 via the Khronos Native Platform
 * Graphics Interface (EGL).
 *
 * Returns: (transfer full): sA string indicating which parameters were tested.
 *  Free with g_free().
 */
gchar *
srt_graphics_dup_parameters_string (SrtGraphics *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS (self), NULL);
  return g_strdup_printf ("%s/%s",
                          _srt_graphics_window_system_string (self->window_system),
                          _srt_graphics_rendering_interface_string (self->rendering_interface));
}

/**
 * srt_graphics_get_messages:
 * @self: a graphics object
 *
 * Return the diagnostic messages produced while checking this graphics
 * stack, if any.
 *
 * Returns: (nullable) (transfer none): A string, which must not be freed,
 *  or %NULL if there were no diagnostic messages.
 */
const char *
srt_graphics_get_messages (SrtGraphics *self)
{
  g_return_val_if_fail (SRT_IS_GRAPHICS (self), NULL);
  return self->messages;
}

/**
 * srt_graphics_get_devices:
 * @self: a graphics object
 *
 * Return the list of graphics devices that have been found.
 *
 * Returns: (transfer full) (element-type SrtGraphicsDevice): The graphics
 *  devices. Free with `g_list_free_full (list, g_object_unref)`.
 */
GList *
srt_graphics_get_devices (SrtGraphics *self)
{
  GList *ret = NULL;
  guint i;

  g_return_val_if_fail (SRT_IS_GRAPHICS (self), NULL);

  for (i = 0; self->devices != NULL && i < self->devices->len; i++)
    ret = g_list_prepend (ret, g_object_ref (g_ptr_array_index (self->devices, i)));

  return g_list_reverse (ret);
}

static SrtGraphicsIssues
_srt_get_graphics_issues_from_json_object (JsonObject *json_obj)
{
  JsonArray *array;
  SrtGraphicsIssues graphics_issues = SRT_GRAPHICS_ISSUES_NONE;
  gsize j;

  /* In graphics, a missing "issues" array means that no issues were found */
  if (json_object_has_member (json_obj, "issues"))
    {
      array = json_object_get_array_member (json_obj, "issues");

      /* We are expecting an array of issues here */
      if (array == NULL)
        {
          g_debug ("'issues' in 'graphics-details' is not an array as expected");
          graphics_issues |= SRT_GRAPHICS_ISSUES_UNKNOWN;
        }
      else
        {
          for (j = 0; j < json_array_get_length (array); j++)
            {
              const gchar *issue_string = json_array_get_string_element (array, j);
              if (!srt_add_flag_from_nick (SRT_TYPE_GRAPHICS_ISSUES,
                                            issue_string,
                                            &graphics_issues,
                                            NULL))
                graphics_issues |= SRT_GRAPHICS_ISSUES_UNKNOWN;
            }
        }
    }
  return graphics_issues;
}

/**
 * _srt_graphics_get_from_report:
 * @json_obj: (not nullable): A JSON Object representing an ABI,
 *  which is checked for a "graphics-details" member
 * @multiarch_tuple: (not nullable) (type filename): A Debian-style multiarch tuple
 *  such as %SRT_ABI_X86_64
 * @cached_graphics: (not optional) (inout): An hash table with the #SrtGraphics
 *  objects that have been found. The hash key is an int generated combining
 *  the graphics window system and rendering interface.
 */
void
_srt_graphics_get_from_report (JsonObject *json_obj,
                               const gchar *multiarch_tuple,
                               GHashTable **cached_graphics)
{
  JsonObject *json_graphics_obj;
  JsonObject *json_stack_obj;
  JsonArray *array;

  g_return_if_fail (json_obj != NULL);
  g_return_if_fail (multiarch_tuple != NULL);
  g_return_if_fail (cached_graphics != NULL && *cached_graphics != NULL);

  if (json_object_has_member (json_obj, "graphics-details"))
    {
      GList *graphics_members;
      json_graphics_obj = json_object_get_object_member (json_obj, "graphics-details");

      if (json_graphics_obj == NULL)
        return;

      graphics_members = json_object_get_members (json_graphics_obj);
      for (GList *l = graphics_members; l != NULL; l = l->next)
        {
          g_autofree gchar *messages = NULL;
          const gchar *renderer = NULL;
          const gchar *version = NULL;
          int exit_status;
          int terminating_signal;
          SrtGraphics *graphics;
          SrtGraphicsLibraryVendor library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN;
          SrtGraphicsIssues graphics_issues = SRT_GRAPHICS_ISSUES_NONE;
          SrtWindowSystem window_system = SRT_WINDOW_SYSTEM_X11;
          SrtRenderingInterface rendering_interface = SRT_RENDERING_INTERFACE_GL;
          g_autoptr(GPtrArray) devices = g_ptr_array_new_with_free_func (g_object_unref);
          gsize j;

          gchar **stack_parts = g_strsplit (l->data, "/", 2);
          if (stack_parts[1] == NULL)
            {
              g_debug ("Expected to find a parameter divided by a slash, instead we got: %s",
                       (gchar *) l->data);
              g_strfreev (stack_parts);
              continue;
            }

          if (!_srt_graphics_window_system_nick_to_enum (stack_parts[0], &window_system, NULL))
            {
              g_debug ("Unable to get the window system from the parsed enum: %s",
                       stack_parts[0]);
              g_strfreev (stack_parts);
              continue;
            }
          if (!_srt_graphics_rendering_interface_nick_to_enum (stack_parts[1],
                                                               &rendering_interface,
                                                               NULL))
            {
              g_debug ("Unable to get the rendering interface from the parsed enum: %s",
                       stack_parts[1]);
              g_strfreev (stack_parts);
              continue;
            }

          g_strfreev (stack_parts);

          json_stack_obj = json_object_get_object_member (json_graphics_obj, l->data);

          if (json_stack_obj == NULL)
            {
              g_debug ("'%s' is not a JSON object as expected", (gchar *) l->data);
              continue;
            }

          messages = _srt_json_object_dup_array_of_lines_member (json_stack_obj, "messages");
          renderer = json_object_get_string_member_with_default (json_stack_obj, "renderer",
                                                                 NULL);
          version = json_object_get_string_member_with_default (json_stack_obj, "version",
                                                                NULL);

          graphics_issues = _srt_get_graphics_issues_from_json_object (json_stack_obj);

          /* A missing exit_status means that its value is the default zero */
          exit_status = json_object_get_int_member_with_default (json_stack_obj, "exit-status", 0);
          terminating_signal = json_object_get_int_member_with_default (json_stack_obj,
                                                                        "terminating-signal",
                                                                        0);

          if (json_object_has_member (json_stack_obj, "library-vendor"))
            {
              const gchar *vendor_string = json_object_get_string_member (json_stack_obj, "library-vendor");
              G_STATIC_ASSERT (sizeof (SrtGraphicsLibraryVendor) == sizeof (gint));
              if (!srt_enum_from_nick (SRT_TYPE_GRAPHICS_LIBRARY_VENDOR,
                                       vendor_string,
                                       (gint *) &library_vendor,
                                       NULL))
                library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN;
            }

          if (json_object_has_member (json_stack_obj, "devices"))
            {
              array = json_object_get_array_member (json_stack_obj, "devices");
              gsize array_size = 0;

              /* We are expecting an array of devices here */
              if (array == NULL)
                g_debug ("'devices' in 'graphics-details' is not an array as expected");
              else
                array_size =  json_array_get_length (array);

              for (j = 0; j < array_size; j++)
                {
                  SrtGraphicsDevice *device = NULL;
                  const gchar *name;
                  const gchar *api_version;
                  const gchar *driver_name;
                  const gchar *driver_version;
                  const gchar *vendor_id;
                  const gchar *device_id;
                  g_autofree gchar *dev_messages = NULL;
                  SrtVkPhysicalDeviceType type = SRT_VK_PHYSICAL_DEVICE_TYPE_OTHER;
                  SrtGraphicsIssues issues = SRT_GRAPHICS_ISSUES_NONE;
                  gint64 driver_id;

                  JsonObject *device_obj = json_array_get_object_element (array, j);
                  name = json_object_get_string_member_with_default (device_obj, "name",
                                                                     NULL);
                  api_version = json_object_get_string_member_with_default (device_obj,
                                                                            "api-version",
                                                                            NULL);
                  driver_id = json_object_get_int_member_with_default (device_obj,
                                                                       "vulkan-driver-id",
                                                                       0);
                  driver_name = json_object_get_string_member_with_default (device_obj,
                                                                            "driver-name",
                                                                            NULL);
                  driver_version = json_object_get_string_member_with_default (device_obj,
                                                                               "driver-version",
                                                                               NULL);
                  vendor_id = json_object_get_string_member_with_default (device_obj,
                                                                          "vendor-id",
                                                                          NULL);
                  device_id = json_object_get_string_member_with_default (device_obj,
                                                                          "device-id",
                                                                          NULL);

                  if (json_object_has_member (device_obj, "type"))
                    {
                      const gchar *type_str = json_object_get_string_member (device_obj, "type");
                      G_STATIC_ASSERT (sizeof (SrtVkPhysicalDeviceType) == sizeof (gint));
                      if (!srt_enum_from_nick (SRT_TYPE_VK_PHYSICAL_DEVICE_TYPE,
                                              type_str,
                                              (gint *) &type,
                                              NULL))
                        type = SRT_VK_PHYSICAL_DEVICE_TYPE_OTHER;
                    }

                  issues = _srt_get_graphics_issues_from_json_object (device_obj);
                  dev_messages = _srt_json_object_dup_array_of_lines_member (device_obj,
                                                                             "messages");

                  if (driver_id < 0 || driver_id > G_MAXUINT32)
                    driver_id = 0;

                  device = _srt_graphics_device_new (name, api_version,
                                                     (guint32) driver_id,
                                                     driver_name,
                                                     driver_version,
                                                     vendor_id, device_id, type,
                                                     issues);
                  _srt_graphics_device_set_messages (device, dev_messages);
                  g_ptr_array_add (devices, device);
                }
            }

          graphics = _srt_graphics_new (multiarch_tuple,
                                        window_system,
                                        rendering_interface,
                                        library_vendor,
                                        renderer,
                                        version,
                                        devices,
                                        graphics_issues,
                                        messages,
                                        exit_status,
                                        terminating_signal);

          int hash_key = _srt_graphics_hash_key (window_system, rendering_interface);
          g_hash_table_insert (*cached_graphics, GINT_TO_POINTER(hash_key), graphics);
        }
    }
}
