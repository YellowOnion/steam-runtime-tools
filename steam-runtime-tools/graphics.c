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
 *
 * #SrtEglIcd is an opaque object representing the metadata describing
 * an EGL ICD.
 * This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 *
 * #SrtVulkanIcd is an opaque object representing the metadata describing
 * a Vulkan ICD.
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
 * @wait_status: (out) (transfer full): The wait status of the helper
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

  if (*wait_status != 0)
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

typedef struct
{
  gchar *name;
  gchar *spec_version;
  gchar **entrypoints;
} DeviceExtension;

typedef struct
{
  gchar *name;
  gchar *spec_version;
} InstanceExtension;

typedef struct
{
  gchar *name;
  gchar *value;
} EnvironmentVariable;

/* EGL and Vulkan ICDs are actually basically the same, but we don't
 * hard-code that in the API.
 * Vulkan Layers have the same structure too but with some extra fields. */
typedef struct
{
  GError *error;
  SrtLoadableIssues issues;
  gchar *api_version;   /* Always NULL when found in a SrtEglIcd */
  gchar *json_path;
  /* Either a filename, or a relative/absolute path in the sysroot */
  gchar *library_path;
  gchar *file_format_version;
  gchar *name;
  gchar *type;
  gchar *implementation_version;
  gchar *description;
  GStrv component_layers;
  /* Standard name => dlsym() name to call instead
   * (element-type utf8 utf8) */
  GHashTable *functions;
  /* (element-type InstanceExtension) */
  GList *instance_extensions;
  /* Standard name to intercept => dlsym() name to call instead
   * (element-type utf8 utf8) */
  GHashTable *pre_instance_functions;
  /* (element-type DeviceExtension) */
  GList *device_extensions;
  EnvironmentVariable enable_env_var;
  EnvironmentVariable disable_env_var;
} SrtLoadable;

static void
device_extension_free (gpointer p)
{
  DeviceExtension *self = p;

  g_free (self->name);
  g_free (self->spec_version);
  g_strfreev (self->entrypoints);
  g_slice_free (DeviceExtension, self);
}

static void
instance_extension_free (gpointer p)
{
  InstanceExtension *self = p;

  g_free (self->name);
  g_free (self->spec_version);
  g_slice_free (InstanceExtension, self);
}

static void
srt_loadable_clear (SrtLoadable *self)
{
  g_clear_error (&self->error);
  g_clear_pointer (&self->api_version, g_free);
  g_clear_pointer (&self->json_path, g_free);
  g_clear_pointer (&self->library_path, g_free);
  g_clear_pointer (&self->file_format_version, g_free);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->type, g_free);
  g_clear_pointer (&self->implementation_version, g_free);
  g_clear_pointer (&self->description, g_free);
  g_clear_pointer (&self->component_layers, g_strfreev);
  g_clear_pointer (&self->functions, g_hash_table_unref);
  g_list_free_full (g_steal_pointer (&self->instance_extensions), instance_extension_free);
  g_clear_pointer (&self->pre_instance_functions, g_hash_table_unref);
  g_list_free_full (g_steal_pointer (&self->device_extensions), device_extension_free);
  g_clear_pointer (&self->enable_env_var.name, g_free);
  g_clear_pointer (&self->enable_env_var.value, g_free);
  g_clear_pointer (&self->disable_env_var.name, g_free);
  g_clear_pointer (&self->disable_env_var.value, g_free);
}

/*
 * See srt_egl_icd_resolve_library_path(),
 * srt_vulkan_icd_resolve_library_path() or
 * srt_vulkan_layer_resolve_library_path()
 */
static gchar *
srt_loadable_resolve_library_path (const SrtLoadable *self)
{
  gchar *dir;
  gchar *ret;

  /*
   * In Vulkan, this function behaves according to the specification:
   *
   * The "library_path" specifies either a filename, a relative pathname,
   * or a full pathname to an ICD shared library file. If "library_path"
   * specifies a relative pathname, it is relative to the path of the
   * JSON manifest file. If "library_path" specifies a filename, the
   * library must live in the system's shared object search path.
   * — https://github.com/KhronosGroup/Vulkan-Loader/blob/sdk-1.2.198.1/docs/LoaderDriverInterface.md#driver-manifest-file-format
   * — https://github.com/KhronosGroup/Vulkan-Loader/blob/sdk-1.2.198.1/docs/LoaderLayerInterface.md#layer-manifest-file-format
   *
   * In GLVND, EGL ICDs with relative pathnames are currently passed
   * directly to dlopen(), which will interpret them as relative to
   * the current working directory - but upstream acknowledge in
   * https://github.com/NVIDIA/libglvnd/issues/187 that this is not
   * actually very useful, and have indicated that they would consider
   * a patch to give it the same behaviour as Vulkan instead.
   */

  if (self->library_path == NULL)
    return NULL;

  if (self->library_path[0] == '/')
    return g_strdup (self->library_path);

  if (strchr (self->library_path, '/') == NULL)
    return g_strdup (self->library_path);

  dir = g_path_get_dirname (self->json_path);
  ret = g_build_filename (dir, self->library_path, NULL);
  g_free (dir);
  g_return_val_if_fail (g_path_is_absolute (ret), ret);
  return ret;
}

/* See srt_egl_icd_check_error(), srt_vulkan_icd_check_error() */
static gboolean
srt_loadable_check_error (const SrtLoadable *self,
                        GError **error)
{
  if (self->error != NULL && error != NULL)
    *error = g_error_copy (self->error);

  return (self->error == NULL);
}

/* See srt_egl_icd_write_to_file(), srt_vulkan_icd_write_to_file() and
 * srt_vulkan_layer_write_to_file() */
static gboolean
srt_loadable_write_to_file (const SrtLoadable *self,
                            const char *path,
                            GType which,
                            GError **error)
{
  JsonBuilder *builder;
  JsonGenerator *generator;
  JsonNode *root;
  gchar *json_output;
  gboolean ret = FALSE;
  const gchar *member;
  GHashTableIter iter;
  gpointer key;
  gpointer value;
  const GList *l;

  if (which == SRT_TYPE_EGL_ICD || which == SRT_TYPE_VULKAN_ICD)
    member = "ICD";
  else if (which == SRT_TYPE_VULKAN_LAYER)
    member = "layer";
  else
    g_return_val_if_reached (FALSE);

  if (!srt_loadable_check_error (self, error))
    {
      g_prefix_error (error,
                      "Cannot save %s metadata to file because it is invalid: ",
                      member);
      return FALSE;
    }

  builder = json_builder_new ();
  json_builder_begin_object (builder);
    {
      if (which == SRT_TYPE_EGL_ICD || which == SRT_TYPE_VULKAN_ICD)
        {
          json_builder_set_member_name (builder, "file_format_version");
          /* We parse and store all the information defined in file format
           * version 1.0.0, but nothing beyond that, so we use this version
           * in our output instead of quoting whatever was in the input.
           *
           * We don't currently need to distinguish between EGL and Vulkan here
           * because the file format version we understand happens to be the
           * same for both. */
           json_builder_add_string_value (builder, "1.0.0");

           json_builder_set_member_name (builder, member);
           json_builder_begin_object (builder);
            {
              json_builder_set_member_name (builder, "library_path");
              json_builder_add_string_value (builder, self->library_path);

              /* In the EGL case we don't have the "api_version" field. */
              if (which == SRT_TYPE_VULKAN_ICD)
                {
                  json_builder_set_member_name (builder, "api_version");
                  json_builder_add_string_value (builder, self->api_version);
                }
            }
           json_builder_end_object (builder);
        }
      else if (which == SRT_TYPE_VULKAN_LAYER)
        {
          json_builder_set_member_name (builder, "file_format_version");
          /* In the Vulkan layer specs the file format version is a required field.
           * However it might happen that we are not aware of its value, e.g. when we
           * parse an s-r-s-i report. Because of that, if the file format version info
           * is missing, we don't consider it a fatal error and we just set it to the
           * lowest version that is required, based on the fields we have. */
          if (self->file_format_version == NULL)
            {
              if (self->pre_instance_functions != NULL)
                json_builder_add_string_value (builder, "1.1.2");
              else if (self->component_layers != NULL && self->component_layers[0] != NULL)
                json_builder_add_string_value (builder, "1.1.1");
              else
                json_builder_add_string_value (builder, "1.1.0");
            }
          else
            {
              json_builder_add_string_value (builder, self->file_format_version);
            }

          json_builder_set_member_name (builder, "layer");
          json_builder_begin_object (builder);
            {
              json_builder_set_member_name (builder, "name");
              json_builder_add_string_value (builder, self->name);

              json_builder_set_member_name (builder, "type");
              json_builder_add_string_value (builder, self->type);

              if (self->library_path != NULL)
                {
                  json_builder_set_member_name (builder, "library_path");
                  json_builder_add_string_value (builder, self->library_path);
                }

              json_builder_set_member_name (builder, "api_version");
              json_builder_add_string_value (builder, self->api_version);

              json_builder_set_member_name (builder, "implementation_version");
              json_builder_add_string_value (builder, self->implementation_version);

              json_builder_set_member_name (builder, "description");
              json_builder_add_string_value (builder, self->description);

              _srt_json_builder_add_strv_value (builder, "component_layers",
                                                (const gchar * const *) self->component_layers,
                                                FALSE);

              if (self->functions != NULL)
                {
                  json_builder_set_member_name (builder, "functions");
                  json_builder_begin_object (builder);
                  g_hash_table_iter_init (&iter, self->functions);
                  while (g_hash_table_iter_next (&iter, &key, &value))
                    {
                      json_builder_set_member_name (builder, key);
                      json_builder_add_string_value (builder, value);
                    }
                  json_builder_end_object (builder);
                }

              if (self->pre_instance_functions != NULL)
                {
                  json_builder_set_member_name (builder, "pre_instance_functions");
                  json_builder_begin_object (builder);
                  g_hash_table_iter_init (&iter, self->pre_instance_functions);
                  while (g_hash_table_iter_next (&iter, &key, &value))
                    {
                      json_builder_set_member_name (builder, key);
                      json_builder_add_string_value (builder, value);
                    }
                  json_builder_end_object (builder);
                }

              if (self->instance_extensions != NULL)
                {
                  json_builder_set_member_name (builder, "instance_extensions");
                  json_builder_begin_array (builder);
                  for (l = self->instance_extensions; l != NULL; l = l->next)
                    {
                      InstanceExtension *ie = l->data;
                      json_builder_begin_object (builder);
                      json_builder_set_member_name (builder, "name");
                      json_builder_add_string_value (builder, ie->name);
                      json_builder_set_member_name (builder, "spec_version");
                      json_builder_add_string_value (builder, ie->spec_version);
                      json_builder_end_object (builder);
                    }
                  json_builder_end_array (builder);
                }

              if (self->device_extensions != NULL)
                {
                  json_builder_set_member_name (builder, "device_extensions");
                  json_builder_begin_array (builder);
                  for (l = self->device_extensions; l != NULL; l = l->next)
                    {
                      DeviceExtension *de = l->data;
                      json_builder_begin_object (builder);
                      json_builder_set_member_name (builder, "name");
                      json_builder_add_string_value (builder, de->name);
                      json_builder_set_member_name (builder, "spec_version");
                      json_builder_add_string_value (builder, de->spec_version);
                      _srt_json_builder_add_strv_value (builder, "entrypoints",
                                                        (const gchar * const *) de->entrypoints,
                                                        FALSE);
                      json_builder_end_object (builder);
                    }
                  json_builder_end_array (builder);
                }

              if (self->enable_env_var.name != NULL)
                {
                  json_builder_set_member_name (builder, "enable_environment");
                  json_builder_begin_object (builder);
                  json_builder_set_member_name (builder, self->enable_env_var.name);
                  json_builder_add_string_value (builder, self->enable_env_var.value);
                  json_builder_end_object (builder);
                }

              if (self->disable_env_var.name != NULL)
                {
                  json_builder_set_member_name (builder, "disable_environment");
                  json_builder_begin_object (builder);
                  json_builder_set_member_name (builder, self->disable_env_var.name);
                  json_builder_add_string_value (builder, self->disable_env_var.value);
                  json_builder_end_object (builder);
                }
            }
          json_builder_end_object (builder);
        }
    }
  json_builder_end_object (builder);

  root = json_builder_get_root (builder);
  generator = json_generator_new ();
  json_generator_set_root (generator, root);
  json_generator_set_pretty (generator, TRUE);
  json_output = json_generator_to_data (generator, NULL);

  ret = g_file_set_contents (path, json_output, -1, error);

  if (!ret)
    g_prefix_error (error, "Cannot save %s metadata to file :", member);

  g_free (json_output);
  g_object_unref (generator);
  json_node_free (root);
  g_object_unref (builder);
  return ret;
}

/*
 * A #GCompareFunc that does not sort the members of the directory.
 */
#define READDIR_ORDER ((GCompareFunc) NULL)

/*
 * load_json_dir:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @dir: A directory to search
 * @suffix: (nullable): A path to append to @dir, such as `"vulkan/icd.d"`
 * @sort: (nullable): If not %NULL, load ICDs sorted by filename in this order
 * @load_json_cb: Called for each potential ICD found
 * @user_data: Passed to @load_json_cb
 */
static void
load_json_dir (const char *sysroot,
               const char *dir,
               const char *suffix,
               GCompareFunc sort,
               void (*load_json_cb) (const char *, const char *, void *),
               void *user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GDir) dir_iter = NULL;
  g_autofree gchar *canon = NULL;
  g_autofree gchar *sysrooted_dir = NULL;
  g_autofree gchar *suffixed_dir = NULL;
  const char *iter_dir;
  const char *member;
  g_autoptr(GPtrArray) members = NULL;
  gsize i;

  g_return_if_fail (sysroot != NULL);
  g_return_if_fail (load_json_cb != NULL);

  if (dir == NULL)
    return;

  if (!g_path_is_absolute (dir))
    {
      canon = g_canonicalize_filename (dir, NULL);
      dir = canon;
    }

  if (suffix != NULL)
    {
      suffixed_dir = g_build_filename (dir, suffix, NULL);
      dir = suffixed_dir;
    }

  sysrooted_dir = g_build_filename (sysroot, dir, NULL);
  iter_dir = sysrooted_dir;

  g_debug ("Looking for ICDs in %s (in sysroot %s)...", dir, sysroot);

  dir_iter = g_dir_open (iter_dir, 0, &error);

  if (dir_iter == NULL)
    {
      g_debug ("Failed to open \"%s\": %s", iter_dir, error->message);
      return;
    }

  members = g_ptr_array_new_with_free_func (g_free);

  while ((member = g_dir_read_name (dir_iter)) != NULL)
    {
      if (!g_str_has_suffix (member, ".json"))
        continue;

      g_ptr_array_add (members, g_strdup (member));
    }

  if (sort != READDIR_ORDER)
    g_ptr_array_sort (members, sort);

  for (i = 0; i < members->len; i++)
    {
      gchar *path;

      member = g_ptr_array_index (members, i);
      path = g_build_filename (dir, member, NULL);
      load_json_cb (sysroot, path, user_data);
      g_free (path);
    }
}

