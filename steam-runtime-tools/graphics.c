/*
 * Copyright © 2019-2020 Collabora Ltd.
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
#include "steam-runtime-tools/enums.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/graphics-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "steam-runtime-tools/json-utils-internal.h"
#include "steam-runtime-tools/library-internal.h"
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
 * @parser: The JsonParser to process the wflinfo results from.
 * @version_string: (out) (transfer none) (not optional):
 * @renderer_string: (out) (transfer none) (not optional):
 */
static SrtGraphicsIssues
_srt_process_wflinfo (JsonParser *parser, const gchar **version_string, const gchar **renderer_string)
{
  g_return_val_if_fail (version_string != NULL, SRT_GRAPHICS_ISSUES_UNKNOWN);
  g_return_val_if_fail (renderer_string != NULL, SRT_GRAPHICS_ISSUES_UNKNOWN);

  SrtGraphicsIssues issues = SRT_GRAPHICS_ISSUES_NONE;

  JsonNode *node = json_parser_get_root (parser);

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

  *version_string = json_object_get_string_member (sub_object, "version string");
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
 * @parser: The JsonParser to process the vulkaninfo results from.
 * @new_version_string: (out) (transfer full) (not optional):
 * @renderer_string: (out) (transfer none) (not optional):
 */
static SrtGraphicsIssues
_srt_process_vulkaninfo (JsonParser *parser, gchar **new_version_string, const gchar **renderer_string)
{
  g_return_val_if_fail (new_version_string != NULL, SRT_GRAPHICS_ISSUES_UNKNOWN);
  g_return_val_if_fail (renderer_string != NULL, SRT_GRAPHICS_ISSUES_UNKNOWN);

  SrtGraphicsIssues issues = SRT_GRAPHICS_ISSUES_NONE;
  JsonNode *node = json_parser_get_root (parser);

  if (node == NULL)
    {
      g_debug ("The json output is empty");
      issues |= SRT_GRAPHICS_ISSUES_CANNOT_LOAD;
      return issues;
    }

  JsonObject *object = json_node_get_object (node);
  JsonNode *sub_node = NULL;
  JsonObject *sub_object = NULL;

  // Parse vulkaninfo output
  unsigned int api_version = 0;
  unsigned int hw_vendor = 0;
  unsigned int hw_device = 0;
  unsigned int driver_version = 0;

  if (!json_object_has_member (object, "VkPhysicalDeviceProperties"))
    {
      g_debug ("The json output doesn't contain VkPhysicalDeviceProperties");
      issues |= SRT_GRAPHICS_ISSUES_CANNOT_LOAD;
      return issues;
    }

  sub_node = json_object_get_member (object, "VkPhysicalDeviceProperties");
  sub_object = json_node_get_object (sub_node);

  if (!json_object_has_member (sub_object, "deviceName") ||
      !json_object_has_member (sub_object, "driverVersion") ||
      !json_object_has_member (sub_object, "apiVersion") ||
      !json_object_has_member (sub_object, "deviceID") ||
      !json_object_has_member (sub_object, "vendorID"))
    {
      g_debug ("Json output is missing deviceName or driverVersion");
      issues |= SRT_GRAPHICS_ISSUES_CANNOT_LOAD;
      return issues;
    }

  api_version = json_object_get_int_member (sub_object, "apiVersion");
  hw_vendor = json_object_get_int_member (sub_object, "vendorID");
  driver_version = json_object_get_int_member (sub_object, "driverVersion");
  hw_device = json_object_get_int_member (sub_object, "deviceID");

  *new_version_string = g_strdup_printf ("%u.%u.%u (device %04x:%04x) (driver %u.%u.%u)",
                                        VK_VERSION_MAJOR (api_version),
                                        VK_VERSION_MINOR (api_version),
                                        VK_VERSION_PATCH (api_version),
                                        hw_vendor,
                                        hw_device,
                                        VK_VERSION_MAJOR (driver_version),
                                        VK_VERSION_MINOR (driver_version),
                                        VK_VERSION_PATCH (driver_version));
  *renderer_string = json_object_get_string_member (sub_object, "deviceName");

  /* NOTE: No need to check for software rendering with vulkan yet */
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
        argv = _srt_get_helper (helpers_path, multiarch_tuple, "vulkaninfo",
                                flags, error);

        if (argv == NULL)
          goto out;

        g_ptr_array_add (argv, g_strdup ("-j"));
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
_argv_for_check_vulkan (const char *helpers_path,
                        SrtTestFlags test_flags,
                        const char *multiarch_tuple,
                        GError **error)
{
  GPtrArray *argv;
  SrtHelperFlags flags = SRT_HELPER_FLAGS_TIME_OUT;

  if (test_flags & SRT_TEST_FLAGS_TIME_OUT_SOONER)
    flags |= SRT_HELPER_FLAGS_TIME_OUT_SOONER;

  argv = _srt_get_helper (helpers_path, multiarch_tuple, "check-vulkan",
                          flags, error);

  if (argv == NULL)
    return NULL;

  g_ptr_array_add (argv, NULL);
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

static GPtrArray *
_argv_for_list_vdpau_drivers (gchar **envp,
                              const char *helpers_path,
                              const char *multiarch_tuple,
                              const char *temp_dir,
                              GError **error)
{
  const gchar *vdpau_driver = NULL;
  GPtrArray *argv;

  g_return_val_if_fail (envp != NULL, NULL);

  vdpau_driver = g_environ_getenv (envp, "VDPAU_DRIVER");
  argv = _srt_get_helper (helpers_path, multiarch_tuple, "capsule-capture-libs",
                          SRT_HELPER_FLAGS_SEARCH_PATH, error);

  if (argv == NULL)
    return NULL;

  g_ptr_array_add (argv, g_strdup ("--dest"));
  g_ptr_array_add (argv, g_strdup (temp_dir));
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname-match:libvdpau_*.so"));
  /* If the driver is not in the ld.so.cache the wildcard-matching will not find it.
   * To increase our chances we specifically search for the chosen driver and some
   * commonly used drivers. */
  if (vdpau_driver != NULL)
    {
      g_ptr_array_add (argv, g_strjoin (NULL,
                                        "no-dependencies:if-exists:even-if-older:soname:libvdpau_",
                                        vdpau_driver, ".so", NULL));
    }
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libvdpau_nouveau.so"));
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libvdpau_nvidia.so"));
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libvdpau_r300.so"));
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libvdpau_r600.so"));
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libvdpau_radeonsi.so"));
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libvdpau_va_gl.so"));
  g_ptr_array_add (argv, NULL);
  return argv;
}

static GPtrArray *
_argv_for_list_glx_icds (const char *helpers_path,
                         const char *multiarch_tuple,
                         const char *temp_dir,
                         GError **error)
{
  GPtrArray *argv;

  argv = _srt_get_helper (helpers_path, multiarch_tuple, "capsule-capture-libs",
                          SRT_HELPER_FLAGS_SEARCH_PATH, error);

  if (argv == NULL)
    return NULL;

  g_ptr_array_add (argv, g_strdup ("--dest"));
  g_ptr_array_add (argv, g_strdup (temp_dir));
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname-match:libGLX_*.so.0"));
  /* This one might seem redundant but it is required because "libGLX_indirect"
   * is usually a symlink to someone else's implementation and can't be found
   * in the ld.so cache, that "capsule-capture-libs" uses. So instead of using
   * a wildcard-matching we have to look it up explicitly. */
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libGLX_indirect.so.0"));
  /* If we are in a container the same might happen also for the other GLX drivers.
   * To increase our chances to find all the libraries we hard code "mesa" and
   * "nvidia" that, in the vast majority of the cases, are all we care about. */
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libGLX_mesa.so.0"));
  g_ptr_array_add (argv, g_strdup ("no-dependencies:if-exists:even-if-older:soname:libGLX_nvidia.so.0"));
  g_ptr_array_add (argv, NULL);
  return argv;
}

static GPtrArray *
_argv_for_list_glx_icds_in_path (const char *helpers_path,
                                 const char *multiarch_tuple,
                                 const char *temp_dir,
                                 const char *base_path,
                                 GError **error)
{
  GPtrArray *argv;

  argv = _srt_get_helper (helpers_path, multiarch_tuple, "capsule-capture-libs",
                          SRT_HELPER_FLAGS_SEARCH_PATH, error);

  if (argv == NULL)
    return NULL;

  gchar *lib_full_path = g_build_filename (base_path, "lib", multiarch_tuple, "libGLX_*.so.*", NULL);

  g_ptr_array_add (argv, g_strdup ("--dest"));
  g_ptr_array_add (argv, g_strdup (temp_dir));
  g_ptr_array_add (argv, g_strjoin (NULL,
                                    "no-dependencies:if-exists:even-if-older:path-match:", lib_full_path,
                                    NULL));
  g_ptr_array_add (argv, NULL);
  g_free (lib_full_path);
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
  JsonParser *parser = NULL;
  const gchar *version_string = NULL;
  gchar *new_version_string = NULL;
  const gchar *renderer_string = NULL;
  GError *error = NULL;
  SrtGraphicsIssues issues = SRT_GRAPHICS_ISSUES_NONE;
  SrtGraphicsIssues non_zero_wait_status_issue = SRT_GRAPHICS_ISSUES_CANNOT_LOAD;
  GStrv my_environ = NULL;
  const gchar *ld_preload;
  gchar *filtered_preload = NULL;
  SrtGraphicsLibraryVendor library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN;

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

  my_environ = g_strdupv (envp);
  ld_preload = g_environ_getenv (my_environ, "LD_PRELOAD");
  if (ld_preload != NULL)
    {
      filtered_preload = _srt_filter_gameoverlayrenderer (ld_preload);
      my_environ = g_environ_setenv (my_environ, "LD_PRELOAD", filtered_preload, TRUE);
    }

  library_vendor = _srt_check_library_vendor (envp, multiarch_tuple,
                                              window_system,
                                              rendering_interface);

  switch (rendering_interface)
    {
      case SRT_RENDERING_INTERFACE_GL:
      case SRT_RENDERING_INTERFACE_GLESV2:
      case SRT_RENDERING_INTERFACE_VULKAN:
        non_zero_wait_status_issue = SRT_GRAPHICS_ISSUES_CANNOT_LOAD;
        break;

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
                                 non_zero_wait_status_issue);

      goto out;
    }

  switch (rendering_interface)
    {
      case SRT_RENDERING_INTERFACE_GL:
      case SRT_RENDERING_INTERFACE_GLESV2:
      case SRT_RENDERING_INTERFACE_VULKAN:
        /* We can't use `json_from_string()` directly because we are targeting an
         * older json-glib version */
        parser = json_parser_new ();

        if (!json_parser_load_from_data (parser, output, -1, &error))
          {
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
        break;

      case SRT_RENDERING_INTERFACE_VDPAU:
      case SRT_RENDERING_INTERFACE_VAAPI:
        /* The output is in plan text, nothing to do here */
        break;

      default:
        g_return_val_if_reached (SRT_GRAPHICS_ISSUES_UNKNOWN);
    }

  switch (rendering_interface)
    {
      case SRT_RENDERING_INTERFACE_GL:
      case SRT_RENDERING_INTERFACE_GLESV2:
        issues |= _srt_process_wflinfo (parser, &version_string, &renderer_string);

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
        issues |= _srt_process_vulkaninfo (parser, &new_version_string, &renderer_string);
        if (new_version_string != NULL)
          {
            version_string = new_version_string;
          }

        /* Now perform *-check-vulkan drawing test */
        g_ptr_array_unref (argv);
        g_clear_pointer (&output, g_free);

        argv = _argv_for_check_vulkan (helpers_path,
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

  /* If we have stderr (or error messages) from both vulkaninfo and
   * check-vulkan, combine them */
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
                                      issues,
                                      child_stderr,
                                      exit_status,
                                      terminating_signal);

  if (parser != NULL)
    g_object_unref (parser);

  g_free (new_version_string);
  g_clear_pointer (&argv, g_ptr_array_unref);
  g_free (output);
  g_free (child_stderr);
  g_free (child_stderr2);
  g_clear_error (&error);
  g_free (filtered_preload);
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
          const gchar *messages = NULL;
          const gchar *renderer = NULL;
          const gchar *version = NULL;
          int exit_status;
          int terminating_signal;
          SrtGraphics *graphics;
          SrtGraphicsLibraryVendor library_vendor = SRT_GRAPHICS_LIBRARY_VENDOR_UNKNOWN;
          SrtGraphicsIssues graphics_issues = SRT_GRAPHICS_ISSUES_NONE;
          SrtWindowSystem window_system = SRT_WINDOW_SYSTEM_X11;
          SrtRenderingInterface rendering_interface = SRT_RENDERING_INTERFACE_GL;

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

          messages = json_object_get_string_member_with_default (json_stack_obj, "messages",
                                                                 NULL);
          renderer = json_object_get_string_member_with_default (json_stack_obj, "renderer",
                                                                 NULL);
          version = json_object_get_string_member_with_default (json_stack_obj, "version",
                                                                NULL);

          /* In graphics, a missing "issues" array means that no issues were found */
          if (json_object_has_member (json_stack_obj, "issues"))
            {
              array = json_object_get_array_member (json_stack_obj, "issues");

              /* We are expecting an array of issues here */
              if (array == NULL)
                {
                  g_debug ("'issues' in 'graphics-details' is not an array as expected");
                  graphics_issues |= SRT_GRAPHICS_ISSUES_UNKNOWN;
                }
              else
                {
                  for (guint j = 0; j < json_array_get_length (array); j++)
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

          graphics = _srt_graphics_new (multiarch_tuple,
                                        window_system,
                                        rendering_interface,
                                        library_vendor,
                                        renderer,
                                        version,
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
  gchar *api_version;   /* Always NULL when found in a SrtEglIcd */
  gchar *json_path;
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
   * — https://github.com/KhronosGroup/Vulkan-Loader/blob/master/loader/LoaderAndLayerInterface.md#icd-manifest-file-format
   * — https://github.com/KhronosGroup/Vulkan-Loader/blob/master/loader/LoaderAndLayerInterface.md#layer-manifest-file-format
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

  g_return_if_fail (sysroot != NULL);
  g_return_if_fail (load_json_cb != NULL);

  for (iter = search_paths;
       iter != NULL && *iter != NULL;
       iter++)
    load_json_dir (sysroot, *iter, suffix, sort, load_json_cb, user_data);
}

/*
 * load_json:
 * @type: %SRT_TYPE_EGL_ICD or %SRT_TYPE_VULKAN_ICD
 * @path: (type filename) (transfer none): Path of JSON file
 * @api_version_out: (out) (type utf8) (transfer full): Used to return
 *  API version for %SRT_TYPE_VULKAN_ICD
 * @library_path_out: (out) (type utf8) (transfer full): Used to return
 *  shared library path
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
    goto out;

  node = json_parser_get_root (parser);

  if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Expected to find a JSON object in \"%s\"", path);
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
      goto out;
    }

  file_format_version = json_node_get_string (subnode);

  if (file_format_version == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "file_format_version in \"%s\" not a string", path);
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
          goto out;
        }
    }

  subnode = json_object_get_member (object, "ICD");

  if (subnode == NULL
      || !JSON_NODE_HOLDS_OBJECT (subnode))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "No \"ICD\" object in \"%s\"", path);
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
          goto out;
        }

      api_version = json_node_get_string (subnode);

      if (api_version == NULL)
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                       "ICD.api_version in \"%s\" not a string", path);
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
      goto out;
    }

  library_path = json_node_get_string (subnode);

  if (library_path == NULL)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "ICD.library_path in \"%s\" not a string", path);
      goto out;
    }

  if (api_version_out != NULL)
    *api_version_out = g_strdup (api_version);

  if (library_path_out != NULL)
    *library_path_out = g_strdup (library_path);

  ret = TRUE;

out:
  g_clear_object (&parser);
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

      case EGL_ICD_PROP_JSON_PATH:
        g_return_if_fail (self->icd.json_path == NULL);
        tmp = g_value_get_string (value);

        if (g_path_is_absolute (tmp))
          {
            self->icd.json_path = g_strdup (tmp);
          }
        else
          {
            gchar *cwd = g_get_current_dir ();

            self->icd.json_path = g_build_filename (cwd, tmp, NULL);
            g_free (cwd);
          }
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

  egl_icd_properties[EGL_ICD_PROP_JSON_PATH] =
    g_param_spec_string ("json-path", "JSON path",
                         "Absolute path to JSON file describing this ICD. "
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
                         "(e.g. /opt/EGL/libEGL_myvendor.so)",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  egl_icd_properties[EGL_ICD_PROP_RESOLVED_LIBRARY_PATH] =
    g_param_spec_string ("resolved-library-path", "Resolved library path",
                         "Library implementing this ICD, expressed as a "
                         "basename to be searched for in the default "
                         "library search path (e.g. libEGL_mesa.so.0) "
                         "or an absolute path (e.g. "
                         "/opt/EGL/libEGL_myvendor.so)",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_EGL_ICD_PROPERTIES,
                                     egl_icd_properties);
}

/**
 * srt_egl_icd_new:
 * @json_path: (transfer none): the absolute path to the JSON file
 * @library_path: (transfer none): the path to the library
 *
 * Returns: (transfer full): a new ICD
 */
static SrtEglIcd *
srt_egl_icd_new (const gchar *json_path,
                 const gchar *library_path)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (library_path != NULL, NULL);

  return g_object_new (SRT_TYPE_EGL_ICD,
                       "json-path", json_path,
                       "library-path", library_path,
                       NULL);
}

/**
 * srt_egl_icd_new_error:
 * @error: (transfer none): Error that occurred when loading the ICD
 *
 * Returns: (transfer full): a new ICD
 */
static SrtEglIcd *
srt_egl_icd_new_error (const gchar *json_path,
                       const GError *error)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (error != NULL, NULL);

  return g_object_new (SRT_TYPE_EGL_ICD,
                       "error", error,
                       "json-path", json_path,
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

  g_return_if_fail (sysroot != NULL);
  g_return_if_fail (list != NULL);

  if (!g_path_is_absolute (filename))
    {
      canon = g_canonicalize_filename (filename, NULL);
      filename = canon;
    }

  in_sysroot = g_build_filename (sysroot, filename, NULL);

  if (load_json (SRT_TYPE_EGL_ICD, in_sysroot,
                 NULL, &library_path, &error))
    {
      g_assert (library_path != NULL);
      g_assert (error == NULL);
      *list = g_list_prepend (*list,
                              srt_egl_icd_new (filename, library_path));
    }
  else
    {
      g_assert (library_path == NULL);
      g_assert (error != NULL);
      *list = g_list_prepend (*list,
                              srt_egl_icd_new_error (filename, error));
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

  return srt_egl_icd_new (self->icd.json_path, path);
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

/*
 * _srt_load_egl_icds:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ`
 *  was this array
 * @multiarch_tuples: (nullable): If not %NULL, and a Flatpak environment
 *  is detected, assume a freedesktop-sdk-based runtime and look for
 *  GL extensions for these multiarch tuples
 *
 * Implementation of srt_system_info_list_egl_icds().
 *
 * Returns: (transfer full) (element-type SrtEglIcd): A list of ICDs,
 *  most-important first
 */
GList *
_srt_load_egl_icds (const char *sysroot,
                    gchar **envp,
                    const char * const *multiarch_tuples)
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
      gchar **filenames = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);

      for (i = 0; filenames[i] != NULL; i++)
        egl_icd_load_json (sysroot, filenames[i], &ret);

      g_strfreev (filenames);
    }
  else
    {
      gchar **dirs;
      gchar *flatpak_info = NULL;

      value = g_environ_getenv (envp, "__EGL_VENDOR_LIBRARY_DIRS");

      flatpak_info = g_build_filename (sysroot, ".flatpak-info", NULL);

      if (value != NULL)
        {
          dirs = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);
          load_json_dirs (sysroot, dirs, NULL, _srt_indirect_strcmp0,
                          egl_icd_load_json_cb, &ret);
          g_strfreev (dirs);
        }
      else if (g_file_test (flatpak_info, G_FILE_TEST_EXISTS)
               && multiarch_tuples != NULL)
        {
          g_debug ("Flatpak detected: assuming freedesktop-based runtime");

          for (i = 0; multiarch_tuples[i] != NULL; i++)
            {
              gchar *tmp;

              /* freedesktop-sdk reconfigures the EGL loader to look here. */
              tmp = g_strdup_printf ("/usr/lib/%s/GL/" EGL_VENDOR_SUFFIX,
                                     multiarch_tuples[i]);
              load_json_dir (sysroot, tmp, NULL, _srt_indirect_strcmp0,
                             egl_icd_load_json_cb, &ret);
              g_free (tmp);
            }
        }
      else
        {
          load_json_dir (sysroot, get_glvnd_sysconfdir (), EGL_VENDOR_SUFFIX,
                         _srt_indirect_strcmp0, egl_icd_load_json_cb, &ret);
          load_json_dir (sysroot, get_glvnd_datadir (), EGL_VENDOR_SUFFIX,
                         _srt_indirect_strcmp0, egl_icd_load_json_cb, &ret);
        }

      g_free (flatpak_info);
    }

  return g_list_reverse (ret);
}

static gchar *
_srt_resolve_library_path (const gchar *library_path)
{
  gchar *base;
  gchar *ret;

  g_return_val_if_fail (library_path != NULL, NULL);

  /* We can't use g_canonicalize_filename() because we are targeting an earlier glib version */
  if (library_path[0] == '/')
    return g_strdup (library_path);

  base = g_get_current_dir ();
  ret = g_build_filename (base, library_path, NULL);
  g_free (base);
  return ret;
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
 * SrtDriDriver:
 *
 * Opaque object representing a Mesa DRI driver.
 */

struct _SrtDriDriver
{
  /*< private >*/
  GObject parent;
  gchar *library_path;
  gboolean is_extra;
};

struct _SrtDriDriverClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum
{
  DRI_DRIVER_PROP_0,
  DRI_DRIVER_PROP_LIBRARY_PATH,
  DRI_DRIVER_PROP_IS_EXTRA,
  DRI_DRIVER_PROP_RESOLVED_LIBRARY_PATH,
  N_DRI_DRIVER_PROPERTIES
};

G_DEFINE_TYPE (SrtDriDriver, srt_dri_driver, G_TYPE_OBJECT)

static void
srt_dri_driver_init (SrtDriDriver *self)
{
}

static void
srt_dri_driver_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  SrtDriDriver *self = SRT_DRI_DRIVER (object);

  switch (prop_id)
    {
      case DRI_DRIVER_PROP_LIBRARY_PATH:
        g_value_set_string (value, self->library_path);
        break;

      case DRI_DRIVER_PROP_IS_EXTRA:
        g_value_set_boolean (value, self->is_extra);
        break;

      case DRI_DRIVER_PROP_RESOLVED_LIBRARY_PATH:
        g_value_take_string (value, srt_dri_driver_resolve_library_path (self));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_dri_driver_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  SrtDriDriver *self = SRT_DRI_DRIVER (object);

  switch (prop_id)
    {
      case DRI_DRIVER_PROP_LIBRARY_PATH:
        g_return_if_fail (self->library_path == NULL);
        self->library_path = g_value_dup_string (value);
        break;

      case DRI_DRIVER_PROP_IS_EXTRA:
        self->is_extra = g_value_get_boolean (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_dri_driver_finalize (GObject *object)
{
  SrtDriDriver *self = SRT_DRI_DRIVER (object);

  g_clear_pointer (&self->library_path, g_free);

  G_OBJECT_CLASS (srt_dri_driver_parent_class)->finalize (object);
}

static GParamSpec *dri_driver_properties[N_DRI_DRIVER_PROPERTIES] = { NULL };

static void
srt_dri_driver_class_init (SrtDriDriverClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_dri_driver_get_property;
  object_class->set_property = srt_dri_driver_set_property;
  object_class->finalize = srt_dri_driver_finalize;

  dri_driver_properties[DRI_DRIVER_PROP_LIBRARY_PATH] =
    g_param_spec_string ("library-path", "Library path",
                         "Path to the DRI driver library. It might be absolute "
                         "(e.g. /usr/lib/dri/i965_dri.so) or relative "
                         "(e.g. custom/dri/i965_dri.so)",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  dri_driver_properties[DRI_DRIVER_PROP_IS_EXTRA] =
    g_param_spec_boolean ("is-extra", "Is extra?",
                          "TRUE if the driver is located in an unusual path",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  dri_driver_properties[DRI_DRIVER_PROP_RESOLVED_LIBRARY_PATH] =
    g_param_spec_string ("resolved-library-path", "Resolved library path",
                         "Absolute path to the DRI driver library. This is similar "
                         "to 'library-path', but is guaranteed to be an "
                         "absolute path (e.g. /usr/lib/dri/i965_dri.so)",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_DRI_DRIVER_PROPERTIES,
                                     dri_driver_properties);
}

/**
 * srt_dri_driver_new:
 * @library_path: (transfer none): the path to the library
 * @is_extra: if the DRI driver is in an unusual path
 *
 * Returns: (transfer full): a new DRI driver
 */
static SrtDriDriver *
srt_dri_driver_new (const gchar *library_path,
                    gboolean is_extra)
{
  g_return_val_if_fail (library_path != NULL, NULL);

  return g_object_new (SRT_TYPE_DRI_DRIVER,
                       "library-path", library_path,
                       "is-extra", is_extra,
                       NULL);
}

/**
 * srt_dri_driver_get_library_path:
 * @self: The DRI driver
 *
 * Return the library path for this DRI driver.
 *
 * Returns: (type filename) (transfer none): #SrtDriDriver:library-path
 */
const gchar *
srt_dri_driver_get_library_path (SrtDriDriver *self)
{
  g_return_val_if_fail (SRT_IS_DRI_DRIVER (self), NULL);
  return self->library_path;
}

/**
 * srt_dri_driver_is_extra:
 * @self: The DRI driver
 *
 * Return a gboolean that indicates if the DRI is in an unusual position.
 *
 * Returns: %TRUE if the DRI driver is in an unusual position.
 */
gboolean
srt_dri_driver_is_extra (SrtDriDriver *self)
{
  g_return_val_if_fail (SRT_IS_DRI_DRIVER (self), FALSE);
  return self->is_extra;
}

/**
 * srt_dri_driver_resolve_library_path:
 * @self: The DRI driver
 *
 * Return the absolute path for this DRI driver.
 * If srt_dri_driver_get_library_path() is already an absolute path, a copy
 * of the same value will be returned.
 *
 * Returns: (type filename) (transfer full): A copy of
 *  #SrtDriDriver:resolved-library-path. Free with g_free().
 */
gchar *
srt_dri_driver_resolve_library_path (SrtDriDriver *self)
{
  g_return_val_if_fail (SRT_IS_DRI_DRIVER (self), NULL);
  g_return_val_if_fail (self->library_path != NULL, NULL);

  return _srt_resolve_library_path (self->library_path);
}

/**
 * _srt_dri_driver_get_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "dri_drivers"
 *  property
 *
 * If the provided @json_obj doesn't have a "dri_drivers" member, or it is
 * malformed, %NULL will be returned.
 *
 * Returns: A list of #SrtDriDriver that have been found, or %NULL if none
 *  has been found.
 */
GList *
_srt_dri_driver_get_from_report (JsonObject *json_obj)
{
  JsonArray *array;
  GList *dri_drivers = NULL;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (json_object_has_member (json_obj, "dri_drivers"))
    {
      array = json_object_get_array_member (json_obj, "dri_drivers");

      if (array == NULL)
        goto out;

      guint length = json_array_get_length (array);
      for (guint j = 0; j < length; j++)
        {
          const gchar *dri_path = NULL;
          gboolean is_extra = FALSE;
          JsonObject *json_dri_obj = json_array_get_object_element (array, j);
          dri_path = json_object_get_string_member_with_default (json_dri_obj, "library_path",
                                                                 NULL);
          is_extra = json_object_get_boolean_member_with_default (json_dri_obj, "is_extra",
                                                                  FALSE);

          dri_drivers = g_list_prepend (dri_drivers, srt_dri_driver_new (dri_path, is_extra));
        }
    }

out:
  return g_list_reverse (dri_drivers);
}

/**
 * _srt_get_library_class:
 * @library: (not nullable) (type filename): The library path to use
 *
 * Return the class of the specified library.
 * If it fails, %ELFCLASSNONE will be returned.
 *
 * Returns: the library class.
 */
static int
_srt_get_library_class (const gchar *library)
{
  Elf *elf = NULL;
  int fd = -1;
  int class = ELFCLASSNONE;

  g_return_val_if_fail (library != NULL, ELFCLASSNONE);

  if (elf_version (EV_CURRENT) == EV_NONE)
    {
      g_debug ("elf_version(EV_CURRENT): %s", elf_errmsg (elf_errno ()));
      goto out;
    }

  if ((fd = open (library, O_RDONLY | O_CLOEXEC, 0)) < 0)
    {
      g_debug ("failed to open %s", library);
      goto out;
    }

  if ((elf = elf_begin (fd, ELF_C_READ, NULL)) == NULL)
    {
      g_debug ("elf_begin() failed: %s", elf_errmsg (elf_errno ()));
      goto out;
    }

  class = gelf_getclass (elf);

out:
  if (elf != NULL)
    elf_end (elf);

  if (fd >= 0)
    close (fd);

  return class;
}

/**
 * _srt_get_extra_modules_directory:
 * @library_search_path: (not nullable) (type filename): The absolute path to a directory that
 *  is in the library search path (e.g. /usr/lib/x86_64-linux-gnu)
 * @multiarch_tuple: (not nullable) (type filename): A Debian-style multiarch tuple
 *  such as %SRT_ABI_X86_64
 * @driver_class: Get the extra directories based on this ELF class, like
 *  ELFCLASS64.
 *
 * Given a loader path, this function tries to create a list of extra directories where it
 * might be possible to find driver modules.
 * E.g. given /usr/lib/x86_64-linux-gnu, return /usr/lib64 and /usr/lib
 *
 * Returns: (transfer full) (element-type gchar *) (nullable): A list of absolute
 *  paths in descending alphabetical order, or %NULL if an error occurred.
 */
static GList *
_srt_get_extra_modules_directory (const gchar *library_search_path,
                                  const gchar *multiarch_tuple,
                                  int driver_class)
{
  GList *ret = NULL;
  const gchar *libqual = NULL;
  gchar *lib_multiarch;
  gchar *dir;

  g_return_val_if_fail (library_search_path != NULL, NULL);
  g_return_val_if_fail (multiarch_tuple != NULL, NULL);

  dir = g_strdup (library_search_path);

  /* If the loader path ends with "/mesa" we try to look one directory above.
   * For example this is how Ubuntu 16.04 works, the loaders are in ${libdir}/mesa
   * and the DRI modules in ${libdir}/dri */
  if (g_str_has_suffix (dir, "/mesa"))
    {
      dir[strlen (dir) - strlen ("/mesa") + 1] = '\0';
      /* Remove the trailing slash */
      if (g_strcmp0 (dir, "/") != 0)
        dir[strlen (dir) - 1] = '\0';
    }

  ret = g_list_prepend (ret, g_build_filename (dir, "dri", NULL));
  g_debug ("Looking in lib directory: %s", (const char *) ret->data);

  lib_multiarch = g_strdup_printf ("/lib/%s", multiarch_tuple);

  if (!g_str_has_suffix (dir, lib_multiarch))
    {
      g_debug ("%s is not in the loader path: %s", lib_multiarch, library_search_path);
      goto out;
    }

  dir[strlen (dir) - strlen (lib_multiarch) + 1] = '\0';

  switch (driver_class)
    {
      case ELFCLASS32:
        libqual = "lib32";
        break;

      case ELFCLASS64:
        libqual = "lib64";
        break;

      case ELFCLASSNONE:
      default:
        g_free (lib_multiarch);
        g_free (dir);
        g_return_val_if_reached (NULL);
    }

  ret = g_list_prepend (ret, g_build_filename (dir, "lib", "dri", NULL));
  g_debug ("Looking in lib directory: %s", (const char *) ret->data);
  ret = g_list_prepend (ret, g_build_filename (dir, libqual, "dri", NULL));
  g_debug ("Looking in libQUAL directory: %s", (const char *) ret->data);

  ret = g_list_sort (ret, (GCompareFunc) strcmp);

out:
  g_free (lib_multiarch);
  g_free (dir);
  return ret;
}

static SrtVaApiDriver *
srt_va_api_driver_new (const gchar *library_path,
                       gboolean is_extra);
static SrtVdpauDriver *
srt_vdpau_driver_new (const gchar *library_path,
                      const gchar *library_link,
                      gboolean is_extra);
static SrtGlxIcd *
srt_glx_icd_new (const gchar *library_soname,
                 const gchar *library_path);

/**
 * _srt_get_modules_from_path:
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ` was this array
 * @helpers_path: (nullable): An optional path to find "inspect-library" helper, PATH
 *  is used if %NULL
 * @multiarch_tuple: (not nullable) (type filename): A Debian-style multiarch tuple
 *  such as %SRT_ABI_X86_64
 * @module_directory_path: (not nullable) (type filename): Path where to
 *  search for driver modules
 * @is_extra: If this path should be considered an extra or not
 * @module: Which graphic module to search
 * @drivers_out: (inout): Prepend the found drivers to this list.
 *  If @module is #SRT_GRAPHICS_DRI_MODULE, the element-type will be #SrtDriDriver.
 *  Otherwise if @module is #SRT_GRAPHICS_VAAPI_MODULE, the element-type will be #SrtVaApiDriver.
 *
 * @drivers_out will be prepended only with modules that are of the same ELF class that
 * corresponds to @multiarch_tuple.
 *
 * Drivers are added to `drivers_out` in reverse lexicographic order
 * (`r600_dri.so` is before `r200_dri.so`, which is before `i965_dri.so`).
 */
static void
_srt_get_modules_from_path (gchar **envp,
                            const char *helpers_path,
                            const char *multiarch_tuple,
                            const char *module_directory_path,
                            gboolean is_extra,
                            SrtGraphicsModule module,
                            GList **drivers_out)
{
  const gchar *member;
  /* We have up to 2 suffixes that we want to list */
  const gchar *module_suffix[3];
  const gchar *module_prefix = NULL;
  GDir *dir = NULL;
  SrtLibraryIssues issues;

  g_return_if_fail (envp != NULL);
  g_return_if_fail (module_directory_path != NULL);
  g_return_if_fail (drivers_out != NULL);

  g_debug ("Looking for %sdrivers in %s",
           is_extra ? "extra " : "",
           module_directory_path);

  switch (module)
    {
      case SRT_GRAPHICS_DRI_MODULE:
        module_suffix[0] = "_dri.so";
        module_suffix[1] = NULL;
        break;

      case SRT_GRAPHICS_VAAPI_MODULE:
        module_suffix[0] = "_drv_video.so";
        module_suffix[1] = NULL;
        break;

      case SRT_GRAPHICS_VDPAU_MODULE:
        module_prefix = "libvdpau_";
        module_suffix[0] = ".so";
        module_suffix[1] = ".so.1";
        module_suffix[2] = NULL;
        break;

      case SRT_GRAPHICS_GLX_MODULE:
      case NUM_SRT_GRAPHICS_MODULES:
      default:
        g_return_if_reached ();
    }

  dir = g_dir_open (module_directory_path, 0, NULL);
  if (dir)
    {
      GPtrArray *in_this_dir = g_ptr_array_new_with_free_func (g_free);
      while ((member = g_dir_read_name (dir)) != NULL)
        {
          for (gsize i = 0; module_suffix[i] != NULL; i++)
            {
              if (g_str_has_suffix (member, module_suffix[i]) &&
                  (module_prefix == NULL || g_str_has_prefix (member, module_prefix)))
                {
                  g_ptr_array_add (in_this_dir, g_build_filename (module_directory_path, member, NULL));
                }
            }
        }

      g_ptr_array_sort (in_this_dir, _srt_indirect_strcmp0);

      for (gsize j = 0; j < in_this_dir->len; j++)
        {
          gchar *this_driver_link = NULL;
          const gchar *this_driver = g_ptr_array_index (in_this_dir, j);
          issues = _srt_check_library_presence (helpers_path, this_driver, multiarch_tuple,
                                                NULL, NULL, envp, SRT_LIBRARY_SYMBOLS_FORMAT_PLAIN, NULL);
          /* If "${multiarch}-inspect-library" was unable to load the driver, it's safe to assume that
           * its ELF class was not what we were searching for. */
          if (issues & SRT_LIBRARY_ISSUES_CANNOT_LOAD)
            continue;

          switch (module)
            {
              case SRT_GRAPHICS_DRI_MODULE:
                *drivers_out = g_list_prepend (*drivers_out, srt_dri_driver_new (this_driver, is_extra));
                break;

              case SRT_GRAPHICS_VAAPI_MODULE:
                *drivers_out = g_list_prepend (*drivers_out, srt_va_api_driver_new (this_driver, is_extra));
                break;

              case SRT_GRAPHICS_VDPAU_MODULE:
                this_driver_link = g_file_read_link (this_driver, NULL);
                *drivers_out = g_list_prepend (*drivers_out, srt_vdpau_driver_new (this_driver,
                                                                                   this_driver_link,
                                                                                   is_extra));
                g_free (this_driver_link);
                break;

              case SRT_GRAPHICS_GLX_MODULE:
              case NUM_SRT_GRAPHICS_MODULES:
              default:
                g_return_if_reached ();
            }
        }

      g_ptr_array_free (in_this_dir, TRUE);
      g_dir_close (dir);
    }
}

/**
 * _srt_list_modules_from_directory:
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ` was this array
 * @argv: (array zero-terminated=1) (not nullable): The `argv` of the helper to use
 * @tmp_directory: (not nullable) (type filename): Full path to the destination
 *  directory used by the "capsule-capture-libs" helper
 * @known_table: (not optional): set of library names, plus their links, that
 *  we already found. Newely found libraries will be added to this list.
 *  For VDPAU provide a set with just paths where we already looked into, and in
 *  the VDPAU case the set will not be changed by this function.
 * @module: Which graphic module to search
 * @is_extra: If this path should be considered an extra or not. This is used only if
 *  @module is #SRT_GRAPHICS_VDPAU_MODULE.
 * @modules_out: (not optional) (inout): Prepend the found modules to this list.
 *  If @module is #SRT_GRAPHICS_GLX_MODULE, the element-type will be #SrtGlxIcd.
 *  Otherwise if @module is #SRT_GRAPHICS_VDPAU_MODULE, the element-type will be #SrtVdpauDriver.
 *
 * Modules are added to @modules_out in reverse lexicographic order
 * (`libvdpau_r600.so` is before `libvdpau_r300.so`, which is before `libvdpau_nouveau.so`).
 */
static void
_srt_list_modules_from_directory (gchar **envp,
                                  GPtrArray *argv,
                                  const gchar *tmp_directory,
                                  GHashTable *known_table,
                                  SrtGraphicsModule module,
                                  gboolean is_extra,
                                  GList **modules_out)
{
  int exit_status = -1;
  GError *error = NULL;
  gchar *stderr_output = NULL;
  gchar *output = NULL;
  GDir *dir_iter = NULL;
  GPtrArray *members = NULL;
  const gchar *member;
  gchar *full_path = NULL;
  gchar *driver_path = NULL;
  gchar *driver_directory = NULL;
  gchar *driver_link = NULL;
  gchar *soname_path = NULL;

  g_return_if_fail (argv != NULL);
  g_return_if_fail (envp != NULL);
  g_return_if_fail (tmp_directory != NULL);
  g_return_if_fail (known_table != NULL);
  g_return_if_fail (modules_out != NULL);

  if (!g_spawn_sync (NULL,    /* working directory */
                     (gchar **) argv->pdata,
                     envp,
                     G_SPAWN_SEARCH_PATH,       /* flags */
                     _srt_child_setup_unblock_signals,
                     NULL,    /* user data */
                     &output, /* stdout */
                     &stderr_output,
                     &exit_status,
                     &error))
    {
      g_debug ("An error occurred calling the helper: %s", error->message);
      goto out;
    }

  if (exit_status != 0)
    {
      g_debug ("... wait status %d", exit_status);
      goto out;
    }

  dir_iter = g_dir_open (tmp_directory, 0, &error);

  if (dir_iter == NULL)
    {
      g_debug ("Failed to open \"%s\": %s", tmp_directory, error->message);
      goto out;
    }

  members = g_ptr_array_new_with_free_func (g_free);

  while ((member = g_dir_read_name (dir_iter)) != NULL)
    g_ptr_array_add (members, g_strdup (member));

  g_ptr_array_sort (members, _srt_indirect_strcmp0);

  for (gsize i = 0; i < members->len; i++)
    {
      member = g_ptr_array_index (members, i);

      full_path = g_build_filename (tmp_directory, member, NULL);
      driver_path = g_file_read_link (full_path, &error);
      if (driver_path == NULL)
        {
          g_debug ("An error occurred trying to read the symlink: %s", error->message);
          g_free (full_path);
          goto out;
        }
      if (!g_path_is_absolute (driver_path))
        {
          g_debug ("We were expecting an absolute path, instead we have: %s", driver_path);
          g_free (full_path);
          g_free (driver_path);
          goto out;
        }

      switch (module)
        {
          case SRT_GRAPHICS_GLX_MODULE:
            /* Instead of just using just the library name to filter duplicates, we use it in
             * combination with its path. Because in one of the multiple iterations we might
             * find the same library that points to two different locations. And in this
             * case we want to log both of them.
             *
             * `member` cannot contain `/`, so we know we can use `/` to make
             * a composite key for deduplication. */
            soname_path = g_strjoin ("/", member, driver_path, NULL);
            if (!g_hash_table_contains (known_table, soname_path))
              {
                g_hash_table_add (known_table, g_strdup (soname_path));
                *modules_out = g_list_prepend (*modules_out, srt_glx_icd_new (member, driver_path));
              }
            g_free (soname_path);
            break;

          case SRT_GRAPHICS_VDPAU_MODULE:
            driver_directory = g_path_get_dirname (driver_path);
            if (!g_hash_table_contains (known_table, driver_directory))
              {
                /* We do not add `driver_directory` to the hash table because it contains
                 * a list of directories where we already looked into. In this case we are
                 * just adding a single driver instead of searching for all the `libvdpau_*`
                 * files in `driver_directory`. */
                driver_link = g_file_read_link (driver_path, NULL);
                *modules_out = g_list_prepend (*modules_out, srt_vdpau_driver_new (driver_path,
                                                                                  driver_link,
                                                                                  is_extra));
                g_free (driver_link);
              }
            g_free (driver_directory);
            break;

          case SRT_GRAPHICS_DRI_MODULE:
          case SRT_GRAPHICS_VAAPI_MODULE:
          case NUM_SRT_GRAPHICS_MODULES:
          default:
            g_return_if_reached ();
        }

      g_free (full_path);
      g_free (driver_path);
    }

out:
  if (dir_iter != NULL)
    g_dir_close (dir_iter);
  g_clear_pointer (&members, g_ptr_array_unref);
  g_free (output);
  g_free (stderr_output);
  g_clear_error (&error);
}

/**
 * _srt_get_modules_full:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ` was this array
 * @helpers_path: (nullable): An optional path to find "inspect-library" helper, PATH is used if %NULL
 * @multiarch_tuple: (not nullable) (type filename): A Debian-style multiarch tuple
 *  such as %SRT_ABI_X86_64
 * @module: Which graphic module to search
 * @drivers_out: (inout): Prepend the found drivers to this list.
 *  If @module is #SRT_GRAPHICS_DRI_MODULE or #SRT_GRAPHICS_VAAPI_MODULE or
 *  #SRT_GRAPHICS_VDPAU_MODULE, the element-type will be #SrtDriDriver, or
 *  #SrtVaApiDriver or #SrtVdpauDriver, respectively.
 *
 * On exit, `drivers_out` will have the least-preferred directories first and the
 * most-preferred directories last. Within a directory, the drivers will be
 * in reverse lexicographic order: `r600_dri.so` before `r200_dri.so`, which in turn
 * is before `nouveau_dri.so`.
 */
static void
_srt_get_modules_full (const char *sysroot,
                       gchar **envp,
                       const char *helpers_path,
                       const char *multiarch_tuple,
                       SrtGraphicsModule module,
                       GList **drivers_out)
{
  const char * const *loader_libraries;
  static const char *const dri_loaders[] = { "libGLX_mesa.so.0", "libEGL_mesa.so.0",
                                             "libGL.so.1", NULL };
  static const char *const va_api_loaders[] = { "libva.so.2", "libva.so.1", NULL };
  static const char *const vdpau_loaders[] = { "libvdpau.so.1", NULL };
  const gchar *env_override;
  const gchar *drivers_path;
  const gchar *force_elf_class = NULL;
  const gchar *ld_library_path = NULL;
  gchar *flatpak_info;
  gchar *tmp_dir = NULL;
  GHashTable *drivers_set;
  gboolean is_extra = FALSE;
  int driver_class;
  GPtrArray *vdpau_argv = NULL;
  GError *error = NULL;

  g_return_if_fail (envp != NULL);
  g_return_if_fail (multiarch_tuple != NULL);
  g_return_if_fail (drivers_out != NULL);
  g_return_if_fail (sysroot != NULL);
  g_return_if_fail (_srt_check_not_setuid ());

  switch (module)
    {
      case SRT_GRAPHICS_DRI_MODULE:
        loader_libraries = dri_loaders;
        env_override = "LIBGL_DRIVERS_PATH";
        break;

      case SRT_GRAPHICS_VAAPI_MODULE:
        loader_libraries = va_api_loaders;
        env_override = "LIBVA_DRIVERS_PATH";
        break;

      case SRT_GRAPHICS_VDPAU_MODULE:
        loader_libraries = vdpau_loaders;
        env_override = "VDPAU_DRIVER_PATH";
        break;

      case SRT_GRAPHICS_GLX_MODULE:
      case NUM_SRT_GRAPHICS_MODULES:
      default:
        g_return_if_reached ();
    }

  drivers_path = g_environ_getenv (envp, env_override);
  force_elf_class = g_environ_getenv (envp, "SRT_TEST_FORCE_ELF");
  ld_library_path = g_environ_getenv (envp, "LD_LIBRARY_PATH");

  flatpak_info = g_build_filename (sysroot, ".flatpak-info", NULL);
  drivers_set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  if (drivers_path)
    {
      g_debug ("A driver path environment is available: %s", drivers_path);
      gchar **entries;
      /* VDPAU_DRIVER_PATH holds just a single path and not a colon separeted
       * list of paths. Because of that we handle the VDPAU case separately to
       * avoid splitting a theoretically valid path like "/usr/lib/custom_d:r/" */
      if (module == SRT_GRAPHICS_VDPAU_MODULE)
        {
          entries = g_new (gchar*, 2);
          entries[0] = g_strdup (drivers_path);
          entries[1] = NULL;
        }
      else
        {
          entries = g_strsplit (drivers_path, ":", 0);
        }

      for (gchar **entry = entries; entry != NULL && *entry != NULL; entry++)
        {
          if (*entry[0] == '\0')
            continue;

          if (!g_hash_table_contains (drivers_set, *entry))
            {
              g_hash_table_add (drivers_set, g_strdup (*entry));
              _srt_get_modules_from_path (envp, helpers_path, multiarch_tuple, *entry,
                                          FALSE, module, drivers_out);
            }
        }
      g_strfreev (entries);

      /* We continue to search for libraries but we mark them all as "extra" because the
       * loader wouldn't have picked them up. */
      is_extra = TRUE;
    }

  /* If we are in a Flatpak environment we search in the same paths that Flatpak uses,
   * keeping also the same search order.
   *
   * For VA-API these are the paths used:
   * "%{libdir}/dri:%{libdir}/dri/intel-vaapi-driver:%{libdir}/GL/lib/dri"
   * (reference:
   * <https://gitlab.com/freedesktop-sdk/freedesktop-sdk/blob/master/elements/components/libva.bst>)
   *
   * For Mesa there is just a single path:
   * "%{libdir}/GL/lib/dri"
   * (really `GL/default/lib/dri` or `GL/mesa-git/lib/dri`, but `GL/lib/dri` is
   * populated with symbolic links; reference:
   * <https://gitlab.com/freedesktop-sdk/freedesktop-sdk/blob/master/elements/extensions/mesa/mesa.bst>
   * and
   * <https://gitlab.com/freedesktop-sdk/freedesktop-sdk/blob/master/elements/flatpak-images/platform.bst>)
   *
   * For VDPAU there is just a single path:
   * "%{libdir}/vdpau"
   * (reference:
   * <https://gitlab.com/freedesktop-sdk/freedesktop-sdk/blob/master/elements/components/libvdpau.bst>)
   * */
  if (g_file_test (flatpak_info, G_FILE_TEST_EXISTS))
    {
      gchar *libdir = g_build_filename (sysroot, "usr", "lib", multiarch_tuple, NULL);
      if (module == SRT_GRAPHICS_VAAPI_MODULE)
        {
          gchar *libdir_dri = g_build_filename (libdir, "dri", NULL);
          gchar *intel_vaapi = g_build_filename (libdir_dri, "intel-vaapi-driver", NULL);
          if (!g_hash_table_contains (drivers_set, libdir_dri))
            {
              g_hash_table_add (drivers_set, g_strdup (libdir_dri));
              _srt_get_modules_from_path (envp, helpers_path, multiarch_tuple, libdir_dri,
                                          is_extra, module, drivers_out);
            }
          if (!g_hash_table_contains (drivers_set, intel_vaapi))
            {
              g_hash_table_add (drivers_set, g_strdup (intel_vaapi));
              _srt_get_modules_from_path (envp, helpers_path, multiarch_tuple, intel_vaapi,
                                          is_extra, module, drivers_out);
            }
          g_free (libdir_dri);
          g_free (intel_vaapi);
        }

      if (module == SRT_GRAPHICS_VAAPI_MODULE || module == SRT_GRAPHICS_DRI_MODULE)
        {
          gchar *gl_lib_dri = g_build_filename (libdir, "GL", "lib", "dri", NULL);
          if (!g_hash_table_contains (drivers_set, gl_lib_dri))
            {
              g_hash_table_add (drivers_set, g_strdup (gl_lib_dri));
              _srt_get_modules_from_path (envp, helpers_path, multiarch_tuple, gl_lib_dri,
                                          is_extra, module, drivers_out);
            }
          g_free (gl_lib_dri);
        }

      g_free (libdir);

      /* We continue to search for libraries but we mark them all as "extra" because the
       * loader wouldn't have picked them up.
       * The only exception is for VDPAU, becuase in a Flatpak environment the search path
       * is the same as in a non container environment. */
      if (module != SRT_GRAPHICS_VDPAU_MODULE)
        is_extra = TRUE;
    }

  for (gsize i = 0; loader_libraries[i] != NULL; i++)
    {
      SrtLibrary *library_details = NULL;
      char *driver_canonical_path;
      gchar *libdir;
      gchar *libdir_driver;
      GList *extras = NULL;
      SrtLibraryIssues issues;

      issues = _srt_check_library_presence (helpers_path,
                                            loader_libraries[i],
                                            multiarch_tuple,
                                            NULL,   /* symbols path */
                                            NULL,   /* hidden dependencies */
                                            envp,
                                            SRT_LIBRARY_SYMBOLS_FORMAT_PLAIN,
                                            &library_details);

      if (issues & (SRT_LIBRARY_ISSUES_CANNOT_LOAD |
                    SRT_LIBRARY_ISSUES_UNKNOWN |
                    SRT_LIBRARY_ISSUES_TIMEOUT))
        {
          const char *messages = srt_library_get_messages (library_details);

          if (messages == NULL || messages[0] == '\0')
            messages = "(no diagnostic output)";

          g_debug ("Unable to load library %s: %s",
                   loader_libraries[i],
                   messages);
        }

      const gchar *loader_path = srt_library_get_absolute_path (library_details);
      if (loader_path == NULL)
        {
          g_debug ("loader path for %s is NULL", loader_libraries[i]);
          g_object_unref (library_details);
          continue;
        }

      /* The path might still be a symbolic link or it can contains ./ or ../ */
      driver_canonical_path = realpath (loader_path, NULL);
      if (driver_canonical_path == NULL)
        {
          g_debug ("realpath(%s): %s", loader_path, g_strerror (errno));
          g_object_unref (library_details);
          continue;
        }
      libdir = g_path_get_dirname (driver_canonical_path);

      if (module == SRT_GRAPHICS_VDPAU_MODULE)
        libdir_driver = g_build_filename (libdir, "vdpau", NULL);
      else
        libdir_driver = g_build_filename (libdir, "dri", NULL);

      if (!g_hash_table_contains (drivers_set, libdir_driver))
        {
          g_hash_table_add (drivers_set, g_strdup (libdir_driver));
          _srt_get_modules_from_path (envp, helpers_path, multiarch_tuple,
                                      libdir_driver, is_extra, module, drivers_out);
        }

      if (force_elf_class)
        {
          if (g_strcmp0 (force_elf_class, "64") == 0)
            driver_class = ELFCLASS64;
          else
            driver_class = ELFCLASS32;
        }
      else
        {
          driver_class = _srt_get_library_class (driver_canonical_path);
        }

      const GList *this_extra_path;
      if (driver_class != ELFCLASSNONE)
        {
          extras = _srt_get_extra_modules_directory (libdir, multiarch_tuple, driver_class);
          for (this_extra_path = extras; this_extra_path != NULL; this_extra_path = this_extra_path->next)
            {
              if (!g_hash_table_contains (drivers_set, this_extra_path->data))
                {
                  g_hash_table_add (drivers_set, g_strdup (this_extra_path->data));
                  _srt_get_modules_from_path (envp, helpers_path, multiarch_tuple,
                                              this_extra_path->data, TRUE, module,
                                              drivers_out);
                }
            }
        }

      free (driver_canonical_path);
      g_free (libdir);
      g_free (libdir_driver);
      g_object_unref (library_details);
      if (extras)
        g_list_free_full (extras, g_free);
    }



  if (module == SRT_GRAPHICS_VDPAU_MODULE)
    {
      /* VDPAU modules are also loaded by just dlopening the bare filename
       * libvdpau_${VDPAU_DRIVER}.so
       * To cover that we search in all directories listed in LD_LIBRARY_PATH. */
      if (ld_library_path != NULL)
        {
          gchar **entries = g_strsplit (ld_library_path, ":", 0);
          gchar **entry;
          char *entry_realpath;

          for (entry = entries; entry != NULL && *entry != NULL; entry++)
            {
              /* Scripts that manipulate LD_LIBRARY_PATH have a habit of
               * adding empty entries */
              if (*entry[0] == '\0')
                continue;

              entry_realpath = realpath (*entry, NULL);
              if (entry_realpath == NULL)
                {
                  g_debug ("realpath(%s): %s", *entry, g_strerror (errno));
                  continue;
                }
              if (!g_hash_table_contains (drivers_set, entry_realpath))
                {
                  g_hash_table_add (drivers_set, g_strdup (entry_realpath));
                  _srt_get_modules_from_path (envp, helpers_path, multiarch_tuple,
                                              entry_realpath, is_extra, module,
                                              drivers_out);
                }
              free (entry_realpath);
            }
          g_strfreev (entries);
        }

      /* Also use "capsule-capture-libs" to search for VDPAU drivers that we might have
       * missed */
      tmp_dir = g_dir_make_tmp ("vdpau-drivers-XXXXXX", &error);
      if (tmp_dir == NULL)
        {
          g_debug ("An error occurred trying to create a temporary folder: %s", error->message);
          goto out;
        }
      vdpau_argv = _argv_for_list_vdpau_drivers (envp, helpers_path, multiarch_tuple, tmp_dir, &error);
      if (vdpau_argv == NULL)
        {
          g_debug ("An error occurred trying to capture VDPAU drivers: %s", error->message);
          goto out;
        }
      _srt_list_modules_from_directory (envp, vdpau_argv, tmp_dir, drivers_set,
                                        SRT_GRAPHICS_VDPAU_MODULE, is_extra, drivers_out);

      /* Debian used to hardcode "/usr/lib/vdpau" as an additional search path for VDPAU.
       * However since libvdpau 1.3-1 it has been removed; reference:
       * <https://salsa.debian.org/nvidia-team/libvdpau/commit/11a3cd84>
       * Just to be sure to not miss a potentially valid library path we search on it
       * unconditionally, flagging it as extra. */
      gchar *debian_additional = g_build_filename (sysroot, "usr", "lib", "vdpau", NULL);
      if (!g_hash_table_contains (drivers_set, debian_additional))
        {
          _srt_get_modules_from_path (envp, helpers_path, multiarch_tuple,
                                      debian_additional, TRUE, module,
                                      drivers_out);
        }
      g_free (debian_additional);
    }

out:
  g_clear_pointer (&vdpau_argv, g_ptr_array_unref);
  if (tmp_dir)
    {
      if (!_srt_rm_rf (tmp_dir))
        g_debug ("Unable to remove the temporary directory: %s", tmp_dir);
    }
  g_free (tmp_dir);
  g_hash_table_unref (drivers_set);
  g_free (flatpak_info);
  g_clear_error (&error);
}

/*
 * _srt_list_glx_icds:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ` was this array
 * @helpers_path: (nullable): An optional path to find "capsule-capture-libs" helper,
 *  PATH is used if %NULL
 * @multiarch_tuple: (not nullable) (type filename): A Debian-style multiarch tuple
 *  such as %SRT_ABI_X86_64
 * @drivers_out: (inout): Prepend the found drivers to this list as opaque
 *  #SrtGlxIcd objects. There is no guarantee about the order of the list
 *
 * Implementation of srt_system_info_list_glx_icds().
 */
static void
_srt_list_glx_icds (const char *sysroot,
                    gchar **envp,
                    const char *helpers_path,
                    const char *multiarch_tuple,
                    GList **drivers_out)
{
  GPtrArray *by_soname_argv = NULL;
  GPtrArray *overrides_argv = NULL;
  GError *error = NULL;
  gchar *by_soname_tmp_dir = NULL;
  gchar *overrides_tmp_dir = NULL;
  gchar *overrides_path = NULL;
  GHashTable *known_libs = NULL;

  g_return_if_fail (sysroot != NULL);
  g_return_if_fail (multiarch_tuple != NULL);
  g_return_if_fail (drivers_out != NULL);
  g_return_if_fail (_srt_check_not_setuid ());

  known_libs = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  by_soname_tmp_dir = g_dir_make_tmp ("glx-icds-XXXXXX", &error);
  if (by_soname_tmp_dir == NULL)
    {
      g_debug ("An error occurred trying to create a temporary folder: %s", error->message);
      goto out;
    }

  by_soname_argv = _argv_for_list_glx_icds (helpers_path, multiarch_tuple, by_soname_tmp_dir, &error);

  if (by_soname_argv == NULL)
    {
      g_debug ("An error occurred trying to capture glx ICDs: %s", error->message);
      goto out;
    }

  _srt_list_modules_from_directory (envp, by_soname_argv, by_soname_tmp_dir, known_libs,
                                    SRT_GRAPHICS_GLX_MODULE, FALSE, drivers_out);

  /* When in a container we might miss valid GLX drivers because the `ld.so.cache` in
   * use doesn't have a reference about them. To fix that we also include every
   * "libGLX_*.so.*" libraries that we find in the "/overrides/lib/${multiarch}" folder */
  overrides_path = g_build_filename (sysroot, "/overrides", NULL);
  if (g_file_test (overrides_path, G_FILE_TEST_IS_DIR))
    {
      overrides_tmp_dir = g_dir_make_tmp ("glx-icds-XXXXXX", &error);
      if (overrides_tmp_dir == NULL)
        {
          g_debug ("An error occurred trying to create a temporary folder: %s", error->message);
          goto out;
        }

      overrides_argv = _argv_for_list_glx_icds_in_path (helpers_path, multiarch_tuple, overrides_tmp_dir, overrides_path, &error);

      if (overrides_argv == NULL)
        {
          g_debug ("An error occurred trying to capture glx ICDs: %s", error->message);
          goto out;
        }

      _srt_list_modules_from_directory (envp, overrides_argv, overrides_tmp_dir, known_libs,
                                        SRT_GRAPHICS_GLX_MODULE, FALSE, drivers_out);
    }

out:
  g_clear_pointer (&by_soname_argv, g_ptr_array_unref);
  g_clear_pointer (&overrides_argv, g_ptr_array_unref);
  if (by_soname_tmp_dir)
    {
      if (!_srt_rm_rf (by_soname_tmp_dir))
        g_debug ("Unable to remove the temporary directory: %s", by_soname_tmp_dir);
    }
  if (overrides_tmp_dir)
    {
      if (!_srt_rm_rf (overrides_tmp_dir))
        g_debug ("Unable to remove the temporary directory: %s", overrides_tmp_dir);
    }
  g_free (by_soname_tmp_dir);
  g_free (overrides_tmp_dir);
  g_free (overrides_path);
  g_hash_table_unref (known_libs);
  g_clear_error (&error);
}

/**
 * _srt_list_graphics_modules:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ` was this array
 * @helpers_path: (nullable): An optional path to find "inspect-library" helper, PATH is used if %NULL
 * @multiarch_tuple: (not nullable) (type filename): A Debian-style multiarch tuple
 *  such as %SRT_ABI_X86_64
 * @which: Graphics modules to look for
 *
 * Implementation of srt_system_info_list_dri_drivers() etc.
 *
 * The returned list for GLX modules is in an unspecified order.
 *
 * Instead the returned list for all the other graphics modules will have the
 * most-preferred directories first and the least-preferred directories last.
 * Within a directory, the drivers will be in lexicographic order, for example
 * `nouveau_dri.so`, `r200_dri.so`, `r600_dri.so` in that order.
 *
 * Returns: (transfer full) (element-type GObject) (nullable): A list of
 *  opaque #SrtDriDriver, etc. objects, or %NULL if nothing was found. Free with
 *  `g_list_free_full(list, g_object_unref)`.
 */
GList *
_srt_list_graphics_modules (const char *sysroot,
                            gchar **envp,
                            const char *helpers_path,
                            const char *multiarch_tuple,
                            SrtGraphicsModule which)
{
  GList *drivers = NULL;

  g_return_val_if_fail (sysroot != NULL, NULL);
  g_return_val_if_fail (multiarch_tuple != NULL, NULL);

  if (which == SRT_GRAPHICS_GLX_MODULE)
    _srt_list_glx_icds (sysroot, envp, helpers_path, multiarch_tuple, &drivers);
  else
    _srt_get_modules_full (sysroot, envp, helpers_path, multiarch_tuple, which,
                           &drivers);

  return g_list_reverse (drivers);
}

/**
 * SrtVaApiDriver:
 *
 * Opaque object representing a VA-API driver.
 */

struct _SrtVaApiDriver
{
  /*< private >*/
  GObject parent;
  gchar *library_path;
  gboolean is_extra;
};

struct _SrtVaApiDriverClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum
{
  VA_API_DRIVER_PROP_0,
  VA_API_DRIVER_PROP_LIBRARY_PATH,
  VA_API_DRIVER_PROP_IS_EXTRA,
  VA_API_DRIVER_PROP_RESOLVED_LIBRARY_PATH,
  N_VA_API_DRIVER_PROPERTIES
};

G_DEFINE_TYPE (SrtVaApiDriver, srt_va_api_driver, G_TYPE_OBJECT)

static void
srt_va_api_driver_init (SrtVaApiDriver *self)
{
}

static void
srt_va_api_driver_get_property (GObject *object,
                                guint prop_id,
                                GValue *value,
                                GParamSpec *pspec)
{
  SrtVaApiDriver *self = SRT_VA_API_DRIVER (object);

  switch (prop_id)
    {
      case VA_API_DRIVER_PROP_LIBRARY_PATH:
        g_value_set_string (value, self->library_path);
        break;

      case VA_API_DRIVER_PROP_IS_EXTRA:
        g_value_set_boolean (value, self->is_extra);
        break;

      case VA_API_DRIVER_PROP_RESOLVED_LIBRARY_PATH:
        g_value_take_string (value, srt_va_api_driver_resolve_library_path (self));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_va_api_driver_set_property (GObject *object,
                                guint prop_id,
                                const GValue *value,
                                GParamSpec *pspec)
{
  SrtVaApiDriver *self = SRT_VA_API_DRIVER (object);

  switch (prop_id)
    {
      case VA_API_DRIVER_PROP_LIBRARY_PATH:
        g_return_if_fail (self->library_path == NULL);
        self->library_path = g_value_dup_string (value);
        break;

      case VA_API_DRIVER_PROP_IS_EXTRA:
        self->is_extra = g_value_get_boolean (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_va_api_driver_finalize (GObject *object)
{
  SrtVaApiDriver *self = SRT_VA_API_DRIVER (object);

  g_clear_pointer (&self->library_path, g_free);

  G_OBJECT_CLASS (srt_va_api_driver_parent_class)->finalize (object);
}

static GParamSpec *va_api_driver_properties[N_VA_API_DRIVER_PROPERTIES] = { NULL };

static void
srt_va_api_driver_class_init (SrtVaApiDriverClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_va_api_driver_get_property;
  object_class->set_property = srt_va_api_driver_set_property;
  object_class->finalize = srt_va_api_driver_finalize;

  va_api_driver_properties[VA_API_DRIVER_PROP_LIBRARY_PATH] =
    g_param_spec_string ("library-path", "Library path",
                         "Path to the DRI driver library. It might be absolute "
                         "(e.g. /usr/lib/dri/iHD_drv_video.so) or relative "
                         "(e.g. custom/dri/iHD_drv_video.so)",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  va_api_driver_properties[VA_API_DRIVER_PROP_IS_EXTRA] =
    g_param_spec_boolean ("is-extra", "Is extra?",
                          "TRUE if the driver is located in an unusual path",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  va_api_driver_properties[VA_API_DRIVER_PROP_RESOLVED_LIBRARY_PATH] =
    g_param_spec_string ("resolved-library-path", "Resolved library path",
                         "Absolute path to the DRI driver library. This is similar "
                         "to 'library-path', but is guaranteed to be an "
                         "absolute path (e.g. /usr/lib/dri/iHD_drv_video.so)",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_VA_API_DRIVER_PROPERTIES,
                                     va_api_driver_properties);
}

/**
 * srt_va_api_driver_new:
 * @library_path: (transfer none): the path to the library
 * @is_extra: if the DRI driver is in an unusual path
 *
 * Returns: (transfer full): a new VA-API driver
 */
static SrtVaApiDriver *
srt_va_api_driver_new (const gchar *library_path,
                       gboolean is_extra)
{
  g_return_val_if_fail (library_path != NULL, NULL);

  return g_object_new (SRT_TYPE_VA_API_DRIVER,
                       "library-path", library_path,
                       "is-extra", is_extra,
                       NULL);
}

/**
 * srt_va_api_driver_get_library_path:
 * @self: The VA-API driver
 *
 * Return the library path for this VA-API driver.
 *
 * Returns: (type filename) (transfer none): #SrtVaApiDriver:library-path
 */
const gchar *
srt_va_api_driver_get_library_path (SrtVaApiDriver *self)
{
  g_return_val_if_fail (SRT_IS_VA_API_DRIVER (self), NULL);
  return self->library_path;
}

/**
 * srt_va_api_driver_is_extra:
 * @self: The VA-API driver
 *
 * Return a gboolean that indicates if the VA-API is in an unusual position.
 *
 * Returns: %TRUE if the VA-API driver is in an unusual position.
 */
gboolean
srt_va_api_driver_is_extra (SrtVaApiDriver *self)
{
  g_return_val_if_fail (SRT_IS_VA_API_DRIVER (self), FALSE);
  return self->is_extra;
}

/**
 * srt_va_api_driver_resolve_library_path:
 * @self: The VA-API driver
 *
 * Return the absolute library path for this VA-API driver.
 * If srt_va_api_driver_get_library_path() is already an absolute path, a copy
 * of the same value will be returned.
 *
 * Returns: (type filename) (transfer full): A copy of
 *  #SrtVaApiDriver:resolved-library-path. Free with g_free().
 */
gchar *
srt_va_api_driver_resolve_library_path (SrtVaApiDriver *self)
{
  g_return_val_if_fail (SRT_IS_VA_API_DRIVER (self), NULL);
  g_return_val_if_fail (self->library_path != NULL, NULL);

  return _srt_resolve_library_path (self->library_path);
}

/**
 * _srt_va_api_driver_get_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "va-api_drivers"
 *  property
 *
 * If the provided @json_obj doesn't have a "va-api_drivers" member, or it is
 * malformed, %NULL will be returned.
 *
 * Returns: A list of #SrtVaApiDriver that have been found, or %NULL if none
 *  has been found.
 */
GList *
_srt_va_api_driver_get_from_report (JsonObject *json_obj)
{
  JsonArray *array;
  GList *va_api_drivers = NULL;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (json_object_has_member (json_obj, "va-api_drivers"))
    {
      array = json_object_get_array_member (json_obj, "va-api_drivers");

      if (array == NULL)
        goto out;

      guint length = json_array_get_length (array);
      for (guint j = 0; j < length; j++)
        {
          const gchar *va_api_path = NULL;
          gboolean is_extra = FALSE;
          JsonObject *json_va_api_obj = json_array_get_object_element (array, j);
          va_api_path = json_object_get_string_member_with_default (json_va_api_obj,
                                                                    "library_path",
                                                                    NULL);
          is_extra = json_object_get_boolean_member_with_default (json_va_api_obj,
                                                                  "is_extra",
                                                                  FALSE);

          va_api_drivers = g_list_prepend (va_api_drivers, srt_va_api_driver_new (va_api_path, is_extra));
        }
    }

out:
  return g_list_reverse (va_api_drivers);
}

/**
 * SrtVdpauDriver:
 *
 * Opaque object representing a VDPAU driver.
 */

struct _SrtVdpauDriver
{
  /*< private >*/
  GObject parent;
  gchar *library_path;
  gchar *library_link;
  gboolean is_extra;
};

struct _SrtVdpauDriverClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum
{
  VDPAU_DRIVER_PROP_0,
  VDPAU_DRIVER_PROP_LIBRARY_PATH,
  VDPAU_DRIVER_PROP_LIBRARY_LINK,
  VDPAU_DRIVER_PROP_IS_EXTRA,
  VDPAU_DRIVER_PROP_RESOLVED_LIBRARY_PATH,
  N_VDPAU_DRIVER_PROPERTIES
};

G_DEFINE_TYPE (SrtVdpauDriver, srt_vdpau_driver, G_TYPE_OBJECT)

static void
srt_vdpau_driver_init (SrtVdpauDriver *self)
{
}

static void
srt_vdpau_driver_get_property (GObject *object,
                               guint prop_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  SrtVdpauDriver *self = SRT_VDPAU_DRIVER (object);

  switch (prop_id)
    {
      case VDPAU_DRIVER_PROP_LIBRARY_PATH:
        g_value_set_string (value, self->library_path);
        break;

      case VDPAU_DRIVER_PROP_LIBRARY_LINK:
        g_value_set_string (value, self->library_link);
        break;

      case VDPAU_DRIVER_PROP_IS_EXTRA:
        g_value_set_boolean (value, self->is_extra);
        break;

      case VDPAU_DRIVER_PROP_RESOLVED_LIBRARY_PATH:
        g_value_take_string (value, srt_vdpau_driver_resolve_library_path (self));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_vdpau_driver_set_property (GObject *object,
                               guint prop_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  SrtVdpauDriver *self = SRT_VDPAU_DRIVER (object);

  switch (prop_id)
    {
      case VDPAU_DRIVER_PROP_LIBRARY_PATH:
        g_return_if_fail (self->library_path == NULL);
        self->library_path = g_value_dup_string (value);
        break;

      case VDPAU_DRIVER_PROP_LIBRARY_LINK:
        g_return_if_fail (self->library_link == NULL);
        self->library_link = g_value_dup_string (value);
        break;

      case VDPAU_DRIVER_PROP_IS_EXTRA:
        self->is_extra = g_value_get_boolean (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_vdpau_driver_finalize (GObject *object)
{
  SrtVdpauDriver *self = SRT_VDPAU_DRIVER (object);

  g_clear_pointer (&self->library_path, g_free);
  g_clear_pointer (&self->library_link, g_free);

  G_OBJECT_CLASS (srt_vdpau_driver_parent_class)->finalize (object);
}

static GParamSpec *vdpau_driver_properties[N_VDPAU_DRIVER_PROPERTIES] = { NULL };

static void
srt_vdpau_driver_class_init (SrtVdpauDriverClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_vdpau_driver_get_property;
  object_class->set_property = srt_vdpau_driver_set_property;
  object_class->finalize = srt_vdpau_driver_finalize;

  vdpau_driver_properties[VDPAU_DRIVER_PROP_LIBRARY_PATH] =
    g_param_spec_string ("library-path", "Library path",
                         "Path to the VDPAU driver library. It might be absolute "
                         "(e.g. /usr/lib/vdpau/libvdpau_radeonsi.so) or relative "
                         "(e.g. custom/vdpau/libvdpau_radeonsi.so)",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vdpau_driver_properties[VDPAU_DRIVER_PROP_LIBRARY_LINK] =
    g_param_spec_string ("library-link", "Library symlink contents",
                         "Contents of the symbolik link of the VDPAU driver library",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vdpau_driver_properties[VDPAU_DRIVER_PROP_IS_EXTRA] =
    g_param_spec_boolean ("is-extra", "Is extra?",
                          "TRUE if the driver is located in an unusual path",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  vdpau_driver_properties[VDPAU_DRIVER_PROP_RESOLVED_LIBRARY_PATH] =
    g_param_spec_string ("resolved-library-path", "Resolved library path",
                         "Absolute path to the VDPAU driver library. This is similar "
                         "to 'library-path', but is guaranteed to be an "
                         "absolute path (e.g. /usr/lib/vdpau/libvdpau_radeonsi.so)",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_VDPAU_DRIVER_PROPERTIES,
                                     vdpau_driver_properties);
}

/**
 * srt_vdpau_driver_new:
 * @library_path: (transfer none): the path to the library
 * @library_link: (transfer none) (nullable): the content of the library symlink
 * @is_extra: if the VDPAU driver is in an unusual path
 *
 * Returns: (transfer full): a new VDPAU driver
 */
static SrtVdpauDriver *
srt_vdpau_driver_new (const gchar *library_path,
                      const gchar *library_link,
                      gboolean is_extra)
{
  g_return_val_if_fail (library_path != NULL, NULL);

  return g_object_new (SRT_TYPE_VDPAU_DRIVER,
                       "library-path", library_path,
                       "library-link", library_link,
                       "is-extra", is_extra,
                       NULL);
}

/**
 * srt_vdpau_driver_get_library_path:
 * @self: The VDPAU driver
 *
 * Return the library path for this VDPAU driver.
 *
 * Returns: (type filename) (transfer none): #SrtVdpauDriver:library-path
 */
const gchar *
srt_vdpau_driver_get_library_path (SrtVdpauDriver *self)
{
  g_return_val_if_fail (SRT_IS_VDPAU_DRIVER (self), NULL);
  return self->library_path;
}

/**
 * srt_vdpau_driver_get_library_link:
 * @self: The VDPAU driver
 *
 * Return the content of the symbolic link for this VDPAU driver or %NULL
 * if the library path is not a symlink.
 *
 * Returns: (type filename) (transfer none) (nullable): #SrtVdpauDriver:library-link
 */
const gchar *
srt_vdpau_driver_get_library_link (SrtVdpauDriver *self)
{
  g_return_val_if_fail (SRT_IS_VDPAU_DRIVER (self), NULL);
  return self->library_link;
}

/**
 * srt_vdpau_driver_is_extra:
 * @self: The VDPAU driver
 *
 * Return a gboolean that indicates if the VDPAU is in an unusual position.
 *
 * Returns: %TRUE if the VDPAU driver is in an unusual position.
 */
gboolean
srt_vdpau_driver_is_extra (SrtVdpauDriver *self)
{
  g_return_val_if_fail (SRT_IS_VDPAU_DRIVER (self), FALSE);
  return self->is_extra;
}

/**
 * srt_vdpau_driver_resolve_library_path:
 * @self: The VDPAU driver
 *
 * Return the absolute library path for this VDPAU driver.
 * If srt_vdpau_driver_get_library_path() is already an absolute path, a copy
 * of the same value will be returned.
 *
 * Returns: (type filename) (transfer full): A copy of
 *  #SrtVdpauDriver:resolved-library-path. Free with g_free().
 */
gchar *
srt_vdpau_driver_resolve_library_path (SrtVdpauDriver *self)
{
  g_return_val_if_fail (SRT_IS_VDPAU_DRIVER (self), NULL);
  g_return_val_if_fail (self->library_path != NULL, NULL);

  return _srt_resolve_library_path (self->library_path);
}

/**
 * _srt_vdpau_driver_get_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "vdpau_drivers"
 *  property
 *
 * If the provided @json_obj doesn't have a "vdpau_drivers" member, or it is
 * malformed, %NULL will be returned.
 *
 * Returns: A list of #SrtVdpauDriver that have been found, or %NULL if none
 *  has been found.
 */
GList *
_srt_vdpau_driver_get_from_report (JsonObject *json_obj)
{
  JsonArray *array;
  GList *vdpau_drivers = NULL;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (json_object_has_member (json_obj, "vdpau_drivers"))
    {
      array = json_object_get_array_member (json_obj, "vdpau_drivers");

      if (array == NULL)
        goto out;

      guint length = json_array_get_length (array);
      for (guint j = 0; j < length; j++)
        {
          const gchar *vdpau_path = NULL;
          const gchar *vdpau_link = NULL;
          gboolean is_extra = FALSE;
          JsonObject *json_vdpau_obj = json_array_get_object_element (array, j);
          vdpau_path = json_object_get_string_member_with_default (json_vdpau_obj, "library_path",
                                                                   NULL);
          vdpau_link = json_object_get_string_member_with_default (json_vdpau_obj, "library_link",
                                                                   NULL);
          is_extra = json_object_get_boolean_member_with_default (json_vdpau_obj, "is_extra",
                                                                  FALSE);

          vdpau_drivers = g_list_prepend (vdpau_drivers, srt_vdpau_driver_new (vdpau_path, vdpau_link, is_extra));
        }
    }

out:
  return g_list_reverse (vdpau_drivers);
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

      case VULKAN_ICD_PROP_JSON_PATH:
        g_return_if_fail (self->icd.json_path == NULL);
        tmp = g_value_get_string (value);

        if (g_path_is_absolute (tmp))
          {
            self->icd.json_path = g_strdup (tmp);
          }
        else
          {
            gchar *cwd = g_get_current_dir ();

            self->icd.json_path = g_build_filename (cwd, tmp, NULL);
            g_free (cwd);
          }
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

  vulkan_icd_properties[VULKAN_ICD_PROP_JSON_PATH] =
    g_param_spec_string ("json-path", "JSON path",
                         "Absolute path to JSON file describing this ICD. "
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
                         "(e.g. /opt/vulkan/libvulkan_myvendor.so)",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  vulkan_icd_properties[VULKAN_ICD_PROP_RESOLVED_LIBRARY_PATH] =
    g_param_spec_string ("resolved-library-path", "Resolved library path",
                         "Library implementing this ICD, expressed as a "
                         "basename to be searched for in the default "
                         "library search path (e.g. libvulkan_myvendor.so) "
                         "or an absolute path "
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
 *
 * Returns: (transfer full): a new ICD
 */
static SrtVulkanIcd *
srt_vulkan_icd_new (const gchar *json_path,
                    const gchar *api_version,
                    const gchar *library_path)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (api_version != NULL, NULL);
  g_return_val_if_fail (library_path != NULL, NULL);

  return g_object_new (SRT_TYPE_VULKAN_ICD,
                       "api-version", api_version,
                       "json-path", json_path,
                       "library-path", library_path,
                       NULL);
}

/**
 * srt_vulkan_icd_new_error:
 * @error: (transfer none): Error that occurred when loading the ICD
 *
 * Returns: (transfer full): a new ICD
 */
static SrtVulkanIcd *
srt_vulkan_icd_new_error (const gchar *json_path,
                          const GError *error)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (error != NULL, NULL);

  return g_object_new (SRT_TYPE_VULKAN_ICD,
                       "error", error,
                       "json-path", json_path,
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
                             path);
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

  g_return_if_fail (list != NULL);

  if (!g_path_is_absolute (filename))
    {
      canon = g_canonicalize_filename (filename, NULL);
      filename = canon;
    }

  in_sysroot = g_build_filename (sysroot, filename, NULL);

  if (load_json (SRT_TYPE_VULKAN_ICD, in_sysroot,
                 &api_version, &library_path, &error))
    {
      g_assert (api_version != NULL);
      g_assert (library_path != NULL);
      g_assert (error == NULL);
      *list = g_list_prepend (*list,
                              srt_vulkan_icd_new (filename,
                                                  api_version,
                                                  library_path));
    }
  else
    {
      g_assert (api_version == NULL);
      g_assert (library_path == NULL);
      g_assert (error != NULL);
      *list = g_list_prepend (*list,
                              srt_vulkan_icd_new_error (filename, error));
    }
}

static void
vulkan_icd_load_json_cb (const char *sysroot,
                         const char *filename,
                         void *user_data)
{
  vulkan_icd_load_json (sysroot, filename, user_data);
}

#define VULKAN_ICD_SUFFIX "vulkan/icd.d"

/*
 * Return the ${sysconfdir} that we assume the Vulkan loader has.
 * See get_glvnd_sysconfdir().
 */
static const char *
get_vulkan_sysconfdir (void)
{
  return "/etc";
}

static gchar **
get_vulkan_search_paths (const char *sysroot,
                         gchar **envp,
                         const char * const *multiarch_tuples,
                         const char *suffix)
{
  GPtrArray *search_paths = g_ptr_array_new ();
  g_auto(GStrv) dirs = NULL;
  g_autofree gchar *flatpak_info = NULL;
  const gchar *value;
  gsize i;

  /* The reference Vulkan loader doesn't entirely follow
   * https://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html:
   * it skips XDG_CONFIG_HOME and goes directly to XDG_CONFIG_DIRS.
   * https://github.com/KhronosGroup/Vulkan-Loader/issues/246 */
  value = g_environ_getenv (envp, "XDG_CONFIG_DIRS");

  /* Constant and non-configurable fallback, as per
   * https://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html */
  if (value == NULL)
    value = "/etc/xdg";

  dirs = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);
  for (i = 0; dirs[i] != NULL; i++)
    g_ptr_array_add (search_paths, g_build_filename (dirs[i], suffix, NULL));

  g_clear_pointer (&dirs, g_strfreev);

  value = get_vulkan_sysconfdir ();
  g_ptr_array_add (search_paths, g_build_filename (value, suffix, NULL));

  /* This is hard-coded in the reference loader: if its own sysconfdir
   * is not /etc, it searches /etc afterwards. (In practice this
   * won't trigger at the moment, because we assume the Vulkan
   * loader's sysconfdir *is* /etc.) */
  if (g_strcmp0 (value, "/etc") != 0)
    g_ptr_array_add (search_paths, g_build_filename ("/etc", suffix, NULL));

  flatpak_info = g_build_filename (sysroot, ".flatpak-info", NULL);

  /* freedesktop-sdk patches the Vulkan loader to look here for ICDs. */
  if (g_file_test (flatpak_info, G_FILE_TEST_EXISTS)
      && multiarch_tuples != NULL)
    {
      g_debug ("Flatpak detected: assuming freedesktop-based runtime");

      for (i = 0; multiarch_tuples[i] != NULL; i++)
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
    }

  /* The reference Vulkan loader doesn't entirely follow
   * https://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html:
   * it searches XDG_DATA_HOME *after* XDG_DATA_DIRS, and it still
   * searches ~/.local/share even if XDG_DATA_HOME is set.
   * https://github.com/KhronosGroup/Vulkan-Loader/issues/245 */

  value = g_environ_getenv (envp, "XDG_DATA_DIRS");

  /* Constant and non-configurable fallback, as per
   * https://standards.freedesktop.org/basedir-spec/basedir-spec-latest.html */
  if (value == NULL)
    value = "/usr/local/share" G_SEARCHPATH_SEPARATOR_S "/usr/share";

  dirs = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);
  for (i = 0; dirs[i] != NULL; i++)
    g_ptr_array_add (search_paths, g_build_filename (dirs[i], suffix, NULL));

  /* I don't know why this is searched *after* XDG_DATA_DIRS in the
   * reference loader, but we match that behaviour. */
  value = g_environ_getenv (envp, "XDG_DATA_HOME");
  if (value != NULL)
    g_ptr_array_add (search_paths, g_build_filename (value, suffix, NULL));

  /* libvulkan searches this unconditionally, even if XDG_DATA_HOME
   * is set. */
  value = g_environ_getenv (envp, "HOME");

  if (value == NULL)
    value = g_get_home_dir ();

  g_ptr_array_add (search_paths, g_build_filename (value, ".local", "share",
                                                   suffix, NULL));

  g_ptr_array_add (search_paths, NULL);

  return (GStrv) g_ptr_array_free (search_paths, FALSE);
}

/*
 * _srt_load_vulkan_icds:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ` was this
 *  array
 * @multiarch_tuples: (nullable): If not %NULL, and a Flatpak environment
 *  is detected, assume a freedesktop-sdk-based runtime and look for
 *  GL extensions for these multiarch tuples
 *
 * Implementation of srt_system_info_list_vulkan_icds().
 *
 * Returns: (transfer full) (element-type SrtVulkanIcd): A list of ICDs,
 *  most-important first
 */
GList *
_srt_load_vulkan_icds (const char *sysroot,
                       gchar **envp,
                       const char * const *multiarch_tuples)
{
  const gchar *value;
  gsize i;
  /* To avoid O(n**2) performance, we build this list in reverse order,
   * then reverse it at the end. */
  GList *ret = NULL;

  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail (envp != NULL, NULL);

  /* See
   * https://github.com/KhronosGroup/Vulkan-Loader/blob/master/loader/LoaderAndLayerInterface.md#icd-manifest-file-format
   * for more details of the search order - but beware that the
   * documentation is not completely up to date (as of September 2019)
   * so you should also look at the reference implementation. */

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
      g_auto(GStrv) search_paths = get_vulkan_search_paths (sysroot, envp, multiarch_tuples,
                                                            VULKAN_ICD_SUFFIX);
      load_json_dirs (sysroot, search_paths, NULL, READDIR_ORDER,
                      vulkan_icd_load_json_cb, &ret);
    }

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

      case VULKAN_LAYER_PROP_JSON_PATH:
        g_return_if_fail (self->layer.json_path == NULL);
        tmp = g_value_get_string (value);

        if (g_path_is_absolute (tmp))
          {
            self->layer.json_path = g_strdup (tmp);
          }
        else
          {
            g_autofree gchar *cwd = g_get_current_dir ();
            self->layer.json_path = g_build_filename (cwd, tmp, NULL);
          }
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

  vulkan_layer_properties[VULKAN_LAYER_PROP_JSON_PATH] =
    g_param_spec_string ("json-path", "JSON path",
                         "Absolute path to JSON file describing this layer. "
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
                      GStrv component_layers)
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
                       NULL);
}

/**
 * srt_vulkan_layer_new:
 * @json_path: (transfer none): the absolute path to the JSON file
 * @error: (transfer none): Error that occurred when loading the layer
 *
 * Returns: (transfer full): a new SrtVulkanLayer
 */
static SrtVulkanLayer *
srt_vulkan_layer_new_error (const gchar *json_path,
                            const GError *error)
{
  g_return_val_if_fail (json_path != NULL, NULL);
  g_return_val_if_fail (error != NULL, NULL);

  return g_object_new (SRT_TYPE_VULKAN_LAYER,
                       "error", error,
                       "json-path", json_path,
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
      return srt_vulkan_layer_new_error (path, error);
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
      return srt_vulkan_layer_new_error (path, error);
    }

  vulkan_layer = srt_vulkan_layer_new (path, name, type, library_path, api_version,
                                       implementation_version, description, component_layers);

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
load_vulkan_layer_json (const gchar *path)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(JsonParser) parser = NULL;
  JsonNode *node = NULL;
  JsonNode *arr_node = NULL;
  JsonObject *object = NULL;
  JsonObject *json_layer = NULL;
  JsonArray *json_layers = NULL;
  const gchar *file_format_version = NULL;
  guint length;
  gsize i;
  GList *ret_list = NULL;

  g_return_val_if_fail (path != NULL, NULL);

  g_debug ("Attempting to load the json layer from %s", path);

  parser = json_parser_new ();

  if (!json_parser_load_from_file (parser, path, &error))
    {
      g_debug ("error %s", error->message);
      return g_list_prepend (ret_list, srt_vulkan_layer_new_error (path, error));
    }

  node = json_parser_get_root (parser);

  if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Expected to find a JSON object in \"%s\"", path);
      return g_list_prepend (ret_list, srt_vulkan_layer_new_error (path, error));
    }

  object = json_node_get_object (node);

  file_format_version = json_object_get_string_member_with_default (object,
                                                                    "file_format_version",
                                                                    NULL);

  if (file_format_version == NULL)
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "file_format_version in \"%s\" is missing or not a string", path);
      return g_list_prepend (ret_list, srt_vulkan_layer_new_error (path, error));
    }

  /* At the time of writing the latest layer manifest file version is the
   * 1.1.2 and forward compatibility is not guaranteed */
  if (g_strcmp0 (file_format_version, "1.0.0") == 0 ||
      g_strcmp0 (file_format_version, "1.0.1") == 0 ||
      g_strcmp0 (file_format_version, "1.1.0") == 0 ||
      g_strcmp0 (file_format_version, "1.1.1") == 0 ||
      g_strcmp0 (file_format_version, "1.1.2") == 0)
    {
      g_debug ("file_format_version is \"%s\"", file_format_version);
    }
  else
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "Vulkan layer file_format_version \"%s\" in \"%s\" is not supported",
                   file_format_version, path);
      return g_list_prepend (ret_list, srt_vulkan_layer_new_error (path, error));
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
          return g_list_prepend (ret_list, srt_vulkan_layer_new_error (path, error));
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
              ret_list = g_list_prepend (ret_list, srt_vulkan_layer_new_error (path, error));
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
          return g_list_prepend (ret_list, srt_vulkan_layer_new_error (path, error));
        }
      ret_list = g_list_prepend (ret_list, vulkan_layer_parse_json (path, file_format_version,
                                                                    json_layer));
    }
  else
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "The layer definitions in \"%s\" is missing both the \"layer\" and \"layers\" fields",
                   path);
      return g_list_prepend (ret_list, srt_vulkan_layer_new_error (path, error));
    }
  return ret_list;
}

static void
vulkan_layer_load_json (const char *sysroot,
                        const char *filename,
                        GList **list)
{
  g_autofree gchar *canon = NULL;
  g_autofree gchar *in_sysroot = NULL;

  g_return_if_fail (sysroot != NULL);
  g_return_if_fail (list != NULL);

  if (!g_path_is_absolute (filename))
    {
      canon = g_canonicalize_filename (filename, NULL);
      filename = canon;
    }

  in_sysroot = g_build_filename (sysroot, filename, NULL);

  *list = g_list_concat (load_vulkan_layer_json (in_sysroot), *list);
}

static void
vulkan_layer_load_json_cb (const char *sysroot,
                           const char *filename,
                           void *user_data)
{
  vulkan_layer_load_json (sysroot, filename, user_data);
}

#define EXPLICIT_VULKAN_LAYER_SUFFIX "vulkan/explicit_layer.d"
#define IMPLICIT_VULKAN_LAYER_SUFFIX "vulkan/implicit_layer.d"

/*
 * _srt_load_vulkan_layers:
 * @sysroot: (not nullable): The root directory, usually `/`
 * @envp: (array zero-terminated=1) (not nullable): Behave as though `environ`
 *  was this array
 * @explicit: If %TRUE, load explicit layers, otherwise load implicit layers.
 *
 * Implementation of srt_system_info_list_explicit_vulkan_layers() and
 * srt_system_info_list_implicit_vulkan_layers().
 *
 * Returns: (transfer full) (element-type SrtVulkanLayer): A list of Vulkan
 *  layers, most-important first
 */
GList *
_srt_load_vulkan_layers (const char *sysroot,
                         gchar **envp,
                         gboolean explicit)
{
  GList *ret = NULL;
  g_auto(GStrv) search_paths = NULL;
  const gchar *value;
  const gchar *suffix;

  g_return_val_if_fail (_srt_check_not_setuid (), NULL);
  g_return_val_if_fail (envp != NULL, NULL);

  if (explicit)
    suffix = EXPLICIT_VULKAN_LAYER_SUFFIX;
  else
    suffix = IMPLICIT_VULKAN_LAYER_SUFFIX;

  value = g_environ_getenv (envp, "VK_LAYER_PATH");

  /* As in the Vulkan-Loader implementation, implicit layers are not
   * overridden by "VK_LAYER_PATH"
   * https://github.com/KhronosGroup/Vulkan-Loader/blob/f8a8762/loader/loader.c#L4743
   */
  if (value != NULL && explicit)
    {
      g_auto(GStrv) dirs = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);
      load_json_dirs (sysroot, dirs, NULL, _srt_indirect_strcmp0,
                      vulkan_layer_load_json_cb, &ret);
    }
  else
    {
      search_paths = get_vulkan_search_paths (sysroot, envp, NULL, suffix);
      g_debug ("SEARCH PATHS %s", search_paths[0]);
      load_json_dirs (sysroot, search_paths, NULL, _srt_indirect_strcmp0,
                      vulkan_layer_load_json_cb, &ret);
    }

  return g_list_reverse (ret);
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
                                              self->layer.component_layers);

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
                                                description, component_layers);
                  driver_info = g_list_prepend (driver_info, layer);
                }
              else if ((which == SRT_TYPE_EGL_ICD || which == SRT_TYPE_VULKAN_ICD) &&
                       library_path != NULL)
                {
                  if (which == SRT_TYPE_EGL_ICD)
                    driver_info = g_list_prepend (driver_info, srt_egl_icd_new (json_path,
                                                                                library_path));
                  else if (which == SRT_TYPE_VULKAN_ICD)
                    driver_info = g_list_prepend (driver_info, srt_vulkan_icd_new (json_path,
                                                                                   api_version,
                                                                                   library_path));
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
                                                                                      error));
                  else if (which == SRT_TYPE_VULKAN_ICD)
                    driver_info = g_list_prepend (driver_info, srt_vulkan_icd_new_error (json_path,
                                                                                         error));
                  else if (which == SRT_TYPE_VULKAN_LAYER)
                    driver_info = g_list_prepend (driver_info, srt_vulkan_layer_new_error (json_path,
                                                                                           error));
                  else
                    g_return_val_if_reached (NULL);

                  g_clear_error (&error);
                }
            }
        }
    }
out:
  return driver_info;
}

/**
 * SrtGlxIcd:
 *
 * Opaque object representing a GLVND GLX ICD.
 */

struct _SrtGlxIcd
{
  /*< private >*/
  GObject parent;
  gchar *library_soname;
  gchar *library_path;
};

struct _SrtGlxIcdClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum
{
  GLX_ICD_PROP_0,
  GLX_ICD_PROP_LIBRARY_SONAME,
  GLX_ICD_PROP_LIBRARY_PATH,
  N_GLX_ICD_PROPERTIES
};

G_DEFINE_TYPE (SrtGlxIcd, srt_glx_icd, G_TYPE_OBJECT)

static void
srt_glx_icd_init (SrtGlxIcd *self)
{
}

static void
srt_glx_icd_get_property (GObject *object,
                          guint prop_id,
                          GValue *value,
                          GParamSpec *pspec)
{
  SrtGlxIcd *self = SRT_GLX_ICD (object);

  switch (prop_id)
    {
      case GLX_ICD_PROP_LIBRARY_SONAME:
        g_value_set_string (value, self->library_soname);
        break;

      case GLX_ICD_PROP_LIBRARY_PATH:
        g_value_set_string (value, self->library_path);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_glx_icd_set_property (GObject *object,
                          guint prop_id,
                          const GValue *value,
                          GParamSpec *pspec)
{
  SrtGlxIcd *self = SRT_GLX_ICD (object);

  switch (prop_id)
    {
      case GLX_ICD_PROP_LIBRARY_SONAME:
        g_return_if_fail (self->library_soname == NULL);
        self->library_soname = g_value_dup_string (value);
        break;

      case GLX_ICD_PROP_LIBRARY_PATH:
        g_return_if_fail (self->library_path == NULL);
        self->library_path = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_glx_icd_finalize (GObject *object)
{
  SrtGlxIcd *self = SRT_GLX_ICD (object);

  g_clear_pointer (&self->library_soname, g_free);
  g_clear_pointer (&self->library_path, g_free);

  G_OBJECT_CLASS (srt_glx_icd_parent_class)->finalize (object);
}

static GParamSpec *glx_icd_properties[N_GLX_ICD_PROPERTIES] = { NULL };

static void
srt_glx_icd_class_init (SrtGlxIcdClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_glx_icd_get_property;
  object_class->set_property = srt_glx_icd_set_property;
  object_class->finalize = srt_glx_icd_finalize;

  glx_icd_properties[GLX_ICD_PROP_LIBRARY_SONAME] =
    g_param_spec_string ("library-soname", "Library soname",
                         "SONAME of the GLX ICD library",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  glx_icd_properties[GLX_ICD_PROP_LIBRARY_PATH] =
    g_param_spec_string ("library-path", "Library absolute path",
                         "Absolute path to the GLX ICD library",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_GLX_ICD_PROPERTIES,
                                     glx_icd_properties);
}

/**
 * srt_glx_icd_new:
 * @library_soname: (transfer none): the soname of the library
 * @library_path: (transfer none): the absolute path of the library
 *
 * Returns: (transfer full): a new GLVND GLX ICD
 */
static SrtGlxIcd *
srt_glx_icd_new (const gchar *library_soname,
                 const gchar *library_path)
{
  g_return_val_if_fail (library_soname != NULL, NULL);
  g_return_val_if_fail (library_path != NULL, NULL);
  g_return_val_if_fail (g_path_is_absolute (library_path), NULL);

  return g_object_new (SRT_TYPE_GLX_ICD,
                       "library-soname", library_soname,
                       "library-path", library_path,
                       NULL);
}

/**
 * srt_glx_icd_get_library_soname:
 * @self: The GLX ICD
 *
 * Return the library SONAME for this GLX ICD, for example `libGLX_mesa.so.0`.
 *
 * Returns: (type filename) (transfer none): #SrtGlxIcd:library-soname
 */
const gchar *
srt_glx_icd_get_library_soname (SrtGlxIcd *self)
{
  g_return_val_if_fail (SRT_IS_GLX_ICD (self), NULL);
  return self->library_soname;
}

/**
 * srt_glx_icd_get_library_path:
 * @self: The GLX ICD
 *
 * Return the absolute path to the library that implements this GLX soname.
 *
 * Returns: (type filename) (transfer none): #SrtGlxIcd:library-path
 */
const gchar *
srt_glx_icd_get_library_path (SrtGlxIcd *self)
{
  g_return_val_if_fail (SRT_IS_GLX_ICD (self), NULL);
  return self->library_path;
}

/**
 * _srt_glx_icd_get_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "glx_drivers"
 *  property
 *
 * If the provided @json_obj doesn't have a "glx_drivers" member, or it is
 * malformed, %NULL will be returned.
 *
 * Returns: A list of #SrtGlxIcd that have been found, or %NULL if none
 *  has been found.
 */
GList *
_srt_glx_icd_get_from_report (JsonObject *json_obj)
{
  JsonArray *array;
  GList *glx_drivers = NULL;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (json_object_has_member (json_obj, "glx_drivers"))
    {
      array = json_object_get_array_member (json_obj, "glx_drivers");

      if (array == NULL)
        goto out;

      guint length = json_array_get_length (array);
      for (guint j = 0; j < length; j++)
        {
          const gchar *glx_path = NULL;
          const gchar *glx_soname = NULL;
          JsonObject *json_glx_obj = json_array_get_object_element (array, j);
          glx_path = json_object_get_string_member_with_default (json_glx_obj, "library_path",
                                                                 NULL);
          glx_soname = json_object_get_string_member_with_default (json_glx_obj, "library_soname",
                                                                   NULL);

          glx_drivers = g_list_prepend (glx_drivers, srt_glx_icd_new (glx_soname, glx_path));
        }
    }

out:
  return g_list_reverse (glx_drivers);
}
