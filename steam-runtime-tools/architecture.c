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

#include "steam-runtime-tools/architecture.h"

#include <dlfcn.h>
#include <link.h>
#include <string.h>

static gchar *helpers_path = NULL;

static const char *
_srt_get_helpers_path (void)
{
  const char *path;
  void *handle = NULL;
  struct link_map *map = NULL;

  path = helpers_path;

  if (path != NULL)
    goto out;

  path = g_getenv ("SRT_HELPERS_PATH");

  if (path != NULL)
    goto out;

  handle = dlopen (NULL, RTLD_LAZY|RTLD_GLOBAL);

  if (handle == NULL)
    {
      g_warning ("Unable to dlopen self: %s", dlerror ());
      goto out;
    }

  if (dlinfo (handle, RTLD_DI_LINKMAP, &map) != 0)
    {
      g_warning ("dlinfo RTLD_DI_LINKMAP: %s", dlerror ());
      goto out;
    }

  while (map != NULL && map->l_prev != NULL)
    map = map->l_prev;

  for (; map != NULL; map = map->l_next)
    {
      g_debug ("%s", map->l_name);

      if (g_str_has_suffix (map->l_name, "/" _SRT_SONAME))
        {
          gchar *dir = g_path_get_dirname (map->l_name);

          if (g_str_has_suffix (dir, "/" _SRT_MULTIARCH))
            dir[strlen (dir) - strlen ("/" _SRT_MULTIARCH)] = '\0';

          if (g_str_has_suffix (dir, "/lib"))
            dir[strlen (dir) - strlen ("/lib")] = '\0';

          /* deliberate one-per-process leak */
          helpers_path = g_build_filename (
              dir, "libexec", "steam-runtime-tools-" _SRT_API_MAJOR,
              NULL);
          path = helpers_path;

          g_free (dir);
          break;
        }
    }

out:
  if (handle != NULL)
    dlclose (handle);

  /* We have to return *something* non-NULL */
  if (path == NULL)
    {
      g_warning ("Unable to determine path to helpers");
      path = "/";
    }

  return path;
}

static gboolean
_srt_architecture_can_run (const char *multiarch)
{
  gchar *helper = NULL;
  const gchar *argv[] = { "true", NULL };
  int exit_status = -1;
  GError *error = NULL;
  gboolean ret = FALSE;

  helper = g_strdup_printf ("%s/%s-true",
                            _srt_get_helpers_path (), multiarch);
  argv[0] = helper;
  g_debug ("Testing architecture %s with %s", multiarch, helper);

  if (!g_spawn_sync (NULL,    /* working directory */
                     (gchar **) argv,
                     NULL,    /* envp */
                     0,       /* flags */
                     NULL,    /* child setup */
                     NULL,    /* user data */
                     NULL,    /* stdout */
                     NULL,    /* stderr */
                     &exit_status,
                     &error))
    {
      g_debug ("... %s", error->message);
      goto out;
    }

  if (exit_status != 0)
    {
      g_debug ("... wait status %d", exit_status);
      goto out;
    }

  g_debug ("... it works");
  ret = TRUE;
out:
  g_free (helper);
  g_clear_error (&error);
  return ret;
}

gboolean
srt_architecture_can_run_i386 (void)
{
  return _srt_architecture_can_run ("i386-linux-gnu");
}

gboolean
srt_architecture_can_run_x86_64 (void)
{
  return _srt_architecture_can_run ("x86_64-linux-gnu");
}
