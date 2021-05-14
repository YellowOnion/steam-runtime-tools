/*
 * Copyright 2018-2021 Wim Taymans
 * Copyright 2021 Collabora Ltd.
 *
 * SPDX-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "wrap-pipewire.h"

/* From Pipewire 0.3.27 */
#define PW_DEFAULT_REMOTE "pipewire-0"
#define DEFAULT_SYSTEM_RUNTIME_DIR "/run/pipewire"

/* Adapted from Pipewire 0.3.27 */
static const char *
get_remote (void)
{
  const char *name = NULL;

  name = getenv("PIPEWIRE_REMOTE");

  if (name == NULL || name[0] == '\0')
    name = PW_DEFAULT_REMOTE;

  return name;
}

/* Adapted from Pipewire 0.3.27 */
static const char *
get_runtime_dir (void)
{
  const char *runtime_dir;

  runtime_dir = g_getenv ("PIPEWIRE_RUNTIME_DIR");

  if (runtime_dir == NULL)
    runtime_dir = g_getenv ("XDG_RUNTIME_DIR");

  if (runtime_dir == NULL)
    runtime_dir = g_getenv ("HOME");

  if (runtime_dir == NULL)
    runtime_dir = g_getenv ("USERPROFILE");

  if (runtime_dir == NULL)
    runtime_dir = g_get_home_dir ();

  return runtime_dir;
}

void
pv_wrap_add_pipewire_args (FlatpakBwrap *sharing_bwrap,
                           PvEnviron *container_env)
{
  g_autoptr(GDir) dir = NULL;
  const char *remote = get_remote ();
  const char *runtime_dir = get_runtime_dir ();
  const char *member;

  /* Make Pipewire look in the container's XDG_RUNTIME_DIR */
  pv_environ_lock_env (container_env, "PIPEWIRE_RUNTIME_DIR", NULL);

  if (g_file_test (DEFAULT_SYSTEM_RUNTIME_DIR, G_FILE_TEST_IS_DIR))
    flatpak_bwrap_add_args (sharing_bwrap,
                            "--ro-bind",
                              DEFAULT_SYSTEM_RUNTIME_DIR,
                              DEFAULT_SYSTEM_RUNTIME_DIR,
                            NULL);

  dir = g_dir_open (runtime_dir, 0, NULL);

  if (dir == NULL)
    return;

  for (member = g_dir_read_name (dir);
       member != NULL;
       member = g_dir_read_name (dir))
    {
      /* Assume that anything starting with pipewire- is a (default or
       * extra) Pipewire socket */
      if (g_str_has_prefix (member, "pipewire-"))
        {
          g_autofree gchar *host_socket =
            g_build_filename (runtime_dir, member, NULL);
          g_autofree gchar *container_socket =
            g_strdup_printf ("/run/user/%d/%s", getuid (), member);

          flatpak_bwrap_add_args (sharing_bwrap,
                                  "--ro-bind",
                                    host_socket,
                                    container_socket,
                                  NULL);
        }
    }

  if (!g_str_has_prefix (remote, "pipewire-"))
    {
      /* If the configured Pipewire socket is something weird, remap it
       * to be named pv-pipewire to avoid colliding with anything else */
      g_autofree gchar *host_socket =
        g_build_filename (runtime_dir, remote, NULL);

      if (g_file_test (host_socket, G_FILE_TEST_EXISTS))
        {
          g_autofree gchar *container_socket =
            g_strdup_printf ("/run/user/%d/pv-pipewire", getuid ());

          pv_environ_lock_env (container_env, "PIPEWIRE_REMOTE", "pv-pipewire");
          flatpak_bwrap_add_args (sharing_bwrap,
                                  "--ro-bind",
                                    host_socket,
                                    container_socket,
                                  NULL);
        }
      else
        {
          pv_environ_lock_env (container_env, "PIPEWIRE_REMOTE", NULL);
        }
    }
}
