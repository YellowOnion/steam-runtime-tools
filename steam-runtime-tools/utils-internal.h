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

#include <glib.h>

typedef enum
{
  SRT_HELPER_FLAGS_SEARCH_PATH = (1 << 0),
  SRT_HELPER_FLAGS_TIME_OUT = (1 << 1),
  SRT_HELPER_FLAGS_TIME_OUT_SOONER = (1 << 2),
  SRT_HELPER_FLAGS_NONE = 0
} SrtHelperFlags;

G_GNUC_INTERNAL GPtrArray *_srt_get_helper (const char *helpers_path,
                                            const char *multiarch,
                                            const char *base,
                                            SrtHelperFlags flags,
                                            GError **error);
gchar *_srt_filter_gameoverlayrenderer (const gchar *input);
G_GNUC_INTERNAL const char *_srt_find_myself (const char **helpers_path_out,
                                              GError **error);

G_GNUC_INTERNAL gboolean _srt_process_timeout_wait_status (int wait_status,
                                                           int *exit_status,
                                                           int *terminating_signal);

const char *srt_enum_value_to_nick (GType enum_type,
                                    int value);

G_GNUC_INTERNAL void _srt_child_setup_unblock_signals (gpointer ignored);

/* not G_GNUC_INTERNAL because s-r-s-i calls it */
void _srt_unblock_signals (void);

G_GNUC_INTERNAL int _srt_indirect_strcmp0 (gconstpointer left,
                                           gconstpointer right);
