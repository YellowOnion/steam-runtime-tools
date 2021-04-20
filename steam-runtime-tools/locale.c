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

#include "steam-runtime-tools/locale.h"

#include "steam-runtime-tools/architecture.h"
#include "steam-runtime-tools/enums.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "steam-runtime-tools/json-utils-internal.h"
#include "steam-runtime-tools/locale-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include <sys/types.h>
#include <sys/wait.h>

#include <json-glib/json-glib.h>

/**
 * SECTION:locale
 * @title: Locale information
 * @short_description: Information about languages, character sets etc.
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtLocale is an opaque object representing a locale.
 * This is a reference-counted object: use g_object_ref() and
 * g_object_unref() to manage its lifecycle.
 */

G_DEFINE_QUARK (srt-locale-error-quark, srt_locale_error)

struct _SrtLocale
{
  /*< private >*/
  GObject parent;
  GQuark requested;
  GQuark result;
  GQuark charset;
  gboolean is_utf8;
};

struct _SrtLocaleClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum {
  PROP_0,
  PROP_REQUESTED_NAME,
  PROP_RESULTING_NAME,
  PROP_CHARSET,
  PROP_IS_UTF8,
  N_PROPERTIES
};

G_DEFINE_TYPE (SrtLocale, srt_locale, G_TYPE_OBJECT)

static void
srt_locale_init (SrtLocale *self)
{
}

