/*
 * Copyright © 2019 Collabora Ltd.
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
#include "steam-runtime-tools/glib-compat.h"
#include "steam-runtime-tools/graphics-internal.h"
#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

#include <stdint.h>
#include <string.h>
#include <sys/wait.h>

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
  gchar *messages;
  gchar *renderer_string;
  gchar *version_string;
};

struct _SrtGraphicsClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum {
  PROP_0,
  PROP_ISSUES,
  PROP_MESSAGES,
  PROP_MULTIARCH_TUPLE,
  PROP_WINDOW_SYSTEM,
  PROP_RENDERING_INTERFACE,
  PROP_RENDERER_STRING,
  PROP_VERSION_STRING,
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
  g_return_val_if_fail (version_string != NULL, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (renderer_string != NULL, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);

  SrtGraphicsIssues issues = SRT_GRAPHICS_ISSUES_NONE;

  JsonNode *node = json_parser_get_root (parser);
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
  g_return_val_if_fail (new_version_string != NULL, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (renderer_string != NULL, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);

  SrtGraphicsIssues issues = SRT_GRAPHICS_ISSUES_NONE;
  JsonNode *node = json_parser_get_root (parser);
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

static GPtrArray *
_argv_for_graphics_test (const char *helpers_path,
                         SrtTestFlags test_flags,
                         const char *multiarch_tuple,
                         SrtWindowSystem window_system,
                         SrtRenderingInterface rendering_interface,
                         GError **error)
{
  GPtrArray *argv = NULL;
  gchar *platformstring = NULL;
  SrtHelperFlags flags = (SRT_HELPER_FLAGS_TIME_OUT
                          | SRT_HELPER_FLAGS_SEARCH_PATH);

  if (test_flags & SRT_TEST_FLAGS_TIME_OUT_SOONER)
    flags |= SRT_HELPER_FLAGS_TIME_OUT_SOONER;

  if (window_system == SRT_WINDOW_SYSTEM_GLX)
    {
      if (rendering_interface == SRT_RENDERING_INTERFACE_GL)
        {
          platformstring = g_strdup ("glx");
        }
      else
        {
          g_critical ("GLX window system only makes sense with GL "
                      "rendering interface, not %d",
                      rendering_interface);
          g_return_val_if_reached (NULL);
        }
    }
  else if (window_system == SRT_WINDOW_SYSTEM_X11)
    {
      if (rendering_interface == SRT_RENDERING_INTERFACE_GL)
        {
          platformstring = g_strdup ("glx");
          window_system = SRT_WINDOW_SYSTEM_GLX;
        }
      else if (rendering_interface == SRT_RENDERING_INTERFACE_GLESV2)
        {
          platformstring = g_strdup ("x11_egl");
          window_system = SRT_WINDOW_SYSTEM_EGL_X11;
        }
      else if (rendering_interface == SRT_RENDERING_INTERFACE_VULKAN)
        {
          // Vulkan, don't set platformstring, just set argv later.
        }
      else
        {
          /* should not be reached because the precondition checks
           * should have caught this */
          g_return_val_if_reached (NULL);
        }
    }
  else if (window_system == SRT_WINDOW_SYSTEM_EGL_X11)
    {
      if (rendering_interface == SRT_RENDERING_INTERFACE_GL
          || rendering_interface == SRT_RENDERING_INTERFACE_GLESV2)
        {
          platformstring = g_strdup ("x11_egl");
        }
      else
        {
          /* e.g. Vulkan (when implemented) */
          g_critical ("EGL window system only makes sense with a GL-based "
                      "rendering interface, not %d",
                      rendering_interface);
          g_return_val_if_reached (NULL);
        }
    }
  else
    {
      /* should not be reached because the precondition checks should
       * have caught this */
      g_return_val_if_reached (NULL);
    }

  if (rendering_interface == SRT_RENDERING_INTERFACE_GL
      || rendering_interface == SRT_RENDERING_INTERFACE_GLESV2)
    {
      const char *api;

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
    }
  else if (rendering_interface == SRT_RENDERING_INTERFACE_VULKAN)
    {
      argv = _srt_get_helper (helpers_path, multiarch_tuple, "vulkaninfo",
                              flags, error);

      if (argv == NULL)
        goto out;

      g_ptr_array_add (argv, g_strdup ("-j"));
    }
  else
    {
      /* should not be reached because the precondition checks should
       * have caught this */
      g_return_val_if_reached (NULL);
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

/**
 * _srt_check_graphics:
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
_srt_check_graphics (const char *helpers_path,
                     SrtTestFlags test_flags,
                     const char *multiarch_tuple,
                     SrtWindowSystem window_system,
                     SrtRenderingInterface rendering_interface,
                     SrtGraphics **details_out)
{
  gchar *output = NULL;
  gchar *child_stderr = NULL;
  gchar *child_stderr2 = NULL;
  int exit_status = -1;
  JsonParser *parser = NULL;
  const gchar *version_string = NULL;
  gchar *new_version_string = NULL;
  const gchar *renderer_string = NULL;
  GError *error = NULL;
  SrtGraphicsIssues issues = SRT_GRAPHICS_ISSUES_NONE;
  GStrv my_environ = NULL;
  const gchar *ld_preload;
  gchar *filtered_preload = NULL;
  gboolean parse_wflinfo = (rendering_interface != SRT_RENDERING_INTERFACE_VULKAN);

  g_return_val_if_fail (details_out == NULL || *details_out == NULL, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (window_system >= 0, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (window_system < SRT_N_WINDOW_SYSTEMS, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (rendering_interface >= 0, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (rendering_interface < SRT_N_RENDERING_INTERFACES, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (_srt_check_not_setuid (), SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);

  GPtrArray *argv = _argv_for_graphics_test (helpers_path,
                                             test_flags,
                                             multiarch_tuple,
                                             window_system,
                                             rendering_interface,
                                             &error);

  if (argv == NULL)
    {
      issues |= SRT_GRAPHICS_ISSUES_CANNOT_LOAD;
      /* Put the error message in the 'messages' */
      child_stderr = g_strdup (error->message);
      goto out;
    }

  my_environ = g_get_environ ();
  ld_preload = g_environ_getenv (my_environ, "LD_PRELOAD");
  if (ld_preload != NULL)
    {
      filtered_preload = _srt_filter_gameoverlayrenderer (ld_preload);
      my_environ = g_environ_setenv (my_environ, "LD_PRELOAD", filtered_preload, TRUE);
    }

  if (!g_spawn_sync (NULL,    /* working directory */
                     (gchar **) argv->pdata,
                     my_environ,    /* envp */
                     G_SPAWN_SEARCH_PATH,       /* flags */
                     NULL,    /* child setup */
                     NULL,    /* user data */
                     &output, /* stdout */
                     &child_stderr,
                     &exit_status,
                     &error))
    {
      g_debug ("An error occurred calling the helper: %s", error->message);
      issues |= SRT_GRAPHICS_ISSUES_CANNOT_LOAD;
      goto out;
    }

  if (exit_status != 0)
    {
      g_debug ("... wait status %d", exit_status);
      issues |= SRT_GRAPHICS_ISSUES_CANNOT_LOAD;

      // TERM signal gives us 124 from timeout man page
      if (WIFEXITED (exit_status) && WEXITSTATUS (exit_status) == 124) // timeout command killed the helper
        {
          g_debug ("helper killed by timeout command");
          issues |= SRT_GRAPHICS_ISSUES_TIMEOUT;
        }

      goto out;
    }

  /* We can't use `json_from_string()` directly because we are targeting an
   * older json-glib version */
  parser = json_parser_new ();

  if (!json_parser_load_from_data (parser, output, -1, &error))
    {
      g_debug ("The helper output is not a valid JSON: %s", error->message);
      issues |= SRT_GRAPHICS_ISSUES_CANNOT_LOAD;
      goto out;
    }

  /* Process json output */
  if (parse_wflinfo)
    {
      issues |= _srt_process_wflinfo (parser, &version_string, &renderer_string);
    }
  else
    {
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
      if (!g_spawn_sync (NULL,    /* working directory */
                         (gchar **) argv->pdata,
                         my_environ,    /* envp */
                         G_SPAWN_SEARCH_PATH,       /* flags */
                         NULL,    /* child setup */
                         NULL,    /* user data */
                         &output, /* stdout */
                         &child_stderr2,
                         &exit_status,
                         &error))
        {
          g_debug ("An error occurred calling the helper: %s", error->message);
          issues |= SRT_GRAPHICS_ISSUES_CANNOT_DRAW;
          goto out;
        }

      if (exit_status != 0)
        {
          g_debug ("... wait status %d", exit_status);
          issues |= SRT_GRAPHICS_ISSUES_CANNOT_DRAW;

          // TERM signal gives us 124 from timeout man page
          if (WIFEXITED (exit_status) && WEXITSTATUS (exit_status) == 124) // timeout command killed the helper
            {
              g_debug ("helper killed by timeout command");
              issues |= SRT_GRAPHICS_ISSUES_TIMEOUT;
            }
        }
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
                                      renderer_string,
                                      version_string,
                                      issues,
                                      child_stderr);

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
  g_return_val_if_fail (SRT_IS_GRAPHICS (self), SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  return self->issues;
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

/* EGL and Vulkan ICDs are actually basically the same, but we don't
 * hard-code that in the API. */
typedef struct
{
  GError *error;
  gchar *api_version;   /* Always NULL when found in a SrtEglIcd */
  gchar *json_path;
  gchar *library_path;
} SrtIcd;

static void
srt_icd_clear (SrtIcd *self)
{
  g_clear_error (&self->error);
  g_clear_pointer (&self->api_version, g_free);
  g_clear_pointer (&self->json_path, g_free);
  g_clear_pointer (&self->library_path, g_free);
}

/*
 * See srt_egl_icd_resolve_library_path(),
 * srt_vulkan_icd_resolve_library_path()
 */
static gchar *
srt_icd_resolve_library_path (const SrtIcd *self)
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
srt_icd_check_error (const SrtIcd *self,
                     GError **error)
{
  if (self->error != NULL && error != NULL)
    *error = g_error_copy (self->error);

  return (self->error == NULL);
}

/* See srt_egl_icd_write_to_file(), srt_vulkan_icd_write_to_file() */
static gboolean
srt_icd_write_to_file (const SrtIcd *self,
                       const char *path,
                       GError **error)
{
  JsonBuilder *builder;
  JsonGenerator *generator;
  JsonNode *root;
  gchar *json_output;
  gboolean ret = FALSE;

  if (!srt_icd_check_error (self, error))
    {
      g_prefix_error (error,
                      "Cannot save ICD metadata to file because it is invalid: ");
      return FALSE;
    }

  builder = json_builder_new ();
  json_builder_begin_object (builder);
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

    json_builder_set_member_name (builder, "ICD");
    json_builder_begin_object (builder);
    {
      json_builder_set_member_name (builder, "library_path");
      json_builder_add_string_value (builder, self->library_path);

      /* In the EGL case this will be NULL. In the Vulkan case it will
       * be non-NULL, because if the API version was missing, we would
       * have set the error indicator, so we wouldn't get here. */
      if (self->api_version != NULL)
        {
          json_builder_set_member_name (builder, "api_version");
          json_builder_add_string_value (builder, self->api_version);
        }
    }
    json_builder_end_object (builder);
  }
  json_builder_end_object (builder);

  root = json_builder_get_root (builder);
  generator = json_generator_new ();
  json_generator_set_root (generator, root);
  json_generator_set_pretty (generator, TRUE);
  json_output = json_generator_to_data (generator, NULL);

  ret = g_file_set_contents (path, json_output, -1, error);

  if (!ret)
    g_prefix_error (error,
                    "Cannot save ICD metadata to file :");

  g_free (json_output);
  g_object_unref (generator);
  json_node_free (root);
  g_object_unref (builder);
  return ret;
}