/*
 * load_json_dir:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @search_paths: Directories to search
 * @suffix: (nullable): A path to append to @dir, such as `"vulkan/icd.d"`
 * @sort: (nullable): If not %NULL, load ICDs sorted by filename in this order
 * @load_json_cb: Called for each potential ICD found
 * @user_data: Passed to @load_json_cb
 *
 * If @search_paths contains duplicated directories they'll be filtered out
 * to prevent loading the same JSONs multiple times.
 */
static void
load_json_dirs (const char *sysroot,
                GStrv search_paths,
                const char *suffix,
                GCompareFunc sort,
                void (*load_json_cb) (const char *, const char *, void *),
                void *user_data)
{
  gchar **iter;
  g_autoptr(GHashTable) searched_set = NULL;
  g_autoptr(GError) error = NULL;
  glnx_autofd int sysroot_fd = -1;

  g_return_if_fail (sysroot != NULL);
  g_return_if_fail (load_json_cb != NULL);

  searched_set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  if (!glnx_opendirat (-1, sysroot, FALSE, &sysroot_fd, &error))
    {
      g_warning ("An error occurred trying to open \"%s\": %s", sysroot,
                 error->message);
      return;
    }

  for (iter = search_paths;
       iter != NULL && *iter != NULL;
       iter++)
    {
      glnx_autofd int file_fd = -1;
      g_autofree gchar *file_realpath_in_sysroot = NULL;

      file_fd = _srt_resolve_in_sysroot (sysroot_fd,
                                         *iter, SRT_RESOLVE_FLAGS_NONE,
                                         &file_realpath_in_sysroot, &error);

      if (file_realpath_in_sysroot == NULL)
        {
          /* Skip it if the path doesn't exist or is not reachable */
          g_debug ("An error occurred while resolving \"%s\": %s", *iter, error->message);
          g_clear_error (&error);
          continue;
        }

      if (!g_hash_table_contains (searched_set, file_realpath_in_sysroot))
        {
          g_hash_table_add (searched_set, g_steal_pointer (&file_realpath_in_sysroot));
          load_json_dir (sysroot, *iter, suffix, sort, load_json_cb, user_data);
        }
      else
        {
          g_debug ("Skipping \"%s\" because we already loaded the JSONs from it",
                   file_realpath_in_sysroot);
        }
    }
}

/*
 * load_json:
 * @type: %SRT_TYPE_EGL_ICD or %SRT_TYPE_VULKAN_ICD
 * @path: (type filename) (transfer none): Path of JSON file
 * @api_version_out: (out) (type utf8) (transfer full): Used to return
 *  API version for %SRT_TYPE_VULKAN_ICD
 * @library_path_out: (out) (type utf8) (transfer full): Used to return
 *  shared library path
 * @issues_out: (out) (optional): Used to return problems with this ICD.
 *  Note that this is set even if this function fails.
 * @error: Used to raise an error on failure
 *
 * Try to load an EGL or Vulkan ICD from a JSON file.
 *
 * Returns: %TRUE if the JSON file was loaded successfully
 */
static gboolean
load_json (GType type,
           const char *path,
           gchar **api_version_out,
           gchar **library_path_out,
           SrtLoadableIssues *issues_out,
           GError **error)
{
  JsonParser *parser = NULL;
  gboolean ret = FALSE;
  /* These are all borrowed from the parser */
  JsonNode *node;
  JsonObject *object;
  JsonNode *subnode;
  JsonObject *icd_object;
  const char *file_format_version;
  const char *api_version = NULL;
  const char *library_path;
  SrtLoadableIssues issues = SRT_LOADABLE_ISSUES_NONE;

  g_return_val_if_fail (type == SRT_TYPE_VULKAN_ICD
                        || type == SRT_TYPE_EGL_ICD,
                        FALSE);
  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (api_version_out == NULL || *api_version_out == NULL,
                        FALSE);
  g_return_val_if_fail (library_path_out == NULL || *library_path_out == NULL,
                        FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_debug ("Attempting to load %s from %s", g_type_name (type), path);

  parser = json_parser_new ();

  if (!json_parser_load_from_file (parser, path, error))
    {
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  node = json_parser_get_root (parser);

  if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Expected to find a JSON object in \"%s\"", path);
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  object = json_node_get_object (node);

  subnode = json_object_get_member (object, "file_format_version");

  if (subnode == NULL
      || !JSON_NODE_HOLDS_VALUE (subnode))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "file_format_version in \"%s\" missing or not a value",
                   path);
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  file_format_version = json_node_get_string (subnode);

  if (file_format_version == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "file_format_version in \"%s\" not a string", path);
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  if (type == SRT_TYPE_VULKAN_ICD)
    {
      /*
       * The compatibility rules for Vulkan ICDs are not clear.
       * See https://github.com/KhronosGroup/Vulkan-Loader/issues/248
       *
       * The reference loader currently logs a warning, but carries on
       * anyway, if the file format version is not 1.0.0 or 1.0.1.
       * However, on #248 there's a suggestion that all the format versions
       * that are valid for layer JSON (1.0.x up to 1.0.1 and 1.1.x up
       * to 1.1.2) should also be considered valid for ICD JSON. For now
       * we assume that the rule is the same as for EGL, below.
       */
      if (!g_str_has_prefix (file_format_version, "1.0."))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "Vulkan file_format_version in \"%s\" is not 1.0.x",
                       path);
          issues |= SRT_LOADABLE_ISSUES_UNSUPPORTED;
          goto out;
        }
    }
  else
    {
      g_assert (type == SRT_TYPE_EGL_ICD);
      /*
       * For EGL, all 1.0.x versions are officially backwards compatible
       * with 1.0.0.
       * https://github.com/NVIDIA/libglvnd/blob/master/src/EGL/icd_enumeration.md
       */
      if (!g_str_has_prefix (file_format_version, "1.0."))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "EGL file_format_version in \"%s\" is not 1.0.x",
                       path);
          issues |= SRT_LOADABLE_ISSUES_UNSUPPORTED;
          goto out;
        }
    }

  subnode = json_object_get_member (object, "ICD");

  if (subnode == NULL
      || !JSON_NODE_HOLDS_OBJECT (subnode))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No \"ICD\" object in \"%s\"", path);
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  icd_object = json_node_get_object (subnode);

  if (type == SRT_TYPE_VULKAN_ICD)
    {
      subnode = json_object_get_member (icd_object, "api_version");

      if (subnode == NULL
          || !JSON_NODE_HOLDS_VALUE (subnode))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "ICD.api_version in \"%s\" missing or not a value",
                       path);
          issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
          goto out;
        }

      api_version = json_node_get_string (subnode);

      if (api_version == NULL)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "ICD.api_version in \"%s\" not a string", path);
          issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
          goto out;
        }
    }

  subnode = json_object_get_member (icd_object, "library_path");

  if (subnode == NULL
      || !JSON_NODE_HOLDS_VALUE (subnode))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "ICD.library_path in \"%s\" missing or not a value",
                   path);
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  library_path = json_node_get_string (subnode);

  if (library_path == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "ICD.library_path in \"%s\" not a string", path);
      issues |= SRT_LOADABLE_ISSUES_CANNOT_LOAD;
      goto out;
    }

  if (api_version_out != NULL)
    *api_version_out = g_strdup (api_version);

  if (library_path_out != NULL)
    *library_path_out = g_strdup (library_path);

  ret = TRUE;

out:
  g_clear_object (&parser);

  if (issues_out != NULL)
    *issues_out = issues;

  return ret;
}

/**
 * SrtEglIcd:
 *
 * Opaque object representing an EGL ICD.
 */

struct _SrtEglIcd
{
  /*< private >*/
  GObject parent;
  SrtLoadable icd;
};

struct _SrtEglIcdClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum
{
  EGL_ICD_PROP_0,
  EGL_ICD_PROP_ERROR,
  EGL_ICD_PROP_ISSUES,
  EGL_ICD_PROP_JSON_PATH,
  EGL_ICD_PROP_LIBRARY_PATH,
  EGL_ICD_PROP_RESOLVED_LIBRARY_PATH,
  N_EGL_ICD_PROPERTIES
};

G_DEFINE_TYPE (SrtEglIcd, srt_egl_icd, G_TYPE_OBJECT)

static void
srt_egl_icd_init (SrtEglIcd *self)
{
}

static void
srt_egl_icd_get_property (GObject *object,
                          guint prop_id,
                          GValue *value,
                          GParamSpec *pspec)
{
  SrtEglIcd *self = SRT_EGL_ICD (object);

  switch (prop_id)
    {
      case EGL_ICD_PROP_ERROR:
        g_value_set_boxed (value, self->icd.error);
        break;

      case EGL_ICD_PROP_ISSUES:
        g_value_set_flags (value, self->icd.issues);
        break;

      case EGL_ICD_PROP_JSON_PATH:
        g_value_set_string (value, self->icd.json_path);
        break;

      case EGL_ICD_PROP_LIBRARY_PATH:
        g_value_set_string (value, self->icd.library_path);
        break;

      case EGL_ICD_PROP_RESOLVED_LIBRARY_PATH:
        g_value_take_string (value, srt_egl_icd_resolve_library_path (self));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_egl_icd_set_property (GObject *object,
                          guint prop_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
  SrtEglIcd *self = SRT_EGL_ICD (object);
  const char *tmp;

  switch (prop_id)
    {
      case EGL_ICD_PROP_ERROR:
        g_return_if_fail (self->icd.error == NULL);
        self->icd.error = g_value_dup_boxed (value);
        break;

      case EGL_ICD_PROP_ISSUES:
        g_return_if_fail (self->icd.issues == 0);
        self->icd.issues = g_value_get_flags (value);
        break;

      case EGL_ICD_PROP_JSON_PATH:
        g_return_if_fail (self->icd.json_path == NULL);
        tmp = g_value_get_string (value);
        self->icd.json_path = g_canonicalize_filename (tmp, NULL);
        break;

      case EGL_ICD_PROP_LIBRARY_PATH:
        g_return_if_fail (self->icd.library_path == NULL);
        self->icd.library_path = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_egl_icd_constructed (GObject *object)
{
  SrtEglIcd *self = SRT_EGL_ICD (object);

  g_return_if_fail (self->icd.json_path != NULL);
  g_return_if_fail (g_path_is_absolute (self->icd.json_path));
  g_return_if_fail (self->icd.api_version == NULL);

  if (self->icd.error != NULL)
    g_return_if_fail (self->icd.library_path == NULL);
  else
    g_return_if_fail (self->icd.library_path != NULL);
}

static void
srt_egl_icd_finalize (GObject *object)
{
  SrtEglIcd *self = SRT_EGL_ICD (object);

  srt_loadable_clear (&self->icd);

  G_OBJECT_CLASS (srt_egl_icd_parent_class)->finalize (object);
}

static GParamSpec *egl_icd_properties[N_EGL_ICD_PROPERTIES] = { NULL };

static void
srt_egl_icd_class_init (SrtEglIcdClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_egl_icd_get_property;
  object_class->set_property = srt_egl_icd_set_property;
  object_class->constructed = srt_egl_icd_constructed;
  object_class->finalize = srt_egl_icd_finalize;

  egl_icd_properties[EGL_ICD_PROP_ERROR] =
    g_param_spec_boxed ("error", "Error",
                        "GError describing how this ICD failed to load, or NULL",
                        G_TYPE_ERROR,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  egl_icd_properties[EGL_ICD_PROP_ISSUES] =
    g_param_spec_flags ("issues", "Issues", "Problems with this ICD",
                        SRT_TYPE_LOADABLE_ISSUES, SRT_LOADABLE_ISSUES_NONE,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  egl_icd_properties[EGL_ICD_PROP_JSON_PATH] =
    g_param_spec_string ("json-path", "JSON path",
                         "Absolute path to JSON file describing this ICD. "
                         "If examining a sysroot, this path is set as though "
                         "the sysroot was the root directory. "
                         "When constructing the object, a relative path can "
                         "be given: it will be converted to an absolute path.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  egl_icd_properties[EGL_ICD_PROP_LIBRARY_PATH] =
    g_param_spec_string ("library-path", "Library path",
                         "Library implementing this ICD, expressed as a "
                         "basename to be searched for in the default "
                         "library search path (e.g. libEGL_mesa.so.0), "
                         "a relative path containing '/' to be resolved "
                         "relative to #SrtEglIcd:json-path (e.g. "
                         "./libEGL_myvendor.so), or an absolute path "
                         "as though the sysroot (if any) was the root "
                         "(e.g. /opt/EGL/libEGL_myvendor.so)",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  egl_icd_properties[EGL_ICD_PROP_RESOLVED_LIBRARY_PATH] =
    g_param_spec_string ("resolved-library-path", "Resolved library path",
                         "Library implementing this ICD, expressed as a "
                         "basename to be searched for in the default "
                         "library search path (e.g. libEGL_mesa.so.0) "
                         "or an absolute path as though the sysroot (if any) "
                         "was the root "
                         "(e.g. /opt/EGL/libEGL_myvendor.so)",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_EGL_ICD_PROPERTIES,
                                     egl_icd_properties);
}

/**
 * srt_egl_icd_new:
 * @json_path: (transfer none): the absolute path to the JSON file
 * @library_path: (transfer none): the path to the library
 * @issues: problems with this ICD
 *
 * Returns: (transfer full): a new ICD
 */
static SrtEglIcd *
srt_egl_icd_new (const gchar *json_path,
                 const gchar *library_path,
                 SrtLoadableIssues issues)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (library_path != NULL, NULL);

  return g_object_new (SRT_TYPE_EGL_ICD,
                       "json-path", json_path,
                       "library-path", library_path,
                       "issues", issues,
                       NULL);
}

/**
 * srt_egl_icd_new_error:
 * @issues: problems with this ICD
 * @error: (transfer none): Error that occurred when loading the ICD
 *
 * Returns: (transfer full): a new ICD
 */
static SrtEglIcd *
srt_egl_icd_new_error (const gchar *json_path,
                       SrtLoadableIssues issues,
                       const GError *error)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (error != NULL, NULL);

  return g_object_new (SRT_TYPE_EGL_ICD,
                       "error", error,
                       "json-path", json_path,
                       "issues", issues,
                       NULL);
}

/**
 * srt_egl_icd_check_error:
 * @self: The ICD
 * @error: Used to return #SrtEglIcd:error if the ICD description could
 *  not be loaded
 *
 * Check whether we failed to load the JSON describing this EGL ICD.
 * Note that this does not actually `dlopen()` the ICD itself.
 *
 * Returns: %TRUE if the JSON was loaded successfully
 */
gboolean
srt_egl_icd_check_error (SrtEglIcd *self,
                         GError **error)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return srt_loadable_check_error (&self->icd, error);
}

/**
 * srt_egl_icd_get_json_path:
 * @self: The ICD
 *
 * Return the absolute path to the JSON file representing this ICD.
 *
 * Returns: (type filename) (transfer none): #SrtEglIcd:json-path
 */
const gchar *
srt_egl_icd_get_json_path (SrtEglIcd *self)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), NULL);
  return self->icd.json_path;
}

/**
 * srt_egl_icd_get_library_path:
 * @self: The ICD
 *
 * Return the library path for this ICD. It is either an absolute path,
 * a path relative to srt_egl_icd_get_json_path() containing at least one
 * directory separator (slash), or a basename to be loaded from the
 * shared library search path.
 *
 * If the JSON description for this ICD could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type filename) (transfer none) (nullable): #SrtEglIcd:library-path
 */
const gchar *
srt_egl_icd_get_library_path (SrtEglIcd *self)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), NULL);
  return self->icd.library_path;
}

