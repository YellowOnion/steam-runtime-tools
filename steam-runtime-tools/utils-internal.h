/*<private_header>*/
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

#pragma once

#include <dirent.h>
#include <fcntl.h>
#include <gelf.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib.h>

#include <steam-runtime-tools/macros.h>
#include <steam-runtime-tools/glib-backports-internal.h>

typedef enum
{
  SRT_HELPER_FLAGS_SEARCH_PATH = (1 << 0),
  SRT_HELPER_FLAGS_TIME_OUT = (1 << 1),
  SRT_HELPER_FLAGS_TIME_OUT_SOONER = (1 << 2),
  SRT_HELPER_FLAGS_NONE = 0
} SrtHelperFlags;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(Elf, elf_end);

G_GNUC_INTERNAL gboolean _srt_check_not_setuid (void);

G_GNUC_INTERNAL GPtrArray *_srt_get_helper (const char *helpers_path,
                                            const char *multiarch,
                                            const char *base,
                                            SrtHelperFlags flags,
                                            GError **error);
G_GNUC_INTERNAL gchar *_srt_filter_gameoverlayrenderer (const gchar *input);
G_GNUC_INTERNAL gchar **_srt_filter_gameoverlayrenderer_from_envp (gchar **envp);
G_GNUC_INTERNAL const char *_srt_find_myself (const char **helpers_path_out,
                                              GError **error);
G_GNUC_INTERNAL gchar *_srt_find_executable_dir (GError **error);

G_GNUC_INTERNAL gboolean _srt_process_timeout_wait_status (int wait_status,
                                                           int *exit_status,
                                                           int *terminating_signal);

_SRT_PRIVATE_EXPORT
const char *srt_enum_value_to_nick (GType enum_type,
                                    int value);

_SRT_PRIVATE_EXPORT
gboolean srt_enum_from_nick (GType enum_type,
                             const gchar *nick,
                             gint *value_out,
                             GError **error);

_SRT_PRIVATE_EXPORT
gboolean srt_add_flag_from_nick (GType flags_type,
                                 const gchar *string,
                                 guint *value_out,
                                 GError **error);

G_GNUC_INTERNAL void _srt_child_setup_unblock_signals (gpointer ignored);

_SRT_PRIVATE_EXPORT
void _srt_unblock_signals (void);

G_GNUC_INTERNAL int _srt_indirect_strcmp0 (gconstpointer left,
                                           gconstpointer right);

/*
 * Same as g_strcmp0(), but with the signature of a #GCompareFunc
 */
#define _srt_generic_strcmp0 ((GCompareFunc) (GCallback) g_strcmp0)

_SRT_PRIVATE_EXPORT
gboolean _srt_rm_rf (const char *directory);

_SRT_PRIVATE_EXPORT
FILE *_srt_divert_stdout_to_stderr (GError **error);

G_GNUC_INTERNAL
gboolean _srt_file_get_contents_in_sysroot (int sysroot_fd,
                                            const char *path,
                                            gchar **contents,
                                            gsize *len,
                                            GError **error);

G_GNUC_INTERNAL
gboolean _srt_file_test_in_sysroot (const char *sysroot,
                                    int sysroot_fd,
                                    const char *filename,
                                    GFileTest test);

G_GNUC_INTERNAL const char * const *_srt_peek_environ_nonnull (void);

G_GNUC_INTERNAL void _srt_setenv_disable_gio_modules (void);

G_GNUC_INTERNAL gboolean _srt_str_is_integer (const char *str);

gboolean _srt_fstatat_is_same_file (int afd, const char *a,
                                    int bfd, const char *b);
guint _srt_struct_stat_devino_hash (gconstpointer p);
gboolean _srt_struct_stat_devino_equal (gconstpointer p1,
                                        gconstpointer p2);

G_GNUC_INTERNAL gboolean _srt_steam_command_via_pipe (const char * const *arguments,
                                                      gssize n_arguments,
                                                      GError **error);

G_GNUC_INTERNAL gchar **_srt_recursive_list_content (const gchar *sysroot,
                                                     int sysroot_fd,
                                                     const gchar *directory,
                                                     gchar **envp,
                                                     gchar ***messages_out);

G_GNUC_INTERNAL const char *_srt_get_path_after (const char *str,
                                                 const char *prefix);

/*
 * _srt_is_same_stat:
 * @a: a stat buffer
 * @b: a stat buffer
 *
 * Returns: %TRUE if a and b identify the same inode
 */
 __attribute__((nonnull)) static inline gboolean
_srt_is_same_stat (const struct stat *a,
                   const struct stat *b)
{
  return (a->st_dev == b->st_dev && a->st_ino == b->st_ino);
}

