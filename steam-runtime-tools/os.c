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

#include "steam-runtime-tools/os-internal.h"

#include <string.h>

#include "steam-runtime-tools/glib-compat.h"

#include <glib.h>

#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

G_GNUC_INTERNAL void
_srt_os_release_init (SrtOsRelease *self)
{
  self->build_id = NULL;
  self->id = NULL;
  self->id_like = NULL;
  self->name = NULL;
  self->pretty_name = NULL;
  self->variant = NULL;
  self->variant_id = NULL;
  self->version_codename = NULL;
  self->version_id = NULL;
  self->populated = FALSE;
}

static const struct
{
  const char *path;
  gboolean only_in_run_host;
}
os_release_paths[] =
{
  { "/etc/os-release", FALSE },
  { "/usr/lib/os-release", FALSE },
  /* https://github.com/flatpak/flatpak/pull/3733 */
  { "/os-release", TRUE }
};

static void
do_line (SrtOsRelease *self,
         const char *path,
         gchar *line)
{
  char *equals;
  gchar *unquoted = NULL;
  gchar **dest = NULL;
  GError *local_error = NULL;

  /* Modify line in-place to strip leading and trailing whitespace */
  g_strstrip (line);

  if (line[0] == '\0' || line[0] == '#')
    return;

  g_debug ("%s: %s", path, line);

  equals = strchr (line, '=');

  if (equals == NULL)
    {
      g_debug ("Unable to parse line \"%s\" in %s: no \"=\" found",
               line, path);
      return;
    }

  unquoted = g_shell_unquote (equals + 1, &local_error);

  if (unquoted == NULL)
    {
      g_debug ("Unable to parse line \"%s\" in %s: %s",
               line, path, local_error->message);
      g_clear_error (&local_error);
      return;
    }

  *equals = '\0';

  if (strcmp (line, "BUILD_ID") == 0)
    dest = &self->build_id;
  else if (strcmp (line, "ID") == 0)
    dest = &self->id;
  else if (strcmp (line, "ID_LIKE") == 0)
    dest = &self->id_like;
  else if (strcmp (line, "NAME") == 0)
    dest = &self->name;
  else if (strcmp (line, "PRETTY_NAME") == 0)
    dest = &self->pretty_name;
  else if (strcmp (line, "VARIANT") == 0)
    dest = &self->variant;
  else if (strcmp (line, "VARIANT_ID") == 0)
    dest = &self->variant_id;
  else if (strcmp (line, "VERSION_CODENAME") == 0)
    dest = &self->version_codename;
  else if (strcmp (line, "VERSION_ID") == 0)
    dest = &self->version_id;

  if (dest != NULL)
    {
      if (*dest != NULL)
        {
          /* Using the last one matches the behaviour of a shell script
           * that uses ". /usr/lib/os-release" */
          g_debug ("%s appears more than once in %s, will use last instance",
                   line, path);
          g_free (*dest);
        }

      *dest = unquoted;
    }
  else
    {
      g_free (unquoted);
    }
}

G_GNUC_INTERNAL void
_srt_os_release_populate (SrtOsRelease *self,
                          const char *sysroot)
{
  gsize i;

  g_return_if_fail (_srt_check_not_setuid ());
  g_return_if_fail (!self->populated);
  g_return_if_fail (self->build_id == NULL);
  g_return_if_fail (self->id == NULL);
  g_return_if_fail (self->name == NULL);
  g_return_if_fail (self->pretty_name == NULL);
  g_return_if_fail (self->variant == NULL);
  g_return_if_fail (self->variant_id == NULL);
  g_return_if_fail (self->version_codename == NULL);
  g_return_if_fail (self->version_id == NULL);

  for (i = 0; i < G_N_ELEMENTS (os_release_paths); i++)
    {
      const char *path = os_release_paths[i].path;
      gboolean only_in_run_host = os_release_paths[i].only_in_run_host;
      gchar *built_path = NULL;
      gchar *contents = NULL;
      char *beginning_of_line;
      GError *local_error = NULL;
      gsize len;
      gsize j;

      if (only_in_run_host
          && (sysroot == NULL || !g_str_has_suffix (sysroot, "/run/host")))
        continue;

      if (sysroot != NULL)
        {
          built_path = g_build_filename (sysroot, path, NULL);
          path = built_path;
        }

      if (!g_file_get_contents (path, &contents, &len, &local_error))
        {
          g_debug ("Unable to open %s: %s", path, local_error->message);
          g_clear_error (&local_error);
          g_free (built_path);
          continue;
        }

      beginning_of_line = contents;

      for (j = 0; j < len; j++)
        {
          if (contents[j] == '\n')
            {
              contents[j] = '\0';
              do_line (self, path, beginning_of_line);
              /* g_file_get_contents adds an extra \0 at contents[len],
               * so this is safe to do without overrunning the buffer */
              beginning_of_line = contents + j + 1;
            }
        }

      /* Collect a possible partial line */
      do_line (self, path, beginning_of_line);

      g_free (contents);
      g_free (built_path);
      break;
    }

  /* Special case for the Steam Runtime: Flatpak-style scout images have
   * historically not had a VERSION_CODENAME in os-release(5), but
   * we know that version 1 is scout, so let's add it. */
  if (self->version_codename == NULL
      && g_strcmp0 (self->id, "steamrt") == 0
      && g_strcmp0 (self->version_id, "1") == 0)
    self->version_codename = g_strdup ("scout");

  /* Special case for the Steam Runtime: we got this wrong in the past. */
  if (g_strcmp0 (self->id_like, "ubuntu") == 0)
    {
      g_free (self->id_like);
      self->id_like = g_strdup ("ubuntu debian");
    }

  self->populated = TRUE;
}

