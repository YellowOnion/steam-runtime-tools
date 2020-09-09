/*
 * Cut-down version of common/flatpak-run.c from Flatpak
 * Last updated: Flatpak 1.6.1
 *
 * Copyright © 2017-2019 Collabora Ltd.
 * Copyright © 2014-2019 Red Hat, Inc
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
 *
 * Authors:
 *       Alexander Larsson <alexl@redhat.com>
 */

#include "config.h"

#include "flatpak-run-private.h"

#include <sys/utsname.h>

#include <X11/Xauth.h>
#include <gio/gio.h>
#include "libglnx/libglnx.h"

#include "flatpak-utils-base-private.h"
#include "flatpak-utils-private.h"

/* In Flatpak this is optional, in pressure-vessel not so much */
#define ENABLE_XAUTH

const char * const abs_usrmerged_dirs[] =
{
  "/bin",
  "/lib",
  "/lib32",
  "/lib64",
  "/sbin",
  NULL
};
const char * const *flatpak_abs_usrmerged_dirs = abs_usrmerged_dirs;

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
#endif /* ENABLE_XAUTH */

void
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

      if (glnx_open_anonymous_tmpfile_full (O_RDWR | O_CLOEXEC, "/tmp", &xauth_tmpf, NULL))
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

gboolean
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

void
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

  /* Also allow ALSA access. This was added in 1.8, and is not ideally named. However,
   * since the practical permission of ALSA and PulseAudio are essentially the same, and
   * since we don't want to add more permissions for something we plan to replace with
   * portals/pipewire going forward we reinterpret pulseaudio to also mean ALSA.
   */
  if (g_file_test ("/dev/snd", G_FILE_TEST_IS_DIR))
    flatpak_bwrap_add_args (bwrap, "--dev-bind", "/dev/snd", "/dev/snd", NULL);
}

