/*
 * Copyright © 2021 Collabora Ltd.
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

#include "steam-runtime-tools/container.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <gio/gio.h>

#include "steam-runtime-tools/container-internal.h"
#include "steam-runtime-tools/enums.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

/**
 * SECTION:container-info
 * @title: Container info
 * @short_description: Information about the eventual container that is currently in use
 * @include: steam-runtime-tools/steam-runtime-tools.h
 */

struct _SrtContainerInfo
{
  /*< private >*/
  GObject parent;
  gchar *host_directory;
  SrtContainerType type;
};

struct _SrtContainerInfoClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum {
  PROP_0,
  PROP_HOST_DIRECTORY,
  PROP_TYPE,
  N_PROPERTIES
};

G_DEFINE_TYPE (SrtContainerInfo, srt_container_info, G_TYPE_OBJECT)

static void
srt_container_info_init (SrtContainerInfo *self)
{
}

static void
srt_container_info_get_property (GObject *object,
                                 guint prop_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
  SrtContainerInfo *self = SRT_CONTAINER_INFO (object);

  switch (prop_id)
    {
      case PROP_HOST_DIRECTORY:
        g_value_set_string (value, self->host_directory);
        break;

      case PROP_TYPE:
        g_value_set_enum (value, self->type);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_container_info_set_property (GObject *object,
                                 guint prop_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
  SrtContainerInfo *self = SRT_CONTAINER_INFO (object);

  switch (prop_id)
    {
      case PROP_HOST_DIRECTORY:
        /* Construct-only */
        g_return_if_fail (self->host_directory == NULL);
        self->host_directory = g_value_dup_string (value);
        break;

      case PROP_TYPE:
        /* Construct-only */
        g_return_if_fail (self->type == 0);
        self->type = g_value_get_enum (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_container_info_finalize (GObject *object)
{
  SrtContainerInfo *self = SRT_CONTAINER_INFO (object);

  g_free (self->host_directory);

  G_OBJECT_CLASS (srt_container_info_parent_class)->finalize (object);
}

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void
srt_container_info_class_init (SrtContainerInfoClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_container_info_get_property;
  object_class->set_property = srt_container_info_set_property;
  object_class->finalize = srt_container_info_finalize;

  properties[PROP_HOST_DIRECTORY] =
    g_param_spec_string ("host-directory", "Host directory",
                         "Absolute path where important files from the host "
                         "system can be found",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  properties[PROP_TYPE] =
    g_param_spec_enum ("type", "Container type",
                       "Which container type is currently in use",
                       SRT_TYPE_CONTAINER_TYPE, SRT_CONTAINER_TYPE_UNKNOWN,
                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

typedef struct
{
  SrtContainerType type;
  const char *name;
} ContainerTypeName;

static const ContainerTypeName container_types[] =
{
  { SRT_CONTAINER_TYPE_DOCKER, "docker" },
  { SRT_CONTAINER_TYPE_FLATPAK, "flatpak" },
  { SRT_CONTAINER_TYPE_PODMAN, "podman" },
  { SRT_CONTAINER_TYPE_PRESSURE_VESSEL, "pressure-vessel" },
};

static SrtContainerType
container_type_from_name (const char *name)
{
  gsize i;

  for (i = 0; i < G_N_ELEMENTS (container_types); i++)
    {
      const ContainerTypeName *entry = &container_types[i];

      if (strcmp (entry->name, name) == 0)
        return entry->type;
    }

  return SRT_CONTAINER_TYPE_UNKNOWN;
}

/**
 * _srt_check_container:
 * @sysroot_fd: Sysroot file descriptor
 * @sysroot: (not nullable) (type filename): Path to the sysroot
 *
 * Gather and return information about the container that is currently in use.
 *
 * Returns: (transfer full): A new #SrtContainerInfo object.
 *  Free with g_object_unref().
 */
SrtContainerInfo *
_srt_check_container (int sysroot_fd,
                      const gchar *sysroot)
{
  g_autofree gchar *contents = NULL;
  g_autofree gchar *run_host_path = NULL;
  g_autofree gchar *host_directory = NULL;
  SrtContainerType type = SRT_CONTAINER_TYPE_UNKNOWN;
  glnx_autofd int run_host_fd = -1;

  g_return_val_if_fail (sysroot != NULL, NULL);

  if (sysroot_fd < 0)
    {
      g_debug ("Cannot find container info: previously failed to open "
               "sysroot %s", sysroot);
      goto out;
    }

  g_debug ("Finding container info in sysroot %s...", sysroot);

  run_host_fd = _srt_resolve_in_sysroot (sysroot_fd, "/run/host",
                                         SRT_RESOLVE_FLAGS_DIRECTORY,
                                         &run_host_path, NULL);

  if (run_host_path != NULL)
    host_directory = g_build_filename (sysroot, run_host_path, NULL);

  if (_srt_file_get_contents_in_sysroot (sysroot_fd,
                                         "/run/host/container-manager",
                                         &contents, NULL, NULL))
    {
      g_strchomp (contents);
      type = container_type_from_name (contents);
      g_debug ("Type %d based on /run/host/container-manager", type);
      goto out;
    }

  if (_srt_file_get_contents_in_sysroot (sysroot_fd,
                                         "/run/systemd/container",
                                         &contents, NULL, NULL))
    {
      g_strchomp (contents);
      type = container_type_from_name (contents);
      g_debug ("Type %d based on /run/systemd/container", type);
      goto out;
    }

  if (_srt_file_test_in_sysroot (sysroot, sysroot_fd, "/.flatpak-info",
                                 G_FILE_TEST_IS_REGULAR))
    {
      type = SRT_CONTAINER_TYPE_FLATPAK;
      g_debug ("Flatpak based on /.flatpak-info");
      goto out;
    }

  if (_srt_file_test_in_sysroot (sysroot, sysroot_fd, "/run/pressure-vessel",
                                 G_FILE_TEST_IS_DIR))
    {
      type = SRT_CONTAINER_TYPE_PRESSURE_VESSEL;
      g_debug ("pressure-vessel based on /run/pressure-vessel");
      goto out;
    }

  if (_srt_file_test_in_sysroot (sysroot, sysroot_fd, "/.dockerenv",
                                 G_FILE_TEST_EXISTS))
    {
      type = SRT_CONTAINER_TYPE_DOCKER;
      g_debug ("Docker based on /.dockerenv");
      goto out;
    }

  if (_srt_file_test_in_sysroot (sysroot, sysroot_fd, "/run/.containerenv",
                                 G_FILE_TEST_EXISTS))
    {
      type = SRT_CONTAINER_TYPE_PODMAN;
      g_debug ("Podman based on /run/.containerenv");
      goto out;
    }

  if (_srt_file_get_contents_in_sysroot (sysroot_fd, "/proc/1/cgroup",
                                         &contents, NULL, NULL))
    {
      if (strstr (contents, "/docker/") != NULL)
        type = SRT_CONTAINER_TYPE_DOCKER;

      if (type != SRT_CONTAINER_TYPE_UNKNOWN)
        {
          g_debug ("Type %d based on /proc/1/cgroup", type);
          goto out;
        }

      g_clear_pointer (&contents, g_free);
    }

  if (run_host_fd >= 0)
    {
      g_debug ("Unknown container technology based on /run/host");
      type = SRT_CONTAINER_TYPE_UNKNOWN;
      goto out;
    }

  /* We haven't found any particular evidence of being in a container */
  g_debug ("Probably not a container");
  type = SRT_CONTAINER_TYPE_NONE;

out:
  return _srt_container_info_new (type, host_directory);
}

/**
 * srt_container_info_get_container_type:
 * @self: A SrtContainerInfo object
 *
 * If the program appears to be running in a container, return what sort
 * of container it is.
 *
 * Implementation of srt_system_info_get_container_type().
 *
 * Returns: A recognised container type, or %SRT_CONTAINER_TYPE_NONE
 *  if a container cannot be detected, or %SRT_CONTAINER_TYPE_UNKNOWN
 *  if unsure.
 */
SrtContainerType
srt_container_info_get_container_type (SrtContainerInfo *self)
{
  g_return_val_if_fail (SRT_IS_CONTAINER_INFO (self), SRT_CONTAINER_TYPE_UNKNOWN);
  return self->type;
}

/**
 * srt_container_info_get_container_host_directory:
 * @self: A SrtContainerInfo object
 *
 * If the program appears to be running in a container, return the
 * directory where host files can be found. For example, if this function
 * returns `/run/host`, it might be possible to load the host system's
 * `/usr/lib/os-release` by reading `/run/host/usr/lib/os-release`.
 *
 * The returned directory is usually not complete. For example,
 * in a Flatpak app, `/run/host` will sometimes contain the host system's
 * `/etc` and `/usr`, but only if suitable permissions flags are set.
 *
 * Implementation of srt_system_info_dup_container_host_directory().
 *
 * Returns: (type filename) (nullable): A path from which at least some
 *  host-system files can be loaded, typically `/run/host`, or %NULL if
 *  unknown or unavailable.
 */
const gchar *
srt_container_info_get_container_host_directory (SrtContainerInfo *self)
{
  g_return_val_if_fail (SRT_IS_CONTAINER_INFO (self), NULL);
  return self->host_directory;
}

/**
 * _srt_system_info_get_container_info_from_report:
 * @json_obj: (not nullable): A JSON Object used to search for "container"
 *  property
 * @host_path: (not nullable): Used to return the host path
 *
 * If the provided @json_obj doesn't have a "container" member,
 * %SRT_CONTAINER_TYPE_UNKNOWN will be returned.
 * If @json_obj has some elements that we can't parse, the returned
 * #SrtContainerType will be set to %SRT_CONTAINER_TYPE_UNKNOWN.
 *
 * Returns: The found container type
 */
SrtContainerInfo *
_srt_container_info_get_from_report (JsonObject *json_obj)
{
  JsonObject *json_sub_obj;
  JsonObject *json_host_obj = NULL;
  const gchar *type_string = NULL;
  const gchar *host_path = NULL;
  SrtContainerType type = SRT_CONTAINER_TYPE_UNKNOWN;

  g_return_val_if_fail (json_obj != NULL, NULL);

  if (json_object_has_member (json_obj, "container"))
    {
      json_sub_obj = json_object_get_object_member (json_obj, "container");

      if (json_sub_obj == NULL)
        goto out;

      if (json_object_has_member (json_sub_obj, "type"))
        {
          type_string = json_object_get_string_member (json_sub_obj, "type");
          G_STATIC_ASSERT (sizeof (SrtContainerType) == sizeof (gint));
          if (!srt_enum_from_nick (SRT_TYPE_CONTAINER_TYPE, type_string, (gint *) &type, NULL))
            {
              g_debug ("The parsed container type '%s' is not a known element", type_string);
              type = SRT_CONTAINER_TYPE_UNKNOWN;
            }
        }

      if (json_object_has_member (json_sub_obj, "host"))
        {
          json_host_obj = json_object_get_object_member (json_sub_obj, "host");
          host_path = json_object_get_string_member_with_default (json_host_obj, "path",
                                                                  NULL);
        }
    }

out:
  return _srt_container_info_new (type,
                                  host_path);
}
