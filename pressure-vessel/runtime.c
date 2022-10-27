/*
 * Copyright © 2020-2022 Collabora Ltd.
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

#include "runtime.h"

#include <sysexits.h>

#include <gio/gio.h>

/* Include these before steam-runtime-tools.h so that their backport of
 * G_DEFINE_AUTOPTR_CLEANUP_FUNC will be visible to it */
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/json-glib-backports-internal.h"
#include "libglnx.h"

#include <steam-runtime-tools/steam-runtime-tools.h>

#include "steam-runtime-tools/graphics-internal.h"
#include "steam-runtime-tools/profiling-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/system-info-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include "bwrap.h"
#include "bwrap-lock.h"
#include "enumtypes.h"
#include "exports.h"
#include "flatpak-run-private.h"
#include "mtree.h"
#include "supported-architectures.h"
#include "tree-copy.h"
#include "utils.h"

typedef struct
{
  GCancellable *cancellable;
  GThread *thread;
  SrtSystemInfo *system_info;
} EnumerationThread;

/*
 * PvRuntime:
 *
 * Object representing a runtime to be used as the /usr for a game.
 */

struct _PvRuntime
{
  GObject parent;

  gchar *bubblewrap;
  gchar *source;
  gchar *id;
  gchar *deployment;
  gchar *source_files;          /* either deployment or that + "/files" */
  const gchar *pv_prefix;
  const gchar *helpers_path;
  PvBwrapLock *runtime_lock;
  GStrv original_environ;

  gchar *libcapsule_knowledge;
  gchar *runtime_abi_json;
  gchar *variable_dir;
  gchar *mutable_sysroot;
  gchar *tmpdir;
  gchar *overrides;
  const gchar *overrides_in_container;
  gchar *container_access;
  FlatpakBwrap *container_access_adverb;
  const gchar *runtime_files;   /* either source_files or mutable_sysroot */
  gchar *runtime_usr;           /* either runtime_files or that + "/usr" */
  gchar *runtime_app;           /* runtime_files + "/app" */
  gchar *runtime_files_on_host;
  const gchar *adverb_in_container;
  PvGraphicsProvider *provider;
  PvGraphicsProvider *interpreter_host_provider;
  const gchar *host_in_current_namespace;
  EnumerationThread indep_thread;
  EnumerationThread host_thread;
  EnumerationThread *arch_host_threads;
  EnumerationThread *arch_threads;
  SrtDirentCompareFunc arbitrary_dirent_order;
  GCompareFunc arbitrary_str_order;

  PvRuntimeFlags flags;
  int variable_dir_fd;
  int mutable_sysroot_fd;
  int host_fd;
  int root_fd;
  int runtime_files_fd;
  gboolean any_libc_from_provider;
  gboolean all_libc_from_provider;
  gboolean runtime_is_just_usr;
  gboolean is_steamrt;
  gboolean is_scout;
  gboolean is_flatpak_env;
};

struct _PvRuntimeClass
{
  GObjectClass parent;
};

enum {
  PROP_0,
  PROP_BUBBLEWRAP,
  PROP_GRAPHICS_PROVIDER,
  PROP_INTERPRETER_HOST_PROVIDER,
  PROP_SOURCE,
  PROP_ORIGINAL_ENVIRON,
  PROP_FLAGS,
  PROP_ID,
  PROP_VARIABLE_DIR,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void pv_runtime_initable_iface_init (GInitableIface *iface,
                                            gpointer unused);

G_DEFINE_TYPE_WITH_CODE (PvRuntime, pv_runtime, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                pv_runtime_initable_iface_init))

static gboolean pv_runtime_symlinkat (const gchar *target,
                                      int destination_dirfd,
                                      const gchar *destination,
                                      GError **error);

/*
 * Return whether @path is expected to be a mutable directory in
 * the container.
 */
static gboolean
path_mutable_in_container_namespace (const char *path)
{
  static const char * const no[] =
  {
    "run/gfx",
    "run/host",
  };
  static const char * const yes[] =
  {
    "etc",
    "overrides",
    "run",
    "tmp",
    "var",
    "usr",
  };
  gsize i;

  while (path[0] == '/')
    path++;

  for (i = 0; i < G_N_ELEMENTS (no); i++)
    {
      if (_srt_get_path_after (path, no[i]) != NULL)
        return FALSE;
    }

  for (i = 0; i < G_N_ELEMENTS (yes); i++)
    {
      if (_srt_get_path_after (path, yes[i]) != NULL)
        return TRUE;
    }

  return FALSE;
}

/*
 * Return whether @path is likely to be visible as-is in the container.
 */
static gboolean
path_visible_in_container_namespace (PvRuntimeFlags flags,
                                     const char *path)
{
  while (path[0] == '/')
    path++;

  /* This is mounted in wrap.c as a special case: NixOS uses a lot of
   * absolute paths in /nix/store which we need to make available. */
  if (!(flags & PV_RUNTIME_FLAGS_FLATPAK_SUBSANDBOX)
      && g_str_has_prefix (path, "nix")
      && (path[3] == '\0' || path[3] == '/'))
    return TRUE;

  /* Similar, but for Guix */
  if (!(flags & PV_RUNTIME_FLAGS_FLATPAK_SUBSANDBOX)
      && g_str_has_prefix (path, "gnu/store")
      && (path[9] == '\0' || path[9] == '/'))
    return TRUE;

  return FALSE;
}

/*
 * Return whether @path is likely to be visible in the provider mount point
 * (e.g. /run/host).
 * This needs to be kept approximately in sync with pv_bwrap_bind_usr()
 * and Flatpak's --filesystem=host-os and --filesystem=host-etc special
 * keywords.
 */
static gboolean
path_visible_in_provider_namespace (PvRuntimeFlags flags,
                                    const char *path)
{
  while (path[0] == '/')
    path++;

  /* In a Flatpak subsandbox, the provider is /run/parent, and
   * /run/parent/app in the subsandbox has the same content as /app
   * in Steam. */
  if ((flags & PV_RUNTIME_FLAGS_FLATPAK_SUBSANDBOX)
      && g_str_has_prefix (path, "app")
      && (path[3] == '\0' || path[3] == '/'))
    return TRUE;

  if (g_str_has_prefix (path, "usr") &&
      (path[3] == '\0' || path[3] == '/'))
    return TRUE;

  if (g_str_has_prefix (path, "lib"))
    return TRUE;

  if (g_str_has_prefix (path, "bin") &&
      (path[3] == '\0' || path[3] == '/'))
    return TRUE;

  if (g_str_has_prefix (path, "sbin") ||
      (path[4] == '\0' || path[4] == '/'))
    return TRUE;

  /* If the provider is /run/host, flatpak_exports_add_host_etc_expose()
   * in wrap.c is responsible for mounting /etc on /run/host/etc.
   *
   * In a Flatpak subsandbox environment, flatpak_run_app() makes
   * /run/parent/etc a symlink to /run/parent/usr/etc.
   *
   * Otherwise, bind_runtime_base() is responsible for mounting the provider's
   * /etc on /run/gfx/etc. */
  if (g_str_has_prefix (path, "etc")
      && (path[3] == '\0' || path[3] == '/'))
    return TRUE;

  return FALSE;
}

typedef enum
{
  MAKE_SYMLINK_FLAGS_INTERPRETER_ROOT = (1 << 0),
  MAKE_SYMLINK_FLAGS_NONE = 0
} MakeSymlinkFlags;

/*
 * pv_runtime_make_symlink_in_container:
 * @self: the runtime
 * @bwrap: the arguments for bubblewrap
 * @target: target of the symlink
 * @path: absolute or root-relative path in the container
 * @error: you know how this works
 *
 * Try to make @path a symlink to @target in the container, by whichever
 * mechanism seems best: either editing the mutable sysroot in-place,
 * or telling bubblewrap to create a symlink in a transient directory
 * like /etc, /run, /var.
 *
 * Returns: %TRUE on success
 */
static gboolean
pv_runtime_make_symlink_in_container (PvRuntime *self,
                                      FlatpakBwrap *bwrap,
                                      const char *target,
                                      const char *path,
                                      MakeSymlinkFlags flags,
                                      GError **error)
{
  g_autofree char *alloc_dest = NULL;
  const char *dest = path;

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (target != NULL, FALSE);
  g_return_val_if_fail (path != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if ((flags & MAKE_SYMLINK_FLAGS_INTERPRETER_ROOT)
      && (self->flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT))
    dest = alloc_dest = g_build_filename (PV_RUNTIME_PATH_INTERPRETER_ROOT,
                                          path, NULL);

  if (_srt_get_path_after (path, "usr") != NULL)
    {
      if (self->mutable_sysroot_fd >= 0)
        {
          g_autofree gchar *parent = g_path_get_dirname (path);
          const char *base = glnx_basename (path);
          glnx_autofd int parent_fd = -1;

          parent_fd = _srt_resolve_in_sysroot (self->mutable_sysroot_fd,
                                               parent,
                                               SRT_RESOLVE_FLAGS_MKDIR_P,
                                               NULL, error);

          if (parent_fd < 0)
            goto error;

          if (!pv_runtime_symlinkat (target, parent_fd, base, error))
            goto error;

          return TRUE;
        }
      else
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY,
                       "Cannot modify /usr when not copying runtime");
          goto error;
        }
    }

  if (bwrap != NULL
      && path_mutable_in_container_namespace (path))
    {
      /* Note that "--symlink foo bar" is equivalent to "--symlink foo /bar":
       * both end up creating the symlink at /newroot/bar */
      flatpak_bwrap_add_args (bwrap,
                              "--symlink",
                              target,
                              dest,
                              NULL);
      return TRUE;
    }

  g_set_error (error, G_IO_ERROR, G_IO_ERROR_READ_ONLY,
               "Not modifiable in current configuration");
error:
  g_prefix_error (error, "Not making \"%s\" a symlink to \"%s\": ",
                  dest, target);
  return FALSE;
}

typedef struct
{
  gsize multiarch_index;
  const PvMultiarchDetails *details;
  gchar *aliases_in_current_namespace;
  gchar *capsule_capture_libs_basename;
  gchar *capsule_capture_libs;
  gchar *libdir_in_current_namespace;
  gchar *libdir_in_container;
  gchar *ld_so;
} RuntimeArchitecture;

static gboolean
runtime_architecture_init (RuntimeArchitecture *self,
                           PvRuntime *runtime)
{
  const gchar *argv[] = { NULL, "--print-ld.so", NULL };

  g_return_val_if_fail (self->multiarch_index < PV_N_SUPPORTED_ARCHITECTURES,
                        FALSE);
  g_return_val_if_fail (self->details == NULL, FALSE);

  self->details = &pv_multiarch_details[self->multiarch_index];
  g_return_val_if_fail (self->details != NULL, FALSE);
  g_return_val_if_fail (self->details->tuple != NULL, FALSE);
  g_return_val_if_fail (g_strcmp0 (pv_multiarch_tuples[self->multiarch_index],
                                   self->details->tuple) == 0, FALSE);

  self->capsule_capture_libs_basename = g_strdup_printf ("%s-capsule-capture-libs",
                                                         self->details->tuple);
  self->capsule_capture_libs = g_build_filename (runtime->helpers_path,
                                                 self->capsule_capture_libs_basename,
                                                 NULL);
  self->libdir_in_current_namespace = g_build_filename (runtime->overrides, "lib",
                                                        self->details->tuple, NULL);
  self->libdir_in_container = g_build_filename (runtime->overrides_in_container,
                                                "lib", self->details->tuple, NULL);

  self->aliases_in_current_namespace = g_build_filename (self->libdir_in_current_namespace,
                                                         "aliases", NULL);

  /* This has the side-effect of testing whether we can run binaries
   * for this architecture on the current environment. We
   * assume that this is the same as whether we can run them
   * on the host, if different. */
  argv[0] = self->capsule_capture_libs;
  pv_run_sync (argv, NULL, NULL, &self->ld_so, NULL);

  if (self->ld_so == NULL)
    {
      g_info ("Cannot determine ld.so for %s", self->details->tuple);
      return FALSE;
    }

  return TRUE;
}

static gboolean
runtime_architecture_check_valid (RuntimeArchitecture *self)
{
  g_return_val_if_fail (self->multiarch_index < PV_N_SUPPORTED_ARCHITECTURES, FALSE);
  g_return_val_if_fail (self->details == &pv_multiarch_details[self->multiarch_index], FALSE);
  g_return_val_if_fail (self->capsule_capture_libs_basename != NULL, FALSE);
  g_return_val_if_fail (self->capsule_capture_libs != NULL, FALSE);
  g_return_val_if_fail (self->libdir_in_current_namespace != NULL, FALSE);
  g_return_val_if_fail (self->libdir_in_container != NULL, FALSE);
  g_return_val_if_fail (self->aliases_in_current_namespace != NULL, FALSE);
  g_return_val_if_fail (self->ld_so != NULL, FALSE);
  return TRUE;
}

static void
runtime_architecture_clear (RuntimeArchitecture *self)
{
  self->multiarch_index = G_MAXSIZE;
  self->details = NULL;
  g_clear_pointer (&self->capsule_capture_libs_basename, g_free);
  g_clear_pointer (&self->capsule_capture_libs, g_free);
  g_clear_pointer (&self->libdir_in_current_namespace, g_free);
  g_clear_pointer (&self->libdir_in_container, g_free);
  g_clear_pointer (&self->aliases_in_current_namespace, g_free);
  g_clear_pointer (&self->ld_so, g_free);
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (RuntimeArchitecture,
                                  runtime_architecture_clear)

static gboolean pv_runtime_use_provider_graphics_stack (PvRuntime *self,
                                                        FlatpakBwrap *bwrap,
                                                        PvEnviron *container_env,
                                                        GError **error);
static void pv_runtime_set_search_paths (PvRuntime *self,
                                         PvEnviron *container_env);

static void
pv_runtime_init (PvRuntime *self)
{
  self->any_libc_from_provider = FALSE;
  self->all_libc_from_provider = FALSE;
  self->variable_dir_fd = -1;
  self->mutable_sysroot_fd = -1;
  self->host_fd = -1;
  self->is_flatpak_env = g_file_test ("/.flatpak-info",
                                      G_FILE_TEST_IS_REGULAR);
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

      case PROP_GRAPHICS_PROVIDER:
        g_value_set_object (value, self->provider);
        break;

      case PROP_INTERPRETER_HOST_PROVIDER:
        g_value_set_object (value, self->interpreter_host_provider);
        break;

      case PROP_ORIGINAL_ENVIRON:
        g_value_set_boxed (value, self->original_environ);
        break;

      case PROP_FLAGS:
        g_value_set_flags (value, self->flags);
        break;

      case PROP_ID:
        g_value_set_string (value, self->id);
        break;

      case PROP_VARIABLE_DIR:
        g_value_set_string (value, self->variable_dir);
        break;

      case PROP_SOURCE:
        g_value_set_string (value, self->source);
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
  const char *path;

  switch (prop_id)
    {
      case PROP_BUBBLEWRAP:
        /* Construct-only */
        g_return_if_fail (self->bubblewrap == NULL);
        self->bubblewrap = g_value_dup_string (value);
        break;

      case PROP_GRAPHICS_PROVIDER:
        /* Construct-only */
        g_return_if_fail (self->provider == NULL);
        self->provider = g_value_dup_object (value);
        break;

      case PROP_INTERPRETER_HOST_PROVIDER:
        /* Construct-only */
        g_return_if_fail (self->interpreter_host_provider == NULL);
        self->interpreter_host_provider = g_value_dup_object (value);
        break;

      case PROP_ORIGINAL_ENVIRON:
        /* Construct-only */
        g_return_if_fail (self->original_environ == NULL);

        self->original_environ = g_value_dup_boxed (value);
        break;

      case PROP_FLAGS:
        self->flags = g_value_get_flags (value);
        break;

      case PROP_VARIABLE_DIR:
        /* Construct-only */
        g_return_if_fail (self->variable_dir == NULL);
        path = g_value_get_string (value);

        if (path != NULL)
          {
            self->variable_dir = realpath (path, NULL);

            if (self->variable_dir == NULL)
              {
                /* It doesn't exist. Keep the non-canonical path so we
                 * can warn about it later */
                self->variable_dir = g_strdup (path);
              }
          }

        break;

      case PROP_ID:
        /* Construct-only */
        g_return_if_fail (self->id == NULL);
        self->id = g_value_dup_string (value);
        break;

      case PROP_SOURCE:
        /* Construct-only */
        g_return_if_fail (self->source == NULL);
        path = g_value_get_string (value);

        if (path != NULL)
          {
            self->source = realpath (path, NULL);

            if (self->source == NULL)
              {
                /* It doesn't exist. Keep the non-canonical path so we
                 * can warn about it later */
                self->source = g_strdup (path);
              }
          }

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

  g_return_if_fail (self->original_environ != NULL);
  g_return_if_fail (self->source != NULL);
}

static void
pv_runtime_maybe_garbage_collect_subdir (const char *description,
                                         const char *parent,
                                         int parent_fd,
                                         const char *member)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(PvBwrapLock) temp_lock = NULL;
  g_autofree gchar *keep = NULL;
  g_autofree gchar *ref = NULL;
  struct stat ignore;

  g_return_if_fail (parent != NULL);
  g_return_if_fail (parent_fd >= 0);
  g_return_if_fail (member != NULL);

  g_debug ("Found %s %s/%s, considering whether to delete it...",
           description, parent, member);

  keep = g_build_filename (member, "keep", NULL);

  if (glnx_fstatat (parent_fd, keep, &ignore,
                    AT_SYMLINK_NOFOLLOW, &local_error))
    {
      g_debug ("Not deleting \"%s/%s\": ./keep exists",
               parent, member);
      return;
    }
  else if (!g_error_matches (local_error, G_IO_ERROR,
                             G_IO_ERROR_NOT_FOUND))
    {
      /* EACCES or something? Give it the benefit of the doubt */
      g_warning ("Not deleting \"%s/%s\": unable to stat ./keep: %s",
               parent, member, local_error->message);
      return;
    }

  g_clear_error (&local_error);

  ref = g_build_filename (member, ".ref", NULL);
  temp_lock = pv_bwrap_lock_new (parent_fd, ref,
                                 (PV_BWRAP_LOCK_FLAGS_CREATE |
                                  PV_BWRAP_LOCK_FLAGS_WRITE),
                                 &local_error);

  if (temp_lock == NULL)
    {
      g_info ("Not deleting \"%s/%s\": unable to get lock: %s",
              parent, member, local_error->message);
      return;
    }

  g_debug ("Deleting \"%s/%s\"...", parent, member);

  /* We have the lock, which would not have happened if someone was
   * still using the runtime, so we can safely delete it. */
  if (!glnx_shutil_rm_rf_at (parent_fd, member, NULL, &local_error))
    {
      g_debug ("Unable to delete %s/%s: %s",
               parent, member, local_error->message);
    }
}

static gboolean
is_old_runtime_deployment (const char *name)
{
  if (g_str_has_prefix (name, "scout_before_"))
    return TRUE;

  if (g_str_has_prefix (name, "soldier_before_"))
    return TRUE;

  if (g_str_has_prefix (name, "scout_0."))
    return TRUE;

  if (g_str_has_prefix (name, "soldier_0."))
    return TRUE;

  if (g_str_has_prefix (name, ".scout_")
      && g_str_has_suffix (name, "_unpack-temp"))
    return TRUE;

  if (g_str_has_prefix (name, ".soldier_")
      && g_str_has_suffix (name, "_unpack-temp"))
    return TRUE;

  return FALSE;
}

gboolean
pv_runtime_garbage_collect_legacy (const char *variable_dir,
                                   const char *runtime_base,
                                   SrtDirentCompareFunc arbitrary_dirent_order,
                                   GError **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(PvBwrapLock) variable_lock = NULL;
  g_autoptr(PvBwrapLock) base_lock = NULL;
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer = NULL;
  g_auto(SrtDirIter) variable_dir_iter = SRT_DIR_ITER_CLEARED;
  g_auto(SrtDirIter) runtime_base_iter = SRT_DIR_ITER_CLEARED;
  glnx_autofd int variable_dir_fd = -1;
  glnx_autofd int runtime_base_fd = -1;
  struct
  {
    const char *path;
    SrtDirIter *iter;
  } iters[] = {
    { variable_dir, &variable_dir_iter },
    { runtime_base, &runtime_base_iter },
  };
  gsize i;

  g_return_val_if_fail (variable_dir != NULL, FALSE);
  g_return_val_if_fail (runtime_base != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  timer = _srt_profiling_start ("Cleaning up legacy runtimes in %s and %s",
                                variable_dir, runtime_base);

  if (g_mkdir_with_parents (variable_dir, 0700) != 0)
    return glnx_throw_errno_prefix (error, "Unable to create %s",
                                    variable_dir);

  if (!glnx_opendirat (AT_FDCWD, variable_dir, TRUE,
                       &variable_dir_fd, error))
    return FALSE;

  if (!glnx_opendirat (AT_FDCWD, runtime_base, TRUE,
                       &runtime_base_fd, error))
    return FALSE;

  variable_lock = pv_bwrap_lock_new (variable_dir_fd, ".ref",
                                     (PV_BWRAP_LOCK_FLAGS_CREATE
                                      | PV_BWRAP_LOCK_FLAGS_WRITE),
                                     &local_error);

  /* If we can't take the lock immediately, just don't do GC */
  if (variable_lock == NULL)
    return TRUE;

  /* We take out locks on both the variable directory and the base
   * directory, because historically in the shell scripts we only
   * locked the base directory, and we later moved to locking only the
   * variable directory. Now that we're in C code it seems safest to
   * lock both. */
  base_lock = pv_bwrap_lock_new (runtime_base_fd, ".ref",
                                 (PV_BWRAP_LOCK_FLAGS_CREATE
                                  | PV_BWRAP_LOCK_FLAGS_WRITE),
                                 &local_error);

  /* Same here */
  if (base_lock == NULL)
    return TRUE;

  for (i = 0; i < G_N_ELEMENTS (iters); i++)
    {
      const char * const symlinks[] = { "scout", "soldier" };
      gsize j;

      if (!_srt_dir_iter_init_at (iters[i].iter, AT_FDCWD, iters[i].path,
                                  (SRT_DIR_ITER_FLAGS_FOLLOW
                                   | SRT_DIR_ITER_FLAGS_ENSURE_DTYPE),
                                  arbitrary_dirent_order,
                                  error))
        return FALSE;

      g_debug ("Cleaning up old subdirectories in %s...",
               iters[i].path);

      while (TRUE)
        {
          struct dirent *dent;

          if (!_srt_dir_iter_next_dent (iters[i].iter, &dent, NULL, error))
            return FALSE;

          if (dent == NULL)
            break;

          switch (dent->d_type)
            {
              case DT_DIR:
                break;

              case DT_BLK:
              case DT_CHR:
              case DT_FIFO:
              case DT_LNK:
              case DT_REG:
              case DT_SOCK:
              case DT_UNKNOWN:
              default:
                g_debug ("Ignoring %s/%s: not a directory",
                         iters[i].path, dent->d_name);
                continue;
            }

          if (!is_old_runtime_deployment (dent->d_name))
            continue;

          pv_runtime_maybe_garbage_collect_subdir ("legacy runtime",
                                                   iters[i].path,
                                                   iters[i].iter->real_iter.fd,
                                                   dent->d_name);
        }

      g_debug ("Cleaning up old symlinks in %s...",
               iters[i].path);

      for (j = 0; j < G_N_ELEMENTS (symlinks); j++)
        pv_delete_dangling_symlink (iters[i].iter->real_iter.fd, iters[i].path,
                                    symlinks[j]);
    }

  return TRUE;
}

static gboolean
pv_runtime_garbage_collect (PvRuntime *self,
                            PvBwrapLock *variable_dir_lock,
                            GError **error)
{
  g_auto(SrtDirIter) iter = SRT_DIR_ITER_CLEARED;
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer = NULL;

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (self->variable_dir != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  /* We don't actually *use* this: it just acts as an assertion that
   * we are holding the lock on the parent directory. */
  g_return_val_if_fail (variable_dir_lock != NULL, FALSE);

  timer = _srt_profiling_start ("Cleaning up temporary runtimes in %s",
                                self->variable_dir);

  if (!_srt_dir_iter_init_at (&iter, AT_FDCWD, self->variable_dir,
                              (SRT_DIR_ITER_FLAGS_FOLLOW
                               | SRT_DIR_ITER_FLAGS_ENSURE_DTYPE),
                              self->arbitrary_dirent_order,
                              error))
    return FALSE;

  while (TRUE)
    {
      struct dirent *dent;

      if (!_srt_dir_iter_next_dent (&iter, &dent, NULL, error))
        return FALSE;

      if (dent == NULL)
        break;

      switch (dent->d_type)
        {
          case DT_DIR:
            break;

          case DT_BLK:
          case DT_CHR:
          case DT_FIFO:
          case DT_LNK:
          case DT_REG:
          case DT_SOCK:
          case DT_UNKNOWN:
          default:
            g_debug ("Ignoring %s/%s: not a directory",
                     self->variable_dir, dent->d_name);
            continue;
        }

      if (g_str_has_prefix (dent->d_name, "deploy-"))
        {
          if (_srt_fstatat_is_same_file (self->variable_dir_fd,
                                         dent->d_name,
                                         AT_FDCWD,
                                         self->deployment))
            {
              g_debug ("Ignoring %s/%s: is the current version",
                       self->variable_dir, dent->d_name);
              continue;
            }
        }
      else if (!g_str_has_prefix (dent->d_name, "tmp-"))
        {
          g_debug ("Ignoring %s/%s: not tmp-*",
                   self->variable_dir, dent->d_name);
          continue;
        }

      pv_runtime_maybe_garbage_collect_subdir ("temporary runtime",
                                               self->variable_dir,
                                               self->variable_dir_fd,
                                               dent->d_name);
    }

  return TRUE;
}

static gboolean
pv_runtime_init_variable_dir (PvRuntime *self,
                              GError **error)
{
  /* Nothing to do in this case */
  if (self->variable_dir == NULL)
    return TRUE;

  if (g_mkdir_with_parents (self->variable_dir, 0700) != 0)
    return glnx_throw_errno_prefix (error, "Unable to create %s",
                                    self->variable_dir);

  if (!glnx_opendirat (AT_FDCWD, self->variable_dir, TRUE,
                       &self->variable_dir_fd, error))
    return FALSE;

  return TRUE;
}

static gboolean
pv_runtime_create_copy (PvRuntime *self,
                        PvBwrapLock *variable_dir_lock,
                        const char *usr_mtree,
                        PvMtreeApplyFlags mtree_flags,
                        GError **error)
{
  g_autofree gchar *dest_usr = NULL;
  g_autofree gchar *temp_dir = NULL;
  g_auto(SrtDirIter) dir = SRT_DIR_ITER_CLEARED;
  g_autoptr(PvBwrapLock) copy_lock = NULL;
  G_GNUC_UNUSED g_autoptr(PvBwrapLock) source_lock = NULL;
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer = NULL;
  struct dirent *dent;
  glnx_autofd int temp_dir_fd = -1;
  gboolean is_just_usr;

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (self->variable_dir != NULL, FALSE);
  g_return_val_if_fail (self->flags & PV_RUNTIME_FLAGS_COPY_RUNTIME, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  /* We don't actually *use* this: it just acts as an assertion that
   * we are holding the lock on the parent directory. */
  g_return_val_if_fail (variable_dir_lock != NULL, FALSE);

  timer = _srt_profiling_start ("Temporary runtime copy");

  temp_dir = g_build_filename (self->variable_dir, "tmp-XXXXXX", NULL);

  if (g_mkdtemp (temp_dir) == NULL)
    return glnx_throw_errno_prefix (error,
                                    "Cannot create temporary directory \"%s\"",
                                    temp_dir);

  dest_usr = g_build_filename (temp_dir, "usr", NULL);

  if (usr_mtree != NULL)
    {
      is_just_usr = TRUE;
    }
  else
    {
      g_autofree gchar *source_usr_subdir = g_build_filename (self->source_files,
                                                              "usr", NULL);

      is_just_usr = !g_file_test (source_usr_subdir, G_FILE_TEST_IS_DIR);
    }

  if (is_just_usr)
    {
      /* ${source_files}/usr does not exist, so assume it's a merged /usr,
       * for example ./scout/files. Copy ${source_files}/bin to
       * ${temp_dir}/usr/bin, etc. */
      if (usr_mtree != NULL)
        {
          /* If there's a manifest available, it's actually quicker to iterate
           * through the manifest and use that to populate a new copy of the
           * runtime that it would be to do the equivalent of `cp -al` -
           * presumably because the mtree is probably contiguous on disk,
           * and the nested directories are probably not. */
          glnx_autofd int dest_usr_fd = -1;

          if (!glnx_ensure_dir (AT_FDCWD, dest_usr, 0755, error))
            return FALSE;

          if (!glnx_opendirat (AT_FDCWD, dest_usr, FALSE, &dest_usr_fd, error))
            {
              g_prefix_error (error, "Unable to open \"%s\": ", dest_usr);
              return FALSE;
            }

          if (!pv_mtree_apply (usr_mtree, dest_usr, dest_usr_fd,
                               self->source_files,
                               mtree_flags | PV_MTREE_APPLY_FLAGS_EXPECT_HARD_LINKS,
                               error))
            return FALSE;
        }
      else
        {
          /* Fall back to assuming that what's on-disk is correct. */
          if (!pv_cheap_tree_copy (self->source_files, dest_usr,
                                   PV_COPY_FLAGS_EXPECT_HARD_LINKS, error))
            return FALSE;
        }
    }
  else
    {
      /* ${source_files}/usr exists, so assume it's a complete sysroot.
       * Merge ${source_files}/bin and ${source_files}/usr/bin into
       * ${temp_dir}/usr/bin, etc. */
      g_assert (usr_mtree == NULL);

      if (!pv_cheap_tree_copy (self->source_files, temp_dir,
                               PV_COPY_FLAGS_USRMERGE, error))
        return FALSE;
    }

  if (!glnx_opendirat (-1, temp_dir, FALSE, &temp_dir_fd, error))
    return FALSE;

  /* We need to break the hard link for the lock file, otherwise the
   * temporary copy will share its locked/unlocked state with the
   * original. */
  if (TEMP_FAILURE_RETRY (unlinkat (temp_dir_fd, ".ref", 0)) != 0
      && errno != ENOENT)
    return glnx_throw_errno_prefix (error,
                                    "Cannot remove \"%s/.ref\"",
                                    temp_dir);

  if (TEMP_FAILURE_RETRY (unlinkat (temp_dir_fd, "usr/.ref", 0)) != 0
      && errno != ENOENT)
    return glnx_throw_errno_prefix (error,
                                    "Cannot remove \"%s/usr/.ref\"",
                                    temp_dir);

  /* Create the copy in a pre-locked state. After the lock on the parent
   * directory is released, the copy continues to have a read lock,
   * preventing it from being modified or deleted while in use (even if
   * a cleanup process successfully obtains a write lock on the parent).
   *
   * Because we control the structure of the runtime in this case, we
   * actually lock /usr/.ref instead of /.ref, and ensure that /.ref
   * is a symlink to it. This might become important if we pass the
   * runtime's /usr to Flatpak, which normally takes out a lock on
   * /usr/.ref (obviously this will only work if the runtime happens
   * to be merged-/usr). */
  copy_lock = pv_bwrap_lock_new (temp_dir_fd, "usr/.ref",
                                 PV_BWRAP_LOCK_FLAGS_CREATE,
                                 error);

  if (copy_lock == NULL)
    return glnx_prefix_error (error,
                              "Unable to lock \"%s/.ref\" in temporary runtime",
                              dest_usr);

  if (is_just_usr)
    {
      if (TEMP_FAILURE_RETRY (symlinkat ("usr/.ref",
                                         temp_dir_fd,
                                         ".ref")) != 0)
        return glnx_throw_errno_prefix (error,
                                        "Cannot create symlink \"%s/.ref\" -> usr/.ref",
                                        temp_dir);
    }

  if (!_srt_dir_iter_init_at (&dir, AT_FDCWD, dest_usr,
                              SRT_DIR_ITER_FLAGS_FOLLOW,
                              self->arbitrary_dirent_order,
                              error))
    return FALSE;

  while (_srt_dir_iter_next_dent (&dir, &dent, NULL, NULL) && dent != NULL)
    {
      const char *member = dent->d_name;

      /* Create symlinks ${temp_dir}/bin -> usr/bin, etc. if missing.
       *
       * Also make ${temp_dir}/etc, ${temp_dir}/var symlinks to etc
       * and var, for the benefit of tools like capsule-capture-libs
       * accessing /etc/ld.so.cache in the incomplete container (for the
       * final container command-line they get merged by bind_runtime()
       * instead). */
      if (g_str_equal (member, "bin") ||
          g_str_equal (member, "etc") ||
          (g_str_has_prefix (member, "lib") &&
           !g_str_equal (member, "libexec")) ||
          g_str_equal (member, "sbin") ||
          g_str_equal (member, "var"))
        {
          g_autofree gchar *dest = g_build_filename (temp_dir, member, NULL);
          g_autofree gchar *target = g_build_filename ("usr", member, NULL);

          if (symlink (target, dest) != 0)
            {
              /* Ignore EEXIST in the case where it was not just /usr:
               * it's fine if the runtime we copied from source_files
               * already had either directories or symlinks in its root
               * directory */
              if (is_just_usr || errno != EEXIST)
                return glnx_throw_errno_prefix (error,
                                                "Cannot create symlink \"%s\" -> %s",
                                                dest, target);
            }
        }
    }

  /* Hand over from holding a lock on the source to just holding a lock
   * on the copy. We'll release source_lock when we leave this scope */
  source_lock = g_steal_pointer (&self->runtime_lock);
  self->runtime_lock = g_steal_pointer (&copy_lock);
  self->mutable_sysroot = g_steal_pointer (&temp_dir);
  self->mutable_sysroot_fd = glnx_steal_fd (&temp_dir_fd);

  return TRUE;
}

static gboolean
gstring_replace_suffix (GString *s,
                        const char *suffix,
                        const char *replacement)
{
  gsize len = strlen (suffix);

  if (s->len >= len
      && strcmp (&s->str[s->len - len], suffix) == 0)
    {
      g_string_truncate (s, s->len - len);
      g_string_append (s, replacement);
      return TRUE;
    }

  return FALSE;
}

/*
 * mutable_lock: (out) (not optional):
 */
static gboolean
pv_runtime_unpack (PvRuntime *self,
                   PvBwrapLock **mutable_lock,
                   GError **error)
{
  g_autoptr(GString) debug_tarball = NULL;
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer = NULL;
  g_autofree gchar *deploy_basename = NULL;
  g_autofree gchar *unpack_dir = NULL;

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (mutable_lock != NULL, FALSE);
  g_return_val_if_fail (*mutable_lock == NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (self->source != NULL, FALSE);
  g_return_val_if_fail (self->variable_dir != NULL, FALSE);
  g_return_val_if_fail (self->variable_dir_fd >= 0, FALSE);
  g_return_val_if_fail (self->deployment == NULL, FALSE);

  if (!g_file_test (self->source, G_FILE_TEST_IS_REGULAR))
    return glnx_throw (error, "\"%s\" is not a regular file", self->source);

  if (!g_str_has_suffix (self->source, ".tar.gz"))
    return glnx_throw (error, "\"%s\" is not a .tar.gz file", self->source);

  if (self->id == NULL)
    {
      g_autoptr(GString) build_id_file = g_string_new (self->source);
      g_autofree char *id = NULL;
      gsize len;
      gsize i;

      if (gstring_replace_suffix (build_id_file, "-runtime.tar.gz",
                                  "-buildid.txt")
          || gstring_replace_suffix (build_id_file, "-sysroot.tar.gz",
                                     "-buildid.txt"))
        {
          if (!g_file_get_contents (build_id_file->str, &id, &len, error))
            {
              g_prefix_error (error, "Unable to determine build ID from \"%s\": ",
                              build_id_file->str);
              return FALSE;
            }

          if (len == 0)
            return glnx_throw (error, "Build ID in \"%s\" is empty",
                               build_id_file->str);

          for (i = 0; i < len; i++)
            {
              /* Ignore a trailing newline */
              if (i + 1 == len && id[i] == '\n')
                {
                  id[i] = '\0';
                  break;
                }

              /* Allow dot, dash or underscore, but not at the beginning */
              if (i > 0 && strchr (".-_", id[i]) != NULL)
                continue;

              if (!g_ascii_isalnum (id[i]))
                return glnx_throw (error, "Build ID in \"%s\" is invalid",
                                   build_id_file->str);
            }

          self->id = g_steal_pointer (&id);
        }
    }

  if (self->id == NULL)
    return glnx_throw (error, "Cannot unpack archive without unique ID");

  deploy_basename = g_strdup_printf ("deploy-%s", self->id);
  self->deployment = g_build_filename (self->variable_dir,
                                       deploy_basename, NULL);

  /* Fast path: if we already unpacked it, nothing more to do! */
  if (g_file_test (self->deployment, G_FILE_TEST_IS_DIR))
    return TRUE;

  /* Lock the parent directory. Anything that directly manipulates the
   * unpacked runtimes is expected to do the same, so that
   * it cannot be deleting unpacked runtimes at the same time we're
   * creating them.
   *
   * This is an exclusive lock, to avoid two concurrent processes trying
   * to unpack the same runtime. */
  *mutable_lock = pv_bwrap_lock_new (self->variable_dir_fd, ".ref",
                                     (PV_BWRAP_LOCK_FLAGS_CREATE
                                      | PV_BWRAP_LOCK_FLAGS_WAIT),
                                     error);

  if (*mutable_lock == NULL)
    return FALSE;

  /* Slow path: we need to do this the hard way. */
  timer = _srt_profiling_start ("Unpacking %s", self->source);
  unpack_dir = g_build_filename (self->variable_dir, "tmp-XXXXXX", NULL);

  if (g_mkdtemp (unpack_dir) == NULL)
    return glnx_throw_errno_prefix (error,
                                    "Cannot create temporary directory \"%s\"",
                                    unpack_dir);

  g_info ("Unpacking \"%s\" into \"%s\"...", self->source, unpack_dir);

    {
      g_autoptr(FlatpakBwrap) tar = flatpak_bwrap_new (NULL);

      flatpak_bwrap_add_args (tar,
                              "tar",
                              "--force-local",
                              "-C", unpack_dir,
                              NULL);

      if (self->flags & PV_RUNTIME_FLAGS_VERBOSE)
        flatpak_bwrap_add_arg (tar, "-v");

      flatpak_bwrap_add_args (tar,
                              "-xf", self->source,
                              NULL);
      flatpak_bwrap_finish (tar);

      if (!pv_bwrap_run_sync (tar, NULL, error))
        {
          glnx_shutil_rm_rf_at (-1, unpack_dir, NULL, NULL);
          return FALSE;
        }
    }

  debug_tarball = g_string_new (self->source);

  if (gstring_replace_suffix (debug_tarball, "-runtime.tar.gz",
                              "-debug.tar.gz")
      && g_file_test (debug_tarball->str, G_FILE_TEST_EXISTS))
    {
      g_autoptr(FlatpakBwrap) tar = flatpak_bwrap_new (NULL);
      g_autoptr(GError) local_error = NULL;
      g_autofree char *files_lib_debug = NULL;

      files_lib_debug = g_build_filename (unpack_dir, "files", "lib",
                                          "debug", NULL);

      flatpak_bwrap_add_args (tar,
                              "tar",
                              "--force-local",
                              "-C", files_lib_debug,
                              NULL);

      if (self->flags & PV_RUNTIME_FLAGS_VERBOSE)
        flatpak_bwrap_add_arg (tar, "-v");

      flatpak_bwrap_add_args (tar,
                              "-xf", debug_tarball->str,
                              "files/",
                              NULL);
      flatpak_bwrap_finish (tar);

      if (!pv_bwrap_run_sync (tar, NULL, &local_error))
        g_debug ("Ignoring error unpacking detached debug symbols: %s",
                 local_error->message);
    }

  g_info ("Renaming \"%s\" to \"%s\"...", unpack_dir, deploy_basename);

  if (!glnx_renameat (self->variable_dir_fd, unpack_dir,
                      self->variable_dir_fd, deploy_basename,
                      error))
    {
      glnx_shutil_rm_rf_at (-1, unpack_dir, NULL, NULL);
      return FALSE;
    }

  return TRUE;
}

typedef struct
{
  const PvMultiarchDetails *details;
  PvRuntimeFlags flags;
  PvGraphicsProvider *provider;
  GCancellable *cancellable;
} EnumerationThreadInputs;

/* Called in main thread */
static EnumerationThreadInputs *
enumeration_thread_inputs_new (const PvMultiarchDetails *details,
                               PvRuntimeFlags flags,
                               PvGraphicsProvider *provider,
                               GCancellable *cancellable)
{
  EnumerationThreadInputs *self = g_new0 (EnumerationThreadInputs, 1);

  self->details = details;
  self->flags = self->flags;
  self->provider = g_object_ref (provider);
  self->cancellable = g_object_ref (cancellable);
  return self;
}

/* Called in enumeration thread */
static void
enumeration_thread_inputs_free (EnumerationThreadInputs *self)
{
  g_object_unref (self->cancellable);
  g_object_unref (self->provider);
  g_free (self);
}

/* Called in enumeration thread */
static gpointer
enumerate_arch (gpointer data)
{
  EnumerationThreadInputs *inputs = data;
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer =
    _srt_profiling_start ("Enumerating %s drivers in thread",
                          inputs->details->tuple);
  g_autoptr(SrtSystemInfo) system_info =
    pv_graphics_provider_create_system_info (inputs->provider);

  if (g_cancellable_is_cancelled (inputs->cancellable))
    goto out;

  /* At the moment the real host is included only when FEX emulator is in use.
   * Skipping VDPAU until there is a real use case for it, because
   * it only supports one search path entry, which is problematic for us. */
  if (pv_supported_architectures_include_machine_type (inputs->details->machine_type))
    {
      G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) part_timer =
        _srt_profiling_start ("Enumerating %s VDPAU drivers in thread",
                              inputs->details->tuple);
      G_GNUC_UNUSED g_autoptr(SrtObjectList) drivers = NULL;

      /* We ignore the results. system_info will cache them for later
       * calls, so when we're doing the actual work, redoing this call
       * will just retrieve them */
      drivers = srt_system_info_list_vdpau_drivers (system_info,
                                                    inputs->details->tuple,
                                                    SRT_DRIVER_FLAGS_NONE);
    }

  if (g_cancellable_is_cancelled (inputs->cancellable))
    goto out;

  if (TRUE)
    {
      G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) part_timer =
        _srt_profiling_start ("Enumerating %s DRI drivers in thread",
                              inputs->details->tuple);
      G_GNUC_UNUSED g_autoptr(SrtObjectList) drivers = NULL;

      drivers = srt_system_info_list_dri_drivers (system_info,
                                                  inputs->details->tuple,
                                                  SRT_DRIVER_FLAGS_NONE);
    }

  if (g_cancellable_is_cancelled (inputs->cancellable))
    goto out;

  if (TRUE)
    {
      G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) part_timer =
        _srt_profiling_start ("Enumerating %s VA-API drivers in thread",
                              inputs->details->tuple);
      G_GNUC_UNUSED g_autoptr(SrtObjectList) drivers = NULL;

      drivers = srt_system_info_list_va_api_drivers (system_info,
                                                     inputs->details->tuple,
                                                     SRT_DRIVER_FLAGS_NONE);
    }

  if (g_cancellable_is_cancelled (inputs->cancellable))
    goto out;

  g_free (srt_system_info_dup_libdl_platform (system_info,
                                              inputs->details->tuple, NULL));

out:
  enumeration_thread_inputs_free (inputs);
  return g_steal_pointer (&system_info);
}

static void
cache_indep_graphics_stack (SrtSystemInfo *system_info,
                            PvRuntimeFlags flags,
                            const char * const *multiarch_tuples,
                            GCancellable *cancellable)
{
  if (g_cancellable_is_cancelled (cancellable))
    return;

  if (TRUE)
    {
      G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) part_timer =
        _srt_profiling_start ("Enumerating EGL ICDs in thread");
      G_GNUC_UNUSED g_autoptr(SrtObjectList) drivers = NULL;

      drivers = srt_system_info_list_egl_icds (system_info, multiarch_tuples);
    }

  if (g_cancellable_is_cancelled (cancellable))
    return;

  if (TRUE)
    {
      G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) part_timer =
        _srt_profiling_start ("Enumerating EGL external platforms in thread");
      G_GNUC_UNUSED g_autoptr(SrtObjectList) drivers = NULL;

      drivers = srt_system_info_list_egl_external_platforms (system_info, multiarch_tuples);
    }

  if (g_cancellable_is_cancelled (cancellable))
    return;

  if (TRUE)
    {
      G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) part_timer =
        _srt_profiling_start ("Enumerating Vulkan ICDs in thread");
      G_GNUC_UNUSED g_autoptr(SrtObjectList) drivers = NULL;

      drivers = srt_system_info_list_vulkan_icds (system_info, multiarch_tuples);
    }

  if (g_cancellable_is_cancelled (cancellable))
    return;

  if (flags & PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS)
    {
      G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) part_timer =
        _srt_profiling_start ("Enumerating Vulkan layers in thread");
      G_GNUC_UNUSED g_autoptr(SrtObjectList) exp_layers = NULL;
      G_GNUC_UNUSED g_autoptr(SrtObjectList) imp_layers = NULL;

      exp_layers = srt_system_info_list_explicit_vulkan_layers (system_info);
      imp_layers = srt_system_info_list_implicit_vulkan_layers (system_info);
    }
}