/**
 * srt_egl_icd_get_issues:
 * @self: The ICD
 *
 * Return the problems found when parsing and loading @self.
 *
 * Returns: A bitfield containing problems, or %SRT_LOADABLE_ISSUES_NONE
 *  if no problems were found
 */
SrtLoadableIssues
srt_egl_icd_get_issues (SrtEglIcd *self)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), SRT_LOADABLE_ISSUES_UNKNOWN);
  return self->icd.issues;
}

/*
 * @self: The ICD
 * @is_duplicated: if %TRUE, this ICD is a duplicated of another ICD
 *
 * @self issues is adjusted accordingly to the @is_duplicated value.
 */
static void
_srt_egl_icd_set_is_duplicated (SrtEglIcd *self,
                                gboolean is_duplicated)
{
  g_return_if_fail (SRT_IS_EGL_ICD (self));
  if (is_duplicated)
    self->icd.issues |= SRT_LOADABLE_ISSUES_DUPLICATED;
  else
    self->icd.issues &= ~SRT_LOADABLE_ISSUES_DUPLICATED;
}

/*
 * egl_icd_load_json:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @filename: The filename of the metadata
 * @list: (element-type SrtEglIcd) (inout): Prepend the
 *  resulting #SrtEglIcd to this list
 *
 * Load a single ICD metadata file.
 */
static void
egl_icd_load_json (const char *sysroot,
                   const char *filename,
                   GList **list)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *canon = NULL;
  g_autofree gchar *in_sysroot = NULL;
  g_autofree gchar *library_path = NULL;
  SrtLoadableIssues issues = SRT_LOADABLE_ISSUES_NONE;

  g_return_if_fail (sysroot != NULL);
  g_return_if_fail (list != NULL);

  if (!g_path_is_absolute (filename))
    {
      canon = g_canonicalize_filename (filename, NULL);
      filename = canon;
    }

  in_sysroot = g_build_filename (sysroot, filename, NULL);

  if (load_json (SRT_TYPE_EGL_ICD, in_sysroot,
                 NULL, &library_path, &issues, &error))
    {
      g_assert (library_path != NULL);
      g_assert (error == NULL);
      *list = g_list_prepend (*list,
                              srt_egl_icd_new (filename, library_path, issues));
    }
  else
    {
      g_assert (library_path == NULL);
      g_assert (error != NULL);
      *list = g_list_prepend (*list,
                              srt_egl_icd_new_error (filename, issues, error));
    }
}

/**
 * srt_egl_icd_resolve_library_path:
 * @self: An ICD
 *
 * Return the path that can be passed to `dlopen()` for this ICD.
 *
 * If srt_egl_icd_get_library_path() is a relative path, return the
 * absolute path that is the result of interpreting it relative to
 * an appropriate location (the exact interpretation is subject to change,
 * depending on upstream decisions).
 *
 * Otherwise return a copy of srt_egl_icd_get_library_path().
 *
 * The result is either the basename of a shared library (to be found
 * relative to some directory listed in `$LD_LIBRARY_PATH`, `/etc/ld.so.conf`,
 * `/etc/ld.so.conf.d` or the hard-coded library search path), or an
 * absolute path.
 *
 * Returns: (transfer full) (type filename) (nullable): A copy
 *  of #SrtEglIcd:resolved-library-path. Free with g_free().
 */
gchar *
srt_egl_icd_resolve_library_path (SrtEglIcd *self)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), NULL);
  return srt_loadable_resolve_library_path (&self->icd);
}

/**
 * srt_egl_icd_new_replace_library_path:
 * @self: An ICD
 * @path: (type filename) (transfer none): A path
 *
 * Return a copy of @self with the srt_egl_icd_get_library_path()
 * changed to @path. For example, this is useful when setting up a
 * container where the underlying shared object will be made available
 * at a different absolute path.
 *
 * If @self is in an error state, this returns a new reference to @self.
 *
 * Returns: (transfer full): A new reference to a #SrtEglIcd. Free with
 *  g_object_unref().
 */
SrtEglIcd *
srt_egl_icd_new_replace_library_path (SrtEglIcd *self,
                                      const char *path)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), NULL);

  if (self->icd.error != NULL)
    return g_object_ref (self);

  return srt_egl_icd_new (self->icd.json_path, path, self->icd.issues);
}

/**
 * srt_egl_icd_write_to_file:
 * @self: An ICD
 * @path: (type filename): A filename
 * @error: Used to describe the error on failure
 *
 * Serialize @self to the given JSON file.
 *
 * Returns: %TRUE on success
 */
gboolean
srt_egl_icd_write_to_file (SrtEglIcd *self,
                           const char *path,
                           GError **error)
{
  g_return_val_if_fail (SRT_IS_EGL_ICD (self), FALSE);
  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return srt_loadable_write_to_file (&self->icd, path, SRT_TYPE_EGL_ICD, error);
}

static void
egl_icd_load_json_cb (const char *sysroot,
                      const char *filename,
                      void *user_data)
{
  egl_icd_load_json (sysroot, filename, user_data);
}

#define EGL_VENDOR_SUFFIX "glvnd/egl_vendor.d"

/*
 * Return the ${sysconfdir} that we assume GLVND has.
 *
 * steam-runtime-tools is typically installed in the Steam Runtime,
 * which is not part of the operating system, so we cannot assume
 * that our own prefix is the same as GLVND. Assume a conventional
 * OS-wide installation of GLVND.
 */
static const char *
get_glvnd_sysconfdir (void)
{
  return "/etc";
}

/*
 * Return the ${datadir} that we assume GLVND has. See above.
 */
static const char *
get_glvnd_datadir (void)
{
  return "/usr/share";
}

static void _srt_vulkan_icd_set_is_duplicated (SrtVulkanIcd *self,
                                               gboolean is_duplicated);
static void _srt_vulkan_layer_set_is_duplicated (SrtVulkanLayer *self,
                                                 gboolean is_duplicated);

static void
_update_duplicated_value (GType which,
                          GHashTable *loadable_seen,
                          const gchar *key,
                          void *loadable_to_check)
{
  if (key == NULL)
    return;

  if (g_hash_table_contains (loadable_seen, key))
    {
      if (which == SRT_TYPE_VULKAN_ICD)
        {
          SrtVulkanIcd *vulkan_icd = g_hash_table_lookup (loadable_seen, key);
          _srt_vulkan_icd_set_is_duplicated (vulkan_icd, TRUE);
          _srt_vulkan_icd_set_is_duplicated (loadable_to_check, TRUE);
        }
      else if (which == SRT_TYPE_EGL_ICD)
        {
          SrtEglIcd *egl_icd = g_hash_table_lookup (loadable_seen, key);
          _srt_egl_icd_set_is_duplicated (egl_icd, TRUE);
          _srt_egl_icd_set_is_duplicated (loadable_to_check, TRUE);
        }
      else if (which == SRT_TYPE_VULKAN_LAYER)
        {
          SrtVulkanLayer *vulkan_layer = g_hash_table_lookup (loadable_seen, key);
          _srt_vulkan_layer_set_is_duplicated (vulkan_layer, TRUE);
          _srt_vulkan_layer_set_is_duplicated (loadable_to_check, TRUE);
        }
      else
        {
          g_return_if_reached ();
        }
    }
  else
    {
      g_hash_table_replace (loadable_seen,
                            g_strdup (key),
                            loadable_to_check);
    }
}

/*
 * Use 'inspect-library' to get the absolute path of @library_path,
 * resolving also its eventual symbolic links.
 */
static gchar *
_get_library_canonical_path (gchar **envp,
                             const char *helpers_path,
                             const char *multiarch,
                             const gchar *library_path)
{
  g_autoptr(SrtLibrary) library = NULL;
  _srt_check_library_presence (helpers_path, library_path, multiarch, NULL,
                               NULL, envp,
                               SRT_LIBRARY_SYMBOLS_FORMAT_PLAIN, &library);

  /* Use realpath() because the path might still be a symbolic link or it can
   * contains ./ or ../
   * The absolute path is gathered using 'inspect-library', so we don't have
   * to worry about still having special tokens, like ${LIB}, in the path. */
  return realpath (srt_library_get_absolute_path (library), NULL);
}

/*
 * @helpers_path: (nullable): An optional path to find "inspect-library"
 *  helper, PATH is used if %NULL
 * @loadable: (inout) (element-type SrtVulkanLayer):
 *
 * Iterate the provided @loadable list and update their "issues" property
 * to include the SRT_LOADABLE_ISSUES_DUPLICATED bit if they are duplicated.
 * Two ICDs are considered to be duplicated if they have the same absolute
 * library path.
 * Two Vulkan layers are considered to be duplicated if they have the same
 * name and absolute library path.
 */
static void
_srt_loadable_flag_duplicates (GType which,
                               gchar **envp,
                               const char *helpers_path,
                               const char * const *multiarch_tuples,
                               GList *loadable)
{
  g_autoptr(GHashTable) loadable_seen = NULL;
  gsize i;
  GList *l;

  g_return_if_fail (which == SRT_TYPE_VULKAN_ICD
                    || which == SRT_TYPE_EGL_ICD
                    || which == SRT_TYPE_VULKAN_LAYER);

  loadable_seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  for (l = loadable; l != NULL; l = l->next)
    {
      g_autofree gchar *resolved_path = NULL;
      const gchar *name = NULL;

      if (which == SRT_TYPE_VULKAN_ICD || which == SRT_TYPE_EGL_ICD)
        {
          if (which == SRT_TYPE_VULKAN_ICD)
            resolved_path = srt_vulkan_icd_resolve_library_path (l->data);
          else
            resolved_path = srt_egl_icd_resolve_library_path (l->data);

          if (resolved_path == NULL)
            continue;

          if (multiarch_tuples == NULL)
            {
              /* If we don't have the multiarch_tuples just use the
               * resolved_path as is */
              _update_duplicated_value (which, loadable_seen, resolved_path, l->data);
            }
          else
            {
              for (i = 0; multiarch_tuples[i] != NULL; i++)
                {
                  g_autofree gchar *canonical_path = NULL;
                  canonical_path = _get_library_canonical_path (envp, helpers_path,
                                                                multiarch_tuples[i],
                                                                resolved_path);

                  if (canonical_path == NULL)
                    {
                      /* Either the library is of a different ELF class or it is missing */
                      g_debug ("Unable to get the absolute path of \"%s\" via inspect-library",
                               resolved_path);
                      continue;
                    }

                  _update_duplicated_value (which, loadable_seen, canonical_path, l->data);
                }
            }
        }
      else if (which == SRT_TYPE_VULKAN_LAYER)
        {
          resolved_path = srt_vulkan_layer_resolve_library_path (l->data);
          name = srt_vulkan_layer_get_name (l->data);

          if (resolved_path == NULL && name == NULL)
            continue;

          if (multiarch_tuples == NULL || resolved_path == NULL)
            {
              g_autofree gchar *hash_key = NULL;
              /* We need a key for the hashtable that includes the name and
               * the path in it. We use '//' as a separator between the two
               * values, because we don't expect to have '//' in the
               * path, nor in the name. In the very unlikely event where
               * a collision happens, we will just consider two layers
               * as duplicated when in reality they weren't. */
              hash_key = g_strdup_printf ("%s//%s", name, resolved_path);
              _update_duplicated_value (which, loadable_seen, hash_key, l->data);
            }
          else
            {
              for (i = 0; multiarch_tuples[i] != NULL; i++)
                {
                  g_autofree gchar *canonical_path = NULL;
                  g_autofree gchar *hash_key = NULL;
                  canonical_path = _get_library_canonical_path (envp, helpers_path,
                                                                multiarch_tuples[i],
                                                                resolved_path);

                  if (canonical_path == NULL)
                    {
                      /* Either the library is of a different ELF class or it is missing */
                      g_debug ("Unable to get the absolute path of \"%s\" via inspect-library",
                               resolved_path);
                      continue;
                    }

                  /* We need a key for the hashtable that includes the name and
                   * the canonical path in it. We use '//' as a separator
                   * between the two values, because we don't expect to have
                   * '//' in the path, nor in the name. In the very unlikely
                   * event where a collision happens, we will just consider
                   * two layers as duplicated when in reality they weren't. */
                  hash_key = g_strdup_printf ("%s//%s", name, canonical_path);
                  _update_duplicated_value (which, loadable_seen, hash_key, l->data);
                }
            }
        }
      else
        {
          g_return_if_reached ();
        }
    }
}

/*
 * _srt_load_egl_icds:
 * @helpers_path: (nullable): An optional path to find "inspect-library"
 *  helper, PATH is used if %NULL
 * @sysroot: (not nullable): The root directory, usually `/`
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ`
 *  was this array
 * @multiarch_tuples: (nullable): If not %NULL, and a Flatpak environment
 *  is detected, assume a freedesktop-sdk-based runtime and look for
 *  GL extensions for these multiarch tuples. Also if not %NULL, duplicated
 *  EGL ICDs are searched by their absolute path, obtained using
 *  "inspect-library" in the provided multiarch tuples, instead of just their
 *  resolved library path.
 * @check_flags: Whether to check for problems
 *
 * Implementation of srt_system_info_list_egl_icds().
 *
 * Returns: (transfer full) (element-type SrtEglIcd): A list of ICDs,
 *  most-important first
 */