static void
srt_locale_get_property (GObject *object,
                         guint prop_id,
                         GValue *value,
                         GParamSpec *pspec)
{
  SrtLocale *self = SRT_LOCALE (object);

  switch (prop_id)
    {
      case PROP_REQUESTED_NAME:
        g_value_set_string (value, g_quark_to_string (self->requested));
        break;

      case PROP_RESULTING_NAME:
        g_value_set_string (value, g_quark_to_string (self->result));
        break;

      case PROP_CHARSET:
        g_value_set_string (value, g_quark_to_string (self->charset));
        break;

      case PROP_IS_UTF8:
        g_value_set_boolean (value, self->is_utf8);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_locale_set_property (GObject *object,
                         guint prop_id,
                         const GValue *value,
                         GParamSpec *pspec)
{
  SrtLocale *self = SRT_LOCALE (object);

  switch (prop_id)
    {
      case PROP_REQUESTED_NAME:
        /* Intern the string since we only expect to deal with a few values */
        self->requested = g_quark_from_string (g_value_get_string (value));
        break;

      case PROP_RESULTING_NAME:
        self->result = g_quark_from_string (g_value_get_string (value));
        break;

      case PROP_CHARSET:
        self->charset = g_quark_from_string (g_value_get_string (value));
        break;

      case PROP_IS_UTF8:
        self->is_utf8 = g_value_get_boolean (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void
srt_locale_class_init (SrtLocaleClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_locale_get_property;
  object_class->set_property = srt_locale_set_property;

  properties[PROP_REQUESTED_NAME] =
    g_param_spec_string ("requested-name", "Requested name",
                         "The locale name that was checked",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_RESULTING_NAME] =
    g_param_spec_string ("resulting-name", "Resulting name",
                         "The locale name that was the result of calling "
                         "setlocale()",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_CHARSET] =
    g_param_spec_string ("charset", "Character set",
                         "The name of a character set, typically UTF-8",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_IS_UTF8] =
    g_param_spec_boolean ("is-utf8", "Is UTF-8?",
                          "TRUE if the character set is UTF-8",
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

/*
 * _srt_check_locale:
 * @envp: Environment variables
 * @helpers_path: Path to find helper executables
 * @multiarch_tuple: Multiarch tuple of helper executable to use
 * @requested_name: The locale name to check for
 * @error: Used to return an error if %NULL is returned
 *
 * Check whether the given locale can be set.
 *
 * On success, a #SrtLocale object with more details is returned.
 * On failure, an error in the %SRT_LOCALE_ERROR domain is set.
 *
 * Returns: (transfer full): A #SrtLocale object, or %NULL
 */
SrtLocale *
_srt_check_locale (gchar **envp,
                   const char *helpers_path,
                   const char *multiarch_tuple,
                   const char *requested_name,
                   GError **error)
{
  GPtrArray *argv = NULL;
  gchar *output = NULL;
  g_autoptr(JsonNode) node = NULL;
  JsonObject *object = NULL;
  SrtLocale *ret = NULL;
  GStrv my_environ = NULL;
  int exit_status;

  g_return_val_if_fail (error == NULL || *error == NULL, NULL);
  g_return_val_if_fail (envp != NULL, NULL);
  g_return_val_if_fail (requested_name != NULL, NULL);
  g_return_val_if_fail (_srt_check_not_setuid (), NULL);

  if (multiarch_tuple == NULL)
    multiarch_tuple = _SRT_MULTIARCH;

  argv = _srt_get_helper (helpers_path, multiarch_tuple, "check-locale",
                          SRT_HELPER_FLAGS_NONE, error);

  if (argv == NULL)
    goto out;

  my_environ = _srt_filter_gameoverlayrenderer_from_envp (envp);

  g_ptr_array_add (argv, g_strdup (requested_name));

  g_ptr_array_add (argv, NULL);

  g_debug ("Running %s %s",
           (const char *) g_ptr_array_index (argv, 0),
           (const char *) g_ptr_array_index (argv, 1));

  if (!g_spawn_sync (NULL,    /* working directory */
                     (gchar **) argv->pdata,
                     my_environ,
                     G_SPAWN_DEFAULT,
                     _srt_child_setup_unblock_signals,
                     NULL,    /* user data */
                     &output, /* stdout */
                     NULL,    /* stderr */
                     &exit_status,
                     error))
    {
      g_debug ("-> g_spawn error");
      goto out;
    }

  if (!WIFEXITED (exit_status))
    {
      g_debug ("-> wait status: %d", exit_status);
      g_set_error (error, SRT_LOCALE_ERROR, SRT_LOCALE_ERROR_INTERNAL_ERROR,
                   "Unhandled wait status %d (killed by signal?)",
                   exit_status);
      goto out;
    }

  exit_status = WEXITSTATUS (exit_status);
  g_debug ("-> exit status: %d", exit_status);

  if (exit_status != 0 && exit_status != 1)
    {
      g_set_error (error, SRT_LOCALE_ERROR, SRT_LOCALE_ERROR_INTERNAL_ERROR,
                   "Unhandled exit status %d", exit_status);
      goto out;
    }

  node = json_from_string (output, error);
  if (node == NULL)
    {
      g_debug ("-> invalid JSON");
      goto out;
    }

  object = json_node_get_object (node);

  if (exit_status == 1)
    {
      if (json_object_has_member (object, "error"))
        {
          g_debug ("-> %s",
                   json_object_get_string_member (object, "error"));
          g_set_error (error, SRT_LOCALE_ERROR, SRT_LOCALE_ERROR_FAILED, "%s",
                       json_object_get_string_member (object, "error"));
        }
      else
        {
          g_debug ("-> unknown error");
          g_set_error (error, SRT_LOCALE_ERROR, SRT_LOCALE_ERROR_FAILED,
                       "Unknown error setting locale \"%s\"", requested_name);
        }

      goto out;
    }

  if (!json_object_has_member (object, "charset")
      || !json_object_has_member (object, "is_utf8")
      || !json_object_has_member (object, "result"))
    {
      g_debug ("-> required fields not set");
      g_set_error (error, SRT_LOCALE_ERROR, SRT_LOCALE_ERROR_INTERNAL_ERROR,
                   "Helper subprocess did not return required fields");
      goto out;
    }

  ret = _srt_locale_new (requested_name,
                         json_object_get_string_member (object, "result"),
                         json_object_get_string_member (object, "charset"),
                         json_object_get_boolean_member (object, "is_utf8"));

  g_debug ("-> %s (charset=%s) (utf8=%s)",
           srt_locale_get_resulting_name (ret),
           srt_locale_get_charset (ret),
           srt_locale_is_utf8 (ret) ? "yes" : "no");

out:
  if (error != NULL
      && *error != NULL
      && (*error)->domain != SRT_LOCALE_ERROR)
    {
      g_prefix_error (error, "Unable to check whether locale \"%s\" works: ",
                      requested_name);
      (*error)->domain = SRT_LOCALE_ERROR;
      (*error)->code = SRT_LOCALE_ERROR_INTERNAL_ERROR;
    }

  g_clear_pointer (&argv, g_ptr_array_unref);
  g_free (output);
  g_strfreev (my_environ);
  return ret;
}

/**
 * _srt_locale_get_locale_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for the locale's
 *  properties, e.g. "resulting-name" and "charset"
 * @requested_name: (not nullable): The locale name related to the provided
 *  @json_obj
 * @error: Used to raise an error on failure
 *
 * Returns: A new #SrtLocale or %NULL if an error occurred.
 */
SrtLocale *
_srt_locale_get_locale_from_report (JsonObject *json_obj,
                                    const gchar *requested_name,
                                    GError **error)
{
  SrtLocale *locale = NULL;
  const gchar *resulting_name = NULL;
  const gchar *charset = NULL;
  gboolean is_utf8 = FALSE;
  GQuark error_domain;
  gint error_code;
  const gchar *error_message;

  g_return_val_if_fail (json_obj != NULL, NULL);
  g_return_val_if_fail (requested_name != NULL, NULL);
  g_return_val_if_fail (error != NULL && *error == NULL, NULL);

  resulting_name = json_object_get_string_member_with_default (json_obj, "resulting-name", NULL);
  charset = json_object_get_string_member_with_default (json_obj, "charset", NULL);
  is_utf8 = json_object_get_boolean_member_with_default (json_obj, "is_utf8", FALSE);
  error_domain = g_quark_from_string (json_object_get_string_member_with_default (json_obj,
                                                                                  "error-domain",
                                                                                  NULL));
  error_code = json_object_get_int_member_with_default (json_obj, "error-code", -1);
  error_message = json_object_get_string_member_with_default (json_obj, "error",
                                                              "(missing error message)");

  if (resulting_name != NULL && charset != NULL)
    {
      locale = _srt_locale_new (requested_name,
                                resulting_name,
                                charset,
                                is_utf8);
    }
  else
    {
      if (error_domain == 0)
        {
          error_domain = G_IO_ERROR;
          error_code = G_IO_ERROR_FAILED;
        }
      g_set_error_literal (error,
                           error_domain,
                           error_code,
                           error_message);
    }

  return locale;
}

/**
 * _srt_locale_get_issues_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "locale-issues"
 *  property
 *
 * If the provided @json_obj doesn't have a "locale-issues" member, or it is
 * malformed, %SRT_LOCALE_ISSUES_UNKNOWN will be returned.
 * If @json_obj has some elements that we can't parse,
 * %SRT_LOCALE_ISSUES_UNKNOWN will be added to the returned #SrtLocaleIssues.
 *
 * Returns: The %SrtLocaleIssues that has been found
 */
SrtLocaleIssues
_srt_locale_get_issues_from_report (JsonObject *json_obj)
{
  g_return_val_if_fail (json_obj != NULL, SRT_LOCALE_ISSUES_UNKNOWN);

  return srt_get_flags_from_json_array (SRT_TYPE_LOCALE_ISSUES,
                                        json_obj,
                                        "locale-issues",
                                        SRT_LOCALE_ISSUES_UNKNOWN);
}

/**
 * srt_locale_get_requested_name:
 * @self: a locale object
 *
 * Return the name of the locale that was checked.
 *
 * Returns: #SrtLocale:requested-name
 */
const char *
srt_locale_get_requested_name (SrtLocale *self)
{
  g_return_val_if_fail (SRT_IS_LOCALE (self), NULL);
  return g_quark_to_string (self->requested);
}

/**
 * srt_locale_get_resulting_name:
 * @self: a locale object
 *
 * Return the name of the locale that was actually set when
 * #SrtLocale:requested-name was requested. For example, if
 * #SrtLocale:requested-name is `POSIX`, the locale that is actually
 * set will typically be named `C`.
 *
 * Returns: #SrtLocale:resulting-name
 */
const char *
srt_locale_get_resulting_name (SrtLocale *self)
{
  g_return_val_if_fail (SRT_IS_LOCALE (self), NULL);
  return g_quark_to_string (self->result);
}

/**
 * srt_locale_get_charset:
 * @self: a locale object
 *
 * Return the character set used by the locale, hopefully `UTF-8`.
 *
 * Returns: #SrtLocale:charset
 */
const char *
srt_locale_get_charset (SrtLocale *self)
{
  g_return_val_if_fail (SRT_IS_LOCALE (self), NULL);
  return g_quark_to_string (self->charset);
}

/**
 * srt_locale_is_utf8:
 * @self: a locale object
 *
 * Return %TRUE if the locale appears to be a UTF-8 locale.
 * For example, `C.UTF-8` and `en_US.UTF-8` are UTF-8 locales,
 * but `C` and `en_US` are typically not.
 *
 * Returns: #SrtLocale:is-utf8
 */
gboolean
srt_locale_is_utf8 (SrtLocale *self)
{
  g_return_val_if_fail (SRT_IS_LOCALE (self), FALSE);
  return self->is_utf8;
}
