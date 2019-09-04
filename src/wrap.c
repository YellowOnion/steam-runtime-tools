/* pressure-vessel-wrap — run a program in a container that protects $HOME,
 * optionally using a Flatpak-style runtime.
 *
 * Contains code taken from Flatpak.
 *
 * Copyright © 2014-2019 Red Hat, Inc
 * Copyright © 2017-2019 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "subprojects/libglnx/config.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <X11/Xauth.h>

#include <locale.h>
#include <stdlib.h>
#include <sys/utsname.h>

#include "libglnx.h"

#include "bwrap.h"
#include "glib-backports.h"
#include "flatpak-bwrap-private.h"
#include "flatpak-utils-private.h"
#include "utils.h"
#include "wrap-interactive.h"

/*
 * Supported Debian-style multiarch tuples
 */
static const char * const multiarch_tuples[] =
{
  "x86_64-linux-gnu",
  "i386-linux-gnu"
};

/*
 * Directories other than /usr/lib that we must search for loadable
 * modules, in the same order as multiarch_tuples
 */
static const char * const libquals[] =
{
  "lib64",
  "lib32"
};

/* In Flatpak this is optional, in pressure-vessel not so much */
#define ENABLE_XAUTH

static gchar *
find_executable_dir (GError **error)
{
  g_autofree gchar *target = glnx_readlinkat_malloc (-1, "/proc/self/exe",
                                                     NULL, error);

  if (target == NULL)
    return glnx_prefix_error_null (error, "Unable to resolve /proc/self/exe");

  return g_path_get_dirname (target);
}

static void
search_path_append (GString *search_path,
                    const gchar *item)
{
  if (search_path->len != 0)
    g_string_append (search_path, ":");

  g_string_append (search_path, item);
}

static gchar *
find_bwrap (const char *tools_dir)
{
  static const char * const flatpak_libexecdirs[] =
  {
    "/usr/local/libexec",
    "/usr/libexec",
    "/usr/lib/flatpak"
  };
  const char *tmp;
  g_autofree gchar *candidate = NULL;
  g_autofree gchar *dirname = NULL;
  gsize i;

  g_return_val_if_fail (tools_dir != NULL, NULL);

  tmp = g_getenv ("BWRAP");

  if (tmp != NULL)
    return g_strdup (tmp);

  candidate = g_find_program_in_path ("bwrap");

  if (candidate != NULL)
    return g_steal_pointer (&candidate);

  for (i = 0; i < G_N_ELEMENTS (flatpak_libexecdirs); i++)
    {
      candidate = g_build_filename (flatpak_libexecdirs[i],
                                    "flatpak-bwrap", NULL);

      if (g_file_test (candidate, G_FILE_TEST_IS_EXECUTABLE))
        return g_steal_pointer (&candidate);
      else
        g_clear_pointer (&candidate, g_free);
    }

  candidate = g_build_filename (tools_dir, "bwrap", NULL);

  if (g_file_test (candidate, G_FILE_TEST_IS_EXECUTABLE))
    return g_steal_pointer (&candidate);
  else
    g_clear_pointer (&candidate, g_free);

  return NULL;
}

static gchar *
check_bwrap (const char *tools_dir)
{
  g_autoptr(GError) local_error = NULL;
  GError **error = &local_error;
  g_autofree gchar *bwrap_executable = find_bwrap (tools_dir);
  const char *bwrap_test_argv[] =
  {
    NULL,
    "--bind", "/", "/",
    "true",
    NULL
  };

  g_return_val_if_fail (tools_dir != NULL, NULL);

  if (bwrap_executable == NULL)
    {
      g_warning ("Cannot find bwrap");
    }
  else
    {
      int exit_status;
      g_autofree gchar *child_stdout = NULL;
      g_autofree gchar *child_stderr = NULL;

      bwrap_test_argv[0] = bwrap_executable;

      if (!g_spawn_sync (NULL,  /* cwd */
                         (gchar **) bwrap_test_argv,
                         NULL,  /* environ */
                         G_SPAWN_DEFAULT,
                         NULL, NULL,    /* child setup */
                         &child_stdout,
                         &child_stderr,
                         &exit_status,
                         error))
        {
          g_warning ("Cannot run bwrap: %s", local_error->message);
          g_clear_error (&local_error);
        }
      else if (exit_status != 0)
        {
          g_warning ("Cannot run bwrap: exit status %d", exit_status);

          if (child_stdout != NULL && child_stdout[0] != '\0')
            g_warning ("Output:\n%s", child_stdout);

          if (child_stderr != NULL && child_stderr[0] != '\0')
            g_warning ("Diagnostic output:\n%s", child_stderr);
        }
      else
        {
          return g_steal_pointer (&bwrap_executable);
        }
    }

  return NULL;
}

/* From Flatpak */
static char *
extract_unix_path_from_dbus_address (const char *address)
{
  const char *path, *path_end;

  if (address == NULL)
    return NULL;

  if (!g_str_has_prefix (address, "unix:"))
    return NULL;

  path = strstr (address, "path=");
  if (path == NULL)
    return NULL;
  path += strlen ("path=");
  path_end = path;
  while (*path_end != 0 && *path_end != ',')
    path_end++;

  return g_strndup (path, path_end - path);
}

/* From Flatpak */
#ifdef ENABLE_XAUTH
static gboolean
auth_streq (char *str,
            char *au_str,
            int   au_len)
{
  return au_len == strlen (str) && memcmp (str, au_str, au_len) == 0;
}

static gboolean
xauth_entry_should_propagate (Xauth *xa,
                              char  *hostname,
                              char  *number)
{
  /* ensure entry isn't for remote access */
  if (xa->family != FamilyLocal && xa->family != FamilyWild)
    return FALSE;

  /* ensure entry is for this machine */
  if (xa->family == FamilyLocal && !auth_streq (hostname, xa->address, xa->address_length))
    return FALSE;

  /* ensure entry is for this session */
  if (xa->number != NULL && !auth_streq (number, xa->number, xa->number_length))
    return FALSE;

  return TRUE;
}

static void
write_xauth (char *number, FILE *output)
{
  Xauth *xa, local_xa;
  char *filename;
  FILE *f;
  struct utsname unames;

  if (uname (&unames))
    {
      g_warning ("uname failed");
      return;
    }

  filename = XauFileName ();
  f = fopen (filename, "rb");
  if (f == NULL)
    return;

  while (TRUE)
    {
      xa = XauReadAuth (f);
      if (xa == NULL)
        break;
      if (xauth_entry_should_propagate (xa, unames.nodename, number))
        {
          local_xa = *xa;
          if (local_xa.number)
            {
              local_xa.number = (char *) "99";
              local_xa.number_length = 2;
            }

          if (!XauWriteAuth (output, &local_xa))
            g_warning ("xauth write error");
        }

      XauDisposeAuth (xa);
    }

  fclose (f);
}
#endif

/* Adapted from Flatpak */
static void
flatpak_run_add_x11_args (FlatpakBwrap *bwrap,
                          gboolean      allowed)
{
  g_autofree char *x11_socket = NULL;
  const char *display;

  /* Always cover /tmp/.X11-unix, that way we never see the host one in case
   * we have access to the host /tmp. If you request X access we'll put the right
   * thing in this anyway.
   */
  flatpak_bwrap_add_args (bwrap,
                          "--tmpfs", "/tmp/.X11-unix",
                          NULL);

  if (!allowed)
    {
      flatpak_bwrap_unset_env (bwrap, "DISPLAY");
      return;
    }

  g_debug ("Allowing x11 access");

  display = g_getenv ("DISPLAY");
  if (display && display[0] == ':' && g_ascii_isdigit (display[1]))
    {
      const char *display_nr = &display[1];
      const char *display_nr_end = display_nr;
      g_autofree char *d = NULL;

      while (g_ascii_isdigit (*display_nr_end))
        display_nr_end++;

      d = g_strndup (display_nr, display_nr_end - display_nr);
      x11_socket = g_strdup_printf ("/tmp/.X11-unix/X%s", d);

      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind", x11_socket, "/tmp/.X11-unix/X99",
                              NULL);
      flatpak_bwrap_set_env (bwrap, "DISPLAY", ":99.0", TRUE);

#ifdef ENABLE_XAUTH
      g_auto(GLnxTmpfile) xauth_tmpf  = { 0, };

      if (glnx_open_anonymous_tmpfile (O_RDWR | O_CLOEXEC, &xauth_tmpf, NULL))
        {
          FILE *output = fdopen (xauth_tmpf.fd, "wb");
          if (output != NULL)
            {
              /* fd is now owned by output, steal it from the tmpfile */
              int tmp_fd = dup (glnx_steal_fd (&xauth_tmpf.fd));
              if (tmp_fd != -1)
                {
                  g_autofree char *dest = g_strdup_printf ("/run/user/%d/Xauthority", getuid ());

                  write_xauth (d, output);
                  flatpak_bwrap_add_args_data_fd (bwrap, "--ro-bind-data", tmp_fd, dest);

                  flatpak_bwrap_set_env (bwrap, "XAUTHORITY", dest, TRUE);
                }

              fclose (output);

              if (tmp_fd != -1)
                lseek (tmp_fd, 0, SEEK_SET);
            }
        }
#endif
    }
  else
    {
      flatpak_bwrap_unset_env (bwrap, "DISPLAY");
    }
}