GList *
_srt_load_egl_icds (const char *helpers_path,
                    const char *sysroot,
                    gchar **envp,
                    const char * const *multiarch_tuples,
                    SrtCheckFlags check_flags)
{
  const gchar *value;
  gsize i;
  /* To avoid O(n**2) performance, we build this list in reverse order,
   * then reverse it at the end. */
  GList *ret = NULL;

  g_return_val_if_fail (sysroot != NULL, NULL);
  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail (envp != NULL, NULL);

  /* See
   * https://github.com/NVIDIA/libglvnd/blob/master/src/EGL/icd_enumeration.md
   * for details of the search order. */

  value = g_environ_getenv (envp, "__EGL_VENDOR_LIBRARY_FILENAMES");

  if (value != NULL)
    {
      g_auto(GStrv) filenames = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);

      for (i = 0; filenames[i] != NULL; i++)
        egl_icd_load_json (sysroot, filenames[i], &ret);
    }
  else
    {
      g_autofree gchar *flatpak_info = NULL;

      value = g_environ_getenv (envp, "__EGL_VENDOR_LIBRARY_DIRS");

      flatpak_info = g_build_filename (sysroot, ".flatpak-info", NULL);

      if (value != NULL)
        {
          g_auto(GStrv) dirs = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);

          load_json_dirs (sysroot, dirs, NULL, _srt_indirect_strcmp0,
                          egl_icd_load_json_cb, &ret);
        }
      else if (g_file_test (flatpak_info, G_FILE_TEST_EXISTS)
               && multiarch_tuples != NULL)
        {
          g_debug ("Flatpak detected: assuming freedesktop-based runtime");

          for (i = 0; multiarch_tuples[i] != NULL; i++)
            {
              g_autofree gchar *tmp = NULL;

              /* freedesktop-sdk reconfigures the EGL loader to look here. */
              tmp = g_strdup_printf ("/usr/lib/%s/GL/" EGL_VENDOR_SUFFIX,
                                     multiarch_tuples[i]);
              load_json_dir (sysroot, tmp, NULL, _srt_indirect_strcmp0,
                             egl_icd_load_json_cb, &ret);
            }
        }
      else
        {
          load_json_dir (sysroot, get_glvnd_sysconfdir (), EGL_VENDOR_SUFFIX,
                         _srt_indirect_strcmp0, egl_icd_load_json_cb, &ret);
          load_json_dir (sysroot, get_glvnd_datadir (), EGL_VENDOR_SUFFIX,
                         _srt_indirect_strcmp0, egl_icd_load_json_cb, &ret);
        }
    }

  if (!(check_flags & SRT_CHECK_FLAGS_SKIP_SLOW_CHECKS))
    _srt_loadable_flag_duplicates (SRT_TYPE_EGL_ICD, envp, helpers_path,
                                   multiarch_tuples, ret);

  return g_list_reverse (ret);
}

static GList *
get_driver_loadables_from_json_report (JsonObject *json_obj,
                                       GType which,
                                       gboolean explicit);

/**
 * _srt_get_egl_from_json_report:
 * @json_obj: (not nullable): A JSON Object used to search for "egl" property
 *
 * Returns: A list of #SrtEglIcd that have been found, or %NULL if none
 *  has been found.
 */
GList *
_srt_get_egl_from_json_report (JsonObject *json_obj)
{
  return get_driver_loadables_from_json_report (json_obj, SRT_TYPE_EGL_ICD, FALSE);
}

/**
 * SrtVulkanIcd:
 *
 * Opaque object representing a Vulkan ICD.
 */

struct _SrtVulkanIcd
{
  /*< private >*/
  GObject parent;
  SrtLoadable icd;
};

struct _SrtVulkanIcdClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum
{
  VULKAN_ICD_PROP_0,
  VULKAN_ICD_PROP_API_VERSION,
  VULKAN_ICD_PROP_ERROR,
  VULKAN_ICD_PROP_ISSUES,
  VULKAN_ICD_PROP_JSON_PATH,
  VULKAN_ICD_PROP_LIBRARY_PATH,
  VULKAN_ICD_PROP_RESOLVED_LIBRARY_PATH,
  N_VULKAN_ICD_PROPERTIES
};

G_DEFINE_TYPE (SrtVulkanIcd, srt_vulkan_icd, G_TYPE_OBJECT)

static void
srt_vulkan_icd_init (SrtVulkanIcd *self)
{
}

static void
srt_vulkan_icd_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  SrtVulkanIcd *self = SRT_VULKAN_ICD (object);

  switch (prop_id)
    {
      case VULKAN_ICD_PROP_API_VERSION:
        g_value_set_string (value, self->icd.api_version);
        break;

      case VULKAN_ICD_PROP_ERROR:
        g_value_set_boxed (value, self->icd.error);
        break;

      case VULKAN_ICD_PROP_ISSUES:
        g_value_set_flags (value, self->icd.issues);
        break;

      case VULKAN_ICD_PROP_JSON_PATH:
        g_value_set_string (value, self->icd.json_path);
        break;

      case VULKAN_ICD_PROP_LIBRARY_PATH:
        g_value_set_string (value, self->icd.library_path);
        break;

      case VULKAN_ICD_PROP_RESOLVED_LIBRARY_PATH:
        g_value_take_string (value, srt_vulkan_icd_resolve_library_path (self));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_vulkan_icd_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  SrtVulkanIcd *self = SRT_VULKAN_ICD (object);
  const char *tmp;

  switch (prop_id)
    {
      case VULKAN_ICD_PROP_API_VERSION:
        g_return_if_fail (self->icd.api_version == NULL);
        self->icd.api_version = g_value_dup_string (value);
        break;

      case VULKAN_ICD_PROP_ERROR:
        g_return_if_fail (self->icd.error == NULL);
        self->icd.error = g_value_dup_boxed (value);
        break;

      case VULKAN_ICD_PROP_ISSUES:
        g_return_if_fail (self->icd.issues == 0);
        self->icd.issues = g_value_get_flags (value);
        break;

      case VULKAN_ICD_PROP_JSON_PATH:
        g_return_if_fail (self->icd.json_path == NULL);
        tmp = g_value_get_string (value);
        self->icd.json_path = g_canonicalize_filename (tmp, NULL);
        break;

      case VULKAN_ICD_PROP_LIBRARY_PATH:
        g_return_if_fail (self->icd.library_path == NULL);
        self->icd.library_path = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_vulkan_icd_constructed (GObject *object)
{
  SrtVulkanIcd *self = SRT_VULKAN_ICD (object);

  g_return_if_fail (self->icd.json_path != NULL);
  g_return_if_fail (g_path_is_absolute (self->icd.json_path));

  if (self->icd.error != NULL)
    {
      g_return_if_fail (self->icd.api_version == NULL);
      g_return_if_fail (self->icd.library_path == NULL);
    }
  else
    {
      g_return_if_fail (self->icd.api_version != NULL);
      g_return_if_fail (self->icd.library_path != NULL);
    }
}

static void
srt_vulkan_icd_finalize (GObject *object)
{
  SrtVulkanIcd *self = SRT_VULKAN_ICD (object);

  srt_loadable_clear (&self->icd);

  G_OBJECT_CLASS (srt_vulkan_icd_parent_class)->finalize (object);
}

static GParamSpec *vulkan_icd_properties[N_VULKAN_ICD_PROPERTIES] = { NULL };

static void
srt_vulkan_icd_class_init (SrtVulkanIcdClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_vulkan_icd_get_property;
  object_class->set_property = srt_vulkan_icd_set_property;
  object_class->constructed = srt_vulkan_icd_constructed;
  object_class->finalize = srt_vulkan_icd_finalize;

  vulkan_icd_properties[VULKAN_ICD_PROP_API_VERSION] =
    g_param_spec_string ("api-version", "API version",
                         "Vulkan API version implemented by this ICD",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_icd_properties[VULKAN_ICD_PROP_ERROR] =
    g_param_spec_boxed ("error", "Error",
                        "GError describing how this ICD failed to load, or NULL",
                        G_TYPE_ERROR,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  vulkan_icd_properties[VULKAN_ICD_PROP_ISSUES] =
    g_param_spec_flags ("issues", "Issues", "Problems with this ICD",
                        SRT_TYPE_LOADABLE_ISSUES, SRT_LOADABLE_ISSUES_NONE,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  vulkan_icd_properties[VULKAN_ICD_PROP_JSON_PATH] =
    g_param_spec_string ("json-path", "JSON path",
                         "Absolute path to JSON file describing this ICD. "
                         "If examining a sysroot, this path is set as though "
                         "the sysroot was the root directory. "
                         "When constructing the object, a relative path can "
                         "be given: it will be converted to an absolute path.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_icd_properties[VULKAN_ICD_PROP_LIBRARY_PATH] =
    g_param_spec_string ("library-path", "Library path",
                         "Library implementing this ICD, expressed as a "
                         "basename to be searched for in the default "
                         "library search path (e.g. libvulkan_myvendor.so), "
                         "a relative path containing '/' to be resolved "
                         "relative to #SrtVulkanIcd:json-path (e.g. "
                         "./libvulkan_myvendor.so), or an absolute path "
                         "as though the sysroot (if any) was the root "
                         "(e.g. /opt/vulkan/libvulkan_myvendor.so)",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_icd_properties[VULKAN_ICD_PROP_RESOLVED_LIBRARY_PATH] =
    g_param_spec_string ("resolved-library-path", "Resolved library path",
                         "Library implementing this ICD, expressed as a "
                         "basename to be searched for in the default "
                         "library search path (e.g. libvulkan_myvendor.so) "
                         "or an absolute path as though the sysroot, if any, "
                         "was the root "
                         "(e.g. /opt/vulkan/libvulkan_myvendor.so)",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_VULKAN_ICD_PROPERTIES,
                                     vulkan_icd_properties);
}

/**
 * srt_vulkan_icd_new:
 * @json_path: (transfer none): the absolute path to the JSON file
 * @api_version: (transfer none): the API version
 * @library_path: (transfer none): the path to the library
 * @issues: problems with this ICD
 *
 * Returns: (transfer full): a new ICD
 */
static SrtVulkanIcd *
srt_vulkan_icd_new (const gchar *json_path,
                    const gchar *api_version,
                    const gchar *library_path,
                    SrtLoadableIssues issues)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (api_version != NULL, NULL);
  g_return_val_if_fail (library_path != NULL, NULL);

  return g_object_new (SRT_TYPE_VULKAN_ICD,
                       "api-version", api_version,
                       "json-path", json_path,
                       "library-path", library_path,
                       "issues", issues,
                       NULL);
}

/**
 * srt_vulkan_icd_new_error:
 * @issues: problems with this ICD
 * @error: (transfer none): Error that occurred when loading the ICD
 *
 * Returns: (transfer full): a new ICD
 */
static SrtVulkanIcd *
srt_vulkan_icd_new_error (const gchar *json_path,
                          SrtLoadableIssues issues,
                          const GError *error)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (error != NULL, NULL);

  return g_object_new (SRT_TYPE_VULKAN_ICD,
                       "error", error,
                       "json-path", json_path,
                       "issues", issues,
                       NULL);
}

/**
 * srt_vulkan_icd_check_error:
 * @self: The ICD
 * @error: Used to return details if the ICD description could not be loaded
 *
 * Check whether we failed to load the JSON describing this Vulkan ICD.
 * Note that this does not actually `dlopen()` the ICD itself.
 *
 * Returns: %TRUE if the JSON was loaded successfully
 */
gboolean
srt_vulkan_icd_check_error (SrtVulkanIcd *self,
                            GError **error)
{
  g_return_val_if_fail (SRT_IS_VULKAN_ICD (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return srt_loadable_check_error (&self->icd, error);
}

/**
 * srt_vulkan_icd_get_api_version:
 * @self: The ICD
 *
 * Return the Vulkan API version of this ICD.
 *
 * If the JSON description for this ICD could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type utf8) (transfer none) (nullable): The API version as a string
 */
const gchar *
srt_vulkan_icd_get_api_version (SrtVulkanIcd *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_ICD (self), NULL);
  return self->icd.api_version;
}

/**
 * srt_vulkan_icd_get_json_path:
 * @self: The ICD
 *
 * Return the absolute path to the JSON file representing this ICD.
 *
 * Returns: (type filename) (transfer none): #SrtVulkanIcd:json-path
 */
const gchar *
srt_vulkan_icd_get_json_path (SrtVulkanIcd *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_ICD (self), NULL);
  return self->icd.json_path;
}

/**
 * srt_vulkan_icd_get_library_path:
 * @self: The ICD
 *
 * Return the library path for this ICD. It is either an absolute path,
 * a path relative to srt_vulkan_icd_get_json_path() containing at least one
 * directory separator (slash), or a basename to be loaded from the
 * shared library search path.
 *
 * If the JSON description for this ICD could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type filename) (transfer none) (nullable): #SrtVulkanIcd:library-path
 */
const gchar *
srt_vulkan_icd_get_library_path (SrtVulkanIcd *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_ICD (self), NULL);
  return self->icd.library_path;
}

/**
 * srt_vulkan_icd_get_issues:
 * @self: The ICD
 *
 * Return the problems found when parsing and loading @self.
 *
 * Returns: A bitfield containing problems, or %SRT_LOADABLE_ISSUES_NONE
 *  if no problems were found
 */
SrtLoadableIssues
srt_vulkan_icd_get_issues (SrtVulkanIcd *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_ICD (self), SRT_LOADABLE_ISSUES_UNKNOWN);
  return self->icd.issues;
}

/*
 * @self: The ICD
 * @is_duplicated: if %TRUE, this ICD is a duplicated of another ICD
 *
 * @self issues is adjusted accordingly to the @is_duplicated value.
 */
static void
_srt_vulkan_icd_set_is_duplicated (SrtVulkanIcd *self,
                                   gboolean is_duplicated)
{
  g_return_if_fail (SRT_IS_VULKAN_ICD (self));
  if (is_duplicated)
    self->icd.issues |= SRT_LOADABLE_ISSUES_DUPLICATED;
  else
    self->icd.issues &= ~SRT_LOADABLE_ISSUES_DUPLICATED;
}

/**
 * srt_vulkan_icd_resolve_library_path:
 * @self: An ICD
 *
 * Return the path that can be passed to `dlopen()` for this ICD.
 *
 * If srt_vulkan_icd_get_library_path() is a relative path, return the
 * absolute path that is the result of interpreting it relative to
 * srt_vulkan_icd_get_json_path(). Otherwise return a copy of
 * srt_vulkan_icd_get_library_path().
 *
 * The result is either the basename of a shared library (to be found
 * relative to some directory listed in `$LD_LIBRARY_PATH`, `/etc/ld.so.conf`,
 * `/etc/ld.so.conf.d` or the hard-coded library search path), or an
 * absolute path.
 *
 * Returns: (transfer full) (type filename) (nullable): A copy
 *  of #SrtVulkanIcd:resolved-library-path. Free with g_free().
 */
gchar *
srt_vulkan_icd_resolve_library_path (SrtVulkanIcd *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_ICD (self), NULL);
  return srt_loadable_resolve_library_path (&self->icd);
}

/**
 * srt_vulkan_icd_write_to_file:
 * @self: An ICD
 * @path: (type filename): A filename
 * @error: Used to describe the error on failure
 *
 * Serialize @self to the given JSON file.
 *
 * Returns: %TRUE on success
 */
gboolean
srt_vulkan_icd_write_to_file (SrtVulkanIcd *self,
                              const char *path,
                              GError **error)
{
  g_return_val_if_fail (SRT_IS_VULKAN_ICD (self), FALSE);
  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return srt_loadable_write_to_file (&self->icd, path, SRT_TYPE_VULKAN_ICD, error);
}

/**
 * srt_vulkan_icd_new_replace_library_path:
 * @self: An ICD
 * @path: (type filename) (transfer none): A path
 *
 * Return a copy of @self with the srt_vulkan_icd_get_library_path()
 * changed to @path. For example, this is useful when setting up a
 * container where the underlying shared object will be made available
 * at a different absolute path.
 *
 * If @self is in an error state, this returns a new reference to @self.
 *
 * Note that @self issues are copied to the new #SrtVulkanIcd copy, including
 * the eventual %SRT_LOADABLE_ISSUES_DUPLICATED.
 *
 * Returns: (transfer full): A new reference to a #SrtVulkanIcd. Free with
 *  g_object_unref().
 */
SrtVulkanIcd *
srt_vulkan_icd_new_replace_library_path (SrtVulkanIcd *self,
                                         const char *path)
{
  g_return_val_if_fail (SRT_IS_VULKAN_ICD (self), NULL);

  if (self->icd.error != NULL)
    return g_object_ref (self);

  return srt_vulkan_icd_new (self->icd.json_path,
                             self->icd.api_version,
                             path,
                             self->icd.issues);
}

/*
 * vulkan_icd_load_json:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @filename: The filename of the metadata
 * @list: (element-type SrtVulkanIcd) (inout): Prepend the
 *  resulting #SrtVulkanIcd to this list
 *
 * Load a single ICD metadata file.
 */
static void
vulkan_icd_load_json (const char *sysroot,
                      const char *filename,
                      GList **list)
{
  g_autoptr(GError) error = NULL;
  g_autofree gchar *canon = NULL;
  g_autofree gchar *in_sysroot = NULL;
  g_autofree gchar *api_version = NULL;
  g_autofree gchar *library_path = NULL;
  SrtLoadableIssues issues = SRT_LOADABLE_ISSUES_NONE;

  g_return_if_fail (list != NULL);

  if (!g_path_is_absolute (filename))
    {
      canon = g_canonicalize_filename (filename, NULL);
      filename = canon;
    }

  in_sysroot = g_build_filename (sysroot, filename, NULL);

  if (load_json (SRT_TYPE_VULKAN_ICD, in_sysroot,
                 &api_version, &library_path, &issues, &error))
    {
      g_assert (api_version != NULL);
      g_assert (library_path != NULL);
      g_assert (error == NULL);
      *list = g_list_prepend (*list,
                              srt_vulkan_icd_new (filename,
                                                  api_version,
                                                  library_path,
                                                  issues));
    }
  else
    {
      g_assert (api_version == NULL);
      g_assert (library_path == NULL);
      g_assert (error != NULL);
      *list = g_list_prepend (*list,
                              srt_vulkan_icd_new_error (filename, issues, error));
    }
}

static void
vulkan_icd_load_json_cb (const char *sysroot,
                         const char *filename,
                         void *user_data)
{
  vulkan_icd_load_json (sysroot, filename, user_data);
}

/*
 * Return the ${sysconfdir} that we assume the Vulkan loader has.
 * See get_glvnd_sysconfdir().
 */
static const char *
get_vulkan_sysconfdir (void)
{
  return "/etc";
}

/* Reference:
 * https://github.com/KhronosGroup/Vulkan-Loader/blob/sdk-1.2.198.1/docs/LoaderLayerInterface.md#linux-layer-discovery
 * https://github.com/KhronosGroup/Vulkan-Loader/blob/sdk-1.2.198.1/docs/LoaderDriverInterface.md#driver-discovery-on-linux
 *
 * ICDs (drivers) and loaders are currently exactly the same, except for
 * the suffix used. If they diverge in future, this function will need more
 * parameters. */
gchar **
_srt_graphics_get_vulkan_search_paths (const char *sysroot,
                                       gchar **envp,
                                       const char * const *multiarch_tuples,
                                       const char *suffix)
{
  GPtrArray *search_paths = g_ptr_array_new ();
  g_auto(GStrv) dirs = NULL;
  g_autofree gchar *flatpak_info = NULL;
  const char *home;
  const gchar *value;
  gsize i;

  home = g_environ_getenv (envp, "HOME");

  if (home == NULL)
    home = g_get_home_dir ();

  /* 1. $XDG_CONFIG_HOME or $HOME/.config (since 1.2.198) */
  value = g_environ_getenv (envp, "XDG_CONFIG_HOME");

  if (value != NULL)
    g_ptr_array_add (search_paths, g_build_filename (value, suffix, NULL));
  else if (home != NULL)
    g_ptr_array_add (search_paths, g_build_filename (home, ".config", suffix, NULL));

  /* 1a. $XDG_CONFIG_DIRS or /etc/xdg */
  value = g_environ_getenv (envp, "XDG_CONFIG_DIRS");

  /* Constant and non-configurable fallback, as per
   * https://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html */
  if (value == NULL)
    value = "/etc/xdg";

  dirs = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);
  for (i = 0; dirs[i] != NULL; i++)
    g_ptr_array_add (search_paths, g_build_filename (dirs[i], suffix, NULL));

  g_clear_pointer (&dirs, g_strfreev);

  /* 2. SYSCONFDIR */
  value = get_vulkan_sysconfdir ();
  g_ptr_array_add (search_paths, g_build_filename (value, suffix, NULL));

  /* 3. EXTRASYSCONFDIR.
   * This is hard-coded in the reference loader: if its own sysconfdir
   * is not /etc, it searches /etc afterwards. (In practice this
   * won't trigger at the moment, because we assume the Vulkan
   * loader's sysconfdir *is* /etc.) */
  if (g_strcmp0 (value, "/etc") != 0)
    g_ptr_array_add (search_paths, g_build_filename ("/etc", suffix, NULL));

  flatpak_info = g_build_filename (sysroot, ".flatpak-info", NULL);

  /* freedesktop-sdk patches the Vulkan loader to look here for ICDs,
   * after EXTRASYSCONFDIR but before XDG_DATA_HOME.
   * https://gitlab.com/freedesktop-sdk/freedesktop-sdk/-/blob/master/patches/vulkan/vulkan-libdir-path.patch */
  if (g_file_test (flatpak_info, G_FILE_TEST_EXISTS))
    {
      g_debug ("Flatpak detected: assuming freedesktop-based runtime");

      for (i = 0; multiarch_tuples != NULL && multiarch_tuples[i] != NULL; i++)
        {
          /* GL extensions */
          g_ptr_array_add (search_paths, g_build_filename ("/usr/lib",
                                                           multiarch_tuples[i],
                                                           "GL",
                                                           suffix,
                                                           NULL));
          /* Built-in Mesa stack */
          g_ptr_array_add (search_paths, g_build_filename ("/usr/lib",
                                                           multiarch_tuples[i],
                                                           suffix,
                                                           NULL));
        }

      /* https://gitlab.com/freedesktop-sdk/freedesktop-sdk/-/merge_requests/3398 */
      g_ptr_array_add (search_paths, g_build_filename ("/usr/lib/extensions/vulkan/share",
                                                       suffix,
                                                       NULL));
    }

  /* 4. $XDG_DATA_HOME or $HOME/.local/share.
   * In previous versions of steam-runtime-tools, we misinterpreted the
   * Vulkan-Loader code and thought it was loading $XDG_DATA_HOME *and*
   * $HOME/.local/share (inconsistent with the basedir spec). This was
   * incorrect: it only used $HOME/.local/share as a fallback, consistent
   * with the basedir spec.
   *
   * Unfortunately, Steam currently relies on layers in $HOME/.local/share
   * being found, even if $XDG_DATA_HOME is set to something else:
   * https://github.com/ValveSoftware/steam-for-linux/issues/8337
   * So for now we continue to follow the misinterpretation, to make the
   * Steam Overlay more likely to work in pressure-vessel containers. */
  value = g_environ_getenv (envp, "XDG_DATA_HOME");

  if (value != NULL)
    g_ptr_array_add (search_paths, g_build_filename (value, suffix, NULL));

  /* When steam-for-linux#8337 has been fixed, this should become an 'else if' */
  if (home != NULL)
    g_ptr_array_add (search_paths,
                     g_build_filename (home, ".local", "share", suffix, NULL));

  /* 5. $XDG_DATA_DIRS or /usr/local/share:/usr/share */
  value = g_environ_getenv (envp, "XDG_DATA_DIRS");

  /* Constant and non-configurable fallback, as per
   * https://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html */
  if (value == NULL)
    value = "/usr/local/share" G_SEARCHPATH_SEPARATOR_S "/usr/share";

  dirs = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);
  for (i = 0; dirs[i] != NULL; i++)
    g_ptr_array_add (search_paths, g_build_filename (dirs[i], suffix, NULL));

  g_ptr_array_add (search_paths, NULL);

  return (GStrv) g_ptr_array_free (search_paths, FALSE);
}

