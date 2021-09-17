/*
 * Copyright © 2020-2021 Collabora Ltd.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "graphics-provider.h"

#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/system-info-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include "utils.h"

enum {
  PROP_0,
  PROP_PATH_IN_CURRENT_NS,
  PROP_PATH_IN_CONTAINER_NS,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void pv_graphics_provider_initable_iface_init (GInitableIface *iface,
                                                      gpointer unused);

G_DEFINE_TYPE_WITH_CODE (PvGraphicsProvider, pv_graphics_provider, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                pv_graphics_provider_initable_iface_init))

static void
pv_graphics_provider_init (PvGraphicsProvider *self)
{
  self->fd = -1;
}

static void
pv_graphics_provider_get_property (GObject *object,
                                   guint prop_id,
                                   GValue *value,
                                   GParamSpec *pspec)
{
  PvGraphicsProvider *self = PV_GRAPHICS_PROVIDER (object);

  switch (prop_id)
    {
      case PROP_PATH_IN_CURRENT_NS:
        g_value_set_string (value, self->path_in_current_ns);
        break;

      case PROP_PATH_IN_CONTAINER_NS:
        g_value_set_string (value, self->path_in_container_ns);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
pv_graphics_provider_set_property (GObject *object,
                                   guint prop_id,
                                   const GValue *value,
                                   GParamSpec *pspec)
{
  PvGraphicsProvider *self = PV_GRAPHICS_PROVIDER (object);

  switch (prop_id)
    {
      case PROP_PATH_IN_CURRENT_NS:
        /* Construct-only */
        g_return_if_fail (self->path_in_current_ns == NULL);
        self->path_in_current_ns = g_value_dup_string (value);
        break;

      case PROP_PATH_IN_CONTAINER_NS:
        /* Construct-only */
        g_return_if_fail (self->path_in_container_ns == NULL);
        self->path_in_container_ns = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
pv_graphics_provider_constructed (GObject *object)
{
  PvGraphicsProvider *self = PV_GRAPHICS_PROVIDER (object);

  G_OBJECT_CLASS (pv_graphics_provider_parent_class)->constructed (object);

  g_return_if_fail (self->path_in_current_ns != NULL);
  g_return_if_fail (self->path_in_container_ns != NULL);
}

static gboolean
pv_graphics_provider_initable_init (GInitable *initable,
                                    GCancellable *cancellable G_GNUC_UNUSED,
                                    GError **error)
{
  PvGraphicsProvider *self = PV_GRAPHICS_PROVIDER (initable);

  g_return_val_if_fail (PV_IS_GRAPHICS_PROVIDER (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!glnx_opendirat (-1, self->path_in_current_ns, FALSE,
                       &self->fd, error))
    return FALSE;

  /* Path that, when resolved in the host namespace, points to us */
  self->path_in_host_ns = pv_current_namespace_path_to_host_path (self->path_in_current_ns);

  return TRUE;
}

static void
pv_graphics_provider_finalize (GObject *object)
{
  PvGraphicsProvider *self = PV_GRAPHICS_PROVIDER (object);

  glnx_close_fd (&self->fd);
  g_free (self->path_in_current_ns);
  g_free (self->path_in_host_ns);
  g_free (self->path_in_container_ns);

  G_OBJECT_CLASS (pv_graphics_provider_parent_class)->finalize (object);
}

static void
pv_graphics_provider_class_init (PvGraphicsProviderClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = pv_graphics_provider_get_property;
  object_class->set_property = pv_graphics_provider_set_property;
  object_class->constructed = pv_graphics_provider_constructed;
  object_class->finalize = pv_graphics_provider_finalize;

  properties[PROP_PATH_IN_CURRENT_NS] =
    g_param_spec_string ("path-in-current-ns", "Path in current namespace",
                         ("Path to the graphics provider in the current "
                          "namespace, typically /"),
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_PATH_IN_CONTAINER_NS] =
    g_param_spec_string ("path-in-container-ns", "Path in container namespace",
                         ("Path to the graphics provider in the container "
                          "namespace, typically /run/host"),
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

gchar *
pv_graphics_provider_search_in_path_and_bin (PvGraphicsProvider *self,
                                             const gchar *program_name)
{
  const gchar * const common_bin_dirs[] =
  {
    "/usr/bin",
    "/bin",
    "/usr/sbin",
    "/sbin",
    NULL
  };

  g_return_val_if_fail (PV_IS_GRAPHICS_PROVIDER (self), NULL);
  g_return_val_if_fail (program_name != NULL, NULL);

  if (g_strcmp0 (self->path_in_current_ns, "/") == 0)
    {
      gchar *found_path = g_find_program_in_path (program_name);
      if (found_path != NULL)
        return found_path;
    }

  for (gsize i = 0; i < G_N_ELEMENTS (common_bin_dirs) - 1; i++)
    {
      g_autofree gchar *test_path = g_build_filename (common_bin_dirs[i],
                                                      program_name,
                                                      NULL);
      if (_srt_file_test_in_sysroot (self->path_in_current_ns, self->fd,
                                     test_path, G_FILE_TEST_IS_EXECUTABLE))
        return g_steal_pointer (&test_path);
    }

  return NULL;
}

PvGraphicsProvider *
pv_graphics_provider_new (const char *path_in_current_ns,
                          const char *path_in_container_ns,
                          GError **error)
{
  g_return_val_if_fail (path_in_current_ns != NULL, NULL);
  g_return_val_if_fail (path_in_container_ns != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  return g_initable_new (PV_TYPE_GRAPHICS_PROVIDER,
                         NULL,
                         error,
                         "path-in-container-ns", path_in_container_ns,
                         "path-in-current-ns", path_in_current_ns,
                         NULL);
}

/*
 * Create a new SrtSystemInfo, suitable for use in a separate thread.
 */
SrtSystemInfo *
pv_graphics_provider_create_system_info (PvGraphicsProvider *self)
{
  g_autoptr(SrtSystemInfo) system_info = NULL;

  g_return_val_if_fail (PV_IS_GRAPHICS_PROVIDER (self), NULL);

  system_info = srt_system_info_new (NULL);
  srt_system_info_set_sysroot (system_info, self->path_in_current_ns);
  _srt_system_info_set_check_flags (system_info,
                                    (SRT_CHECK_FLAGS_SKIP_SLOW_CHECKS
                                     | SRT_CHECK_FLAGS_SKIP_EXTRAS));
  return g_steal_pointer (&system_info);
}

static void
pv_graphics_provider_initable_iface_init (GInitableIface *iface,
                                          gpointer unused G_GNUC_UNUSED)
{
  iface->init = pv_graphics_provider_initable_init;
}
