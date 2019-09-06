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
#include "steam-runtime-tools/graphics-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include <string.h>

#include <json-glib/json-glib.h>

/**
 * SECTION:graphics
 * @title: Graphics compatibility check
 * @short_description: Get information about system's graphics capabilities
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtGraphics is an opaque object representing a graphics capabilities.
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

  switch (prop_id)
    {
      case PROP_ISSUES:
        /* Construct-only */
        g_return_if_fail (self->issues == 0);
        self->issues = g_value_get_flags (value);
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

/**
 * _srt_check_graphics:
 * @helpers_path: An optional path to find wflinfo helpers, PATH is used if null.
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
                     const char *multiarch_tuple,
                     SrtWindowSystem window_system,
                     SrtRenderingInterface rendering_interface,
                     SrtGraphics **details_out)
{
  GPtrArray *argv = NULL;
  gchar *output = NULL;
  int exit_status = -1;
  JsonParser *parser = NULL;
  JsonNode *node = NULL;
  JsonObject *object = NULL;
  JsonNode *sub_node = NULL;
  JsonObject *sub_object = NULL;
  const gchar *version_string = NULL;
  const gchar *renderer_string = NULL;
  GError *error = NULL;
  SrtGraphicsIssues issues = SRT_GRAPHICS_ISSUES_NONE;
  GStrv my_environ = NULL;
  const gchar *ld_preload;
  gchar *filtered_preload = NULL;

  g_return_val_if_fail (details_out == NULL || *details_out == NULL, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (window_system >= 0, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (window_system < SRT_N_WINDOW_SYSTEMS, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (rendering_interface >= 0, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
  g_return_val_if_fail (rendering_interface < SRT_N_RENDERING_INTERFACES, SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);

  gchar *platformstring = NULL;

  argv = g_ptr_array_new_with_free_func (g_free);

  if (window_system == SRT_WINDOW_SYSTEM_GLX)
    {
      platformstring = g_strdup ("glx");
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
      else
        {
          /* should not be reached because the precondition checks
           * should have caught this */
          g_return_val_if_reached (SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
        }
    }
  else if (window_system == SRT_WINDOW_SYSTEM_EGL_X11)
    {
      platformstring = g_strdup ("x11_egl");
    }
  else
    {
      /* should not be reached because the precondition checks should
       * have caught this */
      g_return_val_if_reached (SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
    }

  if (rendering_interface == SRT_RENDERING_INTERFACE_GL)
    {
      if (helpers_path != NULL)
        {
          g_ptr_array_add (argv, g_strdup_printf ("%s/%s-wflinfo", helpers_path, multiarch_tuple));
        }
      else
        {
          g_ptr_array_add (argv, g_strdup_printf ("%s-wflinfo", multiarch_tuple));
        }
      g_ptr_array_add (argv, g_strdup_printf ("--platform=%s", platformstring));
      g_ptr_array_add (argv, g_strdup ("--api=gl"));
      g_ptr_array_add (argv, g_strdup ("--format=json"));
    }
  else if (rendering_interface == SRT_RENDERING_INTERFACE_GLESV2)
    {
      if (helpers_path != NULL)
        {
          g_ptr_array_add (argv, g_strdup_printf ("%s/%s-wflinfo", helpers_path, multiarch_tuple));
        }
      else
        {
          g_ptr_array_add (argv, g_strdup_printf ("%s-wflinfo", multiarch_tuple));
        }
      g_ptr_array_add (argv, g_strdup_printf ("--platform=%s", platformstring));
      g_ptr_array_add (argv, g_strdup ("--api=gles2"));
      g_ptr_array_add (argv, g_strdup ("--format=json"));
    }
  else
    {
      g_return_val_if_reached (SRT_GRAPHICS_ISSUES_INTERNAL_ERROR);
    }
  g_ptr_array_add (argv, NULL);

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
                     NULL,    /* stderr */
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

  node = json_parser_get_root (parser);
  object = json_node_get_object (node);

  /* Process json output */
  if (!json_object_has_member (object, "OpenGL"))
    {
      g_debug ("The json output doesn't contain an OpenGL object");
      issues |= SRT_GRAPHICS_ISSUES_CANNOT_LOAD;
      goto out;
    }

  sub_node = json_object_get_member (object, "OpenGL");
  sub_object = json_node_get_object (sub_node);

  if (!json_object_has_member (sub_object, "version string") ||
      !json_object_has_member (sub_object, "renderer string"))
    {
      g_debug ("Json output is missing version or renderer");
      issues |= SRT_GRAPHICS_ISSUES_CANNOT_LOAD;
      goto out;
    }

  version_string = json_object_get_string_member (sub_object, "version string");
  renderer_string = json_object_get_string_member (sub_object, "renderer string");

  /* Check renderer to see if we are using software rendering */
  if (strstr (renderer_string, "llvmpipe") != NULL ||
      strstr (renderer_string, "software rasterizer") != NULL ||
      strstr (renderer_string, "softpipe") != NULL)
    {
      issues |= SRT_GRAPHICS_ISSUES_SOFTWARE_RENDERING;
    }

out:
  if (details_out != NULL)
    *details_out = _srt_graphics_new (multiarch_tuple,
                                      window_system,
                                      rendering_interface,
                                      renderer_string,
                                      version_string,
                                      issues);

  if (parser != NULL)
    g_object_unref (parser);

  g_ptr_array_unref (argv);
  g_free (output);
  g_clear_error (&error);
  g_free (platformstring);
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