/*
 * _srt_load_vulkan_icds:
 * @helpers_path: (nullable): An optional path to find "inspect-library"
 *  helper, PATH is used if %NULL
 * @sysroot: (not nullable): The root directory, usually `/`
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ` was this
 *  array
 * @multiarch_tuples: (nullable): If not %NULL, and a Flatpak environment
 *  is detected, assume a freedesktop-sdk-based runtime and look for
 *  GL extensions for these multiarch tuples
 * @multiarch_tuples: (nullable): If not %NULL, and a Flatpak environment
 *  is detected, assume a freedesktop-sdk-based runtime and look for
 *  GL extensions for these multiarch tuples. Also if not %NULL, duplicated
 *  Vulkan ICDs are searched by their absolute path, obtained using
 *  "inspect-library" in the provided multiarch tuples, instead of just their
 *  resolved library path.
 * @check_flags: Whether to check for problems
 *
 * Implementation of srt_system_info_list_vulkan_icds().
 *
 * Returns: (transfer full) (element-type SrtVulkanIcd): A list of ICDs,
 *  most-important first
 */
GList *
_srt_load_vulkan_icds (const char *helpers_path,
                       const char *sysroot,
                       gchar **envp,
                       const char * const *multiarch_tuples,
                       SrtCheckFlags check_flags)
{
  const gchar *value;
  gsize i;
  /* To avoid O(n**2) performance, we build this list in reverse order,
   * then reverse it at the end. */
  GList *ret = NULL;

  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail (envp != NULL, NULL);

  /* Reference:
   * https://github.com/KhronosGroup/Vulkan-Loader/blob/sdk-1.2.198.1/docs/LoaderDriverInterface.md#overriding-the-default-driver-discovery
   */
  value = g_environ_getenv (envp, "VK_ICD_FILENAMES");

  if (value != NULL)
    {
      gchar **filenames = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);

      for (i = 0; filenames[i] != NULL; i++)
        vulkan_icd_load_json (sysroot, filenames[i], &ret);

      g_strfreev (filenames);
    }
  else
    {
      g_auto(GStrv) search_paths = _srt_graphics_get_vulkan_search_paths (sysroot,
                                                                          envp,
                                                                          multiarch_tuples,
                                                                          _SRT_GRAPHICS_VULKAN_ICD_SUFFIX);
      load_json_dirs (sysroot, search_paths, NULL, READDIR_ORDER,
                      vulkan_icd_load_json_cb, &ret);
    }

  if (!(check_flags & SRT_CHECK_FLAGS_SKIP_SLOW_CHECKS))
    _srt_loadable_flag_duplicates (SRT_TYPE_VULKAN_ICD, envp, helpers_path,
                                   multiarch_tuples, ret);

  return g_list_reverse (ret);
}

/**
 * _srt_get_vulkan_from_json_report:
 * @json_obj: (not nullable): A JSON Object used to search for "vulkan" property
 *
 * Returns: A list of #SrtVulkanIcd that have been found, or %NULL if none
 *  has been found.
 */
GList *
_srt_get_vulkan_from_json_report (JsonObject *json_obj)
{
  return get_driver_loadables_from_json_report (json_obj, SRT_TYPE_VULKAN_ICD, FALSE);
}

/**
 * SrtVulkanLayer:
 *
 * Opaque object representing a Vulkan layer.
 */

struct _SrtVulkanLayer
{
  /*< private >*/
  GObject parent;
  SrtLoadable layer;
};

struct _SrtVulkanLayerClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum
{
  VULKAN_LAYER_PROP_0,
  VULKAN_LAYER_PROP_ERROR,
  VULKAN_LAYER_PROP_ISSUES,
  VULKAN_LAYER_PROP_JSON_PATH,
  VULKAN_LAYER_PROP_NAME,
  VULKAN_LAYER_PROP_TYPE,
  VULKAN_LAYER_PROP_LIBRARY_PATH,
  VULKAN_LAYER_PROP_API_VERSION,
  VULKAN_LAYER_PROP_IMPLEMENTATION_VERSION,
  VULKAN_LAYER_PROP_DESCRIPTION,
  VULKAN_LAYER_PROP_COMPONENT_LAYERS,
  N_VULKAN_LAYER_PROPERTIES
};

G_DEFINE_TYPE (SrtVulkanLayer, srt_vulkan_layer, G_TYPE_OBJECT)

static void
srt_vulkan_layer_init (SrtVulkanLayer *self)
{
}