static gboolean
flatpak_run_add_wayland_args (FlatpakBwrap *bwrap)
{
  const char *wayland_display;
  g_autofree char *user_runtime_dir = flatpak_get_real_xdg_runtime_dir ();
  g_autofree char *wayland_socket = NULL;
  g_autofree char *sandbox_wayland_socket = NULL;
  gboolean res = FALSE;
  struct stat statbuf;

  wayland_display = g_getenv ("WAYLAND_DISPLAY");
  if (!wayland_display)
    wayland_display = "wayland-0";

  wayland_socket = g_build_filename (user_runtime_dir, wayland_display, NULL);
  sandbox_wayland_socket = g_strdup_printf ("/run/user/%d/%s", getuid (), wayland_display);

  if (stat (wayland_socket, &statbuf) == 0 &&
      (statbuf.st_mode & S_IFMT) == S_IFSOCK)
    {
      res = TRUE;
      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind", wayland_socket, sandbox_wayland_socket,
                              NULL);
    }
  return res;
}

/* Try to find a default server from a pulseaudio confguration file */
static char *
flatpak_run_get_pulseaudio_server_user_config (const char *path)
{
  g_autoptr(GFile) file = g_file_new_for_path (path);
  g_autoptr(GError) my_error = NULL;
  g_autoptr(GFileInputStream) input_stream = NULL;
  g_autoptr(GDataInputStream) data_stream = NULL;
  size_t len;

  input_stream = g_file_read (file, NULL, &my_error);
  if (my_error)
    {
      g_debug ("Pulseaudio user configuration file '%s': %s", path, my_error->message);
      return NULL;
    }

  data_stream = g_data_input_stream_new (G_INPUT_STREAM (input_stream));

  while (TRUE)
    {
      g_autofree char *line = g_data_input_stream_read_line (data_stream, &len, NULL, NULL);
      if (line == NULL)
        break;

      g_strchug (line);

      if ((*line  == '\0') || (*line == ';') || (*line == '#'))
        continue;

      if (g_str_has_prefix (line, ".include "))
        {
          g_autofree char *rec_path = g_strdup (line + 9);
          g_strstrip (rec_path);
          char *found = flatpak_run_get_pulseaudio_server_user_config (rec_path);
          if (found)
            return found;
        }
      else if (g_str_has_prefix (line, "["))
        {
          return NULL;
        }
      else
        {
          g_auto(GStrv) tokens = g_strsplit (line, "=", 2);

          if ((tokens[0] != NULL) && (tokens[1] != NULL))
            {
              g_strchomp (tokens[0]);
              if (strcmp ("default-server", tokens[0]) == 0)
                {
                  g_strstrip (tokens[1]);
                  g_debug ("Found pulseaudio socket from configuration file '%s': %s", path, tokens[1]);
                  return g_strdup (tokens[1]);
                }
            }
        }
    }

  return NULL;
}

static char *
flatpak_run_get_pulseaudio_server (void)
{
  const char * pulse_clientconfig;
  char *pulse_server;
  g_autofree char *pulse_user_config = NULL;

  pulse_server = g_strdup (g_getenv ("PULSE_SERVER"));
  if (pulse_server)
    return pulse_server;

  pulse_clientconfig = g_getenv ("PULSE_CLIENTCONFIG");
  if (pulse_clientconfig)
    return flatpak_run_get_pulseaudio_server_user_config (pulse_clientconfig);

  pulse_user_config = g_build_filename (g_get_user_config_dir (), "pulse/client.conf", NULL);
  pulse_server = flatpak_run_get_pulseaudio_server_user_config (pulse_user_config);
  if (pulse_server)
    return pulse_server;

  pulse_server = flatpak_run_get_pulseaudio_server_user_config ("/etc/pulse/client.conf");
  if (pulse_server)
    return pulse_server;

  return NULL;
}

static char *
flatpak_run_parse_pulse_server (const char *value)
{
  g_auto(GStrv) servers = g_strsplit (value, " ", 0);
  gsize i;

  for (i = 0; servers[i] != NULL; i++)
    {
      const char *server = servers[i];
      if (g_str_has_prefix (server, "{"))
        {
          const char * closing = strstr (server, "}");
          if (closing == NULL)
            continue;
          server = closing + 1;
        }
      if (g_str_has_prefix (server, "unix:"))
        return g_strdup (server + 5);
    }

  return NULL;
}

static void
flatpak_run_add_pulseaudio_args (FlatpakBwrap *bwrap)
{
  g_autofree char *pulseaudio_server = flatpak_run_get_pulseaudio_server ();
  g_autofree char *pulseaudio_socket = NULL;
  g_autofree char *user_runtime_dir = flatpak_get_real_xdg_runtime_dir ();

  if (pulseaudio_server)
    pulseaudio_socket = flatpak_run_parse_pulse_server (pulseaudio_server);

  if (!pulseaudio_socket)
    pulseaudio_socket = g_build_filename (user_runtime_dir, "pulse/native", NULL);

  flatpak_bwrap_unset_env (bwrap, "PULSE_SERVER");

  /* SteamOS system-wide PulseAudio instance */
  if (!g_file_test (pulseaudio_socket, G_FILE_TEST_EXISTS))
    {
      g_clear_pointer (&pulseaudio_socket, g_free);
      pulseaudio_socket = g_strdup ("/var/run/pulse/native");
    }

  if (g_file_test (pulseaudio_socket, G_FILE_TEST_EXISTS))
    {
      gboolean share_shm = FALSE; /* TODO: When do we add this? */
      g_autofree char *client_config = g_strdup_printf ("enable-shm=%s\n", share_shm ? "yes" : "no");
      g_autofree char *sandbox_socket_path = g_strdup_printf ("/run/user/%d/pulse/native", getuid ());
      g_autofree char *pulse_server = g_strdup_printf ("unix:/run/user/%d/pulse/native", getuid ());
      g_autofree char *config_path = g_strdup_printf ("/run/user/%d/pulse/config", getuid ());

      /* FIXME - error handling */
      if (!flatpak_bwrap_add_args_data (bwrap, "pulseaudio", client_config, -1, config_path, NULL))
        return;

      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind", pulseaudio_socket, sandbox_socket_path,
                              NULL);

      flatpak_bwrap_set_env (bwrap, "PULSE_SERVER", pulse_server, TRUE);
      flatpak_bwrap_set_env (bwrap, "PULSE_CLIENTCONFIG", config_path, TRUE);
    }
  else
    g_debug ("Could not find pulseaudio socket");
}

/* Simplified from Flatpak: we never restrict access to the D-Bus system bus */
static gboolean
flatpak_run_add_system_dbus_args (FlatpakBwrap   *app_bwrap)
{
  const char *dbus_address = g_getenv ("DBUS_SYSTEM_BUS_ADDRESS");
  g_autofree char *real_dbus_address = NULL;
  g_autofree char *dbus_system_socket = NULL;

  if (dbus_address != NULL)
    dbus_system_socket = extract_unix_path_from_dbus_address (dbus_address);
  else if (g_file_test ("/var/run/dbus/system_bus_socket", G_FILE_TEST_EXISTS))
    dbus_system_socket = g_strdup ("/var/run/dbus/system_bus_socket");

  if (dbus_system_socket != NULL)
    {
      flatpak_bwrap_add_args (app_bwrap,
                              "--ro-bind", dbus_system_socket, "/run/dbus/system_bus_socket",
                              NULL);
      flatpak_bwrap_set_env (app_bwrap, "DBUS_SYSTEM_BUS_ADDRESS", "unix:path=/run/dbus/system_bus_socket", TRUE);

      return TRUE;
    }

  return FALSE;
}

/* Simplified from Flatpak: we never restrict access to the D-Bus session bus */
static gboolean
flatpak_run_add_session_dbus_args (FlatpakBwrap   *app_bwrap)
{
  const char *dbus_address = g_getenv ("DBUS_SESSION_BUS_ADDRESS");
  char *dbus_session_socket = NULL;
  g_autofree char *sandbox_socket_path = g_strdup_printf ("/run/user/%d/bus", getuid ());
  g_autofree char *sandbox_dbus_address = g_strdup_printf ("unix:path=/run/user/%d/bus", getuid ());
  g_autofree char *user_runtime_dir = flatpak_get_real_xdg_runtime_dir ();

  /* FIXME: upstream the use of user_runtime_dir to Flatpak */

  if (dbus_address != NULL)
    {
      dbus_session_socket = extract_unix_path_from_dbus_address (dbus_address);
    }
  else
    {
      struct stat statbuf;

      dbus_session_socket = g_build_filename (user_runtime_dir, "bus", NULL);

      if (stat (dbus_session_socket, &statbuf) < 0
          || (statbuf.st_mode & S_IFMT) != S_IFSOCK
          || statbuf.st_uid != getuid ())
        return FALSE;
    }

  if (dbus_session_socket != NULL)
    {
      flatpak_bwrap_add_args (app_bwrap,
                              "--ro-bind", dbus_session_socket, sandbox_socket_path,
                              NULL);
      flatpak_bwrap_set_env (app_bwrap, "DBUS_SESSION_BUS_ADDRESS", sandbox_dbus_address, TRUE);

      return TRUE;
    }

  return FALSE;
}

