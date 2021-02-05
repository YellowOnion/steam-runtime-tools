/*
 * Copyright © 2020 Collabora Ltd.
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

#include "steam-runtime-tools/xdg-portal.h"

#include "steam-runtime-tools/enums.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "steam-runtime-tools/json-utils-internal.h"
#include "steam-runtime-tools/xdg-portal-internal.h"
#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

#include <errno.h>
#include <string.h>
#include <sys/types.h>

#include <json-glib/json-glib.h>

/**
 * SECTION:xdg-portal
 * @title: XDG portals support check
 * @short_description: Get information about system's XDG portals support
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtXdgPortal is an opaque object representing the XDG portals support.
 * This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 *
 * #SrtXdgPortalBackend is an opaque object representing an XDG portal
 * backend. This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 *
 * #SrtXdgPortalInterface is an opaque object representing an XDG portal
 * inerface. This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 */

struct _SrtXdgPortalInterface
{
  /*< private >*/
  GObject parent;
  gchar *name;
  gboolean available;
  guint version;
};

struct _SrtXdgPortalInterfaceClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum {
  PROP_INTERFACE_0,
  PROP_INTERFACE_NAME,
  PROP_INTERFACE_VERSION,
  PROP_INTERFACE_IS_AVAILABLE,
  N_INTERFACE_PROPERTIES,
};

G_DEFINE_TYPE (SrtXdgPortalInterface, srt_xdg_portal_interface, G_TYPE_OBJECT)

static void
srt_xdg_portal_interface_init (SrtXdgPortalInterface *self)
{
}