/*
 * indirect_strcmp0:
 * @left: A non-%NULL pointer to a (possibly %NULL) `const char *`
 * @right: A non-%NULL pointer to a (possibly %NULL) `const char *`
 *
 * A #GCompareFunc to sort pointers to strings in lexicographic
 * (g_strcmp0()) order.
 *
 * Returns: An integer < 0 if left < right, > 0 if left > right,
 *  or 0 if left == right or if they are not comparable
 */
static int
indirect_strcmp0 (gconstpointer left,
                  gconstpointer right)
{
  const gchar * const *l = left;
  const gchar * const *r = right;

  g_return_val_if_fail (l != NULL, 0);
  g_return_val_if_fail (r != NULL, 0);
  return g_strcmp0 (*l, *r);
}

/*
 * A #GCompareFunc that does not sort the members of the directory.
 */
#define READDIR_ORDER ((GCompareFunc) NULL)

/*
 * load_json_dir:
 * @sysroot: (nullable): Interpret directory names as being inside this
 *  sysroot, mainly for unit testing
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
  GError *error = NULL;
  GDir *dir_iter = NULL;
  gchar *sysrooted_dir = NULL;
  gchar *suffixed_dir = NULL;
  const char *iter_dir;
  const char *member;
  GPtrArray *members;
  gsize i;

  g_return_if_fail (load_json_cb != NULL);

  if (dir == NULL)
    return;

  if (suffix != NULL)
    {
      suffixed_dir = g_build_filename (dir, suffix, NULL);
      dir = suffixed_dir;
    }

  iter_dir = dir;

  if (sysroot != NULL)
    {
      sysrooted_dir = g_build_filename (sysroot, dir, NULL);
      iter_dir = sysrooted_dir;
    }

  g_debug ("Looking for ICDs in %s...", dir);

  dir_iter = g_dir_open (iter_dir, 0, &error);

  if (dir_iter == NULL)
    {
      g_debug ("Failed to open \"%s\": %s", iter_dir, error->message);
      goto out;
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

out:
  if (dir_iter != NULL)
    g_dir_close (dir_iter);

  g_free (suffixed_dir);
  g_free (sysrooted_dir);
  g_clear_error (&error);
}

/*
 * load_json_dir:
 * @sysroot: (nullable): Interpret directory names as being inside this
 *  sysroot, mainly for unit testing
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
  SrtIcd icd;
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

  srt_icd_clear (&self->icd);

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
  return srt_icd_check_error (&self->icd, error);
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
 * @sysroot: Interpret the filename as being inside this sysroot,
 *  mainly for unit testing
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
  GError *error = NULL;
  gchar *in_sysroot = NULL;
  gchar *library_path = NULL;

  g_return_if_fail (list != NULL);

  if (sysroot != NULL)
    in_sysroot = g_build_filename (sysroot, filename, NULL);

  if (load_json (SRT_TYPE_EGL_ICD,
                 in_sysroot == NULL ? filename : in_sysroot,
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

  g_free (in_sysroot);
  g_free (library_path);
  g_clear_error (&error);
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
  return srt_icd_resolve_library_path (&self->icd);
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
  return srt_icd_write_to_file (&self->icd, path, error);
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
 * @envp: (array zero-terminated=1): Behave as though `environ` was this
 *  array
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
_srt_load_egl_icds (gchar **envp,
                    const char * const *multiarch_tuples)
{
  gchar **environ_copy = NULL;
  const gchar *sysroot;
  const gchar *value;
  gsize i;
  /* To avoid O(n**2) performance, we build this list in reverse order,
   * then reverse it at the end. */
  GList *ret = NULL;

  g_return_val_if_fail (_srt_check_not_setuid (), NULL);

  if (envp == NULL)
    {
      environ_copy = g_get_environ ();
      envp = environ_copy;
    }

  /* See
   * https://github.com/NVIDIA/libglvnd/blob/master/src/EGL/icd_enumeration.md
   * for details of the search order. */

  sysroot = g_environ_getenv (envp, "SRT_TEST_SYSROOT");
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

      flatpak_info = g_build_filename (sysroot != NULL ? sysroot : "/",
                                       ".flatpak-info", NULL);

      if (value != NULL)
        {
          dirs = g_strsplit (value, G_SEARCHPATH_SEPARATOR_S, -1);
          load_json_dirs (sysroot, dirs, NULL, indirect_strcmp0,
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
              load_json_dir (sysroot, tmp, NULL, indirect_strcmp0,
                             egl_icd_load_json_cb, &ret);
              g_free (tmp);
            }
        }
      else
        {
          load_json_dir (sysroot, get_glvnd_sysconfdir (), EGL_VENDOR_SUFFIX,
                         indirect_strcmp0, egl_icd_load_json_cb, &ret);
          load_json_dir (sysroot, get_glvnd_datadir (), EGL_VENDOR_SUFFIX,
                         indirect_strcmp0, egl_icd_load_json_cb, &ret);
        }

      g_free (flatpak_info);
    }

  g_strfreev (environ_copy);
  return g_list_reverse (ret);
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
  SrtIcd icd;
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

  srt_icd_clear (&self->icd);

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
  return srt_icd_check_error (&self->icd, error);
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
  return srt_icd_resolve_library_path (&self->icd);
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
  return srt_icd_write_to_file (&self->icd, path, error);
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
 * @sysroot: Interpret the filename as being inside this sysroot,
 *  mainly for unit testing
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
  GError *error = NULL;
  gchar *in_sysroot = NULL;
  gchar *api_version = NULL;
  gchar *library_path = NULL;

  g_return_if_fail (list != NULL);

  if (sysroot != NULL)
    in_sysroot = g_build_filename (sysroot, filename, NULL);

  if (load_json (SRT_TYPE_VULKAN_ICD,
                 in_sysroot == NULL ? filename : in_sysroot,
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

  g_free (in_sysroot);
  g_free (api_version);
  g_free (library_path);
  g_clear_error (&error);
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

/*
 * _srt_load_vulkan_icds:
 * @envp: (array zero-terminated=1): Behave as though `environ` was this
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
_srt_load_vulkan_icds (gchar **envp,
                       const char * const *multiarch_tuples)
{
  gchar **environ_copy = NULL;
  const gchar *sysroot;
  const gchar *value;
  gsize i;
  /* To avoid O(n**2) performance, we build this list in reverse order,
   * then reverse it at the end. */
  GList *ret = NULL;

  g_return_val_if_fail (_srt_check_not_setuid (), NULL);

  if (envp == NULL)
    {
      environ_copy = g_get_environ ();
      envp = environ_copy;
    }

  /* See
   * https://github.com/KhronosGroup/Vulkan-Loader/blob/master/loader/LoaderAndLayerInterface.md#icd-manifest-file-format
   * for more details of the search order - but beware that the
   * documentation is not completely up to date (as of September 2019)
   * so you should also look at the reference implementation. */

  sysroot = g_environ_getenv (envp, "SRT_TEST_SYSROOT");
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
      gchar **dirs;
      gchar *tmp = NULL;
      gchar *flatpak_info = NULL;

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
      load_json_dirs (sysroot, dirs, VULKAN_ICD_SUFFIX, READDIR_ORDER,
                      vulkan_icd_load_json_cb, &ret);
      g_strfreev (dirs);

      value = get_vulkan_sysconfdir ();
      load_json_dir (sysroot, value, VULKAN_ICD_SUFFIX,
                     READDIR_ORDER, vulkan_icd_load_json_cb, &ret);

      /* This is hard-coded in the reference loader: if its own sysconfdir
       * is not /etc, it searches /etc afterwards. (In practice this
       * won't trigger at the moment, because we assume the Vulkan
       * loader's sysconfdir *is* /etc.) */
      if (g_strcmp0 (value, "/etc") != 0)
        load_json_dir (sysroot, "/etc", VULKAN_ICD_SUFFIX,
                       READDIR_ORDER, vulkan_icd_load_json_cb, &ret);

      flatpak_info = g_build_filename (sysroot != NULL ? sysroot : "/",
                                       ".flatpak-info", NULL);

      /* freedesktop-sdk patches the Vulkan loader to look here. */
      if (g_file_test (flatpak_info, G_FILE_TEST_EXISTS)
          && multiarch_tuples != NULL)
        {
          g_debug ("Flatpak detected: assuming freedesktop-based runtime");

          for (i = 0; multiarch_tuples[i] != NULL; i++)
            {
              /* GL extensions */
              tmp = g_build_filename ("/usr/lib",
                                      multiarch_tuples[i],
                                      "GL",
                                      VULKAN_ICD_SUFFIX,
                                      NULL);
              load_json_dir (sysroot, tmp, NULL, READDIR_ORDER,
                             vulkan_icd_load_json_cb, &ret);
              g_free (tmp);

              /* Built-in Mesa stack */
              tmp = g_build_filename ("/usr/lib",
                                      multiarch_tuples[i],
                                      VULKAN_ICD_SUFFIX,
                                      NULL);
              load_json_dir (sysroot, tmp, NULL, READDIR_ORDER,
                             vulkan_icd_load_json_cb, &ret);
              g_free (tmp);
            }
        }

      g_free (flatpak_info);

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
      load_json_dirs (sysroot, dirs, VULKAN_ICD_SUFFIX, READDIR_ORDER,
                      vulkan_icd_load_json_cb, &ret);
      g_strfreev (dirs);

      /* I don't know why this is searched *after* XDG_DATA_DIRS in the
       * reference loader, but we match that behaviour. */
      value = g_environ_getenv (envp, "XDG_DATA_HOME");
      load_json_dir (sysroot, value, VULKAN_ICD_SUFFIX, READDIR_ORDER,
                     vulkan_icd_load_json_cb, &ret);

      /* libvulkan searches this unconditionally, even if XDG_DATA_HOME
       * is set. */
      value = g_environ_getenv (envp, "HOME");

      if (value == NULL)
        value = g_get_home_dir ();

      tmp = g_build_filename (value, ".local", "share",
                              VULKAN_ICD_SUFFIX, NULL);
      load_json_dir (sysroot, tmp, NULL, READDIR_ORDER,
                     vulkan_icd_load_json_cb, &ret);
      g_free (tmp);
    }

  g_strfreev (environ_copy);
  return g_list_reverse (ret);
}