/* Simplified from Flatpak: we never restrict access to the D-Bus system bus */
gboolean
flatpak_run_add_system_dbus_args (FlatpakBwrap   *app_bwrap)
{
  const char *dbus_address = g_getenv ("DBUS_SYSTEM_BUS_ADDRESS");
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
gboolean
flatpak_run_add_session_dbus_args (FlatpakBwrap   *app_bwrap)
{
  const char *dbus_address = g_getenv ("DBUS_SESSION_BUS_ADDRESS");
  g_autofree char *dbus_session_socket = NULL;
  g_autofree char *sandbox_socket_path = g_strdup_printf ("/run/user/%d/bus", getuid ());
  g_autofree char *sandbox_dbus_address = g_strdup_printf ("unix:path=/run/user/%d/bus", getuid ());

  if (dbus_address != NULL)
    {
      dbus_session_socket = extract_unix_path_from_dbus_address (dbus_address);
    }
  else
    {
      g_autofree char *user_runtime_dir = flatpak_get_real_xdg_runtime_dir ();
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

#if 0

static gboolean
flatpak_run_add_a11y_dbus_args (FlatpakBwrap   *app_bwrap,
                                FlatpakBwrap   *proxy_arg_bwrap,
                                FlatpakContext *context,
                                FlatpakRunFlags flags)
{
  g_autoptr(GDBusConnection) session_bus = NULL;
  g_autofree char *a11y_address = NULL;
  g_autoptr(GError) local_error = NULL;
  g_autoptr(GDBusMessage) reply = NULL;
  g_autoptr(GDBusMessage) msg = NULL;
  g_autofree char *proxy_socket = NULL;

  if ((flags & FLATPAK_RUN_FLAG_NO_A11Y_BUS_PROXY) != 0)
    return FALSE;

  session_bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
  if (session_bus == NULL)
    return FALSE;

  msg = g_dbus_message_new_method_call ("org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress");
  g_dbus_message_set_body (msg, g_variant_new ("()"));
  reply =
    g_dbus_connection_send_message_with_reply_sync (session_bus, msg,
                                                    G_DBUS_SEND_MESSAGE_FLAGS_NONE,
                                                    30000,
                                                    NULL,
                                                    NULL,
                                                    NULL);
  if (reply)
    {
      if (g_dbus_message_to_gerror (reply, &local_error))
        {
          if (!g_error_matches (local_error, G_DBUS_ERROR, G_DBUS_ERROR_SERVICE_UNKNOWN))
            g_message ("Can't find a11y bus: %s", local_error->message);
        }
      else
        {
          g_variant_get (g_dbus_message_get_body (reply),
                         "(s)", &a11y_address);
        }
    }

  if (!a11y_address)
    return FALSE;

  proxy_socket = create_proxy_socket ("a11y-bus-proxy-XXXXXX");
  if (proxy_socket == NULL)
    return FALSE;

  g_autofree char *sandbox_socket_path = g_strdup_printf ("/run/user/%d/at-spi-bus", getuid ());
  g_autofree char *sandbox_dbus_address = g_strdup_printf ("unix:path=/run/user/%d/at-spi-bus", getuid ());

  flatpak_bwrap_add_args (proxy_arg_bwrap,
                          a11y_address,
                          proxy_socket, "--filter", "--sloppy-names",
                          "--call=org.a11y.atspi.Registry=org.a11y.atspi.Socket.Embed@/org/a11y/atspi/accessible/root",
                          "--call=org.a11y.atspi.Registry=org.a11y.atspi.Socket.Unembed@/org/a11y/atspi/accessible/root",
                          "--call=org.a11y.atspi.Registry=org.a11y.atspi.Registry.GetRegisteredEvents@/org/a11y/atspi/registry",
                          "--call=org.a11y.atspi.Registry=org.a11y.atspi.DeviceEventController.GetKeystrokeListeners@/org/a11y/atspi/registry/deviceeventcontroller",
                          "--call=org.a11y.atspi.Registry=org.a11y.atspi.DeviceEventController.GetDeviceEventListeners@/org/a11y/atspi/registry/deviceeventcontroller",
                          "--call=org.a11y.atspi.Registry=org.a11y.atspi.DeviceEventController.NotifyListenersSync@/org/a11y/atspi/registry/deviceeventcontroller",
                          "--call=org.a11y.atspi.Registry=org.a11y.atspi.DeviceEventController.NotifyListenersAsync@/org/a11y/atspi/registry/deviceeventcontroller",
                          NULL);

  if ((flags & FLATPAK_RUN_FLAG_LOG_A11Y_BUS) != 0)
    flatpak_bwrap_add_args (proxy_arg_bwrap, "--log", NULL);

  flatpak_bwrap_add_args (app_bwrap,
                          "--ro-bind", proxy_socket, sandbox_socket_path,
                          NULL);
  flatpak_bwrap_set_env (app_bwrap, "AT_SPI_BUS_ADDRESS", sandbox_dbus_address, TRUE);

  return TRUE;
}

/* This wraps the argv in a bwrap call, primary to allow the
   command to be run with a proper /.flatpak-info with data
   taken from app_info_path */
static gboolean
add_bwrap_wrapper (FlatpakBwrap *bwrap,
                   const char   *app_info_path,
                   GError      **error)
{
  glnx_autofd int app_info_fd = -1;
  g_auto(GLnxDirFdIterator) dir_iter = { 0 };
  struct dirent *dent;
  g_autofree char *user_runtime_dir = flatpak_get_real_xdg_runtime_dir ();
  g_autofree char *proxy_socket_dir = g_build_filename (user_runtime_dir, ".dbus-proxy/", NULL);

  app_info_fd = open (app_info_path, O_RDONLY | O_CLOEXEC);
  if (app_info_fd == -1)
    return glnx_throw_errno_prefix (error, _("Failed to open app info file"));

  if (!glnx_dirfd_iterator_init_at (AT_FDCWD, "/", FALSE, &dir_iter, error))
    return FALSE;

  flatpak_bwrap_add_arg (bwrap, flatpak_get_bwrap ());

  while (TRUE)
    {
      glnx_autofd int o_path_fd = -1;
      struct statfs stfs;

      if (!glnx_dirfd_iterator_next_dent_ensure_dtype (&dir_iter, &dent, NULL, error))
        return FALSE;

      if (dent == NULL)
        break;

      if (strcmp (dent->d_name, ".flatpak-info") == 0)
        continue;

      /* O_PATH + fstatfs is the magic that we need to statfs without automounting the target */
      o_path_fd = openat (dir_iter.fd, dent->d_name, O_PATH | O_NOFOLLOW | O_CLOEXEC);
      if (o_path_fd == -1 || fstatfs (o_path_fd, &stfs) != 0 || stfs.f_type == AUTOFS_SUPER_MAGIC)
        continue; /* AUTOFS mounts are risky and can cause us to block (see issue #1633), so ignore it. Its unlikely the proxy needs such a directory. */

      if (dent->d_type == DT_DIR)
        {
          if (strcmp (dent->d_name, "tmp") == 0 ||
              strcmp (dent->d_name, "var") == 0 ||
              strcmp (dent->d_name, "run") == 0)
            flatpak_bwrap_add_arg (bwrap, "--bind");
          else
            flatpak_bwrap_add_arg (bwrap, "--ro-bind");

          flatpak_bwrap_add_arg_printf (bwrap, "/%s", dent->d_name);
          flatpak_bwrap_add_arg_printf (bwrap, "/%s", dent->d_name);
        }
      else if (dent->d_type == DT_LNK)
        {
          g_autofree gchar *target = NULL;

          target = glnx_readlinkat_malloc (dir_iter.fd, dent->d_name,
                                           NULL, error);
          if (target == NULL)
            return FALSE;
          flatpak_bwrap_add_args (bwrap, "--symlink", target, NULL);
          flatpak_bwrap_add_arg_printf (bwrap, "/%s", dent->d_name);
        }
    }

  flatpak_bwrap_add_args (bwrap, "--bind", proxy_socket_dir, proxy_socket_dir, NULL);

  /* This is a file rather than a bind mount, because it will then
     not be unmounted from the namespace when the namespace dies. */
  flatpak_bwrap_add_args_data_fd (bwrap, "--file", glnx_steal_fd (&app_info_fd), "/.flatpak-info");

  if (!flatpak_bwrap_bundle_args (bwrap, 1, -1, FALSE, error))
    return FALSE;

  return TRUE;
}

static gboolean
start_dbus_proxy (FlatpakBwrap *app_bwrap,
                  FlatpakBwrap *proxy_arg_bwrap,
                  const char   *app_info_path,
                  GError      **error)
{
  char x = 'x';
  const char *proxy;
  g_autofree char *commandline = NULL;
  g_autoptr(FlatpakBwrap) proxy_bwrap = NULL;
  int sync_fds[2] = {-1, -1};
  int proxy_start_index;
  g_auto(GStrv) minimal_envp = NULL;

  minimal_envp = flatpak_run_get_minimal_env (FALSE, FALSE);
  proxy_bwrap = flatpak_bwrap_new (NULL);

  if (!add_bwrap_wrapper (proxy_bwrap, app_info_path, error))
    return FALSE;

  proxy = g_getenv ("FLATPAK_DBUSPROXY");
  if (proxy == NULL)
    proxy = DBUSPROXY;

  flatpak_bwrap_add_arg (proxy_bwrap, proxy);

  proxy_start_index = proxy_bwrap->argv->len;

  if (pipe2 (sync_fds, O_CLOEXEC) < 0)
    {
      g_set_error_literal (error, G_IO_ERROR, g_io_error_from_errno (errno),
                           _("Unable to create sync pipe"));
      return FALSE;
    }

  /* read end goes to app */
  flatpak_bwrap_add_args_data_fd (app_bwrap, "--sync-fd", sync_fds[0], NULL);

  /* write end goes to proxy */
  flatpak_bwrap_add_fd (proxy_bwrap, sync_fds[1]);
  flatpak_bwrap_add_arg_printf (proxy_bwrap, "--fd=%d", sync_fds[1]);

  /* Note: This steals the fds from proxy_arg_bwrap */
  flatpak_bwrap_append_bwrap (proxy_bwrap, proxy_arg_bwrap);

  if (!flatpak_bwrap_bundle_args (proxy_bwrap, proxy_start_index, -1, TRUE, error))
    return FALSE;

  flatpak_bwrap_finish (proxy_bwrap);

  commandline = flatpak_quote_argv ((const char **) proxy_bwrap->argv->pdata, -1);
  g_debug ("Running '%s'", commandline);

  /* We use LEAVE_DESCRIPTORS_OPEN to work around dead-lock, see flatpak_close_fds_workaround */
  if (!g_spawn_async (NULL,
                      (char **) proxy_bwrap->argv->pdata,
                      NULL,
                      G_SPAWN_SEARCH_PATH | G_SPAWN_LEAVE_DESCRIPTORS_OPEN,
                      flatpak_bwrap_child_setup_cb, proxy_bwrap->fds,
                      NULL, error))
    return FALSE;

  /* The write end can be closed now, otherwise the read below will hang of xdg-dbus-proxy
     fails to start. */
  g_clear_pointer (&proxy_bwrap, flatpak_bwrap_free);

  /* Sync with proxy, i.e. wait until its listening on the sockets */
  if (read (sync_fds[0], &x, 1) != 1)
    {
      g_set_error_literal (error, G_IO_ERROR, g_io_error_from_errno (errno),
                           _("Failed to sync with dbus proxy"));
      return FALSE;
    }

  return TRUE;
}

static int
flatpak_extension_compare_by_path (gconstpointer _a,
                                   gconstpointer _b)
{
  const FlatpakExtension *a = _a;
  const FlatpakExtension *b = _b;

  return g_strcmp0 (a->directory, b->directory);
}

gboolean
flatpak_run_add_extension_args (FlatpakBwrap *bwrap,
                                GKeyFile     *metakey,
                                const char   *full_ref,
                                gboolean      use_ld_so_cache,
                                char        **extensions_out,
                                GCancellable *cancellable,
                                GError      **error)
{
  g_auto(GStrv) parts = NULL;
  g_autoptr(GString) used_extensions = g_string_new ("");
  gboolean is_app;
  GList *extensions, *path_sorted_extensions, *l;
  g_autoptr(GString) ld_library_path = g_string_new ("");
  int count = 0;
  g_autoptr(GHashTable) mounted_tmpfs =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
  g_autoptr(GHashTable) created_symlink =
    g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  parts = g_strsplit (full_ref, "/", 0);
  if (g_strv_length (parts) != 4)
    return flatpak_fail_error (error, FLATPAK_ERROR_INVALID_REF, _("Failed to determine parts from ref: %s"), full_ref);

  is_app = strcmp (parts[0], "app") == 0;

  extensions = flatpak_list_extensions (metakey,
                                        parts[2], parts[3]);

  /* First we apply all the bindings, they are sorted alphabetically in order for parent directory
     to be mounted before child directories */
  path_sorted_extensions = g_list_copy (extensions);
  path_sorted_extensions = g_list_sort (path_sorted_extensions, flatpak_extension_compare_by_path);

  for (l = path_sorted_extensions; l != NULL; l = l->next)
    {
      FlatpakExtension *ext = l->data;
      g_autofree char *directory = g_build_filename (is_app ? "/app" : "/usr", ext->directory, NULL);
      g_autofree char *full_directory = g_build_filename (directory, ext->subdir_suffix, NULL);
      g_autofree char *ref = g_build_filename (full_directory, ".ref", NULL);
      g_autofree char *real_ref = g_build_filename (ext->files_path, ext->directory, ".ref", NULL);

      if (ext->needs_tmpfs)
        {
          g_autofree char *parent = g_path_get_dirname (directory);

          if (g_hash_table_lookup (mounted_tmpfs, parent) == NULL)
            {
              flatpak_bwrap_add_args (bwrap,
                                      "--tmpfs", parent,
                                      NULL);
              g_hash_table_insert (mounted_tmpfs, g_steal_pointer (&parent), "mounted");
            }
        }

      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind", ext->files_path, full_directory,
                              NULL);

      if (g_file_test (real_ref, G_FILE_TEST_EXISTS))
        flatpak_bwrap_add_args (bwrap,
                                "--lock-file", ref,
                                NULL);
    }

  g_list_free (path_sorted_extensions);

  /* Then apply library directories and file merging, in extension prio order */

  for (l = extensions; l != NULL; l = l->next)
    {
      FlatpakExtension *ext = l->data;
      g_autofree char *directory = g_build_filename (is_app ? "/app" : "/usr", ext->directory, NULL);
      g_autofree char *full_directory = g_build_filename (directory, ext->subdir_suffix, NULL);
      int i;

      if (used_extensions->len > 0)
        g_string_append (used_extensions, ";");
      g_string_append (used_extensions, ext->installed_id);
      g_string_append (used_extensions, "=");
      if (ext->commit != NULL)
        g_string_append (used_extensions, ext->commit);
      else
        g_string_append (used_extensions, "local");

      if (ext->add_ld_path)
        {
          g_autofree char *ld_path = g_build_filename (full_directory, ext->add_ld_path, NULL);

          if (use_ld_so_cache)
            {
              g_autofree char *contents = g_strconcat (ld_path, "\n", NULL);
              /* We prepend app or runtime and a counter in order to get the include order correct for the conf files */
              g_autofree char *ld_so_conf_file = g_strdup_printf ("%s-%03d-%s.conf", parts[0], ++count, ext->installed_id);
              g_autofree char *ld_so_conf_file_path = g_build_filename ("/run/flatpak/ld.so.conf.d", ld_so_conf_file, NULL);

              if (!flatpak_bwrap_add_args_data (bwrap, "ld-so-conf",
                                                contents, -1, ld_so_conf_file_path, error))
                return FALSE;
            }
          else
            {
              if (ld_library_path->len != 0)
                g_string_append (ld_library_path, ":");
              g_string_append (ld_library_path, ld_path);
            }
        }

      for (i = 0; ext->merge_dirs != NULL && ext->merge_dirs[i] != NULL; i++)
        {
          g_autofree char *parent = g_path_get_dirname (directory);
          g_autofree char *merge_dir = g_build_filename (parent, ext->merge_dirs[i], NULL);
          g_autofree char *source_dir = g_build_filename (ext->files_path, ext->merge_dirs[i], NULL);
          g_auto(GLnxDirFdIterator) source_iter = { 0 };
          struct dirent *dent;

          if (glnx_dirfd_iterator_init_at (AT_FDCWD, source_dir, TRUE, &source_iter, NULL))
            {
              while (glnx_dirfd_iterator_next_dent (&source_iter, &dent, NULL, NULL) && dent != NULL)
                {
                  g_autofree char *symlink_path = g_build_filename (merge_dir, dent->d_name, NULL);
                  /* Only create the first, because extensions are listed in prio order */
                  if (g_hash_table_lookup (created_symlink, symlink_path) == NULL)
                    {
                      g_autofree char *symlink = g_build_filename (directory, ext->merge_dirs[i], dent->d_name, NULL);
                      flatpak_bwrap_add_args (bwrap,
                                              "--symlink", symlink, symlink_path,
                                              NULL);
                      g_hash_table_insert (created_symlink, g_steal_pointer (&symlink_path), "created");
                    }
                }
            }
        }
    }

  g_list_free_full (extensions, (GDestroyNotify) flatpak_extension_free);

  if (ld_library_path->len != 0)
    {
      const gchar *old_ld_path = g_environ_getenv (bwrap->envp, "LD_LIBRARY_PATH");

      if (old_ld_path != NULL && *old_ld_path != 0)
        {
          if (is_app)
            {
              g_string_append (ld_library_path, ":");
              g_string_append (ld_library_path, old_ld_path);
            }
          else
            {
              g_string_prepend (ld_library_path, ":");
              g_string_prepend (ld_library_path, old_ld_path);
            }
        }

      flatpak_bwrap_set_env (bwrap, "LD_LIBRARY_PATH", ld_library_path->str, TRUE);
    }

  if (extensions_out)
    *extensions_out = g_string_free (g_steal_pointer (&used_extensions), FALSE);

  return TRUE;
}

gboolean
flatpak_run_add_environment_args (FlatpakBwrap    *bwrap,
                                  const char      *app_info_path,
                                  FlatpakRunFlags  flags,
                                  const char      *app_id,
                                  FlatpakContext  *context,
                                  GFile           *app_id_dir,
                                  GPtrArray       *previous_app_id_dirs,
                                  FlatpakExports **exports_out,
                                  GCancellable    *cancellable,
                                  GError         **error)
{
  g_autoptr(GError) my_error = NULL;
  g_autoptr(FlatpakExports) exports = NULL;
  g_autoptr(FlatpakBwrap) proxy_arg_bwrap = flatpak_bwrap_new (flatpak_bwrap_empty_env);
  gboolean has_wayland = FALSE;
  gboolean allow_x11 = FALSE;

  if ((context->shares & FLATPAK_CONTEXT_SHARED_IPC) == 0)
    {
      g_debug ("Disallowing ipc access");
      flatpak_bwrap_add_args (bwrap, "--unshare-ipc", NULL);
    }

  if ((context->shares & FLATPAK_CONTEXT_SHARED_NETWORK) == 0)
    {
      g_debug ("Disallowing network access");
      flatpak_bwrap_add_args (bwrap, "--unshare-net", NULL);
    }

  if (context->devices & FLATPAK_CONTEXT_DEVICE_ALL)
    {
      flatpak_bwrap_add_args (bwrap,
                              "--dev-bind", "/dev", "/dev",
                              NULL);
      /* Don't expose the host /dev/shm, just the device nodes, unless explicitly allowed */
      if (g_file_test ("/dev/shm", G_FILE_TEST_IS_DIR))
        {
          if ((context->devices & FLATPAK_CONTEXT_DEVICE_SHM) == 0)
            flatpak_bwrap_add_args (bwrap,
                                    "--tmpfs", "/dev/shm",
                                    NULL);
        }
      else if (g_file_test ("/dev/shm", G_FILE_TEST_IS_SYMLINK))
        {
          g_autofree char *link = flatpak_readlink ("/dev/shm", NULL);

          /* On debian (with sysv init) the host /dev/shm is a symlink to /run/shm, so we can't
             mount on top of it. */
          if (g_strcmp0 (link, "/run/shm") == 0)
            {
              if (context->devices & FLATPAK_CONTEXT_DEVICE_SHM &&
                  g_file_test ("/run/shm", G_FILE_TEST_IS_DIR))
                flatpak_bwrap_add_args (bwrap,
                                        "--bind", "/run/shm", "/run/shm",
                                        NULL);
              else
                flatpak_bwrap_add_args (bwrap,
                                        "--dir", "/run/shm",
                                        NULL);
            }
          else
            g_warning ("Unexpected /dev/shm symlink %s", link);
        }
    }
  else
    {
      flatpak_bwrap_add_args (bwrap,
                              "--dev", "/dev",
                              NULL);
      if (context->devices & FLATPAK_CONTEXT_DEVICE_DRI)
        {
          g_debug ("Allowing dri access");
          int i;
          char *dri_devices[] = {
            "/dev/dri",
            /* mali */
            "/dev/mali",
            "/dev/mali0",
            "/dev/umplock",
            /* nvidia */
            "/dev/nvidiactl",
            "/dev/nvidia-modeset",
            /* nvidia OpenCL/CUDA */
            "/dev/nvidia-uvm",
            "/dev/nvidia-uvm-tools",
          };

          for (i = 0; i < G_N_ELEMENTS (dri_devices); i++)
            {
              if (g_file_test (dri_devices[i], G_FILE_TEST_EXISTS))
                flatpak_bwrap_add_args (bwrap, "--dev-bind", dri_devices[i], dri_devices[i], NULL);
            }

          /* Each Nvidia card gets its own device.
             This is a fairly arbitrary limit but ASUS sells mining boards supporting 20 in theory. */
          char nvidia_dev[14]; /* /dev/nvidia plus up to 2 digits */
          for (i = 0; i < 20; i++)
            {
              g_snprintf (nvidia_dev, sizeof (nvidia_dev), "/dev/nvidia%d", i);
              if (g_file_test (nvidia_dev, G_FILE_TEST_EXISTS))
                flatpak_bwrap_add_args (bwrap, "--dev-bind", nvidia_dev, nvidia_dev, NULL);
            }
        }

      if (context->devices & FLATPAK_CONTEXT_DEVICE_KVM)
        {
          g_debug ("Allowing kvm access");
          if (g_file_test ("/dev/kvm", G_FILE_TEST_EXISTS))
            flatpak_bwrap_add_args (bwrap, "--dev-bind", "/dev/kvm", "/dev/kvm", NULL);
        }

      if (context->devices & FLATPAK_CONTEXT_DEVICE_SHM)
        {
          /* This is a symlink to /run/shm on debian, so bind to real target */
          g_autofree char *real_dev_shm = realpath ("/dev/shm", NULL);

          g_debug ("Allowing /dev/shm access (as %s)", real_dev_shm);
          if (real_dev_shm != NULL)
              flatpak_bwrap_add_args (bwrap, "--bind", real_dev_shm, "/dev/shm", NULL);
        }
    }

  flatpak_context_append_bwrap_filesystem (context, bwrap, app_id, app_id_dir, previous_app_id_dirs, &exports);

  if (context->sockets & FLATPAK_CONTEXT_SOCKET_WAYLAND)
    {
      g_debug ("Allowing wayland access");
      has_wayland = flatpak_run_add_wayland_args (bwrap);
    }

  if ((context->sockets & FLATPAK_CONTEXT_SOCKET_FALLBACK_X11) != 0)
    allow_x11 = !has_wayland;
  else
    allow_x11 = (context->sockets & FLATPAK_CONTEXT_SOCKET_X11) != 0;

  flatpak_run_add_x11_args (bwrap, allow_x11);

  if (context->sockets & FLATPAK_CONTEXT_SOCKET_SSH_AUTH)
    {
      flatpak_run_add_ssh_args (bwrap);
    }

  if (context->sockets & FLATPAK_CONTEXT_SOCKET_PULSEAUDIO)
    {
      g_debug ("Allowing pulseaudio access");
      flatpak_run_add_pulseaudio_args (bwrap);
    }

  if (context->sockets & FLATPAK_CONTEXT_SOCKET_PCSC)
    {
      flatpak_run_add_pcsc_args (bwrap);
    }

  if (context->sockets & FLATPAK_CONTEXT_SOCKET_CUPS)
    {
      flatpak_run_add_cups_args (bwrap);
    }

  flatpak_run_add_session_dbus_args (bwrap, proxy_arg_bwrap, context, flags, app_id);
  flatpak_run_add_system_dbus_args (bwrap, proxy_arg_bwrap, context, flags);
  flatpak_run_add_a11y_dbus_args (bwrap, proxy_arg_bwrap, context, flags);

  if (g_environ_getenv (bwrap->envp, "LD_LIBRARY_PATH") != NULL)
    {
      /* LD_LIBRARY_PATH is overridden for setuid helper, so pass it as cmdline arg */
      flatpak_bwrap_add_args (bwrap,
                              "--setenv", "LD_LIBRARY_PATH", g_environ_getenv (bwrap->envp, "LD_LIBRARY_PATH"),
                              NULL);
      flatpak_bwrap_unset_env (bwrap, "LD_LIBRARY_PATH");
    }

  if (g_environ_getenv (bwrap->envp, "TMPDIR") != NULL)
    {
      /* TMPDIR is overridden for setuid helper, so pass it as cmdline arg */
      flatpak_bwrap_add_args (bwrap,
                              "--setenv", "TMPDIR", g_environ_getenv (bwrap->envp, "TMPDIR"),
                              NULL);
      flatpak_bwrap_unset_env (bwrap, "TMPDIR");
    }

  /* Must run this before spawning the dbus proxy, to ensure it
     ends up in the app cgroup */
  if (!flatpak_run_in_transient_unit (app_id, &my_error))
    {
      /* We still run along even if we don't get a cgroup, as nothing
         really depends on it. Its just nice to have */
      g_debug ("Failed to run in transient scope: %s", my_error->message);
      g_clear_error (&my_error);
    }

  if (!flatpak_bwrap_is_empty (proxy_arg_bwrap) &&
      !start_dbus_proxy (bwrap, proxy_arg_bwrap, app_info_path, error))
    return FALSE;

  if (exports_out)
    *exports_out = g_steal_pointer (&exports);

  return TRUE;
}

typedef struct
{
  const char *env;
  const char *val;
} ExportData;

static const ExportData default_exports[] = {
  {"PATH", "/app/bin:/usr/bin"},
  /* We always want to unset LD_LIBRARY_PATH to avoid inheriting weird
   * dependencies from the host. But if not using ld.so.cache this is
   * later set. */
  {"LD_LIBRARY_PATH", NULL},
  {"XDG_CONFIG_DIRS", "/app/etc/xdg:/etc/xdg"},
  {"XDG_DATA_DIRS", "/app/share:/usr/share"},
  {"SHELL", "/bin/sh"},
  {"TMPDIR", NULL}, /* Unset TMPDIR as it may not exist in the sandbox */

  /* Some env vars are common enough and will affect the sandbox badly
     if set on the host. We clear these always. */
  {"PYTHONPATH", NULL},
  {"PERLLIB", NULL},
  {"PERL5LIB", NULL},
  {"XCURSOR_PATH", NULL},
};

static const ExportData no_ld_so_cache_exports[] = {
  {"LD_LIBRARY_PATH", "/app/lib"},
};

static const ExportData devel_exports[] = {
  {"ACLOCAL_PATH", "/app/share/aclocal"},
  {"C_INCLUDE_PATH", "/app/include"},
  {"CPLUS_INCLUDE_PATH", "/app/include"},
  {"LDFLAGS", "-L/app/lib "},
  {"PKG_CONFIG_PATH", "/app/lib/pkgconfig:/app/share/pkgconfig:/usr/lib/pkgconfig:/usr/share/pkgconfig"},
  {"LC_ALL", "en_US.utf8"},
};

static void
add_exports (GPtrArray        *env_array,
             const ExportData *exports,
             gsize             n_exports)
{
  int i;

  for (i = 0; i < n_exports; i++)
    {
      if (exports[i].val)
        g_ptr_array_add (env_array, g_strdup_printf ("%s=%s", exports[i].env, exports[i].val));
    }
}

char **
flatpak_run_get_minimal_env (gboolean devel, gboolean use_ld_so_cache)
{
  GPtrArray *env_array;
  static const char * const copy[] = {
    "PWD",
    "GDMSESSION",
    "XDG_CURRENT_DESKTOP",
    "XDG_SESSION_DESKTOP",
    "DESKTOP_SESSION",
    "EMAIL_ADDRESS",
    "HOME",
    "HOSTNAME",
    "LOGNAME",
    "REAL_NAME",
    "TERM",
    "USER",
    "USERNAME",
  };
  static const char * const copy_nodevel[] = {
    "LANG",
    "LANGUAGE",
    "LC_ALL",
    "LC_ADDRESS",
    "LC_COLLATE",
    "LC_CTYPE",
    "LC_IDENTIFICATION",
    "LC_MEASUREMENT",
    "LC_MESSAGES",
    "LC_MONETARY",
    "LC_NAME",
    "LC_NUMERIC",
    "LC_PAPER",
    "LC_TELEPHONE",
    "LC_TIME",
  };
  int i;

  env_array = g_ptr_array_new_with_free_func (g_free);

  add_exports (env_array, default_exports, G_N_ELEMENTS (default_exports));

  if (!use_ld_so_cache)
    add_exports (env_array, no_ld_so_cache_exports, G_N_ELEMENTS (no_ld_so_cache_exports));

  if (devel)
    add_exports (env_array, devel_exports, G_N_ELEMENTS (devel_exports));

  for (i = 0; i < G_N_ELEMENTS (copy); i++)
    {
      const char *current = g_getenv (copy[i]);
      if (current)
        g_ptr_array_add (env_array, g_strdup_printf ("%s=%s", copy[i], current));
    }

  if (!devel)
    {
      for (i = 0; i < G_N_ELEMENTS (copy_nodevel); i++)
        {
          const char *current = g_getenv (copy_nodevel[i]);
          if (current)
            g_ptr_array_add (env_array, g_strdup_printf ("%s=%s", copy_nodevel[i], current));
        }
    }

  g_ptr_array_add (env_array, NULL);
  return (char **) g_ptr_array_free (env_array, FALSE);
}

static char **
apply_exports (char            **envp,
               const ExportData *exports,
               gsize             n_exports)
{
  int i;

  for (i = 0; i < n_exports; i++)
    {
      const char *value = exports[i].val;

      if (value)
        envp = g_environ_setenv (envp, exports[i].env, value, TRUE);
      else
        envp = g_environ_unsetenv (envp, exports[i].env);
    }

  return envp;
}

void
flatpak_run_apply_env_default (FlatpakBwrap *bwrap, gboolean use_ld_so_cache)
{
  bwrap->envp = apply_exports (bwrap->envp, default_exports, G_N_ELEMENTS (default_exports));

  if (!use_ld_so_cache)
    bwrap->envp = apply_exports (bwrap->envp, no_ld_so_cache_exports, G_N_ELEMENTS (no_ld_so_cache_exports));
}

static void
flatpak_run_apply_env_prompt (FlatpakBwrap *bwrap, const char *app_id)
{
  /* A custom shell prompt. FLATPAK_ID is always set.
   * PS1 can be overwritten by runtime metadata or by --env overrides
   */
  flatpak_bwrap_set_env (bwrap, "FLATPAK_ID", app_id, TRUE);
  flatpak_bwrap_set_env (bwrap, "PS1", "[📦 $FLATPAK_ID \\W]\\$ ", FALSE);
}

#endif

void
flatpak_run_apply_env_appid (FlatpakBwrap *bwrap,
                             GFile        *app_dir)
{
  g_autoptr(GFile) app_dir_data = NULL;
  g_autoptr(GFile) app_dir_config = NULL;
  g_autoptr(GFile) app_dir_cache = NULL;

  app_dir_data = g_file_get_child (app_dir, "data");
  app_dir_config = g_file_get_child (app_dir, "config");
  app_dir_cache = g_file_get_child (app_dir, "cache");
  flatpak_bwrap_set_env (bwrap, "XDG_DATA_HOME", flatpak_file_get_path_cached (app_dir_data), TRUE);
  flatpak_bwrap_set_env (bwrap, "XDG_CONFIG_HOME", flatpak_file_get_path_cached (app_dir_config), TRUE);
  flatpak_bwrap_set_env (bwrap, "XDG_CACHE_HOME", flatpak_file_get_path_cached (app_dir_cache), TRUE);

  if (g_getenv ("XDG_DATA_HOME"))
    flatpak_bwrap_set_env (bwrap, "HOST_XDG_DATA_HOME", g_getenv ("XDG_DATA_HOME"), TRUE);
  if (g_getenv ("XDG_CONFIG_HOME"))
    flatpak_bwrap_set_env (bwrap, "HOST_XDG_CONFIG_HOME", g_getenv ("XDG_CONFIG_HOME"), TRUE);
  if (g_getenv ("XDG_CACHE_HOME"))
    flatpak_bwrap_set_env (bwrap, "HOST_XDG_CACHE_HOME", g_getenv ("XDG_CACHE_HOME"), TRUE);
}

#if 0

void
flatpak_run_apply_env_vars (FlatpakBwrap *bwrap, FlatpakContext *context)
{
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, context->env_vars);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      const char *var = key;
      const char *val = value;

      if (val && val[0] != 0)
        flatpak_bwrap_set_env (bwrap, var, val, TRUE);
      else
        flatpak_bwrap_unset_env (bwrap, var);
    }
}