static void
srt_xdg_portal_interface_get_property (GObject *object,
                                       guint prop_id,
                                       GValue *value,
                                       GParamSpec *pspec)
{
  SrtXdgPortalInterface *self = SRT_XDG_PORTAL_INTERFACE (object);

  switch (prop_id)
    {
      case PROP_INTERFACE_NAME:
        g_value_set_string (value, self->name);
        break;

      case PROP_INTERFACE_VERSION:
        g_value_set_uint (value, self->version);
        break;

      case PROP_INTERFACE_IS_AVAILABLE:
        g_value_set_boolean (value, self->available);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_xdg_portal_interface_set_property (GObject *object,
                                       guint prop_id,
                                       const GValue *value,
                                       GParamSpec *pspec)
{
  SrtXdgPortalInterface *self = SRT_XDG_PORTAL_INTERFACE (object);

  switch (prop_id)
    {
      case PROP_INTERFACE_NAME:
        /* Construct-only */
        g_return_if_fail (self->name == NULL);
        self->name = g_value_dup_string (value);
        break;

      case PROP_INTERFACE_VERSION:
        /* Construct-only */
        g_return_if_fail (self->version == 0);
        self->version = g_value_get_uint (value);
        break;

      case PROP_INTERFACE_IS_AVAILABLE:
        g_return_if_fail (self->available == FALSE);
        self->available = g_value_get_boolean (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_xdg_portal_interface_finalize (GObject *object)
{
  SrtXdgPortalInterface *self = SRT_XDG_PORTAL_INTERFACE (object);

  g_free (self->name);

  G_OBJECT_CLASS (srt_xdg_portal_interface_parent_class)->finalize (object);
}

static GParamSpec *interface_properties[N_INTERFACE_PROPERTIES] = { NULL };

static void
srt_xdg_portal_interface_class_init (SrtXdgPortalInterfaceClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_xdg_portal_interface_get_property;
  object_class->set_property = srt_xdg_portal_interface_set_property;
  object_class->finalize = srt_xdg_portal_interface_finalize;

  interface_properties[PROP_INTERFACE_NAME] =
    g_param_spec_string ("name", "Name",
                         "Name of this XDG portal interface, e.g. 'org.freedesktop.portal.Email'",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  interface_properties[PROP_INTERFACE_VERSION] =
    g_param_spec_uint ("version", "Version",
                       "The version property of this XDG portal interface",
                       0,
                       G_MAXUINT32,
                       0,
                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  interface_properties[PROP_INTERFACE_IS_AVAILABLE] =
    g_param_spec_boolean ("is-available", "Is available?",
                          "TRUE if this XDG portal is available",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_INTERFACE_PROPERTIES, interface_properties);
}

struct _SrtXdgPortalBackend
{
  /*< private >*/
  GObject parent;
  gchar *name;
  gboolean available;
};

struct _SrtXdgPortalBackendClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum {
  PROP_BACKEND_0,
  PROP_BACKEND_NAME,
  PROP_BACKEND_IS_AVAILABLE,
  N_PROP_BACKEND_PROPERTIES,
};

G_DEFINE_TYPE (SrtXdgPortalBackend, srt_xdg_portal_backend, G_TYPE_OBJECT)

static void
srt_xdg_portal_backend_init (SrtXdgPortalBackend *self)
{
}

static void
srt_xdg_portal_backend_get_property (GObject *object,
                                     guint prop_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
  SrtXdgPortalBackend *self = SRT_XDG_PORTAL_BACKEND (object);

  switch (prop_id)
    {
      case PROP_BACKEND_NAME:
        g_value_set_string (value, self->name);
        break;

      case PROP_BACKEND_IS_AVAILABLE:
        g_value_set_boolean (value, self->available);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_xdg_portal_backend_set_property (GObject *object,
                                     guint prop_id,
                                     const GValue *value,
                                     GParamSpec *pspec)
{
  SrtXdgPortalBackend *self = SRT_XDG_PORTAL_BACKEND (object);

  switch (prop_id)
    {
      case PROP_BACKEND_NAME:
        /* Construct-only */
        g_return_if_fail (self->name == NULL);
        self->name = g_value_dup_string (value);
        break;

      case PROP_BACKEND_IS_AVAILABLE:
        g_return_if_fail (self->available == FALSE);
        self->available = g_value_get_boolean (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_xdg_portal_backend_finalize (GObject *object)
{
  SrtXdgPortalBackend *self = SRT_XDG_PORTAL_BACKEND (object);

  g_free (self->name);

  G_OBJECT_CLASS (srt_xdg_portal_backend_parent_class)->finalize (object);
}

static GParamSpec *backend_properties[N_PROP_BACKEND_PROPERTIES] = { NULL };

static void
srt_xdg_portal_backend_class_init (SrtXdgPortalBackendClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_xdg_portal_backend_get_property;
  object_class->set_property = srt_xdg_portal_backend_set_property;
  object_class->finalize = srt_xdg_portal_backend_finalize;

  backend_properties[PROP_BACKEND_NAME] =
    g_param_spec_string ("name", "Name",
                         "Name of this XDG portal backend, e.g. 'org.freedesktop.impl.portal.desktop.gtk'",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  backend_properties[PROP_BACKEND_IS_AVAILABLE] =
    g_param_spec_boolean ("is-available", "Is available?",
                          "TRUE if this XDG portal is available",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROP_BACKEND_PROPERTIES, backend_properties);
}

struct _SrtXdgPortal
{
  /*< private >*/
  GObject parent;
  gchar *messages;
  SrtXdgPortalIssues issues;
  GPtrArray *portals_backends;
  GPtrArray *portals_interfaces;
};

struct _SrtXdgPortalClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum {
  PROP_PORTAL_0,
  PROP_PORTAL_MESSAGES,
  PROP_PORTAL_ISSUES,
  PROP_PORTAL_BACKENDS,
  PROP_PORTAL_INTERFACES,
  N_PORTAL_PROPERTIES,
};

G_DEFINE_TYPE (SrtXdgPortal, srt_xdg_portal, G_TYPE_OBJECT)

static void
srt_xdg_portal_init (SrtXdgPortal *self)
{
}

static void
srt_xdg_portal_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
  SrtXdgPortal *self = SRT_XDG_PORTAL (object);

  switch (prop_id)
    {
      case PROP_PORTAL_MESSAGES:
        g_value_set_string (value, self->messages);
        break;

      case PROP_PORTAL_ISSUES:
        g_value_set_flags (value, self->issues);
        break;

      case PROP_PORTAL_BACKENDS:
        g_value_set_boxed (value, self->portals_backends);
        break;

      case PROP_PORTAL_INTERFACES:
        g_value_set_boxed (value, self->portals_interfaces);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_xdg_portal_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
  SrtXdgPortal *self = SRT_XDG_PORTAL (object);

  switch (prop_id)
    {
      case PROP_PORTAL_MESSAGES:
        /* Construct-only */
        g_return_if_fail (self->messages == NULL);
        self->messages = g_value_dup_string (value);
        break;

      case PROP_PORTAL_ISSUES:
        /* Construct-only */
        g_return_if_fail (self->issues == 0);
        self->issues = g_value_get_flags (value);
        break;

      case PROP_PORTAL_BACKENDS:
        /* Construct-only */
        g_return_if_fail (self->portals_backends == NULL);
        self->portals_backends = g_value_dup_boxed (value);
        break;

      case PROP_PORTAL_INTERFACES:
        /* Construct-only */
        g_return_if_fail (self->portals_interfaces == NULL);
        self->portals_interfaces = g_value_dup_boxed (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_xdg_portal_dispose (GObject *object)
{
  SrtXdgPortal *self = SRT_XDG_PORTAL (object);

  g_clear_pointer (&self->portals_backends, g_ptr_array_unref);
  g_clear_pointer (&self->portals_interfaces, g_ptr_array_unref);

  G_OBJECT_CLASS (srt_xdg_portal_parent_class)->dispose (object);
}

static void
srt_xdg_portal_finalize (GObject *object)
{
  SrtXdgPortal *self = SRT_XDG_PORTAL (object);

  g_free (self->messages);

  G_OBJECT_CLASS (srt_xdg_portal_parent_class)->finalize (object);
}

static GParamSpec *properties[N_PORTAL_PROPERTIES] = { NULL };

static void
srt_xdg_portal_class_init (SrtXdgPortalClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_xdg_portal_get_property;
  object_class->set_property = srt_xdg_portal_set_property;
  object_class->dispose = srt_xdg_portal_dispose;
  object_class->finalize = srt_xdg_portal_finalize;

  properties[PROP_PORTAL_MESSAGES] =
    g_param_spec_string ("messages", "Messages",
                         "Diagnostic messages produced while checking XDG portals support",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_PORTAL_ISSUES] =
    g_param_spec_flags ("issues", "Issues", "Problems with the XDG portals",
                        SRT_TYPE_XDG_PORTAL_ISSUES, SRT_XDG_PORTAL_ISSUES_NONE,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  properties[PROP_PORTAL_BACKENDS] =
    g_param_spec_boxed ("backends", "Backends",
                        "List of XDG portals backends that have been checked",
                        G_TYPE_PTR_ARRAY,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  properties[PROP_PORTAL_INTERFACES] =
    g_param_spec_boxed ("interfaces", "Interfaces",
                        "List of XDG portals interfaces that have been checked",
                        G_TYPE_PTR_ARRAY,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PORTAL_PROPERTIES, properties);
}

/**
 * _srt_check_xdg_portals:
 * @envp: (not nullable): Environment variables to use
 * @helpers_path: An optional path to find check-xdg-portal helper, PATH
 *  is used if null.
 * @test_flags: Flags used during automated testing
 * @multiarch_tuple: Multiarch tuple of helper executable to use
 * @details_out: The SrtXdgPortal object containing the details of the check.
 *
 * Return the problems found when checking the available XDG portals.
 *
 * Returns: A bitfield containing problems, or %SRT_XDG_PORTAL_ISSUES_NONE
 *  if no problems were found
 */
G_GNUC_INTERNAL SrtXdgPortalIssues
_srt_check_xdg_portals (gchar **envp,
                        const char *helpers_path,
                        SrtTestFlags test_flags,
                        SrtContainerType container_type,
                        const char *multiarch_tuple,
                        SrtXdgPortal **details_out)
{
  g_autoptr(GError) local_error = NULL;
  SrtHelperFlags helper_flags = (SRT_HELPER_FLAGS_TIME_OUT
                                 | SRT_HELPER_FLAGS_SEARCH_PATH);
  g_autoptr(GPtrArray) argv = NULL;
  g_autofree gchar *output = NULL;
  g_autofree gchar *stderr_messages = NULL;
  int wait_status = -1;
  int exit_status = -1;
  int terminating_signal = 0;
  JsonObject *object = NULL;
  JsonObject *interfaces_object = NULL;
  JsonObject *backends_object = NULL;
  g_autoptr(JsonNode) node = NULL;
  g_autoptr(GList) interfaces_members = NULL;
  g_autoptr(GList) backends_members = NULL;
  g_autoptr(GPtrArray) backends = g_ptr_array_new_full (2, g_object_unref);
  g_autoptr(GPtrArray) interfaces = g_ptr_array_new_full (2, g_object_unref);
  SrtXdgPortalIssues issues = SRT_XDG_PORTAL_ISSUES_NONE;
  gboolean has_implementation = FALSE;

  g_return_val_if_fail (envp != NULL, SRT_XDG_PORTAL_ISSUES_UNKNOWN);
  g_return_val_if_fail (multiarch_tuple != NULL, SRT_XDG_PORTAL_ISSUES_UNKNOWN);
  g_return_val_if_fail (details_out == NULL || *details_out == NULL,
                        SRT_XDG_PORTAL_ISSUES_UNKNOWN);

  if (test_flags & SRT_TEST_FLAGS_TIME_OUT_SOONER)
    helper_flags |= SRT_HELPER_FLAGS_TIME_OUT_SOONER;

  argv = _srt_get_helper (helpers_path, multiarch_tuple, "check-xdg-portal",
                          helper_flags, &local_error);

  if (argv == NULL)
    {
      g_debug ("An error occurred trying to check the D-Bus portals capabilities: %s",
               local_error->message);
      issues |= SRT_XDG_PORTAL_ISSUES_UNKNOWN;
      if (details_out != NULL)
        *details_out = _srt_xdg_portal_new (local_error->message, issues, NULL, NULL);
      return issues;
    }

  /* NULL terminate the array */
  g_ptr_array_add (argv, NULL);

  if (!g_spawn_sync (NULL,    /* working directory */
                     (gchar **) argv->pdata,
                     envp,
                     G_SPAWN_SEARCH_PATH,
                     _srt_child_setup_unblock_signals,
                     NULL,    /* user data */
                     &output, /* stdout */
                     &stderr_messages,
                     &wait_status,
                     &local_error))
    {
      g_debug ("An error occurred calling the helper: %s", local_error->message);
      issues |= SRT_XDG_PORTAL_ISSUES_UNKNOWN;
      if (details_out != NULL)
        *details_out = _srt_xdg_portal_new (local_error->message, issues, NULL, NULL);
      return issues;
    }

  /* Normalize the empty string (expected to be common) to NULL */
  if (stderr_messages != NULL && stderr_messages[0] == '\0')
    g_clear_pointer (&stderr_messages, g_free);

  if (wait_status != 0)
    {
      g_debug ("... wait status %d", wait_status);
      if (_srt_process_timeout_wait_status (wait_status, &exit_status, &terminating_signal))
        {
          issues |= SRT_XDG_PORTAL_ISSUES_TIMEOUT;
          if (details_out != NULL)
            *details_out = _srt_xdg_portal_new (NULL, issues, NULL, NULL);
          return issues;
        }
    }
  else
    {
      exit_status = 0;
    }

  if (exit_status == 1)
    g_debug ("The helper exited with 1, either a required xdg portal is missing or an error occurred");

  if (output == NULL || output[0] == '\0')
    {
      g_debug ("The helper exited without printing the expected JSON in output");
      issues |= SRT_XDG_PORTAL_ISSUES_UNKNOWN;
      if (details_out != NULL)
        *details_out = _srt_xdg_portal_new (stderr_messages, issues, NULL, NULL);
      return issues;
    }

  node = json_from_string (output, &local_error);

  if (node == NULL || !JSON_NODE_HOLDS_OBJECT (node))
    {
      g_debug ("The helper output is not a JSON object");
      issues |= SRT_XDG_PORTAL_ISSUES_UNKNOWN;
      if (details_out != NULL)
        *details_out = _srt_xdg_portal_new (local_error == NULL ?
                                              "Helper output is not a JSON object" :
                                              local_error->message,
                                            issues, NULL, NULL);
      return issues;
    }

  object = json_node_get_object (node);

  if (!json_object_has_member (object, "interfaces"))
    {
      g_debug ("The helper output JSON is malformed or incomplete");
      issues |= SRT_XDG_PORTAL_ISSUES_UNKNOWN;
      if (details_out != NULL)
        *details_out = _srt_xdg_portal_new ("Helper output does not contain 'interfaces'",
                                            issues, NULL, NULL);
      return issues;
    }

  interfaces_object = json_object_get_object_member (object, "interfaces");
  interfaces_members = json_object_get_members (interfaces_object);
  for (const GList *l = interfaces_members; l != NULL; l = l->next)
    {
      gboolean available = FALSE;
      guint version = 0;
      JsonObject *interface_object = NULL;
      interface_object = json_object_get_object_member (interfaces_object, l->data);
      if (json_object_has_member (interface_object, "available"))
        available = json_object_get_boolean_member (interface_object, "available");
      else
        g_debug ("The helper output JSON is missing the 'available' field, we assume it to be 'FALSE'");
      if (available)
        {
          if (json_object_has_member (interface_object, "version"))
            version = json_object_get_int_member (interface_object, "version");
          else
            g_debug ("The helper output JSON is missing the 'version' field, we assume it to be '0'");
        }
      else
        {
          issues |= SRT_XDG_PORTAL_ISSUES_MISSING_INTERFACE;
        }

      if (details_out != NULL)
        g_ptr_array_add (interfaces,
                         _srt_xdg_portal_interface_new (l->data, available, version));
    }

  if (interfaces_members == NULL)
    issues |= SRT_XDG_PORTAL_ISSUES_MISSING_INTERFACE;

  /* If 'backends' is missing is not necessarily an error */
  if (json_object_has_member (object, "backends"))
    {
      backends_object = json_object_get_object_member (object, "backends");
      backends_members = json_object_get_members (backends_object);
      for (const GList *l = backends_members; l != NULL; l = l->next)
        {
          gboolean available = FALSE;
          JsonObject *backend_object = NULL;
          backend_object = json_object_get_object_member (backends_object, l->data);
          if (json_object_has_member (backend_object, "available"))
            available = json_object_get_boolean_member (backend_object, "available");
          else
            g_debug ("The helper output JSON is missing the 'available' field, we assume it to be 'FALSE'");

          if (available)
            has_implementation = TRUE;

          if (details_out != NULL)
            g_ptr_array_add (backends,
                             _srt_xdg_portal_backend_new (l->data, available));
        }
    }

  if (container_type == SRT_CONTAINER_TYPE_FLATPAK)
    {
      g_debug ("In a Flatpak container we are not allowed to contact the portals implementations");
    }
  else if (!has_implementation)
    {
      issues |= SRT_XDG_PORTAL_ISSUES_NO_IMPLEMENTATION;
    }

  if (details_out != NULL)
    {
      *details_out = _srt_xdg_portal_new (stderr_messages,
                                          issues,
                                          backends,
                                          interfaces);
    }

  return issues;
}

/**
 * _srt_xdg_portal_get_info_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for the XDG portals info
 *
 * Returns: A new #SrtXdgPortal.
 */
SrtXdgPortal *
_srt_xdg_portal_get_info_from_report (JsonObject *json_obj)
{
  JsonObject *json_portals_obj = NULL;
  JsonObject *json_details_obj = NULL;
  JsonObject *json_interfaces_obj = NULL;
  JsonObject *json_backends_obj = NULL;
  g_autoptr(GList) interfaces_members = NULL;
  g_autoptr(GList) backends_members = NULL;
  g_autoptr(GPtrArray) backends = g_ptr_array_new_full (2, g_object_unref);
  g_autoptr(GPtrArray) interfaces = g_ptr_array_new_full (2, g_object_unref);
  SrtXdgPortalIssues issues = SRT_XDG_PORTAL_ISSUES_NONE;
  g_autofree gchar *messages = NULL;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (!json_object_has_member (json_obj, "xdg-portals"))
    {
      g_debug ("'xdg-portals' entry is missing");
      return _srt_xdg_portal_new (NULL, SRT_XDG_PORTAL_ISSUES_UNKNOWN, NULL, NULL);
    }

  json_portals_obj = json_object_get_object_member (json_obj, "xdg-portals");

  issues = srt_get_flags_from_json_array (SRT_TYPE_XDG_PORTAL_ISSUES,
                                          json_portals_obj,
                                          "issues",
                                          SRT_XDG_PORTAL_ISSUES_UNKNOWN);

  messages = _srt_json_object_dup_array_of_lines_member (json_portals_obj, "messages");

  if (!json_object_has_member (json_portals_obj, "details"))
    return _srt_xdg_portal_new (messages, issues, NULL, NULL);

  json_details_obj = json_object_get_object_member (json_portals_obj, "details");

  if (json_object_has_member (json_details_obj, "interfaces"))
    {
      json_interfaces_obj = json_object_get_object_member (json_details_obj, "interfaces");
      interfaces_members = json_object_get_members (json_interfaces_obj);
      for (const GList *l = interfaces_members; l != NULL; l = l->next)
        {
          gboolean available = FALSE;
          gint64 version;
          JsonObject *json_portal_obj = json_object_get_object_member (json_interfaces_obj, l->data);
          available = json_object_get_boolean_member_with_default (json_portal_obj, "available",
                                                                   FALSE);
          version = json_object_get_int_member_with_default (json_portal_obj, "version", 0);

          g_ptr_array_add (interfaces,
                           _srt_xdg_portal_interface_new (l->data, available, version));
        }
    }

  if (json_object_has_member (json_details_obj, "backends"))
    {
      json_backends_obj = json_object_get_object_member (json_details_obj, "backends");
      backends_members = json_object_get_members (json_backends_obj);
      for (const GList *l = backends_members; l != NULL; l = l->next)
        {
          gboolean available = FALSE;
          JsonObject *json_portal_obj = json_object_get_object_member (json_backends_obj, l->data);
          available = json_object_get_boolean_member_with_default (json_portal_obj, "available",
                                                                   FALSE);

          g_ptr_array_add (backends, _srt_xdg_portal_backend_new (l->data, available));
        }
    }

  return _srt_xdg_portal_new (messages,
                              issues,
                              backends,
                              interfaces);
}

/**
 * srt_xdg_portal_get_messages:
 * @self: an XDG portal object
 *
 * Return the diagnostic messages shown by the XDG portal check, or %NULL if
 * none. The returned pointer is valid as long as a reference to @self is held.
 *
 * Returns: (nullable) (transfer none): #SrtXdgPortal:messages
 */
const char *
srt_xdg_portal_get_messages (SrtXdgPortal *self)
{
  g_return_val_if_fail (SRT_IS_XDG_PORTAL (self), NULL);
  return self->messages;
}

/**
 * srt_xdg_portal_get_issues:
 * @self: an XDG portal object
 *
 * Return flags indicating issues found while checking for XDG desktop portals.
 *
 * Returns: #SrtXdgPortal:issues
 */
SrtXdgPortalIssues
srt_xdg_portal_get_issues (SrtXdgPortal *self)
{
  g_return_val_if_fail (SRT_IS_XDG_PORTAL (self), SRT_XDG_PORTAL_ISSUES_UNKNOWN);
  return self->issues;
}

/**
 * srt_xdg_portal_get_backends:
 * @self: an XDG portal object
 *
 * Return the list of XDG portals backends that have been checked.
 *
 * Returns: (transfer full) (element-type SrtXdgPortalBackend): The backends.
 *  Free with `g_list_free_full (list, g_object_unref)`.
 */
GList *
srt_xdg_portal_get_backends (SrtXdgPortal *self)
{
  GList *ret = NULL;
  guint i;

  g_return_val_if_fail (SRT_IS_XDG_PORTAL (self), NULL);

  for (i = 0; self->portals_backends != NULL && i < self->portals_backends->len; i++)
    ret = g_list_prepend (ret, g_object_ref (g_ptr_array_index (self->portals_backends, i)));

  return g_list_reverse (ret);
}

/**
 * srt_xdg_portal_get_interfaces:
 * @self: an XDG portal object
 *
 * Return the list of XDG portals interfaces that have been checked.
 *
 * Returns: (transfer full) (element-type SrtXdgPortalInterface): The interfaces.
 *  Free with `g_list_free_full (list, g_object_unref)`.
 */
GList *
srt_xdg_portal_get_interfaces (SrtXdgPortal *self)
{
  GList *ret = NULL;
  guint i;

  g_return_val_if_fail (SRT_IS_XDG_PORTAL (self), NULL);

  for (i = 0; self->portals_interfaces != NULL && i < self->portals_interfaces->len; i++)
    ret = g_list_prepend (ret, g_object_ref (g_ptr_array_index (self->portals_interfaces, i)));

  return g_list_reverse (ret);
}

/**
 * srt_xdg_portal_backend_get_name:
 * @self: an XDG portal backend object
 *
 * Return the name of the XDG portal backend, for example
 * `org.freedesktop.impl.portal.desktop.gtk` for the GTK/GNOME
 * implementation. The returned string remains valid as long as
 * a reference to @self is held.
 *
 * Returns: (transfer none) (not nullable): #SrtXdgPortalBackend:name
 */
const char *
srt_xdg_portal_backend_get_name (SrtXdgPortalBackend *self)
{
  g_return_val_if_fail (SRT_IS_XDG_PORTAL_BACKEND (self), NULL);
  return self->name;
}

/**
 * srt_xdg_portal_backend_is_available:
 * @self: an XDG portal backend object
 *
 * Return %TRUE if the XDG portal backend is available.
 *
 * Returns: #SrtXdgPortalBackend:is-available
 */
gboolean
srt_xdg_portal_backend_is_available (SrtXdgPortalBackend *self)
{
  g_return_val_if_fail (SRT_IS_XDG_PORTAL_BACKEND (self), FALSE);
  return self->available;
}

/**
 * srt_xdg_portal_interface_get_name:
 * @self: an XDG portal interface object
 *
 * Return the name of the XDG portal interface, for example
 * `org.freedesktop.portal.Email`. The returned string
 * remains valid as long as a reference to @self is held.
 *
 * Returns: (transfer none) (not nullable): #SrtXdgPortalInterface:name
 */
const char *
srt_xdg_portal_interface_get_name (SrtXdgPortalInterface *self)
{
  g_return_val_if_fail (SRT_IS_XDG_PORTAL_INTERFACE (self), NULL);
  return self->name;
}

/**
 * srt_xdg_portal_interface_is_available:
 * @self: an XDG portal interface object
 *
 * Return %TRUE if the XDG portal interface is available.
 *
 * Returns: #SrtXdgPortalInterface:is-available
 */
gboolean
srt_xdg_portal_interface_is_available (SrtXdgPortalInterface *self)
{
  g_return_val_if_fail (SRT_IS_XDG_PORTAL_INTERFACE (self), FALSE);
  return self->available;
}

/**
 * srt_xdg_portal_interface_get_version:
 * @self: an XDG portal interface object
 *
 * Return the version property of the XDG portal interface,
 * or 0 if unknown or unavailable.
 *
 * Returns: #SrtXdgPortalInterface:version
 */
guint32
srt_xdg_portal_interface_get_version (SrtXdgPortalInterface *self)
{
  g_return_val_if_fail (SRT_IS_XDG_PORTAL_INTERFACE (self), 0);
  return self->version;
}
