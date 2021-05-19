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

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>

#include <glib.h>

#include <steam-runtime-tools/macros.h>

typedef enum
{
  SRT_HELPER_FLAGS_SEARCH_PATH = (1 << 0),
  SRT_HELPER_FLAGS_TIME_OUT = (1 << 1),
  SRT_HELPER_FLAGS_TIME_OUT_SOONER = (1 << 2),
  SRT_HELPER_FLAGS_NONE = 0
} SrtHelperFlags;

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

G_GNUC_INTERNAL gboolean _srt_steam_command_via_pipe (const char * const *arguments,
                                                      gssize n_arguments,
                                                      GError **error);

G_GNUC_INTERNAL gchar **_srt_recursive_list_content (const gchar *sysroot,
                                                     int sysroot_fd,
                                                     const gchar *directory,
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
