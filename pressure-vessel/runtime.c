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

#include "runtime.h"

#include <sysexits.h>

#include <gio/gio.h>

/* Include these before steam-runtime-tools.h so that their backport of
 * G_DEFINE_AUTOPTR_CLEANUP_FUNC will be visible to it */
#include "steam-runtime-tools/glib-backports-internal.h"
#include "libglnx/libglnx.h"

#include <steam-runtime-tools/steam-runtime-tools.h>

#include "steam-runtime-tools/graphics-internal.h"
#include "steam-runtime-tools/resolve-in-sysroot-internal.h"
#include "steam-runtime-tools/utils-internal.h"

#include "bwrap.h"
#include "bwrap-lock.h"
#include "elf-utils.h"
#include "enumtypes.h"
#include "exports.h"
#include "flatpak-run-private.h"
#include "tree-copy.h"
#include "utils.h"

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
  gchar *tools_dir;
  PvBwrapLock *runtime_lock;
  GStrv original_environ;

  gchar *libcapsule_knowledge;
  gchar *variable_dir;
  gchar *mutable_sysroot;
  gchar *tmpdir;
  gchar *overrides;
  const gchar *overrides_in_container;
  gchar *container_access;
  FlatpakBwrap *container_access_adverb;
  const gchar *runtime_files;   /* either source_files or mutable_sysroot */
  gchar *runtime_usr;           /* either runtime_files or that + "/usr" */
  gchar *runtime_files_on_host;
  const gchar *adverb_in_container;
  gchar *provider_in_current_namespace;
  gchar *provider_in_host_namespace;
  gchar *provider_in_container_namespace;
  const gchar *host_in_current_namespace;

  PvRuntimeFlags flags;
  int variable_dir_fd;
  int mutable_sysroot_fd;
  int provider_fd;
  gboolean any_libc_from_provider;
  gboolean all_libc_from_provider;
  gboolean runtime_is_just_usr;
  gboolean is_steamrt;
  gboolean is_scout;
};

struct _PvRuntimeClass
{
  GObjectClass parent;
};

enum {
  PROP_0,
  PROP_BUBBLEWRAP,
  PROP_SOURCE,
  PROP_ORIGINAL_ENVIRON,
  PROP_FLAGS,
  PROP_ID,
  PROP_VARIABLE_DIR,
  PROP_PROVIDER_IN_CURRENT_NAMESPACE,
  PROP_PROVIDER_IN_CONTAINER_NAMESPACE,
  PROP_TOOLS_DIRECTORY,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void pv_runtime_initable_iface_init (GInitableIface *iface,
                                            gpointer unused);

G_DEFINE_TYPE_WITH_CODE (PvRuntime, pv_runtime, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_INITABLE,
                                                pv_runtime_initable_iface_init))

/*
 * Return whether @path is likely to be visible in the provider mount point
 * (e.g. /run/host).
 * This needs to be kept approximately in sync with pv_bwrap_bind_usr()
 * and Flatpak's --filesystem=host-os special keyword.
 *
 * This doesn't currently handle /etc: we make the pessimistic assumption
 * that /etc/ld.so.cache, etc., are not shared.
 */
static gboolean
path_visible_in_provider_namespace (PvRuntimeFlags flags,
                                    const char *path)
{
  while (path[0] == '/')
    path++;

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

  return FALSE;
}

/*
 * Supported Debian-style multiarch tuples
 */
static const char * const multiarch_tuples[] =
{
  "x86_64-linux-gnu",
  "i386-linux-gnu",
  NULL
};

typedef struct
{
  const char *tuple;

  /* Directories other than /usr/lib that we must search for loadable
   * modules, least-ambiguous first, most-ambiguous last, not including
   * Debian-style multiarch directories which are automatically derived
   * from @tuple.
   * - Exherbo <GNU-tuple>/lib
   * - Red-Hat- or Arch-style lib<QUAL>
   * - etc.
   * Size is completely arbitrary, expand as needed */
  const char *multilib[3];

  /* Alternative paths for ld.so.cache, other than ld.so.cache itself.
   * Size is completely arbitrary, expand as needed */
  const char *other_ld_so_cache[2];

  /* Known values that ${PLATFORM} can expand to.
   * Refer to sysdeps/x86/cpu-features.c and sysdeps/x86/dl-procinfo.c
   * in glibc.
   * Size is completely arbitrary, expand as needed */
  const char *platforms[5];
} MultiarchDetails;

/*
 * More details, in the same order as multiarch_tuples
 */
static const MultiarchDetails multiarch_details[] =
{
  {
    .tuple = "x86_64-linux-gnu",
    .multilib = { "x86_64-pc-linux-gnu/lib", "lib64", NULL },
    .other_ld_so_cache = { "ld-x86_64-pc-linux-gnu.cache", NULL },
    .platforms = { "xeon_phi", "haswell", "x86_64", NULL },
  },
  {
    .tuple = "i386-linux-gnu",
    .multilib = { "i686-pc-linux-gnu/lib", "lib32", NULL },
    .other_ld_so_cache = { "ld-i686-pc-linux-gnu.cache", NULL },
    .platforms = { "i686", "i586", "i486", "i386", NULL },
  },
};
G_STATIC_ASSERT (G_N_ELEMENTS (multiarch_details)
                 == G_N_ELEMENTS (multiarch_tuples) - 1);

/* Architecture-independent ld.so.cache filenames, other than the
 * conventional filename /etc/ld.so.cache used upstream and in Debian
 * (we assume this is also what's used in our runtimes). */
static const char * const other_ld_so_cache[] =
{
  /* Clear Linux */
  "/var/cache/ldconfig/ld.so.cache",
};

/*
 * @MULTIARCH_LIBDIRS_FLAGS_REMOVE_OVERRIDDEN:
 *  Return all library directories from which we might need to delete
 *  overridden libraries shipped in the runtime.
 */
typedef enum
{
  MULTIARCH_LIBDIRS_FLAGS_REMOVE_OVERRIDDEN = (1 << 0),
  MULTIARCH_LIBDIRS_FLAGS_NONE = 0
} MultiarchLibdirsFlags;

/*
 * Get the library directories associated with @self, most important or
 * unambiguous first.
 *
 * Returns: (transfer container) (element-type filename):
 */
static GPtrArray *
multiarch_details_get_libdirs (const MultiarchDetails *self,
                               MultiarchLibdirsFlags flags)
{
  g_autoptr(GPtrArray) dirs = g_ptr_array_new_with_free_func (g_free);
  gsize j;

  /* Multiarch is the least ambiguous so we put it first.
   *
   * We historically searched /usr/lib before /lib, but Debian actually
   * does the opposite, and we follow that here.
   *
   * Arguably we should search /usr/local/lib before /lib before /usr/lib,
   * but we don't currently try /usr/local/lib. We could add a flag
   * for that if we don't want to do it unconditionally. */
  g_ptr_array_add (dirs,
                   g_build_filename ("/lib", self->tuple, NULL));
  g_ptr_array_add (dirs,
                   g_build_filename ("/usr", "lib", self->tuple, NULL));

  if (flags & MULTIARCH_LIBDIRS_FLAGS_REMOVE_OVERRIDDEN)
    g_ptr_array_add (dirs,
                     g_build_filename ("/usr", "lib", "mesa",
                                       self->tuple, NULL));

  /* Try other multilib variants next. This includes
   * Exherbo/cross-compilation-style per-architecture prefixes,
   * Red-Hat-style lib64 and Arch-style lib32. */
  for (j = 0; j < G_N_ELEMENTS (self->multilib); j++)
    {
      if (self->multilib[j] == NULL)
        break;

      g_ptr_array_add (dirs,
                       g_build_filename ("/", self->multilib[j], NULL));
      g_ptr_array_add (dirs,
                       g_build_filename ("/usr", self->multilib[j], NULL));
    }

  /* /lib and /usr/lib are lowest priority because they're the most
   * ambiguous: we don't know whether they're meant to contain 32- or
   * 64-bit libraries. */
  g_ptr_array_add (dirs, g_strdup ("/lib"));
  g_ptr_array_add (dirs, g_strdup ("/usr/lib"));

  return g_steal_pointer (&dirs);
}

typedef struct
{
  gsize multiarch_index;
  const MultiarchDetails *details;
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

  g_return_val_if_fail (self->multiarch_index < G_N_ELEMENTS (multiarch_details),
                        FALSE);
  g_return_val_if_fail (self->multiarch_index < G_N_ELEMENTS (multiarch_tuples) - 1,
                        FALSE);
  g_return_val_if_fail (self->details == NULL, FALSE);

  self->details = &multiarch_details[self->multiarch_index];
  g_return_val_if_fail (self->details != NULL, FALSE);
  g_return_val_if_fail (self->details->tuple != NULL, FALSE);
  g_return_val_if_fail (g_strcmp0 (multiarch_tuples[self->multiarch_index],
                                   self->details->tuple) == 0, FALSE);

  self->capsule_capture_libs_basename = g_strdup_printf ("%s-capsule-capture-libs",
                                                         self->details->tuple);
  self->capsule_capture_libs = g_build_filename (runtime->tools_dir,
                                                 self->capsule_capture_libs_basename,
                                                 NULL);
  self->libdir_in_current_namespace = g_build_filename (runtime->overrides, "lib",
                                                        self->details->tuple, NULL);
  self->libdir_in_container = g_build_filename (runtime->overrides_in_container,
                                                "lib", self->details->tuple, NULL);

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
  g_return_val_if_fail (self->multiarch_index < G_N_ELEMENTS (multiarch_details), FALSE);
  g_return_val_if_fail (self->details == &multiarch_details[self->multiarch_index], FALSE);
  g_return_val_if_fail (self->capsule_capture_libs_basename != NULL, FALSE);
  g_return_val_if_fail (self->capsule_capture_libs != NULL, FALSE);
  g_return_val_if_fail (self->libdir_in_current_namespace != NULL, FALSE);
  g_return_val_if_fail (self->libdir_in_container != NULL, FALSE);
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

      case PROP_PROVIDER_IN_CURRENT_NAMESPACE:
        g_value_set_string (value, self->provider_in_current_namespace);
        break;

      case PROP_PROVIDER_IN_CONTAINER_NAMESPACE:
        g_value_set_string (value, self->provider_in_container_namespace);
        break;

      case PROP_SOURCE:
        g_value_set_string (value, self->source);
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
  const char *path;

  switch (prop_id)
    {
      case PROP_BUBBLEWRAP:
        /* Construct-only */
        g_return_if_fail (self->bubblewrap == NULL);
        self->bubblewrap = g_value_dup_string (value);
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

      case PROP_PROVIDER_IN_CURRENT_NAMESPACE:
        /* Construct-only */
        g_return_if_fail (self->provider_in_current_namespace == NULL);
        self->provider_in_current_namespace = g_value_dup_string (value);
        break;

      case PROP_PROVIDER_IN_CONTAINER_NAMESPACE:
        /* Construct-only */
        g_return_if_fail (self->provider_in_container_namespace == NULL);
        self->provider_in_container_namespace = g_value_dup_string (value);
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
  g_return_if_fail (self->original_environ != NULL);
  g_return_if_fail (self->provider_in_current_namespace != NULL);
  g_return_if_fail (self->provider_in_container_namespace != NULL);
  g_return_if_fail (self->source != NULL);
  g_return_if_fail (self->tools_dir != NULL);
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
                                   GError **error)
{
  g_autoptr(GError) local_error = NULL;
  g_autoptr(PvBwrapLock) variable_lock = NULL;
  g_autoptr(PvBwrapLock) base_lock = NULL;
  g_auto(GLnxDirFdIterator) variable_dir_iter = { FALSE };
  g_auto(GLnxDirFdIterator) runtime_base_iter = { FALSE };
  glnx_autofd int variable_dir_fd = -1;
  glnx_autofd int runtime_base_fd = -1;
  struct
  {
    const char *path;
    GLnxDirFdIterator *iter;
  } iters[] = {
    { variable_dir, &variable_dir_iter },
    { runtime_base, &runtime_base_iter },
  };
  gsize i;

  g_return_val_if_fail (variable_dir != NULL, FALSE);
  g_return_val_if_fail (runtime_base != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

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

      if (!glnx_dirfd_iterator_init_at (AT_FDCWD, iters[i].path,
                                        TRUE, iters[i].iter, error))
        return FALSE;

      g_debug ("Cleaning up old subdirectories in %s...",
               iters[i].path);

      while (TRUE)
        {
          struct dirent *dent;

          if (!glnx_dirfd_iterator_next_dent_ensure_dtype (iters[i].iter,
                                                           &dent, NULL, error))
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
                                                   iters[i].iter->fd,
                                                   dent->d_name);
        }

      g_debug ("Cleaning up old symlinks in %s...",
               iters[i].path);

      for (j = 0; j < G_N_ELEMENTS (symlinks); j++)
        pv_delete_dangling_symlink (iters[i].iter->fd, iters[i].path,
                                    symlinks[j]);
    }

  return TRUE;
}