static void
srt_vulkan_layer_get_property (GObject *object,
                               guint prop_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  SrtVulkanLayer *self = SRT_VULKAN_LAYER (object);

  switch (prop_id)
    {
      case VULKAN_LAYER_PROP_ERROR:
        g_value_set_boxed (value, self->layer.error);
        break;

      case VULKAN_LAYER_PROP_ISSUES:
        g_value_set_flags (value, self->layer.issues);
        break;

      case VULKAN_LAYER_PROP_JSON_PATH:
        g_value_set_string (value, self->layer.json_path);
        break;

      case VULKAN_LAYER_PROP_NAME:
        g_value_set_string (value, self->layer.name);
        break;

      case VULKAN_LAYER_PROP_TYPE:
        g_value_set_string (value, self->layer.type);
        break;

      case VULKAN_LAYER_PROP_LIBRARY_PATH:
        g_value_set_string (value, self->layer.library_path);
        break;

      case VULKAN_LAYER_PROP_API_VERSION:
        g_value_set_string (value, self->layer.api_version);
        break;

      case VULKAN_LAYER_PROP_IMPLEMENTATION_VERSION:
        g_value_set_string (value, self->layer.implementation_version);
        break;

      case VULKAN_LAYER_PROP_DESCRIPTION:
        g_value_set_string (value, self->layer.description);
        break;

      case VULKAN_LAYER_PROP_COMPONENT_LAYERS:
        g_value_set_boxed (value, self->layer.component_layers);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_vulkan_layer_set_property (GObject *object,
                               guint prop_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  SrtVulkanLayer *self = SRT_VULKAN_LAYER (object);
  const char *tmp;

  switch (prop_id)
    {
      case VULKAN_LAYER_PROP_ERROR:
        g_return_if_fail (self->layer.error == NULL);
        self->layer.error = g_value_dup_boxed (value);
        break;

      case VULKAN_LAYER_PROP_ISSUES:
        g_return_if_fail (self->layer.issues == 0);
        self->layer.issues = g_value_get_flags (value);
        break;

      case VULKAN_LAYER_PROP_JSON_PATH:
        g_return_if_fail (self->layer.json_path == NULL);
        tmp = g_value_get_string (value);
        self->layer.json_path = g_canonicalize_filename (tmp, NULL);
        break;

      case VULKAN_LAYER_PROP_NAME:
        g_return_if_fail (self->layer.name == NULL);
        self->layer.name = g_value_dup_string (value);
        break;

      case VULKAN_LAYER_PROP_TYPE:
        g_return_if_fail (self->layer.type == NULL);
        self->layer.type = g_value_dup_string (value);
        break;

      case VULKAN_LAYER_PROP_LIBRARY_PATH:
        g_return_if_fail (self->layer.library_path == NULL);
        self->layer.library_path = g_value_dup_string (value);
        break;

      case VULKAN_LAYER_PROP_API_VERSION:
        g_return_if_fail (self->layer.api_version == NULL);
        self->layer.api_version = g_value_dup_string (value);
        break;

      case VULKAN_LAYER_PROP_IMPLEMENTATION_VERSION:
        g_return_if_fail (self->layer.implementation_version == NULL);
        self->layer.implementation_version = g_value_dup_string (value);
        break;

      case VULKAN_LAYER_PROP_DESCRIPTION:
        g_return_if_fail (self->layer.description == NULL);
        self->layer.description = g_value_dup_string (value);
        break;

      case VULKAN_LAYER_PROP_COMPONENT_LAYERS:
        g_return_if_fail (self->layer.component_layers == NULL);
        self->layer.component_layers = g_value_dup_boxed (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_vulkan_layer_constructed (GObject *object)
{
  SrtVulkanLayer *self = SRT_VULKAN_LAYER (object);

  g_return_if_fail (self->layer.json_path != NULL);
  g_return_if_fail (g_path_is_absolute (self->layer.json_path));

  if (self->layer.error != NULL)
    {
      g_return_if_fail (self->layer.name == NULL);
      g_return_if_fail (self->layer.type == NULL);
      g_return_if_fail (self->layer.library_path == NULL);
      g_return_if_fail (self->layer.api_version == NULL);
      g_return_if_fail (self->layer.implementation_version == NULL);
      g_return_if_fail (self->layer.description == NULL);
      g_return_if_fail (self->layer.component_layers == NULL);
    }
  else
    {
      g_return_if_fail (self->layer.name != NULL);
      g_return_if_fail (self->layer.type != NULL);
      g_return_if_fail (self->layer.api_version != NULL);
      g_return_if_fail (self->layer.implementation_version != NULL);
      g_return_if_fail (self->layer.description != NULL);

      if (self->layer.library_path == NULL)
        g_return_if_fail (self->layer.component_layers != NULL && self->layer.component_layers[0] != NULL);
      else if (self->layer.component_layers == NULL || self->layer.component_layers[0] == NULL)
        g_return_if_fail (self->layer.library_path != NULL);
      else
        g_return_if_reached ();
    }
}

static void
srt_vulkan_layer_finalize (GObject *object)
{
  SrtVulkanLayer *self = SRT_VULKAN_LAYER (object);

  srt_loadable_clear (&self->layer);

  G_OBJECT_CLASS (srt_vulkan_layer_parent_class)->finalize (object);
}

static GParamSpec *vulkan_layer_properties[N_VULKAN_LAYER_PROPERTIES] = { NULL };

static void
srt_vulkan_layer_class_init (SrtVulkanLayerClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_vulkan_layer_get_property;
  object_class->set_property = srt_vulkan_layer_set_property;
  object_class->constructed = srt_vulkan_layer_constructed;
  object_class->finalize = srt_vulkan_layer_finalize;

  vulkan_layer_properties[VULKAN_LAYER_PROP_ERROR] =
    g_param_spec_boxed ("error", "Error",
                        "GError describing how this layer failed to load, or NULL",
                        G_TYPE_ERROR,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  vulkan_layer_properties[VULKAN_LAYER_PROP_ISSUES] =
    g_param_spec_flags ("issues", "Issues", "Problems with this layer",
                        SRT_TYPE_LOADABLE_ISSUES, SRT_LOADABLE_ISSUES_NONE,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  vulkan_layer_properties[VULKAN_LAYER_PROP_JSON_PATH] =
    g_param_spec_string ("json-path", "JSON path",
                         "Absolute path to JSON file describing this layer. "
                         "If examining a sysroot, this path is set as though "
                         "the sysroot was the root directory. "
                         "When constructing the object, a relative path can "
                         "be given: it will be converted to an absolute path.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_layer_properties[VULKAN_LAYER_PROP_NAME] =
    g_param_spec_string ("name", "name",
                         "The name that uniquely identify this layer to "
                         "applications.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_layer_properties[VULKAN_LAYER_PROP_TYPE] =
    g_param_spec_string ("type", "type",
                         "The type of this layer. It is expected to be either "
                         "GLOBAL or INSTANCE.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_layer_properties[VULKAN_LAYER_PROP_LIBRARY_PATH] =
    g_param_spec_string ("library-path", "Library path",
                         "Library implementing this layer, expressed as a "
                         "basename to be searched for in the default "
                         "library search path (e.g. vkOverlayLayer.so), "
                         "a relative path containing '/' to be resolved "
                         "relative to #SrtVulkanLayer:json-path (e.g. "
                         "./vkOverlayLayer.so), or an absolute path "
                         "as though the sysroot (if any) was the root "
                         "(e.g. /opt/vulkan/vkOverlayLayer.so).",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_layer_properties[VULKAN_LAYER_PROP_API_VERSION] =
    g_param_spec_string ("api-version", "API version",
                         "The version number of the Vulkan API that the "
                         "shared library was built against.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_layer_properties[VULKAN_LAYER_PROP_IMPLEMENTATION_VERSION] =
    g_param_spec_string ("implementation-version", "Implementation version",
                         "Version of the implemented layer.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_layer_properties[VULKAN_LAYER_PROP_DESCRIPTION] =
    g_param_spec_string ("description", "Description",
                         "Brief description of the layer.",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_layer_properties[VULKAN_LAYER_PROP_COMPONENT_LAYERS] =
    g_param_spec_boxed ("component-layers", "Component layers",
                        "Component layer names that are part of a meta-layer.",
                        G_TYPE_STRV,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_VULKAN_LAYER_PROPERTIES,
                                     vulkan_layer_properties);
}

/**
 * srt_vulkan_layer_new:
 * @json_path: (transfer none): the absolute path to the JSON file
 * @name: (transfer none): the layer unique name
 * @type: (transfer none): the type of the layer
 * @library_path: (transfer none): the path to the library
 * @api_version: (transfer none): the API version
 * @implementation_version: (transfer none): the version of the implemented
 *  layer
 * @description: (transfer none): the description of the layer
 * @component_layers: (transfer none): the component layer names part of a
 *  meta-layer
 * @issues: problems with this layer
 *
 * @component_layers must be %NULL if @library_path has been defined.
 * @library_path must be %NULL if @component_layers has been defined.
 *
 * Returns: (transfer full): a new SrtVulkanLayer
 */
static SrtVulkanLayer *
srt_vulkan_layer_new (const gchar *json_path,
                      const gchar *name,
                      const gchar *type,
                      const gchar *library_path,
                      const gchar *api_version,
                      const gchar *implementation_version,
                      const gchar *description,
                      GStrv component_layers,
                      SrtLoadableIssues issues)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);
  g_return_val_if_fail (type != NULL, NULL);
  g_return_val_if_fail (api_version != NULL, NULL);
  g_return_val_if_fail (implementation_version != NULL, NULL);
  g_return_val_if_fail (description != NULL, NULL);
  if (library_path == NULL)
    g_return_val_if_fail (component_layers != NULL && *component_layers != NULL, NULL);
  else if (component_layers == NULL || *component_layers == NULL)
    g_return_val_if_fail (library_path != NULL, NULL);
  else
    g_return_val_if_reached (NULL);

  return g_object_new (SRT_TYPE_VULKAN_LAYER,
                       "json-path", json_path,
                       "name", name,
                       "type", type,
                       "library-path", library_path,
                       "api-version", api_version,
                       "implementation-version", implementation_version,
                       "description", description,
                       "component-layers", component_layers,
                       "issues", issues,
                       NULL);
}

/**
 * srt_vulkan_layer_new_error:
 * @json_path: (transfer none): the absolute path to the JSON file
 * @issues: problems with this layer
 * @error: (transfer none): Error that occurred when loading the layer
 *
 * Returns: (transfer full): a new SrtVulkanLayer
 */
static SrtVulkanLayer *
srt_vulkan_layer_new_error (const gchar *json_path,
                            SrtLoadableIssues issues,
                            const GError *error)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (error != NULL, NULL);

  return g_object_new (SRT_TYPE_VULKAN_LAYER,
                       "error", error,
                       "json-path", json_path,
                       "issues", issues,
                       NULL);
}

static void
_vulkan_layer_parse_json_environment_field (const gchar *member_name,
                                            EnvironmentVariable *env_var,
                                            JsonObject* json_layer)
{
  JsonObject *env_obj = NULL;
  g_autoptr(GList) members = NULL;

  g_return_if_fail (member_name != NULL);
  g_return_if_fail (env_var != NULL);
  g_return_if_fail (env_var->name == NULL);
  g_return_if_fail (env_var->value == NULL);
  g_return_if_fail (json_layer != NULL);

  if (json_object_has_member (json_layer, member_name))
    env_obj = json_object_get_object_member (json_layer, member_name);
  if (env_obj != NULL)
    {
      members = json_object_get_members (env_obj);
      if (members != NULL)
        {
          const gchar *value = json_object_get_string_member_with_default (env_obj,
                                                                           members->data,
                                                                           NULL);

          if (value == NULL)
            {
              g_debug ("The Vulkan layer property '%s' has an element with an "
                       "invalid value, trying to continue...", member_name);
            }
          else
            {
              env_var->name = g_strdup (members->data);
              env_var->value = g_strdup (value);
            }

          if (members->next != NULL)
            g_debug ("The Vulkan layer property '%s' has more than the expected "
                     "number of elements, trying to continue...", member_name);
        }
    }
}

static SrtVulkanLayer *
vulkan_layer_parse_json (const gchar *path,
                         const gchar *file_format_version,
                         JsonObject *json_layer)
{
  const gchar *name = NULL;
  const gchar *type = NULL;
  const gchar *library_path = NULL;
  const gchar *api_version = NULL;
  const gchar *implementation_version = NULL;
  const gchar *description = NULL;
  g_auto(GStrv) component_layers = NULL;
  JsonArray *instance_json_array = NULL;
  JsonArray *device_json_array = NULL;
  JsonNode *arr_node;
  JsonObject *functions_obj = NULL;
  JsonObject *pre_instance_obj = NULL;
  SrtVulkanLayer *vulkan_layer = NULL;
  g_autoptr(GError) error = NULL;
  guint array_length;
  gsize i;
  GList *l;

  g_return_val_if_fail (path != NULL, NULL);
  g_return_val_if_fail (file_format_version != NULL, NULL);
  g_return_val_if_fail (json_layer != NULL, NULL);

  name = json_object_get_string_member_with_default (json_layer, "name", NULL);

  type = json_object_get_string_member_with_default (json_layer, "type", NULL);
  library_path = json_object_get_string_member_with_default (json_layer, "library_path", NULL);
  api_version = json_object_get_string_member_with_default (json_layer, "api_version", NULL);
  implementation_version = json_object_get_string_member_with_default (json_layer,
                                                                       "implementation_version",
                                                                       NULL);
  description = json_object_get_string_member_with_default (json_layer, "description", NULL);

  component_layers = _srt_json_object_dup_strv_member (json_layer, "component_layers", NULL);

  /* Don't distinguish between absent, and present with empty value */
  if (component_layers != NULL && component_layers[0] == NULL)
    g_clear_pointer (&component_layers, g_free);

  if (library_path != NULL && component_layers != NULL)
    {
      g_debug ("The parsed JSON layer has both 'library_path' and 'component_layers' "
               "fields. This is not allowed.");
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Vulkan layer in \"%s\" cannot be parsed because it is not allowed to list "
                   "both 'library_path' and 'component_layers' fields",
                   path);
      return srt_vulkan_layer_new_error (path, SRT_LOADABLE_ISSUES_CANNOT_LOAD, error);
    }

  if (name == NULL ||
      type == NULL ||
      api_version == NULL ||
      implementation_version == NULL ||
      description == NULL ||
      (library_path == NULL && component_layers == NULL))
    {
      g_debug ("A required field is missing from the JSON layer");
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Vulkan layer in \"%s\" cannot be parsed because it is missing a required field",
                   path);
      return srt_vulkan_layer_new_error (path, SRT_LOADABLE_ISSUES_CANNOT_LOAD, error);
    }

  vulkan_layer = srt_vulkan_layer_new (path, name, type, library_path, api_version,
                                       implementation_version, description, component_layers,
                                       SRT_LOADABLE_ISSUES_NONE);

  vulkan_layer->layer.file_format_version = g_strdup (file_format_version);

  if (json_object_has_member (json_layer, "functions"))
    functions_obj = json_object_get_object_member (json_layer, "functions");
  if (functions_obj != NULL)
    {
      g_autoptr(GList) members = NULL;
      vulkan_layer->layer.functions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                             g_free, g_free);
      members = json_object_get_members (functions_obj);
      for (l = members; l != NULL; l = l->next)
        {
          const gchar *value = json_object_get_string_member_with_default (functions_obj, l->data,
                                                                           NULL);
          if (value == NULL)
            g_debug ("The Vulkan layer property 'functions' has an element with an invalid "
                     "value, trying to continue...");
          else
            g_hash_table_insert (vulkan_layer->layer.functions,
                                 g_strdup (l->data), g_strdup (value));
        }
    }

  if (json_object_has_member (json_layer, "pre_instance_functions"))
    pre_instance_obj = json_object_get_object_member (json_layer, "pre_instance_functions");
  if (pre_instance_obj != NULL)
    {
      g_autoptr(GList) members = NULL;
      vulkan_layer->layer.pre_instance_functions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                                          g_free, g_free);
      members = json_object_get_members (pre_instance_obj);
      for (l = members; l != NULL; l = l->next)
        {
          const gchar *value = json_object_get_string_member_with_default (pre_instance_obj,
                                                                           l->data, NULL);
          if (value == NULL)
            g_debug ("The Vulkan layer property 'pre_instance_functions' has an "
                     "element with an invalid value, trying to continue...");
          else
            g_hash_table_insert (vulkan_layer->layer.pre_instance_functions,
                                 g_strdup (l->data), g_strdup (value));
        }
    }

  arr_node = json_object_get_member (json_layer, "instance_extensions");
  if (arr_node != NULL && JSON_NODE_HOLDS_ARRAY (arr_node))
    instance_json_array = json_node_get_array (arr_node);
  if (instance_json_array != NULL)
    {
      array_length = json_array_get_length (instance_json_array);
      for (i = 0; i < array_length; i++)
        {
          InstanceExtension *ie = g_slice_new0 (InstanceExtension);
          JsonObject *instance_extension = json_array_get_object_element (instance_json_array, i);
          ie->name = g_strdup (json_object_get_string_member_with_default (instance_extension,
                                                                           "name", NULL));
          ie->spec_version = g_strdup (json_object_get_string_member_with_default (instance_extension,
                                                                                   "spec_version",
                                                                                   NULL));

          if (ie->name == NULL || ie->spec_version == NULL)
            {
              g_debug ("The Vulkan layer property 'instance_extensions' is "
                       "missing some expected values, trying to continue...");
              instance_extension_free (ie);
            }
          else
            {
              vulkan_layer->layer.instance_extensions = g_list_prepend (vulkan_layer->layer.instance_extensions,
                                                                        ie);
            }
        }
      vulkan_layer->layer.instance_extensions = g_list_reverse (vulkan_layer->layer.instance_extensions);
    }

  arr_node = json_object_get_member (json_layer, "device_extensions");
  if (arr_node != NULL && JSON_NODE_HOLDS_ARRAY (arr_node))
    device_json_array = json_node_get_array (arr_node);
  if (device_json_array != NULL)
    {
      array_length = json_array_get_length (device_json_array);
      for (i = 0; i < array_length; i++)
        {
          DeviceExtension *de = g_slice_new0 (DeviceExtension);
          JsonObject *device_extension = json_array_get_object_element (device_json_array, i);
          de->name = g_strdup (json_object_get_string_member_with_default (device_extension,
                                                                           "name", NULL));
          de->spec_version = g_strdup (json_object_get_string_member_with_default (device_extension,
                                                                                   "spec_version",
                                                                                   NULL));
          de->entrypoints = _srt_json_object_dup_strv_member (device_extension, "entrypoints", NULL);

          if (de->name == NULL || de->spec_version == NULL)
            {
              g_debug ("The Vulkan layer json is missing some expected values");
              device_extension_free (de);
            }
          else
            {
              vulkan_layer->layer.device_extensions = g_list_prepend (vulkan_layer->layer.device_extensions,
                                                                      de);
            }
        }
    }

  _vulkan_layer_parse_json_environment_field ("enable_environment",
                                              &vulkan_layer->layer.enable_env_var,
                                              json_layer);

  _vulkan_layer_parse_json_environment_field ("disable_environment",
                                              &vulkan_layer->layer.disable_env_var,
                                              json_layer);

  return vulkan_layer;
}

/**
 * load_vulkan_layer_json:
 * @path: (not nullable): Path to a Vulkan layer JSON file
 *
 * Returns: (transfer full) (element-type SrtVulkanLayer): A list of Vulkan
 *  layers, least-important first
 */
static GList *
load_vulkan_layer_json (const gchar *sysroot,
                        const gchar *path)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(JsonParser) parser = NULL;
  JsonNode *node = NULL;
  JsonNode *arr_node = NULL;
  JsonObject *object = NULL;
  JsonObject *json_layer = NULL;
  JsonArray *json_layers = NULL;
  const gchar *file_format_version = NULL;
  g_autofree gchar *in_sysroot = NULL;
  g_autofree gchar *canon = NULL;
  guint length;
  gsize i;
  GList *ret_list = NULL;

  g_return_val_if_fail (path != NULL, NULL);

  if (!g_path_is_absolute (path))
    {
      canon = g_canonicalize_filename (path, NULL);
      path = canon;
    }

  in_sysroot = g_build_filename (sysroot, path, NULL);

  g_debug ("Attempting to load the json layer from %s", in_sysroot);

  parser = json_parser_new ();

  if (!json_parser_load_from_file (parser, in_sysroot, &error))
    {
      g_debug ("error %s", error->message);
      return g_list_prepend (ret_list,
                             srt_vulkan_layer_new_error (path,
                                                         SRT_LOADABLE_ISSUES_CANNOT_LOAD,
                                                         error));
    }

  node = json_parser_get_root (parser);

  if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Expected to find a JSON object in \"%s\"", path);
      return g_list_prepend (ret_list,
                             srt_vulkan_layer_new_error (path,
                                                         SRT_LOADABLE_ISSUES_CANNOT_LOAD,
                                                         error));
    }

  object = json_node_get_object (node);

  file_format_version = json_object_get_string_member_with_default (object,
                                                                    "file_format_version",
                                                                    NULL);

  if (file_format_version == NULL)
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "file_format_version in \"%s\" is missing or not a string", path);
      return g_list_prepend (ret_list,
                             srt_vulkan_layer_new_error (path,
                                                         SRT_LOADABLE_ISSUES_CANNOT_LOAD,
                                                         error));
    }

  /* At the time of writing the latest layer manifest file version is
   * 1.2.0 and forward compatibility is not guaranteed */
  if (strverscmp (file_format_version, "1.2.0") <= 0)
    {
      g_debug ("file_format_version is \"%s\"", file_format_version);
    }
  else
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Vulkan layer file_format_version \"%s\" in \"%s\" is not supported",
                   file_format_version, path);
      return g_list_prepend (ret_list,
                             srt_vulkan_layer_new_error (path,
                                                         SRT_LOADABLE_ISSUES_UNSUPPORTED,
                                                         error));
    }
  if (json_object_has_member (object, "layers"))
    {
      arr_node = json_object_get_member (object, "layers");
      if (arr_node != NULL && JSON_NODE_HOLDS_ARRAY (arr_node))
        json_layers = json_node_get_array (arr_node);

      if (json_layers == NULL)
        {
          g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "\"layers\" in \"%s\" is not an array as expected", path);
          return g_list_prepend (ret_list,
                                 srt_vulkan_layer_new_error (path,
                                                             SRT_LOADABLE_ISSUES_CANNOT_LOAD,
                                                             error));
        }
      length = json_array_get_length (json_layers);
      for (i = 0; i < length; i++)
        {
          json_layer = json_array_get_object_element (json_layers, i);
          if (json_layer == NULL)
            {
              /* Try to continue parsing */
              g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                           "the layer in \"%s\" is not an object as expected", path);
              ret_list = g_list_prepend (ret_list,
                                         srt_vulkan_layer_new_error (path,
                                                                     SRT_LOADABLE_ISSUES_CANNOT_LOAD,
                                                                     error));
              g_clear_error (&error);
              continue;
            }
          ret_list = g_list_prepend (ret_list, vulkan_layer_parse_json (path, file_format_version,
                                                                        json_layer));
        }
    }
  else if (json_object_has_member (object, "layer"))
    {
      json_layer = json_object_get_object_member (object, "layer");
      if (json_layer == NULL)
        {
          g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "\"layer\" in \"%s\" is not an object as expected", path);
          return g_list_prepend (ret_list,
                                 srt_vulkan_layer_new_error (path,
                                                             SRT_LOADABLE_ISSUES_CANNOT_LOAD,
                                                             error));
        }
      ret_list = g_list_prepend (ret_list, vulkan_layer_parse_json (path, file_format_version,
                                                                    json_layer));
    }
  else
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "The layer definitions in \"%s\" is missing both the \"layer\" and \"layers\" fields",
                   path);
          return g_list_prepend (ret_list,
                                 srt_vulkan_layer_new_error (path,
                                                             SRT_LOADABLE_ISSUES_CANNOT_LOAD,
                                                             error));
    }
  return ret_list;
}

