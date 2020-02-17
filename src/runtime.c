/*
 * Copyright © 2020 Collabora Ltd.
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

#include "src/runtime.h"

#include <gio/gio.h>

#include "bwrap.h"
#include "bwrap-lock.h"

/*
 * PvRuntime:
 *
 * Object representing a runtime to be used as the /usr for a game.
 */

struct _PvRuntime
{
  GObject parent;

  gchar *bubblewrap;
  gchar *source_files;
  gchar *tools_dir;
  PvBwrapLock *runtime_lock;
};

struct _PvRuntimeClass
{
  GObjectClass parent;
};

enum {
  PROP_0,
  PROP_BUBBLEWRAP,
  PROP_SOURCE_FILES,
  PROP_TOOLS_DIRECTORY,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void pv_runtime_initable_iface_init (GInitableIface *iface,
                                            gpointer unused);

G_DEFINE_TYPE_WITH_CODE (PvRuntime, pv_runtime, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                pv_runtime_initable_iface_init))

static void
pv_runtime_init (PvRuntime *self)
{
}

static void
pv_runtime_get_property (GObject *object,
                         guint prop_id,
                         GValue *value,
                         GParamSpec *pspec)
{
  PvRuntime *self = PV_RUNTIME (object);

  switch (prop_id)
    {
      case PROP_BUBBLEWRAP:
        g_value_set_string (value, self->bubblewrap);
        break;

      case PROP_SOURCE_FILES:
        g_value_set_string (value, self->source_files);
        break;

      case PROP_TOOLS_DIRECTORY:
        g_value_set_string (value, self->tools_dir);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
pv_runtime_set_property (GObject *object,
                         guint prop_id,
                         const GValue *value,
                         GParamSpec *pspec)
{
  PvRuntime *self = PV_RUNTIME (object);

  switch (prop_id)
    {
      case PROP_BUBBLEWRAP:
        /* Construct-only */
        g_return_if_fail (self->bubblewrap == NULL);
        self->bubblewrap = g_value_dup_string (value);
        break;

      case PROP_SOURCE_FILES:
        /* Construct-only */
        g_return_if_fail (self->source_files == NULL);
        self->source_files = g_value_dup_string (value);
        break;

      case PROP_TOOLS_DIRECTORY:
        /* Construct-only */
        g_return_if_fail (self->tools_dir == NULL);
        self->tools_dir = g_value_dup_string (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
pv_runtime_constructed (GObject *object)
{
  PvRuntime *self = PV_RUNTIME (object);

  G_OBJECT_CLASS (pv_runtime_parent_class)->constructed (object);

  g_return_if_fail (self->bubblewrap != NULL);
  g_return_if_fail (self->source_files != NULL);
  g_return_if_fail (self->tools_dir != NULL);
}

static gboolean
pv_runtime_initable_init (GInitable *initable,
                          GCancellable *cancellable G_GNUC_UNUSED,
                          GError **error)
{
  PvRuntime *self = PV_RUNTIME (initable);
  g_autofree gchar *files_ref = NULL;

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!g_file_test (self->bubblewrap, G_FILE_TEST_IS_EXECUTABLE))
    {
      return glnx_throw (error, "\"%s\" is not executable",
                         self->bubblewrap);
    }

  if (!g_file_test (self->source_files, G_FILE_TEST_IS_DIR))
    {
      return glnx_throw (error, "\"%s\" is not a directory",
                         self->source_files);
    }

  if (!g_file_test (self->tools_dir, G_FILE_TEST_IS_DIR))
    {
      return glnx_throw (error, "\"%s\" is not a directory",
                         self->tools_dir);
    }

  /* Take a lock on the runtime until we're finished with setup,
   * to make sure it doesn't get deleted. */
  files_ref = g_build_filename (self->source_files, ".ref", NULL);
  self->runtime_lock = pv_bwrap_lock_new (files_ref,
                                          PV_BWRAP_LOCK_FLAGS_CREATE,
                                          error);

  /* If the runtime is being deleted, ... don't use it, I suppose? */
  if (self->runtime_lock == NULL)
    return FALSE;

  return TRUE;
}

static void
pv_runtime_finalize (GObject *object)
{
  PvRuntime *self = PV_RUNTIME (object);

  g_free (self->bubblewrap);
  g_free (self->source_files);
  g_free (self->tools_dir);
  pv_bwrap_lock_free (self->runtime_lock);

  G_OBJECT_CLASS (pv_runtime_parent_class)->finalize (object);
}

static void
pv_runtime_class_init (PvRuntimeClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = pv_runtime_get_property;
  object_class->set_property = pv_runtime_set_property;
  object_class->constructed = pv_runtime_constructed;
  object_class->finalize = pv_runtime_finalize;

  properties[PROP_BUBBLEWRAP] =
    g_param_spec_string ("bubblewrap", "Bubblewrap",
                         "Bubblewrap executable",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_SOURCE_FILES] =
    g_param_spec_string ("source-files", "Source files",
                         ("Path to read-only runtime files (merged-/usr "
                          "or sysroot) on host system"),
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_TOOLS_DIRECTORY] =
    g_param_spec_string ("tools-directory", "Tools directory",
                         "Path to pressure-vessel/bin on host system",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

PvRuntime *
pv_runtime_new (const char *source_files,
                const char *bubblewrap,
                const char *tools_dir,
                GError **error)
{
  g_return_val_if_fail (source_files != NULL, NULL);
  g_return_val_if_fail (bubblewrap != NULL, NULL);
  g_return_val_if_fail (tools_dir != NULL, NULL);

  return g_initable_new (PV_TYPE_RUNTIME,
                         NULL,
                         error,
                         "bubblewrap", bubblewrap,
                         "source-files", source_files,
                         "tools-directory", tools_dir,
                         NULL);
}

/* If we are using a runtime, pass the lock fd to the executed process,
 * and make it act as a subreaper for the game itself.
 *
 * If we were using --unshare-pid then we could use bwrap --sync-fd
 * and rely on bubblewrap's init process for this, but we currently
 * can't do that without breaking gameoverlayrender.so's assumptions. */
void
pv_runtime_append_lock_adverb (PvRuntime *self,
                               FlatpakBwrap *bwrap)
{
  g_return_if_fail (PV_IS_RUNTIME (self));
  g_return_if_fail (!pv_bwrap_was_finished (bwrap));

  flatpak_bwrap_add_args (bwrap,
                          "/run/pressure-vessel/bin/pressure-vessel-with-lock",
                          "--subreaper",
                          NULL);

  if (pv_bwrap_lock_is_ofd (self->runtime_lock))
    {
      int fd = pv_bwrap_lock_steal_fd (self->runtime_lock);
      g_autofree gchar *fd_str = NULL;

      g_debug ("Passing lock fd %d down to with-lock", fd);
      flatpak_bwrap_add_fd (bwrap, fd);
      fd_str = g_strdup_printf ("%d", fd);
      flatpak_bwrap_add_args (bwrap,
                              "--fd", fd_str,
                              NULL);
    }
  else
    {
      /*
       * We were unable to take out an open file descriptor lock,
       * so it will be released on fork(). Tell the with-lock process
       * to take out its own compatible lock instead. There will be
       * a short window during which we have lost our lock but the
       * with-lock process has not taken its lock - that's unavoidable
       * if we want to use exec() to replace ourselves with the
       * container.
       *
       * pv_bwrap_bind_usr() arranges for /.ref to either be a
       * symbolic link to /usr/.ref which is the runtime_lock
       * (if opt_runtime is a merged /usr), or the runtime_lock
       * itself (otherwise).
       */
      g_debug ("Telling process in container to lock /.ref");
      flatpak_bwrap_add_args (bwrap,
                              "--lock-file", "/.ref",
                              NULL);
    }

  flatpak_bwrap_add_args (bwrap,
                          "--",
                          NULL);
}

static void
pv_runtime_initable_iface_init (GInitableIface *iface,
                                gpointer unused G_GNUC_UNUSED)
{
  iface->init = pv_runtime_initable_init;
}
