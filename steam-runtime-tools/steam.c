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

#include "steam-runtime-tools/steam.h"
#include "steam-runtime-tools/steam-internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include "steam-runtime-tools/desktop-entry-internal.h"
#include "steam-runtime-tools/enums.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

/**
 * SECTION:steam
 * @title: Steam installation
 * @short_description: Information about the Steam installation
 * @include: steam-runtime-tools/steam-runtime-tools.h
 *
 * #SrtSteamIssues represents problems encountered with the Steam
 * installation.
 */

struct _SrtSteam
{
  /*< private >*/
  GObject parent;
  SrtSteamIssues issues;
  gchar *install_path;
  gchar *data_path;
  gchar *bin32_path;
};

struct _SrtSteamClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum {
  PROP_0,
  PROP_ISSUES,
  PROP_INSTALL_PATH,
  PROP_DATA_PATH,
  PROP_BIN32_PATH,
  N_PROPERTIES
};

G_DEFINE_TYPE (SrtSteam, srt_steam, G_TYPE_OBJECT)

static void
srt_steam_init (SrtSteam *self)
{
}

static void
srt_steam_get_property (GObject *object,
                        guint prop_id,
                        GValue *value,
                        GParamSpec *pspec)
{
  SrtSteam *self = SRT_STEAM (object);

  switch (prop_id)
    {
      case PROP_ISSUES:
        g_value_set_flags (value, self->issues);
        break;

      case PROP_INSTALL_PATH:
        g_value_set_string (value, self->install_path);
        break;

      case PROP_DATA_PATH:
        g_value_set_string (value, self->data_path);
        break;

      case PROP_BIN32_PATH:
        g_value_set_string (value, self->bin32_path);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_steam_set_property (GObject *object,
                        guint prop_id,
                        const GValue *value,
                        GParamSpec *pspec)
{
  SrtSteam *self = SRT_STEAM (object);

  switch (prop_id)
    {
      case PROP_ISSUES:
        /* Construct-only */
        g_return_if_fail (self->issues == 0);
        self->issues = g_value_get_flags (value);
        break;

      case PROP_INSTALL_PATH:
        /* Construct only */
        g_return_if_fail (self->install_path == NULL);
        self->install_path = g_value_dup_string (value);
        break;

      case PROP_DATA_PATH:
        /* Construct only */
        g_return_if_fail (self->data_path == NULL);
        self->data_path = g_value_dup_string (value);
        break;

      case PROP_BIN32_PATH:
        /* Construct only */
        g_return_if_fail (self->bin32_path == NULL);
        self->bin32_path = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_steam_finalize (GObject *object)
{
  SrtSteam *self = SRT_STEAM (object);

  g_free (self->install_path);
  g_free (self->data_path);
  g_free (self->bin32_path);

  G_OBJECT_CLASS (srt_steam_parent_class)->finalize (object);
}

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void
srt_steam_class_init (SrtSteamClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_steam_get_property;
  object_class->set_property = srt_steam_set_property;
  object_class->finalize = srt_steam_finalize;

  properties[PROP_ISSUES] =
    g_param_spec_flags ("issues", "Issues", "Problems with the steam installation",
                        SRT_TYPE_STEAM_ISSUES, SRT_STEAM_ISSUES_NONE,
                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS);

  properties[PROP_INSTALL_PATH] =
    g_param_spec_string ("install-path", "Install path",
                         "Absolute path to the Steam installation",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_DATA_PATH] =
    g_param_spec_string ("data-path", "Data path",
                         "Absolute path to the Steam data directory, which is usually "
                         "the same as install-path, but may be different while "
                         "testing a new Steam release",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_BIN32_PATH] =
    g_param_spec_string ("bin32-path", "Bin32 path",
                         "Absolute path to `ubuntu12_32`",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

/**
 * srt_steam_get_issues:
 * @self: The #SrtSteam object
 *
 * Return the problems found in @self.
 *
 * Returns: A bitfield containing problems, or %SRT_STEAM_ISSUES_NONE
 *  if no problems were found
 */
SrtSteamIssues
srt_steam_get_issues (SrtSteam *self)
{
  g_return_val_if_fail (SRT_IS_STEAM (self), SRT_STEAM_ISSUES_UNKNOWN);
  return self->issues;
}
/**
 * srt_steam_get_install_path:
 * @self: The #SrtSteam object
 *
 * Returns: (nullable) (type filename): The absolute path to the Steam
 *  installation, which is valid as long as @self is not destroyed.
 */
const char *
srt_steam_get_install_path (SrtSteam *self)
{
  g_return_val_if_fail (SRT_IS_STEAM (self), NULL);
  return self->install_path;
}

/**
 * srt_steam_get_data_path:
 * @self: The #SrtSteam object
 *
 * Used to return the absolute path to the Steam data directory, which
 * is usually the same as `srt_steam_get_install_path`, but may be different
 * while testing a new Steam release.
 *
 * Returns: (nullable) (type filename): The absolute path to the Steam data
 *  directory, which is valid as long as @self is not destroyed.
 */
const char *
srt_steam_get_data_path (SrtSteam *self)
{
  g_return_val_if_fail (SRT_IS_STEAM (self), NULL);
  return self->data_path;
}

/**
 * srt_steam_get_bin32_path:
 * @self: The #SrtSteam object
 *
 * Returns: (nullable) (type filename): The absolute path to `ubuntu12_32`,
 *  which is valid as long as @self is not destroyed.
 */
const char *
srt_steam_get_bin32_path (SrtSteam *self)
{
  g_return_val_if_fail (SRT_IS_STEAM (self), NULL);
  return self->bin32_path;
}

/**
 * _srt_steam_check:
 * @my_environ: (nullable): The list of environment variables to use.
 * @more_details_out: (out) (optional) (transfer full): Used to return an
 *  #SrtSteam object representing information about the current Steam
 *  installation. Free with `g_object_unref()`.
 *
 * Please note that @my_environ can't be used when checking the default
 * desktop entry that handles `steam:` URIs.
 *
 * Returns: A bitfield containing problems, or %SRT_STEAM_ISSUES_NONE
 *  if no problems were found
 */
SrtSteamIssues
_srt_steam_check (const GStrv my_environ,
                  SrtSteam **more_details_out)
{
  SrtSteamIssues issues = SRT_STEAM_ISSUES_NONE;
  gchar *dot_steam_bin32 = NULL;
  gchar *dot_steam_steam = NULL;
  gchar *dot_steam_root = NULL;
  gchar *default_steam_path = NULL;
  GAppInfo *default_app = NULL;
  char *install_path = NULL;
  char *data_path = NULL;
  char *bin32 = NULL;
  const char *home = NULL;
  const char *user_data = NULL;
  const char *steam_script = NULL;
  const char *commandline = NULL;
  const char *executable = NULL;
  const char *app_id = NULL;
  gboolean in_flatpak = FALSE;
  GStrv env = NULL;
  GError *error = NULL;

  g_return_val_if_fail (_srt_check_not_setuid (), SRT_STEAM_ISSUES_UNKNOWN);
  g_return_val_if_fail (more_details_out == NULL || *more_details_out == NULL,
                        SRT_STEAM_ISSUES_UNKNOWN);

  env = (my_environ == NULL) ? g_get_environ () : g_strdupv (my_environ);

  home = g_environ_getenv (env, "HOME");
  user_data = g_environ_getenv (env, "XDG_DATA_HOME");

  if (home == NULL)
    home = g_get_home_dir ();

  if (user_data == NULL)
    user_data = g_get_user_data_dir ();

  default_steam_path = g_build_filename (user_data,
                                         "Steam", NULL);

  dot_steam_bin32 = g_build_filename (home, ".steam", "bin32", NULL);
  dot_steam_steam = g_build_filename (home, ".steam", "steam", NULL);
  dot_steam_root = g_build_filename (home, ".steam", "root", NULL);

  /* Canonically, ~/.steam/steam is a symlink to the Steam data directory.
   * This is used to install games, for example. It is *not* used to
   * install the Steam client itself.
   *
   * (This is ignoring the Valve-internal "beta universe", which uses
   * ~/.steam/steambeta instead, and is not open to the public.) */
  if (g_file_test (dot_steam_steam, G_FILE_TEST_IS_SYMLINK))
    {
      data_path = realpath (dot_steam_steam, NULL);

      if (data_path == NULL)
        g_debug ("realpath(%s): %s", dot_steam_steam, g_strerror (errno));
    }
  else if (g_file_test (dot_steam_steam, G_FILE_TEST_IS_DIR))
    {
      /* e.g. https://bugs.debian.org/916303 */
      issues |= SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_SYMLINK;

      data_path = realpath (dot_steam_steam, NULL);

      if (data_path == NULL)
        g_debug ("realpath(%s): %s", dot_steam_steam, g_strerror (errno));
    }
  else
    {
      /* e.g. https://bugs.debian.org/916303 */
      issues |= SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_SYMLINK;
    }

  if (data_path == NULL
      || !g_file_test (data_path, G_FILE_TEST_IS_DIR))
    issues |= SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_DIRECTORY;

  /* Canonically, ~/.steam/root is a symlink to the Steam installation.
   * This is *usually* the same thing as the Steam data directory, but
   * it can be different when testing a new Steam client build. */
  if (g_file_test (dot_steam_root, G_FILE_TEST_IS_SYMLINK))
    {
      install_path = realpath (dot_steam_root, NULL);

      if (install_path == NULL)
        g_debug ("realpath(%s): %s", dot_steam_root, g_strerror (errno));
    }
  else
    {
      issues |= SRT_STEAM_ISSUES_DOT_STEAM_ROOT_NOT_SYMLINK;
    }

  if (install_path == NULL
      || !g_file_test (install_path, G_FILE_TEST_IS_DIR))
    issues |= SRT_STEAM_ISSUES_DOT_STEAM_ROOT_NOT_DIRECTORY;

  /* If ~/.steam/root doesn't work, try going up one level from
   * ubuntu12_32, to which ~/.steam/bin32 is a symlink */
  if (install_path == NULL &&
      g_file_test (dot_steam_bin32, G_FILE_TEST_IS_SYMLINK))
    {
      bin32 = realpath (dot_steam_bin32, NULL);

      if (bin32 == NULL)
        {
          g_debug ("realpath(%s): %s", dot_steam_bin32, g_strerror (errno));
        }
      else if (!g_str_has_suffix (bin32, "/ubuntu12_32"))
        {
          g_debug ("Unexpected bin32 path: %s -> %s", dot_steam_bin32,
                   bin32);
        }
      else
        {
          install_path = strndup (bin32, strlen (bin32) - strlen ("/ubuntu12_32"));

          /* We don't try to survive out-of-memory */
          if (install_path == NULL)
            g_error ("strndup: %s", g_strerror (errno));
        }
    }

  /* If we have an installation path but no data path, or vice versa,
   * assume they match. */
  if (install_path == NULL && data_path != NULL)
    {
      install_path = strdup (data_path);

      /* We don't try to survive out-of-memory */
      if (install_path == NULL)
        g_error ("strdup: %s", g_strerror (errno));
    }

  if (data_path == NULL && install_path != NULL)
    {
      data_path = strdup (install_path);

      /* We don't try to survive out-of-memory */
      if (data_path == NULL)
        g_error ("strdup: %s", g_strerror (errno));
    }

  /* If *that* doesn't work, try the default installation location. */
  if (install_path == NULL)
    {
      install_path = realpath (default_steam_path, NULL);

      if (install_path == NULL)
        g_debug ("realpath(%s): %s", default_steam_path, g_strerror (errno));
    }

  if (data_path == NULL)
    {
      data_path = realpath (default_steam_path, NULL);

      if (data_path == NULL)
        g_debug ("realpath(%s): %s", default_steam_path, g_strerror (errno));
    }

  if (install_path == NULL)
    {
      g_debug ("Unable to find Steam installation");
      issues |= SRT_STEAM_ISSUES_CANNOT_FIND;
    }
  else
    {
      g_debug ("Found Steam installation at %s", install_path);

      /* If we haven't found ubuntu12_32 yet, it's a subdirectory of the
       * Steam installation */
      if (bin32 == NULL)
        {
          /* We don't try to survive out-of-memory */
          if (asprintf (&bin32, "%s/ubuntu12_32", install_path) < 0)
            g_error ("asprintf: %s", g_strerror (errno));
        }

      if (bin32 != NULL)
        {
          g_debug ("Found ubuntu12_32 directory at %s", bin32);
        }
      else
        {
          g_debug ("Unable to find ubuntu12_32 directory");
        }
    }

  if (data_path == NULL)
    {
      g_debug ("Unable to find Steam data");
      issues |= SRT_STEAM_ISSUES_CANNOT_FIND_DATA;
    }
  else
    {
      g_debug ("Found Steam data at %s", data_path);
    }

  default_app = g_app_info_get_default_for_uri_scheme ("steam");

  if (default_app == NULL)
    {
      GList *desktop_entries = _srt_list_steam_desktop_entries ();
      /* If we are running from the Flatpak version of Steam we can't tell
       * which one is the default `steam` URI handler.
       * So we just list them all and check if we have the known
       * "com.valvesoftware.Steam.desktop" that is used in the Flathub's
       * version of Steam */
      for (GList *iter = desktop_entries; iter != NULL; iter = iter->next)
        {
          if (g_strcmp0 (srt_desktop_entry_get_id (iter->data), "com.valvesoftware.Steam.desktop") != 0)
            continue;

          /* If we have the desktop entry "com.valvesoftware.Steam.desktop"
           * with a commandline that starts with "/app/bin/" we are fairly
           * sure to be inside a Flatpak environment. Otherwise report the
           * issues about the missing and unexpected Steam URI handler */
          commandline = srt_desktop_entry_get_commandline (iter->data);
          if (g_str_has_prefix (commandline, "/app/bin/")
              && g_str_has_suffix (commandline, "%U"))
            {
              g_debug ("It seems like this is a Flatpak environment. The missing default app for `steam:` URLs is not an issue");
              in_flatpak = TRUE;
            }
          else
            {
              issues |= SRT_STEAM_ISSUES_UNEXPECTED_STEAM_URI_HANDLER;
            }
        }

      if (!in_flatpak)
        {
          g_debug ("There isn't a default app that can handle `steam:` URLs");
          issues |= SRT_STEAM_ISSUES_MISSING_STEAM_URI_HANDLER;
        }
    }
  else
    {
      executable = g_app_info_get_executable (default_app);
      commandline = g_app_info_get_commandline (default_app);
      app_id = g_app_info_get_id (default_app);
      gboolean found_expected_steam_uri_handler = FALSE;

      if (commandline != NULL)
        {
          gchar **argvp;
          if (g_shell_parse_argv (commandline, NULL, &argvp, &error))
            {
              const char *const expectations[] = { executable, "%U", NULL };
              gsize i;
              for (i = 0; argvp[i] != NULL && expectations[i] != NULL; i++)
                {
                  if (g_strcmp0 (argvp[i], expectations[i]) != 0)
                    break;
                }

              if (argvp[i] == NULL && expectations[i] == NULL)
                found_expected_steam_uri_handler = TRUE;

              g_strfreev (argvp);
            }
          else
            {
              g_debug ("Cannot parse \"Exec=%s\" like a shell would: %s", commandline, error->message);
              g_clear_error (&error);
            }

          if (!found_expected_steam_uri_handler)
            {
              /* If we are running from the host system, do not flag the Flatpak
               * version of Steam as unexpected URI handler */
              if (g_str_has_prefix (commandline, executable)
                  && g_str_has_suffix (commandline, "com.valvesoftware.Steam @@u %U @@")
                  && g_pattern_match_simple ("* --command=/app/bin/*", commandline))
                {
                  found_expected_steam_uri_handler = TRUE;
                }
            }
        }

      /* Exclude the special case `/usr/bin/env steam %U` that we use in our unit tests */
      if (!found_expected_steam_uri_handler && (g_strcmp0 (commandline, "/usr/bin/env steam %U") != 0))
        issues |= SRT_STEAM_ISSUES_UNEXPECTED_STEAM_URI_HANDLER;

      if (g_strcmp0 (app_id, "steam.desktop") != 0 && g_strcmp0 (app_id, "com.valvesoftware.Steam.desktop") != 0)
        {
          g_debug ("The default Steam app handler id is not what we expected: %s", app_id ? app_id : "NULL");
          issues |= SRT_STEAM_ISSUES_UNEXPECTED_STEAM_DESKTOP_ID;
        }
    }

  steam_script = g_environ_getenv (env, "STEAMSCRIPT");
  if (steam_script == NULL)
    {
      g_debug ("\"STEAMSCRIPT\" environment variable is missing");
      issues |= SRT_STEAM_ISSUES_STEAMSCRIPT_NOT_IN_ENVIRONMENT;

      if (executable != NULL &&
          g_strcmp0 (executable, "/usr/bin/steam") != 0 &&
          /* Arch Linux steam.desktop */
          g_strcmp0 (executable, "/usr/bin/steam-runtime") != 0 &&
          /* Debian steam.desktop */
          g_strcmp0 (executable, "/usr/games/steam"))
        {
          g_debug ("The default Steam app executable is not what we expected: %s", executable);
          issues |= SRT_STEAM_ISSUES_UNEXPECTED_STEAM_URI_HANDLER;
        }
    }
  else
    {
      if (!in_flatpak
          && g_strcmp0 (executable, steam_script) != 0
          && g_strcmp0 (executable, "/usr/bin/flatpak") != 0)
        {
          g_debug ("Unexpectedly \"STEAMSCRIPT\" environment variable and the default Steam app "
                   "executable point to different paths: \"%s\" and \"%s\"", steam_script, executable);
          issues |= SRT_STEAM_ISSUES_UNEXPECTED_STEAM_URI_HANDLER;
        }
    }

  if (more_details_out != NULL)
    *more_details_out = _srt_steam_new (issues,
                                        install_path,
                                        data_path,
                                        bin32);

  free (install_path);
  free (data_path);
  free (bin32);
  g_free (dot_steam_bin32);
  g_free (dot_steam_steam);
  g_free (dot_steam_root);
  g_free (default_steam_path);
  g_strfreev (env);
  return issues;
}

/**
 * _srt_steam_get_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for
 *  "steam-installation" property
 *
 * If the provided @json_obj doesn't have a "steam-installation" member,
 * #SrtSteamIssues of the returned #SrtSteam will be set to
 * %SRT_STEAM_ISSUES_UNKNOWN.
 *
 * Returns: (transfer full): a new #StrSteam object.
 */
SrtSteam *
_srt_steam_get_from_report (JsonObject *json_obj)
{
  JsonObject *json_sub_obj;
  JsonArray *array;
  SrtSteamIssues issues = SRT_STEAM_ISSUES_UNKNOWN;
  const gchar *install_path = NULL;
  const gchar *data_path = NULL;
  const gchar *bin32_path = NULL;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (json_object_has_member (json_obj, "steam-installation"))
    {
      json_sub_obj = json_object_get_object_member (json_obj, "steam-installation");

      if (json_sub_obj == NULL)
        goto out;

      if (json_object_has_member (json_sub_obj, "issues"))
        {
          issues = SRT_STEAM_ISSUES_NONE;
          array = json_object_get_array_member (json_sub_obj, "issues");

          /* We are expecting an array of issues here */
          if (array == NULL)
            {
              g_debug ("'issues' in 'steam-installation' is not an array as expected");
              issues |= SRT_STEAM_ISSUES_UNKNOWN;
            }
          else
            {
              for (guint i = 0; i < json_array_get_length (array); i++)
                {
                  const gchar *issue_string = json_array_get_string_element (array, i);
                  if (!srt_add_flag_from_nick (SRT_TYPE_STEAM_ISSUES, issue_string, &issues, NULL))
                    issues |= SRT_STEAM_ISSUES_UNKNOWN;
                }
            }
        }

      if (json_object_has_member (json_sub_obj, "path"))
        install_path = json_object_get_string_member (json_sub_obj, "path");

      if (json_object_has_member (json_sub_obj, "data_path"))
        data_path = json_object_get_string_member (json_sub_obj, "data_path");

      if (json_object_has_member (json_sub_obj, "bin32_path"))
        bin32_path = json_object_get_string_member (json_sub_obj, "bin32_path");
    }

out:
  return _srt_steam_new (issues,
                         install_path,
                         data_path,
                         bin32_path);
}