static void
vulkan_layer_load_json (const char *sysroot,
                        const char *filename,
                        GList **list)
{
  g_return_if_fail (sysroot != NULL);
  g_return_if_fail (filename != NULL);
  g_return_if_fail (list != NULL);

  *list = g_list_concat (load_vulkan_layer_json (sysroot, filename), *list);
}

static void
vulkan_layer_load_json_cb (const char *sysroot,
                           const char *filename,
                           void *user_data)
{
  vulkan_layer_load_json (sysroot, filename, user_data);
}

/*
 * _srt_load_vulkan_layers_extended:
 * @helpers_path: (nullable): An optional path to find "inspect-library"
 *  helper, PATH is used if %NULL
 * @sysroot: (not nullable): The root directory, usually `/`
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ`
 *  was this array
 * @multiarch_tuples: (nullable): If not %NULL, duplicated
 *  Vulkan layers are searched by their absolute path, obtained using
 *  'inspect-library' in the provided multiarch tuples, instead of just their
 *  resolved library path.
 * @explicit: If %TRUE, load explicit layers, otherwise load implicit layers.
 * @check_flags: Whether to check for problems
 *
 * Implementation of srt_system_info_list_explicit_vulkan_layers() and
 * srt_system_info_list_implicit_vulkan_layers().
 *
 * Returns: (transfer full) (element-type SrtVulkanLayer): A list of Vulkan
 *  layers, most-important first
 */
GList *
_srt_load_vulkan_layers_extended (const char *helpers_path,
                                  const char *sysroot,
                                  gchar **envp,
                                  const char * const *multiarch_tuples,
                                  gboolean explicit,
                                  SrtCheckFlags check_flags)
{
  GList *ret = NULL;
  g_auto(GStrv) search_paths = NULL;
  const gchar *value;
  const gchar *suffix;

  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail (envp != NULL, NULL);

  if (explicit)
    suffix = _SRT_GRAPHICS_EXPLICIT_VULKAN_LAYER_SUFFIX;
  else
    suffix = _SRT_GRAPHICS_IMPLICIT_VULKAN_LAYER_SUFFIX;

  value = g_environ_getenv (envp, "VK_LAYER_PATH");

  /* As in the Vulkan-Loader implementation, implicit layers are not
   * overridden by "VK_LAYER_PATH"
   * https://github.com/KhronosGroup/Vulkan-Loader/blob/sdk-1.2.198.1/docs/LoaderApplicationInterface.md#forcing-layer-source-folders
   * https://github.com/KhronosGroup/Vulkan-Loader/blob/sdk-1.2.198.1/loader/loader.c#L3559
   */
  if (value != NULL && explicit)
    {
      g_auto(GStrv) dirs = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);
      load_json_dirs (sysroot, dirs, NULL, _srt_indirect_strcmp0,
                      vulkan_layer_load_json_cb, &ret);
    }
  else
    {
      search_paths = _srt_graphics_get_vulkan_search_paths (sysroot, envp,
                                                            multiarch_tuples,
                                                            suffix);
      g_debug ("SEARCH PATHS %s", search_paths[0]);
      load_json_dirs (sysroot, search_paths, NULL, _srt_indirect_strcmp0,
                      vulkan_layer_load_json_cb, &ret);
    }

  if (!(check_flags & SRT_CHECK_FLAGS_SKIP_SLOW_CHECKS))
    _srt_loadable_flag_duplicates (SRT_TYPE_VULKAN_LAYER, envp, helpers_path,
                                   multiarch_tuples, ret);

  return g_list_reverse (ret);
}

/*
 * _srt_load_vulkan_layers:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ`
 *  was this array
 * @explicit: If %TRUE, load explicit layers, otherwise load implicit layers.
 *
 * This function has been deprecated, use _srt_load_vulkan_layers_extended()
 * instead
 *
 * Returns: (transfer full) (element-type SrtVulkanLayer): A list of Vulkan
 *  layers, most-important first
 */
GList *
_srt_load_vulkan_layers (const char *sysroot,
                         gchar **envp,
                         gboolean explicit)
{
  return _srt_load_vulkan_layers_extended (NULL, sysroot, envp, NULL, explicit,
                                           SRT_CHECK_FLAGS_NONE);
}

static SrtVulkanLayer *
vulkan_layer_dup (SrtVulkanLayer *self)
{
  GHashTableIter iter;
  gpointer key;
  gpointer value;
  const GList *l;

  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);

  SrtVulkanLayer *ret = srt_vulkan_layer_new (self->layer.json_path, self->layer.name,
                                              self->layer.type, self->layer.library_path,
                                              self->layer.api_version,
                                              self->layer.implementation_version,
                                              self->layer.description,
                                              self->layer.component_layers,
                                              self->layer.issues);

  ret->layer.file_format_version = g_strdup (self->layer.file_format_version);

  if (self->layer.functions != NULL)
    {
      ret->layer.functions = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
      g_hash_table_iter_init (&iter, self->layer.functions);
      while (g_hash_table_iter_next (&iter, &key, &value))
        g_hash_table_insert (ret->layer.functions, g_strdup (key), g_strdup (value));
    }

  if (self->layer.pre_instance_functions != NULL)
    {
      ret->layer.pre_instance_functions = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                                 g_free, g_free);
      g_hash_table_iter_init (&iter, self->layer.pre_instance_functions);
      while (g_hash_table_iter_next (&iter, &key, &value))
        g_hash_table_insert (ret->layer.pre_instance_functions, g_strdup (key), g_strdup (value));
    }

  for (l = self->layer.instance_extensions; l != NULL; l = l->next)
    {
      InstanceExtension *ie = g_slice_new0 (InstanceExtension);
      InstanceExtension *self_ie = l->data;
      ie->name = g_strdup (self_ie->name);
      ie->spec_version = g_strdup (self_ie->spec_version);
      ret->layer.instance_extensions = g_list_prepend (ret->layer.instance_extensions, ie);
    }
  ret->layer.instance_extensions = g_list_reverse (ret->layer.instance_extensions);

  for (l = self->layer.device_extensions; l != NULL; l = l->next)
    {
      DeviceExtension *de = g_slice_new0 (DeviceExtension);
      DeviceExtension *self_de = l->data;
      de->name = g_strdup (self_de->name);
      de->spec_version = g_strdup (self_de->spec_version);
      de->entrypoints = g_strdupv (self_de->entrypoints);
      ret->layer.device_extensions = g_list_prepend (ret->layer.device_extensions, de);
    }
  ret->layer.device_extensions = g_list_reverse (ret->layer.device_extensions);

  ret->layer.enable_env_var.name = g_strdup (self->layer.enable_env_var.name);
  ret->layer.enable_env_var.value = g_strdup (self->layer.enable_env_var.value);

  ret->layer.disable_env_var.name = g_strdup (self->layer.disable_env_var.name);
  ret->layer.disable_env_var.value = g_strdup (self->layer.disable_env_var.value);

  return ret;
}

/**
 * srt_vulkan_layer_new_replace_library_path:
 * @self: A Vulkan layer
 * @path: (type filename) (transfer none): A path
 *
 * Return a copy of @self with the srt_vulkan_layer_get_library_path()
 * changed to @path. For example, this is useful when setting up a
 * container where the underlying shared object will be made available
 * at a different absolute path.
 *
 * If @self does not have #SrtVulkanLayer:library-path set, or if it
 * is in an error state, this returns a new reference to @self.
 *
 * Returns: (transfer full): A new reference to a #SrtVulkanLayer. Free with
 *  g_object_unref().
 */
SrtVulkanLayer *
srt_vulkan_layer_new_replace_library_path (SrtVulkanLayer *self,
                                           const gchar *library_path)
{
  SrtVulkanLayer *ret = NULL;

  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  g_return_val_if_fail (library_path != NULL, NULL);

  if (self->layer.error != NULL || self->layer.library_path == NULL)
    return g_object_ref (self);

  ret = vulkan_layer_dup (self);
  g_return_val_if_fail (ret != NULL, NULL);

  g_free (ret->layer.library_path);

  ret->layer.library_path = g_strdup (library_path);

  return ret;
}