/* Called in enumeration thread */
static gpointer
enumerate_indep (gpointer data)
{
  EnumerationThreadInputs *inputs = data;
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer =
    _srt_profiling_start ("Enumerating cross-architecture ICDs in thread");
  g_autoptr(SrtSystemInfo) system_info =
    pv_graphics_provider_create_system_info (inputs->provider);

  cache_indep_graphics_stack (system_info, inputs->flags, pv_multiarch_tuples,
                              inputs->cancellable);

  enumeration_thread_inputs_free (inputs);
  return g_steal_pointer (&system_info);
}

/*
 * Must be called from same thread as enumeration_thread_start_arch()
 * or enumeration_thread_start_indep().
 *
 * Returns: (transfer none):
 */
static SrtSystemInfo *
enumeration_thread_join (EnumerationThread *self)
{
  if (self->thread != NULL)
    {
      g_assert (self->system_info == NULL);
      g_cancellable_cancel (self->cancellable);
      self->system_info = g_thread_join (g_steal_pointer (&self->thread));
    }

  return self->system_info;
}

static void
enumeration_thread_clear (EnumerationThread *self)
{
  enumeration_thread_join (self);
  g_clear_object (&self->system_info);
  g_clear_object (&self->cancellable);
}

static void
enumeration_threads_clear (EnumerationThread **arr,
                           gsize n)
{
  EnumerationThread *threads = *arr;
  gsize i;

  *arr = NULL;

  if (threads == NULL)
    return;

  for (i = 0; i < n; i++)
    enumeration_thread_clear (threads + i);

  g_free (threads);
}

/* Must be called in main thread */
static void
enumeration_thread_start_arch (EnumerationThread *self,
                               const PvMultiarchDetails *details,
                               PvRuntimeFlags flags,
                               PvGraphicsProvider *provider)
{
  g_return_if_fail (self->cancellable == NULL);
  g_return_if_fail (self->system_info == NULL);
  g_return_if_fail (self->thread == NULL);

  self->cancellable = g_cancellable_new ();
  self->thread = g_thread_new (details->tuple, enumerate_arch,
                               enumeration_thread_inputs_new (details, flags,
                                                              provider,
                                                              self->cancellable));
}

/* Must be called in main thread */
static void
enumeration_thread_start_indep (EnumerationThread *self,
                                PvRuntimeFlags flags,
                                PvGraphicsProvider *provider,
                                const gchar *thread_name)
{
  g_return_if_fail (self->cancellable == NULL);
  g_return_if_fail (self->system_info == NULL);
  g_return_if_fail (self->thread == NULL);

  self->cancellable = g_cancellable_new ();
  self->thread = g_thread_new (thread_name == NULL ? "cross-architecture" : thread_name,
                               enumerate_indep,
                               enumeration_thread_inputs_new (NULL, flags,
                                                              provider,
                                                              self->cancellable));
}

static gboolean
pv_runtime_initable_init (GInitable *initable,
                          GCancellable *cancellable G_GNUC_UNUSED,
                          GError **error)
{
  PvRuntime *self = PV_RUNTIME (initable);
  g_autoptr(PvBwrapLock) mutable_lock = NULL;
  g_autofree gchar *contents = NULL;
  g_autofree gchar *os_release = NULL;
  g_autofree gchar *usr_mtree = NULL;
  gsize len;
  PvMtreeApplyFlags mtree_flags = PV_MTREE_APPLY_FLAGS_NONE;

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  self->pv_prefix = _srt_find_myself (&self->helpers_path, error);

  if (self->pv_prefix == NULL)
    return FALSE;

  /* Enumerating the graphics provider's drivers only requires things
   * we already know, so start this first, and let it run in parallel
   * with other setup. The results go in the SrtSystemInfo's cache
   * for future use. */
  if (self->provider != NULL
      && !(self->flags & PV_RUNTIME_FLAGS_SINGLE_THREAD))
    {
      gsize i;

      enumeration_thread_start_indep (&self->indep_thread,
                                      self->flags,
                                      self->provider, NULL);

      if (self->interpreter_host_provider != NULL)
        {
          enumeration_thread_start_indep (&self->host_thread,
                                          self->flags,
                                          self->interpreter_host_provider,
                                          "real-host");

          self->arch_host_threads = g_new0 (EnumerationThread,
                                            PV_N_SUPPORTED_ARCHITECTURES_AS_EMULATOR_HOST);

          for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES_AS_EMULATOR_HOST; i++)
            enumeration_thread_start_arch (&self->arch_host_threads[i],
                                           &pv_multiarch_as_emulator_details[i],
                                           self->flags,
                                           self->interpreter_host_provider);
        }

      self->arch_threads = g_new0 (EnumerationThread,
                                   PV_N_SUPPORTED_ARCHITECTURES);

      for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
        enumeration_thread_start_arch (&self->arch_threads[i],
                                       &pv_multiarch_details[i],
                                       self->flags,
                                       self->provider);
    }

  /* If we are in Flatpak container we don't expect to have a working bwrap */
  if (self->bubblewrap != NULL
      && !g_file_test (self->bubblewrap, G_FILE_TEST_IS_EXECUTABLE))
    {
      return glnx_throw (error, "\"%s\" is not executable",
                         self->bubblewrap);
    }

  if (!pv_runtime_init_variable_dir (self, error))
    return FALSE;

  if (self->flags & PV_RUNTIME_FLAGS_UNPACK_ARCHIVE)
    {
      if (self->variable_dir_fd < 0)
        return glnx_throw (error,
                           "Cannot unpack archive without variable directory");

      if (!pv_runtime_unpack (self, &mutable_lock, error))
        return FALSE;

      /* Set by pv_runtime_unpack */
      g_assert (self->deployment != NULL);
    }
  else
    {
      self->deployment = g_strdup (self->source);
    }

  if (!g_file_test (self->deployment, G_FILE_TEST_IS_DIR))
    {
      return glnx_throw (error, "\"%s\" is not a directory",
                         self->deployment);
    }

  /* If the deployment contains usr-mtree.txt, assume that it's a
   * Flatpak-style merged-/usr runtime, and usr-mtree.txt describes
   * what's in the runtime. The content is taken from the files/
   * directory, but files not listed in the mtree are not included.
   *
   * The manifest compresses well (about 3:1 if sha256sums are included)
   * so try to read a compressed version first, falling back to
   * uncompressed. */
  usr_mtree = g_build_filename (self->deployment, "usr-mtree.txt.gz", NULL);

  if (g_file_test (usr_mtree, G_FILE_TEST_IS_REGULAR))
    {
      mtree_flags |= PV_MTREE_APPLY_FLAGS_GZIP;
    }
  else
    {
      g_clear_pointer (&usr_mtree, g_free);
      usr_mtree = g_build_filename (self->deployment, "usr-mtree.txt", NULL);
    }

  if (!g_file_test (usr_mtree, G_FILE_TEST_IS_REGULAR))
    g_clear_pointer (&usr_mtree, g_free);

  /* Or, if it contains ./files/, assume it's a Flatpak-style runtime where
   * ./files is a merged /usr and ./metadata is an optional GKeyFile. */
  self->source_files = g_build_filename (self->deployment, "files", NULL);

  if (usr_mtree != NULL)
    {
      g_debug ("Assuming %s is a merged-/usr runtime because it has "
               "a /usr mtree",
               self->deployment);
    }
  else if (g_file_test (self->source_files, G_FILE_TEST_IS_DIR))
    {
      g_debug ("Assuming %s is a Flatpak-style runtime", self->deployment);
    }
  else
    {
      g_debug ("Assuming %s is a sysroot or merged /usr", self->deployment);
      g_clear_pointer (&self->source_files, g_free);
      self->source_files = g_strdup (self->deployment);
    }

  g_debug ("Taking runtime files from: %s", self->source_files);

  /* Take a lock on the runtime until we're finished with setup,
   * to make sure it doesn't get deleted.
   *
   * If the runtime is mounted read-only in the container, it will
   * continue to be locked until all processes in the container exit.
   * If we make a temporary mutable copy, we only hold this lock until
   * setup has finished. */
  if (self->runtime_lock == NULL)
    {
      g_autofree gchar *files_ref = NULL;

      files_ref = g_build_filename (self->source_files, ".ref", NULL);
      self->runtime_lock = pv_bwrap_lock_new (AT_FDCWD, files_ref,
                                              PV_BWRAP_LOCK_FLAGS_CREATE,
                                              error);
    }

  /* If the runtime is being deleted, ... don't use it, I suppose? */
  if (self->runtime_lock == NULL)
    return FALSE;

  /* GC old runtimes (if they have become unused) before we create a
   * new one. This means we should only ever have one temporary runtime
   * copy per game that is run concurrently. */
  if (self->variable_dir_fd >= 0
      && (self->flags & PV_RUNTIME_FLAGS_GC_RUNTIMES))
    {
      g_autoptr(GError) local_error = NULL;

      /* Take out an exclusive lock for GC so that we will not conflict
       * with other concurrent processes that are halfway through
       * deploying or unpacking a runtime. */
      if (mutable_lock == NULL)
        mutable_lock = pv_bwrap_lock_new (self->variable_dir_fd, ".ref",
                                          (PV_BWRAP_LOCK_FLAGS_CREATE
                                           | PV_BWRAP_LOCK_FLAGS_WRITE),
                                          &local_error);

      if (mutable_lock == NULL)
        g_debug ("Unable to take an exclusive lock, skipping GC: %s",
                 local_error->message);
      else if (!pv_runtime_garbage_collect (self, mutable_lock, error))
        return FALSE;
    }

  /* Always copy the runtime into var/ before applying a manifest. */
  if (usr_mtree != NULL)
    self->flags |= PV_RUNTIME_FLAGS_COPY_RUNTIME;

  /* Always copy the runtime into var/ if we are setting it up as an
   * overlay rootfs for FEX-Emu or similar. This lets us require that
   * we're using a mutable sysroot, which is a lot simpler. */
  if (self->flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
    self->flags |= PV_RUNTIME_FLAGS_COPY_RUNTIME;

  if (self->flags & PV_RUNTIME_FLAGS_DETERMINISTIC)
    {
      self->arbitrary_dirent_order = _srt_dirent_strcmp;
      self->arbitrary_str_order = _srt_generic_strcmp0;
    }

  if (self->flags & PV_RUNTIME_FLAGS_COPY_RUNTIME)
    {
      if (self->variable_dir_fd < 0)
        return glnx_throw (error,
                           "Cannot copy runtime without variable directory");

      /* This time take out a non-exclusive lock: any number of processes
       * can safely be creating their own temporary copy at the same
       * time. If another process is doing GC, wait for it to finish,
       * then take our lock. */
      if (mutable_lock == NULL)
        mutable_lock = pv_bwrap_lock_new (self->variable_dir_fd, ".ref",
                                          (PV_BWRAP_LOCK_FLAGS_CREATE
                                           | PV_BWRAP_LOCK_FLAGS_WAIT),
                                          error);

      if (mutable_lock == NULL)
        return FALSE;

      if (!pv_runtime_create_copy (self, mutable_lock, usr_mtree,
                                   mtree_flags, error))
        return FALSE;
    }

  if (self->mutable_sysroot != NULL)
    {
      self->overrides_in_container = "/usr/lib/pressure-vessel/overrides";
      self->overrides = g_build_filename (self->mutable_sysroot,
                                          self->overrides_in_container, NULL);
      self->runtime_files = self->mutable_sysroot;
    }
  else
    {
      /* We currently only need a temporary directory if we don't have
       * a mutable sysroot to work with. */
      g_autofree gchar *tmpdir = g_dir_make_tmp ("pressure-vessel-wrap.XXXXXX",
                                                 error);

      if (tmpdir == NULL)
        return FALSE;

      self->tmpdir = realpath (tmpdir, NULL);

      if (self->tmpdir == NULL)
        return glnx_throw_errno_prefix (error, "realpath(\"%s\")", tmpdir);

      self->overrides = g_build_filename (self->tmpdir, "overrides", NULL);
      self->overrides_in_container = "/overrides";
      self->runtime_files = self->source_files;
    }

  self->runtime_files_on_host = pv_current_namespace_path_to_host_path (self->runtime_files);

  g_mkdir (self->overrides, 0700);

  self->runtime_app = g_build_filename (self->runtime_files, "app", NULL);
  self->runtime_usr = g_build_filename (self->runtime_files, "usr", NULL);

  if (g_file_test (self->runtime_usr, G_FILE_TEST_IS_DIR))
    {
      self->runtime_is_just_usr = FALSE;
    }
  else
    {
      /* runtime_files is just a merged /usr. */
      self->runtime_is_just_usr = TRUE;
      g_free (self->runtime_usr);
      self->runtime_usr = g_strdup (self->runtime_files);
    }

  self->libcapsule_knowledge = g_build_filename (self->runtime_usr,
                                                 "lib", "steamrt",
                                                 "libcapsule-knowledge.keyfile",
                                                 NULL);

  if (!g_file_test (self->libcapsule_knowledge, G_FILE_TEST_EXISTS))
    g_clear_pointer (&self->libcapsule_knowledge, g_free);

  self->runtime_abi_json = g_build_filename (self->runtime_usr, "lib", "steamrt",
                                             "steam-runtime-abi.json", NULL);

  if (!g_file_test (self->runtime_abi_json, G_FILE_TEST_EXISTS))
    g_clear_pointer (&self->runtime_abi_json, g_free);

  os_release = g_build_filename (self->runtime_usr, "lib", "os-release", NULL);

  /* TODO: Teach SrtSystemInfo to be able to load lib/os-release from
   * a merged-/usr, so we don't need to open-code this here */
  if (g_file_get_contents (os_release, &contents, &len, NULL))
    {
      gsize i;
      g_autofree gchar *id = NULL;
      g_autofree gchar *version_id = NULL;
      char *beginning_of_line = contents;

      for (i = 0; i < len; i++)
        {
          if (contents[i] == '\n')
            {
              contents[i] = '\0';

              if (id == NULL &&
                  g_str_has_prefix (beginning_of_line, "ID="))
                id = g_shell_unquote (beginning_of_line + strlen ("ID="), NULL);
              else if (version_id == NULL &&
                       g_str_has_prefix (beginning_of_line, "VERSION_ID="))
                version_id = g_shell_unquote (beginning_of_line + strlen ("VERSION_ID="), NULL);

              beginning_of_line = contents + i + 1;
            }
        }

      if (g_strcmp0 (id, "steamrt") == 0)
        {
          self->is_steamrt = TRUE;

          if (g_strcmp0 (version_id, "1") == 0)
            self->is_scout = TRUE;
        }
    }

  /* Opening /proc/self/root rather than / lets us bypass FEX-Emu's
   * redirection from the real root filesystem into its "rootfs". */
  if (!glnx_opendirat (-1, "/proc/self/root", TRUE, &self->root_fd, error))
    return FALSE;

  /* If we are in a Flatpak environment we expect to have the host system
   * mounted in `/run/host`. Otherwise we assume that the host system, in the
   * current namespace, is the root - but again use /proc/self/root to bypass
   * FEX-Emu's redirection. */
  if (g_file_test ("/.flatpak-info", G_FILE_TEST_IS_REGULAR))
    self->host_in_current_namespace = "/run/host";
  else
    self->host_in_current_namespace = "/proc/self/root";

  if (!glnx_opendirat (-1, self->host_in_current_namespace, TRUE,
                       &self->host_fd, error))
    return FALSE;

  if (!glnx_opendirat (-1, self->runtime_files, TRUE,
                       &self->runtime_files_fd, error))
    return FALSE;

  return TRUE;
}

void
pv_runtime_cleanup (PvRuntime *self)
{
  g_autoptr(GError) local_error = NULL;

  g_return_if_fail (PV_IS_RUNTIME (self));

  if (self->tmpdir != NULL &&
      !glnx_shutil_rm_rf_at (-1, self->tmpdir, NULL, &local_error))
    {
      g_warning ("Unable to delete temporary directory: %s",
                 local_error->message);
    }

  g_clear_pointer (&self->overrides, g_free);
  g_clear_pointer (&self->container_access, g_free);
  g_clear_pointer (&self->container_access_adverb, flatpak_bwrap_free);
  g_clear_pointer (&self->tmpdir, g_free);
}

static void
pv_runtime_dispose (GObject *object)
{
  PvRuntime *self = PV_RUNTIME (object);

  g_clear_object (&self->provider);
  g_clear_object (&self->interpreter_host_provider);
  enumeration_thread_clear (&self->indep_thread);
  enumeration_thread_clear (&self->host_thread);
  enumeration_threads_clear (&self->arch_host_threads,
                             PV_N_SUPPORTED_ARCHITECTURES_AS_EMULATOR_HOST);
  enumeration_threads_clear (&self->arch_threads,
                             PV_N_SUPPORTED_ARCHITECTURES);

  G_OBJECT_CLASS (pv_runtime_parent_class)->dispose (object);
}