/*
 * _srt_is_same_file:
 * @a: a path
 * @b: a path
 *
 * Returns: %TRUE if a and b are names for the same inode.
 */
static inline gboolean
_srt_is_same_file (const gchar *a,
                   const gchar *b)
{
  return _srt_fstatat_is_same_file (AT_FDCWD, a,
                                    AT_FDCWD, b);
}

int _srt_set_compatible_resource_limits (pid_t pid);

gboolean _srt_boolean_environment (const gchar *name,
                                   gboolean def);

static inline gboolean
_srt_all_bits_set (unsigned int flags,
                   unsigned int bits)
{
  return (flags == (flags | bits));
}

void _srt_async_signal_safe_error (const char *message,
                                   int exit_status) G_GNUC_NORETURN;

void _srt_get_current_dirs (gchar **cwd_p,
                            gchar **cwd_l);

gchar *_srt_get_random_uuid (GError **error);

const char *_srt_get_steam_app_id (void);

gboolean _srt_fd_set_close_on_exec (int fd,
                                    gboolean close_on_exec,
                                    GError **error);

gboolean _srt_open_elf (int dfd,
                        const gchar *file_path,
                        int *fd,
                        Elf **elf,
                        GError **error);

/*
 * SrtHashTableIter:
 * @real_iter: The underlying iterator
 *
 * Similar to #GHashTableIter, but optionally sorts the keys in a
 * user-specified order (which is implemented by caching a sorted list
 * of keys the first time _str_hash_table_iter_next() is called).
 *
 * Unlike #GHashTableIter, this data structure allocates resources, which
 * must be cleared after use by using _srt_hash_table_iter_clear() or
 * automatically by using
 * `g_auto(SrtHashTableIter) = SRT_HASH_TABLE_ITER_CLEARED`.
 */
typedef struct
{
  /*< public >*/
  GHashTableIter real_iter;
  /*< private >*/
  GHashTable *table;
  gpointer *sorted_keys;
  guint sorted_n;
  guint sorted_next;
} SrtHashTableIter;

/*
 * SRT_HASH_TABLE_ITER_CLEARED:
 *
 * Constant initializer to set a #SrtHashTableIter to a state from which
 * it can be cleared or initialized, but no other actions.
 */
#define SRT_HASH_TABLE_ITER_CLEARED { {}, NULL, NULL, 0, 0 }

void _srt_hash_table_iter_init (SrtHashTableIter *iter,
                                GHashTable *table);
void _srt_hash_table_iter_init_sorted (SrtHashTableIter *iter,
                                       GHashTable *table,
                                       GCompareFunc cmp);
gboolean _srt_hash_table_iter_next (SrtHashTableIter *iter,
                                    gpointer key_p,
                                    gpointer value_p);

/*
 * _srt_hash_table_iter_clear:
 * @iter: An iterator
 *
 * Free memory used to cache the sorted keys of @iter, if any.
 *
 * Unlike the rest of the #SrtHashTableIter interface, it is valid to call
 * this function on a #SrtHashTableIter that has already been cleared, or
 * was initialized to %SRT_HASH_TABLE_ITER_CLEARED and never subsequently
 * used.
 */