static gboolean
pv_runtime_garbage_collect (PvRuntime *self,
                            PvBwrapLock *variable_dir_lock,
                            GError **error)
{
  g_auto(GLnxDirFdIterator) iter = { FALSE };

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (self->variable_dir != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  /* We don't actually *use* this: it just acts as an assertion that
   * we are holding the lock on the parent directory. */
  g_return_val_if_fail (variable_dir_lock != NULL, FALSE);

  if (!glnx_dirfd_iterator_init_at (AT_FDCWD, self->variable_dir,
                                    TRUE, &iter, error))
    return FALSE;

  while (TRUE)
    {
      struct dirent *dent;

      if (!glnx_dirfd_iterator_next_dent_ensure_dtype (&iter, &dent,
                                                       NULL, error))
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
          /* Don't GC old deployments unless we know which one is current
           * and therefore should not be deleted */
          if (self->id == NULL)
            {
              g_debug ("Ignoring %s/deploy-*: current ID not known",
                       self->variable_dir);
              continue;
            }

          /* Don't GC the current deployment */
          if (strcmp (dent->d_name + strlen ("deploy-"), self->id) == 0)
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
                        GError **error)
{
  g_autofree gchar *dest_usr = NULL;
  g_autofree gchar *source_usr_subdir = NULL;
  g_autofree gchar *temp_dir = NULL;
  g_autoptr(GDir) dir = NULL;
  g_autoptr(PvBwrapLock) copy_lock = NULL;
  g_autoptr(PvBwrapLock) source_lock = NULL;
  const char *member;
  const char *source_usr;
  glnx_autofd int temp_dir_fd = -1;
  gboolean is_just_usr;

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (self->variable_dir != NULL, FALSE);
  g_return_val_if_fail (self->flags & PV_RUNTIME_FLAGS_COPY_RUNTIME, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  /* We don't actually *use* this: it just acts as an assertion that
   * we are holding the lock on the parent directory. */
  g_return_val_if_fail (variable_dir_lock != NULL, FALSE);

  temp_dir = g_build_filename (self->variable_dir, "tmp-XXXXXX", NULL);

  if (g_mkdtemp (temp_dir) == NULL)
    return glnx_throw_errno_prefix (error,
                                    "Cannot create temporary directory \"%s\"",
                                    temp_dir);

  source_usr_subdir = g_build_filename (self->source_files, "usr", NULL);
  dest_usr = g_build_filename (temp_dir, "usr", NULL);

  is_just_usr = !g_file_test (source_usr_subdir, G_FILE_TEST_IS_DIR);

  if (is_just_usr)
    {
      /* ${source_files}/usr does not exist, so assume it's a merged /usr,
       * for example ./scout/files. Copy ${source_files}/bin to
       * ${temp_dir}/usr/bin, etc. */
      source_usr = self->source_files;

      if (!pv_cheap_tree_copy (self->source_files, dest_usr,
                               PV_COPY_FLAGS_NONE, error))
        return FALSE;
    }
  else
    {
      /* ${source_files}/usr exists, so assume it's a complete sysroot.
       * Merge ${source_files}/bin and ${source_files}/usr/bin into
       * ${temp_dir}/usr/bin, etc. */
      source_usr = source_usr_subdir;

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

  dir = g_dir_open (source_usr, 0, error);

  if (dir == NULL)
    return FALSE;

  for (member = g_dir_read_name (dir);
       member != NULL;
       member = g_dir_read_name (dir))
    {
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

static gboolean
pv_runtime_initable_init (GInitable *initable,
                          GCancellable *cancellable G_GNUC_UNUSED,
                          GError **error)
{
  PvRuntime *self = PV_RUNTIME (initable);
  g_autoptr(PvBwrapLock) mutable_lock = NULL;
  g_autofree gchar *contents = NULL;
  g_autofree gchar *os_release = NULL;
  gsize len;

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* If we are in Flatpak container we don't expect to have a working bwrap */
  if (!g_file_test ("/.flatpak-info", G_FILE_TEST_IS_REGULAR)
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

  /* If it contains ./files/, assume it's a Flatpak-style runtime where
   * ./files is a merged /usr and ./metadata is an optional GKeyFile */
  self->source_files = g_build_filename (self->deployment, "files", NULL);

  if (g_file_test (self->source_files, G_FILE_TEST_IS_DIR))
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

  if (!g_file_test (self->tools_dir, G_FILE_TEST_IS_DIR))
    {
      return glnx_throw (error, "\"%s\" is not a directory",
                         self->tools_dir);
    }

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

      if (!pv_runtime_create_copy (self, mutable_lock, error))
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

  if (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK)
    {
      if (!glnx_opendirat (-1, self->provider_in_current_namespace, FALSE,
                           &self->provider_fd, error))
        return FALSE;

      /* Path that, when resolved in the host namespace, points to the provider */
      self->provider_in_host_namespace =
        pv_current_namespace_path_to_host_path (self->provider_in_current_namespace);
    }

  /* If we are in a Flatpak environment we expect to have the host system
   * mounted in `/run/host`. Otherwise we assume that the host system, in the
   * current namespace, is the root. */
  if (g_file_test ("/.flatpak-info", G_FILE_TEST_IS_REGULAR))
    self->host_in_current_namespace = "/run/host";
  else
    self->host_in_current_namespace = "/";

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
pv_runtime_finalize (GObject *object)
{
  PvRuntime *self = PV_RUNTIME (object);

  pv_runtime_cleanup (self);
  g_free (self->bubblewrap);
  g_strfreev (self->original_environ);
  g_free (self->libcapsule_knowledge);
  glnx_close_fd (&self->variable_dir_fd);
  g_free (self->variable_dir);
  glnx_close_fd (&self->mutable_sysroot_fd);
  g_free (self->mutable_sysroot);
  glnx_close_fd (&self->provider_fd);
  g_free (self->provider_in_current_namespace);
  g_free (self->provider_in_host_namespace);
  g_free (self->provider_in_container_namespace);
  g_free (self->runtime_files_on_host);
  g_free (self->runtime_usr);
  g_free (self->source);
  g_free (self->source_files);
  g_free (self->deployment);
  g_free (self->tools_dir);

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
  object_class->finalize = pv_runtime_finalize;

  properties[PROP_BUBBLEWRAP] =
    g_param_spec_string ("bubblewrap", "Bubblewrap",
                         "Bubblewrap executable",
                         NULL,
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

  properties[PROP_PROVIDER_IN_CURRENT_NAMESPACE] =
    g_param_spec_string ("provider-in-current-namespace",
                         "Provider in current namespace",
                         ("Path that, when resolved in the current namespace, "
                          "points to the provider"),
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_PROVIDER_IN_CONTAINER_NAMESPACE] =
    g_param_spec_string ("provider-in-container-namespace",
                         "Provider in container namespace",
                         ("Path to a directory in which the provider will be "
                          "accessible from inside the container"),
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

  properties[PROP_TOOLS_DIRECTORY] =
    g_param_spec_string ("tools-directory", "Tools directory",
                         "Path to pressure-vessel/bin in current namespace",
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
                const char *tools_dir,
                const char *provider_in_current_namespace,
                const char *provider_in_container_namespace,
                const GStrv original_environ,
                PvRuntimeFlags flags,
                GError **error)
{
  g_return_val_if_fail (source != NULL, NULL);
  g_return_val_if_fail (bubblewrap != NULL, NULL);
  g_return_val_if_fail (tools_dir != NULL, NULL);
  g_return_val_if_fail ((flags & ~(PV_RUNTIME_FLAGS_MASK)) == 0, NULL);

  return g_initable_new (PV_TYPE_RUNTIME,
                         NULL,
                         error,
                         "bubblewrap", bubblewrap,
                         "original-environ", original_environ,
                         "variable-dir", variable_dir,
                         "source", source,
                         "id", id,
                         "tools-directory", tools_dir,
                         "provider-in-current-namespace",
                           provider_in_current_namespace,
                         "provider-in-container-namespace",
                           provider_in_container_namespace == NULL ? "/run/host" : provider_in_container_namespace,
                         "flags", flags,
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
                              self->runtime_files,
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

  g_return_val_if_fail (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK, NULL);

  ret = pv_bwrap_copy (self->container_access_adverb);

  /* If we have a custom "LD_LIBRARY_PATH", we want to preserve
   * it when calling capsule-capture-libs */
  ld_library_path = g_environ_getenv (self->original_environ, "LD_LIBRARY_PATH");
  if (ld_library_path != NULL)
    flatpak_bwrap_set_env (ret, "LD_LIBRARY_PATH", ld_library_path, TRUE);

  /* Every symlink that starts with exactly /app/ (for Flatpak) */
  remap_app = g_strjoin (NULL, "/app/", "=",
                         self->provider_in_container_namespace,
                         "/app/", NULL);

  /* Every symlink that starts with exactly /usr/ */
  remap_usr = g_strjoin (NULL, "/usr/", "=",
                         self->provider_in_container_namespace,
                         "/usr/", NULL);

  /* Every symlink that starts with /lib, e.g. /lib64 */
  remap_lib = g_strjoin (NULL, "/lib", "=",
                         self->provider_in_container_namespace,
                         "/lib", NULL);

  flatpak_bwrap_add_args (ret,
                          arch->capsule_capture_libs,
                          "--container", self->container_access,
                          "--remap-link-prefix", remap_app,
                          "--remap-link-prefix", remap_usr,
                          "--remap-link-prefix", remap_lib,
                          "--provider",
                            self->provider_in_current_namespace,
                          NULL);

  if (self->libcapsule_knowledge)
    flatpak_bwrap_add_args (ret,
                            "--library-knowledge", self->libcapsule_knowledge,
                            NULL);

  return ret;
}

static gboolean
collect_s2tc (PvRuntime *self,
              RuntimeArchitecture *arch,
              const char *libdir,
              GError **error)
{
  g_autofree gchar *s2tc = g_build_filename (libdir, "libtxc_dxtn.so", NULL);
  g_autofree gchar *s2tc_in_current_namespace = g_build_filename (
                                                  self->provider_in_current_namespace,
                                                  s2tc,
                                                  NULL);

  g_return_val_if_fail (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK, FALSE);

  if (g_file_test (s2tc_in_current_namespace, G_FILE_TEST_EXISTS))
    {
      g_autoptr(FlatpakBwrap) temp_bwrap = NULL;
      g_autofree gchar *expr = NULL;

      g_debug ("Collecting s2tc \"%s\" and its dependencies...", s2tc);
      expr = g_strdup_printf ("path-match:%s", s2tc);

      if (!pv_runtime_provide_container_access (self, error))
        return FALSE;

      temp_bwrap = pv_runtime_get_capsule_capture_libs (self, arch);
      flatpak_bwrap_add_args (temp_bwrap,
                              "--dest", arch->libdir_in_current_namespace,
                              expr,
                              NULL);
      flatpak_bwrap_finish (temp_bwrap);

      if (!pv_bwrap_run_sync (temp_bwrap, NULL, error))
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

typedef struct
{
  /* (type SrtEglIcd) or (type SrtVulkanIcd) or (type SrtVdpauDriver)
   * or (type SrtVaApiDriver) or (type SrtVulkanLayer) or
   * (type SrtDriDriver) */
  gpointer icd;
  gchar *resolved_library;
  /* Last entry is always NONEXISTENT; keyed by the index of a multiarch
   * tuple in multiarch_tuples. */
  IcdKind kinds[G_N_ELEMENTS (multiarch_tuples)];
  /* Last entry is always NULL */
  gchar *paths_in_container[G_N_ELEMENTS (multiarch_tuples)];
} IcdDetails;

static IcdDetails *
icd_details_new (gpointer icd)
{
  IcdDetails *self;
  gsize i;

  g_return_val_if_fail (G_IS_OBJECT (icd), NULL);
  g_return_val_if_fail (SRT_IS_DRI_DRIVER (icd) ||
                        SRT_IS_EGL_ICD (icd) ||
                        SRT_IS_VULKAN_ICD (icd) ||
                        SRT_IS_VULKAN_LAYER (icd) ||
                        SRT_IS_VDPAU_DRIVER (icd) ||
                        SRT_IS_VA_API_DRIVER (icd),
                        NULL);

  self = g_slice_new0 (IcdDetails);
  self->icd = g_object_ref (icd);
  self->resolved_library = NULL;

  for (i = 0; i < G_N_ELEMENTS (multiarch_tuples); i++)
    {
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
  g_free (self->resolved_library);

  for (i = 0; i < G_N_ELEMENTS (multiarch_tuples); i++)
    g_free (self->paths_in_container[i]);

  g_slice_free (IcdDetails, self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (IcdDetails, icd_details_free)

/*
 * @destination: (not nullable): where to capture the libraries
 * @patterns: (not nullable): array of patterns for capsule-capture-libs
 */
static gboolean
pv_runtime_capture_libraries (PvRuntime *self,
                              RuntimeArchitecture *arch,
                              const gchar *destination,
                              GPtrArray *patterns,
                              GError **error)
{
  g_autoptr(FlatpakBwrap) temp_bwrap = NULL;
  gsize i;

  g_return_val_if_fail (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK, FALSE);
  g_return_val_if_fail (runtime_architecture_check_valid (arch), FALSE);
  g_return_val_if_fail (destination != NULL, FALSE);
  g_return_val_if_fail (patterns != NULL, FALSE);

  if (!pv_runtime_provide_container_access (self, error))
    return FALSE;

  temp_bwrap = pv_runtime_get_capsule_capture_libs (self, arch);
  flatpak_bwrap_add_args (temp_bwrap, "--dest", destination, NULL);

  for (i = 0; i < patterns->len; i++)
    flatpak_bwrap_add_arg (temp_bwrap, (gchar *)g_ptr_array_index (patterns, i));

  flatpak_bwrap_finish (temp_bwrap);

  if (!pv_bwrap_run_sync (temp_bwrap, NULL, error))
    return FALSE;

  return TRUE;
}

/*
 * @sequence_number: numbered directory to use to disambiguate between
 *  colliding files with the same basename
 * @requested_subdir: (not nullable):
 * @use_numbered_subdirs: (inout) (not optional): if %TRUE, use a
 *  numbered subdirectory per ICD, for the rare case where not all
 *  drivers have a unique basename or where order matters
 * @use_subdir_for_kind_soname: if %TRUE, the @requested_subdir will be always
 *  used. If %FALSE, the @requested_subdir will not be used when the provided
 *  library is of the kind "ICD_KIND_SONAME".
 * @dependency_patterns: (inout) (not nullable): array of patterns for
 *  capsule-capture-libs
 * @search_path: (nullable): Add the parent directory of the resulting
 *  ICD to this search path if necessary
 *
 * Bind the provided @details ICD without its dependencies, and update
 * @dependency_patterns with @details dependency pattern.
 */
static gboolean
bind_icd (PvRuntime *self,
          RuntimeArchitecture *arch,
          gsize sequence_number,
          const char *requested_subdir,
          IcdDetails *details,
          gboolean *use_numbered_subdirs,
          gboolean use_subdir_for_kind_soname,
          GPtrArray *dependency_patterns,
          GString *search_path,
          GError **error)
{
  static const char options[] = "if-exists:if-same-abi";
  g_autofree gchar *in_current_namespace = NULL;
  g_autofree gchar *pattern = NULL;
  g_autofree gchar *dependency_pattern = NULL;
  g_autofree gchar *seq_str = NULL;
  g_autofree gchar *final_path = NULL;
  const char *base;
  const char *mode;
  g_autoptr(FlatpakBwrap) temp_bwrap = NULL;
  gsize multiarch_index;
  g_autoptr(GDir) dir = NULL;
  gsize dir_elements_before = 0;
  gsize dir_elements_after = 0;
  const gchar *subdir = requested_subdir;

  g_return_val_if_fail (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK, FALSE);
  g_return_val_if_fail (runtime_architecture_check_valid (arch), FALSE);
  g_return_val_if_fail (subdir != NULL, FALSE);
  g_return_val_if_fail (details != NULL, FALSE);
  g_return_val_if_fail (details->resolved_library != NULL, FALSE);
  multiarch_index = arch->multiarch_index;
  g_return_val_if_fail (details->kinds[multiarch_index] == ICD_KIND_NONEXISTENT,
                        FALSE);
  g_return_val_if_fail (details->paths_in_container[multiarch_index] == NULL,
                        FALSE);
  g_return_val_if_fail (use_numbered_subdirs != NULL, FALSE);
  g_return_val_if_fail (dependency_patterns != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  g_info ("Capturing loadable module: %s", details->resolved_library);

  if (g_path_is_absolute (details->resolved_library))
    {
      details->kinds[multiarch_index] = ICD_KIND_ABSOLUTE;
      mode = "path";
    }
  else
    {
      details->kinds[multiarch_index] = ICD_KIND_SONAME;
      mode = "soname";
      if (!use_subdir_for_kind_soname)
        subdir = "";
    }

  in_current_namespace = g_build_filename (arch->libdir_in_current_namespace,
                                           subdir, NULL);

  if (g_mkdir_with_parents (in_current_namespace, 0700) != 0)
    return glnx_throw_errno_prefix (error, "Unable to create %s",
                                    in_current_namespace);

  base = glnx_basename (details->resolved_library);

  /* Check whether we can get away with avoiding the sequence number.
   * Depending on the type of ICD, we might want to use the sequence
   * number to force a specific load order. */
  if (!*use_numbered_subdirs)
    {
      g_autofree gchar *path = NULL;

      path = g_build_filename (in_current_namespace, base, NULL);

      /* No, we can't: the ICD would collide with one that we already
       * set up */
      if (g_file_test (path, G_FILE_TEST_IS_SYMLINK))
        *use_numbered_subdirs = TRUE;
    }

  /* If we can't avoid the numbered subdirectory, or want to use one
   * to force a specific load order, create it. */
  if (*use_numbered_subdirs && subdir[0] != '\0')
    {
      seq_str = g_strdup_printf ("%" G_GSIZE_FORMAT, sequence_number);
      g_clear_pointer (&in_current_namespace, g_free);
      in_current_namespace = g_build_filename (arch->libdir_in_current_namespace,
                                               subdir, seq_str, NULL);

      if (g_mkdir_with_parents (in_current_namespace, 0700) != 0)
        return glnx_throw_errno_prefix (error, "Unable to create %s",
                                        in_current_namespace);
    }

  final_path = g_build_filename (in_current_namespace, base, NULL);
  if (g_file_test (final_path, G_FILE_TEST_IS_SYMLINK))
    {
      g_info ("\"%s\" is already present, skipping", final_path);
      return TRUE;
    }

  dir = g_dir_open (in_current_namespace, 0, error);
  if (dir == NULL)
    return FALSE;

  /* Number of elements before trying to capture the library */
  while (g_dir_read_name (dir))
    dir_elements_before++;

  pattern = g_strdup_printf ("no-dependencies:even-if-older:%s:%s:%s",
                             options, mode, details->resolved_library);
  dependency_pattern = g_strdup_printf ("only-dependencies:%s:%s:%s",
                                        options, mode, details->resolved_library);

  if (!pv_runtime_provide_container_access (self, error))
    return FALSE;

  temp_bwrap = pv_runtime_get_capsule_capture_libs (self, arch);
  flatpak_bwrap_add_args (temp_bwrap,
                          "--dest", in_current_namespace,
                          pattern,
                          NULL);
  flatpak_bwrap_finish (temp_bwrap);

  if (!pv_bwrap_run_sync (temp_bwrap, NULL, error))
    return FALSE;

  g_clear_pointer (&temp_bwrap, flatpak_bwrap_free);

  g_dir_rewind (dir);
  while (g_dir_read_name (dir))
    dir_elements_after++;

  if (dir_elements_before == dir_elements_after)
    {
      /* If we have the same number of elements it means that we didn't
       * create a symlink to the ICD itself (it must have been nonexistent
       * or for a different ABI). When this happens we set the kinds to
       * "NONEXISTENT" and return early without trying to capture the
       * dependencies. */
      details->kinds[multiarch_index] = ICD_KIND_NONEXISTENT;
      /* If the directory is empty we can also remove it */
      g_rmdir (in_current_namespace);
      return TRUE;
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

  g_ptr_array_add (dependency_patterns, g_steal_pointer (&dependency_pattern));

  if (details->kinds[multiarch_index] == ICD_KIND_ABSOLUTE)
    {
      g_assert (in_current_namespace != NULL);
      details->paths_in_container[multiarch_index] = g_build_filename (arch->libdir_in_container,
                                                                       subdir,
                                                                       seq_str ? seq_str : "",
                                                                       glnx_basename (details->resolved_library),
                                                                       NULL);
    }

  return TRUE;
}

static gboolean
bind_runtime_base (PvRuntime *self,
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
    NULL
  };
  g_autofree gchar *xrd = g_strdup_printf ("/run/user/%ld", (long) geteuid ());
  gsize i, j;
  const gchar *member;

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (!pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (container_env != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!pv_bwrap_bind_usr (bwrap, self->runtime_files_on_host, self->runtime_files, "/", error))
    return FALSE;

  /* In the case where we have a mutable sysroot, we mount the overrides
   * as part of /usr. Make /overrides a symbolic link, to be nice to
   * older steam-runtime-tools versions. */

  if (self->mutable_sysroot != NULL)
    {
      g_assert (self->overrides_in_container[0] == '/');
      g_assert (g_strcmp0 (self->overrides_in_container, "/overrides") != 0);
      flatpak_bwrap_add_args (bwrap,
                              "--symlink",
                              &self->overrides_in_container[1],
                              "/overrides",
                              NULL);

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

  pv_environ_lock_env (container_env, "XDG_RUNTIME_DIR", xrd);

  if ((self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK)
      && (g_strcmp0 (self->provider_in_host_namespace, "/") != 0
          || g_strcmp0 (self->provider_in_container_namespace, "/run/host") != 0))
    {
      g_autofree gchar *provider_etc = NULL;

      if (!pv_bwrap_bind_usr (bwrap,
                              self->provider_in_host_namespace,
                              self->provider_in_current_namespace,
                              self->provider_in_container_namespace,
                              error))
        return FALSE;

      provider_etc = g_build_filename (self->provider_in_current_namespace,
                                       "etc", NULL);

      if (g_file_test (provider_etc, G_FILE_TEST_IS_DIR))
        {
          g_autofree gchar *in_host = NULL;
          g_autofree gchar *in_container = NULL;

          in_host = g_build_filename (self->provider_in_host_namespace,
                                      "etc", NULL);
          in_container = g_build_filename (self->provider_in_container_namespace,
                                           "etc", NULL);

          flatpak_bwrap_add_args (bwrap,
                                  "--ro-bind", in_host, in_container,
                                  NULL);
        }
    }

  for (i = 0; i < G_N_ELEMENTS (bind_mutable); i++)
    {
      g_autofree gchar *path = g_build_filename (self->runtime_files,
                                                 bind_mutable[i],
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

          if (g_strv_contains (from_host, dest))
            continue;

          if ((self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK)
              && g_strv_contains (from_provider, dest))
            continue;

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

  /* glibc from some distributions will want to load the ld.so cache from
   * a distribution-specific path, e.g. Clear Linux uses
   * /var/cache/ldconfig/ld.so.cache. For simplicity, we make all these
   * paths symlinks to /etc/ld.so.cache, so that we only have to populate
   * the cache in one place. */
  for (i = 0; i < G_N_ELEMENTS (other_ld_so_cache); i++)
    {
      const char *path = other_ld_so_cache[i];

      flatpak_bwrap_add_args (bwrap,
                              "--symlink", "/etc/ld.so.cache", path,
                              NULL);
    }

  /* glibc from some distributions will want to load the ld.so cache from
   * a distribution- and architecture-specific path, e.g. Exherbo
   * does this. Again, for simplicity we direct all these to the same path:
   * it's OK to mix multiple architectures' libraries into one cache,
   * as done in upstream glibc (and Debian, Arch, etc.). */
  for (i = 0; i < G_N_ELEMENTS (multiarch_details); i++)
    {
      const MultiarchDetails *details = &multiarch_details[i];

      for (j = 0; j < G_N_ELEMENTS (details->other_ld_so_cache); j++)
        {
          const char *base = details->other_ld_so_cache[j];
          g_autofree gchar *path = NULL;

          if (base == NULL)
            break;

          path = g_build_filename ("/etc", base, NULL);
          flatpak_bwrap_add_args (bwrap,
                                  "--symlink", "/etc/ld.so.cache", path,
                                  NULL);
        }
    }

  /* If we are in a Flatpak environment, we need to test if these files are
   * available in the host, and not in the current environment, because we will
   * run bwrap in the host system */
  if (_srt_file_test_in_sysroot (self->host_in_current_namespace, -1,
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
  else if (_srt_file_test_in_sysroot (self->host_in_current_namespace, -1,
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

      if (_srt_file_test_in_sysroot (self->host_in_current_namespace, -1,
                                     item, G_FILE_TEST_EXISTS))
        flatpak_bwrap_add_args (bwrap,
                                "--ro-bind", item, item,
                                NULL);
    }

  if (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK)
    {
      for (i = 0; from_provider[i] != NULL; i++)
        {
          const char *item = from_provider[i];
          g_autoptr(GError) local_error = NULL;
          g_autofree char *path_in_provider = NULL;
          glnx_autofd int fd = -1;

          fd = _srt_resolve_in_sysroot (self->provider_fd, item,
                                        SRT_RESOLVE_FLAGS_NONE,
                                        &path_in_provider,
                                        &local_error);

          if (fd >= 0)
            {
              g_autofree char *host_path = NULL;

              host_path = g_build_filename (self->provider_in_host_namespace,
                                            path_in_provider, NULL);
              flatpak_bwrap_add_args (bwrap,
                                      "--ro-bind", host_path, item,
                                      NULL);
            }
          else
            {
              g_debug ("Cannot resolve \"%s\" in \"%s\": %s",
                       item, self->provider_in_current_namespace,
                       local_error->message);
              g_clear_error (&local_error);
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

  pv_export_symlink_targets (exports, self->overrides);

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
  if (_srt_file_test_in_sysroot (self->host_in_current_namespace, -1,
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
  TAKE_FROM_PROVIDER_FLAGS_NONE = 0
} TakeFromProviderFlags;

static gboolean
pv_runtime_take_from_provider (PvRuntime *self,
                               FlatpakBwrap *bwrap,
                               const char *source_in_provider,
                               const char *dest_in_container,
                               TakeFromProviderFlags flags,
                               GError **error)
{
  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK, FALSE);
  g_return_val_if_fail (bwrap == NULL || !pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (bwrap != NULL || self->mutable_sysroot != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (flags & TAKE_FROM_PROVIDER_FLAGS_IF_DIR)
    {
      if (!_srt_file_test_in_sysroot (self->provider_in_current_namespace, -1,
                                      source_in_provider, G_FILE_TEST_IS_DIR))
        return TRUE;
    }

  if (flags & TAKE_FROM_PROVIDER_FLAGS_IF_EXISTS)
    {
      if (!_srt_file_test_in_sysroot (self->provider_in_current_namespace, -1,
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
      if (!path_visible_in_provider_namespace (self->flags, source_in_provider))
        {
          if (flags & TAKE_FROM_PROVIDER_FLAGS_COPY_FALLBACK)
            {
              glnx_autofd int file_fd = -1;
              glnx_autofd int dest_fd = -1;
              glnx_autofd int sysroot_fd = -1;

              if (!glnx_opendirat (-1, self->provider_in_current_namespace,
                                   FALSE, &sysroot_fd, error))
                return FALSE;

              file_fd = _srt_resolve_in_sysroot (sysroot_fd, source_in_provider,
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
                                                self->provider_in_current_namespace,
                                                source_in_provider,
                                                dest_in_container);

              return TRUE;
            }
          else
            {
              g_warning ("\"%s\" is unlikely to appear in \"%s\"",
                         source_in_provider, self->provider_in_container_namespace);
              /* ... but try it anyway, it can't hurt */
            }
        }

      target = g_build_filename (self->provider_in_container_namespace, source_in_provider, NULL);

      if (TEMP_FAILURE_RETRY (symlinkat (target, parent_dirfd, base)) != 0)
        return glnx_throw_errno_prefix (error,
                                        "Unable to create symlink \"%s/%s\" -> \"%s\"",
                                        self->mutable_sysroot,
                                        dest_in_container, target);
    }
  else
    {
      /* We can't edit the runtime in-place, so tell bubblewrap to mount
       * a new version over the top */
      g_assert (bwrap != NULL);

      if (flags & TAKE_FROM_PROVIDER_FLAGS_IF_CONTAINER_COMPATIBLE)
        {
          g_autofree gchar *dest = NULL;

          if (g_str_has_prefix (dest_in_container, "/usr/"))
            dest = g_build_filename (self->runtime_usr,
                                     dest_in_container + strlen ("/usr/"),
                                     NULL);
          else
            dest = g_build_filename (self->runtime_files,
                                     dest_in_container,
                                     NULL);

          if (g_file_test (source_in_provider, G_FILE_TEST_IS_DIR))
            {
              if (!g_file_test (dest, G_FILE_TEST_IS_DIR))
                {
                  g_warning ("Not mounting \"%s\" over non-directory file or "
                             "nonexistent path \"%s\"",
                             source_in_provider, dest);
                  return TRUE;
                }
            }
          else
            {
              if (g_file_test (dest, G_FILE_TEST_IS_DIR) ||
                  !g_file_test (dest, G_FILE_TEST_EXISTS))
                {
                  g_warning ("Not mounting \"%s\" over directory or "
                             "nonexistent path \"%s\"",
                             source_in_provider, dest);
                  return TRUE;
                }
            }
        }

      flatpak_bwrap_add_args (bwrap,
                              "--ro-bind", source_in_provider, dest_in_container,
                              NULL);
    }

  return TRUE;
}

static gboolean
pv_runtime_remove_overridden_libraries (PvRuntime *self,
                                        RuntimeArchitecture *arch,
                                        GError **error)
{
  g_autoptr(GPtrArray) dirs = NULL;
  GHashTable **delete = NULL;
  GLnxDirFdIterator *iters = NULL;
  gboolean ret = FALSE;
  gsize i;

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (arch != NULL, FALSE);
  g_return_val_if_fail (arch->ld_so != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* Not applicable/possible if we don't have a mutable sysroot */
  g_return_val_if_fail (self->mutable_sysroot != NULL, FALSE);

  dirs = multiarch_details_get_libdirs (arch->details,
                                        MULTIARCH_LIBDIRS_FLAGS_REMOVE_OVERRIDDEN);
  delete = g_new0 (GHashTable *, dirs->len);
  iters = g_new0 (GLnxDirFdIterator, dirs->len);

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
              if (iters[j].initialized
                  && _srt_fstatat_is_same_file (libdir_fd, "",
                                                iters[j].fd, ""))
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

      if (!glnx_dirfd_iterator_init_take_fd (&libdir_fd, &iters[i], error))
        {
          glnx_prefix_error (error, "Unable to start iterating \"%s/%s\"",
                             self->mutable_sysroot,
                             libdir);
          goto out;
        }

      delete[i] = g_hash_table_new_full (g_str_hash, g_str_equal,
                                         g_free, g_free);

      while (TRUE)
        {
          g_autoptr(Elf) elf = NULL;
          g_autoptr(GError) local_error = NULL;
          glnx_autofd int libfd = -1;
          g_autofree gchar *path = NULL;
          g_autofree gchar *soname = NULL;
          g_autofree gchar *target = NULL;

          if (!glnx_dirfd_iterator_next_dent_ensure_dtype (&iters[i], &dent,
                                                           NULL, error))
            return glnx_prefix_error (error, "Unable to iterate over \"%s/%s\"",
                                      self->mutable_sysroot,
                                      libdir);

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

          path = g_build_filename (libdir, dent->d_name, NULL);

          /* scope for soname_link */
            {
              g_autofree gchar *soname_link = NULL;

              soname_link = g_build_filename (arch->libdir_in_current_namespace,
                                              dent->d_name, NULL);

              /* If we found libfoo.so.1 in the container, and libfoo.so.1
               * also exists among the overrides, delete it. */
              if (g_file_test (soname_link, G_FILE_TEST_IS_SYMLINK))
                {
                  g_hash_table_replace (delete[i],
                                        g_strdup (dent->d_name),
                                        g_steal_pointer (&soname_link));
                  continue;
                }
            }

          target = glnx_readlinkat_malloc (iters[i].fd, dent->d_name,
                                           NULL, NULL);

          if (target != NULL)
            {
              g_autofree gchar *soname_link = NULL;

              soname_link = g_build_filename (arch->libdir_in_current_namespace,
                                              glnx_basename (target),
                                              NULL);

              /* If the symlink in the container points to
               * /foo/bar/libfoo.so.1, and libfoo.so.1 also exists among
               * the overrides, delete it. */
              if (g_file_test (soname_link, G_FILE_TEST_IS_SYMLINK))
                {
                  g_hash_table_replace (delete[i],
                                        g_strdup (dent->d_name),
                                        g_steal_pointer (&soname_link));
                  continue;
                }
            }

          libfd = _srt_resolve_in_sysroot (self->mutable_sysroot_fd, path,
                                           SRT_RESOLVE_FLAGS_READABLE, NULL,
                                           &local_error);

          if (libfd < 0)
            {
              g_warning ("Unable to open %s/%s for reading: %s",
                         self->mutable_sysroot, path, local_error->message);
              g_clear_error (&local_error);
              continue;
            }

          elf = pv_elf_open_fd (libfd, &local_error);

          if (elf != NULL)
            soname = pv_elf_get_soname (elf, &local_error);

          if (soname == NULL)
            {
              g_warning ("Unable to get SONAME of %s/%s: %s",
                         self->mutable_sysroot, path, local_error->message);
              g_clear_error (&local_error);
              continue;
            }

          /* If we found a library with SONAME libfoo.so.1 in the
           * container, and libfoo.so.1 also exists among the overrides,
           * delete it. */
            {
              g_autofree gchar *soname_link = NULL;

              soname_link = g_build_filename (arch->libdir_in_current_namespace,
                                              soname, NULL);

              if (g_file_test (soname_link, G_FILE_TEST_IS_SYMLINK))
                {
                  g_hash_table_replace (delete[i],
                                        g_strdup (dent->d_name),
                                        g_steal_pointer (&soname_link));
                  continue;
                }
            }
        }
    }

  for (i = 0; i < dirs->len; i++)
    {
      const char *libdir = g_ptr_array_index (dirs, i);

      if (delete[i] == NULL)
        continue;

      g_assert (iters[i].initialized);
      g_assert (iters[i].fd >= 0);

      GLNX_HASH_TABLE_FOREACH_KV (delete[i],
                                  const char *, name,
                                  const char *, reason)
        {
          g_autoptr(GError) local_error = NULL;

          g_debug ("Deleting %s/%s/%s because %s replaces it",
                   self->mutable_sysroot, libdir, name, reason);

          if (!glnx_unlinkat (iters[i].fd, name, 0, &local_error))
            {
              g_warning ("Unable to delete %s/%s/%s: %s",
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
          glnx_dirfd_iterator_clear (&iters[i]);
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
  g_autofree gchar *ld_so_in_provider = NULL;

  g_return_val_if_fail (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK, FALSE);
  g_return_val_if_fail (bwrap != NULL || self->mutable_sysroot != NULL, FALSE);

  g_debug ("Making provider's ld.so visible in container");

  path_fd = _srt_resolve_in_sysroot (self->provider_fd,
                                     arch->ld_so, SRT_RESOLVE_FLAGS_READABLE,
                                     &ld_so_relative_to_provider, error);

  if (path_fd < 0)
    {
      g_prefix_error (error, "Unable to determine provider path to %s: ",
                      arch->ld_so);
      return FALSE;
    }

  ld_so_in_provider = g_build_filename (self->provider_in_host_namespace,
                                        ld_so_relative_to_provider, NULL);

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
      !pv_runtime_take_from_provider (self, bwrap, ld_so_in_provider,
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
  return pv_runtime_take_from_provider (self, bwrap, ld_so_in_provider,
                                        ld_so_in_runtime,
                                        TAKE_FROM_PROVIDER_FLAGS_NONE, error);
}

static gchar *
pv_runtime_search_in_path_and_bin (PvRuntime *self,
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

  if (g_strcmp0 (self->host_in_current_namespace, "/") == 0)
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
      if (_srt_file_test_in_sysroot (self->host_in_current_namespace, -1,
                                     test_path, G_FILE_TEST_IS_EXECUTABLE))
        return g_steal_pointer (&test_path);
    }

  return NULL;
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
 * @seq: Sequence number of @details, used to make unique filenames
 * @search_path: Used to build `$VK_ICD_FILENAMES` or a similar search path
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
                     gsize seq,
                     GString *search_path,
                     GError **error)
{
  SrtVulkanLayer *layer = NULL;
  SrtVulkanIcd *icd = NULL;
  SrtEglIcd *egl = NULL;
  gboolean need_provider_json = FALSE;
  gsize i;

  g_return_val_if_fail (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK, FALSE);
  g_return_val_if_fail (bwrap != NULL || self->mutable_sysroot != NULL, FALSE);

  if (SRT_IS_VULKAN_LAYER (details->icd))
    layer = SRT_VULKAN_LAYER (details->icd);
  else if (SRT_IS_VULKAN_ICD (details->icd))
    icd = SRT_VULKAN_ICD (details->icd);
  else if (SRT_IS_EGL_ICD (details->icd))
    egl = SRT_EGL_ICD (details->icd);
  else
    g_return_val_if_reached (FALSE);

  /* If the layer failed to load, there's nothing to make available */
  if (layer != NULL)
    {
      if (!srt_vulkan_layer_check_error (layer, NULL))
        return TRUE;
    }
  else if (egl != NULL)
    {
      if (!srt_egl_icd_check_error (egl, NULL))
        return TRUE;
    }
  else
    {
      if (!srt_vulkan_icd_check_error (icd, NULL))
        return TRUE;
    }

  for (i = 0; i < G_N_ELEMENTS (multiarch_tuples) - 1; i++)
    {
      g_assert (i < G_N_ELEMENTS (details->kinds));
      g_assert (i < G_N_ELEMENTS (details->paths_in_container));

      if (details->kinds[i] == ICD_KIND_ABSOLUTE)
        {
          g_autofree gchar *write_to_file = NULL;
          g_autofree gchar *json_in_container = NULL;
          g_autofree gchar *json_base = NULL;

          g_assert (details->paths_in_container[i] != NULL);

          json_base = g_strdup_printf ("%" G_GSIZE_FORMAT "-%s.json",
                                       seq, multiarch_tuples[i]);
          write_to_file = g_build_filename (write_to_dir, json_base, NULL);
          json_in_container = g_build_filename (self->overrides_in_container,
                                                "share", sub_dir,
                                                json_base, NULL);

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
      else
        json_in_provider = srt_vulkan_icd_get_json_path (icd);

      json_base = g_strdup_printf ("%" G_GSIZE_FORMAT ".json", seq);
      json_in_container = g_build_filename (self->overrides_in_container,
                                            "share", sub_dir,
                                            json_base, NULL);

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
 * @search_path: Used to build `$VK_ICD_FILENAMES` or a similar search path
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

  g_return_val_if_fail (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK, FALSE);
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
                                j, search_path, error))
        return FALSE;
    }

  return TRUE;
}

static gboolean
collect_vulkan_layers (PvRuntime *self,
                       const GPtrArray *layer_details,
                       GPtrArray *dependency_patterns,
                       RuntimeArchitecture *arch,
                       const gchar *dir_name,
                       GError **error)
{
  gsize j;

  g_return_val_if_fail (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK, FALSE);
  g_return_val_if_fail (dependency_patterns != NULL, FALSE);
  g_return_val_if_fail (dir_name != NULL, FALSE);

  for (j = 0; j < layer_details->len; j++)
    {
      g_autoptr(SrtLibrary) library = NULL;
      SrtLibraryIssues issues;
      IcdDetails *details = g_ptr_array_index (layer_details, j);
      SrtVulkanLayer *layer = SRT_VULKAN_LAYER (details->icd);
      /* We don't have to use multiple directories unless there are
       * filename collisions, because the order of the JSON manifests
       * might matter, but the order of the actual libraries does not. */
      gboolean use_numbered_subdirs = FALSE;
      /* If we have just a SONAME, we do not want to place the library
       * under a subdir, otherwise ld.so will not be able to find it */
      const gboolean use_subdir_for_kind_soname = FALSE;

      if (!srt_vulkan_layer_check_error (layer, NULL))
        continue;

      /* For meta-layers we don't have a library path */
      if (srt_vulkan_layer_get_library_path(layer) == NULL)
        {
          details->kinds[arch->multiarch_index] = ICD_KIND_META_LAYER;
          continue;
        }

      /* If the library_path is relative to the JSON file, turn it into an
       * absolute path. If it's already absolute, or if it's a basename to be
       * looked up in the system library search path, use it as-is. */
      details->resolved_library = srt_vulkan_layer_resolve_library_path (layer);
      g_assert (details->resolved_library != NULL);

      if (strchr (details->resolved_library, '/') != NULL &&
          (strstr (details->resolved_library, "$ORIGIN/") != NULL ||
           strstr (details->resolved_library, "${ORIGIN}") != NULL ||
           strstr (details->resolved_library, "$LIB/") != NULL ||
           strstr (details->resolved_library, "${LIB}") != NULL ||
           strstr (details->resolved_library, "$PLATFORM/") != NULL ||
           strstr (details->resolved_library, "${PLATFORM}") != NULL))
        {
          /* When loading a library by its absolute or relative path
           * (but not when searching the library path for its basename),
           * glibc expands dynamic string tokens: LIB, PLATFORM, ORIGIN.
           * libcapsule cannot expand these special tokens: the only thing
           * that knows the correct magic values for them is glibc, which has
           * no API to tell us. The only way we can find out the library's
           * real location is to tell libdl to load (dlopen) the library, and
           * see what the resulting path is. */
          if (g_strcmp0 (self->provider_in_current_namespace, "/") == 0)
            {
              /* It's in our current namespace, so we can dlopen it. */
              issues = srt_check_library_presence (details->resolved_library,
                                                   arch->details->tuple, NULL,
                                                   SRT_LIBRARY_SYMBOLS_FORMAT_PLAIN,
                                                   &library);
              if (issues & (SRT_LIBRARY_ISSUES_CANNOT_LOAD |
                            SRT_LIBRARY_ISSUES_UNKNOWN |
                            SRT_LIBRARY_ISSUES_TIMEOUT))
                {
                  g_info ("Unable to load library %s: %s", details->resolved_library,
                          srt_library_get_messages (library));
                  continue;
                }
              g_free (details->resolved_library);
              details->resolved_library = g_strdup (srt_library_get_absolute_path (library));
            }
          else
            {
              /* Sorry, we can't know how to load this. */
              g_info ("Cannot support ld.so special tokens, e.g. ${LIB}, when provider "
                      "is not the root filesystem");
              continue;
            }
        }

      if (!bind_icd (self, arch, j, dir_name, details, &use_numbered_subdirs,
                     use_subdir_for_kind_soname, dependency_patterns, NULL, error))
        return FALSE;
    }

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
      glnx_autofd int fd = -1;

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

      /* Do it the hard way, by asking a process running in the
       * container (or at least a container resembling the one we
       * are going to use) to resolve it for us */
      temp_bwrap = flatpak_bwrap_new (NULL);
      flatpak_bwrap_add_args (temp_bwrap,
                              self->bubblewrap,
                              NULL);

      if (!pv_bwrap_bind_usr (temp_bwrap,
                              self->runtime_files_on_host,
                              self->runtime_files,
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

      if (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK)
        {
          g_autofree gchar *provider_etc = NULL;
          g_autofree gchar *provider_etc_dest = NULL;

          if (!pv_bwrap_bind_usr (temp_bwrap,
                                  self->provider_in_host_namespace,
                                  self->provider_in_current_namespace,
                                  self->provider_in_container_namespace,
                                  error))
            return FALSE;

          provider_etc = g_build_filename (self->provider_in_host_namespace,
                                           "etc", NULL);
          provider_etc_dest = g_build_filename (self->provider_in_container_namespace,
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
  static const char * const soname_globs[] =
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

  for (i = 0; i < G_N_ELEMENTS (soname_globs); i++)
    g_ptr_array_add (patterns,
                     g_strdup_printf ("if-exists:even-if-older:soname-match:%s",
                                      soname_globs[i]));
}

static gboolean
pv_runtime_collect_libc_family (PvRuntime *self,
                                RuntimeArchitecture *arch,
                                FlatpakBwrap *bwrap,
                                const char *libc,
                                const char *ld_so_in_runtime,
                                const char *provider_in_container_namespace_guarded,
                                GHashTable *gconv_in_provider,
                                GError **error)
{
  g_autoptr(FlatpakBwrap) temp_bwrap = NULL;
  g_autofree char *libc_target = NULL;

  g_return_val_if_fail (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK, FALSE);
  g_return_val_if_fail (bwrap != NULL || self->mutable_sysroot != NULL, FALSE);

  if (!pv_runtime_take_ld_so_from_provider (self, arch,
                                            ld_so_in_runtime,
                                            bwrap, error))
    return FALSE;

  /* Collect miscellaneous libraries that libc might dlopen. */
  temp_bwrap = pv_runtime_get_capsule_capture_libs (self, arch);
  flatpak_bwrap_add_args (temp_bwrap,
                          "--dest", arch->libdir_in_current_namespace,
                          "if-exists:libidn2.so.0",
                          "if-exists:even-if-older:soname-match:libnss_compat.so.*",
                          "if-exists:even-if-older:soname-match:libnss_db.so.*",
                          "if-exists:even-if-older:soname-match:libnss_dns.so.*",
                          "if-exists:even-if-older:soname-match:libnss_files.so.*",
                          NULL);
  flatpak_bwrap_finish (temp_bwrap);

  if (!pv_bwrap_run_sync (temp_bwrap, NULL, error))
    return FALSE;

  g_clear_pointer (&temp_bwrap, flatpak_bwrap_free);

  libc_target = glnx_readlinkat_malloc (-1, libc, NULL, NULL);
  if (libc_target != NULL)
    {
      g_autofree gchar *dir = NULL;
      g_autofree gchar *gconv_dir_in_provider = NULL;
      gboolean found = FALSE;

      dir = g_path_get_dirname (libc_target);

      if (g_str_has_prefix (dir, provider_in_container_namespace_guarded))
        memmove (dir,
                 dir + strlen (self->provider_in_container_namespace),
                 strlen (dir) - strlen (self->provider_in_container_namespace) + 1);

      /* We are assuming that in the glibc "Makeconfig", $(libdir) was the same as
       * $(slibdir) (this is the upstream default) or the same as "/usr$(slibdir)"
       * (like in Debian without the mergerd /usr). We also assume that $(gconvdir)
       * had its default value "$(libdir)/gconv".
       * We check /usr first because otherwise, if the host is merged-/usr and the
       * container is not, we might end up binding /lib instead of /usr/lib
       * and that could cause issues. */
      if (g_str_has_prefix (dir, "/usr/"))
        memmove (dir, dir + strlen ("/usr"), strlen (dir) - strlen ("/usr") + 1);

      gconv_dir_in_provider = g_build_filename ("/usr", dir, "gconv", NULL);

      if (_srt_file_test_in_sysroot (self->provider_in_current_namespace,
                                     -1,
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
          gconv_dir_in_provider = g_build_filename ("/usr", dir, "gconv", NULL);

          if (_srt_file_test_in_sysroot (self->provider_in_current_namespace,
                                         -1,
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
                  "to be located in \"%s/gconv\", but instead it is missing",
                  dir);
        }
    }

  return TRUE;
}

static void
pv_runtime_collect_lib_data (PvRuntime *self,
                             RuntimeArchitecture *arch,
                             const char *dir_basename,
                             const char *lib_path,
                             const char *provider_in_container_namespace_guarded,
                             GHashTable *data_in_provider)
{
  g_autofree char *target = NULL;
  target = glnx_readlinkat_malloc (-1, lib_path, NULL, NULL);

  g_return_if_fail (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK);

  if (target != NULL)
    {
      g_autofree gchar *dir = NULL;
      g_autofree gchar *lib_multiarch = NULL;
      g_autofree gchar *dir_in_provider = NULL;

      dir = g_path_get_dirname (target);

      lib_multiarch = g_build_filename ("/lib", arch->details->tuple, NULL);
      if (g_str_has_suffix (dir, lib_multiarch))
        dir[strlen (dir) - strlen (lib_multiarch)] = '\0';
      else if (g_str_has_suffix (dir, "/lib64"))
        dir[strlen (dir) - strlen ("/lib64")] = '\0';
      else if (g_str_has_suffix (dir, "/lib32"))
        dir[strlen (dir) - strlen ("/lib32")] = '\0';
      else if (g_str_has_suffix (dir, "/lib"))
        dir[strlen (dir) - strlen ("/lib")] = '\0';

      if (g_str_has_prefix (dir, provider_in_container_namespace_guarded))
        memmove (dir,
                 dir + strlen (self->provider_in_container_namespace),
                 strlen (dir) - strlen (self->provider_in_container_namespace) + 1);

      dir_in_provider = g_build_filename (dir, "share", dir_basename, NULL);

      if (_srt_file_test_in_sysroot (self->provider_in_current_namespace,
                                     -1,
                                     dir_in_provider,
                                     G_FILE_TEST_IS_DIR))
        {
          g_hash_table_add (data_in_provider,
                            g_steal_pointer (&dir_in_provider));
        }
      else
        {
          g_info ("We were expecting the %s directory in the provider to "
                  "be located in \"%s/share/%s\", but instead it is "
                  "missing", dir_basename, dir, dir_basename);
        }
    }
}

static gboolean
pv_runtime_finish_lib_data (PvRuntime *self,
                            FlatpakBwrap *bwrap,
                            const gchar *dir_basename,
                            const gchar *lib_name,
                            gboolean all_from_provider,
                            GHashTable *data_in_provider,
                            GError **error)
{
  g_autofree gchar *best_data_in_provider = NULL;
  g_autofree gchar *canonical_path = NULL;

  g_return_val_if_fail (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK, FALSE);
  g_return_val_if_fail (bwrap != NULL || self->mutable_sysroot != NULL, FALSE);
  g_return_val_if_fail (dir_basename != NULL, FALSE);

  canonical_path = g_build_filename ("/usr", "share", dir_basename, NULL);

  if (g_hash_table_size (data_in_provider) > 0 && !all_from_provider)
    {
      /* See the explanation in the similar
       * "any_libc_from_provider && !all_libc_from_provider" case, above */
      g_warning ("Using %s from provider system for some but not all "
                 "architectures! Will take /usr/share/%s from provider.",
                 lib_name, dir_basename);
    }

  if (g_hash_table_size (data_in_provider) == 1)
    {
      best_data_in_provider = g_strdup (
        pv_hash_table_get_arbitrary_key (data_in_provider));
    }
  else if (g_hash_table_size (data_in_provider) > 1)
    {
      g_warning ("Found more than one possible %s data directory from provider",
                 dir_basename);
      /* Prioritize "/usr/share/{dir_basename}" if available. Otherwise randomly pick
       * the first directory in the hash table */
      if (g_hash_table_contains (data_in_provider, canonical_path))
        best_data_in_provider = g_strdup (canonical_path);
      else
        best_data_in_provider = g_strdup (
          pv_hash_table_get_arbitrary_key (data_in_provider));
    }

  if (best_data_in_provider != NULL)
    {
      return pv_runtime_take_from_provider (self, bwrap,
                                            best_data_in_provider,
                                            canonical_path,
                                            TAKE_FROM_PROVIDER_FLAGS_IF_CONTAINER_COMPATIBLE,
                                            error);
    }
  else
    {
      return TRUE;
    }
}

static gboolean
pv_runtime_finish_libc_family (PvRuntime *self,
                               FlatpakBwrap *bwrap,
                               GHashTable *gconv_in_provider,
                               GError **error)
{
  g_autofree gchar *localedef = NULL;
  g_autofree gchar *ldconfig = NULL;
  g_autofree gchar *locale = NULL;
  GHashTableIter iter;
  const gchar *gconv_path;

  g_return_val_if_fail (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK, FALSE);
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

      if (!pv_runtime_take_from_provider (self, bwrap,
                                          "/usr/lib/locale",
                                          "/usr/lib/locale",
                                          TAKE_FROM_PROVIDER_FLAGS_IF_EXISTS,
                                          error))
        return FALSE;

      if (!pv_runtime_take_from_provider (self, bwrap,
                                          "/usr/share/i18n",
                                          "/usr/share/i18n",
                                          TAKE_FROM_PROVIDER_FLAGS_IF_EXISTS,
                                          error))
        return FALSE;

      localedef = pv_runtime_search_in_path_and_bin (self, "localedef");

      if (localedef == NULL)
        {
          g_warning ("Cannot find localedef");
        }
      else if (!pv_runtime_take_from_provider (self, bwrap, localedef,
                                               "/usr/bin/localedef",
                                               TAKE_FROM_PROVIDER_FLAGS_IF_CONTAINER_COMPATIBLE,
                                               error))
        {
          return FALSE;
        }

      locale = pv_runtime_search_in_path_and_bin (self, "locale");

      if (locale == NULL)
        {
          g_warning ("Cannot find locale");
        }
      else if (!pv_runtime_take_from_provider (self, bwrap, locale,
                                               "/usr/bin/locale",
                                               TAKE_FROM_PROVIDER_FLAGS_IF_CONTAINER_COMPATIBLE,
                                               error))
        {
          return FALSE;
        }

      ldconfig = pv_runtime_search_in_path_and_bin (self, "ldconfig");

      if (ldconfig == NULL)
        {
          g_warning ("Cannot find ldconfig");
        }
      else if (!pv_runtime_take_from_provider (self, bwrap,
                                               ldconfig,
                                               "/sbin/ldconfig",
                                               TAKE_FROM_PROVIDER_FLAGS_NONE,
                                               error))
        {
          return FALSE;
        }

      g_debug ("Making provider gconv modules visible in container");

      g_hash_table_iter_init (&iter, gconv_in_provider);
      while (g_hash_table_iter_next (&iter, (gpointer *)&gconv_path, NULL))
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
pv_runtime_use_provider_graphics_stack (PvRuntime *self,
                                        FlatpakBwrap *bwrap,
                                        PvEnviron *container_env,
                                        GError **error)
{
  gsize i, j;
  g_autoptr(GString) dri_path = g_string_new ("");
  g_autoptr(GString) egl_path = g_string_new ("");
  g_autoptr(GString) vulkan_path = g_string_new ("");
  /* We are currently using the explicit and implicit Vulkan layer paths
   * only to check if we binded at least a single layer */
  g_autoptr(GString) vulkan_exp_layer_path = g_string_new ("");
  g_autoptr(GString) vulkan_imp_layer_path = g_string_new ("");
  g_autoptr(GString) va_api_path = g_string_new ("");
  gboolean any_architecture_works = FALSE;
  g_autoptr(SrtSystemInfo) system_info = srt_system_info_new (NULL);
  g_autoptr(SrtObjectList) egl_icds = NULL;
  g_autoptr(SrtObjectList) vulkan_icds = NULL;
  g_autoptr(SrtObjectList) vulkan_explicit_layers = NULL;
  g_autoptr(SrtObjectList) vulkan_implicit_layers = NULL;
  g_autoptr(GPtrArray) egl_icd_details = NULL;      /* (element-type IcdDetails) */
  g_autoptr(GPtrArray) vulkan_icd_details = NULL;   /* (element-type IcdDetails) */
  g_autoptr(GPtrArray) vulkan_exp_layer_details = NULL;   /* (element-type IcdDetails) */
  g_autoptr(GPtrArray) vulkan_imp_layer_details = NULL;   /* (element-type IcdDetails) */
  guint n_egl_icds;
  guint n_vulkan_icds;
  const GList *icd_iter;
  gboolean all_libglx_from_provider = TRUE;
  gboolean all_libdrm_from_provider = TRUE;
  g_autoptr(GHashTable) drirc_data_in_provider = g_hash_table_new_full (g_str_hash,
                                                                        g_str_equal,
                                                                        g_free, NULL);
  g_autoptr(GHashTable) libdrm_data_in_provider = g_hash_table_new_full (g_str_hash,
                                                                         g_str_equal,
                                                                         g_free, NULL);
  g_autoptr(GHashTable) gconv_in_provider = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                                     g_free, NULL);
  g_autofree gchar *provider_in_container_namespace_guarded = NULL;
  if (g_str_has_suffix (self->provider_in_container_namespace, "/"))
    provider_in_container_namespace_guarded =
      g_strdup (self->provider_in_container_namespace);
  else
    provider_in_container_namespace_guarded =
      g_strdup_printf ("%s/", self->provider_in_container_namespace);

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK, FALSE);
  g_return_val_if_fail (bwrap != NULL || self->mutable_sysroot != NULL, FALSE);
  g_return_val_if_fail (bwrap == NULL || !pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (container_env != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (!pv_runtime_provide_container_access (self, error))
    return FALSE;

  srt_system_info_set_sysroot (system_info, self->provider_in_current_namespace);

  g_debug ("Enumerating EGL ICDs on provider system...");
  egl_icds = srt_system_info_list_egl_icds (system_info, multiarch_tuples);
  n_egl_icds = g_list_length (egl_icds);

  egl_icd_details = g_ptr_array_new_full (n_egl_icds,
                                          (GDestroyNotify) G_CALLBACK (icd_details_free));

  for (icd_iter = egl_icds, j = 0;
       icd_iter != NULL;
       icd_iter = icd_iter->next, j++)
    {
      SrtEglIcd *icd = icd_iter->data;
      const gchar *path = srt_egl_icd_get_json_path (icd);
      GError *local_error = NULL;

      if (!srt_egl_icd_check_error (icd, &local_error))
        {
          g_info ("Failed to load EGL ICD #%" G_GSIZE_FORMAT  " from %s: %s",
                  j, path, local_error->message);
          g_clear_error (&local_error);
          continue;
        }

      g_info ("EGL ICD #%" G_GSIZE_FORMAT " at %s: %s",
              j, path, srt_egl_icd_get_library_path (icd));

      g_ptr_array_add (egl_icd_details, icd_details_new (icd));
    }

  g_debug ("Enumerating Vulkan ICDs on provider system...");
  vulkan_icds = srt_system_info_list_vulkan_icds (system_info,
                                                  multiarch_tuples);
  n_vulkan_icds = g_list_length (vulkan_icds);

  vulkan_icd_details = g_ptr_array_new_full (n_vulkan_icds,
                                             (GDestroyNotify) G_CALLBACK (icd_details_free));

  for (icd_iter = vulkan_icds, j = 0;
       icd_iter != NULL;
       icd_iter = icd_iter->next, j++)
    {
      SrtVulkanIcd *icd = icd_iter->data;
      const gchar *path = srt_vulkan_icd_get_json_path (icd);
      GError *local_error = NULL;

      if (!srt_vulkan_icd_check_error (icd, &local_error))
        {
          g_info ("Failed to load Vulkan ICD #%" G_GSIZE_FORMAT " from %s: %s",
                  j, path, local_error->message);
          g_clear_error (&local_error);
          continue;
        }

      g_info ("Vulkan ICD #%" G_GSIZE_FORMAT " at %s: %s",
              j, path, srt_vulkan_icd_get_library_path (icd));

      g_ptr_array_add (vulkan_icd_details, icd_details_new (icd));
    }

  if (self->flags & PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS)
    {
      g_debug ("Enumerating Vulkan explicit layers on provider system...");
      vulkan_explicit_layers = srt_system_info_list_explicit_vulkan_layers (system_info);

      vulkan_exp_layer_details = g_ptr_array_new_full (g_list_length (vulkan_explicit_layers),
                                                       (GDestroyNotify) G_CALLBACK (icd_details_free));

      for (icd_iter = vulkan_explicit_layers, j = 0;
          icd_iter != NULL;
          icd_iter = icd_iter->next, j++)
        {
          SrtVulkanLayer *layer = icd_iter->data;
          const gchar *path = srt_vulkan_layer_get_json_path (layer);
          GError *local_error = NULL;

          if (!srt_vulkan_layer_check_error (layer, &local_error))
            {
              g_info ("Failed to load Vulkan explicit layer #%" G_GSIZE_FORMAT
                      " from %s: %s", j, path, local_error->message);
              g_clear_error (&local_error);
              continue;
            }

          g_info ("Vulkan explicit layer #%" G_GSIZE_FORMAT " at %s: %s",
                  j, path, srt_vulkan_layer_get_library_path (layer));

          g_ptr_array_add (vulkan_exp_layer_details, icd_details_new (layer));
        }

      g_debug ("Enumerating Vulkan implicit layers on provider system...");
      vulkan_implicit_layers = srt_system_info_list_implicit_vulkan_layers (system_info);

      vulkan_imp_layer_details = g_ptr_array_new_full (g_list_length (vulkan_implicit_layers),
                                                       (GDestroyNotify) G_CALLBACK (icd_details_free));

      for (icd_iter = vulkan_implicit_layers, j = 0;
          icd_iter != NULL;
          icd_iter = icd_iter->next, j++)
        {
          SrtVulkanLayer *layer = icd_iter->data;
          const gchar *path = srt_vulkan_layer_get_json_path (layer);
          const gchar *library_path = NULL;
          GError *local_error = NULL;

          if (!srt_vulkan_layer_check_error (layer, &local_error))
            {
              g_info ("Failed to load Vulkan implicit layer #%" G_GSIZE_FORMAT
                      " from %s: %s", j, path, local_error->message);
              g_clear_error (&local_error);
              continue;
            }

          library_path = srt_vulkan_layer_get_library_path (layer);

          g_info ("Vulkan implicit layer #%" G_GSIZE_FORMAT " at %s: %s",
                  j, path, library_path != NULL ? library_path : "meta-layer");

          g_ptr_array_add (vulkan_imp_layer_details, icd_details_new (layer));
        }
    }

  /* We set this FALSE later if we decide not to use the provider libc
   * for some architecture. */
  self->all_libc_from_provider = TRUE;

  g_assert (multiarch_tuples[G_N_ELEMENTS (multiarch_tuples) - 1] == NULL);

  for (i = 0; i < G_N_ELEMENTS (multiarch_tuples) - 1; i++)
    {
      g_auto (RuntimeArchitecture) arch_on_stack = { i };
      RuntimeArchitecture *arch = &arch_on_stack;

      g_debug ("Checking for %s libraries...", multiarch_tuples[i]);

      if (runtime_architecture_init (arch, self))
        {
          g_autoptr(GPtrArray) dirs = NULL;
          g_autofree gchar *this_dri_path_in_container = g_build_filename (arch->libdir_in_container,
                                                                           "dri", NULL);
          g_autofree gchar *libc = NULL;
          /* Can either be relative to the sysroot, or absolute */
          g_autofree gchar *ld_so_in_runtime = NULL;
          g_autofree gchar *libdrm = NULL;
          g_autofree gchar *libglx_mesa = NULL;
          g_autoptr(SrtObjectList) dri_drivers = NULL;
          g_autoptr(SrtObjectList) vdpau_drivers = NULL;
          g_autoptr(SrtObjectList) va_api_drivers = NULL;
          gboolean use_numbered_subdirs;
          gboolean use_subdir_for_kind_soname;
          g_autoptr(GPtrArray) patterns = NULL;

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

          g_debug ("Collecting graphics drivers from provider system...");

          collect_graphics_libraries_patterns (patterns);

          g_debug ("Collecting %s EGL drivers from host system...",
                   arch->details->tuple);
          /* As with Vulkan layers, the order of the manifests matters
           * but the order of the actual libraries does not. */
          use_numbered_subdirs = FALSE;
          /* If we have just a SONAME, we do not want to place the library
           * under a subdir, otherwise ld.so will not be able to find it */
          use_subdir_for_kind_soname = FALSE;

          for (j = 0; j < egl_icd_details->len; j++)
            {
              IcdDetails *details = g_ptr_array_index (egl_icd_details, j);
              SrtEglIcd *icd = SRT_EGL_ICD (details->icd);

              if (!srt_egl_icd_check_error (icd, NULL))
                continue;

              details->resolved_library = srt_egl_icd_resolve_library_path (icd);
              g_assert (details->resolved_library != NULL);

              if (!bind_icd (self, arch, j, "glvnd", details,
                             &use_numbered_subdirs, use_subdir_for_kind_soname,
                             patterns, NULL, error))
                return FALSE;
            }

          g_debug ("Collecting %s Vulkan drivers from host system...",
                   arch->details->tuple);
          /* As with Vulkan layers, the order of the manifests matters
           * but the order of the actual libraries does not. */
          use_numbered_subdirs = FALSE;
          /* If we have just a SONAME, we do not want to place the library
           * under a subdir, otherwise ld.so will not be able to find it */
          use_subdir_for_kind_soname = FALSE;

          for (j = 0; j < vulkan_icd_details->len; j++)
            {
              IcdDetails *details = g_ptr_array_index (vulkan_icd_details, j);
              SrtVulkanIcd *icd = SRT_VULKAN_ICD (details->icd);

              if (!srt_vulkan_icd_check_error (icd, NULL))
                continue;

              details->resolved_library = srt_vulkan_icd_resolve_library_path (icd);
              g_assert (details->resolved_library != NULL);

              if (!bind_icd (self, arch, j, "vulkan", details,
                             &use_numbered_subdirs, use_subdir_for_kind_soname,
                             patterns, NULL, error))
                return FALSE;
            }

          if (self->flags & PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS)
            {
              g_debug ("Collecting Vulkan explicit layers from host system...");
              if (!collect_vulkan_layers (self, vulkan_exp_layer_details,
                                          patterns, arch, "vulkan_exp_layer", error))
                return FALSE;

              g_debug ("Collecting Vulkan implicit layers from host system...");
              if (!collect_vulkan_layers (self, vulkan_imp_layer_details,
                                          patterns, arch, "vulkan_imp_layer", error))
                return FALSE;
            }

          g_debug ("Enumerating %s VDPAU ICDs on host system...", arch->details->tuple);
          vdpau_drivers = srt_system_info_list_vdpau_drivers (system_info,
                                                              arch->details->tuple,
                                                              SRT_DRIVER_FLAGS_NONE);
          /* The VDPAU loader looks up drivers by name, not by readdir(),
           * so order doesn't matter unless there are name collisions. */
          use_numbered_subdirs = FALSE;
          /* These libraries are always expected to be located under the
           * "vdpau" subdir */
          use_subdir_for_kind_soname = TRUE;

          for (icd_iter = vdpau_drivers, j = 0; icd_iter != NULL; icd_iter = icd_iter->next, j++)
            {
              g_autoptr(IcdDetails) details = icd_details_new (icd_iter->data);
              details->resolved_library = srt_vdpau_driver_resolve_library_path (details->icd);
              g_assert (details->resolved_library != NULL);
              g_assert (g_path_is_absolute (details->resolved_library));

              /* In practice we won't actually use the sequence number for VDPAU
               * because they can only be located in a single directory,
               * so by definition we can't have collisions. Anything that
               * ends up in a numbered subdirectory won't get used. */
              if (!bind_icd (self, arch, j, "vdpau", details,
                             &use_numbered_subdirs, use_subdir_for_kind_soname,
                             patterns, NULL, error))
                return FALSE;
            }

          g_debug ("Enumerating %s DRI drivers on host system...",
                   arch->details->tuple);
          dri_drivers = srt_system_info_list_dri_drivers (system_info,
                                                          arch->details->tuple,
                                                          SRT_DRIVER_FLAGS_NONE);
          /* The DRI loader looks up drivers by name, not by readdir(),
           * so order doesn't matter unless there are name collisions. */
          use_numbered_subdirs = FALSE;
          /* These libraries are always expected to be located under the
           * "dri" subdir */
          use_subdir_for_kind_soname = TRUE;

          for (icd_iter = dri_drivers, j = 0; icd_iter != NULL; icd_iter = icd_iter->next, j++)
            {
              g_autoptr(IcdDetails) details = icd_details_new (icd_iter->data);

              details->resolved_library = srt_dri_driver_resolve_library_path (details->icd);
              g_assert (details->resolved_library != NULL);
              g_assert (g_path_is_absolute (details->resolved_library));

              if (!bind_icd (self, arch, j, "dri", details,
                             &use_numbered_subdirs, use_subdir_for_kind_soname,
                             patterns, dri_path, error))
                return FALSE;
            }

          g_debug ("Enumerating %s VA-API drivers on host system...",
                   arch->details->tuple);
          va_api_drivers = srt_system_info_list_va_api_drivers (system_info,
                                                                arch->details->tuple,
                                                                SRT_DRIVER_FLAGS_NONE);
          /* The VA-API loader looks up drivers by name, not by readdir(),
           * so order doesn't matter unless there are name collisions. */
          use_numbered_subdirs = FALSE;
          /* These libraries are always expected to be located under the
           * "dri" subdir */
          use_subdir_for_kind_soname = TRUE;

          for (icd_iter = va_api_drivers, j = 0; icd_iter != NULL; icd_iter = icd_iter->next, j++)
            {
              g_autoptr(IcdDetails) details = icd_details_new (icd_iter->data);

              details->resolved_library = srt_va_api_driver_resolve_library_path (details->icd);
              g_assert (details->resolved_library != NULL);
              g_assert (g_path_is_absolute (details->resolved_library));

              if (!bind_icd (self, arch, j, "dri", details,
                             &use_numbered_subdirs, use_subdir_for_kind_soname,
                             patterns, va_api_path, error))
                return FALSE;
            }

          if (!pv_runtime_capture_libraries (self, arch,
                                             arch->libdir_in_current_namespace,
                                             patterns, error))
            return FALSE;

          libc = g_build_filename (arch->libdir_in_current_namespace, "libc.so.6", NULL);

          /* If we are going to use the provider's libc6 (likely)
           * then we have to use its ld.so too. */
          if (g_file_test (libc, G_FILE_TEST_IS_SYMLINK))
            {
              if (!pv_runtime_collect_libc_family (self, arch, bwrap,
                                                   libc, ld_so_in_runtime,
                                                   provider_in_container_namespace_guarded,
                                                   gconv_in_provider,
                                                   error))
                return FALSE;

              self->any_libc_from_provider = TRUE;
            }
          else
            {
              self->all_libc_from_provider = FALSE;
            }

          libdrm = g_build_filename (arch->libdir_in_current_namespace, "libdrm.so.2", NULL);

          /* If we have libdrm.so.2 in overrides we also want to mount
           * ${prefix}/share/libdrm from the host. ${prefix} is derived from
           * the absolute path of libdrm.so.2 */
          if (g_file_test (libdrm, G_FILE_TEST_IS_SYMLINK))
            {
              pv_runtime_collect_lib_data (self, arch, "libdrm", libdrm,
                                           provider_in_container_namespace_guarded,
                                           libdrm_data_in_provider);
            }
          else
            {
              /* For at least a single architecture, libdrm is newer in the container */
              all_libdrm_from_provider = FALSE;
            }

          libglx_mesa = g_build_filename (arch->libdir_in_current_namespace, "libGLX_mesa.so.0", NULL);

          /* If we have libGLX_mesa.so.0 in overrides we also want to mount
           * ${prefix}/share/drirc.d from the host. ${prefix} is derived from
           * the absolute path of libGLX_mesa.so.0 */
          if (g_file_test (libglx_mesa, G_FILE_TEST_IS_SYMLINK))
            {
              pv_runtime_collect_lib_data (self, arch, "drirc.d", libglx_mesa,
                                           provider_in_container_namespace_guarded,
                                           drirc_data_in_provider);
            }
          else
            {
              /* For at least a single architecture, libGLX_mesa is newer in the container */
              all_libglx_from_provider = FALSE;
            }

          dirs = multiarch_details_get_libdirs (arch->details,
                                                MULTIARCH_LIBDIRS_FLAGS_NONE);

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

          /* Make sure we do this last, so that we have really copied
           * everything from the host that we are going to */
          if (self->mutable_sysroot != NULL &&
              !pv_runtime_remove_overridden_libraries (self, arch, error))
            return FALSE;
        }
    }

  if (!any_architecture_works)
    {
      GString *archs = g_string_new ("");

      g_assert (multiarch_tuples[G_N_ELEMENTS (multiarch_tuples) - 1] == NULL);

      for (i = 0; i < G_N_ELEMENTS (multiarch_tuples) - 1; i++)
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

  if (!pv_runtime_finish_libc_family (self, bwrap, gconv_in_provider, error))
    return FALSE;

  if (!pv_runtime_finish_lib_data (self, bwrap, "libdrm", "libdrm.so.2",
                                   all_libdrm_from_provider,
                                   libdrm_data_in_provider, error))
    return FALSE;

  if (!pv_runtime_finish_lib_data (self, bwrap, "drirc.d", "libGLX_mesa.so.0",
                                   all_libglx_from_provider,
                                   drirc_data_in_provider, error))
    return FALSE;

  g_debug ("Setting up EGL ICD JSON...");

  if (!setup_each_json_manifest (self, bwrap, "glvnd/egl_vendor.d",
                                 egl_icd_details, egl_path, error))
    return FALSE;

  g_debug ("Setting up Vulkan ICD JSON...");
  if (!setup_each_json_manifest (self, bwrap, "vulkan/icd.d",
                                 vulkan_icd_details, vulkan_path, error))
    return FALSE;

  if (self->flags & PV_RUNTIME_FLAGS_IMPORT_VULKAN_LAYERS)
    {
      g_debug ("Setting up Vulkan explicit layer JSON...");
      if (!setup_each_json_manifest (self, bwrap, "vulkan/explicit_layer.d",
                                     vulkan_exp_layer_details,
                                     vulkan_exp_layer_path, error))
        return FALSE;

      g_debug ("Setting up Vulkan implicit layer JSON...");
      if (!setup_each_json_manifest (self, bwrap, "vulkan/implicit_layer.d",
                                     vulkan_imp_layer_details,
                                     vulkan_imp_layer_path, error))
        return FALSE;
    }

  if (dri_path->len != 0)
    pv_environ_lock_env (container_env, "LIBGL_DRIVERS_PATH", dri_path->str);
  else
    pv_environ_lock_env (container_env, "LIBGL_DRIVERS_PATH", NULL);

  if (egl_path->len != 0)
    pv_environ_lock_env (container_env, "__EGL_VENDOR_LIBRARY_FILENAMES",
                         egl_path->str);
  else
    pv_environ_lock_env (container_env, "__EGL_VENDOR_LIBRARY_FILENAMES",
                         NULL);

  pv_environ_lock_env (container_env, "__EGL_VENDOR_LIBRARY_DIRS", NULL);

  if (vulkan_path->len != 0)
    pv_environ_lock_env (container_env, "VK_ICD_FILENAMES", vulkan_path->str);
  else
    pv_environ_lock_env (container_env, "VK_ICD_FILENAMES", NULL);

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

          pv_environ_lock_env (container_env, "XDG_DATA_DIRS",
                               prepended_data_dirs);
        }
      pv_environ_lock_env (container_env, "VK_LAYER_PATH", NULL);
    }

  if (va_api_path->len != 0)
    pv_environ_lock_env (container_env, "LIBVA_DRIVERS_PATH",
                         va_api_path->str);
  else
    pv_environ_lock_env (container_env, "LIBVA_DRIVERS_PATH", NULL);

  /* We binded the VDPAU drivers in "%{libdir}/vdpau".
   * Unfortunately VDPAU_DRIVER_PATH can hold just a single path, so we can't
   * easily list both x86_64 and i386 drivers path.
   * As a workaround we set VDPAU_DRIVER_PATH to
   * "/overrides/lib/platform-${PLATFORM}/vdpau" (which is a symlink that we
   * already created). */
  g_autofree gchar *vdpau_val = g_strdup_printf ("%s/lib/platform-${PLATFORM}/vdpau",
                                                 self->overrides_in_container);

  pv_environ_lock_env (container_env, "VDPAU_DRIVER_PATH", vdpau_val);
  return TRUE;
}

gboolean
pv_runtime_bind (PvRuntime *self,
                 FlatpakExports *exports,
                 FlatpakBwrap *bwrap,
                 PvEnviron *container_env,
                 GError **error)
{
  g_autofree gchar *pressure_vessel_prefix = NULL;

  g_return_val_if_fail (PV_IS_RUNTIME (self), FALSE);
  g_return_val_if_fail ((exports == NULL) == (bwrap == NULL), FALSE);
  g_return_val_if_fail (bwrap == NULL || !pv_bwrap_was_finished (bwrap), FALSE);
  g_return_val_if_fail (bwrap != NULL || self->mutable_sysroot != NULL, FALSE);
  g_return_val_if_fail (container_env != NULL, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (bwrap != NULL
      && !bind_runtime_base (self, bwrap, container_env, error))
    return FALSE;

  if (self->flags & PV_RUNTIME_FLAGS_PROVIDER_GRAPHICS_STACK)
    {
      if (!pv_runtime_use_provider_graphics_stack (self, bwrap,
                                                   container_env,
                                                   error))
        return FALSE;
    }

  if (bwrap != NULL)
    bind_runtime_finish (self, exports, bwrap);

  pressure_vessel_prefix = g_path_get_dirname (self->tools_dir);

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

      if (!pv_cheap_tree_copy (pressure_vessel_prefix, dest,
                               PV_COPY_FLAGS_NONE, error))
        return FALSE;

      if (bwrap != NULL)
        flatpak_bwrap_add_args (bwrap,
                                "--symlink",
                                "/usr/lib/pressure-vessel/from-host",
                                "/run/pressure-vessel/pv-from-host",
                                NULL);

      self->adverb_in_container = "/usr/lib/pressure-vessel/from-host/bin/pressure-vessel-adverb";
    }
  else
    {
      g_autofree gchar *pressure_vessel_prefix_in_host_namespace =
        pv_current_namespace_path_to_host_path (pressure_vessel_prefix);

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
                                                               multiarch_tuples,
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
                flatpak_exports_add_path_tmpfs (exports, dir);
            }
        }
    }

  /* Some games detect that they have been run outside the Steam Runtime
   * and try to re-run themselves via Steam. Trick them into thinking
   * they are in the LD_LIBRARY_PATH Steam Runtime.
   *
   * We do not do this for games developed against soldier, because
   * backwards compatibility is not a concern for game developers who
   * have specifically opted-in to using the newer runtime. */
  if (self->is_scout)
    pv_environ_lock_env (container_env, "STEAM_RUNTIME", "/");

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

  /* TODO: Adapt the use_ld_so_cache code from Flatpak instead
   * of setting LD_LIBRARY_PATH, for better robustness against
   * games that set their own LD_LIBRARY_PATH ignoring what they
   * got from the environment */
  g_assert (multiarch_tuples[G_N_ELEMENTS (multiarch_tuples) - 1] == NULL);

  for (i = 0; i < G_N_ELEMENTS (multiarch_tuples) - 1; i++)
    {
      g_autofree gchar *ld_path = NULL;

      ld_path = g_build_filename (self->overrides_in_container, "lib",
                                  multiarch_tuples[i], NULL);

      pv_search_path_append (ld_library_path, ld_path);
    }

  /* If the runtime is Debian-based, make sure we search where ncurses-base
   * puts terminfo, even if we're using a non-Debian-based libtinfo.so.6. */
  terminfo_path = g_build_filename (self->source_files, "lib", "terminfo",
                                    NULL);

  if (g_file_test (terminfo_path, G_FILE_TEST_IS_DIR))
    pv_environ_lock_env (container_env, "TERMINFO_DIRS", "/lib/terminfo");

  /* The PATH from outside the container doesn't really make sense inside the
   * container: in principle the layout could be totally different. */
  pv_environ_lock_env (container_env, "PATH", "/usr/bin:/bin");
  pv_environ_lock_env (container_env, "LD_LIBRARY_PATH", ld_library_path->str);
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