static void
pv_runtime_finalize (GObject *object)
{
  PvRuntime *self = PV_RUNTIME (object);

  pv_runtime_cleanup (self);
  g_free (self->bubblewrap);
  glnx_close_fd (&self->host_fd);
  g_strfreev (self->original_environ);
  g_free (self->libcapsule_knowledge);
  g_free (self->runtime_abi_json);
  glnx_close_fd (&self->variable_dir_fd);
  g_free (self->variable_dir);
  glnx_close_fd (&self->mutable_sysroot_fd);
  g_free (self->mutable_sysroot);
  glnx_close_fd (&self->root_fd);
  g_free (self->runtime_files_on_host);
  g_free (self->runtime_app);
  glnx_close_fd (&self->runtime_files_fd);
  g_free (self->runtime_usr);
  g_free (self->source);
  g_free (self->source_files);
  g_free (self->deployment);
  g_free (self->id);

  if (self->runtime_lock != NULL)
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
  object_class->dispose = pv_runtime_dispose;
  object_class->finalize = pv_runtime_finalize;

  properties[PROP_BUBBLEWRAP] =
    g_param_spec_string ("bubblewrap", "Bubblewrap",
                         "Bubblewrap executable",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_GRAPHICS_PROVIDER] =
    g_param_spec_object ("graphics-provider",
                         "Graphics provider",
                         "Sysroot used for graphics stack, or NULL",
                         PV_TYPE_GRAPHICS_PROVIDER,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_INTERPRETER_HOST_PROVIDER] =
    g_param_spec_object ("interpreter-host-provider",
                         "Interpreter host graphics provider",
                         "Sysroot used for the interpreter host graphics stack, or NULL",
                         PV_TYPE_GRAPHICS_PROVIDER,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_ORIGINAL_ENVIRON] =
    g_param_spec_boxed ("original-environ", "Original environ",
                        "The original environ to use",
                        G_TYPE_STRV,
                        (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS));

  properties[PROP_FLAGS] =
    g_param_spec_flags ("flags", "Flags",
                        "Flags affecting how we set up the runtime",
                        PV_TYPE_RUNTIME_FLAGS, PV_RUNTIME_FLAGS_NONE,
                        (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS));

  properties[PROP_ID] =
    g_param_spec_string ("id", "ID",
                         "Unique identifier of runtime to be unpacked",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_VARIABLE_DIR] =
    g_param_spec_string ("variable-dir", "Variable directory",
                         ("Path to directory for temporary files, or NULL"),
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_SOURCE] =
    g_param_spec_string ("source", "Source",
                         ("Path to read-only runtime files (merged-/usr "
                          "or sysroot) or archive, in current namespace"),
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

PvRuntime *
pv_runtime_new (const char *source,
                const char *id,
                const char *variable_dir,
                const char *bubblewrap,
                PvGraphicsProvider *provider,
                PvGraphicsProvider *interpreter_host_provider,
                const GStrv original_environ,
                PvRuntimeFlags flags,
                GError **error)
{
  g_return_val_if_fail (source != NULL, NULL);
  g_return_val_if_fail ((flags & ~(PV_RUNTIME_FLAGS_MASK)) == 0, NULL);

  return g_initable_new (PV_TYPE_RUNTIME,
                         NULL,
                         error,
                         "bubblewrap", bubblewrap,
                         "graphics-provider", provider,
                         "interpreter-host-provider", interpreter_host_provider,
                         "original-environ", original_environ,
                         "variable-dir", variable_dir,
                         "source", source,
                         "id", id,
                         "flags", flags,
                         NULL);
}

static void
pv_runtime_adverb_regenerate_ld_so_cache (PvRuntime *self,
                                          FlatpakBwrap *adverb_argv)
{
  g_autoptr(GString) ldlp_after_regen = g_string_new ("");
  g_autofree gchar *regen_dir = NULL;
  gsize i;

  /* This directory was set up in bind_runtime_ld_so() */
  if (self->is_flatpak_env)
    {
      const gchar *xrd;

      /* As in bind_runtime_ld_so(), we expect Flatpak to provide this
       * in practice, even if the host system does not. */
      xrd = g_environ_getenv (self->original_environ, "XDG_RUNTIME_DIR");
      g_return_if_fail (xrd != NULL);

      regen_dir = g_build_filename (xrd, "pressure-vessel", "ldso", NULL);
    }
  else
    {
      regen_dir = g_strdup ("/run/pressure-vessel/ldso");
    }

  flatpak_bwrap_add_args (adverb_argv,
                          "--regenerate-ld.so-cache", regen_dir,
                          NULL);

  /* This logic to build the search path matches
   * pv_runtime_set_search_paths(), except that here, we split them up:
   * the directories containing SONAMEs go in ld.so.conf, and only the
   * directories containing aliases go in LD_LIBRARY_PATH. */
  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      g_autofree gchar *ld_path = NULL;
      g_autofree gchar *aliases = NULL;

      ld_path = g_build_filename (self->overrides_in_container, "lib",
                                  pv_multiarch_tuples[i], NULL);

      aliases = g_build_filename (self->overrides_in_container, "lib",
                                  pv_multiarch_tuples[i], "aliases", NULL);

      flatpak_bwrap_add_args (adverb_argv,
                              "--add-ld.so-path", ld_path,
                              NULL);

      /* If we are not operating from a mutable sysroot, then we do not
       * have the opportunity to delete the runtime's version of overridden
       * libraries, so ldconfig will see both the provider's version and
       * the runtime's version. If the runtime's version has an OS ABI tag
       * and the provider's version does not, then ldconfig will prioritize
       * the runtime's older version. Work around this by adding the
       * provider's version to LD_LIBRARY_PATH *as well as* regenerating
       * the ld.so.cache - this will not work for games that incorrectly
       * reset the LD_LIBRARY_PATH, but is better than nothing! */
      if (self->mutable_sysroot == NULL)
        pv_search_path_append (ldlp_after_regen, ld_path);

      pv_search_path_append (ldlp_after_regen, aliases);
    }

  flatpak_bwrap_add_args (adverb_argv,
                          "--set-ld-library-path", ldlp_after_regen->str,
                          NULL);
}

/* If we are using a runtime, ensure the locales to be generated,
 * pass the lock fd to the executed process,
 * and make it act as a subreaper for the game itself.
 *
 * If we were using --unshare-pid then we could use bwrap --sync-fd
 * and rely on bubblewrap's init process for this, but we currently
 * can't do that without breaking gameoverlayrender.so's assumptions,
 * and we want -adverb for its locale functionality anyway. */
gboolean
pv_runtime_get_adverb (PvRuntime *self,
                       FlatpakBwrap *bwrap)
{
  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  /* This will be true if pv_runtime_bind() was successfully called. */
  g_return_val_if_fail (self->adverb_in_container != NULL, FALSE);
  g_return_val_if_fail (bwrap != NULL, FALSE);
  g_return_val_if_fail (flatpak_bwrap_is_empty (bwrap), FALSE);
  g_return_val_if_fail (!pv_bwrap_was_finished (bwrap), FALSE);

  flatpak_bwrap_add_arg (bwrap, self->adverb_in_container);

  if (self->flags & PV_RUNTIME_FLAGS_GENERATE_LOCALES)
    flatpak_bwrap_add_args (bwrap, "--generate-locales", NULL);

  if (pv_bwrap_lock_is_ofd (self->runtime_lock))
    {
      int fd = pv_bwrap_lock_steal_fd (self->runtime_lock);
      g_autofree gchar *fd_str = NULL;

      g_debug ("Passing lock fd %d down to adverb", fd);
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
       * so it will be released on fork(). Tell the adverb process
       * to take out its own compatible lock instead. There will be
       * a short window during which we have lost our lock but the
       * adverb process has not taken its lock - that's unavoidable
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

  pv_runtime_adverb_regenerate_ld_so_cache (self, bwrap);

  return TRUE;
}

/*
 * Set self->container_access_adverb to a (possibly empty) command prefix
 * that will result in the container being available at
 * self->container_access, with write access to self->overrides, and
 * read-only access to everything else.
 */
static gboolean
pv_runtime_provide_container_access (PvRuntime *self,
                                     GError **error)
{
  if (self->container_access_adverb != NULL)
    return TRUE;

  if (!self->runtime_is_just_usr)
    {
      static const char * const need_top_level[] =
      {
        "bin",
        "etc",
        "lib",
        "sbin",
      };
      gsize i;

      /* If we are working with a runtime that has a root directory containing
       * /etc and /usr, we can just access it via its path - that's "the same
       * shape" that the final system is going to be.
       *
       * In particular, if we are working with a writeable copy of a runtime
       * that we are editing in-place, it's always like that. */
      g_info ("%s: Setting up runtime without using bwrap",
              G_STRFUNC);
      self->container_access_adverb = flatpak_bwrap_new (NULL);
      self->container_access = g_strdup (self->runtime_files);

      /* This is going to go poorly for us if the runtime is not complete.
       * !self->runtime_is_just_usr means we know it has a /usr subdirectory,
       * but that doesn't guarantee that it has /bin, /lib, /sbin (either
       * in the form of real directories or symlinks into /usr) and /etc
       * (for at least /etc/alternatives and /etc/ld.so.cache).
       *
       * This check is not intended to be exhaustive, merely something
       * that will catch obvious mistakes like completely forgetting to
       * add the merged-/usr symlinks.
       *
       * In practice we also need /lib64 for 64-bit-capable runtimes,
       * but a pure 32-bit runtime would legitimately not have that,
       * so we don't check for it. */
      for (i = 0; i < G_N_ELEMENTS (need_top_level); i++)
        {
          g_autofree gchar *path = g_build_filename (self->runtime_files,
                                                     need_top_level[i],
                                                     NULL);

          if (!g_file_test (path, G_FILE_TEST_IS_DIR))
            g_warning ("%s does not exist, this probably won't work",
                       path);
        }
    }
  else
    {
      g_autofree gchar *etc = NULL;
      g_autofree gchar *etc_dest = NULL;

      /* If we're in FEX-Emu or similar, then we require a mutable sysroot,
       * but a mutable sysroot is never just /usr. */
      g_return_val_if_fail (!(self->flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT), FALSE);

      if (self->bubblewrap == NULL)
        return glnx_throw (error,
                           "Cannot run bubblewrap to set up runtime");

      /* Otherwise, will we need to use bwrap to build a directory hierarchy
       * that is the same shape as the final system. */
      g_info ("%s: Using bwrap to set up runtime that is just /usr",
              G_STRFUNC);

      /* By design, writeable copies of the runtime never need this:
       * the writeable copy is a complete sysroot, not just a merged /usr. */
      g_assert (self->mutable_sysroot == NULL);
      g_assert (self->tmpdir != NULL);

      self->container_access = g_build_filename (self->tmpdir, "mnt", NULL);
      g_mkdir (self->container_access, 0700);

      self->container_access_adverb = flatpak_bwrap_new (NULL);
      flatpak_bwrap_add_args (self->container_access_adverb,
                              self->bubblewrap,
                              "--ro-bind", "/", "/",
                              "--bind", self->overrides, self->overrides,
                              "--tmpfs", self->container_access,
                              NULL);

      if (!pv_bwrap_bind_usr (self->container_access_adverb,
                              self->runtime_files_on_host,
                              self->runtime_files_fd,
                              self->container_access,
                              error))
        return FALSE;

      /* For simplicity we bind all of /etc here */
      etc = g_build_filename (self->runtime_files_on_host,
                              "etc", NULL);
      etc_dest = g_build_filename (self->container_access,
                                   "etc", NULL);
      flatpak_bwrap_add_args (self->container_access_adverb,
                              "--ro-bind", etc, etc_dest,
                              NULL);
    }

  return TRUE;
}

static FlatpakBwrap *
pv_runtime_get_capsule_capture_libs (PvRuntime *self,
                                     RuntimeArchitecture *arch)
{
  const gchar *ld_library_path;
  g_autofree gchar *remap_app = NULL;
  g_autofree gchar *remap_usr = NULL;
  g_autofree gchar *remap_lib = NULL;
  FlatpakBwrap *ret;

  g_return_val_if_fail (self->provider != NULL, NULL);

  ret = pv_bwrap_copy (self->container_access_adverb);

  /* If we have a custom "LD_LIBRARY_PATH", we want to preserve
   * it when calling capsule-capture-libs */
  ld_library_path = g_environ_getenv (self->original_environ, "LD_LIBRARY_PATH");
  if (ld_library_path != NULL)
    flatpak_bwrap_set_env (ret, "LD_LIBRARY_PATH", ld_library_path, TRUE);

  /* Every symlink that starts with exactly /app/ (for Flatpak) */
  remap_app = g_strjoin (NULL, "/app/", "=",
                         self->provider->path_in_container_ns,
                         "/app/", NULL);

  /* Every symlink that starts with exactly /usr/ */
  remap_usr = g_strjoin (NULL, "/usr/", "=",
                         self->provider->path_in_container_ns,
                         "/usr/", NULL);

  /* Every symlink that starts with /lib, e.g. /lib64 */
  remap_lib = g_strjoin (NULL, "/lib", "=",
                         self->provider->path_in_container_ns,
                         "/lib", NULL);

  flatpak_bwrap_add_args (ret,
                          arch->capsule_capture_libs,
                          "--container", self->container_access,
                          "--remap-link-prefix", remap_app,
                          "--remap-link-prefix", remap_usr,
                          "--remap-link-prefix", remap_lib,
                          "--provider",
                            self->provider->path_in_current_ns,
                          NULL);

  if (self->libcapsule_knowledge)
    flatpak_bwrap_add_args (ret,
                            "--library-knowledge", self->libcapsule_knowledge,
                            NULL);

  return ret;
}

static gboolean pv_runtime_capture_libraries (PvRuntime *self,
                                              RuntimeArchitecture *arch,
                                              const char *destination,
                                              const char *profiling_message,
                                              const char * const *patterns,
                                              gsize n_patterns,
                                              GError **error);

static gboolean
collect_s2tc (PvRuntime *self,
              RuntimeArchitecture *arch,
              const char *libdir,
              GError **error)
{
  g_autofree gchar *s2tc = g_build_filename (libdir, "libtxc_dxtn.so", NULL);
  g_autofree gchar *s2tc_in_current_namespace = NULL;

  g_return_val_if_fail (self->provider != NULL, FALSE);

  s2tc_in_current_namespace = g_build_filename (self->provider->path_in_current_ns,
                                                s2tc, NULL);

  if (g_file_test (s2tc_in_current_namespace, G_FILE_TEST_EXISTS))
    {
      g_autofree gchar *expr = NULL;

      g_debug ("Collecting s2tc \"%s\" and its dependencies...", s2tc);
      expr = g_strdup_printf ("path-match:%s", s2tc);

      if (!pv_runtime_capture_libraries (self, arch,
                                         arch->libdir_in_current_namespace, expr,
                                         (const char * const *) &expr, 1, error))
        return FALSE;
    }

  return TRUE;
}

typedef enum
{
  ICD_KIND_NONEXISTENT,
  ICD_KIND_ABSOLUTE,
  ICD_KIND_SONAME,
  ICD_KIND_META_LAYER,
} IcdKind;

static const char *
icd_kind_to_string (IcdKind kind)
{
  switch (kind)
    {
      case ICD_KIND_NONEXISTENT:
        return "does not exist";

      case ICD_KIND_ABSOLUTE:
        return "absolute path";

      case ICD_KIND_SONAME:
        return "SONAME";

      case ICD_KIND_META_LAYER:
        return "Vulkan meta-layer";

      default:
        g_return_val_if_reached ("(internal error!)");
    }
}

typedef struct
{
  /* (type SrtEglIcd) or (type SrtVulkanIcd) or (type SrtVdpauDriver)
   * or (type SrtVaApiDriver) or (type SrtVulkanLayer) or
   * (type SrtDriDriver) */
  gpointer icd;
  /* Some sort of name borrowed from icd */
  const char *debug_name;
  /* Either SONAME, or absolute path in the provider's namespace.
   * Keyed by the index of a multiarch tuple in multiarch_tuples. */
  gchar *resolved_libraries[PV_N_SUPPORTED_ARCHITECTURES];
  /* Keyed by the index of a multiarch tuple in multiarch_tuples. */
  IcdKind kinds[PV_N_SUPPORTED_ARCHITECTURES];
  gchar *paths_in_container[PV_N_SUPPORTED_ARCHITECTURES];
} IcdDetails;

static IcdDetails *
icd_details_new (gpointer icd)
{
  IcdDetails *self;
  gsize i;
  const char *name;

  g_return_val_if_fail (G_IS_OBJECT (icd), NULL);

  if (SRT_IS_DRI_DRIVER (icd))
    name = srt_dri_driver_get_library_path (icd);
  else if (SRT_IS_EGL_ICD (icd))
    name = srt_egl_icd_get_json_path (icd);
  else if (SRT_IS_EGL_EXTERNAL_PLATFORM (icd))
    name = srt_egl_external_platform_get_json_path (icd);
  else if (SRT_IS_VULKAN_ICD (icd))
    name = srt_vulkan_icd_get_json_path (icd);
  else if (SRT_IS_VULKAN_LAYER (icd))
    name = srt_vulkan_layer_get_json_path (icd);
  else if (SRT_IS_VDPAU_DRIVER (icd))
    name = srt_vdpau_driver_get_library_path (icd);
  else if (SRT_IS_VA_API_DRIVER (icd))
    name = srt_va_api_driver_get_library_path (icd);
  else
    g_return_val_if_reached (NULL);

  self = g_slice_new0 (IcdDetails);
  self->icd = g_object_ref (icd);
  self->debug_name = name;

  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      self->resolved_libraries[i] = NULL;
      self->kinds[i] = ICD_KIND_NONEXISTENT;
      self->paths_in_container[i] = NULL;
    }

  return self;
}

static void
icd_details_free (IcdDetails *self)
{
  gsize i;

  g_object_unref (self->icd);

  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      g_free (self->resolved_libraries[i]);
      g_free (self->paths_in_container[i]);
    }

  g_slice_free (IcdDetails, self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (IcdDetails, icd_details_free)

/*
 * pv_runtime_capture_libraries:
 * @self: (not nullable): The runtime
 * @arch: (not nullable): An architecture
 * @destination: (not nullable): Where to capture the libraries
 * @profiling_message: (nullable): Description of this operation, for
 *  profiling. If %NULL, the profiling timer is not used.
 * @patterns: (not nullable) (array length=n_patterns): Patterns for capsule-capture-libs
 * @n_patterns: Number of patterns in @patterns, which must be greater than 0
 * @error: Used to return an error on failure
 *
 * Use capsule-capture-libs to capture libraries for architecture @arch
 * matching @patterns, creating symlinks in @destination.
 *
 * Returns: %TRUE on success
 */
static gboolean
pv_runtime_capture_libraries (PvRuntime *self,
                              RuntimeArchitecture *arch,
                              const gchar *destination,
                              const char *profiling_message,
                              const char * const *patterns,
                              gsize n_patterns,
                              GError **error)
{
  g_autoptr(FlatpakBwrap) temp_bwrap = NULL;
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer = NULL;
  gsize i;

  g_return_val_if_fail (self->provider != NULL, FALSE);
  g_return_val_if_fail (runtime_architecture_check_valid (arch), FALSE);
  g_return_val_if_fail (destination != NULL, FALSE);
  g_return_val_if_fail (n_patterns > 0, FALSE);
  g_return_val_if_fail (patterns != NULL, FALSE);

  if (profiling_message != NULL)
    timer = _srt_profiling_start ("%s", profiling_message);

  if (!pv_runtime_provide_container_access (self, error))
    return FALSE;

  temp_bwrap = pv_runtime_get_capsule_capture_libs (self, arch);
  flatpak_bwrap_add_args (temp_bwrap, "--dest", destination, NULL);

  for (i = 0; i < n_patterns; i++)
    flatpak_bwrap_add_arg (temp_bwrap, patterns[i]);

  flatpak_bwrap_finish (temp_bwrap);

  if (!pv_bwrap_run_sync (temp_bwrap, NULL, error))
    return FALSE;

  return TRUE;
}

/*
 * @requested_subdir: (not nullable):
 * @details_arr: (array length=n_details):
 * @n_details: Number of entries in @details_arr
 * @use_numbered_subdirs: (inout) (not optional): if %TRUE, use a
 *  numbered subdirectory per ICD, for the rare case where not all
 *  drivers have a unique basename or where order matters
 * @libdir_patterns: (inout) (not nullable): array of patterns for
 *  capsule-capture-libs whose destination will be `${libdir}`
 * @search_path: (nullable): Add the parent directory of the resulting
 *  ICD to this search path if necessary
 *
 * For each driver in @details_arr that is an absolute path, put a symlink
 * in `${libdir}/${requested_subdir}` or `${libdir}/${requested_subdir}/${n}`.
 * Also add a pattern to @libdir_patterns that will capture
 * its dependencies into `${libdir}`.
 *
 * For each driver in @details_arr that is a SONAME, instead add a pattern
 * to @libdir_patterns that will capture the driver and its dependencies
 * into `${libdir}`.
 *
 * Change `details->kinds[multiarch_index]` from %ICD_KIND_NONEXISTENT
 * to %ICD_KIND_ABSOLUTE, %ICD_KIND_SONAME or %ICD_KIND_NONEXISTENT
 * as appropriate.
 */
static gboolean
bind_icds (PvRuntime *self,
           RuntimeArchitecture *arch,
           const char *subdir,
           IcdDetails **details_arr,
           gsize n_details,
           gboolean *use_numbered_subdirs,
           GPtrArray *libdir_patterns,
           GString *search_path,
           GError **error)
{
  static const char options[] = "if-exists:if-same-abi";
  g_autoptr(GHashTable) basename_set = NULL;
  /* If details_arr[i]->kinds[multiarch_index] is ICD_KIND_ABSOLUTE,
   * then basenames[i] is the basename of the file; otherwise %NULL. */
  g_autofree const char **basenames = NULL;
  /* If details_arr[i] will not be passed to capsule-capture-libs because
   * it represents an ICD_KIND_ABSOLUTE driver that is a hard link or
   * symlink to a driver that was already seen at position j < i,
   * then captured_instead[i] == j.
   * Otherwise captured_instead[i] = G_MAXSIZE. */
  g_autofree gsize *captured_instead = NULL;
  g_autofree gchar *subdir_in_current_namespace = NULL;
  glnx_autofd int subdir_fd = -1;
  gsize multiarch_index;
  gsize i;
  int digits = pv_count_decimal_digits (n_details);

  g_return_val_if_fail (self->provider != NULL, FALSE);
  g_return_val_if_fail (runtime_architecture_check_valid (arch), FALSE);
  g_return_val_if_fail (subdir != NULL, FALSE);
  g_return_val_if_fail (n_details == 0 || details_arr != NULL, FALSE);
  g_return_val_if_fail (use_numbered_subdirs != NULL, FALSE);
  g_return_val_if_fail (libdir_patterns != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  multiarch_index = arch->multiarch_index;

  /* Iterate through the drivers to classify them into ABSOLUTE, SONAME
   * or missing. Add the SONAMEs to @patterns. */
  for (i = 0; i < n_details; i++)
    {
      IcdDetails *details = details_arr[i];

      g_return_val_if_fail (details != NULL, FALSE);

      if (details->resolved_libraries[multiarch_index] == NULL)
        continue;

      g_return_val_if_fail (details->kinds[multiarch_index] == ICD_KIND_NONEXISTENT,
                            FALSE);
      g_return_val_if_fail (details->paths_in_container[multiarch_index] == NULL,
                            FALSE);

      g_info ("Capturing %s loadable module #%" G_GSIZE_FORMAT ": %s",
              subdir, i, details->debug_name);
      g_info ("Checking for implementation on %s: %s",
              arch->details->tuple,
              details->resolved_libraries[multiarch_index]);

      if (!g_path_is_absolute (details->resolved_libraries[multiarch_index]))
        {
          g_autofree gchar *pattern = NULL;

          g_debug ("Classified as SONAME");
          details->kinds[multiarch_index] = ICD_KIND_SONAME;

          pattern = g_strdup_printf ("even-if-older:%s:soname:%s",
                                     options,
                                     details->resolved_libraries[multiarch_index]);
          g_ptr_array_add (libdir_patterns, g_steal_pointer (&pattern));
          continue;
        }

      g_debug ("Classified as path-based");
      details->kinds[multiarch_index] = ICD_KIND_ABSOLUTE;

      /* We set subdir_in_current_namespace non-NULL if and only if at least
       * one driver is ICD_KIND_ABSOLUTE */
      if (subdir_in_current_namespace == NULL)
        subdir_in_current_namespace = g_build_filename (arch->libdir_in_current_namespace,
                                                        subdir, NULL);
    }

  /* If no driver was ICD_KIND_ABSOLUTE, then there is nothing more to do */
  if (subdir_in_current_namespace == NULL)
    goto success;

  if (g_mkdir_with_parents (subdir_in_current_namespace, 0700) != 0)
    return glnx_throw_errno_prefix (error, "Unable to create %s",
                                    subdir_in_current_namespace);

  if (!glnx_opendirat (-1, subdir_in_current_namespace, TRUE,
                       &subdir_fd, error))
    return FALSE;

  /* Decide whether we need to use numbered subdirectories.
   * If there are file collisions, then the answer is yes we do:
   * .../glvnd/0/libEGL_example.so -> /usr/.../libEGL_example.so,
   * .../glvnd/1/libEGL_example.so -> /opt/.../libEGL_example.so,
   * and so on. If not (common case), we can use a single directory:
   * .../glvnd/libEGL_one.so -> /usr/.../libEGL_one.so,
   * .../glvnd/libEGL_two.so -> /opt/.../libEGL_two.so,
   * and so on. */
  basename_set = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);
  basenames = g_new0 (const char *, n_details);

  for (i = 0; i < n_details && !*use_numbered_subdirs; i++)
    {
      IcdDetails *details = details_arr[i];
      const char *base;

      if (details->kinds[multiarch_index] != ICD_KIND_ABSOLUTE)
        continue;

      base = glnx_basename (details->resolved_libraries[multiarch_index]);
      basenames[i] = base;

      if (g_hash_table_contains (basename_set, base))
        {
          /* The ICD is (at least potentially) going to collide with
           * another from this batch */
          *use_numbered_subdirs = TRUE;
        }
      else
        {
          g_autofree gchar *path = g_build_filename (subdir_in_current_namespace,
                                                     base, NULL);

          g_hash_table_add (basename_set, (char *) base);

          /* The ICD would collide with one that we already set up */
          if (g_file_test (path, G_FILE_TEST_IS_SYMLINK))
            *use_numbered_subdirs = TRUE;
        }
    }

  /* If we've decided there are no collisions, then we can process all
   * drivers as a single batch, because they're all going to the same
   * place. */
  if (!*use_numbered_subdirs)
    {
      g_autoptr(GPtrArray) patterns = g_ptr_array_new_full (n_details, g_free);
      /* (element-type (struct stat) gsize)
       * Key: Identity of a file
       * Value: Index of first ICD_KIND_ABSOLUTE in details_arr[] and
       *  basenames[] that is a symlink or hard link to that file */
      g_autoptr(GHashTable) unique_drivers = NULL;

      unique_drivers = g_hash_table_new_full (_srt_struct_stat_devino_hash,
                                              _srt_struct_stat_devino_equal,
                                              g_free,
                                              NULL);
      captured_instead = g_new (gsize, n_details);

      for (i = 0; i < n_details; i++)
        captured_instead[i] = G_MAXSIZE;

      for (i = 0; i < n_details; i++)
        {
          IcdDetails *details = details_arr[i];
          g_autofree gchar *pattern = NULL;
          struct stat stat_buf;
          glnx_autofd int fd = -1;

          if (details->kinds[multiarch_index] != ICD_KIND_ABSOLUTE)
            continue;

          fd = _srt_resolve_in_sysroot (self->provider->fd,
                                        details->resolved_libraries[multiarch_index],
                                        SRT_RESOLVE_FLAGS_NONE,
                                        NULL, NULL);

          if (fd >= 0 && fstat (fd, &stat_buf) == 0)
            {
              gpointer value;

              if (g_hash_table_lookup_extended (unique_drivers, &stat_buf,
                                                NULL, &value))
                {
                  gsize other = GPOINTER_TO_SIZE (value);

                  /* @details points to a different name (hard link or
                   * symlink) for the same file as @driver, so we can
                   * capture it just once (with the name driver->captured_as),
                   * and then duplicate that symlink for the other items
                   * of driver->other_names. */
                  g_assert (other < i);
                  captured_instead[i] = other;
                  continue;
                }

              g_hash_table_replace (unique_drivers,
                                    g_memdup2 (&stat_buf, sizeof (struct stat)),
                                    GSIZE_TO_POINTER (i));
            }
          else
            {
              g_warning ("Unable to look up resolved path \"%s\" in provider",
                         details->resolved_libraries[multiarch_index]);
            }

          pattern = g_strdup_printf ("no-dependencies:even-if-older:%s:path:%s",
                                     options,
                                     details->resolved_libraries[multiarch_index]);
          g_ptr_array_add (patterns, g_steal_pointer (&pattern));
        }

      if (patterns->len > 0
          && !pv_runtime_capture_libraries (self, arch, subdir_in_current_namespace,
                                            subdir_in_current_namespace,
                                            (const char * const *) patterns->pdata,
                                            patterns->len, error))
        return FALSE;
    }

  /* Finish the per-driver processing. If we're using numbered
   * subdirectories, this includes the actual captures; if not, this
   * is just cleanup. */
  for (i = 0; i < n_details; i++)
    {
      IcdDetails *details = details_arr[i];
      g_autofree gchar *numbered_subdir = NULL;
      g_autofree gchar *dependency_pattern = NULL;
      g_autofree gchar *seq_str = NULL;
      glnx_autofd int numbered_subdir_fd = -1;
      const char *dest_in_current_namespace = NULL;
      const char *base;
      int dest_fd;
      struct stat stat_buf;

      if (details->kinds[multiarch_index] != ICD_KIND_ABSOLUTE)
        continue;

      base = basenames[i];

      if (base == NULL)
        {
          base = glnx_basename (details->resolved_libraries[multiarch_index]);
          basenames[i] = base;
        }

      if (captured_instead != NULL)
        {
          gsize other = captured_instead[i];

          /* We only do this if all the basenames are unique, and therefore
           * we are not using numbered subdirectories */
          g_assert (!*use_numbered_subdirs);

          /* If icd_details[i] is a hard link or symlink to the same
           * ICD_KIND_ABSOLUTE file as icd_details[other], then we can
           * treat it as equivalent. We don't need to run capsule-capture-libs
           * again, because it would create a symlink for icd_details[i]
           * if and only if it would have done so for icd_details[other]. */
          if (other != G_MAXSIZE)
            {
              g_autofree gchar *target = NULL;

              g_assert (other < i);
              g_assert (basenames[other] != NULL);
              g_debug ("\"%s\" is the same driver as \"%s\"",
                       base, basenames[other]);
              target = glnx_readlinkat_malloc (subdir_fd,
                                               basenames[other],
                                               NULL, NULL);

              if (target == NULL)
                {
                  g_debug ("\"%s\" was not created: not creating \"%s\" either",
                           basenames[other], base);
                  details->kinds[multiarch_index] = ICD_KIND_NONEXISTENT;
                }
              else
                {
                  g_debug ("\"%s\" was created: making \"%s\" equivalent",
                           basenames[other], base);

                  if (!pv_runtime_symlinkat (target, subdir_fd, base, error))
                    return FALSE;
                }

              /* We don't need to capture the dependencies of icd_details[i],
               * because we are already going to capture the dependencies of
               * icd_details[other], and they are the same file */
              continue;
            }
        }

      /* If we can't avoid the numbered subdirectory, or want to use one
       * to force a specific load order, create it. */
      if (*use_numbered_subdirs && subdir[0] != '\0')
        {
          g_autofree gchar *pattern = NULL;

          seq_str = g_strdup_printf ("%.*" G_GSIZE_FORMAT, digits, i);
          numbered_subdir = g_build_filename (subdir_in_current_namespace,
                                              seq_str, NULL);

          if (!glnx_ensure_dir (subdir_fd, seq_str, 0700, error))
            {
              g_prefix_error (error, "Unable to create \"%s\": ",
                              numbered_subdir);
              return FALSE;
            }

          if (!glnx_opendirat (subdir_fd, seq_str, TRUE,
                               &numbered_subdir_fd, error))
            return FALSE;

          dest_in_current_namespace = numbered_subdir;
          dest_fd = numbered_subdir_fd;
          pattern = g_strdup_printf ("no-dependencies:even-if-older:%s:path:%s",
                                     options,
                                     details->resolved_libraries[multiarch_index]);

          if (!pv_runtime_capture_libraries (self, arch, dest_in_current_namespace, pattern,
                                             (const char * const *) &pattern, 1, error))
            return FALSE;
        }
      else
        {
          dest_in_current_namespace = subdir_in_current_namespace;
          dest_fd = subdir_fd;
        }

      if (fstatat (dest_fd, base, &stat_buf, AT_SYMLINK_NOFOLLOW) < 0)
        {
          g_debug ("\"%s/%s\" was not created: %s",
                   dest_in_current_namespace, base, g_strerror (errno));

          /* capsule-capture-libs didn't actually create the symlink,
           * which means the ICD is nonexistent or the wrong architecture.
           * We don't need to capture the dependencies in this case. */
          details->kinds[multiarch_index] = ICD_KIND_NONEXISTENT;
          /* If the directory is empty we can also remove it.
           * This is opportunistic, so ignore ENOTEMPTY. */
          if (numbered_subdir != NULL)
            g_rmdir (numbered_subdir);

          continue;
        }
      else if (!S_ISLNK (stat_buf.st_mode))
        {
          /* This is unexpected! capsule-capture-libs creates symlinks,
           * not any other sort of file */
          g_warning ("\"%s/%s\" was created but not as a symlink (%o)",
                     dest_in_current_namespace, base, stat_buf.st_mode);
        }

      /* Only add the numbered subdirectories to the search path. Their
       * parent is expected to be there already. */
      if (search_path != NULL && seq_str != NULL)
        {
          g_autofree gchar *in_container = NULL;

          in_container = g_build_filename (arch->libdir_in_container,
                                           subdir, seq_str, NULL);
          pv_search_path_append (search_path, in_container);
        }

      dependency_pattern = g_strdup_printf ("only-dependencies:%s:path:%s",
                                            options,
                                            details->resolved_libraries[multiarch_index]);
      g_ptr_array_add (libdir_patterns, g_steal_pointer (&dependency_pattern));

      details->paths_in_container[multiarch_index] = g_build_filename (arch->libdir_in_container,
                                                                       subdir,
                                                                       seq_str ? seq_str : "",
                                                                       base,
                                                                       NULL);
    }

success:
  for (i = 0; i < n_details; i++)
    {
      IcdDetails *details = details_arr[i];
      const char *type_str = icd_kind_to_string (details->kinds[multiarch_index]);

      g_info ("Captured %s loadable module #%" G_GSIZE_FORMAT ": %s",
              subdir, i, details->debug_name);
      g_info ("Implementation on %s: %s",
              arch->details->tuple, type_str);
    }

  return TRUE;
}

static gboolean
bind_gfx_provider (PvRuntime *self,
                   FlatpakBwrap *bwrap,
                   const char *prepend_path,
                   GError **error)
{
  g_autofree gchar *provider_etc = NULL;
  g_autofree gchar *mount_point = NULL;

  mount_point = g_build_filename (prepend_path,
                                  self->provider->path_in_container_ns,
                                  NULL);

  if (!pv_bwrap_bind_usr (bwrap,
                          self->provider->path_in_host_ns,
                          self->provider->fd,
                          mount_point,
                          error))
    return FALSE;

  provider_etc = g_build_filename (self->provider->path_in_current_ns,
                                   "etc", NULL);

  if (g_file_test (provider_etc, G_FILE_TEST_IS_DIR))
    {
      g_autofree gchar *in_host = NULL;
      g_autofree gchar *in_container = NULL;

      in_host = g_build_filename (self->provider->path_in_host_ns,
                                  "etc", NULL);
      in_container = g_build_filename (mount_point, "etc", NULL);
      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind", in_host, in_container,
                              NULL);
    }

  return TRUE;
}

static gboolean
bind_runtime_base (PvRuntime *self,
                   FlatpakExports *exports,
                   FlatpakBwrap *bwrap,
                   PvEnviron *container_env,
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
    "/etc/asound.conf",
    "/etc/ld.so.cache",
    "/etc/ld.so.conf",
    "/etc/localtime",
    "/etc/machine-id",
    "/var/cache/ldconfig",
    "/var/lib/dbus",
    "/var/lib/dhcp",
    "/var/lib/sudo",
    "/var/lib/urandom",
    NULL
  };
  static const char * const from_host[] =
  {
    /* TODO: Synthesize a passwd with only the user and nobody,
     * like Flatpak does? */
    "/etc/group",
    "/etc/passwd",
    "/etc/host.conf",
    "/etc/hosts",
    "/etc/resolv.conf",
    NULL
  };
  static const char * const from_provider[] =
  {
    "/etc/amd",
    "/etc/drirc",
    "/etc/nvidia",
    "/run/bumblebee.socket",
    NULL
  };
  static const char * const redirect_into_interpreter_root[] =
  {
    "/etc/alternatives",
    "/etc/ld.so.conf.d",
    NULL
  };
  g_autofree gchar *xrd = g_strdup_printf ("/run/user/%ld", (long) geteuid ());
  gsize i;

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (exports != NULL, FALSE);
  g_return_val_if_fail (!pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (container_env != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (self->flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
    {
      /* If we're in an emulator like FEX-Emu, we need to use the host
       * OS's /usr as our real root directory, and set the runtime up
       * in a different directory. */

      if (!pv_bwrap_bind_usr (bwrap, "/", self->root_fd, "/", error))
        return FALSE;

      /* We need at least a subset of the host's /etc, for ld.so.cache
       * and so on. For now, we only support host OSs that use the
       * interoperable path; OS-specific variant paths like the ones in
       * ClearLinux and Exherbo could be added later if required. */
      flatpak_bwrap_add_args (bwrap,
                              "--symlink", "/run/host/etc/alternatives", "/etc/alternatives",
                              "--symlink", "/run/host/etc/ld.so.cache", "/etc/ld.so.cache",
                              "--symlink", "/run/host/etc/ld.so.conf", "/etc/ld.so.conf",
                              "--symlink", "/run/host/etc/ld.so.conf.d", "/etc/ld.so.conf.d",
                              NULL);

      if (!pv_bwrap_bind_usr (bwrap,
                              self->runtime_files_on_host,
                              self->runtime_files_fd,
                              PV_RUNTIME_PATH_INTERPRETER_ROOT, error))
        return FALSE;

      /* Force FEX-Emu to use this root filesystem instead of the one
       * it would "naturally" have used. Parts of it will be symlinks
       * into /run/gfx, which contains bind-mounts from FEX-Emu's original
       * rootfs.
       *
       * We cannot do this via pv_environ_setenv(), since that sets the
       * environment in which we execute pv-bwrap, but that needs to be
       * using the old environment to find the rootfs, since it has not
       * pivoted its root directory yet.
       *
       * TODO: Generalize this to other interpreters/emulators */
      flatpak_bwrap_add_args (bwrap,
                              "--setenv", "FEX_ROOTFS", PV_RUNTIME_PATH_INTERPRETER_ROOT,
                              NULL);
    }
  else
    {
      if (!pv_bwrap_bind_usr (bwrap,
                              self->runtime_files_on_host,
                              self->runtime_files_fd,
                              "/", error))
        return FALSE;
    }

  /* In the case where we have a mutable sysroot, we mount the overrides
   * as part of /usr. Make /overrides a symbolic link, to be nice to
   * older steam-runtime-tools versions. */

  if (self->mutable_sysroot != NULL)
    {
      g_autoptr(GError) local_error = NULL;

      g_assert (self->overrides_in_container[0] == '/');
      g_assert (g_strcmp0 (self->overrides_in_container, "/overrides") != 0);

      if (!pv_runtime_make_symlink_in_container (self, bwrap,
                                                 &self->overrides_in_container[1],
                                                 "/overrides",
                                                 MAKE_SYMLINK_FLAGS_INTERPRETER_ROOT,
                                                 &local_error))
        g_warning ("%s", local_error->message);

      /* Also make a matching symbolic link on disk, to make it easier
       * to inspect the sysroot. */
      if (TEMP_FAILURE_RETRY (symlinkat (&self->overrides_in_container[1],
                                         self->mutable_sysroot_fd,
                                         "overrides")) != 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to create symlink \"%s/overrides\" -> \"%s\"",
                                        self->mutable_sysroot,
                                        &self->overrides_in_container[1]);
    }

  flatpak_bwrap_add_args (bwrap,
                          "--dir", "/tmp",
                          "--dir", "/var",
                          "--dir", "/var/tmp",
                          "--symlink", "../run", "/var/run",
                          NULL);

  pv_environ_setenv (container_env, "XDG_RUNTIME_DIR", xrd);

  if (self->provider != NULL
      && (g_strcmp0 (self->provider->path_in_host_ns, "/") != 0
          || g_strcmp0 (self->provider->path_in_container_ns, "/run/host") != 0))
    {
      if (!bind_gfx_provider (self, bwrap, "/", error))
        return FALSE;

      /* Symlinks in the FEX-Emu rootfs are resolved as if it was
       * chrooted into that rootfs, so the graphics-stack provider needs
       * to exist inside the rootfs too. */
      if (self->flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT
          && !bind_gfx_provider (self, bwrap,
                                 PV_RUNTIME_PATH_INTERPRETER_ROOT,
                                 error))
        return FALSE;
    }

  for (i = 0; i < G_N_ELEMENTS (bind_mutable); i++)
    {
      g_autofree gchar *path = g_build_filename (self->runtime_files,
                                                 bind_mutable[i],
                                                 NULL);
      g_auto(SrtDirIter) dir = SRT_DIR_ITER_CLEARED;
      struct dirent *dent;

      if (!_srt_dir_iter_init_at (&dir, AT_FDCWD, path,
                                  SRT_DIR_ITER_FLAGS_FOLLOW,
                                  self->arbitrary_dirent_order,
                                  NULL))
        continue;

      while (_srt_dir_iter_next_dent (&dir, &dent, NULL, NULL) && dent != NULL)
        {
          const char *member = dent->d_name;
          g_autofree gchar *dest = g_build_filename ("/", bind_mutable[i],
                                                     member, NULL);
          g_autofree gchar *full = NULL;
          g_autofree gchar *target = NULL;

          if (g_strv_contains (dont_bind, dest))
            continue;

          if (g_strv_contains (from_host, dest))
            continue;

          if (self->provider != NULL && g_strv_contains (from_provider, dest))
            continue;

          if ((self->flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
              && g_str_has_prefix (dest, "/etc")
              && g_strv_contains (redirect_into_interpreter_root, dest))
            {
              g_autofree gchar *original_dest = g_steal_pointer (&dest);

              /* We have to distinguish between the real /etc, used for
               * FEX-Emu or a similar interpreter/emulator, and the /etc
               * used for the emulated process. The former is a 1:1 copy
               * of the real /etc, but the latter is controlled by us. */
              dest = g_build_filename (PV_RUNTIME_PATH_INTERPRETER_ROOT,
                                       original_dest, NULL);
            }

          full = g_build_filename (self->runtime_files,
                                   bind_mutable[i],
                                   member,
                                   NULL);
          target = glnx_readlinkat_malloc (-1, full, NULL, NULL);

          if (target != NULL)
            {
              flatpak_bwrap_add_args (bwrap, "--symlink", target, dest, NULL);
            }
          else
            {
              /* We will run bwrap in the host system, so translate the path
               * if necessary */
              g_autofree gchar *on_host = pv_current_namespace_path_to_host_path (full);
              flatpak_bwrap_add_args (bwrap, "--ro-bind", on_host, dest, NULL);
            }
        }
    }

  /* If we are in a Flatpak environment, we need to test if these files are
   * available in the host, and not in the current environment, because we will
   * run bwrap in the host system */
  if (_srt_file_test_in_sysroot (self->host_in_current_namespace, self->host_fd,
                                 "/etc/machine-id", G_FILE_TEST_EXISTS))
    {
      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind", "/etc/machine-id", "/etc/machine-id",
                              "--symlink", "/etc/machine-id",
                              "/var/lib/dbus/machine-id",
                              NULL);
    }
  /* We leave this for completeness but in practice we do not expect to have
   * access to the "/var" host directory because Flatpak usually just binds
   * the host's "etc" and "usr". */
  else if (_srt_file_test_in_sysroot (self->host_in_current_namespace, self->host_fd,
                                      "/var/lib/dbus/machine-id",
                                      G_FILE_TEST_EXISTS))
    {
      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind", "/var/lib/dbus/machine-id",
                              "/etc/machine-id",
                              "--symlink", "/etc/machine-id",
                              "/var/lib/dbus/machine-id",
                              NULL);
    }

  for (i = 0; from_host[i] != NULL; i++)
    {
      const char *item = from_host[i];

      if (_srt_file_test_in_sysroot (self->host_in_current_namespace, self->host_fd,
                                     item, G_FILE_TEST_EXISTS))
        flatpak_bwrap_add_args (bwrap,
                                "--ro-bind", item, item,
                                NULL);
    }

  if (self->provider != NULL)
    {
      for (i = 0; from_provider[i] != NULL; i++)
        {
          const char *item = from_provider[i];
          g_autoptr(GError) local_error = NULL;
          g_autofree char *path_in_provider = NULL;
          glnx_autofd int fd = -1;

          fd = _srt_resolve_in_sysroot (self->provider->fd, item,
                                        SRT_RESOLVE_FLAGS_NONE,
                                        &path_in_provider,
                                        &local_error);

          if (fd >= 0)
            {
              g_autofree char *alloc_dest = NULL;
              g_autofree char *host_path = NULL;
              const char *dest = item;

              /* In FEX-Emu or similar, the graphics provider is only used
               * for the emulated architecture, so we put it in the
               * interpreter's overlay rather than in the real root
               * directory. */
              if (self->flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
                {
                  alloc_dest = g_build_filename (PV_RUNTIME_PATH_INTERPRETER_ROOT,
                                                 item, NULL);
                  dest = alloc_dest;
                }

              host_path = g_build_filename (self->provider->path_in_host_ns,
                                            path_in_provider, NULL);
              flatpak_bwrap_add_args (bwrap,
                                      "--ro-bind", host_path, dest,
                                      NULL);
            }
          else
            {
              g_debug ("Cannot resolve \"%s\" in \"%s\": %s",
                       item, self->provider->path_in_current_ns,
                       local_error->message);
              g_clear_error (&local_error);
            }
        }
    }

  return TRUE;
}

/*
 * Exactly as symlinkat(2), except that if the destination already exists,
 * it will be removed.
 */
static gboolean
pv_runtime_symlinkat (const gchar *target,
                      int destination_dirfd,
                      const gchar *destination,
                      GError **error)
{
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!glnx_shutil_rm_rf_at (destination_dirfd, destination, NULL, error))
    return FALSE;

  if (TEMP_FAILURE_RETRY (symlinkat (target, destination_dirfd, destination)) != 0)
    return glnx_throw_errno_prefix (error,
                                    "Unable to create symlink \".../%s\" -> \"%s\"",
                                    destination, target);

  return TRUE;
}

static gboolean
bind_runtime_ld_so (PvRuntime *self,
                    FlatpakBwrap *bwrap,
                    PvEnviron *container_env,
                    GError **error)
{
  gsize i, j;

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (bwrap == NULL || !pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (self->is_flatpak_env || bwrap != NULL, FALSE);
  g_return_val_if_fail (self->mutable_sysroot != NULL || !self->is_flatpak_env, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (self->is_flatpak_env)
    {
      const gchar *xrd = NULL;
      g_autofree gchar *ldso_runtime_dir = NULL;
      g_autofree gchar *xrd_ld_so_conf = NULL;
      g_autofree gchar *xrd_ld_so_cache = NULL;
      glnx_autofd int sysroot_etc_dirfd = -1;
      glnx_autofd int ldso_runtime_dirfd = -1;

      sysroot_etc_dirfd = _srt_resolve_in_sysroot (self->mutable_sysroot_fd,
                                                   "/etc",
                                                   SRT_RESOLVE_FLAGS_MKDIR_P,
                                                   NULL, error);
      if (sysroot_etc_dirfd < 0)
        return FALSE;

      /* Because we're running under Flatpak in this code path,
       * we expect that there is a XDG_RUNTIME_DIR even if the host system
       * doesn't provide one; and because we require Flatpak 1.11.1,
       * we can assume it's shared between our current sandbox and the
       * game's subsandbox, with the same path in both. */
      xrd = g_environ_getenv (self->original_environ, "XDG_RUNTIME_DIR");
      if (xrd == NULL)
        {
          g_warning ("The environment variable XDG_RUNTIME_DIR is not set, skipping regeneration of ld.so");
          return TRUE;
        }

      ldso_runtime_dir = g_build_filename (xrd, "pressure-vessel", "ldso", NULL);
      if (g_mkdir_with_parents (ldso_runtime_dir, 0700) != 0)
        return glnx_throw_errno_prefix (error, "Unable to create %s",
                                        ldso_runtime_dir);

      xrd_ld_so_conf = g_build_filename (ldso_runtime_dir, "ld.so.conf", NULL);
      xrd_ld_so_cache = g_build_filename (ldso_runtime_dir, "ld.so.cache", NULL);

      if (!glnx_opendirat (-1, ldso_runtime_dir, TRUE, &ldso_runtime_dirfd, error))
        return FALSE;

      /* Rename the original ld.so.cache and conf because we will create
       * symlinks in their places */
      if (!glnx_renameat (self->mutable_sysroot_fd, "etc/ld.so.cache",
                          self->mutable_sysroot_fd, "etc/runtime-ld.so.cache", error))
        return FALSE;
      if (!glnx_renameat (self->mutable_sysroot_fd, "etc/ld.so.conf",
                          self->mutable_sysroot_fd, "etc/runtime-ld.so.conf", error))
        return FALSE;

      if (!pv_runtime_symlinkat (xrd_ld_so_cache, self->mutable_sysroot_fd,
                                 "etc/ld.so.cache", error))
        return FALSE;
      if (!pv_runtime_symlinkat (xrd_ld_so_conf, self->mutable_sysroot_fd,
                                 "etc/ld.so.conf", error))
        return FALSE;

      /* Create a symlink to the runtime's version */
      if (!pv_runtime_symlinkat ("/etc/runtime-ld.so.cache", ldso_runtime_dirfd,
                                 "runtime-ld.so.cache", error))
        return FALSE;
      if (!pv_runtime_symlinkat ("/etc/runtime-ld.so.conf", ldso_runtime_dirfd,
                                 "runtime-ld.so.conf", error))
        return FALSE;

      /* Initially it's a symlink to the runtime's version and we rely on
       * LD_LIBRARY_PATH for our overrides, but -adverb will overwrite this
       * symlink */
      if (!pv_runtime_symlinkat ("runtime-ld.so.cache", ldso_runtime_dirfd,
                                 "ld.so.cache", error))
        return FALSE;
      if (!pv_runtime_symlinkat ("runtime-ld.so.conf", ldso_runtime_dirfd,
                                 "ld.so.conf", error))
        return FALSE;

      /* Initially we have the following situation:
       * ($XRD is an abbreviation for $XDG_RUNTIME_DIR)
       * ${mutable_sysroot}/etc/ld.so.cache -> $XRD/pressure-vessel/ldso/ld.so.cache
       * $XRD/pressure-vessel/ldso/ld.so.cache -> runtime-ld.so.cache
       * $XRD/pressure-vessel/ldso/runtime-ld.so.cache -> ${mutable_sysroot}/etc/runtime-ld.so.cache
       * ${mutable_sysroot}/etc/runtime-ld.so.cache is the original runtime's ld.so.cache
       *
       * After exectuting -adverb we expect the symlink $XRD/pressure-vessel/ldso/ld.so.cache
       * to be replaced with a newly generated ld.so.cache that incorporates the
       * necessary paths from LD_LIBRARY_PATH */
    }
  else
    {
      g_assert (bwrap != NULL);

#define MUTABLE_LDSO_DIR "/run/pressure-vessel/ldso"
      /* The absolute path to our modifiable ld.so.cache/.conf symlink,
       * as seen from inside the container and (if applicable) the
       * interpreter root. */
      const gchar *mutable_cache_path = MUTABLE_LDSO_DIR "/ld.so.cache";
      const gchar *mutable_conf_path = MUTABLE_LDSO_DIR "/ld.so.conf";
      /* The absolute path that will be loaded by processes inside the
       * container, as seen by bwrap in /oldroot (so in particular it
       * has the interpreter root prefix if necessary). */
      const gchar *canonical_cache_in_target;
      const gchar *canonical_conf_in_target;
      /* mutable_*_path as seen by bwrap in /newroot */
      const gchar *mutable_cache_in_target;
      const gchar *mutable_conf_in_target;
      /* The locations where we will bind-mount the runtime's
       * ld.so.cache/.conf, as seen by bwrap in /newroot */
      const gchar *runtime_cache_in_target;
      const gchar *runtime_conf_in_target;
      /* The location of the runtime's ld.so.cache/.conf, as seen by
       * bwrap in /oldroot */
      g_autofree gchar *ld_so_cache_on_host = NULL;
      g_autofree gchar *ld_so_conf_on_host = NULL;

      /* We only support runtimes that include /etc/ld.so.cache and
        * /etc/ld.so.conf at their interoperable path. */
      ld_so_cache_on_host = g_build_filename (self->runtime_files_on_host,
                                              "etc", "ld.so.cache", NULL);
      ld_so_conf_on_host = g_build_filename (self->runtime_files_on_host,
                                              "etc", "ld.so.conf", NULL);

      /* Unlike the rest of /etc, if we are running under an interpreter
       * like FEX-Emu, we put our ld.so.cache, ld.so.conf in the target
       * directory, not the root filesystem. This is because we need the
       * interpreter's overlay behaviour: for example, if running FEX-Emu
       * on aarch64, we need aarch64 processes like FEX-Emu itself to be
       * able to see the real host OS's aarch64 ld.so.cache, but the
       * emulated x86 executable that it's running needs to see our x86
       * ld.so.cache in the overlay.
       *
       * Similarly, the targets of all symlinks in the FEX-Emu rootfs
       * must themselves be in the FEX-Emu rootfs.
       *
       * Otherwise, they're the same as for the non-FEX code path, below. */
      if (self->flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
        {
          canonical_cache_in_target = PV_RUNTIME_PATH_INTERPRETER_ROOT "/etc/ld.so.cache";
          canonical_conf_in_target = PV_RUNTIME_PATH_INTERPRETER_ROOT "/etc/ld.so.conf";
          mutable_cache_in_target = PV_RUNTIME_PATH_INTERPRETER_ROOT MUTABLE_LDSO_DIR "/ld.so.cache";
          mutable_conf_in_target = PV_RUNTIME_PATH_INTERPRETER_ROOT MUTABLE_LDSO_DIR "/ld.so.conf";
          runtime_cache_in_target = PV_RUNTIME_PATH_INTERPRETER_ROOT MUTABLE_LDSO_DIR "/runtime-ld.so.cache";
          runtime_conf_in_target = PV_RUNTIME_PATH_INTERPRETER_ROOT MUTABLE_LDSO_DIR "/runtime-ld.so.conf";

          /* To make it a little easier to understand what's going on,
           * make MUTABLE_LDSO_DIR a symlink to the MUTABLE_LDSO_DIR inside
           * the rootfs. */
          flatpak_bwrap_add_args (bwrap,
                                  "--tmpfs",
                                  PV_RUNTIME_PATH_INTERPRETER_ROOT MUTABLE_LDSO_DIR,
                                  "--symlink",
                                  PV_RUNTIME_PATH_INTERPRETER_ROOT MUTABLE_LDSO_DIR,
                                  MUTABLE_LDSO_DIR,
                                  NULL);
        }
      else
        {
          canonical_cache_in_target = "/etc/ld.so.cache";
          canonical_conf_in_target = "/etc/ld.so.conf";
          mutable_cache_in_target = MUTABLE_LDSO_DIR "/ld.so.cache";
          mutable_conf_in_target = MUTABLE_LDSO_DIR "/ld.so.conf";
          runtime_cache_in_target = MUTABLE_LDSO_DIR "/runtime-ld.so.cache";
          runtime_conf_in_target = MUTABLE_LDSO_DIR "/runtime-ld.so.conf";

          flatpak_bwrap_add_args (bwrap,
                                  "--tmpfs", MUTABLE_LDSO_DIR,
                                  NULL);
        }

      flatpak_bwrap_add_args (bwrap,
                              /* We put the ld.so.cache somewhere that we can
                              * overwrite from inside the container by
                              * replacing the symlink. */
                              "--symlink",
                              mutable_cache_path, canonical_cache_in_target,
                              /* ... and the same for its configuration */
                              "--symlink",
                              mutable_conf_path, canonical_conf_in_target,

                              /* Initially it's a symlink to the runtime's
                              * version and we rely on LD_LIBRARY_PATH
                              * for our overrides, but -adverb will
                              * overwrite this symlink. */
                              "--symlink", "runtime-ld.so.cache", mutable_cache_in_target,
                              "--symlink", "runtime-ld.so.conf", mutable_conf_in_target,

                              /* Put the runtime's version in place too. */
                              "--ro-bind", ld_so_cache_on_host, runtime_cache_in_target,
                              "--ro-bind", ld_so_conf_on_host, runtime_conf_in_target,
                              NULL);

      /* glibc from some distributions will want to load the ld.so cache from
       * a distribution-specific path, e.g. Clear Linux uses
       * /var/cache/ldconfig/ld.so.cache. For simplicity, we make all these paths
       * symlinks, so that we only have to populate the cache in one place. */
      for (i = 0; pv_other_ld_so_cache[i] != NULL; i++)
        {
          g_autoptr(GError) local_error = NULL;
          const char *path = pv_other_ld_so_cache[i];

          if (!pv_runtime_make_symlink_in_container (self,
                                                     bwrap,
                                                     mutable_cache_path,
                                                     path,
                                                     MAKE_SYMLINK_FLAGS_INTERPRETER_ROOT,
                                                     &local_error))
            g_warning ("%s", local_error->message);
        }

      /* Similar, but for ld.so.conf, for example on Solus. */
      for (i = 0; pv_other_ld_so_conf[i] != NULL; i++)
        {
          g_autoptr(GError) local_error = NULL;
          const char *path = pv_other_ld_so_conf[i];

          if (!pv_runtime_make_symlink_in_container (self,
                                                     bwrap,
                                                     mutable_conf_path,
                                                     path,
                                                     MAKE_SYMLINK_FLAGS_INTERPRETER_ROOT,
                                                     &local_error))
            g_warning ("%s", local_error->message);
        }

      /* glibc from some distributions will want to load the ld.so cache from
       * a distribution- and architecture-specific path, e.g. Exherbo
       * does this. Again, for simplicity we direct all these to the same path:
       * it's OK to mix multiple architectures' libraries into one cache,
       * as done in upstream glibc (and Debian, Arch, etc.). */
      for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
        {
          const PvMultiarchDetails *details = &pv_multiarch_details[i];

          for (j = 0; j < G_N_ELEMENTS (details->other_ld_so_cache); j++)
            {
              const char *base = details->other_ld_so_cache[j];
              g_autoptr(GError) local_error = NULL;
              g_autofree gchar *path = NULL;

              if (base == NULL)
                break;

              path = g_build_filename ("etc", base, NULL);

              if (!pv_runtime_make_symlink_in_container (self,
                                                         bwrap,
                                                         mutable_cache_path,
                                                         path,
                                                         MAKE_SYMLINK_FLAGS_INTERPRETER_ROOT,
                                                         &local_error))
                g_warning ("%s", local_error->message);
            }
        }
    }

  return TRUE;
}

static void
bind_runtime_finish (PvRuntime *self,
                     FlatpakExports *exports,
                     FlatpakBwrap *bwrap)
{
  g_return_if_fail (PV_IS_RUNTIME (self));
  g_return_if_fail (exports != NULL);
  g_return_if_fail (!pv_bwrap_was_finished (bwrap));

  pv_export_symlink_targets (exports, self->overrides, "overrides");

  if (self->mutable_sysroot == NULL)
    {
      /* self->overrides is in a temporary directory that will be
       * cleaned up before we enter the container, so we need to convert
       * it into a series of --dir and --symlink instructions.
       *
       * We have to do this late, because it adds data fds. */
      pv_bwrap_copy_tree (bwrap, self->overrides, self->overrides_in_container);
    }

  /* /etc/localtime and /etc/resolv.conf can not exist (or be symlinks to
   * non-existing targets), in which case we don't want to attempt to create
   * bogus symlinks or bind mounts, as that will cause flatpak run to fail.
   */
  if (_srt_file_test_in_sysroot (self->host_in_current_namespace, self->host_fd,
                                 "/etc/localtime", G_FILE_TEST_EXISTS))
    {
      g_autofree char *target = NULL;
      gboolean is_reachable = FALSE;
      g_autofree char *tz = flatpak_get_timezone ();
      g_autofree char *timezone_content = g_strdup_printf ("%s\n", tz);
      g_autofree char *localtime_in_current_namespace =
        g_build_filename (self->host_in_current_namespace, "/etc/localtime", NULL);

      target = glnx_readlinkat_malloc (-1, localtime_in_current_namespace, NULL, NULL);

      if (target != NULL)
        {
          g_autoptr(GFile) base_file = NULL;
          g_autoptr(GFile) target_file = NULL;
          g_autofree char *target_canonical = NULL;

          base_file = g_file_new_for_path ("/etc");
          target_file = g_file_resolve_relative_path (base_file, target);
          target_canonical = g_file_get_path (target_file);

          is_reachable = g_str_has_prefix (target_canonical, "/usr/");
        }

      if (is_reachable)
        {
          flatpak_bwrap_add_args (bwrap,
                                  "--symlink", target, "/etc/localtime",
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
}

typedef enum
{
  TAKE_FROM_PROVIDER_FLAGS_IF_DIR = (1 << 0),
  TAKE_FROM_PROVIDER_FLAGS_IF_EXISTS = (1 << 1),
  TAKE_FROM_PROVIDER_FLAGS_IF_CONTAINER_COMPATIBLE = (1 << 2),
  TAKE_FROM_PROVIDER_FLAGS_COPY_FALLBACK = (1 << 3),
  TAKE_FROM_PROVIDER_FLAGS_IF_REGULAR = (1 << 4),
  TAKE_FROM_PROVIDER_FLAGS_NONE = 0
} TakeFromProviderFlags;

/*
 * pv_runtime_take_from_provider:
 * @self: the runtime
 * @bwrap: bubblewrap arguments
 * @source_in_provider: source path in the graphics stack provider's
 *  namespace, either absolute or relative to the root
 * @dest_in_container: destination path in the container we are creating,
 *  either absolute or relative to the root
 * @flags: flags affecting how we do it
 * @error: used to report error
 *
 * Try to arrange for @source_in_provider to be made available at the
 * path @dest_in_container in the container we are creating.
 *
 * Note that neither @source_in_provider nor @dest_in_container is
 * guaranteed to be an absolute path.
 */
static gboolean
pv_runtime_take_from_provider (PvRuntime *self,
                               FlatpakBwrap *bwrap,
                               const char *source_in_provider,
                               const char *dest_in_container,
                               TakeFromProviderFlags flags,
                               GError **error)
{
  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (self->provider != NULL, FALSE);
  g_return_val_if_fail (bwrap == NULL || !pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (bwrap != NULL || self->mutable_sysroot != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (flags & TAKE_FROM_PROVIDER_FLAGS_IF_DIR)
    {
      if (!_srt_file_test_in_sysroot (self->provider->path_in_current_ns,
                                      self->provider->fd,
                                      source_in_provider, G_FILE_TEST_IS_DIR))
        return TRUE;
    }

  if (flags & TAKE_FROM_PROVIDER_FLAGS_IF_REGULAR)
    {
      if (!_srt_file_test_in_sysroot (self->provider->path_in_current_ns,
                                      self->provider->fd,
                                      source_in_provider, G_FILE_TEST_IS_REGULAR))
        return TRUE;
    }

  if (flags & TAKE_FROM_PROVIDER_FLAGS_IF_EXISTS)
    {
      if (!_srt_file_test_in_sysroot (self->provider->path_in_current_ns,
                                      self->provider->fd,
                                      source_in_provider, G_FILE_TEST_EXISTS))
        return TRUE;
    }

  if (self->mutable_sysroot != NULL)
    {
      /* Replace ${mutable_sysroot}/usr/lib/locale with a symlink to
       * /run/host/usr/lib/locale, or similar */
      g_autofree gchar *parent_in_container = NULL;
      g_autofree gchar *target = NULL;
      const char *base;
      glnx_autofd int parent_dirfd = -1;

      parent_in_container = g_path_get_dirname (dest_in_container);
      parent_dirfd = _srt_resolve_in_sysroot (self->mutable_sysroot_fd,
                                              parent_in_container,
                                              SRT_RESOLVE_FLAGS_MKDIR_P,
                                              NULL, error);

      if (parent_dirfd < 0)
        return FALSE;

      base = glnx_basename (dest_in_container);

      if (!glnx_shutil_rm_rf_at (parent_dirfd, base, NULL, error))
        return FALSE;

      /* If it isn't in /usr, /lib, etc., then the symlink will be
       * dangling and this probably isn't going to work. */
      if (path_visible_in_provider_namespace (self->flags, source_in_provider))
        {
          target = g_build_filename (self->provider->path_in_container_ns,
                                     source_in_provider, NULL);
        }
      /* A few paths are always available as-is in the container, such
       * as /nix and /gnu/store */
      else if (path_visible_in_container_namespace (self->flags,
                                                    source_in_provider))
        {
          target = g_build_filename ("/", source_in_provider, NULL);
        }
      else
        {
          if (flags & TAKE_FROM_PROVIDER_FLAGS_COPY_FALLBACK)
            {
              glnx_autofd int file_fd = -1;
              glnx_autofd int dest_fd = -1;

              file_fd = _srt_resolve_in_sysroot (self->provider->fd,
                                                 source_in_provider,
                                                 SRT_RESOLVE_FLAGS_READABLE,
                                                 NULL, error);
              if (file_fd < 0)
                {
                  g_prefix_error (error,
                                  "Unable to make \"%s\" available in container: ",
                                  source_in_provider);
                  return FALSE;
                }

              /* We already deleted ${parent_dirfd}/${base}, and we don't
               * care about atomicity or durability here, so we can just
               * write in-place. The permissions are uninteresting because
               * we're not expecting other users to read this temporary
               * sysroot anyway, so use 0600 just in case the source file
               * has restrictive permissions. */
              dest_fd = TEMP_FAILURE_RETRY (openat (parent_dirfd, base,
                                                    O_WRONLY|O_CLOEXEC|O_NOCTTY|O_CREAT|O_EXCL,
                                                    0600));

              if (dest_fd < 0)
                return glnx_throw_errno_prefix (error,
                                                "Unable to open \"%s\" for writing",
                                                dest_in_container);

              if (glnx_regfile_copy_bytes (file_fd, dest_fd, (off_t) -1) < 0)
                return glnx_throw_errno_prefix (error,
                                                "Unable to copy contents of \"%s/%s\" to \"%s\"",
                                                self->provider->path_in_current_ns,
                                                source_in_provider,
                                                dest_in_container);

              return TRUE;
            }

          g_warning ("\"%s\" is unlikely to appear in \"%s\"",
                     source_in_provider, self->provider->path_in_container_ns);
          /* We might as well try *something*.
           * path_visible_in_provider_namespace() covers all the paths
           * that are going to appear in /run/host or similar, so try with
           * no special prefix here, as though
           * path_visible_in_container_namespace() had returned true:
           * that way, even if we're on a non-FHS distro that puts
           * ld.so in /some/odd/path, it will be possible to use
           * PRESSURE_VESSEL_FILESYSTEMS_RO=/some/odd/path
           * as a workaround until pressure-vessel can be adjusted. */
          target = g_build_filename ("/", source_in_provider, NULL);
        }

      /* By now, all code paths should have ensured it starts with '/' */
      g_return_val_if_fail (target != NULL, FALSE);
      g_return_val_if_fail (target[0] == '/', FALSE);

      if (TEMP_FAILURE_RETRY (symlinkat (target, parent_dirfd, base)) != 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to create symlink \"%s/%s\" -> \"%s\"",
                                        self->mutable_sysroot,
                                        dest_in_container, target);
    }
  else
    {
      g_autofree gchar *source_in_current_ns = NULL;
      g_autofree gchar *realpath_in_provider = NULL;
      g_autofree gchar *abs_dest = NULL;
      glnx_autofd int source_fd = -1;

      /* We can't edit the runtime in-place, so tell bubblewrap to mount
       * a new version over the top */
      g_assert (bwrap != NULL);

      source_fd = _srt_resolve_in_sysroot (self->provider->fd,
                                           source_in_provider,
                                           SRT_RESOLVE_FLAGS_NONE,
                                           &realpath_in_provider, error);

      if (source_fd < 0)
        return FALSE;

      if (flags & TAKE_FROM_PROVIDER_FLAGS_IF_CONTAINER_COMPATIBLE)
        {
          g_autofree gchar *dest = NULL;
          struct stat stat_buf;

          if (g_str_has_prefix (dest_in_container, "/usr/"))
            dest = g_build_filename (self->runtime_usr,
                                     dest_in_container + strlen ("/usr/"),
                                     NULL);
          else if (g_str_has_prefix (dest_in_container, "usr/"))
            dest = g_build_filename (self->runtime_usr,
                                     dest_in_container + strlen ("usr/"),
                                     NULL);
          else
            dest = g_build_filename (self->runtime_files,
                                     dest_in_container,
                                     NULL);

          if (fstat (source_fd, &stat_buf) != 0)
            return glnx_throw_errno_prefix (error,
                                            "fstat \"%s/%s\"",
                                            self->provider->path_in_current_ns,
                                            realpath_in_provider);

          if (S_ISDIR (stat_buf.st_mode))
            {
              if (!g_file_test (dest, G_FILE_TEST_IS_DIR))
                {
                  g_warning ("Not mounting \"%s/%s\" over "
                             "non-directory file or nonexistent path \"%s\"",
                             self->provider->path_in_current_ns,
                             source_in_provider, dest);
                  return TRUE;
                }
            }
          else
            {
              if (g_file_test (dest, G_FILE_TEST_IS_DIR) ||
                  !g_file_test (dest, G_FILE_TEST_EXISTS))
                {
                  g_warning ("Not mounting \"%s/%s\" over directory or "
                             "nonexistent path \"%s\"",
                             self->provider->path_in_current_ns,
                             source_in_provider, dest);
                  return TRUE;
                }
            }
        }

      /* This is not 100% robust against the provider sysroot being
       * modified while we're looking at it, but it's the best we can do. */
      source_in_current_ns = g_build_filename (self->provider->path_in_current_ns,
                                               realpath_in_provider,
                                               NULL);
      abs_dest = g_build_filename ("/", dest_in_container, NULL);
      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind", source_in_current_ns, abs_dest,
                              NULL);
    }

  return TRUE;
}

/*
 * pv_runtime_take_any_from_provider:
 * @self: the runtime
 * @bwrap: bubblewrap arguments
 * @sources_in_provider: (array zero-terminated=1): source paths in the
 *  graphics stack provider's namespace, either absolute or relative
 *  to the root
 * @dest_in_container: destination path in the container we are creating,
 *  either absolute or relative to the root
 * @flags: flags affecting how we do it
 * @error: used to report error
 *
 * Try to arrange for one of @sources_in_provider to be made available
 * at the path @dest_in_container in the container we are creating.
 *
 * Note that neither @source_in_provider nor @dest_in_container is
 * guaranteed to be an absolute path.
 *
 * %TAKE_FROM_PROVIDER_FLAGS_IF_EXISTS is implied.
 */
static gboolean
pv_runtime_take_any_from_provider (PvRuntime *self,
                                   FlatpakBwrap *bwrap,
                                   const char * const *sources_in_provider,
                                   const char *dest_in_container,
                                   TakeFromProviderFlags flags,
                                   GError **error)
{
  SrtResolveFlags resolve_flags = SRT_RESOLVE_FLAGS_NONE;
  gsize i;

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (self->provider != NULL, FALSE);
  g_return_val_if_fail (bwrap == NULL || !pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (bwrap != NULL || self->mutable_sysroot != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* _srt_resolve_in_sysroot() will only return true if it exists, so we
   * won't need to check again */
  flags &= ~TAKE_FROM_PROVIDER_FLAGS_IF_EXISTS;

  /* Delegate responsibility for this to _srt_resolve_in_sysroot() */
  if (flags & TAKE_FROM_PROVIDER_FLAGS_IF_DIR)
    {
      resolve_flags |= SRT_RESOLVE_FLAGS_MUST_BE_DIRECTORY;
      flags &= ~TAKE_FROM_PROVIDER_FLAGS_IF_DIR;
    }

  if (flags & TAKE_FROM_PROVIDER_FLAGS_IF_REGULAR)
    {
      resolve_flags |= SRT_RESOLVE_FLAGS_MUST_BE_REGULAR;
      flags &= ~TAKE_FROM_PROVIDER_FLAGS_IF_REGULAR;
    }

  for (i = 0; sources_in_provider[i] != NULL; i++)
    {
      const char *source_in_provider = sources_in_provider[i];
      glnx_autofd int fd = -1;
      g_autoptr(GError) local_error = NULL;

      fd = _srt_resolve_in_sysroot (self->provider->fd,
                                    source_in_provider, resolve_flags,
                                    NULL, &local_error);

      if (fd < 0)
        {
          if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
            g_debug ("\"%s/%s\": %s",
                     self->provider->path_in_current_ns,
                     source_in_provider, local_error->message);

          continue;
        }

      if (!pv_runtime_take_from_provider (self, bwrap, source_in_provider,
                                          dest_in_container, flags, error))
        return FALSE;

      return TRUE;
    }

  /* None of the possibilities matched */
  g_debug ("Did not find a suitable \"%s\" in provider, ignoring",
           dest_in_container);
  return TRUE;
}

static gboolean
pv_runtime_remove_overridden_libraries (PvRuntime *self,
                                        RuntimeArchitecture *arch,
                                        GError **error)
{
  g_autoptr(GPtrArray) dirs = NULL;
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer = NULL;
  GHashTable **delete = NULL;
  SrtDirIter *iters = NULL;
  gboolean ret = FALSE;
  gsize i;

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (arch != NULL, FALSE);
  g_return_val_if_fail (arch->ld_so != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* Not applicable/possible if we don't have a mutable sysroot */
  g_return_val_if_fail (self->mutable_sysroot != NULL, FALSE);

  timer = _srt_profiling_start ("Removing overridden %s libraries",
                                arch->details->tuple);

  dirs = pv_multiarch_details_get_libdirs (arch->details,
                                           PV_MULTIARCH_LIBDIRS_FLAGS_REMOVE_OVERRIDDEN);
  delete = g_new0 (GHashTable *, dirs->len);
  iters = g_new0 (SrtDirIter, dirs->len);

  for (i = 0; i < dirs->len; i++)
    g_assert (g_path_is_absolute (g_ptr_array_index (dirs, i)));

  /* We have to figure out what we want to delete before we delete anything,
   * because we can't tell whether a symlink points to a library of a
   * particular SONAME if we already deleted the library. */
  for (i = 0; i < dirs->len; i++)
    {
      const char *libdir = g_ptr_array_index (dirs, i);
      glnx_autofd int libdir_fd = -1;
      struct dirent *dent;
      gsize j;

      /* Mostly ignore error: if the library directory cannot be opened,
       * presumably we don't need to do anything with it... */
        {
          g_autoptr(GError) local_error = NULL;

          libdir_fd = _srt_resolve_in_sysroot (self->mutable_sysroot_fd,
                                               libdir,
                                               SRT_RESOLVE_FLAGS_READABLE,
                                               NULL, &local_error);

          if (libdir_fd < 0)
            {
              g_debug ("Cannot resolve \"%s\" in \"%s\", so no need to delete "
                       "libraries from it: %s",
                       libdir, self->mutable_sysroot, local_error->message);
              g_clear_error (&local_error);
              continue;
            }

          for (j = 0; j < i; j++)
            {
              /* No need to inspect a directory if it's one we already
               * looked at (perhaps via symbolic links) */
              if (iters[j].real_iter.initialized
                  && _srt_fstatat_is_same_file (libdir_fd, "",
                                                iters[j].real_iter.fd, ""))
                break;
            }

          if (j < i)
            {
              g_debug ("%s is the same directory as %s, skipping it",
                       libdir, (const char *) g_ptr_array_index (dirs, j));
              continue;
            }
        }

      g_debug ("Removing overridden %s libraries from \"%s\" in \"%s\"...",
               arch->details->tuple, libdir, self->mutable_sysroot);

      if (!_srt_dir_iter_init_take_fd (&iters[i], &libdir_fd,
                                       SRT_DIR_ITER_FLAGS_ENSURE_DTYPE,
                                       self->arbitrary_dirent_order,
                                       error))
        {
          glnx_prefix_error (error, "Unable to start iterating \"%s%s\"",
                             self->mutable_sysroot,
                             libdir);
          goto out;
        }

      delete[i] = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         g_free, g_free);

      while (TRUE)
        {
          g_autofree gchar *target = NULL;
          const char *target_base;

          if (!_srt_dir_iter_next_dent (&iters[i], &dent, NULL, error))
            {
              glnx_prefix_error (error, "Unable to iterate over \"%s%s\"",
                                 self->mutable_sysroot, libdir);
              goto out;
            }

          if (dent == NULL)
            break;

          switch (dent->d_type)
            {
              case DT_REG:
              case DT_LNK:
                break;

              case DT_BLK:
              case DT_CHR:
              case DT_DIR:
              case DT_FIFO:
              case DT_SOCK:
              case DT_UNKNOWN:
              default:
                continue;
            }

          if (!g_str_has_prefix (dent->d_name, "lib"))
            continue;

          if (!g_str_has_suffix (dent->d_name, ".so") &&
              strstr (dent->d_name, ".so.") == NULL)
            continue;

          target = glnx_readlinkat_malloc (iters[i].real_iter.fd, dent->d_name,
                                           NULL, NULL);
          if (target != NULL)
            target_base = glnx_basename (target);
          else
            target_base = NULL;

          /* Suppose we have a shared library libcurl.so.4 -> libcurl.so.4.2.0
           * in the container and libcurl.so.4.7.0 in the provider,
           * with a backwards-compatibility alias libcurl.so.3.
           * dent->d_name might be any of those strings. */

          /* scope for soname_link */
          if (TRUE)   /* to avoid -Wmisleading-indentation */
            {
              g_autofree gchar *soname_link = NULL;

              /* If we're looking at
               * /usr/lib/MULTIARCH/libcurl.so.4 -> libcurl.so.4.2.0, and a
               * symlink .../overrides/lib/MULTIARCH/libcurl.so.4 exists, then
               * we want to delete /usr/lib/MULTIARCH/libcurl.so.4 and
               * /usr/lib/MULTIARCH/libcurl.so.4.2.0. */
              soname_link = g_build_filename (arch->libdir_in_current_namespace,
                                              dent->d_name, NULL);

              if (g_file_test (soname_link, G_FILE_TEST_IS_SYMLINK))
                {
                  if (target_base != NULL)
                    g_hash_table_replace (delete[i],
                                          g_strdup (target_base),
                                          g_strdup (soname_link));

                  g_hash_table_replace (delete[i],
                                        g_strdup (dent->d_name),
                                        g_steal_pointer (&soname_link));
                  continue;
                }
            }

          /* scope for alias_link */
          if (TRUE)   /* to avoid -Wmisleading-indentation */
            {
              g_autofree gchar *alias_link = NULL;
              g_autofree gchar *alias_target = NULL;

              /* If we're looking at
               * /usr/lib/MULTIARCH/libcurl.so.3 -> libcurl.so.4, and a
               * symlink .../aliases/libcurl.so.3 exists and points to
               * e.g. .../overrides/lib/$MULTIARCH/libcurl.so.4, then
               * /usr/lib/MULTIARCH/libcurl.so.3 was overridden and should
               * be deleted; /usr/lib/MULTIARCH/libcurl.so.4 should also
               * be deleted.
               *
               * However, if .../aliases/libcurl.so.3 points to
               * e.g. /usr/lib/MULTIARCH/libcurl.so.4, then the container's
               * library was not overridden and we should not delete
               * anything. */
              alias_link = g_build_filename (arch->aliases_in_current_namespace,
                                             dent->d_name, NULL);
              alias_target = glnx_readlinkat_malloc (AT_FDCWD, alias_link,
                                                     NULL, NULL);

              if (alias_target != NULL
                  && flatpak_has_path_prefix (alias_target,
                                              self->overrides_in_container))
                {
                  if (target_base != NULL)
                    g_hash_table_replace (delete[i],
                                          g_strdup (target_base),
                                          g_strdup (alias_link));

                  g_hash_table_replace (delete[i],
                                        g_strdup (dent->d_name),
                                        g_steal_pointer (&alias_link));
                  continue;
                }
            }

          g_assert ((target != NULL) == (target_base != NULL));

          if (target_base != NULL)
            {
              g_autofree gchar *soname_link = NULL;

              /* If we're looking at
               * /usr/lib/MULTIARCH/libcurl.so -> libcurl.so.4, and a
               * symlink .../overrides/lib/MULTIARCH/libcurl.so.4 exists,
               * then we want to delete /usr/lib/MULTIARCH/libcurl.so
               * and /usr/lib/MULTIARCH/libcurl.so.4. */
              soname_link = g_build_filename (arch->libdir_in_current_namespace,
                                              target_base, NULL);

              if (g_file_test (soname_link, G_FILE_TEST_IS_SYMLINK))
                {
                  g_hash_table_replace (delete[i],
                                        g_strdup (target_base),
                                        g_strdup (soname_link));
                  g_hash_table_replace (delete[i],
                                        g_strdup (dent->d_name),
                                        g_steal_pointer (&soname_link));
                  continue;
                }
            }

          if (target_base != NULL)
            {
              g_autofree gchar *alias_link = NULL;
              g_autofree gchar *alias_target = NULL;

              /* If we're looking at
               * /usr/lib/MULTIARCH/libcurl.so.3 -> libcurl.so.4, and a
               * symlink .../aliases/libcurl.so.3 exists and points to
               * e.g. .../overrides/lib/$MULTIARCH/libcurl.so.4, then
               * /usr/lib/MULTIARCH/libcurl.so.3 was overridden and should
               * be deleted; /usr/lib/MULTIARCH/libcurl.so.4 should also
               * be deleted.
               *
               * However, if .../aliases/libcurl.so.3 points to
               * e.g. /usr/lib/MULTIARCH/libcurl.so.4, then the container's
               * library was not overridden and we should not delete it. */
              alias_link = g_build_filename (arch->aliases_in_current_namespace,
                                             target_base, NULL);
              alias_target = glnx_readlinkat_malloc (AT_FDCWD, alias_link,
                                                     NULL, NULL);

              if (alias_target != NULL
                  && flatpak_has_path_prefix (alias_target,
                                              self->overrides_in_container))
                {
                  g_hash_table_replace (delete[i],
                                        g_strdup (target_base),
                                        g_strdup (alias_link));
                  g_hash_table_replace (delete[i],
                                        g_strdup (dent->d_name),
                                        g_steal_pointer (&alias_link));
                  continue;
                }
            }
        }

      /* Iterate over the directory again, to clean up dangling development
       * symlinks */
      _srt_dir_iter_rewind (&iters[i]);

      while (TRUE)
        {
          g_autofree gchar *target = NULL;
          gpointer reason;

          if (!_srt_dir_iter_next_dent (&iters[i], &dent, NULL, error))
            {
              glnx_prefix_error (error, "Unable to iterate over \"%s%s\"",
                                 self->mutable_sysroot, libdir);
              goto out;
            }

          if (dent == NULL)
            break;

          if (dent->d_type != DT_LNK)
            continue;

          /* If we were going to delete it anyway, ignore */
          if (g_hash_table_lookup_extended (delete[i], dent->d_name, NULL, NULL))
            continue;

          target = glnx_readlinkat_malloc (iters[i].real_iter.fd, dent->d_name,
                                           NULL, NULL);

          /* If we're going to delete the target, also delete the symlink
           * rather than leaving it dangling */
          if (g_hash_table_lookup_extended (delete[i], target, NULL, &reason))
            g_hash_table_replace (delete[i], g_strdup (dent->d_name),
                                  g_strdup (reason));
        }
    }

  for (i = 0; i < dirs->len; i++)
    {
      g_auto(SrtHashTableIter) iter = SRT_HASH_TABLE_ITER_CLEARED;
      const char *libdir = g_ptr_array_index (dirs, i);
      const char *name;
      const char *reason;

      if (delete[i] == NULL)
        continue;

      g_assert (iters[i].real_iter.initialized);
      g_assert (iters[i].real_iter.fd >= 0);

      _srt_hash_table_iter_init_sorted (&iter, delete[i],
                                        self->arbitrary_str_order);

      while (_srt_hash_table_iter_next (&iter, &name, &reason))
        {
          g_autoptr(GError) local_error = NULL;

          g_debug ("Deleting %s%s/%s because %s replaces it",
                   self->mutable_sysroot, libdir, name, reason);

          if (!glnx_unlinkat (iters[i].real_iter.fd, name, 0, &local_error))
            {
              g_warning ("Unable to delete %s%s/%s: %s",
                         self->mutable_sysroot, libdir,
                         name, local_error->message);
              g_clear_error (&local_error);
            }
        }
    }

  ret = TRUE;

out:
  if (dirs != NULL)
    {
      g_assert (delete != NULL);
      g_assert (iters != NULL);

      for (i = 0; i < dirs->len; i++)
        {
          g_clear_pointer (&delete[i], g_hash_table_unref);
          _srt_dir_iter_clear (&iters[i]);
        }
    }

  g_free (delete);
  g_free (iters);
  return ret;
}

static gboolean
pv_runtime_take_ld_so_from_provider (PvRuntime *self,
                                     RuntimeArchitecture *arch,
                                     const gchar *ld_so_in_runtime,
                                     FlatpakBwrap *bwrap,
                                     GError **error)
{
  glnx_autofd int path_fd = -1;
  g_autofree gchar *ld_so_relative_to_provider = NULL;

  g_return_val_if_fail (self->provider != NULL, FALSE);
  g_return_val_if_fail (bwrap != NULL || self->mutable_sysroot != NULL, FALSE);

  g_debug ("Making provider's ld.so visible in container");

  path_fd = _srt_resolve_in_sysroot (self->provider->fd,
                                     arch->ld_so, SRT_RESOLVE_FLAGS_READABLE,
                                     &ld_so_relative_to_provider, error);

  if (path_fd < 0)
    {
      g_prefix_error (error, "Unable to determine provider path to %s: ",
                      arch->ld_so);
      return FALSE;
    }

  g_debug ("Provider path: %s -> %s", arch->ld_so, ld_so_relative_to_provider);
  /* Might be either absolute, or relative to the root */
  g_debug ("Container path: %s -> %s", arch->ld_so, ld_so_in_runtime);

  /* If we have a mutable sysroot, we can delete the interoperable path
   * and replace it with a symlink to what we want.
   * For example, overwrite /lib/ld-linux.so.2 with a symlink to
   * /run/host/lib/i386-linux-gnu/ld-2.30.so, or similar. This avoids
   * having to dereference a long chain of symlinks every time we run
   * an executable. */
  if (self->mutable_sysroot != NULL &&
      !pv_runtime_take_from_provider (self, bwrap, ld_so_relative_to_provider,
                                      arch->ld_so,
                                      TAKE_FROM_PROVIDER_FLAGS_NONE, error))
    return FALSE;

  /* If we don't have a mutable sysroot, we cannot replace symlinks,
   * and we also cannot mount onto symlinks (they get dereferenced),
   * so our only choice is to bind-mount
   * /lib/i386-linux-gnu/ld-2.30.so onto
   * /lib/i386-linux-gnu/ld-2.15.so and so on.
   *
   * In the mutable sysroot case, we don't strictly need to
   * overwrite /lib/i386-linux-gnu/ld-2.15.so with a symlink to
   * /run/host/lib/i386-linux-gnu/ld-2.30.so, but we might as well do
   * it anyway, for extra robustness: if we ever run a ld.so that
   * doesn't match the libc we are using (perhaps via an OS-specific,
   * non-standard path), that's pretty much a disaster, because it will
   * just crash. However, all of those (chains of) non-standard symlinks
   * will end up pointing to ld_so_in_runtime. */
  return pv_runtime_take_from_provider (self, bwrap, ld_so_relative_to_provider,
                                        ld_so_in_runtime,
                                        TAKE_FROM_PROVIDER_FLAGS_NONE, error);
}

/*
 * setup_json_manifest:
 * @self: The runtime
 * @bwrap: Append arguments to this bubblewrap invocation to make files
 *  available in the container
 * @sub_dir: `vulkan/icd.d`, `glvnd/egl_vendor.d` or similar
 * @write_to_dir: Path to `/overrides/share/${sub_dir}` in the
 *  current execution environment
 * @details: An #IcdDetails holding a #SrtVulkanLayer or #SrtVulkanIcd,
 *  whichever is appropriate for @sub_dir
 * @digits: Number of digits to pad length of numeric prefix
 * @seq: Sequence number of @details, used to make unique filenames
 * @search_path: Used to build `$VK_DRIVER_FILES` or a similar search path
 * @error: Used to raise an error on failure
 *
 * Make a single Vulkan layer or ICD available in the container.
 */
static gboolean
setup_json_manifest (PvRuntime *self,
                     FlatpakBwrap *bwrap,
                     const gchar *sub_dir,
                     const gchar *write_to_dir,
                     IcdDetails *details,
                     int digits,
                     gsize seq,
                     GString *search_path,
                     GError **error)
{
  SrtVulkanLayer *layer = NULL;
  SrtVulkanIcd *icd = NULL;
  SrtEglIcd *egl = NULL;
  SrtEglExternalPlatform *ext_platform = NULL;
  gboolean loaded = FALSE;
  gboolean need_provider_json = FALSE;
  gsize i;

  g_return_val_if_fail (self->provider != NULL, FALSE);
  g_return_val_if_fail (bwrap != NULL || self->mutable_sysroot != NULL, FALSE);

  if (SRT_IS_VULKAN_LAYER (details->icd))
    layer = SRT_VULKAN_LAYER (details->icd);
  else if (SRT_IS_VULKAN_ICD (details->icd))
    icd = SRT_VULKAN_ICD (details->icd);
  else if (SRT_IS_EGL_ICD (details->icd))
    egl = SRT_EGL_ICD (details->icd);
  else if (SRT_IS_EGL_EXTERNAL_PLATFORM (details->icd))
    ext_platform = SRT_EGL_EXTERNAL_PLATFORM (details->icd);
  else
    g_return_val_if_reached (FALSE);

  g_debug ("Setting up JSON manifest for %s loadable module #%" G_GSIZE_FORMAT ": %s",
           sub_dir, seq, details->debug_name);

  /* If the layer failed to load, there's nothing to make available */
  if (layer != NULL)
    loaded = srt_vulkan_layer_check_error (layer, NULL);
  else if (egl != NULL)
    loaded = srt_egl_icd_check_error (egl, NULL);
  else if (ext_platform != NULL)
    loaded = srt_egl_external_platform_check_error (ext_platform, NULL);
  else
    loaded = srt_vulkan_icd_check_error (icd, NULL);

  if (!loaded)
    {
      g_debug ("Original JSON manifest failed to load, nothing to do");
      return TRUE;
    }

  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      g_assert (i < G_N_ELEMENTS (details->kinds));
      g_assert (i < G_N_ELEMENTS (details->paths_in_container));

      if (details->kinds[i] == ICD_KIND_ABSOLUTE)
        {
          g_autofree gchar *write_to_file = NULL;
          g_autofree gchar *json_in_container = NULL;
          g_autofree gchar *json_base = NULL;

          g_assert (details->paths_in_container[i] != NULL);

          json_base = g_strdup_printf ("%.*" G_GSIZE_FORMAT "-%s.json",
                                       digits, seq, pv_multiarch_tuples[i]);
          write_to_file = g_build_filename (write_to_dir, json_base, NULL);
          json_in_container = g_build_filename (self->overrides_in_container,
                                                "share", sub_dir,
                                                json_base, NULL);

          g_debug ("Generating \"%s\" with path \"%s\", implementing \"%s\" in container",
                   write_to_file, details->paths_in_container[i],
                   json_in_container);

          if (layer != NULL)
            {
              g_autoptr(SrtVulkanLayer) replacement = NULL;
              replacement = srt_vulkan_layer_new_replace_library_path (layer,
                                                                       details->paths_in_container[i]);

              if (!srt_vulkan_layer_write_to_file (replacement, write_to_file, error))
                return FALSE;
            }
          else if (egl != NULL)
            {
              g_autoptr(SrtEglIcd) replacement = NULL;

              replacement = srt_egl_icd_new_replace_library_path (egl,
                                                                  details->paths_in_container[i]);

              if (!srt_egl_icd_write_to_file (replacement, write_to_file,
                                              error))
                return FALSE;
            }
          else if (ext_platform != NULL)
            {
              g_autoptr(SrtEglExternalPlatform) replacement = NULL;

              replacement = srt_egl_external_platform_new_replace_library_path (ext_platform,
                                                                                details->paths_in_container[i]);

              if (!srt_egl_external_platform_write_to_file (replacement, write_to_file, error))
                return FALSE;
            }
          else
            {
              g_autoptr(SrtVulkanIcd) replacement = NULL;
              replacement = srt_vulkan_icd_new_replace_library_path (icd,
                                                                     details->paths_in_container[i]);

              if (!srt_vulkan_icd_write_to_file (replacement, write_to_file, error))
                return FALSE;
            }

          pv_search_path_append (search_path, json_in_container);
        }
      else if (details->kinds[i] == ICD_KIND_SONAME
               || details->kinds[i] == ICD_KIND_META_LAYER)
        {
          g_debug ("Will use graphics stack provider JSON as-is for %s #%" G_GSIZE_FORMAT,
                   sub_dir, seq);
          need_provider_json = TRUE;
        }
    }

  if (need_provider_json)
    {
      g_autofree gchar *json_in_container = NULL;
      g_autofree gchar *json_base = NULL;
      const char *json_in_provider = NULL;
      if (layer != NULL)
        json_in_provider = srt_vulkan_layer_get_json_path (layer);
      else if (egl != NULL)
        json_in_provider = srt_egl_icd_get_json_path (egl);
      else if (ext_platform != NULL)
        json_in_provider = srt_egl_external_platform_get_json_path (ext_platform);
      else
        json_in_provider = srt_vulkan_icd_get_json_path (icd);

      json_base = g_strdup_printf ("%.*" G_GSIZE_FORMAT ".json", digits, seq);
      json_in_container = g_build_filename (self->overrides_in_container,
                                            "share", sub_dir,
                                            json_base, NULL);

      g_debug ("Copying \"%s\" as-is to implement \"%s\" in container",
               json_in_provider, json_in_container);

      if (!pv_runtime_take_from_provider (self, bwrap,
                                          json_in_provider,
                                          json_in_container,
                                          TAKE_FROM_PROVIDER_FLAGS_COPY_FALLBACK,
                                          error))
        return FALSE;

      pv_search_path_append (search_path, json_in_container);
    }

  return TRUE;
}

/*
 * setup_each_json_manifest:
 * @self: The runtime
 * @bwrap: Append arguments to this bubblewrap invocation to make files
 *  available in the container
 * @sub_dir: `vulkan/icd.d` or similar
 * @write_to_dir: Path to `/overrides/share/${sub_dir}` in the
 *  current execution environment
 * @details: (element-type IcdDetails): A list of #IcdDetails
 *  holding #SrtVulkanLayer, #SrtVulkanIcd or #SrtEglIcd, as appropriate
 *  for @sub_dir
 * @search_path: Used to build `$VK_DRIVER_FILES` or a similar search path
 * @error: Used to raise an error on failure
 *
 * Make a list of Vulkan layers or ICDs available in the container.
 */
static gboolean
setup_each_json_manifest (PvRuntime *self,
                          FlatpakBwrap *bwrap,
                          const gchar *sub_dir,
                          GPtrArray *details,
                          GString *search_path,
                          GError **error)
{
  gsize j;
  g_autofree gchar *write_to_dir = NULL;
  int digits = pv_count_decimal_digits (details->len);

  g_return_val_if_fail (self->provider != NULL, FALSE);
  g_return_val_if_fail (bwrap != NULL || self->mutable_sysroot != NULL, FALSE);

  write_to_dir = g_build_filename (self->overrides,
                                  "share", sub_dir, NULL);

  if (g_mkdir_with_parents (write_to_dir, 0700) != 0)
    {
      glnx_throw_errno_prefix (error, "Unable to create %s", write_to_dir);
      return FALSE;
    }

  for (j = 0; j < details->len; j++)
    {
      if (!setup_json_manifest (self, bwrap, sub_dir, write_to_dir,
                                g_ptr_array_index (details, j),
                                digits, j, search_path, error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
collect_vulkan_layers (PvRuntime *self,
                       const GPtrArray *layer_details,
                       GPtrArray *libdir_patterns,
                       RuntimeArchitecture *arch,
                       const gchar *dir_name,
                       GError **error)
{
  /* We don't have to use multiple directories unless there are
   * filename collisions, because the order of the JSON manifests
   * might matter, but the order of the actual libraries does not. */
  gboolean use_numbered_subdirs = FALSE;
  gsize j;
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer =
    _srt_profiling_start ("Collecting Vulkan %s layers", dir_name);

  g_return_val_if_fail (self->provider != NULL, FALSE);
  g_return_val_if_fail (layer_details != NULL, FALSE);
  g_return_val_if_fail (libdir_patterns != NULL, FALSE);
  g_return_val_if_fail (dir_name != NULL, FALSE);

  for (j = 0; j < layer_details->len; j++)
    {
      g_autoptr(SrtLibrary) library = NULL;
      SrtLibraryIssues issues;
      IcdDetails *details = g_ptr_array_index (layer_details, j);
      SrtVulkanLayer *layer = SRT_VULKAN_LAYER (details->icd);
      const char *resolved_library;
      const gsize multiarch_index = arch->multiarch_index;

      g_assert (details->resolved_libraries[multiarch_index] == NULL);

      if (!srt_vulkan_layer_check_error (layer, NULL))
        continue;

      /* For meta-layers we don't have a library path */
      if (srt_vulkan_layer_get_library_path(layer) == NULL)
        {
          details->kinds[multiarch_index] = ICD_KIND_META_LAYER;
          continue;
        }

      /* If the library_path is relative to the JSON file, turn it into an
       * absolute path. If it's already absolute, or if it's a basename to be
       * looked up in the system library search path, use it as-is. */
      details->resolved_libraries[multiarch_index] = srt_vulkan_layer_resolve_library_path (layer);
      resolved_library = details->resolved_libraries[multiarch_index];
      g_assert (details->resolved_libraries[multiarch_index] != NULL);

      if (strchr (resolved_library, '/') != NULL &&
          (strstr (resolved_library, "$ORIGIN/") != NULL ||
           strstr (resolved_library, "${ORIGIN}") != NULL ||
           strstr (resolved_library, "$LIB/") != NULL ||
           strstr (resolved_library, "${LIB}") != NULL ||
           strstr (resolved_library, "$PLATFORM/") != NULL ||
           strstr (resolved_library, "${PLATFORM}") != NULL))
        {
          /* When loading a library by its absolute or relative path
           * (but not when searching the library path for its basename),
           * glibc expands dynamic string tokens: LIB, PLATFORM, ORIGIN.
           * libcapsule cannot expand these special tokens: the only thing
           * that knows the correct magic values for them is glibc, which has
           * no API to tell us. The only way we can find out the library's
           * real location is to tell libdl to load (dlopen) the library, and
           * see what the resulting path is. */
          if (g_strcmp0 (self->provider->path_in_current_ns, "/") == 0)
            {
              /* It's in our current namespace, so we can dlopen it. */
              g_info ("Evaluating dynamic string tokens in \"%s\"", resolved_library);
              issues = srt_check_library_presence (resolved_library,
                                                   arch->details->tuple, NULL,
                                                   SRT_LIBRARY_SYMBOLS_FORMAT_PLAIN,
                                                   &library);
              if (issues & (SRT_LIBRARY_ISSUES_CANNOT_LOAD |
                            SRT_LIBRARY_ISSUES_UNKNOWN |
                            SRT_LIBRARY_ISSUES_TIMEOUT))
                {
                  g_info ("Unable to load library %s: %s", resolved_library,
                          srt_library_get_messages (library));
                  g_clear_pointer (&details->resolved_libraries[multiarch_index], g_free);
                  continue;
                }

              g_info ("After evaluating dynamic string tokens: \"%s\"",
                      srt_library_get_absolute_path (library));

              /* This is "borrowed" and we are about to invalidate it */
              resolved_library = NULL;

              g_free (details->resolved_libraries[multiarch_index]);
              details->resolved_libraries[multiarch_index] = g_strdup (srt_library_get_absolute_path (library));
            }
          else
            {
              /* Sorry, we can't know how to load this. */
              g_info ("Cannot support ld.so special tokens, e.g. ${LIB}, when provider "
                      "is not the root filesystem: ignoring %s",
                      resolved_library);
              g_clear_pointer (&details->resolved_libraries[multiarch_index], g_free);
              continue;
            }
        }
    }

  if (!bind_icds (self, arch, dir_name,
                  (IcdDetails **) layer_details->pdata,
                  layer_details->len,
                  &use_numbered_subdirs, libdir_patterns, NULL, error))
    return FALSE;

  return TRUE;
}

/*
 * @self: the runtime
 * @arch: An architecture
 * @ld_so_in_runtime: (out) (nullable) (not optional): Used to return
 *  the path to the architecture's ld.so in the runtime, or to
 *  return %NULL if there is none.
 * @error: Used to raise an error on failure
 *
 * Get the path to the ld.so in the runtime, which is either absolute
 * or relative to the sysroot.
 *
 * Returns: %TRUE on success (possibly yielding %NULL via @ld_so_in_runtime)
 */
static gboolean
pv_runtime_get_ld_so (PvRuntime *self,
                      RuntimeArchitecture *arch,
                      gchar **ld_so_in_runtime,
                      GError **error)
{
  if (self->mutable_sysroot != NULL)
    {
      G_GNUC_UNUSED glnx_autofd int fd = -1;

      fd = _srt_resolve_in_sysroot (self->mutable_sysroot_fd,
                                    arch->ld_so,
                                    SRT_RESOLVE_FLAGS_NONE,
                                    ld_so_in_runtime,
                                    NULL);

      /* Ignore fd, and just let it close: we're resolving
       * the path for its side-effect of populating
       * ld_so_in_runtime. */
    }
  else
    {
      g_autoptr(FlatpakBwrap) temp_bwrap = NULL;
      g_autofree gchar *etc = NULL;

      /* When setting up an interpreter root, for simplicity we require
       * the easier mutable sysroot code-path. */
      g_return_val_if_fail (!(self->flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT), FALSE);

      if (self->bubblewrap == NULL)
        return glnx_throw (error,
                           "Cannot run bubblewrap to set up runtime");

      /* Do it the hard way, by asking a process running in the
       * container (or at least a container resembling the one we
       * are going to use) to resolve it for us */
      temp_bwrap = flatpak_bwrap_new (NULL);
      flatpak_bwrap_add_args (temp_bwrap,
                              self->bubblewrap,
                              NULL);

      if (!pv_bwrap_bind_usr (temp_bwrap,
                              self->runtime_files_on_host,
                              self->runtime_files_fd,
                              "/",
                              error))
        return FALSE;

      etc = g_build_filename (self->runtime_files_on_host,
                              "etc", NULL);
      flatpak_bwrap_add_args (temp_bwrap,
                              "--ro-bind",
                              etc,
                              "/etc",
                              NULL);

      if (self->provider != NULL)
        {
          g_autofree gchar *provider_etc = NULL;
          g_autofree gchar *provider_etc_dest = NULL;

          if (!pv_bwrap_bind_usr (temp_bwrap,
                                  self->provider->path_in_host_ns,
                                  self->provider->fd,
                                  self->provider->path_in_container_ns,
                                  error))
            return FALSE;

          provider_etc = g_build_filename (self->provider->path_in_host_ns,
                                           "etc", NULL);
          provider_etc_dest = g_build_filename (self->provider->path_in_container_ns,
                                                "etc", NULL);
          flatpak_bwrap_add_args (temp_bwrap,
                                  "--ro-bind",
                                  provider_etc,
                                  provider_etc_dest,
                                  NULL);
        }

      flatpak_bwrap_set_env (temp_bwrap, "PATH", "/usr/bin:/bin", TRUE);
      flatpak_bwrap_add_args (temp_bwrap,
                              "readlink", "-e", arch->ld_so,
                              NULL);
      flatpak_bwrap_finish (temp_bwrap);

      pv_run_sync ((const char * const *) temp_bwrap->argv->pdata,
                   (const char * const *) temp_bwrap->envp, NULL,
                   ld_so_in_runtime, NULL);
    }

  return TRUE;
}

/*
 * @patterns: (inout) (not nullable):
 */
static void
collect_graphics_libraries_patterns (GPtrArray *patterns)
{
  static const char * const sonames[] =
  {
    /* Vulkan */
    "libvulkan.so.1",

    /* VDPAU */
    "libvdpau.so.1",

    /* VA-API */
    "libva.so.1",
    "libva-drm.so.1",
    "libva-glx.so.1",
    "libva-x11.so.1",
    "libva.so.2",
    "libva-drm.so.2",
    "libva-glx.so.2",
    "libva-x11.so.2",
  };
  /*
   * In principle we could have another array sonames_even_if_older[] here,
   * but in practice we don't want to do that, because we should prefer to
   * use dependency libaries from the runtime if they're strictly newer.
   * Otherwise, games linked against the runtime could fail to start up.
   *
   * Similarly, in principle we could have another array soname_globs[]
   * here, but in practice the libraries that we want to match with
   * wildcards are the same ones we want to take from the host even if
   * they're older than the ones in the runtime: games are expected to
   * look up symbols in all of these libraries with dlsym(), except for
   * a few core symbols that have existed since time immemorial.
   */
  static const char * const soname_globs_even_if_older[] =
  {
    /* NVIDIA proprietary stack */
    "libEGL.so.*",
    "libEGL_nvidia.so.*",
    "libGL.so.*",
    "libGLESv1_CM.so.*",
    "libGLESv1_CM_nvidia.so.*",
    "libGLESv2.so.*",
    "libGLESv2_nvidia.so.*",
    "libGLX.so.*",
    "libGLX_nvidia.so.*",
    "libGLX_indirect.so.*",
    "libGLdispatch.so.*",
    "libOpenGL.so.*",
    "libXNVCtrl.so.*",
    "libcuda.so.*",
    "libglx.so.*",
    "libnvidia-cbl.so.*",
    "libnvidia-cfg.so.*",
    "libnvidia-compiler.so.*",
    "libnvidia-egl-wayland.so.*",
    "libnvidia-eglcore.so.*",
    "libnvidia-encode.so.*",
    "libnvidia-fatbinaryloader.so.*",
    "libnvidia-fbc.so.*",
    "libnvidia-glcore.so.*",
    "libnvidia-glsi.so.*",
    "libnvidia-glvkspirv.so.*",
    "libnvidia-ifr.so.*",
    "libnvidia-ml.so.*",
    "libnvidia-opencl.so.*",
    "libnvidia-opticalflow.so.*",
    "libnvidia-ptxjitcompiler.so.*",
    "libnvidia-rtcore.so.*",
    "libnvidia-tls.so.*",
    "libOpenCL.so.*",
    "libvdpau_nvidia.so.*",
  };
  gsize i;

  g_return_if_fail (patterns != NULL);

  /* Mesa GLX, etc. */
  g_ptr_array_add (patterns, g_strdup ("gl:"));

  for (i = 0; i < G_N_ELEMENTS (sonames); i++)
    g_ptr_array_add (patterns,
                     g_strdup_printf ("if-exists:if-same-abi:soname:%s",
                                      sonames[i]));

  for (i = 0; i < G_N_ELEMENTS (soname_globs_even_if_older); i++)
    g_ptr_array_add (patterns,
                     g_strdup_printf ("if-exists:even-if-older:soname-match:%s",
                                      soname_globs_even_if_older[i]));
}

static void
collect_core_libraries_patterns (GPtrArray *patterns)
{
  /* libudev.so.0 and libudev.so.1 have an ABI that is so close that people
   * sometimes create a symlink libudev.so.0 -> libudev.so.1, even though
   * that's technically incorrect. However, if we capture that library into
   * the container, it breaks our use of ldconfig. */
  static const char * const exact_sonames[] =
  {
    /* If we have libudev from the graphics-stack provider (in practice
     * the host system), it's a lot more likely to be able to understand
     * the data in /run/udev, which is private to the version of udevd
     * and its corresponding libudev. However, it's only safe to do this
     * if it's equal to or newer than the version in the runtime. */
    "libudev.so.1",
    /* Some newer distributions (at least Arch and Debian) have a
     * libudev.so.0 shim implemented in terms of libudev.so.1, which
     * we'll want to use if available. Meanwhile, some older distributions
     * genuinely used libudev.so.0. */
    "libudev.so.0",
  };
  gsize i;

  g_return_if_fail (patterns != NULL);

  for (i = 0; i < G_N_ELEMENTS (exact_sonames); i++)
    g_ptr_array_add (patterns,
                     g_strdup_printf ("if-exists:if-same-abi:exact-soname:%s",
                                      exact_sonames[i]));
}

/*
 * pv_runtime_collect_libc_family:
 * @self: The runtime
 * @arch: Architecture of @libc_symlink
 * @bwrap:
 * @libc_symlink: The symlink created by capsule-capture-libs.
 *  Its target is either `self->provider->path_in_container_ns`
 *  followed by the path to glibc in the graphics stack provider
 *  namespace, or the path to glibc in a non-standard directory such
 *  as /opt with no special prefix.
 * @gconv_in_provider: Map { owned string => itself } representing a set
 *  of paths to /usr/lib/gconv or equivalent in the graphics stack provider
 * @error: Used to raise an error on failure
 */
static gboolean
pv_runtime_collect_libc_family (PvRuntime *self,
                                RuntimeArchitecture *arch,
                                FlatpakBwrap *bwrap,
                                const char *libc_symlink,
                                const char *ld_so_in_runtime,
                                GHashTable *gconv_in_provider,
                                GError **error)
{
  static const char * const patterns[] =
  {
    "if-exists:libidn2.so.0",
    "if-exists:even-if-older:soname-match:libnss_compat.so.*",
    "if-exists:even-if-older:soname-match:libnss_db.so.*",
    "if-exists:even-if-older:soname-match:libnss_dns.so.*",
    "if-exists:even-if-older:soname-match:libnss_files.so.*",
  };
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) libc_timer =
    _srt_profiling_start ("glibc");
  g_autofree char *libc_target = NULL;

  g_return_val_if_fail (self->provider != NULL, FALSE);
  g_return_val_if_fail (bwrap != NULL || self->mutable_sysroot != NULL, FALSE);

  if (!pv_runtime_take_ld_so_from_provider (self, arch,
                                            ld_so_in_runtime,
                                            bwrap, error))
    return FALSE;

  if (!pv_runtime_capture_libraries (self, arch, arch->libdir_in_current_namespace,
                                     NULL, patterns, G_N_ELEMENTS (patterns), error))
    return FALSE;

  libc_target = glnx_readlinkat_malloc (-1, libc_symlink, NULL, NULL);
  if (libc_target != NULL)
    {
      g_autofree gchar *dir = NULL;
      g_autofree gchar *gconv_dir_in_provider = NULL;
      gboolean found = FALSE;
      const char *target_in_provider;
      const char *gconv_prefix;
      gsize n_slashes = 0;

      /* As with pv_runtime_collect_lib_symlink_data(), we need to remove the
       * provider prefix if present. Note that after this, target_in_provider
       * can either be absolute, or relative to the root of the provider. */
      target_in_provider = _srt_get_path_after (libc_target,
                                                self->provider->path_in_container_ns);

      if (target_in_provider == NULL)
        target_in_provider = libc_target;

      /* Either absolute, or relative to the root of the provider */
      dir = g_path_get_dirname (target_in_provider);

      /* Normalize to be relative to the root so we have fewer cases
       * to consider */
      while (dir[n_slashes] == '/')
        n_slashes++;

      if (n_slashes)
        memmove (dir, dir + n_slashes, strlen (dir) - n_slashes + 1);

      g_debug ("glibc directory relative to provider root: %s", dir);

      /* We are assuming that in the glibc "Makeconfig", $(libdir) was the same as
       * $(slibdir) (this is the upstream default) or the same as "/usr$(slibdir)"
       * (like in Debian without the mergerd /usr). We also assume that $(gconvdir)
       * had its default value "$(libdir)/gconv".
       * We prefer /usr because otherwise, if the host is merged-/usr and the
       * container is not, we might end up binding /lib instead of /usr/lib
       * and that could cause issues.
       * Note that this special case is intentionally using g_str_has_prefix()
       * and not flatpak_has_path_prefix(), so that it matches "lib64"
       * or "lib/x86_64-linux-gnu" or similar. */
      if (g_str_has_prefix (dir, "lib"))
        gconv_prefix = "/usr/";
      else
        gconv_prefix = "/";

      gconv_dir_in_provider = g_build_filename (gconv_prefix, dir, "gconv", NULL);
      g_debug ("Checking for gconv in %s", gconv_dir_in_provider);

      if (_srt_file_test_in_sysroot (self->provider->path_in_current_ns,
                                     self->provider->fd,
                                     gconv_dir_in_provider,
                                     G_FILE_TEST_IS_DIR))
        {
          g_hash_table_add (gconv_in_provider, g_steal_pointer (&gconv_dir_in_provider));
          found = TRUE;
        }

      if (!found)
        {
          /* Try again without hwcaps subdirectories.
           * For example, libc6-i386 on SteamOS 2 'brewmaster'
           * contains /lib/i386-linux-gnu/i686/cmov/libc.so.6,
           * for which we want gconv modules from
           * /usr/lib/i386-linux-gnu/gconv, not from
           * /usr/lib/i386-linux-gnu/i686/cmov/gconv. */
          while (g_str_has_suffix (dir, "/cmov") ||
                 g_str_has_suffix (dir, "/i686") ||
                 g_str_has_suffix (dir, "/sse2") ||
                 g_str_has_suffix (dir, "/tls") ||
                 g_str_has_suffix (dir, "/x86_64"))
            {
              char *slash = strrchr (dir, '/');

              g_assert (slash != NULL);
              *slash = '\0';
            }

          g_clear_pointer (&gconv_dir_in_provider, g_free);
          gconv_dir_in_provider = g_build_filename (gconv_prefix, dir, "gconv", NULL);
          g_debug ("Checking for gconv (after removing hwcaps subdirectories) in %s",
                   gconv_dir_in_provider);

          if (_srt_file_test_in_sysroot (self->provider->path_in_current_ns,
                                         self->provider->fd,
                                         gconv_dir_in_provider,
                                         G_FILE_TEST_IS_DIR))
            {
              g_hash_table_add (gconv_in_provider, g_steal_pointer (&gconv_dir_in_provider));
              found = TRUE;
            }
        }

      if (!found)
        {
          g_info ("We were expecting the gconv modules directory in the provider "
                  "to be located in \"%s\", but instead it is missing",
                  gconv_dir_in_provider);
        }
    }

  return TRUE;
}

/*
 * PvRuntimeDataFlags:
 * @PV_RUNTIME_DATA_FLAGS_USR_SHARE_FIRST: If set, look in /usr/share
 *  before attempting to derive a data directory from ${libdir}.
 *  Use this for drivers like the NVIDIA proprietary driver that hard-code
 *  /usr/share rather than having a build-time-configurable prefix.
 * @PV_RUNTIME_DATA_FLAGS_IGNORE_MISSING: Don't log warnings if we can't
 *  find the data. Use this for Vulkan drivers, for which we don't know
 *  which ones came from Mesa.
 * @PV_RUNTIME_DATA_FLAGS_NONE: None of the above.
 *
 * Flags affecting pv_runtime_collect_lib_data().
 */
typedef enum
{
  PV_RUNTIME_DATA_FLAGS_USR_SHARE_FIRST = (1 << 0),
  PV_RUNTIME_DATA_FLAGS_IGNORE_MISSING = (1 << 1),
  PV_RUNTIME_DATA_FLAGS_NONE = 0
} PvRuntimeDataFlags;

/*
 * pv_runtime_collect_lib_data:
 * @self: The runtime
 * @arch: Architecture of @lib_in_provider
 * @dir_basename: Directory in ${datadir}, e.g. `drirc.d`
 * @lib_in_provider: A library in the graphics stack provider, either
 *  absolute or relative to the root of the provider namespace
 * @extra_suffix: (nullable): Subdirectory of ${libdir} where we expect
 *  the library to be located, e.g. `/dri`, or %NULL if none
 * @flags: Flags
 * @data_in_provider: (element-type filename ignored): A map
 *  `{ owned string => itself }` representing the set
 *  of data directories discovered. Each directory is represented
 *  as relative to the root of the provider, e.g. `usr/share/drirc.d`.
 */
static void
pv_runtime_collect_lib_data (PvRuntime *self,
                             RuntimeArchitecture *arch,
                             const char *dir_basename,
                             const char *lib_in_provider,
                             const char *extra_suffix,
                             PvRuntimeDataFlags flags,
                             GHashTable *data_in_provider)
{
  const char *libdir_suffixes[] =
  {
    "/lib/<multiarch>",   /* placeholder, will be replaced */
    "/lib64",
    "/lib32",
    "/lib",
  };
  g_autofree gchar *dir = NULL;
  g_autofree gchar *lib_multiarch = NULL;
  g_autofree gchar *dir_in_provider = NULL;
  g_autofree gchar *dir_in_provider_usr_share = NULL;

  g_return_if_fail (PV_IS_RUNTIME (self));
  g_return_if_fail (self->provider != NULL);
  g_return_if_fail (runtime_architecture_check_valid (arch));
  g_return_if_fail (dir_basename != NULL);
  g_return_if_fail (lib_in_provider != NULL);
  g_return_if_fail (data_in_provider != NULL);

  /* If we are unable to find the lib data in the provider, we try as
   * a last resort `usr/share`. This should help for example Exherbo
   * that uses the unusual `usr/${gnu_tuple}/lib` path for shared
   * libraries.
   *
   * Some libraries, like the NVIDIA proprietary driver, hard-code
   * /usr/share even if they are installed in some other location.
   * For these libraries, we look in this /usr/share-based path
   * *first*. */
  dir_in_provider_usr_share = g_build_filename ("usr", "share", dir_basename, NULL);

  if ((flags & PV_RUNTIME_DATA_FLAGS_USR_SHARE_FIRST)
      && _srt_file_test_in_sysroot (self->provider->path_in_current_ns,
                                    self->provider->fd,
                                    dir_in_provider_usr_share,
                                    G_FILE_TEST_IS_DIR))
    {
      g_debug ("Using \"/%s\" based on hard-coded /usr/share",
               dir_in_provider_usr_share);
      g_hash_table_add (data_in_provider,
                        g_steal_pointer (&dir_in_provider_usr_share));
      return;
    }

  /* lib_in_provider can either be absolute, or relative to the root of
   * the provider: normalize it to relative so we only have to deal with
   * one code path. */
  while (lib_in_provider[0] == '/')
    lib_in_provider++;

  /* Always relative to the root of the provider */
  dir = g_path_get_dirname (lib_in_provider);
  g_return_if_fail (dir[0] != '/');

  /* The logic below works a bit better if we represent the root of the
   * provider (unlikely, but possible) as the empty string */
  if (G_UNLIKELY (strcmp (dir, ".") == 0))
    dir[0] = '\0';

  /* Go up from something like ${libdir}/dri to ${libdir} if necessary */
  if (extra_suffix != NULL && g_str_has_suffix (dir, extra_suffix))
    dir[strlen (dir) - strlen (extra_suffix)] = '\0';

  /* Try to walk up the directory hierarchy from the library directory
   * to find the ${exec_prefix}. We assume that the library directory is
   * either ${exec_prefix}/lib/${multiarch_tuple}, ${exec_prefix}/lib64,
   * ${exec_prefix}/lib32, or ${exec_prefix}/lib.
   *
   * Note that if the library is in /lib, /lib64, etc., this will
   * leave dir empty, but that's OK: dir_in_provider will become
   * something like "share/drirc.d" which will be looked up in the
   * provider namespace. */
  lib_multiarch = g_build_filename ("/lib", arch->details->tuple, NULL);
  libdir_suffixes[0] = lib_multiarch;

  for (gsize i = 0; i < G_N_ELEMENTS (libdir_suffixes); i++)
    {
      if (g_str_has_suffix (dir, libdir_suffixes[i]))
        {
          /* dir might be usr/lib64: truncate to usr. */
          dir[strlen (dir) - strlen (libdir_suffixes[i])] = '\0';
          break;
        }

      if (g_strcmp0 (dir, libdir_suffixes[i] + 1) == 0)
        {
          /* dir is something like lib64: truncate to empty. */
          dir[0] = '\0';
          break;
        }
    }

  /* If ${prefix} and ${exec_prefix} are different, we have no way
   * to predict what the ${prefix} really is; so we are also assuming
   * that the ${exec_prefix} is the same as the ${prefix}.
   *
   * Go back down from the ${prefix} to the data directory,
   * which we assume is ${prefix}/share. (If it isn't, then we have
   * no way to predict what it would be.)
   *
   * As a special exception, if ${exec_prefix} is / then assume the
   * ${datadir} is /usr/share, because there is no /share in the FHS. */
  if (strcmp (dir, "") == 0)
    dir_in_provider = g_build_filename ("usr", "share", dir_basename, NULL);
  else
    dir_in_provider = g_build_filename (dir, "share", dir_basename, NULL);

  g_return_if_fail (dir_in_provider[0] != '/');

  if (_srt_file_test_in_sysroot (self->provider->path_in_current_ns,
                                 self->provider->fd,
                                 dir_in_provider,
                                 G_FILE_TEST_IS_DIR))
    {
      g_debug ("Using \"/%s\" based on library path \"/%s\"",
               dir_in_provider, lib_in_provider);
      g_hash_table_add (data_in_provider,
                        g_steal_pointer (&dir_in_provider));
      return;
    }

  if (!(flags & PV_RUNTIME_DATA_FLAGS_USR_SHARE_FIRST)
      && strcmp (dir_in_provider, dir_in_provider_usr_share) != 0
      && _srt_file_test_in_sysroot (self->provider->path_in_current_ns,
                                    self->provider->fd,
                                    dir_in_provider_usr_share,
                                    G_FILE_TEST_IS_DIR))
    {
      g_debug ("Using \"/%s\" based on fallback to /usr/share, because "
               "\"/%s\" based on \"/%s\" is not a directory",
               dir_in_provider_usr_share, dir_in_provider, lib_in_provider);
      g_hash_table_add (data_in_provider,
                        g_steal_pointer (&dir_in_provider_usr_share));
      return;
    }

  if (flags & PV_RUNTIME_DATA_FLAGS_IGNORE_MISSING)
    {
      g_debug ("Did not find %s adjacent to \"%s\", probably not a problem",
               dir_basename, lib_in_provider);
      return;
    }

  if (g_strcmp0 (dir_in_provider, dir_in_provider_usr_share) == 0)
    g_info ("We were expecting the %s directory in the provider to "
            "be located in \"/%s\" based on \"/%s\", but instead it is missing",
            dir_basename, dir_in_provider, lib_in_provider);
  else
    g_info ("We were expecting the %s directory in the provider to "
            "be located in \"/%s\" or \"/%s\" based on \"/%s\", but "
            "instead it is missing",
            dir_basename, dir_in_provider, dir_in_provider_usr_share,
            lib_in_provider);
}

/*
 * pv_runtime_collect_lib_symlink_data:
 * @self: The runtime
 * @arch: Architecture of @lib_symlink
 * @dir_basename: Directory in ${datadir}, e.g. `drirc.d`
 * @lib_symlink: The symlink created by capsule-capture-libs.
 *  Its target is either `self->provider->path_in_container_ns`
 *  followed by the path to a library in the graphics stack provider
 *  namespace, or the path to a library in a non-standard directory such
 *  as /opt with no special prefix.
 * @flags: Flags
 * @data_in_provider: Map { owned string => itself } representing the set
 *  of data directories discovered
 *
 * Returns: %TRUE if @lib_symlink exists and is a symlink
 */
static gboolean
pv_runtime_collect_lib_symlink_data (PvRuntime *self,
                                     RuntimeArchitecture *arch,
                                     const char *dir_basename,
                                     const char *lib_symlink,
                                     PvRuntimeDataFlags flags,
                                     GHashTable *data_in_provider)
{
  g_autofree char *target = NULL;
  const char *target_in_provider;

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (self->provider != NULL, FALSE);
  g_return_val_if_fail (runtime_architecture_check_valid (arch), FALSE);
  g_return_val_if_fail (dir_basename != NULL, FALSE);
  g_return_val_if_fail (lib_symlink != NULL, FALSE);
  g_return_val_if_fail (data_in_provider != NULL, FALSE);

  target = glnx_readlinkat_malloc (-1, lib_symlink, NULL, NULL);

  if (target == NULL)
    return FALSE;

  /* There are two possibilities for a symlink created by
   * capsule-capture-libs.
   *
   * If capsule-capture-libs found a library in /app, /usr
   * or /lib* (as configured by --remap-link-prefix in
   * pv_runtime_get_capsule_capture_libs()), then the symlink will
   * point to something like /run/host/lib/libfoo.so or
   * /run/gfx/usr/lib64/libbar.so. To find the corresponding path
   * in the graphics stack provider, we can remove the /run/host
   * or /run/gfx prefix.
   *
   * If capsule-capture-libs found a library elsewhere, for example
   * in $HOME or /opt, then we assume it will be visible at the same
   * path in both the graphics stack provider and the final container.
   * In practice this is unlikely to happen unless the graphics stack
   * provider is the same as the current namespace. We do not remove
   * any prefix in this case.
   *
   * Note that after this, target_in_provider can either be absolute,
   * or relative to the root of the provider. */

  target_in_provider = _srt_get_path_after (target,
                                            self->provider->path_in_container_ns);

  if (target_in_provider == NULL)
    target_in_provider = target;

  pv_runtime_collect_lib_data (self, arch, dir_basename,
                               target_in_provider, NULL, flags,
                               data_in_provider);
  return TRUE;
}

static void
collect_one_mesa_drirc (PvRuntime *self,
                        RuntimeArchitecture *arch,
                        const IcdDetails *details,
                        PvRuntimeDataFlags flags,
                        GHashTable *drirc_data_in_provider)
{
  g_autofree gchar *symlink = NULL;
  const gsize multiarch_index = arch->multiarch_index;
  const char *resolved;

  /* This is assumed to be called after collecting ICDs */
  resolved = details->resolved_libraries[multiarch_index];

  switch (details->kinds[multiarch_index])
    {
      case ICD_KIND_ABSOLUTE:
        g_return_if_fail (resolved != NULL);
        pv_runtime_collect_lib_data (self, arch, "drirc.d", resolved,
                                     NULL, flags, drirc_data_in_provider);
        break;

      case ICD_KIND_SONAME:
        /* We already created a symlink in /overrides pointing to the
         * path in the container namespace, which is the same as the
         * path in the provider namespace, but with an optional prefix
         * that we already know how to remove (/run/host or /run/gfx). */
        g_return_if_fail (resolved != NULL);
        symlink = g_build_filename (arch->libdir_in_current_namespace,
                                    glnx_basename (resolved), NULL);
        pv_runtime_collect_lib_symlink_data (self, arch, "drirc.d", symlink,
                                             flags, drirc_data_in_provider);
        break;

      case ICD_KIND_NONEXISTENT:
      case ICD_KIND_META_LAYER:
      default:
        /* Nothing to do - we can't know the path because there is none */
        break;
    }
}

/*
 * For each driver provided by Mesa, other than GLX which is handled
 * elsewhere, look for share/drirc.d nearby.
 *
 * This currently means:
 * - The EGL ICD described in 50_mesa.json (libEGL_mesa.so.0), assumed
 *   to be in ${libdir}
 * - All Vulkan ICDs (we cannot tell which ones came from Mesa!)
 * - All DRI drivers (which are all implicitly from Mesa)
 */
static void
collect_mesa_drirc (PvRuntime *self,
                    RuntimeArchitecture *arch,
                    GPtrArray *egl_icd_details,
                    GPtrArray *vulkan_icd_details,
                    SrtSystemInfo *system_info,
                    GHashTable *drirc_data_in_provider)
{
  g_autoptr(SrtObjectList) dri_drivers = NULL;

  for (guint i = 0; i < egl_icd_details->len; i++)
    {
      IcdDetails *details = g_ptr_array_index (egl_icd_details, i);
      const gsize multiarch_index = arch->multiarch_index;
      const char *resolved;
      const char *base;

      /* This is assumed to be called after collecting ICDs */
      resolved = details->resolved_libraries[multiarch_index];

      if (resolved == NULL)
        continue;

      base = glnx_basename (resolved);

      if (strstr (base, "libEGL_mesa.so") != NULL)
        collect_one_mesa_drirc (self, arch, details,
                                PV_RUNTIME_DATA_FLAGS_NONE,
                                drirc_data_in_provider);
      else
        g_debug ("Assuming \"%s\" is not from Mesa", resolved);
    }

  for (guint i = 0; i < vulkan_icd_details->len; i++)
    {
      IcdDetails *details = g_ptr_array_index (vulkan_icd_details, i);

      /* We don't know which Vulkan ICDs are from Mesa (currently
       * libvulkan_intel.so, libvulkan_lvp.so and libvulkan_radeon.so,
       * but there could be more in future), so we have to assume
       * that all of them are *potentially* Mesa. */
      collect_one_mesa_drirc (self, arch, details,
                              PV_RUNTIME_DATA_FLAGS_IGNORE_MISSING,
                              drirc_data_in_provider);
    }

  /* We assume that by the time we get here, this is already cached,
   * so its time cost will be trivial and therefore there's no need to
   * do additional profiling */
  dri_drivers = srt_system_info_list_dri_drivers (system_info,
                                                  arch->details->tuple,
                                                  SRT_DRIVER_FLAGS_NONE);

  for (const GList *icd_iter = dri_drivers;
       icd_iter != NULL;
       icd_iter = icd_iter->next)
    {
      g_autofree gchar *resolved = NULL;

      resolved = srt_dri_driver_resolve_library_path (icd_iter->data);
      g_return_if_fail (g_path_is_absolute (resolved));
      pv_runtime_collect_lib_data (self, arch, "drirc.d", resolved,
                                   "/dri", PV_RUNTIME_DATA_FLAGS_NONE,
                                   drirc_data_in_provider);
    }
}

/*
 * pv_runtime_finish_lib_data:
 * @self: The runtime
 * @bwrap: Arguments for bubblewrap
 * @dir_basename: Name of a data directory below ${datadir}, such as
 *  `drirc.d`
 * @lib_name: Library we used to populate @data_in_provider, only used
 *  in diagnostic messages
 * @all_from_provider: %TRUE if the instances of @lib_name for all
 *  architectures came from the graphics stack provider
 * @data_in_provider: (element-type filename ignored): A set of
 *  filenames for @dir_basename relative to the root of the graphics
 *  stack provider, for example `{ "usr/share/drirc.d", "app/share/drirc.d" }`
 * @error: Used to raise an error on failure
 *
 * Make each path in @data_in_provider available in the final container
 * at the same path.
 *
 * Additionally, make one of them available at `usr/share/` + @dir_basename.
 *
 * Returns: %TRUE on success
 */
static gboolean
pv_runtime_finish_lib_data (PvRuntime *self,
                            FlatpakBwrap *bwrap,
                            const gchar *dir_basename,
                            const gchar *lib_name,
                            gboolean all_from_provider,
                            GHashTable *data_in_provider,
                            GError **error)
{
  g_autofree gchar *canonical_path = NULL;
  g_auto(SrtHashTableIter) iter = SRT_HASH_TABLE_ITER_CLEARED;
  const gchar *data_path = NULL;

  g_return_val_if_fail (self->provider != NULL, FALSE);
  g_return_val_if_fail (bwrap != NULL || self->mutable_sysroot != NULL, FALSE);
  g_return_val_if_fail (dir_basename != NULL, FALSE);

  canonical_path = g_build_filename ("usr", "share", dir_basename, NULL);

  if (g_hash_table_size (data_in_provider) > 0 && !all_from_provider)
    {
      /* See the explanation in the similar
       * "any_libc_from_provider && !all_libc_from_provider" case, above */
      g_warning ("Using %s from provider system for some but not all "
                 "architectures! Will take /usr/share/%s from provider.",
                 lib_name, dir_basename);
    }

  /* We might have more than one data directory in the provider,
   * e.g. one for each supported multiarch tuple */

  _srt_hash_table_iter_init_sorted (&iter, data_in_provider,
                                    self->arbitrary_str_order);

  while (_srt_hash_table_iter_next (&iter, &data_path, NULL))
    {
      g_warn_if_fail (data_path != NULL && data_path[0] != '/');

      /* If we found a library at foo/lib/libbar.so.0 and then found its
       * data in foo/share/bar, it's reasonable to expect that libbar
       * will still be looking for foo/share/bar in the container. */
      if (!pv_runtime_take_from_provider (self, bwrap,
                                          data_path, data_path,
                                          (TAKE_FROM_PROVIDER_FLAGS_IF_DIR
                                           | TAKE_FROM_PROVIDER_FLAGS_IF_CONTAINER_COMPATIBLE),
                                          error))
        return FALSE;

      if (self->is_flatpak_env
          && g_str_has_prefix (data_path, "app/lib/"))
        {
          /* In a freedesktop.org runtime, for some multiarch, there is
           * a symlink usr/lib/${arch} that points to app/lib/${arch}
           * https://gitlab.com/freedesktop-sdk/freedesktop-sdk/-/blob/70cb5835/elements/multiarch/multiarch-platform.bst#L24
           * If we have a path in app/lib/ here, we also try to
           * replicate the symlink in usr/lib/ */
          g_autofree gchar *path_in_usr = NULL;
          path_in_usr = g_build_filename ("usr",
                                          data_path + strlen ("app"),
                                          NULL);
          if (_srt_fstatat_is_same_file (-1, data_path, -1, path_in_usr))
            {
              if (!pv_runtime_take_from_provider (self, bwrap,
                                                  data_path, path_in_usr,
                                                  TAKE_FROM_PROVIDER_FLAGS_IF_DIR,
                                                  error))
                return FALSE;
            }
        }
    }

  /* In the common case where data_in_provider contains canonical_path,
   * we have already made it available at canonical_path in the container.
   * Nothing more to do here. */
  if (g_hash_table_contains (data_in_provider, canonical_path))
    return TRUE;

  /* In the uncommon case where data_in_provider *does not* contain
   * canonical_path - for example data_in_provider = { usr/local/share/drirc.d }
   * but canonical_path is usr/share/drirc.d - we'll mount it over
   * canonical_path as well, just in case something has hard-coded
   * that path and is expecting to find something consistent there.
   *
   * If data_in_provider contains more than one - for example if we
   * found the x86_64 library in usr/lib/x86_64-linux-gnu but the
   * i386 library in app/lib/i386-linux-gnu, as we do in Flatpak -
   * then we don't have a great way to choose between them, so just
   * pick one and hope for the best. In Flatpak, it is normal for
   * this to happen because of the way multiarch has been implemented,
   * but we know that both are very likely to be up-to-date, so we
   * can pick either one and be happy. Otherwise, we'll warn in this
   * case. */
  if (!self->is_flatpak_env
      && g_hash_table_size (data_in_provider) > 1)
    {
      g_warning ("Found more than one possible %s data directory from provider",
                 dir_basename);
    }

  data_path = pv_hash_table_get_first_key (data_in_provider,
                                           self->arbitrary_str_order);

  if (data_path != NULL)
    return pv_runtime_take_from_provider (self, bwrap,
                                          data_path,
                                          canonical_path,
                                          TAKE_FROM_PROVIDER_FLAGS_IF_CONTAINER_COMPATIBLE,
                                          error);
  else
    return TRUE;
}

static gboolean
pv_runtime_take_misc_data_from_provider (PvRuntime *self,
                                         FlatpakBwrap *bwrap,
                                         GError **error)
{
  static const char * const pci_ids_paths[] =
    {
      "/usr/share/misc/pci.ids",
      "/usr/share/hwdata/pci.ids",
      "/usr/share/pci.ids",
      NULL
    };

  if (!pv_runtime_take_any_from_provider (self, bwrap, pci_ids_paths,
                                          "/usr/share/misc/pci.ids",
                                          TAKE_FROM_PROVIDER_FLAGS_IF_REGULAR,
                                          error))
    return FALSE;

  return TRUE;
}

static gboolean
pv_runtime_finish_libc_family (PvRuntime *self,
                               FlatpakBwrap *bwrap,
                               GHashTable *gconv_in_provider,
                               GError **error)
{
  g_auto(SrtHashTableIter) iter = SRT_HASH_TABLE_ITER_CLEARED;
  const gchar *gconv_path;
  gsize i;
  /* List of paths where we expect to find "locale", sorted by the most
   * preferred to the least preferred.
   * If the canonical "/usr/lib/locale" is missing, we try the Exherbo's
   * "/usr/${gnu_tuple}/lib/locale" too, before giving up.
   * The locale directory is actually architecture-independent, so we just
   * arbitrarily prefer to use "x86_64-pc-linux-gnu" over the 32-bit couterpart */
  static const gchar * const lib_locale_path[] = {
    "/usr/lib/locale",
#if defined(__i386__) || defined(__x86_64__)
    "/usr/x86_64-pc-linux-gnu/lib/locale",
    "/usr/i686-pc-linux-gnu/lib/locale",
#elif defined(__aarch64__)
    "/usr/aarch64-unknown-linux-gnueabi/lib/locale",
#endif
    NULL
  };
  static const struct
  {
    const char *executable;
    const char *target_path;
    enum { OPTIONAL, IMPORTANT, ESSENTIAL } priority;
  } glibc_executables[] =
  {
    /* This is basically the libc-bin Debian package, which is
     * marked Essential. At least ldd can fail to work if it is too
     * dissimilar to the libc.so.6 in use. */
    { "catchsegv" },
    { "getconf" },
    { "getent" },
    { "iconv" },
    { "ldconfig", .priority = ESSENTIAL, .target_path = "/sbin/ldconfig" },
    /* In Ubuntu and old Debian releases (Debian 8 or older), /sbin/ldconfig
     * is a shell script wrapper around the real binary /sbin/ldconfig.real,
     * working around lack of dpkg trigger support in old library packages. */
    { "ldconfig.real", .target_path = "/sbin/ldconfig.real" },
    { "ldd", .priority = IMPORTANT },
    { "locale", .priority = IMPORTANT },
    { "localedef", .priority = IMPORTANT },
    { "pldd" },
    { "tzselect" },
    { "zdump" },
    /* We probably don't need developer tools gencat, rpcgen, memusage,
     * memusagestat, mtrace, sotruss, sprof from libc-dev-bin, libc-devtools
     * (and some have non-trivial dependencies). */
    /* We probably don't need sysadmin tools /usr/sbin/iconvconfig,
     * /usr/sbin/zic from libc-bin. */
  };

  g_return_val_if_fail (self->provider != NULL, FALSE);
  g_return_val_if_fail (bwrap != NULL || self->mutable_sysroot != NULL, FALSE);

  if (self->any_libc_from_provider && !self->all_libc_from_provider)
    {
      /*
       * This shouldn't happen. It would mean that there exist at least
       * two architectures (let's say aaa and bbb) for which we have:
       * provider libc6:aaa < container libc6 < provider libc6:bbb
       * (we know that the container's libc6:aaa and libc6:bbb are
       * constrained to be the same version because that's how multiarch
       * works).
       *
       * If the provider system locales work OK with both the aaa and bbb
       * versions, let's assume they will also work with the intermediate
       * version from the container...
       */
      g_warning ("Using glibc from provider system for some but not all "
                 "architectures! Arbitrarily using provider locales.");
    }

  if (self->any_libc_from_provider)
    {
      g_debug ("Making provider locale data visible in container");

      if (!pv_runtime_take_any_from_provider (self, bwrap, lib_locale_path,
                                              "/usr/lib/locale",
                                              TAKE_FROM_PROVIDER_FLAGS_IF_DIR,
                                              error))
        return FALSE;

      if (!pv_runtime_take_from_provider (self, bwrap,
                                          "/usr/share/i18n",
                                          "/usr/share/i18n",
                                          TAKE_FROM_PROVIDER_FLAGS_IF_EXISTS,
                                          error))
        return FALSE;

      for (i = 0; i < G_N_ELEMENTS (glibc_executables); i++)
        {
          g_autoptr(GError) local_error = NULL;
          const char *executable = glibc_executables[i].executable;
          g_autofree char *provider_impl = NULL;
          g_autofree char *target_path_alloc = NULL;
          const char *target_path = glibc_executables[i].target_path;
          TakeFromProviderFlags flags;

          provider_impl = pv_graphics_provider_search_in_path_and_bin (self->provider,
                                                                       executable);

          if (target_path == NULL)
            {
              target_path_alloc = g_build_filename ("/usr/bin", executable, NULL);
              target_path = target_path_alloc;
            }

          if (glibc_executables[i].priority >= ESSENTIAL)
            flags = TAKE_FROM_PROVIDER_FLAGS_NONE;
          else
            flags = TAKE_FROM_PROVIDER_FLAGS_IF_CONTAINER_COMPATIBLE;

          if (provider_impl == NULL)
            {
              if (glibc_executables[i].priority >= IMPORTANT)
                g_warning ("Cannot find %s", executable);
              else
                g_debug ("Cannot find %s", executable);
            }
          else if (!pv_runtime_take_from_provider (self, bwrap, provider_impl,
                                                   target_path,
                                                   flags,
                                                   &local_error))
            {
              if (glibc_executables[i].priority >= IMPORTANT)
                {
                  g_propagate_error (error, g_steal_pointer (&local_error));
                  return FALSE;
                }
              else
                {
                  g_debug ("Cannot take %s from provider, ignoring: %s",
                           provider_impl, local_error->message);
                  g_clear_error (&local_error);
                }
            }
        }

      g_debug ("Making provider gconv modules visible in container");

      _srt_hash_table_iter_init_sorted (&iter, gconv_in_provider,
                                        self->arbitrary_str_order);

      while (_srt_hash_table_iter_next (&iter, &gconv_path, NULL))
        {
          if (!pv_runtime_take_from_provider (self, bwrap,
                                              gconv_path,
                                              gconv_path,
                                              TAKE_FROM_PROVIDER_FLAGS_IF_DIR,
                                              error))
            return FALSE;
        }
    }
  else
    {
      g_debug ("Using included locale data from container");
      g_debug ("Using included gconv modules from container");
    }

  return TRUE;
}

static gboolean
pv_runtime_create_aliases (PvRuntime *self,
                           RuntimeArchitecture *arch,
                           GError **error)
{
  g_autoptr(JsonParser) parser = NULL;
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer =
    _srt_profiling_start ("Creating library aliases");
  JsonNode *node = NULL;
  JsonArray *libraries_array = NULL;
  JsonArray *aliases_array = NULL;
  JsonObject *object;

  if (self->runtime_abi_json == NULL)
    {
      g_info ("Runtime ABI JSON not present, not creating library aliases");
      return TRUE;
    }

  parser = json_parser_new ();
  if (!json_parser_load_from_file (parser, self->runtime_abi_json, error))
    return glnx_prefix_error (error, "Error parsing the expected JSON object in \"%s\"",
                              self->runtime_abi_json);

  node = json_parser_get_root (parser);
  object = json_node_get_object (node);

  if (!json_object_has_member (object, "shared_libraries"))
    return glnx_throw (error, "No \"shared_libraries\" in the JSON object \"%s\"",
                       self->runtime_abi_json);

  libraries_array = json_object_get_array_member (object, "shared_libraries");
  if (libraries_array == NULL || json_array_get_length (libraries_array) == 0)
    return TRUE;

  for (guint i = 0; i < json_array_get_length (libraries_array); i++)
    {
      const gchar *soname = NULL;
      g_autofree gchar *soname_in_runtime = NULL;
      g_autofree gchar *soname_in_runtime_usr = NULL;
      g_autofree gchar *soname_in_overrides = NULL;
      g_autofree gchar *target = NULL;
      g_autoptr(GList) members = NULL;

      node = json_array_get_element (libraries_array, i);
      if (!JSON_NODE_HOLDS_OBJECT (node))
        continue;

      object = json_node_get_object (node);

      members = json_object_get_members (object);
      if (members == NULL)
        continue;

      soname = members->data;

      object = json_object_get_object_member (object, soname);
      if (!json_object_has_member (object, "aliases"))
        continue;

      aliases_array = json_object_get_array_member (object, "aliases");
      if (aliases_array == NULL || json_array_get_length (aliases_array) == 0)
        continue;

      soname_in_overrides = g_build_filename (arch->libdir_in_current_namespace,
                                              soname, NULL);
      soname_in_runtime_usr = g_build_filename (self->runtime_usr, "lib",
                                                arch->details->tuple, soname, NULL);
      /* We are not always in a merged-/usr runtime, e.g. if we are using a
       * "sysroot" runtime. */
      soname_in_runtime = g_build_filename (self->runtime_files, "lib",
                                            arch->details->tuple, soname, NULL);

      if (g_file_test (soname_in_overrides,
                       (G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_SYMLINK)))
        target = g_build_filename (arch->libdir_in_container, soname, NULL);
      else if (g_file_test (soname_in_runtime_usr,
                            (G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_SYMLINK)))
        target = g_build_filename ("/usr/lib", arch->details->tuple, soname, NULL);
      else if (g_file_test (soname_in_runtime,
                            (G_FILE_TEST_IS_REGULAR | G_FILE_TEST_IS_SYMLINK)))
        target = g_build_filename ("/lib", arch->details->tuple, soname, NULL);
      else
        return glnx_throw (error, "The expected library %s is missing from both the runtime "
                           "and the \"overrides\" directory", soname);

      for (guint j = 0; j < json_array_get_length (aliases_array); j++)
        {
          g_autofree gchar *dest = g_build_filename (arch->aliases_in_current_namespace,
                                                     json_array_get_string_element (aliases_array, j),
                                                     NULL);
          if (symlink (target, dest) != 0)
            return glnx_throw_errno_prefix (error,
                                            "Unable to create symlink %s -> %s",
                                            dest, target);
        }
    }
  return TRUE;
}

/*
 * @egl_icd_details: (element-type IcdDetails):
 * @patterns: (element-type filename):
 */
static gboolean
collect_egl_drivers (PvRuntime *self,
                     RuntimeArchitecture *arch,
                     GPtrArray *egl_icd_details,
                     GPtrArray *patterns,
                     GError **error)
{
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer =
    _srt_profiling_start ("Collecting EGL drivers");
  gsize j;
  /* As with Vulkan layers, the order of the manifests matters
   * but the order of the actual libraries does not. */
  gboolean use_numbered_subdirs = FALSE;
  const gsize multiarch_index = arch->multiarch_index;

  g_debug ("Collecting %s EGL drivers from provider...",
           arch->details->tuple);

  for (j = 0; j < egl_icd_details->len; j++)
    {
      IcdDetails *details = g_ptr_array_index (egl_icd_details, j);
      SrtEglIcd *icd = SRT_EGL_ICD (details->icd);

      g_assert (details->resolved_libraries[multiarch_index] == NULL);

      if (!srt_egl_icd_check_error (icd, NULL))
        continue;

      details->resolved_libraries[multiarch_index] = srt_egl_icd_resolve_library_path (icd);
      g_assert (details->resolved_libraries[multiarch_index] != NULL);
    }

  if (!bind_icds (self, arch, "glvnd",
                  (IcdDetails **) egl_icd_details->pdata,
                  egl_icd_details->len,
                  &use_numbered_subdirs, patterns, NULL, error))
    return FALSE;

  return TRUE;
}

/*
 * @egl_ext_platform_details: (element-type IcdDetails):
 * @patterns: (element-type filename):
 */
static gboolean
collect_egl_ext_platforms (PvRuntime *self,
                           RuntimeArchitecture *arch,
                           GPtrArray *egl_ext_platform_details,
                           GPtrArray *patterns,
                           GError **error)
{
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer =
    _srt_profiling_start ("Collecting EGL external platforms");
  gsize j;
  /* As with Vulkan layers, the order of the manifests matters
   * but the order of the actual libraries does not. */
  gboolean use_numbered_subdirs = FALSE;
  const gsize multiarch_index = arch->multiarch_index;

  g_debug ("Collecting %s EGL external platforms from provider...",
           arch->details->tuple);

  for (j = 0; j < egl_ext_platform_details->len; j++)
    {
      IcdDetails *details = g_ptr_array_index (egl_ext_platform_details, j);
      SrtEglExternalPlatform *ext = SRT_EGL_EXTERNAL_PLATFORM (details->icd);

      g_assert (details->resolved_libraries[multiarch_index] == NULL);

      if (!srt_egl_external_platform_check_error (ext, NULL))
        continue;

      details->resolved_libraries[multiarch_index] = srt_egl_external_platform_resolve_library_path (ext);
      g_assert (details->resolved_libraries[multiarch_index] != NULL);
    }

  if (!bind_icds (self, arch, "egl_external_platform",
                  (IcdDetails **) egl_ext_platform_details->pdata,
                  egl_ext_platform_details->len,
                  &use_numbered_subdirs, patterns, NULL, error))
    return FALSE;

  return TRUE;
}

/*
 * @vulkan_icd_details: (element-type IcdDetails):
 * @patterns: (element-type filename):
 */
static gboolean
collect_vulkan_icds (PvRuntime *self,
                     RuntimeArchitecture *arch,
                     GPtrArray *vulkan_icd_details,
                     GPtrArray *patterns,
                     GError **error)
{
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer =
    _srt_profiling_start ("Collecting Vulkan ICDs");
  gsize j;
  /* As with Vulkan layers, the order of the manifests matters
   * but the order of the actual libraries does not. */
  gboolean use_numbered_subdirs = FALSE;
  const gsize multiarch_index = arch->multiarch_index;

  g_debug ("Collecting %s Vulkan drivers from provider...",
           arch->details->tuple);

  for (j = 0; j < vulkan_icd_details->len; j++)
    {
      IcdDetails *details = g_ptr_array_index (vulkan_icd_details, j);
      SrtVulkanIcd *icd = SRT_VULKAN_ICD (details->icd);

      g_assert (details->resolved_libraries[multiarch_index] == NULL);

      if (!srt_vulkan_icd_check_error (icd, NULL))
        continue;

      details->resolved_libraries[multiarch_index] = srt_vulkan_icd_resolve_library_path (icd);
      g_assert (details->resolved_libraries[multiarch_index] != NULL);
    }

  if (!bind_icds (self, arch, "vulkan",
                  (IcdDetails **) vulkan_icd_details->pdata,
                  vulkan_icd_details->len,
                  &use_numbered_subdirs, patterns, NULL, error))
    return FALSE;

  return TRUE;
}

/*
 * @patterns: (element-type filename):
 */
static gboolean
collect_vdpau_drivers (PvRuntime *self,
                       SrtSystemInfo *system_info,
                       RuntimeArchitecture *arch,
                       GPtrArray *patterns,
                       GError **error)
{
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer =
    _srt_profiling_start ("Collecting VDPAU drivers");
  g_autoptr(GPtrArray) details_arr = NULL;
  g_autoptr(SrtObjectList) vdpau_drivers = NULL;
  /* The VDPAU loader looks up drivers by name, not by readdir(),
   * so order doesn't matter unless there are name collisions. */
  gboolean use_numbered_subdirs = FALSE;
  const GList *icd_iter;
  gsize j;
  const gsize multiarch_index = arch->multiarch_index;

  g_debug ("Enumerating %s VDPAU ICDs on provider...", arch->details->tuple);
    {
      G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) enum_timer =
        _srt_profiling_start ("Enumerating VDPAU drivers");

      vdpau_drivers = srt_system_info_list_vdpau_drivers (system_info,
                                                          arch->details->tuple,
                                                          SRT_DRIVER_FLAGS_NONE);
    }

  details_arr = g_ptr_array_new_full (g_list_length (vdpau_drivers),
                                      (GDestroyNotify) G_CALLBACK (icd_details_free));

  for (icd_iter = vdpau_drivers, j = 0; icd_iter != NULL; icd_iter = icd_iter->next, j++)
    {
      g_autoptr(IcdDetails) details = icd_details_new (icd_iter->data);

      g_assert (details->resolved_libraries[multiarch_index] == NULL);
      details->resolved_libraries[multiarch_index] = srt_vdpau_driver_resolve_library_path (details->icd);
      g_assert (details->resolved_libraries[multiarch_index] != NULL);
      g_assert (g_path_is_absolute (details->resolved_libraries[multiarch_index]));
      g_ptr_array_add (details_arr, g_steal_pointer (&details));
    }

  /* In practice we won't actually use the sequence number for VDPAU
   * because they can only be located in a single directory,
   * so by definition we can't have collisions. Anything that
   * ends up in a numbered subdirectory won't get used. */
  if (!bind_icds (self, arch, "vdpau",
                  (IcdDetails **) details_arr->pdata,
                  details_arr->len,
                  &use_numbered_subdirs, patterns, NULL, error))
    return FALSE;

  for (j = 0; j < details_arr->len; j++)
    {
      IcdDetails *details = g_ptr_array_index (details_arr, j);

      /* Because the path is always absolute, ICD_KIND_SONAME makes
       * no sense */
      g_assert (details->kinds[multiarch_index] != ICD_KIND_SONAME);
    }

  return TRUE;
}

/*
 * @patterns: (element-type filename):
 */
static gboolean
collect_dri_drivers (PvRuntime *self,
                     SrtSystemInfo *system_info,
                     RuntimeArchitecture *arch,
                     GPtrArray *patterns,
                     GString *dri_path,
                     GError **error)
{
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer =
    _srt_profiling_start ("Collecting DRI and VA-API drivers");
  g_autoptr(GPtrArray) details_arr = NULL;
  g_autoptr(SrtObjectList) dri_drivers = NULL;
  g_autoptr(SrtObjectList) va_api_drivers = NULL;
  /* The DRI loader looks up drivers by name, not by readdir(),
   * so order doesn't matter unless there are name collisions. */
  gboolean use_numbered_subdirs = FALSE;
  const GList *icd_iter;
  gsize j;
  const gsize multiarch_index = arch->multiarch_index;

  g_debug ("Enumerating %s DRI drivers on provider...",
           arch->details->tuple);
    {
      G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) enum_timer =
        _srt_profiling_start ("Enumerating DRI drivers");

      dri_drivers = srt_system_info_list_dri_drivers (system_info,
                                                      arch->details->tuple,
                                                      SRT_DRIVER_FLAGS_NONE);
    }

  g_debug ("Enumerating %s VA-API drivers on provider...",
           arch->details->tuple);
    {
      G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) enum_timer =
        _srt_profiling_start ("Enumerating VA-API drivers");

      va_api_drivers = srt_system_info_list_va_api_drivers (system_info,
                                                            arch->details->tuple,
                                                            SRT_DRIVER_FLAGS_NONE);
    }

  details_arr = g_ptr_array_new_full (g_list_length (dri_drivers) + g_list_length (va_api_drivers),
                                      (GDestroyNotify) G_CALLBACK (icd_details_free));

  for (icd_iter = dri_drivers, j = 0; icd_iter != NULL; icd_iter = icd_iter->next, j++)
    {
      g_autoptr(IcdDetails) details = icd_details_new (icd_iter->data);

      g_assert (details->resolved_libraries[multiarch_index] == NULL);
      details->resolved_libraries[multiarch_index] = srt_dri_driver_resolve_library_path (details->icd);
      g_assert (details->resolved_libraries[multiarch_index] != NULL);
      g_assert (g_path_is_absolute (details->resolved_libraries[multiarch_index]));
      g_ptr_array_add (details_arr, g_steal_pointer (&details));
    }

  for (icd_iter = va_api_drivers, j = 0; icd_iter != NULL; icd_iter = icd_iter->next, j++)
    {
      g_autoptr(IcdDetails) details = icd_details_new (icd_iter->data);

      g_assert (details->resolved_libraries[multiarch_index] == NULL);
      details->resolved_libraries[multiarch_index] = srt_va_api_driver_resolve_library_path (details->icd);
      g_assert (details->resolved_libraries[multiarch_index] != NULL);
      g_assert (g_path_is_absolute (details->resolved_libraries[multiarch_index]));
      g_ptr_array_add (details_arr, g_steal_pointer (&details));
    }

  if (!bind_icds (self, arch, "dri",
                  (IcdDetails **) details_arr->pdata,
                  details_arr->len,
                  &use_numbered_subdirs, patterns, dri_path, error))
    return FALSE;

  for (j = 0; j < details_arr->len; j++)
    {
      IcdDetails *details = g_ptr_array_index (details_arr, j);

      /* Because the path is always absolute, ICD_KIND_SONAME makes
       * no sense */
      g_assert (details->kinds[multiarch_index] != ICD_KIND_SONAME);
    }

  return TRUE;
}

/*
 * @search_path: (inout):
 */
static void
pv_append_host_dri_library_paths (PvRuntime *self,
                                  SrtSystemInfo *system_info,
                                  const char *multiarch_tuple,
                                  GString *search_path)
{
  g_autoptr(SrtObjectList) dri_drivers = NULL;
  g_autoptr(SrtObjectList) va_api_drivers = NULL;
  g_autoptr(GHashTable) drivers_set = NULL;
  const GList *icd_iter;
  g_auto(SrtHashTableIter) iter = SRT_HASH_TABLE_ITER_CLEARED;
  const gchar *drivers_path = NULL;
  gsize i;

  g_return_if_fail (search_path != NULL);

  drivers_set = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  g_debug ("Enumerating %s DRI drivers on host...", multiarch_tuple);
  {
    G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) enum_timer =
      _srt_profiling_start ("Enumerating host DRI drivers");

    dri_drivers = srt_system_info_list_dri_drivers (system_info,
                                                    multiarch_tuple,
                                                    SRT_DRIVER_FLAGS_NONE);
  }

  for (icd_iter = dri_drivers, i = 0; icd_iter != NULL; icd_iter = icd_iter->next, i++)
    {
      g_assert (SRT_IS_DRI_DRIVER (icd_iter->data));
      SrtDriDriver *driver = icd_iter->data;
      g_autofree gchar *driver_path = NULL;
      const gchar *lib_path = srt_dri_driver_get_library_path (driver);

      g_debug ("Found DRI driver: %s", lib_path);
      driver_path = g_path_get_dirname (lib_path);
      if (!g_hash_table_contains (drivers_set, driver_path))
        g_hash_table_add (drivers_set, g_steal_pointer (&driver_path));
    }

  g_debug ("Enumerating %s VA-API drivers on host...", multiarch_tuple);
    {
      G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) enum_timer =
        _srt_profiling_start ("Enumerating host VA-API drivers");

      va_api_drivers = srt_system_info_list_va_api_drivers (system_info,
                                                            multiarch_tuple,
                                                            SRT_DRIVER_FLAGS_NONE);
    }

  for (icd_iter = va_api_drivers, i = 0; icd_iter != NULL; icd_iter = icd_iter->next, i++)
    {
      g_assert (SRT_IS_VA_API_DRIVER (icd_iter->data));
      SrtVaApiDriver *driver = icd_iter->data;
      g_autofree gchar *driver_path = NULL;
      const gchar *lib_path = srt_va_api_driver_get_library_path (driver);

      g_debug ("Found VA-API driver: %s", lib_path);
      driver_path = g_path_get_dirname (lib_path);
      if (!g_hash_table_contains (drivers_set, driver_path))
        g_hash_table_add (drivers_set, g_steal_pointer (&driver_path));
    }

  _srt_hash_table_iter_init_sorted (&iter, drivers_set,
                                    self->arbitrary_str_order);

  while (_srt_hash_table_iter_next (&iter, &drivers_path, NULL))
    pv_search_path_append (search_path, drivers_path);
}

/*
 * Returns: (element-type IcdDetails):
 */
static GPtrArray *
pv_enumerate_egl_ext_platforms (SrtSystemInfo *system_info,
                                const char * const *multiarch_tuples,
                                const gchar *which_system)
{
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer = NULL;
  g_autoptr(SrtObjectList) egl_ext_platforms = NULL;
  g_autoptr(GPtrArray) egl_ext_platform_details = NULL;
  gsize i;
  const GList *icd_iter;

  timer = _srt_profiling_start ("Enumerating EGL external platforms on %s system",
                                which_system);
  g_debug ("Enumerating EGL external platforms on %s system...", which_system);
  egl_ext_platforms = srt_system_info_list_egl_external_platforms (system_info,
                                                                   multiarch_tuples);
  egl_ext_platform_details = g_ptr_array_new_full (g_list_length (egl_ext_platforms),
                                                   (GDestroyNotify) G_CALLBACK (icd_details_free));

  for (icd_iter = egl_ext_platforms, i = 0;
       icd_iter != NULL;
       icd_iter = icd_iter->next, i++)
    {
      SrtEglExternalPlatform *ext = icd_iter->data;
      const gchar *path = srt_egl_external_platform_get_json_path (ext);
      GError *local_error = NULL;

      if (!srt_egl_external_platform_check_error (ext, &local_error))
        {
          g_info ("Failed to load EGL external platform #%" G_GSIZE_FORMAT  " from %s: %s",
                  i, path, local_error->message);
          g_clear_error (&local_error);
          continue;
        }

      g_info ("EGL external platform #%" G_GSIZE_FORMAT " at %s: %s",
              i, path, srt_egl_external_platform_get_library_path (ext));

      g_ptr_array_add (egl_ext_platform_details, icd_details_new (ext));
    }

  return g_steal_pointer (&egl_ext_platform_details);
}

/*
 * Returns: (element-type IcdDetails):
 */
static GPtrArray *
pv_enumerate_egl_icds (SrtSystemInfo *system_info,
                       const char * const *multiarch_tuples,
                       const gchar *which_system)
{
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer = NULL;
  g_autoptr(SrtObjectList) egl_icds = NULL;
  g_autoptr(GPtrArray) egl_icd_details = NULL;
  gsize i;
  const GList *icd_iter;

  timer = _srt_profiling_start ("Enumerating EGL ICDs on %s system", which_system);
  g_debug ("Enumerating EGL ICDs on %s system...", which_system);
  egl_icds = srt_system_info_list_egl_icds (system_info, multiarch_tuples);
  egl_icd_details = g_ptr_array_new_full (g_list_length (egl_icds),
                                          (GDestroyNotify) G_CALLBACK (icd_details_free));

  for (icd_iter = egl_icds, i = 0;
       icd_iter != NULL;
       icd_iter = icd_iter->next, i++)
    {
      SrtEglIcd *icd = icd_iter->data;
      const gchar *path = srt_egl_icd_get_json_path (icd);
      GError *local_error = NULL;

      if (!srt_egl_icd_check_error (icd, &local_error))
        {
          g_info ("Failed to load EGL ICD #%" G_GSIZE_FORMAT  " from %s: %s",
                  i, path, local_error->message);
          g_clear_error (&local_error);
          continue;
        }

      g_info ("EGL ICD #%" G_GSIZE_FORMAT " at %s: %s",
              i, path, srt_egl_icd_get_library_path (icd));

      g_ptr_array_add (egl_icd_details, icd_details_new (icd));
    }

  return g_steal_pointer (&egl_icd_details);
}

/*
 * Returns: (element-type IcdDetails):
 */
static GPtrArray *
pv_enumerate_vulkan_icds (SrtSystemInfo *system_info,
                          const char * const *multiarch_tuples,
                          const gchar *which_system)
{
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer = NULL;
  g_autoptr(SrtObjectList) vulkan_icds = NULL;
  g_autoptr(GPtrArray) vulkan_icd_details = NULL;
  gsize i;
  const GList *icd_iter;

  timer = _srt_profiling_start ("Enumerating Vulkan ICDs on %s system", which_system);
  g_debug ("Enumerating Vulkan ICDs on %s system...", which_system);
  vulkan_icds = srt_system_info_list_vulkan_icds (system_info,
                                                  multiarch_tuples);
  vulkan_icd_details = g_ptr_array_new_full (g_list_length (vulkan_icds),
                                             (GDestroyNotify) G_CALLBACK (icd_details_free));

  for (icd_iter = vulkan_icds, i = 0;
       icd_iter != NULL;
       icd_iter = icd_iter->next, i++)
    {
      SrtVulkanIcd *icd = icd_iter->data;
      const gchar *path = srt_vulkan_icd_get_json_path (icd);
      GError *local_error = NULL;

      if (!srt_vulkan_icd_check_error (icd, &local_error))
        {
          g_info ("Failed to load Vulkan ICD #%" G_GSIZE_FORMAT " from %s: %s",
                  i, path, local_error->message);
          g_clear_error (&local_error);
          continue;
        }

      g_info ("Vulkan ICD #%" G_GSIZE_FORMAT " at %s: %s",
              i, path, srt_vulkan_icd_get_library_path (icd));

      g_ptr_array_add (vulkan_icd_details, icd_details_new (icd));
    }

  return g_steal_pointer (&vulkan_icd_details);
}

/*
 * @vulkan_layer_details: (inout) (element-type IcdDetails):
 */
static void
pv_append_vulkan_layers_details (GList *vulkan_layers,
                                 const gchar *which,
                                 GPtrArray *vulkan_layer_details)
{
  gsize i;
  const GList *icd_i;

  for (icd_i = vulkan_layers, i = 0; icd_i != NULL; icd_i = icd_i->next, i++)
    {
      SrtVulkanLayer *layer = icd_i->data;
      const gchar *path = srt_vulkan_layer_get_json_path (layer);
      GError *local_error = NULL;

      if (!srt_vulkan_layer_check_error (layer, &local_error))
        {
          g_info ("Failed to load Vulkan %s layer #%" G_GSIZE_FORMAT
                  " from %s: %s", which, i, path, local_error->message);
          g_clear_error (&local_error);
          continue;
        }

      const gchar *description = srt_vulkan_layer_get_library_path (layer);

      if (description == NULL)
        description = "meta-layer";

      g_info ("Vulkan %s layer #%" G_GSIZE_FORMAT " at %s: %s",
              which, i, path, description);

      g_ptr_array_add (vulkan_layer_details, icd_details_new (layer));
    }
}

/*
 * @vulkan_exp_layer_details: (out) (element-type IcdDetails):
 * @vulkan_imp_layer_details: (out) (element-type IcdDetails):
 */
static void
pv_enumerate_vulkan_layer_details (SrtSystemInfo *system_info,
                                   const gchar *which_system,
                                   GPtrArray **vulkan_exp_layer_details,
                                   GPtrArray **vulkan_imp_layer_details)
{
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer = NULL;
  g_autoptr(SrtObjectList) vulkan_exp_layers = NULL;
  g_autoptr(SrtObjectList) vulkan_imp_layers = NULL;

  g_return_if_fail (vulkan_exp_layer_details != NULL);
  g_return_if_fail (*vulkan_exp_layer_details == NULL);
  g_return_if_fail (vulkan_imp_layer_details != NULL);
  g_return_if_fail (*vulkan_imp_layer_details == NULL);

  timer = _srt_profiling_start ("Enumerating Vulkan layers on %s system", which_system);

  g_debug ("Enumerating Vulkan explicit layers on %s system...", which_system);
  vulkan_exp_layers = srt_system_info_list_explicit_vulkan_layers (system_info);
  *vulkan_exp_layer_details = g_ptr_array_new_full (g_list_length (vulkan_exp_layers),
                                                    (GDestroyNotify) G_CALLBACK (icd_details_free));
  pv_append_vulkan_layers_details (vulkan_exp_layers, "explicit", *vulkan_exp_layer_details);

  g_debug ("Enumerating Vulkan implicit layers on %s system...", which_system);
  vulkan_imp_layers = srt_system_info_list_implicit_vulkan_layers (system_info);
  *vulkan_imp_layer_details = g_ptr_array_new_full (g_list_length (vulkan_imp_layers),
                                                    (GDestroyNotify) G_CALLBACK (icd_details_free));
  pv_append_vulkan_layers_details (vulkan_imp_layers, "implicit", *vulkan_imp_layer_details);
}

typedef struct
{
  GPtrArray *egl_icd_details;   /* (element-type IcdDetails) */
  GPtrArray *egl_ext_platform_details;   /* (element-type IcdDetails) */
  GPtrArray *vulkan_icd_details;   /* (element-type IcdDetails) */
  GPtrArray *vulkan_exp_layer_details;   /* (element-type IcdDetails) */
  GPtrArray *vulkan_imp_layer_details;   /* (element-type IcdDetails) */
} IcdStack;

static IcdStack *
icd_stack_new (void)
{
  return g_slice_new0 (IcdStack);
}

static void
icd_stack_free (IcdStack *self)
{
  g_clear_pointer (&self->egl_icd_details, g_ptr_array_unref);
  g_clear_pointer (&self->egl_ext_platform_details, g_ptr_array_unref);
  g_clear_pointer (&self->vulkan_icd_details, g_ptr_array_unref);
  g_clear_pointer (&self->vulkan_exp_layer_details, g_ptr_array_unref);
  g_clear_pointer (&self->vulkan_imp_layer_details, g_ptr_array_unref);
  g_slice_free (IcdStack, self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (IcdStack, icd_stack_free)

static gboolean
pv_runtime_use_provider_graphics_stack (PvRuntime *self,
                                        FlatpakBwrap *bwrap,
                                        PvEnviron *container_env,
                                        GError **error)
{
  gsize i, j;
  g_autoptr(GString) dri_path = g_string_new ("");
  g_autoptr(GString) egl_path = g_string_new ("");
  g_autoptr(GString) egl_ext_platform_path = g_string_new ("");
  g_autoptr(GString) vulkan_path = g_string_new ("");
  /* We are currently using the explicit and implicit Vulkan layer paths
   * only to check if we binded at least a single layer */
  g_autoptr(GString) vulkan_exp_layer_path = g_string_new ("");
  g_autoptr(GString) vulkan_imp_layer_path = g_string_new ("");
  g_autoptr(GString) va_api_path = g_string_new ("");
  gboolean any_architecture_works = FALSE;
  g_autoptr(SrtSystemInfo) system_info = NULL;
  g_autoptr(SrtSystemInfo) host_system_info = NULL;
  g_autoptr(IcdStack) provider_stack = icd_stack_new ();
  g_autoptr(IcdStack) host_stack = icd_stack_new ();
  G_GNUC_UNUSED g_autoptr(SrtProfilingTimer) timer = NULL;
  g_autoptr(SrtProfilingTimer) part_timer = NULL;
  gboolean all_libglx_from_provider = TRUE;
  gboolean all_libdrm_from_provider = TRUE;
  g_autoptr(GHashTable) drirc_data_in_provider = g_hash_table_new_full (g_str_hash,
                                                                        g_str_equal,
                                                                        g_free, NULL);
  g_autoptr(GHashTable) libdrm_data_in_provider = g_hash_table_new_full (g_str_hash,
                                                                         g_str_equal,
                                                                         g_free, NULL);
  g_autoptr(GHashTable) nvidia_data_in_provider = g_hash_table_new_full (g_str_hash,
                                                                         g_str_equal,
                                                                         g_free, NULL);
  g_autoptr(GHashTable) gconv_in_provider = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                                   g_free, NULL);
  const gchar *provider = "provider";

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (self->provider != NULL, FALSE);
  g_return_val_if_fail (bwrap != NULL || self->mutable_sysroot != NULL, FALSE);
  g_return_val_if_fail (bwrap == NULL || !pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (container_env != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  timer = _srt_profiling_start ("Using graphics stack from %s",
                                self->provider->path_in_current_ns);

  if (!pv_runtime_provide_container_access (self, error))
    return FALSE;

  if (self->flags & PV_RUNTIME_FLAGS_SINGLE_THREAD)
    {
      system_info = pv_graphics_provider_create_system_info (self->provider);
      if (self->interpreter_host_provider != NULL)
        host_system_info = pv_graphics_provider_create_system_info (self->interpreter_host_provider);
    }
  else
    {
      system_info = g_object_ref (enumeration_thread_join (&self->indep_thread));
      if (self->interpreter_host_provider != NULL)
        host_system_info = g_object_ref (enumeration_thread_join (&self->host_thread));
    }

  provider_stack->egl_icd_details = pv_enumerate_egl_icds (system_info,
                                                           pv_multiarch_tuples, provider);
  provider_stack->egl_ext_platform_details = pv_enumerate_egl_ext_platforms (system_info,
                                                                             pv_multiarch_tuples,
                                                                             provider);
  provider_stack->vulkan_icd_details = pv_enumerate_vulkan_icds (system_info,
                                                                 pv_multiarch_tuples,
                                                                 provider);
  if (self->flags & PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS)
    {
      pv_enumerate_vulkan_layer_details (system_info,
                                         provider,
                                         &provider_stack->vulkan_exp_layer_details,
                                         &provider_stack->vulkan_imp_layer_details);
    }

  if (host_system_info != NULL)
    {
      const gchar *which = "host";
      host_stack->egl_icd_details = pv_enumerate_egl_icds (host_system_info, NULL, which);
      host_stack->egl_ext_platform_details = pv_enumerate_egl_ext_platforms (host_system_info,
                                                                             NULL, which);
      host_stack->vulkan_icd_details = pv_enumerate_vulkan_icds (host_system_info, NULL, which);
      if (self->flags & PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS)
        {
          pv_enumerate_vulkan_layer_details (host_system_info,
                                             which,
                                             &host_stack->vulkan_exp_layer_details,
                                             &host_stack->vulkan_imp_layer_details);
        }
    }

  /* We set this FALSE later if we decide not to use the provider libc
   * for some architecture. */
  self->all_libc_from_provider = TRUE;

  g_assert (pv_multiarch_tuples[PV_N_SUPPORTED_ARCHITECTURES] == NULL);

  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      g_autoptr(GError) local_error = NULL;
      g_auto (RuntimeArchitecture) arch_on_stack = { i };
      RuntimeArchitecture *arch = &arch_on_stack;

      part_timer = _srt_profiling_start ("%s libraries", pv_multiarch_tuples[i]);
      g_debug ("Checking for %s libraries...", pv_multiarch_tuples[i]);

      if (runtime_architecture_init (arch, self))
        {
          g_autoptr(GPtrArray) dirs = NULL;
          g_autofree gchar *this_dri_path_in_container = g_build_filename (arch->libdir_in_container,
                                                                           "dri", NULL);
          g_autofree gchar *libc_symlink = NULL;
          /* Can either be relative to the sysroot, or absolute */
          g_autofree gchar *ld_so_in_runtime = NULL;
          g_autofree gchar *libdrm = NULL;
          g_autofree gchar *libdrm_amdgpu = NULL;
          g_autofree gchar *libglx_mesa = NULL;
          g_autofree gchar *libglx_nvidia = NULL;
          g_autofree gchar *platform_token = NULL;
          g_autoptr(GPtrArray) patterns = NULL;
          SrtSystemInfo *arch_system_info;

          if (!pv_runtime_get_ld_so (self, arch, &ld_so_in_runtime, error))
            return FALSE;

          if (ld_so_in_runtime == NULL)
            {
              g_info ("Container does not have %s so it cannot run "
                      "%s binaries",
                      arch->ld_so, arch->details->tuple);
              continue;
            }

          /* Reserve a size of 128 to avoid frequent reallocation due to the
           * expected high number of patterns that will be added to the array. */
          patterns = g_ptr_array_new_full (128, g_free);

          any_architecture_works = TRUE;
          g_debug ("Container path: %s -> %s",
                   arch->ld_so, ld_so_in_runtime);

          pv_search_path_append (dri_path, this_dri_path_in_container);
          pv_search_path_append (va_api_path, this_dri_path_in_container);

          g_mkdir_with_parents (arch->libdir_in_current_namespace, 0755);
          g_mkdir_with_parents (arch->aliases_in_current_namespace, 0755);

          g_debug ("Collecting graphics drivers from provider system...");

          collect_core_libraries_patterns (patterns);
          collect_graphics_libraries_patterns (patterns);

          if (!collect_egl_drivers (self, arch, provider_stack->egl_icd_details,
                                    patterns, error))
            return FALSE;

          if (!collect_egl_ext_platforms (self, arch,
                                          provider_stack->egl_ext_platform_details,
                                          patterns, error))
            return FALSE;

          if (!collect_vulkan_icds (self, arch, provider_stack->vulkan_icd_details,
                                    patterns, error))
            return FALSE;

          if (self->flags & PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS)
            {
              g_debug ("Collecting Vulkan explicit layers from provider...");
              if (!collect_vulkan_layers (self, provider_stack->vulkan_exp_layer_details,
                                          patterns, arch, "vulkan_exp_layer", error))
                return FALSE;

              g_debug ("Collecting Vulkan implicit layers from provider...");
              if (!collect_vulkan_layers (self, provider_stack->vulkan_imp_layer_details,
                                          patterns, arch, "vulkan_imp_layer", error))
                return FALSE;
            }

          if (self->flags & PV_RUNTIME_FLAGS_SINGLE_THREAD)
            arch_system_info = system_info;
          else
            arch_system_info = enumeration_thread_join (&self->arch_threads[i]);

          if (!collect_vdpau_drivers (self, arch_system_info, arch, patterns, error))
            return FALSE;

          if (!collect_dri_drivers (self, arch_system_info, arch, patterns,
                                    dri_path, error))
            return FALSE;

          /* We always have at least one pattern, because
           * collect_graphics_libraries_patterns() unconditionally
           * adds some, so we don't need to conditionalize this call
           * to capsule-capture-libs */
          g_assert (patterns->len > 0);
          g_assert (patterns->pdata != NULL);

          if (!pv_runtime_capture_libraries (self, arch,
                                             arch->libdir_in_current_namespace,
                                             "Main capsule-capture-libs call",
                                             (const char * const *) patterns->pdata,
                                             patterns->len, error))
            return FALSE;

          libc_symlink = g_build_filename (arch->libdir_in_current_namespace, "libc.so.6", NULL);

          /* If we are going to use the provider's libc6 (likely)
           * then we have to use its ld.so too. */
          if (g_file_test (libc_symlink, G_FILE_TEST_IS_SYMLINK))
            {
              if (!pv_runtime_collect_libc_family (self, arch, bwrap,
                                                   libc_symlink, ld_so_in_runtime,
                                                   gconv_in_provider,
                                                   error))
                return FALSE;

              self->any_libc_from_provider = TRUE;
            }
          else
            {
              self->all_libc_from_provider = FALSE;
            }

          libdrm = g_build_filename (arch->libdir_in_current_namespace,
                                     "libdrm.so.2", NULL);
          libdrm_amdgpu = g_build_filename (arch->libdir_in_current_namespace,
                                            "libdrm_amdgpu.so.1", NULL);

          /* If we have libdrm_amdgpu.so.1 in overrides we also want to mount
           * ${prefix}/share/libdrm from the provider. ${prefix} is derived from
           * the absolute path of libdrm_amdgpu.so.1 */
          if (!pv_runtime_collect_lib_symlink_data (self, arch, "libdrm",
                                                    libdrm_amdgpu,
                                                    PV_RUNTIME_DATA_FLAGS_NONE,
                                                    libdrm_data_in_provider)
              && !pv_runtime_collect_lib_symlink_data (self, arch, "libdrm",
                                                       libdrm,
                                                       PV_RUNTIME_DATA_FLAGS_NONE,
                                                       libdrm_data_in_provider))
            {
              /* For at least a single architecture, libdrm is newer in the container */
              all_libdrm_from_provider = FALSE;
            }

          libglx_mesa = g_build_filename (arch->libdir_in_current_namespace, "libGLX_mesa.so.0", NULL);

          /* If we have libGLX_mesa.so.0 in overrides we also want to mount
           * ${prefix}/share/drirc.d from the provider. ${prefix} is derived from
           * the absolute path of libGLX_mesa.so.0 */
          if (!pv_runtime_collect_lib_symlink_data (self, arch, "drirc.d",
                                                    libglx_mesa,
                                                    PV_RUNTIME_DATA_FLAGS_NONE,
                                                    drirc_data_in_provider))
            {
              /* For at least a single architecture, libGLX_mesa is newer in the container */
              all_libglx_from_provider = FALSE;
            }

          collect_mesa_drirc (self, arch, provider_stack->egl_icd_details,
                              provider_stack->vulkan_icd_details, system_info,
                              drirc_data_in_provider);

          libglx_nvidia = g_build_filename (arch->libdir_in_current_namespace, "libGLX_nvidia.so.0", NULL);

          /* If we have libGLX_nvidia.so.0 in overrides we also want to mount
           * /usr/share/nvidia from the provider. In this case it's
           * /usr/share/nvidia that is the preferred path, with
           * ${prefix}/share/nvidia as a fallback. */
          pv_runtime_collect_lib_symlink_data (self, arch, "nvidia",
                                               libglx_nvidia,
                                               PV_RUNTIME_DATA_FLAGS_USR_SHARE_FIRST,
                                               nvidia_data_in_provider);

          dirs = pv_multiarch_details_get_libdirs (arch->details,
                                                   PV_MULTIARCH_LIBDIRS_FLAGS_NONE);

          for (j = 0; j < dirs->len; j++)
            {
              if (!collect_s2tc (self, arch,
                                 g_ptr_array_index (dirs, j),
                                 error))
                return FALSE;
            }

          /* Unfortunately VDPAU_DRIVER_PATH can hold just a single path, so we can't
           * easily list both x86_64 and i386 paths. As a workaround we set
           * VDPAU_DRIVER_PATH based on ${PLATFORM} - but each of our
           * supported ABIs can have multiple values for ${PLATFORM}, so we
           * need to create symlinks. Try to avoid making use of this,
           * because it's fragile (a new glibc version can introduce
           * new platform strings), but for some things like VDPAU it's our
           * only choice. */
          for (j = 0; j < G_N_ELEMENTS (arch->details->platforms); j++)
            {
              g_autofree gchar *platform_link = NULL;

              if (arch->details->platforms[j] == NULL)
                break;

              platform_link = g_strdup_printf ("%s/lib/platform-%s",
                                               self->overrides,
                                               arch->details->platforms[j]);

              if (symlink (arch->details->tuple, platform_link) != 0)
                return glnx_throw_errno_prefix (error,
                                                "Unable to create symlink %s -> %s",
                                                platform_link, arch->details->tuple);
            }

          platform_token = srt_system_info_dup_libdl_platform (arch_system_info,
                                                               pv_multiarch_tuples[i],
                                                               &local_error);
          if (platform_token == NULL)
            {
              /* This is not a critical error, try to continue */
              g_warning ("The dynamic linker expansion of \"$PLATFORM\" is not what we "
                         "expected, VDPAU drivers might not work: %s", local_error->message);
              g_clear_error (&local_error);
            }

          if (!pv_runtime_create_aliases (self, arch, &local_error))
            {
              /* This is not a critical error, try to continue */
              g_warning ("Unable to create library aliases: %s",
                         local_error->message);
              g_clear_error (&local_error);
              continue;
            }

          /* Make sure we do this last, so that we have really copied
           * everything from the provider that we are going to */
          if (self->mutable_sysroot != NULL &&
              !pv_runtime_remove_overridden_libraries (self, arch, error))
            return FALSE;
        }

      g_clear_pointer (&part_timer, _srt_profiling_end);
    }

  if (self->interpreter_host_provider != NULL)
    {
      g_assert (pv_multiarch_as_emulator_tuples[PV_N_SUPPORTED_ARCHITECTURES_AS_EMULATOR_HOST] == NULL);

      for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES_AS_EMULATOR_HOST; i++)
        {
          SrtSystemInfo *arch_system_info;

          if (self->flags & PV_RUNTIME_FLAGS_SINGLE_THREAD)
            arch_system_info = host_system_info;
          else
            arch_system_info = enumeration_thread_join (&self->arch_host_threads[i]);

          pv_append_host_dri_library_paths (self,
                                            arch_system_info,
                                            pv_multiarch_as_emulator_tuples[i],
                                            dri_path);
        }
    }

  part_timer = _srt_profiling_start ("Finishing graphics stack capture");

  if (!any_architecture_works)
    {
      GString *archs = g_string_new ("");

      g_assert (pv_multiarch_tuples[PV_N_SUPPORTED_ARCHITECTURES] == NULL);

      for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
        {
          if (archs->len > 0)
            g_string_append (archs, ", ");

          g_string_append (archs, pv_multiarch_tuples[i]);
        }

      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
                   "None of the supported CPU architectures are common to "
                   "the graphics provider and the container (tried: %s)",
                   archs->str);
      g_string_free (archs, TRUE);
      return FALSE;
    }

  if (!pv_runtime_finish_libc_family (self, bwrap, gconv_in_provider, error))
    return FALSE;

  if (!pv_runtime_finish_lib_data (self, bwrap, "libdrm", "libdrm",
                                   all_libdrm_from_provider,
                                   libdrm_data_in_provider, error))
    return FALSE;

  if (!pv_runtime_finish_lib_data (self, bwrap, "drirc.d", "libGLX_mesa.so.0",
                                   all_libglx_from_provider,
                                   drirc_data_in_provider, error))
    return FALSE;

  if (!pv_runtime_finish_lib_data (self, bwrap, "nvidia", "libGLX_nvidia.so.0",
                                   TRUE, nvidia_data_in_provider, error))
    return FALSE;

  if (!pv_runtime_take_misc_data_from_provider (self, bwrap, error))
    return FALSE;

  g_debug ("Setting up EGL ICD JSON...");

  if (!setup_each_json_manifest (self, bwrap, "glvnd/egl_vendor.d",
                                 provider_stack->egl_icd_details, egl_path, error))
    return FALSE;

  if (host_stack->egl_icd_details != NULL)
    {
      for (i = 0; i < host_stack->egl_icd_details->len; i++)
        {
          IcdDetails *details = g_ptr_array_index (host_stack->egl_icd_details, i);
          SrtEglIcd *icd = SRT_EGL_ICD (details->icd);
          pv_search_path_append (egl_path, srt_egl_icd_get_json_path (icd));
        }
    }

  if (!setup_each_json_manifest (self, bwrap, "egl/egl_external_platform.d",
                                 provider_stack->egl_ext_platform_details,
                                 egl_ext_platform_path, error))
    return FALSE;

  if (host_stack->egl_ext_platform_details != NULL)
    {
      for (i = 0; i < host_stack->egl_ext_platform_details->len; i++)
        {
          IcdDetails *details = g_ptr_array_index (host_stack->egl_ext_platform_details, i);
          SrtEglExternalPlatform *ext_platform = SRT_EGL_EXTERNAL_PLATFORM (details->icd);
          pv_search_path_append (egl_ext_platform_path,
                                 srt_egl_external_platform_get_json_path (ext_platform));
        }
    }

  g_debug ("Setting up Vulkan ICD JSON...");
  if (!setup_each_json_manifest (self, bwrap, "vulkan/icd.d",
                                 provider_stack->vulkan_icd_details, vulkan_path, error))
    return FALSE;

  if (host_stack->vulkan_icd_details != NULL)
    {
      for (i = 0; i < host_stack->vulkan_icd_details->len; i++)
        {
          IcdDetails *details = g_ptr_array_index (host_stack->vulkan_icd_details, i);
          SrtVulkanIcd *icd = SRT_VULKAN_ICD (details->icd);
          pv_search_path_append (vulkan_path, srt_vulkan_icd_get_json_path (icd));
        }
    }

  if (self->flags & PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS)
    {
      g_debug ("Setting up Vulkan explicit layer JSON...");
      if (!setup_each_json_manifest (self, bwrap, "vulkan/explicit_layer.d",
                                     provider_stack->vulkan_exp_layer_details,
                                     vulkan_exp_layer_path, error))
        return FALSE;

      if (host_stack->vulkan_exp_layer_details != NULL)
        {
          for (i = 0; i < host_stack->vulkan_exp_layer_details->len; i++)
            {
              IcdDetails *details = g_ptr_array_index (host_stack->vulkan_exp_layer_details, i);
              SrtVulkanLayer *layer = SRT_VULKAN_LAYER (details->icd);
              pv_search_path_append (vulkan_path, srt_vulkan_layer_get_json_path (layer));
            }
        }

      g_debug ("Setting up Vulkan implicit layer JSON...");
      if (!setup_each_json_manifest (self, bwrap, "vulkan/implicit_layer.d",
                                     provider_stack->vulkan_imp_layer_details,
                                     vulkan_imp_layer_path, error))
        return FALSE;

      if (host_stack->vulkan_imp_layer_details != NULL)
        {
          for (i = 0; i < host_stack->vulkan_imp_layer_details->len; i++)
            {
              IcdDetails *details = g_ptr_array_index (host_stack->vulkan_imp_layer_details, i);
              SrtVulkanLayer *layer = SRT_VULKAN_LAYER (details->icd);
              pv_search_path_append (vulkan_path, srt_vulkan_layer_get_json_path (layer));
            }
        }
    }

  if (dri_path->len != 0)
    {
      pv_environ_setenv (container_env, "LIBGL_DRIVERS_PATH", dri_path->str);
      pv_environ_setenv (container_env, "LIBVA_DRIVERS_PATH", dri_path->str);
    }
  else
    {
      pv_environ_setenv (container_env, "LIBGL_DRIVERS_PATH", NULL);
      pv_environ_setenv (container_env, "LIBVA_DRIVERS_PATH", NULL);
    }

  if (egl_path->len != 0)
    pv_environ_setenv (container_env, "__EGL_VENDOR_LIBRARY_FILENAMES",
                       egl_path->str);
  else
    pv_environ_setenv (container_env, "__EGL_VENDOR_LIBRARY_FILENAMES",
                       NULL);

  pv_environ_setenv (container_env, "__EGL_VENDOR_LIBRARY_DIRS", NULL);

  if (egl_ext_platform_path->len != 0)
    pv_environ_setenv (container_env, "__EGL_EXTERNAL_PLATFORM_CONFIG_FILENAMES",
                       egl_ext_platform_path->str);
  else
    pv_environ_setenv (container_env, "__EGL_EXTERNAL_PLATFORM_CONFIG_FILENAMES",
                       NULL);

  pv_environ_setenv (container_env, "__EGL_EXTERNAL_PLATFORM_CONFIG_DIRS", NULL);

  if (vulkan_path->len != 0)
    {
      /* VK_ICD_FILENAMES is deprecated, VK_DRIVER_FILES takes precedence.
       * Until all branches of the Steam Runtime have a Vulkan-Loader
       * that supports VK_DRIVER_FILES, we need to set both:
       * old Vulkan-Loader versions will use the old variable, while new
       * versions will use the new one. */
      pv_environ_setenv (container_env, "VK_DRIVER_FILES", vulkan_path->str);
      pv_environ_setenv (container_env, "VK_ICD_FILENAMES", vulkan_path->str);
    }
  else
    {
      pv_environ_setenv (container_env, "VK_ICD_FILENAMES", NULL);
      pv_environ_setenv (container_env, "VK_DRIVER_FILES", NULL);
    }

  /* Setting VK_DRIVER_FILES now disables this, but that wasn't the case
   * in Vulkan-Loader 1.3.207, and it seems clearer if we unset it anyway. */
  pv_environ_setenv (container_env, "VK_ADD_DRIVER_FILES", NULL);

  if (self->flags & PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS)
    {
      /* Implicit layers are not affected by "VK_LAYER_PATH". So instead of using
       * this environment variable, we prepend our "/overrides/share" to
       * "XDG_DATA_DIRS" to cover any explicit and implicit layers that we may
       * have. */
      if (vulkan_exp_layer_path->len != 0 || vulkan_imp_layer_path->len != 0)
        {
          const gchar *xdg_data_dirs;
          g_autofree gchar *prepended_data_dirs = NULL;
          g_autofree gchar *override_share = NULL;

          xdg_data_dirs = g_environ_getenv (self->original_environ, "XDG_DATA_DIRS");
          override_share = g_build_filename (self->overrides_in_container, "share", NULL);

          /* Reference:
           * https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html */
          if (xdg_data_dirs == NULL)
            xdg_data_dirs = "/usr/local/share:/usr/share";

          prepended_data_dirs = g_strdup_printf ("%s:%s", override_share, xdg_data_dirs);

          pv_environ_setenv (container_env, "XDG_DATA_DIRS",
                             prepended_data_dirs);
        }
      pv_environ_setenv (container_env, "VK_LAYER_PATH", NULL);
    }

  /* We binded the VDPAU drivers in "%{libdir}/vdpau".
   * Unfortunately VDPAU_DRIVER_PATH can hold just a single path, so we can't
   * easily list both x86_64 and i386 drivers path.
   * As a workaround we set VDPAU_DRIVER_PATH to
   * "/overrides/lib/platform-${PLATFORM}/vdpau" (which is a symlink that we
   * already created). */
  g_autofree gchar *vdpau_val = g_strdup_printf ("%s/lib/platform-${PLATFORM}/vdpau",
                                                 self->overrides_in_container);

  pv_environ_setenv (container_env, "VDPAU_DRIVER_PATH", vdpau_val);
  return TRUE;
}

gboolean
pv_runtime_bind (PvRuntime *self,
                 FlatpakExports *exports,
                 FlatpakBwrap *bwrap,
                 PvEnviron *container_env,
                 GError **error)
{
  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail ((exports == NULL) == (bwrap == NULL), FALSE);
  g_return_val_if_fail (bwrap == NULL || !pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (bwrap != NULL || self->mutable_sysroot != NULL, FALSE);
  g_return_val_if_fail (container_env != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (self->flags & PV_RUNTIME_FLAGS_FLATPAK_SUBSANDBOX)
    {
      g_return_val_if_fail (exports == NULL, FALSE);
      g_return_val_if_fail (bwrap == NULL, FALSE);
    }
  else
    {
      g_return_val_if_fail (exports != NULL, FALSE);
      g_return_val_if_fail (bwrap != NULL, FALSE);
    }

  if (bwrap != NULL
      && !bind_runtime_base (self, exports, bwrap, container_env, error))
    return FALSE;

  if (bwrap != NULL || self->is_flatpak_env)
    {
      if (!bind_runtime_ld_so (self, bwrap, container_env, error))
        return FALSE;
    }

  if (self->provider != NULL)
    {
      if (!pv_runtime_use_provider_graphics_stack (self, bwrap,
                                                   container_env,
                                                   error))
        return FALSE;
    }

  if (bwrap != NULL)
    bind_runtime_finish (self, exports, bwrap);

  /* Make sure pressure-vessel itself is visible there. */
  if (self->mutable_sysroot != NULL)
    {
      g_autofree gchar *dest = NULL;
      glnx_autofd int parent_dirfd = -1;

      parent_dirfd = _srt_resolve_in_sysroot (self->mutable_sysroot_fd,
                                              "/usr/lib/pressure-vessel",
                                              SRT_RESOLVE_FLAGS_MKDIR_P,
                                              NULL, error);

      if (parent_dirfd < 0)
        return FALSE;

      if (!glnx_shutil_rm_rf_at (parent_dirfd, "from-host", NULL, error))
        return FALSE;

      dest = glnx_fdrel_abspath (parent_dirfd, "from-host");

      if (!pv_cheap_tree_copy (self->pv_prefix, dest,
                               PV_COPY_FLAGS_NONE, error))
        return FALSE;

      if (bwrap != NULL)
        flatpak_bwrap_add_args (bwrap,
                                "--symlink",
                                "/usr/lib/pressure-vessel/from-host",
                                "/run/pressure-vessel/pv-from-host",
                                NULL);

      /* FEX-Emu's transparent rewriting of paths gets quite confused
       * across a pivot_root(), making the execve() at the end of the
       * bwrap mainprocess fail. Help it out by using a filename that
       * genuinely exists in the container's physical root filesystem. */
      if (self->flags & PV_RUNTIME_FLAGS_INTERPRETER_ROOT)
        self->adverb_in_container = PV_RUNTIME_PATH_INTERPRETER_ROOT "/usr/lib/pressure-vessel/from-host/bin/pressure-vessel-adverb";
      else
        self->adverb_in_container = "/usr/lib/pressure-vessel/from-host/bin/pressure-vessel-adverb";
    }
  else
    {
      g_autofree gchar *pressure_vessel_prefix_in_host_namespace =
        pv_current_namespace_path_to_host_path (self->pv_prefix);

      g_assert (bwrap != NULL);

      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind",
                              pressure_vessel_prefix_in_host_namespace,
                              "/run/pressure-vessel/pv-from-host",
                              NULL);
      self->adverb_in_container = "/run/pressure-vessel/pv-from-host/bin/pressure-vessel-adverb";
    }

  if ((self->flags & PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS)
      && exports != NULL)
    {
      /* We have added our imported Vulkan layers to the search path,
       * but we can't just remove ~/.local/share, etc. from the search
       * path without breaking unrelated users of the XDG basedirs spec,
       * such as .desktop files and icons. Mask any remaining Vulkan
       * layers by mounting empty directories over the top. */
      static const char * const layer_suffixes[] =
        {
          _SRT_GRAPHICS_EXPLICIT_VULKAN_LAYER_SUFFIX,
          _SRT_GRAPHICS_IMPLICIT_VULKAN_LAYER_SUFFIX,
        };
      gsize i;

      for (i = 0; i < G_N_ELEMENTS (layer_suffixes); i++)
        {
          g_auto(GStrv) search_path = NULL;
          const char *suffix = layer_suffixes[i];
          gsize j;

          search_path = _srt_graphics_get_vulkan_search_paths ("/",
                                                               self->original_environ,
                                                               pv_multiarch_tuples,
                                                               suffix);

          for (j = 0; search_path != NULL && search_path[j] != NULL; j++)
            {
              const char *dir = search_path[j];

              /* We are mounting our own runtime over /usr anyway, so
               * ignore those */
              if (flatpak_has_path_prefix (dir, "/usr"))
                continue;

              /* Otherwise, if the directory exists, mask it */
              if (g_file_test (dir, G_FILE_TEST_IS_DIR))
                {
                  g_info ("Hiding \"%s\" from the container so that \"%s/share/%s\" will be used instead",
                          dir, self->overrides_in_container, suffix);
                  flatpak_exports_add_path_tmpfs (exports, dir);
                }
            }
        }
    }

  if (self->is_scout)
    {
      const gchar *sdl_videodriver;

      /* Some games detect that they have been run outside the Steam Runtime
       * and try to re-run themselves via Steam. Trick them into thinking
       * they are in the LD_LIBRARY_PATH Steam Runtime.
       *
       * We do not do this for games developed against soldier, because
       * backwards compatibility is not a concern for game developers who
       * have specifically opted-in to using the newer runtime. */
      pv_environ_setenv (container_env, "STEAM_RUNTIME", "/");

      /* Scout is configured without Wayland support. For this reason, if
       * the Wayland driver was forced via SDL_VIDEODRIVER, we expect that
       * every game will fail to launch. When we detect this situation we
       * unset SDL_VIDEODRIVER, so that the default x11 gets chosen instead */
      sdl_videodriver = g_environ_getenv (self->original_environ, "SDL_VIDEODRIVER");
      if (g_strcmp0 (sdl_videodriver, "wayland") == 0)
        pv_environ_setenv (container_env, "SDL_VIDEODRIVER", NULL);
    }

  pv_runtime_set_search_paths (self, container_env);

  return TRUE;
}

void
pv_runtime_set_search_paths (PvRuntime *self,
                             PvEnviron *container_env)
{
  g_autoptr(GString) ld_library_path = g_string_new ("");
  g_autofree char *terminfo_path = NULL;
  gsize i;

  /* We need to set LD_LIBRARY_PATH here so that we can run
   * pressure-vessel-adverb, even if it is going to regenerate
   * the ld.so.cache for better robustness before launching the
   * actual game */
  g_assert (pv_multiarch_tuples[PV_N_SUPPORTED_ARCHITECTURES] == NULL);

  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      g_autofree gchar *ld_path = NULL;
      g_autofree gchar *aliases = NULL;

      ld_path = g_build_filename (self->overrides_in_container, "lib",
                                  pv_multiarch_tuples[i], NULL);

      aliases = g_build_filename (self->overrides_in_container, "lib",
                                  pv_multiarch_tuples[i], "aliases", NULL);

      pv_search_path_append (ld_library_path, ld_path);
      pv_search_path_append (ld_library_path, aliases);
    }

  /* If the runtime is Debian-based, make sure we search where ncurses-base
   * puts terminfo, even if we're using a non-Debian-based libtinfo.so.6. */
  terminfo_path = g_build_filename (self->source_files, "lib", "terminfo",
                                    NULL);

  if (g_file_test (terminfo_path, G_FILE_TEST_IS_DIR))
    pv_environ_setenv (container_env, "TERMINFO_DIRS", "/lib/terminfo");

  /* The PATH from outside the container doesn't really make sense inside the
   * container: in principle the layout could be totally different. */
  pv_environ_setenv (container_env, "PATH", "/usr/bin:/bin");
  pv_environ_setenv (container_env, "LD_LIBRARY_PATH", ld_library_path->str);
}

gboolean
pv_runtime_use_shared_sockets (PvRuntime *self,
                               FlatpakBwrap *bwrap,
                               PvEnviron *container_env,
                               GError **error)
{
  if (pv_environ_getenv (container_env, "PULSE_SERVER") != NULL
      || self->is_flatpak_env)
    {
      /* Make the PulseAudio driver the default.
       * We do this unconditionally when we are under Flatpak for parity
       * with the freedesktop.org Platform. */
      const gchar *alsa_config = "pcm.!default pulse\n"
                                 "ctl.!default pulse\n";

      if (bwrap != NULL)
        {
          flatpak_bwrap_add_args_data (bwrap, "asound.conf",
                                       alsa_config, -1,
                                       "/etc/asound.conf",
                                       NULL);
        }
      else if (self->mutable_sysroot_fd >= 0)
        {
          /* In a Flatpak sub-sandbox, we can rely on the fact that
           * Flatpak will mount each item in our copy of the runtime's
           * usr/etc/ into /etc, including some that we would normally
           * skip. */
          if (!glnx_file_replace_contents_at (self->mutable_sysroot_fd,
                                              "usr/etc/asound.conf",
                                              (const guint8 *) alsa_config,
                                              strlen (alsa_config),
                                              GLNX_FILE_REPLACE_NODATASYNC,
                                              NULL, error))
            return FALSE;
        }
      else
        {
          g_warning ("Unable to configure libasound.so.2 to use PulseAudio");
        }
    }

  return TRUE;
}

static void
pv_runtime_initable_iface_init (GInitableIface *iface,
                                gpointer unused G_GNUC_UNUSED)
{
  iface->init = pv_runtime_initable_init;
}

const char *
pv_runtime_get_modified_usr (PvRuntime *self)
{
  g_return_val_if_fail (PV_IS_RUNTIME (self), NULL);
  g_return_val_if_fail (self->mutable_sysroot != NULL, NULL);
  return self->runtime_usr;
}

const char *
pv_runtime_get_modified_app (PvRuntime *self)
{
  g_return_val_if_fail (PV_IS_RUNTIME (self), NULL);
  g_return_val_if_fail (self->mutable_sysroot != NULL, NULL);

  if (g_file_test (self->runtime_app, G_FILE_TEST_IS_DIR))
    return self->runtime_app;
  else
    return NULL;
}

const char *
pv_runtime_get_overrides (PvRuntime *self)
{
  g_return_val_if_fail (PV_IS_RUNTIME (self), NULL);

  return self->overrides;
}

/*
 * Return %TRUE if the runtime provides @library, either directly or
 * via the graphics-stack provider.
 */
gboolean
pv_runtime_has_library (PvRuntime *self,
                        const char *library)
{
  glnx_autofd int source_files_fd = -1;
  gsize i;

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (library != NULL, FALSE);

  g_debug ("Checking whether runtime has library: %s", library);

  for (i = 0; i < PV_N_SUPPORTED_ARCHITECTURES; i++)
    {
      const PvMultiarchDetails *details = &pv_multiarch_details[i];
      g_autoptr(GPtrArray) dirs = NULL;
      gsize j;

      dirs = pv_multiarch_details_get_libdirs (details,
                                               PV_MULTIARCH_LIBDIRS_FLAGS_NONE);

      for (j = 0; j < dirs->len; j++)
        {
          const char *libdir = g_ptr_array_index (dirs, j);
          g_autofree gchar *path = g_build_filename (libdir, library, NULL);

          if (self->mutable_sysroot_fd >= 0)
            {
              glnx_autofd int fd = -1;

              fd = _srt_resolve_in_sysroot (self->mutable_sysroot_fd, path,
                                            SRT_RESOLVE_FLAGS_NONE,
                                            NULL, NULL);

              if (fd >= 0)
                {
                  g_debug ("-> yes, ${mutable_sysroot}/%s", path);
                  return TRUE;
                }
            }
          else
            {
              glnx_autofd int fd = -1;

              /* The runtime isn't necessarily a sysroot (it might just be a
               * merged /usr) but in practice it'll be close enough: we look
               * up each library in /usr/foo and /foo anyway. */
              if (source_files_fd < 0)
                {
                  if (!glnx_opendirat (AT_FDCWD, self->source_files, TRUE,
                                       &source_files_fd, NULL))
                    continue;
                }

              fd = _srt_resolve_in_sysroot (source_files_fd, path,
                                            SRT_RESOLVE_FLAGS_NONE,
                                            NULL, NULL);

              if (fd >= 0)
                {
                  g_debug ("-> yes, ${source_files}/%s", path);
                  return TRUE;
                }
            }

          /* If the graphics stack provider is not the same as the current
           * namespace (in practice this rarely/never happens), we also
           * want to steer clear of libraries that only exist in the
           * graphics stack provider.
           *
           * If the graphics stack provider *is* the current namespace,
           * and the library doesn't exist in the container runtime, then
           * it's OK to use libraries from it in LD_PRELOAD, because there
           * is no other version that might have been meant. */
          if (self->provider != NULL
              && g_strcmp0 (self->provider->path_in_current_ns, "/") != 0)
            {
              glnx_autofd int fd = -1;

              fd = _srt_resolve_in_sysroot (self->provider->fd, path,
                                            SRT_RESOLVE_FLAGS_NONE,
                                            NULL, NULL);

              if (fd >= 0)
                {
                  g_debug ("-> yes, ${provider}/%s", path);
                  return TRUE;
                }
            }
        }
    }

  g_debug ("-> no");
  return FALSE;
}