static inline void
_srt_hash_table_iter_clear (SrtHashTableIter *iter)
{
  SrtHashTableIter zero = SRT_HASH_TABLE_ITER_CLEARED;

  g_free (iter->sorted_keys);
  g_clear_pointer (&iter->table, g_hash_table_unref);
  *iter = zero;
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(SrtHashTableIter, _srt_hash_table_iter_clear)

typedef enum
{
  SRT_DIR_ITER_FLAGS_ENSURE_DTYPE = (1 << 0),
  SRT_DIR_ITER_FLAGS_FOLLOW = (1 << 1),
  SRT_DIR_ITER_FLAGS_SORTED = (1 << 2),
  SRT_DIR_ITER_FLAGS_NONE = 0
} SrtDirIterFlags;

/*
 * SrtDirentCompareFunc:
 *
 * Function to compare two `struct dirent` data structures, as used in
 * #SrtDirIter, `scandir` and `scandirat`.
 */
typedef int (*SrtDirentCompareFunc) (const struct dirent **,
                                     const struct dirent **);

/*
 * SrtDirIter:
 * @real_iter: The underlying iterator
 *
 * Similar to `GLnxDirFdIterator`, but optionally sorts the filenames in a
 * user-specified order (which is implemented by caching a sorted list
 * of filenames the first time _srt_dir_iter_next_dent() is called).
 *
 * Like `GLnxDirFdIterator`, this data structure allocates resources, which
 * must be cleared after use by using _srt_dir_iter_clear() or automatically
 * by using `g_auto(SrtDirIter) = SRT_DIR_ITER_CLEARED`.
 */
typedef struct
{
  /*< public >*/
  GLnxDirFdIterator real_iter;
  /*< private >*/
  SrtDirentCompareFunc cmp;
  GPtrArray *members;
  SrtDirIterFlags flags;
  gsize next_member;
} SrtDirIter;

int _srt_dirent_strcmp (const struct dirent **,
                        const struct dirent **);

/*
 * SRT_DIR_ITER_CLEARED:
 *
 * Constant initializer to set a #SrtDirIter to a state from which
 * it can be cleared or initialized, but no other actions.
 */
#define SRT_DIR_ITER_CLEARED { { .initialized = FALSE }, NULL, NULL, 0, 0 }

/*
 * _srt_dir_iter_init_at:
 * @self: (out caller-allocates): A directory iterator
 * @dfd: A directory fd, or AT_FDCWD, or -1
 * @path: A path relative to @dfd
 * @flags: Flags affecting iteration
 * @cmp: (nullable): If non-%NULL, sort the members of the directory
 *  using this function, typically _srt_dirent_strcmp() or `versionsort`
 * @error: Error indicator
 *
 * Start iterating over @path, relative to @dfd.
 *
 * If the #SrtDirIterFlags include %SRT_DIR_ITER_FLAGS_FOLLOW and the
 * last component of @path is a symlink, follow it.
 *
 * Other flags are stored in the iterator and used to modify the result
 * of _srt_dir_iter_next_dent().
 *
 * Returns: %FALSE on I/O error
 */
static inline gboolean
_srt_dir_iter_init_at (SrtDirIter *self,
                       int dfd,
                       const char *path,
                       SrtDirIterFlags flags,
                       SrtDirentCompareFunc cmp,
                       GError **error)
{
  SrtDirIter zero = SRT_DIR_ITER_CLEARED;
  gboolean follow = FALSE;

  *self = zero;
  self->flags = flags;
  self->cmp = cmp;

  if (flags & SRT_DIR_ITER_FLAGS_FOLLOW)
    follow = TRUE;

  if (!glnx_dirfd_iterator_init_at (dfd, path, follow, &self->real_iter, error))
    return FALSE;

  return TRUE;
}

/*
 * _srt_dir_iter_init_take_fd:
 * @self: (out caller-allocates): A directory iterator
 * @dfdp: (inout) (transfer full): A pointer to a directory fd, or AT_FDCWD, or -1
 * @flags: Flags affecting iteration
 * @error: Error indicator
 *
 * Start iterating over @dfdp.
 *
 * %SRT_DIR_ITER_FLAGS_FOLLOW is ignored if set.
 * Other flags are stored in the iterator and used to modify the result
 * of _srt_dir_iter_next_dent().
 *
 * Returns: %FALSE on I/O error
 */
static inline gboolean
_srt_dir_iter_init_take_fd (SrtDirIter *self,
                            int *dfdp,
                            SrtDirIterFlags flags,
                            SrtDirentCompareFunc cmp,
                            GError **error)
{
  SrtDirIter zero = SRT_DIR_ITER_CLEARED;

  *self = zero;
  self->flags = flags;
  self->cmp = cmp;

  if (!glnx_dirfd_iterator_init_take_fd (dfdp, &self->real_iter, error))
    return FALSE;

  return TRUE;
}

gboolean _srt_dir_iter_next_dent (SrtDirIter *self,
                                  struct dirent **out_dent,
                                  GCancellable *cancellable,
                                  GError **error);

/*
 * _srt_dir_iter_rewind:
 * @self: A directory iterator
 *
 * Return to the beginning of @self.
 */
static inline void
_srt_dir_iter_rewind (SrtDirIter *self)
{
  self->next_member = 0;
  g_clear_pointer (&self->members, g_ptr_array_unref);
  glnx_dirfd_iterator_rewind (&self->real_iter);
}

/*
 * _srt_dir_iter_clear:
 * @iter: An iterator
 *
 * Free resources used by the directory iterator.
 *
 * Unlike the rest of the #SrtDirIter interface, it is valid to call
 * this function on a #SrtDirIter that has already been cleared, or
 * was initialized to %SRT_DIR_ITER_CLEARED and never subsequently used.
 */
static inline void
_srt_dir_iter_clear (SrtDirIter *self)
{
  SrtDirIter zero = SRT_DIR_ITER_CLEARED;

  g_clear_pointer (&self->members, g_ptr_array_unref);
  glnx_dirfd_iterator_clear (&self->real_iter);
  *self = zero;
}

G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(SrtDirIter, _srt_dir_iter_clear)