G_GNUC_INTERNAL void
_srt_os_release_clear (SrtOsRelease *self)
{
  g_clear_pointer (&self->build_id, g_free);
  g_clear_pointer (&self->id, g_free);
  g_clear_pointer (&self->id_like, g_free);
  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->pretty_name, g_free);
  g_clear_pointer (&self->variant, g_free);
  g_clear_pointer (&self->variant_id, g_free);
  g_clear_pointer (&self->version_codename, g_free);
  g_clear_pointer (&self->version_id, g_free);
  self->populated = FALSE;
}

/**
 * _srt_os_release_populate_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "os-release"
 *  property
 * @self: (not nullable): A #SrtOsRelease object to populate
 *
 * If the provided @json_obj doesn't have a "os-release" member,
 * @self will be left untouched.
 */
void
_srt_os_release_populate_from_report (JsonObject *json_obj,
                                      SrtOsRelease *self)
{
  JsonObject *json_sub_obj;
  JsonArray *array;

  g_return_if_fail (json_obj != NULL);
  g_return_if_fail (self != NULL);
  g_return_if_fail (self->build_id == NULL);
  g_return_if_fail (self->id == NULL);
  g_return_if_fail (self->id_like == NULL);
  g_return_if_fail (self->name == NULL);
  g_return_if_fail (self->pretty_name == NULL);
  g_return_if_fail (self->variant == NULL);
  g_return_if_fail (self->variant_id == NULL);
  g_return_if_fail (self->version_codename == NULL);
  g_return_if_fail (self->version_id == NULL);

  if (json_object_has_member (json_obj, "os-release"))
    {
      json_sub_obj = json_object_get_object_member (json_obj, "os-release");

      if (json_sub_obj == NULL)
        {
          g_debug ("'os-release' is not a JSON object as expected");
          return;
        }

      self->populated = TRUE;

      if (json_object_has_member (json_sub_obj, "id"))
        self->id = g_strdup (json_object_get_string_member (json_sub_obj, "id"));

      if (json_object_has_member (json_sub_obj, "id_like"))
        {
          array = json_object_get_array_member (json_sub_obj, "id_like");
          /* We are expecting an array of OS IDs here */
          if (array == NULL)
            {
              g_debug ("'id_like' in 'os-release' is not an array as expected");
            }
          else
            {
              GString *str = g_string_new ("");
              guint length = json_array_get_length (array);
              for (guint i = 0; i < length; i++)
                {
                  const char *temp_id = json_array_get_string_element (array, i);

                  if (str->len > 0)
                    g_string_append_c (str, ' ');

                  g_string_append (str, temp_id);
                }
              self->id_like = g_string_free (str, FALSE);
            }
        }

      if (json_object_has_member (json_sub_obj, "name"))
        self->name = g_strdup (json_object_get_string_member (json_sub_obj, "name"));

      if (json_object_has_member (json_sub_obj, "pretty_name"))
        self->pretty_name = g_strdup (json_object_get_string_member (json_sub_obj, "pretty_name"));

      if (json_object_has_member (json_sub_obj, "version_id"))
        self->version_id = g_strdup (json_object_get_string_member (json_sub_obj, "version_id"));

      if (json_object_has_member (json_sub_obj, "version_codename"))
        self->version_codename = g_strdup (json_object_get_string_member (json_sub_obj, "version_codename"));

      if (json_object_has_member (json_sub_obj, "build_id"))
        self->build_id = g_strdup (json_object_get_string_member (json_sub_obj, "build_id"));

      if (json_object_has_member (json_sub_obj, "variant_id"))
        self->variant_id = g_strdup (json_object_get_string_member (json_sub_obj, "variant_id"));

      if (json_object_has_member (json_sub_obj, "variant"))
        self->variant = g_strdup (json_object_get_string_member (json_sub_obj, "variant"));
    }
}