/**
 * srt_vulkan_layer_write_to_file:
 * @self: The Vulkan layer
 * @path: (type filename): A filename
 * @error: Used to describe the error on failure
 *
 * Serialize @self to the given JSON file.
 *
 * Returns: %TRUE on success
 */
gboolean
srt_vulkan_layer_write_to_file (SrtVulkanLayer *self,
                                const char *path,
                                GError **error)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), FALSE);
  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  return srt_loadable_write_to_file (&self->layer, path, SRT_TYPE_VULKAN_LAYER, error);
}

/**
 * srt_vulkan_layer_get_json_path:
 * @self: The Vulkan layer
 *
 * Return the absolute path to the JSON file representing this layer.
 *
 * Returns: (type filename) (transfer none) (not nullable):
 *  #SrtVulkanLayer:json-path
 */
const gchar *
srt_vulkan_layer_get_json_path (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return self->layer.json_path;
}

/**
 * srt_vulkan_layer_get_library_path:
 * @self: The Vulkan layer
 *
 * Return the library path for this layer. It is either an absolute path,
 * a path relative to srt_vulkan_layer_get_json_path() containing at least one
 * directory separator (slash), or a basename to be loaded from the
 * shared library search path.
 *
 * If the JSON description for this layer could not be loaded, or if
 * #SrtVulkanLayer:component_layers is used, return %NULL instead.
 *
 * Returns: (type filename) (transfer none) (nullable): #SrtVulkanLayer:library-path
 */
const gchar *
srt_vulkan_layer_get_library_path (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return self->layer.library_path;
}

/**
 * srt_vulkan_layer_get_name:
 * @self: The Vulkan layer
 *
 * Return the name that uniquely identify this layer.
 *
 * If the JSON description for this layer could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type utf8) (transfer none) (nullable): #SrtVulkanLayer:name
 */
const gchar *
srt_vulkan_layer_get_name (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return self->layer.name;
}

/**
 * srt_vulkan_layer_get_description:
 * @self: The Vulkan layer
 *
 * Return the description of this layer.
 *
 * If the JSON description for this layer could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type utf8) (transfer none) (nullable): #SrtVulkanLayer:description
 */
const gchar *
srt_vulkan_layer_get_description (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return self->layer.description;
}

/**
 * srt_vulkan_layer_get_api_version:
 * @self: The Vulkan layer
 *
 * Return the Vulkan API version of this layer.
 *
 * If the JSON description for this layer could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type utf8) (transfer none) (nullable): #SrtVulkanLayer:api_version
 */
const gchar *
srt_vulkan_layer_get_api_version (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return self->layer.api_version;
}

/**
 * srt_vulkan_layer_get_type_value:
 * @self: The Vulkan layer
 *
 * Return the type of this layer.
 * The expected values should be either "GLOBAL" or "INSTANCE".
 *
 * If the JSON description for this layer could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type utf8) (transfer none) (nullable): #SrtVulkanLayer:type
 */
const gchar *
srt_vulkan_layer_get_type_value (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return self->layer.type;
}

/**
 * srt_vulkan_layer_get_implementation_version:
 * @self: The Vulkan layer
 *
 * Return the version of the implemented layer.
 *
 * If the JSON description for this layer could not be loaded, return %NULL
 * instead.
 *
 * Returns: (type utf8) (transfer none) (nullable):
 *  #SrtVulkanLayer:implementation_version
 */
const gchar *
srt_vulkan_layer_get_implementation_version (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return self->layer.implementation_version;
}

/**
 * srt_vulkan_layer_get_component_layers:
 * @self: The Vulkan layer
 *
 * Return the component layer names that are part of a meta-layer.
 *
 * If the JSON description for this layer could not be loaded, or if
 * #SrtVulkanLayer:library-path is used, return %NULL instead.
 *
 * Returns: (array zero-terminated=1) (transfer none) (element-type utf8) (nullable):
 *  #SrtVulkanLayer:component_layers
 */
const char * const *
srt_vulkan_layer_get_component_layers (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return (const char * const *) self->layer.component_layers;
}

/**
 * srt_vulkan_layer_get_issues:
 * @self: The Vulkan layer
 *
 * Return the problems found when parsing and loading @self.
 *
 * Returns: A bitfield containing problems, or %SRT_LOADABLE_ISSUES_NONE
 *  if no problems were found
 */
SrtLoadableIssues
srt_vulkan_layer_get_issues (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), SRT_LOADABLE_ISSUES_UNKNOWN);
  return self->layer.issues;
}

/*
 * @self: The Vulkan layer
 * @is_duplicated: if %TRUE, this layer is a duplicated of another layer
 *
 * @self issues is adjusted accordingly to the @is_duplicated value.
 */
static void
_srt_vulkan_layer_set_is_duplicated (SrtVulkanLayer *self,
                                     gboolean is_duplicated)
{
  g_return_if_fail (SRT_IS_VULKAN_LAYER (self));
  if (is_duplicated)
    self->layer.issues |= SRT_LOADABLE_ISSUES_DUPLICATED;
  else
    self->layer.issues &= ~SRT_LOADABLE_ISSUES_DUPLICATED;
}

/**
 * srt_vulkan_layer_resolve_library_path:
 * @self: A Vulkan layer
 *
 * Return the path that can be passed to `dlopen()` for this layer.
 *
 * If srt_vulkan_layer_get_library_path() is a relative path, return the
 * absolute path that is the result of interpreting it relative to
 * srt_vulkan_layer_get_json_path(). Otherwise return a copy of
 * srt_vulkan_layer_get_library_path().
 *
 * The result is either the basename of a shared library (to be found
 * relative to some directory listed in `$LD_LIBRARY_PATH`, `/etc/ld.so.conf`,
 * `/etc/ld.so.conf.d` or the hard-coded library search path), or an
 * absolute path.
 *
 * Returns: (transfer full) (type filename) (nullable): The basename of a
 *  shared library or an absolute path. Free with g_free().
 */
gchar *
srt_vulkan_layer_resolve_library_path (SrtVulkanLayer *self)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), NULL);
  return srt_loadable_resolve_library_path (&self->layer);
}

/**
 * srt_vulkan_layer_check_error:
 * @self: The layer
 * @error: Used to return details if the layer description could not be loaded
 *
 * Check whether we failed to load the JSON describing this Vulkan layer.
 * Note that this does not actually `dlopen()` the layer itself.
 *
 * Returns: %TRUE if the JSON was loaded successfully
 */
gboolean
srt_vulkan_layer_check_error (const SrtVulkanLayer *self,
                              GError **error)
{
  g_return_val_if_fail (SRT_IS_VULKAN_LAYER (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (self->layer.error != NULL && error != NULL)
    *error = g_error_copy (self->layer.error);

  return (self->layer.error == NULL);
}

/**
 * _srt_get_explicit_vulkan_layers_from_json_report:
 * @json_obj: (not nullable): A JSON Object used to search for
 *  "explicit_layers" property in "vulkan"
 *
 * Returns: A list of explicit #SrtVulkanLayer that have been found, or %NULL
 *  if none has been found.
 */
GList *
_srt_get_explicit_vulkan_layers_from_json_report (JsonObject *json_obj)
{
  return get_driver_loadables_from_json_report (json_obj, SRT_TYPE_VULKAN_LAYER, TRUE);
}

/**
 * _srt_get_implicit_vulkan_layers_from_json_report:
 * @json_obj: (not nullable): A JSON Object used to search for
 *  "implicit_layers" property in "vulkan"
 *
 * Returns: A list of implicit #SrtVulkanLayer that have been found, or %NULL
 *  if none has been found.
 */
GList *
_srt_get_implicit_vulkan_layers_from_json_report (JsonObject *json_obj)
{
  return get_driver_loadables_from_json_report (json_obj, SRT_TYPE_VULKAN_LAYER, FALSE);
}

/**
 * get_driver_loadables_from_json_report:
 * @json_obj: (not nullable): A JSON Object used to search for Icd or Layer
 *  properties
 * @which: Used to choose which loadable to search, it can be
 *  %SRT_TYPE_EGL_ICD, %SRT_TYPE_VULKAN_ICD or %SRT_TYPE_VULKAN_LAYER
 * @explicit: If %TRUE, load explicit layers, otherwise load implicit layers.
 *  Currently this value is used only if @which is %SRT_TYPE_VULKAN_LAYER
 *
 * Returns: A list of #SrtEglIcd (if @which is %SRT_TYPE_EGL_ICD) or
 *  #SrtVulkanIcd (if @which is %SRT_TYPE_VULKAN_ICD) or #SrtVulkanLayer (if
 *  @which is %SRT_TYPE_VULKAN_LAYER) that have been found, or
 *  %NULL if none has been found.
 */
static GList *
get_driver_loadables_from_json_report (JsonObject *json_obj,
                                       GType which,
                                       gboolean explicit)
{
  const gchar *member;
  const gchar *sub_member;
  JsonObject *json_sub_obj;
  JsonArray *array;
  GList *driver_info = NULL;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (which == SRT_TYPE_EGL_ICD)
    {
      member = "egl";
      sub_member = "icds";
    }
  else if (which == SRT_TYPE_VULKAN_ICD)
    {
      member = "vulkan";
      sub_member = "icds";
    }
  else if (which == SRT_TYPE_VULKAN_LAYER)
    {
      member = "vulkan";
      if (explicit)
        sub_member = "explicit_layers";
      else
        sub_member = "implicit_layers";
    }
  else
    {
      g_return_val_if_reached (NULL);
    }

  if (json_object_has_member (json_obj, member))
    {
      json_sub_obj = json_object_get_object_member (json_obj, member);

      /* We are expecting an object here */
      if (json_sub_obj == NULL)
        {
          g_debug ("'%s' is not a JSON object as expected", member);
          goto out;
        }

      if (json_object_has_member (json_sub_obj, sub_member))
        {
          array = json_object_get_array_member (json_sub_obj, sub_member);

          /* We are expecting an array of icds here */
          if (array == NULL)
            {
              g_debug ("'%s' is not an array as expected", sub_member);
              goto out;
            }

          for (guint i = 0; i < json_array_get_length (array); i++)
            {
              const gchar *json_path = NULL;
              const gchar *name = NULL;
              const gchar *type = NULL;
              const gchar *description = NULL;
              const gchar *library_path = NULL;
              const gchar *api_version = NULL;
              const gchar *implementation_version = NULL;
              g_auto(GStrv) component_layers = NULL;
              SrtVulkanLayer *layer = NULL;
              SrtLoadableIssues issues;
              GQuark error_domain;
              gint error_code;
              const gchar *error_message;
              GError *error = NULL;
              JsonObject *json_elem_obj = json_array_get_object_element (array, i);
              if (json_object_has_member (json_elem_obj, "json_path"))
                {
                  json_path = json_object_get_string_member (json_elem_obj, "json_path");
                }
              else
                {
                  g_debug ("The parsed '%s' member is missing the expected 'json_path' member, skipping...",
                           sub_member);
                  continue;
                }

              if (which == SRT_TYPE_VULKAN_LAYER)
                {
                  name = json_object_get_string_member_with_default (json_elem_obj, "name", NULL);
                  type = json_object_get_string_member_with_default (json_elem_obj, "type", NULL);
                  implementation_version = json_object_get_string_member_with_default (json_elem_obj,
                                                                                       "implementation_version",
                                                                                       NULL);
                  description = json_object_get_string_member_with_default (json_elem_obj,
                                                                            "description",
                                                                            NULL);

                  component_layers = _srt_json_object_dup_strv_member (json_elem_obj,
                                                                       "component_layers",
                                                                       NULL);

                  /* Don't distinguish between absent, and present with empty value */
                  if (component_layers != NULL && component_layers[0] == NULL)
                    g_clear_pointer (&component_layers, g_free);
                }

              library_path = json_object_get_string_member_with_default (json_elem_obj,
                                                                         "library_path",
                                                                         NULL);
              api_version = json_object_get_string_member_with_default (json_elem_obj,
                                                                        "api_version",
                                                                        NULL);
              issues = srt_get_flags_from_json_array (SRT_TYPE_LOADABLE_ISSUES,
                                                      json_elem_obj,
                                                      "issues",
                                                      SRT_LOADABLE_ISSUES_UNKNOWN);
              error_domain = g_quark_from_string (json_object_get_string_member_with_default (json_elem_obj,
                                                                                              "error-domain",
                                                                                              NULL));
              error_code = json_object_get_int_member_with_default (json_elem_obj, "error-code", -1);
              error_message = json_object_get_string_member_with_default (json_elem_obj,
                                                                          "error",
                                                                          "(missing error message)");

              if (which == SRT_TYPE_VULKAN_LAYER &&
                  (name != NULL &&
                   type != NULL &&
                   api_version != NULL &&
                   implementation_version != NULL &&
                   description != NULL &&
                   ( (library_path != NULL && component_layers == NULL) ||
                     (library_path == NULL && component_layers != NULL) )))
                {
                  layer = srt_vulkan_layer_new (json_path, name, type,
                                                library_path, api_version,
                                                implementation_version,
                                                description, component_layers,
                                                issues);
                  driver_info = g_list_prepend (driver_info, layer);
                }
              else if ((which == SRT_TYPE_EGL_ICD || which == SRT_TYPE_VULKAN_ICD) &&
                       library_path != NULL)
                {
                  if (which == SRT_TYPE_EGL_ICD)
                    driver_info = g_list_prepend (driver_info, srt_egl_icd_new (json_path,
                                                                                library_path,
                                                                                issues));
                  else if (which == SRT_TYPE_VULKAN_ICD)
                    driver_info = g_list_prepend (driver_info, srt_vulkan_icd_new (json_path,
                                                                                   api_version,
                                                                                   library_path,
                                                                                   issues));
                  else
                    g_return_val_if_reached (NULL);
                }
              else
                {
                  if (error_domain == 0)
                    {
                      error_domain = G_IO_ERROR;
                      error_code = G_IO_ERROR_FAILED;
                    }
                  g_set_error_literal (&error,
                                       error_domain,
                                       error_code,
                                       error_message);
                  if (which == SRT_TYPE_EGL_ICD)
                    driver_info = g_list_prepend (driver_info, srt_egl_icd_new_error (json_path,
                                                                                      issues,
                                                                                      error));
                  else if (which == SRT_TYPE_VULKAN_ICD)
                    driver_info = g_list_prepend (driver_info, srt_vulkan_icd_new_error (json_path,
                                                                                         issues,
                                                                                         error));
                  else if (which == SRT_TYPE_VULKAN_LAYER)
                    driver_info = g_list_prepend (driver_info, srt_vulkan_layer_new_error (json_path,
                                                                                           issues,
                                                                                           error));
                  else
                    g_return_val_if_reached (NULL);

                  g_clear_error (&error);
                }
            }
        }
    }
out:
  return g_list_reverse (driver_info);
}