#endif

GFile *
flatpak_get_data_dir (const char *app_id)
{
  g_autoptr(GFile) home = g_file_new_for_path (g_get_home_dir ());
  g_autoptr(GFile) var_app = g_file_resolve_relative_path (home, ".var/app");

  return g_file_get_child (var_app, app_id);
}

#if 0

gboolean
flatpak_ensure_data_dir (GFile        *app_id_dir,
                         GCancellable *cancellable,
                         GError      **error)
{
  g_autoptr(GFile) data_dir = g_file_get_child (app_id_dir, "data");
  g_autoptr(GFile) cache_dir = g_file_get_child (app_id_dir, "cache");
  g_autoptr(GFile) fontconfig_cache_dir = g_file_get_child (cache_dir, "fontconfig");
  g_autoptr(GFile) tmp_dir = g_file_get_child (cache_dir, "tmp");
  g_autoptr(GFile) config_dir = g_file_get_child (app_id_dir, "config");

  if (!flatpak_mkdir_p (data_dir, cancellable, error))
    return FALSE;

  if (!flatpak_mkdir_p (cache_dir, cancellable, error))
    return FALSE;

  if (!flatpak_mkdir_p (fontconfig_cache_dir, cancellable, error))
    return FALSE;

  if (!flatpak_mkdir_p (tmp_dir, cancellable, error))
    return FALSE;

  if (!flatpak_mkdir_p (config_dir, cancellable, error))
    return FALSE;

  return TRUE;
}

#endif