static gchar *
capture_output (const char * const * argv,
                GError **error)
{
  gsize len;
  gint exit_status;
  g_autofree gchar *output = NULL;
  g_autofree gchar *errors = NULL;
  gsize i;
  g_autoptr(GString) command = g_string_new ("");

  for (i = 0; argv[i] != NULL; i++)
    {
      g_autofree gchar *quoted = g_shell_quote (argv[i]);

      g_string_append_printf (command, " %s", quoted);
    }

  g_debug ("run:%s", command->str);

  if (!g_spawn_sync (NULL,  /* cwd */
                     (char **) argv,
                     NULL,  /* env */
                     G_SPAWN_SEARCH_PATH,
                     NULL, NULL,    /* child setup */
                     &output,
                     &errors,
                     &exit_status,
                     error))
    return NULL;

  g_printerr ("%s", errors);

  if (!g_spawn_check_exit_status (exit_status, error))
    return NULL;

  len = strlen (output);

  /* Emulate shell $() */
  if (len > 0 && output[len - 1] == '\n')
    output[len - 1] = '\0';

  g_debug ("-> %s", output);

  return g_steal_pointer (&output);
}

static gboolean
try_bind_dri (FlatpakBwrap *bwrap,
              FlatpakBwrap *mount_runtime_on_scratch,
              const char *overrides,
              const char *scratch,
              const char *tool_path,
              const char *libdir,
              const char *libdir_on_host,
              GError **error)
{
  g_autofree gchar *dri = g_build_filename (libdir, "dri", NULL);
  g_autofree gchar *s2tc = g_build_filename (libdir, "libtxc_dxtn.so", NULL);

  if (g_file_test (dri, G_FILE_TEST_IS_DIR))
    {
      g_autoptr(FlatpakBwrap) temp_bwrap = NULL;
      g_autofree gchar *expr = NULL;
      g_autofree gchar *host_dri = NULL;
      g_autofree gchar *dest_dri = NULL;

      expr = g_strdup_printf ("only-dependencies:if-exists:path-match:%s/dri/*.so",
                              libdir);

      temp_bwrap = flatpak_bwrap_new (NULL);
      flatpak_bwrap_append_bwrap (temp_bwrap, mount_runtime_on_scratch);
      flatpak_bwrap_add_args (temp_bwrap,
                              tool_path,
                              "--container", scratch,
                              "--link-target", "/run/host",
                              "--dest", libdir_on_host,
                              "--provider", "/",
                              expr,
                              NULL);
      flatpak_bwrap_finish (temp_bwrap);

      if (!pv_bwrap_run_sync (temp_bwrap, error))
        return FALSE;

      g_clear_pointer (&temp_bwrap, flatpak_bwrap_free);

      host_dri = g_build_filename ("/run/host", libdir, "dri", NULL);
      dest_dri = g_build_filename (libdir_on_host, "dri", NULL);
      temp_bwrap = flatpak_bwrap_new (NULL);
      flatpak_bwrap_add_args (temp_bwrap,
                              bwrap->argv->pdata[0],
                              "--ro-bind", "/", "/",
                              "--tmpfs", "/run",
                              "--ro-bind", "/", "/run/host",
                              "--bind", overrides, overrides,
                              "sh", "-c",
                              "ln -fns \"$1\"/* \"$2\"",
                              "sh",   /* $0 */
                              host_dri,
                              dest_dri,
                              NULL);
      flatpak_bwrap_finish (temp_bwrap);

      if (!pv_bwrap_run_sync (temp_bwrap, error))
        return FALSE;
    }

  if (g_file_test (s2tc, G_FILE_TEST_EXISTS))
    {
      g_autoptr(FlatpakBwrap) temp_bwrap = NULL;
      g_autofree gchar *expr = NULL;

      expr = g_strdup_printf ("path-match:%s", s2tc);
      temp_bwrap = flatpak_bwrap_new (NULL);
      flatpak_bwrap_append_bwrap (temp_bwrap, mount_runtime_on_scratch);
      flatpak_bwrap_add_args (temp_bwrap,
                              tool_path,
                              "--container", scratch,
                              "--link-target", "/run/host",
                              "--dest", libdir_on_host,
                              "--provider", "/",
                              expr,
                              NULL);
      flatpak_bwrap_finish (temp_bwrap);

      if (!pv_bwrap_run_sync (temp_bwrap, error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
bind_runtime (FlatpakBwrap *bwrap,
              const char *tools_dir,
              const char *runtime,
              const char *overrides,
              const char *scratch,
              GError **error)
{
  static const char * const bind_mutable[] =
  {
    "etc",
    "var/cache",
    "var/lib"
  };
  static const char * const dont_bind[] =
  {
    "/etc/group",
    "/etc/passwd",
    "/etc/host.conf",
    "/etc/hosts",
    "/etc/localtime",
    "/etc/machine-id",
    "/etc/resolv.conf",
    "/var/lib/dbus",
    "/var/lib/dhcp",
    "/var/lib/sudo",
    "/var/lib/urandom",
    NULL
  };
  g_autofree gchar *xrd = g_strdup_printf ("/run/user/%ld", (long) geteuid ());
  g_autofree gchar *usr = NULL;
  gsize i, j;
  const gchar *member;
  g_autoptr(GString) dri_path = g_string_new ("");
  gboolean any_architecture_works = FALSE;

  g_return_val_if_fail (tools_dir != NULL, FALSE);
  g_return_val_if_fail (runtime != NULL, FALSE);
  g_return_val_if_fail (overrides != NULL, FALSE);
  g_return_val_if_fail (scratch != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  usr = g_build_filename (runtime, "usr", NULL);

  if (!pv_bwrap_bind_usr (bwrap, runtime, "/", error))
    return FALSE;

  flatpak_bwrap_add_args (bwrap,
                          "--setenv", "XDG_RUNTIME_DIR", xrd,
                          "--proc", "/proc",
                          "--ro-bind", "/sys", "/sys",
                          "--tmpfs", "/run",
                          "--tmpfs", "/tmp",
                          "--tmpfs", "/var",
                          "--symlink", "../run", "/var/run",
                          NULL);

  for (i = 0; i < G_N_ELEMENTS (bind_mutable); i++)
    {
      g_autofree gchar *path = g_build_filename (runtime, bind_mutable[i],
                                                 NULL);
      g_autoptr(GDir) dir = NULL;

      dir = g_dir_open (path, 0, NULL);

      if (dir == NULL)
        continue;

      for (member = g_dir_read_name (dir);
           member != NULL;
           member = g_dir_read_name (dir))
        {
          g_autofree gchar *dest = g_build_filename ("/", bind_mutable[i],
                                                     member, NULL);
          g_autofree gchar *full = NULL;
          g_autofree gchar *target = NULL;

          if (g_strv_contains (dont_bind, dest))
            continue;

          full = g_build_filename (runtime, bind_mutable[i], member, NULL);
          target = glnx_readlinkat_malloc (-1, full, NULL, NULL);

          if (target != NULL)
            flatpak_bwrap_add_args (bwrap, "--symlink", target, dest, NULL);
          else
            flatpak_bwrap_add_args (bwrap, "--ro-bind", full, dest, NULL);
        }
    }

  if (g_file_test ("/etc/machine-id", G_FILE_TEST_EXISTS))
    {
      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind", "/etc/machine-id", "/etc/machine-id",
                              "--symlink", "/etc/machine-id",
                              "/var/lib/dbus/machine-id",
                              NULL);
    }
  else if (g_file_test ("/var/lib/dbus/machine-id", G_FILE_TEST_EXISTS))
    {
      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind", "/var/lib/dbus/machine-id",
                              "/etc/machine-id",
                              "--symlink", "/etc/machine-id",
                              "/var/lib/dbus/machine-id",
                              NULL);
    }

  /* /etc/localtime and /etc/resolv.conf can not exist (or be symlinks to
   * non-existing targets), in which case we don't want to attempt to create
   * bogus symlinks or bind mounts, as that will cause flatpak run to fail.
   */
  if (g_file_test ("/etc/localtime", G_FILE_TEST_EXISTS))
    {
      g_autofree char *localtime = NULL;
      gboolean is_reachable = FALSE;
      g_autofree char *timezone = flatpak_get_timezone ();
      g_autofree char *timezone_content = g_strdup_printf ("%s\n", timezone);

      localtime = glnx_readlinkat_malloc (-1, "/etc/localtime", NULL, NULL);

      if (localtime != NULL)
        {
          g_autoptr(GFile) base_file = NULL;
          g_autoptr(GFile) target_file = NULL;
          g_autofree char *target_canonical = NULL;

          base_file = g_file_new_for_path ("/etc");
          target_file = g_file_resolve_relative_path (base_file, localtime);
          target_canonical = g_file_get_path (target_file);

          is_reachable = g_str_has_prefix (target_canonical, "/usr/");
        }

      if (is_reachable)
        {
          flatpak_bwrap_add_args (bwrap,
                                  "--symlink", localtime, "/etc/localtime",
                                  NULL);
        }
      else
        {
          flatpak_bwrap_add_args (bwrap,
                                  "--ro-bind", "/etc/localtime", "/etc/localtime",
                                  NULL);
        }

      flatpak_bwrap_add_args_data (bwrap, "timezone",
                                   timezone_content, -1, "/etc/timezone",
                                   NULL);
    }

  if (g_file_test ("/etc/resolv.conf", G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--ro-bind", "/etc/resolv.conf", "/etc/resolv.conf",
                            NULL);
  if (g_file_test ("/etc/host.conf", G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--ro-bind", "/etc/host.conf", "/etc/host.conf",
                            NULL);
  if (g_file_test ("/etc/hosts", G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--ro-bind", "/etc/hosts", "/etc/hosts",
                            NULL);

  /* TODO: Synthesize a passwd with only the user and nobody,
   * like Flatpak does? */
  if (g_file_test ("/etc/passwd", G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--ro-bind", "/etc/passwd", "/etc/passwd",
                            NULL);
  if (g_file_test ("/etc/group", G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--ro-bind", "/etc/group", "/etc/group",
                            NULL);

  flatpak_run_add_wayland_args (bwrap);
  flatpak_run_add_x11_args (bwrap, TRUE);
  flatpak_run_add_pulseaudio_args (bwrap);
  flatpak_run_add_session_dbus_args (bwrap);
  flatpak_run_add_system_dbus_args (bwrap);

  for (i = 0; i < G_N_ELEMENTS (multiarch_tuples); i++)
    {
      g_autofree gchar *tool = g_strdup_printf ("%s-capsule-capture-libs",
                                                multiarch_tuples[i]);
      g_autofree gchar *tool_path = NULL;
      g_autofree gchar *ld_so = NULL;
      const gchar *argv[] = { NULL, "--print-ld.so", NULL };

      g_debug ("Checking for %s libraries...", multiarch_tuples[i]);

      tool_path = g_build_filename (tools_dir, tool, NULL);
      argv[0] = tool_path;

      /* This has the side-effect of testing whether we can run binaries
       * for this architecture on the host system. */
      ld_so = capture_output (argv, NULL);

      if (ld_so != NULL)
        {
          g_auto(GStrv) dirs = NULL;
          g_autoptr(FlatpakBwrap) mount_runtime_on_scratch = NULL;
          g_autoptr(FlatpakBwrap) temp_bwrap = NULL;
          g_autofree gchar *libdir_in_container = g_build_filename ("/overrides",
                                                                    "lib",
                                                                    multiarch_tuples[i],
                                                                    NULL);
          g_autofree gchar *libdir_on_host = g_build_filename (overrides, "lib",
                                                               multiarch_tuples[i],
                                                               NULL);
          g_autofree gchar *this_dri_path_on_host = g_build_filename (libdir_on_host,
                                                                      "dri", NULL);
          g_autofree gchar *this_dri_path_in_container = g_build_filename (libdir_in_container,
                                                                           "dri", NULL);
          g_autofree gchar *libc = NULL;
          g_autofree gchar *ld_so_in_runtime = NULL;
          const gchar *libqual = NULL;

          temp_bwrap = flatpak_bwrap_new (NULL);
          flatpak_bwrap_add_args (temp_bwrap,
                                  bwrap->argv->pdata[0],
                                  NULL);

          if (!pv_bwrap_bind_usr (temp_bwrap, runtime, "/", error))
            return FALSE;

          flatpak_bwrap_add_args (temp_bwrap,
                                  "readlink", "-e", ld_so,
                                  NULL);
          flatpak_bwrap_finish (temp_bwrap);

          ld_so_in_runtime = capture_output ((const char * const *) temp_bwrap->argv->pdata,
                                             NULL);

          g_clear_pointer (&temp_bwrap, flatpak_bwrap_free);

          if (ld_so_in_runtime == NULL)
            {
              g_debug ("Container does not have %s so it cannot run "
                       "%s binaries", ld_so, multiarch_tuples[i]);
              continue;
            }

          any_architecture_works = TRUE;
          g_debug ("Container path: %s -> %s", ld_so, ld_so_in_runtime);

          search_path_append (dri_path, this_dri_path_in_container);

          g_mkdir_with_parents (libdir_on_host, 0755);
          g_mkdir_with_parents (this_dri_path_on_host, 0755);

          mount_runtime_on_scratch = flatpak_bwrap_new (NULL);
          flatpak_bwrap_add_args (mount_runtime_on_scratch,
                                  bwrap->argv->pdata[0],
                                  "--ro-bind", "/", "/",
                                  "--bind", overrides, overrides,
                                  "--tmpfs", scratch,
                                  NULL);
          if (!pv_bwrap_bind_usr (mount_runtime_on_scratch, runtime, scratch,
                         error))
            return FALSE;

          temp_bwrap = flatpak_bwrap_new (NULL);
          flatpak_bwrap_append_bwrap (temp_bwrap, mount_runtime_on_scratch);
          flatpak_bwrap_add_args (temp_bwrap,
                                  tool_path,
                                  "--container", scratch,
                                  "--link-target", "/run/host",
                                  "--dest", libdir_on_host,
                                  "--provider", "/",
                                  "gl:",
                                  NULL);
          flatpak_bwrap_finish (temp_bwrap);

          if (!pv_bwrap_run_sync (temp_bwrap, error))
            return FALSE;

          g_clear_pointer (&temp_bwrap, flatpak_bwrap_free);

          libc = g_build_filename (libdir_on_host, "libc.so.6", NULL);

          /* If we are going to use the host system's libc6 (likely)
           * then we have to use its ld.so too. */
          if (g_file_test (libc, G_FILE_TEST_IS_SYMLINK))
            {
              g_autofree gchar *ld_so_in_host = NULL;

              g_debug ("Making host ld.so visible in container");

              ld_so_in_host = flatpak_canonicalize_filename (ld_so);
              g_debug ("Host path: %s -> %s", ld_so, ld_so_in_host);

              g_clear_pointer (&temp_bwrap, flatpak_bwrap_free);

              temp_bwrap = flatpak_bwrap_new (NULL);
              flatpak_bwrap_add_args (temp_bwrap,
                                      bwrap->argv->pdata[0],
                                      NULL);

              if (!pv_bwrap_bind_usr (temp_bwrap, runtime, "/", error))
                return FALSE;

              flatpak_bwrap_add_args (temp_bwrap,
                                      "readlink", "-f", ld_so,
                                      NULL);
              flatpak_bwrap_finish (temp_bwrap);

              g_debug ("Container path: %s -> %s", ld_so, ld_so_in_runtime);
              flatpak_bwrap_add_args (bwrap,
                                      "--ro-bind", ld_so_in_host,
                                      ld_so_in_runtime,
                                      NULL);

              g_debug ("Making host locale data visible in container");

              if (g_file_test ("/usr/lib/locale", G_FILE_TEST_EXISTS))
                flatpak_bwrap_add_args (bwrap,
                                        "--ro-bind", "/usr/lib/locale",
                                        "/usr/lib/locale",
                                        NULL);

              if (g_file_test ("/usr/share/i18n", G_FILE_TEST_EXISTS))
                flatpak_bwrap_add_args (bwrap,
                                        "--ro-bind", "/usr/share/i18n",
                                        "/usr/share/i18n",
                                        NULL);
            }

          /* /lib32 or /lib64 */
          libqual = libquals[i];

          dirs = g_new0 (gchar *, 7);
          dirs[0] = g_build_filename ("/lib", multiarch_tuples[i], NULL);
          dirs[1] = g_build_filename ("/usr", "lib", multiarch_tuples[i], NULL);
          dirs[2] = g_strdup ("/lib");
          dirs[3] = g_strdup ("/usr/lib");
          dirs[4] = g_build_filename ("/", libqual, NULL);
          dirs[5] = g_build_filename ("/usr", libqual, NULL);

          for (j = 0; j < 6; j++)
            {
              if (!try_bind_dri (bwrap, mount_runtime_on_scratch,
                                 overrides, scratch, tool_path, dirs[j],
                                 libdir_on_host, error))
                return FALSE;
            }
        }
      else
        {
          g_debug ("Cannot determine ld.so for %s", multiarch_tuples[i]);
        }
    }

  if (!any_architecture_works)
    {
      GString *archs = g_string_new ("");

      for (i = 0; i < G_N_ELEMENTS (multiarch_tuples); i++)
        {
          if (archs->len > 0)
            g_string_append (archs, ", ");

          g_string_append (archs, multiarch_tuples[i]);
        }

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "None of the supported CPU architectures are common to "
                   "the host system and the container (tried: %s)",
                   archs->str);
      g_string_free (archs, TRUE);
      return FALSE;
    }

  if (dri_path->len != 0)
      flatpak_bwrap_add_args (bwrap,
                              "--setenv", "LIBGL_DRIVERS_PATH", dri_path->str,
                              NULL);
  else
      flatpak_bwrap_add_args (bwrap,
                              "--unsetenv", "LIBGL_DRIVERS_PATH",
                              NULL);

  pv_bwrap_copy_tree (bwrap, overrides, "/overrides");

  if (!pv_bwrap_bind_usr (bwrap, "/", "/run/host", error))
    return FALSE;

  return TRUE;
}

/* Order matters here: root, steam and steambeta are or might be symlinks
 * to the root of the Steam installation, so we want to bind-mount their
 * targets before we deal with the rest. */
static const char * const steam_api_subdirs[] =
{
  "root", "steam", "steambeta", "bin", "bin32", "bin64", "sdk32", "sdk64",
};

static gboolean
use_fake_home (FlatpakBwrap *bwrap,
               const gchar *fake_home,
               GError **error)
{
  const gchar *real_home = g_get_home_dir ();
  g_autofree gchar *cache = g_build_filename (fake_home, ".cache", NULL);
  g_autofree gchar *cache2 = g_build_filename (fake_home, "cache", NULL);
  g_autofree gchar *tmp = g_build_filename (cache, "tmp", NULL);
  g_autofree gchar *config = g_build_filename (fake_home, ".config", NULL);
  g_autofree gchar *config2 = g_build_filename (fake_home, "config", NULL);
  g_autofree gchar *local = g_build_filename (fake_home, ".local", NULL);
  g_autofree gchar *data = g_build_filename (local, "share", NULL);
  g_autofree gchar *data2 = g_build_filename (fake_home, "data", NULL);
  g_autofree gchar *steam_pid = NULL;
  g_autofree gchar *steam_pipe = NULL;
  g_autoptr(GHashTable) mounted = NULL;
  gsize i;

  g_return_val_if_fail (bwrap != NULL, FALSE);
  g_return_val_if_fail (fake_home != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_mkdir_with_parents (fake_home, 0700);
  g_mkdir_with_parents (cache, 0700);
  g_mkdir_with_parents (tmp, 0700);
  g_mkdir_with_parents (config, 0700);
  g_mkdir_with_parents (local, 0700);
  g_mkdir_with_parents (data, 0700);

  if (!g_file_test (cache2, G_FILE_TEST_EXISTS))
    {
      g_unlink (cache2);

      if (symlink (".cache", cache2) != 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to create symlink %s -> .cache",
                                        cache2);
    }

  if (!g_file_test (config2, G_FILE_TEST_EXISTS))
    {
      g_unlink (config2);

      if (symlink (".config", config2) != 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to create symlink %s -> .config",
                                        config2);
    }

  if (!g_file_test (data2, G_FILE_TEST_EXISTS))
    {
      g_unlink (data2);

      if (symlink (".local/share", data2) != 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to create symlink %s -> .local/share",
                                        data2);
    }

  flatpak_bwrap_add_args (bwrap,
                          "--bind", fake_home, real_home,
                          "--bind", fake_home, fake_home,
                          "--bind", tmp, "/var/tmp",
                          "--setenv", "XDG_CACHE_HOME", cache,
                          "--setenv", "XDG_CONFIG_HOME", config,
                          "--setenv", "XDG_DATA_HOME", data,
                          NULL);

  mounted = g_hash_table_new_full (g_str_hash, g_str_equal,
                                   g_free, NULL);

  /*
   * These might be API entry points, according to Steam/steam.sh.
   * They're usually symlinks into the Steam root, except for in
   * older steam Debian packages that had Debian bug #916303.
   *
   * TODO: We probably want to hide part or all of root, steam,
   * steambeta?
   */
  for (i = 0; i < G_N_ELEMENTS (steam_api_subdirs); i++)
    {
      g_autofree gchar *dir = g_build_filename (real_home, ".steam",
                                                steam_api_subdirs[i], NULL);
      g_autofree gchar *mount_point = g_build_filename (fake_home, ".steam",
                                                        steam_api_subdirs[i],
                                                        NULL);
      g_autofree gchar *target = NULL;

      target = glnx_readlinkat_malloc (-1, dir, NULL, NULL);

      if (target != NULL)
        {
          /* We used to bind-mount these directories, so transition them
           * to symbolic links if we can. */
          if (rmdir (mount_point) != 0 && errno != ENOENT && errno != ENOTDIR)
            g_debug ("rmdir %s: %s", mount_point, g_strerror (errno));

          /* Remove any symlinks that might have already been there. */
          if (unlink (mount_point) != 0 && errno != ENOENT)
            g_debug ("unlink %s: %s", mount_point, g_strerror (errno));

          flatpak_bwrap_add_args (bwrap, "--symlink", target, dir, NULL);

          if (strcmp (steam_api_subdirs[i], "root") == 0
              || strcmp (steam_api_subdirs[i], "steam") == 0
              || strcmp (steam_api_subdirs[i], "steambeta") == 0)
            {
              flatpak_bwrap_add_args (bwrap,
                                      "--ro-bind", target, target,
                                      NULL);
              g_hash_table_add (mounted, g_steal_pointer (&target));
            }
        }
      else if (!g_hash_table_contains (mounted, dir))
        {
          flatpak_bwrap_add_args (bwrap, "--ro-bind", dir, dir, NULL);
          g_hash_table_add (mounted, g_steal_pointer (&dir));
        }
    }

  /* steamclient.so relies on this for communication with Steam */
  steam_pid = g_build_filename (real_home, ".steam", "steam.pid", NULL);

  if (g_file_test (steam_pid, G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--ro-bind", steam_pid, steam_pid,
                            NULL);

  /* Make sure Steam IPC is available.
   * TODO: do we need this? do we need more? */
  steam_pipe = g_build_filename (real_home, ".steam", "steam.pipe", NULL);

  if (g_file_test (steam_pipe, G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--bind", steam_pipe, steam_pipe,
                            NULL);

  return TRUE;
}

typedef enum
{
  TRISTATE_NO = 0,
  TRISTATE_YES,
  TRISTATE_MAYBE
} Tristate;

static char **opt_env_if_host = NULL;
static char *opt_fake_home = NULL;
static char *opt_freedesktop_app_id = NULL;
static char *opt_steam_app_id = NULL;
static char *opt_home = NULL;
static gboolean opt_host_fallback = FALSE;
static PvShell opt_shell = PV_SHELL_NONE;
static GPtrArray *opt_ld_preload = NULL;
static char *opt_runtime_base = NULL;
static char *opt_runtime = NULL;
static Tristate opt_share_home = TRISTATE_MAYBE;
static gboolean opt_verbose = FALSE;
static gboolean opt_version = FALSE;
static PvTerminal opt_terminal = PV_TERMINAL_AUTO;

static gboolean
opt_host_ld_preload_cb (const gchar *option_name,
                        const gchar *value,
                        gpointer data,
                        GError **error)
{
  gchar *preload = g_strdup_printf ("host:%s", value);

  if (opt_ld_preload == NULL)
    opt_ld_preload = g_ptr_array_new_with_free_func (g_free);

  g_ptr_array_add (opt_ld_preload, g_steal_pointer (&preload));

  return TRUE;
}

static gboolean
opt_shell_cb (const gchar *option_name,
              const gchar *value,
              gpointer data,
              GError **error)
{
  if (g_strcmp0 (option_name, "--shell-after") == 0)
    value = "after";
  else if (g_strcmp0 (option_name, "--shell-fail") == 0)
    value = "fail";
  else if (g_strcmp0 (option_name, "--shell-instead") == 0)
    value = "instead";

  if (value == NULL || *value == '\0')
    {
      opt_shell = PV_SHELL_NONE;
      return TRUE;
    }

  switch (value[0])
    {
      case 'a':
        if (g_strcmp0 (value, "after") == 0)
          {
            opt_shell = PV_SHELL_AFTER;
            return TRUE;
          }
        break;

      case 'f':
        if (g_strcmp0 (value, "fail") == 0)
          {
            opt_shell = PV_SHELL_FAIL;
            return TRUE;
          }
        break;

      case 'i':
        if (g_strcmp0 (value, "instead") == 0)
          {
            opt_shell = PV_SHELL_INSTEAD;
            return TRUE;
          }
        break;

      case 'n':
        if (g_strcmp0 (value, "none") == 0 || g_strcmp0 (value, "no") == 0)
          {
            opt_shell = PV_SHELL_NONE;
            return TRUE;
          }
        break;

      default:
        /* fall through to error */
        break;
    }

  g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
               "Unknown choice \"%s\" for %s", value, option_name);
  return FALSE;
}

static gboolean
opt_terminal_cb (const gchar *option_name,
                 const gchar *value,
                 gpointer data,
                 GError **error)
{
  if (g_strcmp0 (option_name, "--tty") == 0)
    value = "tty";
  else if (g_strcmp0 (option_name, "--xterm") == 0)
    value = "xterm";

  if (value == NULL || *value == '\0')
    {
      opt_terminal = PV_TERMINAL_AUTO;
      return TRUE;
    }

  switch (value[0])
    {
      case 'a':
        if (g_strcmp0 (value, "auto") == 0)
          {
            opt_terminal = PV_TERMINAL_AUTO;
            return TRUE;
          }
        break;

      case 'n':
        if (g_strcmp0 (value, "none") == 0 || g_strcmp0 (value, "no") == 0)
          {
            opt_terminal = PV_TERMINAL_NONE;
            return TRUE;
          }
        break;

      case 't':
        if (g_strcmp0 (value, "tty") == 0)
          {
            opt_terminal = PV_TERMINAL_TTY;
            return TRUE;
          }
        break;

      case 'x':
        if (g_strcmp0 (value, "xterm") == 0)
          {
            opt_terminal = PV_TERMINAL_XTERM;
            return TRUE;
          }
        break;

      default:
        /* fall through to error */
        break;
    }

  g_set_error (error, G_OPTION_ERROR, G_OPTION_ERROR_FAILED,
               "Unknown choice \"%s\" for %s", value, option_name);
  return FALSE;
}

static gboolean
opt_share_home_cb (const gchar *option_name,
                   const gchar *value,
                   gpointer data,
                   GError **error)
{
  if (g_strcmp0 (option_name, "--share-home") == 0)
    opt_share_home = TRISTATE_YES;
  else if (g_strcmp0 (option_name, "--unshare-home") == 0)
    opt_share_home = TRISTATE_NO;
  else
    g_return_val_if_reached (FALSE);

  return TRUE;
}

static GOptionEntry options[] =
{
  { "env-if-host", 0, 0, G_OPTION_ARG_STRING_ARRAY, &opt_env_if_host,
    "Set VAR=VAL if COMMAND is run with /usr from the host system, "
    "but not if it is run with /usr from RUNTIME.", "VAR=VAL" },
  { "freedesktop-app-id", 0, 0, G_OPTION_ARG_STRING, &opt_freedesktop_app_id,
    "Make --unshare-home use ~/.var/app/ID as home directory, where ID "
    "is com.example.MyApp or similar. This interoperates with Flatpak. "
    "[Default: $PRESSURE_VESSEL_FDO_APP_ID if set]",
    "ID" },
  { "steam-app-id", 0, 0, G_OPTION_ARG_STRING, &opt_steam_app_id,
    "Make --unshare-home use ~/.var/app/com.steampowered.AppN "
    "as home directory. [Default: $SteamAppId]", "N" },
  { "home", 0, 0, G_OPTION_ARG_FILENAME, &opt_home,
    "Use HOME as home directory. Implies --unshare-home. "
    "[Default: $PRESSURE_VESSEL_HOME if set]", "HOME" },
  { "host-fallback", 0, 0, G_OPTION_ARG_NONE, &opt_host_fallback,
    "Run COMMAND on the host system if we cannot run it in a container.", NULL },
  { "host-ld-preload", 0, 0, G_OPTION_ARG_CALLBACK, &opt_host_ld_preload_cb,
    "Add MODULE from the host system to LD_PRELOAD when executing COMMAND.",
    "MODULE" },
  { "runtime", 0, 0, G_OPTION_ARG_FILENAME, &opt_runtime,
    "Mount the given sysroot or merged /usr in the container, and augment "
    "it with the host system's graphics stack. The empty string "
    "means don't use a runtime. [Default: $PRESSURE_VESSEL_RUNTIME or '']",
    "RUNTIME" },
  { "runtime-base", 0, 0, G_OPTION_ARG_FILENAME, &opt_runtime_base,
    "If a --runtime is a relative path, look for it relative to BASE. "
    "[Default: $PRESSURE_VESSEL_RUNTIME_BASE or '.']",
    "BASE" },
  { "share-home", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_share_home_cb,
    "Use the real home directory. "
    "[Default unless $PRESSURE_VESSEL_HOME is set or "
    "$PRESSURE_VESSEL_SHARE_HOME is 0]",
    NULL },
  { "unshare-home", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_share_home_cb,
    "Use an app-specific home directory chosen according to --home, "
    "--freedesktop-app-id, --steam-app-id or $SteamAppId. "
    "[Default if $PRESSURE_VESSEL_HOME is set or "
    "$PRESSURE_VESSEL_SHARE_HOME is 0]",
    NULL },
  { "shell", 0, 0, G_OPTION_ARG_CALLBACK, opt_shell_cb,
    "--shell=after is equivalent to --shell-after, and so on. "
    "[Default: $PRESSURE_VESSEL_SHELL or 'none']",
    "{none|after|fail|instead}" },
  { "shell-after", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_shell_cb,
    "Run an interactive shell after COMMAND. Executing \"$@\" in that "
    "shell will re-run COMMAND [ARGS].",
    NULL },
  { "shell-fail", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_shell_cb,
    "Run an interactive shell after COMMAND, but only if it fails.",
    NULL },
  { "shell-instead", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_shell_cb,
    "Run an interactive shell instead of COMMAND. Executing \"$@\" in that "
    "shell will run COMMAND [ARGS].",
    NULL },
  { "terminal", 0, 0, G_OPTION_ARG_CALLBACK, opt_terminal_cb,
    "none: disable features that would use a terminal; "
    "auto: equivalent to xterm if a --shell option is used, or none; "
    "xterm: put game output (and --shell if used) in an xterm; "
    "tty: put game output (and --shell if used) on Steam's "
    "controlling tty "
    "[Default: $PRESSURE_VESSEL_TERMINAL or 'auto']",
    "{none|auto|xterm|tty}" },
  { "tty", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_terminal_cb,
    "Equivalent to --terminal=tty", NULL },
  { "xterm", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK, opt_terminal_cb,
    "Equivalent to --terminal=xterm", NULL },
  { "verbose", 0, 0, G_OPTION_ARG_NONE, &opt_verbose,
    "Be more verbose.", NULL },
  { "version", 0, 0, G_OPTION_ARG_NONE, &opt_version,
    "Print version number and exit.", NULL },
  { NULL }
};

static gboolean
boolean_environment (const gchar *name,
                     gboolean def)
{
  const gchar *value = g_getenv (name);

  if (g_strcmp0 (value, "1") == 0)
    return TRUE;

  if (g_strcmp0 (value, "") == 0 || g_strcmp0 (value, "0") == 0)
    return FALSE;

  if (value != NULL)
    g_warning ("Unrecognised value \"%s\" for $%s", value, name);

  return def;
}

static Tristate
tristate_environment (const gchar *name)
{
  const gchar *value = g_getenv (name);

  if (g_strcmp0 (value, "1") == 0)
    return TRISTATE_YES;

  if (g_strcmp0 (value, "0") == 0)
    return TRISTATE_NO;

  if (value != NULL && value[0] != '\0')
    g_warning ("Unrecognised value \"%s\" for $%s", value, name);

  return TRISTATE_MAYBE;
}

static void
cli_log_func (const gchar *log_domain,
              GLogLevelFlags log_level,
              const gchar *message,
              gpointer user_data)
{
  g_printerr ("%s: %s\n", (const char *) user_data, message);
}

int
main (int argc,
      char *argv[])
{
  g_autoptr(GOptionContext) context = NULL;
  g_autoptr(GError) local_error = NULL;
  GError **error = &local_error;
  int ret = 2;
  gsize i;
  g_auto(GStrv) original_argv = NULL;
  int original_argc = argc;
  g_autoptr(FlatpakBwrap) bwrap = NULL;
  g_autoptr(FlatpakBwrap) wrapped_command = NULL;
  g_autofree gchar *tmpdir = NULL;
  g_autofree gchar *scratch = NULL;
  g_autofree gchar *overrides = NULL;
  g_autofree gchar *bwrap_executable = NULL;
  g_autoptr(GString) ld_library_path = g_string_new ("");
  g_autoptr(GString) adjusted_ld_preload = g_string_new ("");
  g_autofree gchar *cwd_p = NULL;
  g_autofree gchar *cwd_l = NULL;
  const gchar *home;
  g_autofree gchar *bwrap_help = NULL;
  g_autofree gchar *tools_dir = NULL;
  const gchar *bwrap_help_argv[] = { "<bwrap>", "--help", NULL };

  setlocale (LC_ALL, "");
  pv_avoid_gvfs ();

  g_set_prgname ("pressure-vessel-wrap");

  g_log_set_handler (G_LOG_DOMAIN,
                     G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE,
                     cli_log_func, (void *) g_get_prgname ());

  original_argv = g_new0 (char *, argc + 1);

  for (i = 0; i < argc; i++)
    original_argv[i] = g_strdup (argv[i]);

  if (g_getenv ("STEAM_RUNTIME") != NULL)
    {
      g_printerr ("%s: This program should not be run in the Steam Runtime.",
                  g_get_prgname ());
      g_printerr ("%s: Use pressure-vessel-unruntime instead.",
                  g_get_prgname ());
      ret = 2;
      goto out;
    }

  /* Set defaults */
  opt_freedesktop_app_id = g_strdup (g_getenv ("PRESSURE_VESSEL_FDO_APP_ID"));

  if (opt_freedesktop_app_id != NULL && opt_freedesktop_app_id[0] == '\0')
    g_clear_pointer (&opt_freedesktop_app_id, g_free);

  opt_home = g_strdup (g_getenv ("PRESSURE_VESSEL_HOME"));

  if (opt_home != NULL && opt_home[0] == '\0')
    g_clear_pointer (&opt_home, g_free);

  opt_share_home = tristate_environment ("PRESSURE_VESSEL_SHARE_HOME");
  opt_verbose = boolean_environment ("PRESSURE_VESSEL_VERBOSE", FALSE);

  if (!opt_shell_cb ("$PRESSURE_VESSEL_SHELL",
                     g_getenv ("PRESSURE_VESSEL_SHELL"), NULL, error))
    goto out;

  if (!opt_terminal_cb ("$PRESSURE_VESSEL_TERMINAL",
                        g_getenv ("PRESSURE_VESSEL_TERMINAL"), NULL, error))
    goto out;

  context = g_option_context_new ("[--] COMMAND [ARGS]\n"
                                  "Run COMMAND [ARGS] in a container.\n");

  g_option_context_add_main_entries (context, options, NULL);

  if (!g_option_context_parse (context, &argc, &argv, error))
    goto out;

  if (opt_runtime == NULL)
    opt_runtime = g_strdup (g_getenv ("PRESSURE_VESSEL_RUNTIME"));

  if (opt_runtime_base == NULL)
    opt_runtime_base = g_strdup (g_getenv ("PRESSURE_VESSEL_RUNTIME_BASE"));

  if (opt_runtime != NULL
      && opt_runtime[0] != '\0'
      && !g_path_is_absolute (opt_runtime)
      && opt_runtime_base != NULL
      && opt_runtime_base[0] != '\0')
    {
      g_autofree gchar *tmp = g_steal_pointer (&opt_runtime);

      opt_runtime = g_build_filename (opt_runtime_base, tmp, NULL);
    }

  if (opt_version)
    {
      g_print ("pressure-vessel version %s\n", VERSION);
      ret = 0;
      goto out;
    }

  if (argc < 2)
    {
      g_printerr ("%s: An executable to run is required\n",
                  g_get_prgname ());
      goto out;
    }

  if (opt_terminal == PV_TERMINAL_AUTO)
    {
      if (opt_shell != PV_SHELL_NONE)
        opt_terminal = PV_TERMINAL_XTERM;
      else
        opt_terminal = PV_TERMINAL_NONE;
    }

  if (opt_terminal == PV_TERMINAL_NONE && opt_shell != PV_SHELL_NONE)
    {
      g_printerr ("%s: --terminal=none is incompatible with --shell\n",
                  g_get_prgname ());
      goto out;
    }

  if (strcmp (argv[1], "--") == 0)
    {
      argv++;
      argc--;
    }

  home = g_get_home_dir ();

  if (opt_share_home == TRISTATE_YES)
    {
      opt_fake_home = NULL;
    }
  else if (opt_home)
    {
      opt_fake_home = g_strdup (opt_home);
    }
  else if (opt_share_home == TRISTATE_MAYBE)
    {
      opt_fake_home = NULL;
    }
  else if (opt_freedesktop_app_id)
    {
      opt_fake_home = g_build_filename (home, ".var", "app",
                                        opt_freedesktop_app_id, NULL);
    }
  else if (opt_steam_app_id)
    {
      opt_freedesktop_app_id = g_strdup_printf ("com.steampowered.App%s",
                                                opt_steam_app_id);
      opt_fake_home = g_build_filename (home, ".var", "app",
                                        opt_freedesktop_app_id, NULL);
    }
  else if (g_getenv ("SteamAppId") != NULL)
    {
      opt_freedesktop_app_id = g_strdup_printf ("com.steampowered.App%s",
                                                g_getenv ("SteamAppId"));
      opt_fake_home = g_build_filename (home, ".var", "app",
                                        opt_freedesktop_app_id, NULL);
    }
  else
    {
      g_printerr ("%s: Either --home, --freedesktop-app-id, --steam-app-id "
                  "or $SteamAppId is required\n",
                  g_get_prgname ());
      goto out;
    }

  if (opt_env_if_host != NULL)
    {
      for (i = 0; opt_env_if_host[i] != NULL; i++)
        {
          const char *equals = strchr (opt_env_if_host[i], '=');

          if (equals == NULL)
            g_printerr ("%s: --env-if-host argument must be of the form "
                        "NAME=VALUE, not \"%s\"\n",
                        g_get_prgname (), opt_env_if_host[i]);
        }
    }

  /* Finished parsing arguments, so any subsequent failures will make
   * us exit 1. */
  ret = 1;

  if (opt_terminal != PV_TERMINAL_TTY)
    {
      int fd;

      if (!glnx_openat_rdonly (-1, "/dev/null", TRUE, &fd, error))
          goto out;

      if (dup2 (fd, STDIN_FILENO) < 0)
        {
          glnx_throw_errno_prefix (error,
                                   "Cannot replace stdin with /dev/null");
          goto out;
        }
    }

  pv_get_current_dirs (&cwd_p, &cwd_l);

  if (opt_verbose)
    {
      g_auto(GStrv) env = g_get_environ ();

      g_log_set_handler (G_LOG_DOMAIN,
                         G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_INFO,
                         cli_log_func, (void *) g_get_prgname ());

      g_message ("Original argv:");

      for (i = 0; i < original_argc; i++)
        {
          g_autofree gchar *quoted = g_shell_quote (original_argv[i]);

          g_message ("\t%" G_GSIZE_FORMAT ": %s", i, quoted);
        }

      g_message ("Current working directory:");
      g_message ("\tPhysical: %s", cwd_p);
      g_message ("\tLogical: %s", cwd_l);

      g_message ("Environment variables:");

      qsort (env, g_strv_length (env), sizeof (char *), pv_envp_cmp);

      for (i = 0; env[i] != NULL; i++)
        {
          g_autofree gchar *quoted = g_shell_quote (env[i]);

          g_message ("\t%s", env[i]);
        }

      g_message ("Wrapped command:");

      for (i = 1; i < argc; i++)
        {
          g_autofree gchar *quoted = g_shell_quote (argv[i]);

          g_message ("\t%" G_GSIZE_FORMAT ": %s", i, quoted);
        }
    }

  tools_dir = find_executable_dir (error);

  if (tools_dir == NULL)
    goto out;

  g_debug ("Found executable directory: %s", tools_dir);

  wrapped_command = flatpak_bwrap_new (NULL);

  switch (opt_terminal)
    {
      case PV_TERMINAL_TTY:
        g_debug ("Wrapping command to use tty");

        if (!pv_bwrap_wrap_tty (wrapped_command, error))
          goto out;

        break;

      case PV_TERMINAL_XTERM:
        g_debug ("Wrapping command with xterm");
        pv_bwrap_wrap_in_xterm (wrapped_command);
        break;

      case PV_TERMINAL_AUTO:
      case PV_TERMINAL_NONE:
      default:
        /* do nothing */
        break;
    }

  if (opt_shell != PV_SHELL_NONE || opt_terminal == PV_TERMINAL_XTERM)
    {
      /* In the (PV_SHELL_NONE, PV_TERMINAL_XTERM) case, just don't let the
       * xterm close before the user has had a chance to see the output */
      pv_bwrap_wrap_interactive (wrapped_command, opt_shell);
    }

  if (argv[1][0] == '-')
    {
      /* Make sure wrapped_command is something we can validly pass to env(1) */
      if (strchr (argv[1], '=') != NULL)
        flatpak_bwrap_add_args (wrapped_command,
                                "sh", "-euc", "exec \"$@\"", "sh",
                                NULL);

      /* Make sure bwrap will interpret wrapped_command as the end of its
       * options */
      flatpak_bwrap_add_arg (wrapped_command, "env");
    }

  g_debug ("Setting arguments for wrapped command");
  flatpak_bwrap_append_argsv (wrapped_command, &argv[1], argc - 1);

  g_debug ("Checking for bwrap...");
  bwrap_executable = check_bwrap (tools_dir);

  if (bwrap_executable == NULL)
    {
      if (opt_host_fallback)
        {
          g_message ("Falling back to executing wrapped command directly");

          if (opt_env_if_host != NULL)
            {
              for (i = 0; opt_env_if_host[i] != NULL; i++)
                {
                  char *equals = strchr (opt_env_if_host[i], '=');

                  g_assert (equals != NULL);

                  *equals = '\0';
                  flatpak_bwrap_set_env (wrapped_command, opt_env_if_host[i],
                                         equals + 1, TRUE);
                }
            }

          flatpak_bwrap_finish (wrapped_command);

          /* flatpak_bwrap_finish did this */
          g_assert (g_ptr_array_index (wrapped_command->argv,
                                       wrapped_command->argv->len - 1) == NULL);

          execvpe (g_ptr_array_index (wrapped_command->argv, 0),
                   (char * const *) wrapped_command->argv->pdata,
                   wrapped_command->envp);

          glnx_throw_errno_prefix (error, "execvpe %s",
                                   (gchar *) g_ptr_array_index (wrapped_command->argv, 0));
          goto out;
        }
      else
        {
          goto out;
        }
    }

  g_debug ("Checking bwrap features...");
  bwrap_help_argv[0] = bwrap_executable;
  bwrap_help = capture_output (bwrap_help_argv, error);

  if (bwrap_help == NULL)
    goto out;

  g_debug ("Creating temporary directories...");
  tmpdir = g_dir_make_tmp ("pressure-vessel-wrap.XXXXXX", error);

  if (tmpdir == NULL)
    goto out;

  scratch = g_build_filename (tmpdir, "scratch", NULL);
  g_mkdir (scratch, 0700);
  overrides = g_build_filename (tmpdir, "overrides", NULL);
  g_mkdir (overrides, 0700);

  bwrap = flatpak_bwrap_new (NULL);
  flatpak_bwrap_add_arg (bwrap, bwrap_executable);

  /* Protect the controlling terminal from the app/game, unless we are
   * running an interactive shell in which case that would break its
   * job control. */
  if (opt_terminal != PV_TERMINAL_TTY)
    flatpak_bwrap_add_arg (bwrap, "--new-session");

  if (opt_runtime != NULL && opt_runtime[0] != '\0')
    {
      g_debug ("Configuring runtime...");

      /* TODO: Adapt the use_ld_so_cache code from Flatpak instead
       * of setting LD_LIBRARY_PATH, for better robustness against
       * games that set their own LD_LIBRARY_PATH ignoring what they
       * got from the environment */
      for (i = 0; i < G_N_ELEMENTS (multiarch_tuples); i++)
        {
          g_autofree gchar *ld_path = NULL;

          ld_path = g_build_filename ("/overrides", "lib",
                                     multiarch_tuples[i], NULL);

          search_path_append (ld_library_path, ld_path);
        }

      /* This would be filtered out by a setuid bwrap, so we have to go
       * via --setenv. */
      flatpak_bwrap_add_args (bwrap,
                              "--setenv", "LD_LIBRARY_PATH",
                              ld_library_path->str,
                              NULL);

      if (!bind_runtime (bwrap, tools_dir, opt_runtime, overrides,
                         scratch, error))
        goto out;
    }
  else
    {
      flatpak_bwrap_add_args (bwrap,
                              "--bind", "/", "/",
                              NULL);
    }

  /* Make /dev available */
  flatpak_bwrap_add_args (bwrap,
                          "--dev-bind", "/dev", "/dev",
                          NULL);

  if (g_file_test ("/dev/pts", G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--dev-bind", "/dev/pts", "/dev/pts",
                            NULL);

  if (g_file_test ("/dev/shm", G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--dev-bind", "/dev/shm", "/dev/shm",
                            NULL);

  /* Protect other users' homes (but guard against the unlikely
   * situation that they don't exist) */
  if (g_file_test ("/home", G_FILE_TEST_EXISTS))
    flatpak_bwrap_add_args (bwrap,
                            "--tmpfs", "/home",
                            NULL);

  if (opt_fake_home == NULL)
    {
      flatpak_bwrap_add_args (bwrap,
                              "--bind", home, home,
                              NULL);
    }
  else
    {
      if (!use_fake_home (bwrap, opt_fake_home, error))
        goto out;
    }

  g_debug ("Adjusting LD_PRELOAD...");

  /* We need the LD_PRELOADs from Steam visible at the paths that were
   * used for them, which might be their physical rather than logical
   * locations. */
  if (opt_ld_preload != NULL)
    {
      for (i = 0; i < opt_ld_preload->len; i++)
        {
          const char *preload = g_ptr_array_index (opt_ld_preload, i);

          g_assert (preload != NULL);

          if (*preload == '\0')
            continue;

          /* We have the beginnings of infrastructure to set a LD_PRELOAD
           * from inside the container, but currently the only thing we
           * support is it coming from the host. */
          g_assert (g_str_has_prefix (preload, "host:"));
          preload = preload + 5;

          if (g_file_test (preload, G_FILE_TEST_EXISTS))
            {
              if (opt_runtime != NULL
                  && opt_runtime[0] != '\0'
                  && (g_str_has_prefix (preload, "/usr/")
                      || g_str_has_prefix (preload, "/lib")))
                {
                  g_autofree gchar *in_run_host = g_build_filename ("/run/host",
                                                                    preload,
                                                                    NULL);

                  /* When using a runtime we can't write to /usr/ or /libQUAL/,
                   * so redirect this preloaded module to the corresponding
                   * location in /run/host. */
                  search_path_append (adjusted_ld_preload, in_run_host);
                }
              else
                {
                  flatpak_bwrap_add_args (bwrap,
                                          "--ro-bind", preload, preload,
                                          NULL);
                  search_path_append (adjusted_ld_preload, preload);
                }
            }
          else
            {
              g_debug ("LD_PRELOAD module '%s' does not exist", preload);
            }
        }
    }

  /* Put the caller's LD_PRELOAD back.
   * This would be filtered out by a setuid bwrap, so we have to go
   * via --setenv. */

  if (adjusted_ld_preload->len != 0)
      flatpak_bwrap_add_args (bwrap,
                              "--setenv", "LD_PRELOAD",
                              adjusted_ld_preload->str,
                              NULL);
  else
      flatpak_bwrap_add_args (bwrap,
                              "--unsetenv", "LD_PRELOAD",
                              NULL);

  /* Make sure the current working directory (the game we are going to
   * run) is available. Some games write here. */

  g_debug ("Making home directory available...");

  if (pv_is_same_file (home, cwd_p))
    {
      g_debug ("Not making physical working directory \"%s\" available to "
               "container because it is the home directory",
               cwd_p);
    }
  else
    {
      flatpak_bwrap_add_args (bwrap,
                              "--bind", cwd_p, cwd_p,
                              NULL);
    }

  flatpak_bwrap_add_args (bwrap,
                          "--chdir", cwd_p,
                          "--unsetenv", "PWD",
                          NULL);

  /* TODO: Potential future expansion: use --unshare-pid for more isolation */

  /* Put Steam Runtime environment variables back, if /usr is mounted
   * from the host. */
  if (opt_runtime == NULL && opt_runtime[0] != '\0')
    {
      g_debug ("Making Steam Runtime available...");

      /* We need libraries from the Steam Runtime, so make sure that's
       * visible (it should never need to be read/write though) */
      if (opt_env_if_host != NULL)
        {
          for (i = 0; opt_env_if_host[i] != NULL; i++)
            {
              char *equals = strchr (opt_env_if_host[i], '=');

              g_assert (equals != NULL);

              if (g_str_has_prefix (opt_env_if_host[i], "STEAM_RUNTIME=/"))
                {
                  flatpak_bwrap_add_args (bwrap,
                                          "--ro-bind", equals + 1,
                                          equals + 1,
                                          NULL);
                }

              *equals = '\0';
              /* We do this via --setenv instead of flatpak_bwrap_set_env()
               * to make sure they aren't filtered out by a setuid bwrap. */
              flatpak_bwrap_add_args (bwrap,
                                      "--setenv", opt_env_if_host[i],
                                      equals + 1,
                                      NULL);
              *equals = '=';
            }
        }
    }

  if (opt_verbose)
    {
      g_message ("%s options before bundling:", bwrap_executable);

      for (i = 0; i < bwrap->argv->len; i++)
        {
          g_autofree gchar *quoted = NULL;

          quoted = g_shell_quote (g_ptr_array_index (bwrap->argv, i));
          g_message ("\t%s", quoted);
        }
    }

  if (!flatpak_bwrap_bundle_args (bwrap, 1, -1, FALSE, error))
    goto out;

  g_debug ("Adding wrapped command...");
  flatpak_bwrap_append_args (bwrap, wrapped_command->argv);

  if (opt_verbose)
    {
      g_message ("Final %s options:", bwrap_executable);

      for (i = 0; i < bwrap->argv->len; i++)
        {
          g_autofree gchar *quoted = NULL;

          quoted = g_shell_quote (g_ptr_array_index (bwrap->argv, i));
          g_message ("\t%s", quoted);
        }

      g_message ("%s environment:", bwrap_executable);

      for (i = 0; bwrap->envp != NULL && bwrap->envp[i] != NULL; i++)
        {
          g_autofree gchar *quoted = NULL;

          quoted = g_shell_quote (bwrap->envp[i]);
          g_message ("\t%s", quoted);
        }
    }

  /* Clean up temporary directory before running our long-running process */
  if (tmpdir != NULL &&
      !glnx_shutil_rm_rf_at (-1, tmpdir, NULL, error))
    {
      g_warning ("Unable to delete temporary directory: %s",
                 local_error->message);
      g_clear_error (&local_error);
    }

  g_clear_pointer (&tmpdir, g_free);

  flatpak_bwrap_finish (bwrap);
  pv_bwrap_execve (bwrap, error);

out:
  if (local_error != NULL)
    g_warning ("%s", local_error->message);

  if (tmpdir != NULL &&
      !glnx_shutil_rm_rf_at (-1, tmpdir, NULL, error))
    {
      g_warning ("Unable to delete temporary directory: %s",
                 local_error->message);
      g_clear_error (&local_error);
    }

  g_clear_pointer (&opt_ld_preload, g_ptr_array_unref);
  g_clear_pointer (&opt_env_if_host, g_strfreev);
  g_clear_pointer (&opt_freedesktop_app_id, g_free);
  g_clear_pointer (&opt_steam_app_id, g_free);
  g_clear_pointer (&opt_home, g_free);
  g_clear_pointer (&opt_fake_home, g_free);
  g_clear_pointer (&opt_runtime_base, g_free);
  g_clear_pointer (&opt_runtime, g_free);

  return ret;
}
