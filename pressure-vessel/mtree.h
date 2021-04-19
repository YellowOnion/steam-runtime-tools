/*
 * Copyright © 2021 Collabora Ltd.
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

#include "steam-runtime-tools/glib-backports-internal.h"
#include "libglnx/libglnx.h"

typedef enum
{
  PV_MTREE_APPLY_FLAGS_GZIP = (1 << 0),
  PV_MTREE_APPLY_FLAGS_NONE = 0
} PvMtreeApplyFlags;

typedef enum
{
  PV_MTREE_ENTRY_KIND_UNKNOWN = '\0',
  PV_MTREE_ENTRY_KIND_BLOCK = 'b',
  PV_MTREE_ENTRY_KIND_CHAR = 'c',
  PV_MTREE_ENTRY_KIND_DIR = 'd',
  PV_MTREE_ENTRY_KIND_FIFO = 'p',
  PV_MTREE_ENTRY_KIND_FILE = '-',
  PV_MTREE_ENTRY_KIND_LINK = 'l',
  PV_MTREE_ENTRY_KIND_SOCKET = 's',
} PvMtreeEntryKind;

typedef struct _PvMtreeEntry PvMtreeEntry;

struct _PvMtreeEntry
{
  gchar *name;
  gchar *contents;
  gchar *link;
  gchar *sha256;
  goffset size;
  GTimeSpan mtime_usec;
  int mode;
  PvMtreeEntryKind kind;
};

#define PV_MTREE_ENTRY_BLANK \
{ \
  .size = -1, \
  .mtime_usec = -1, \
  .mode = -1, \
}

gboolean pv_mtree_entry_parse (const char *line,
                               PvMtreeEntry *entry,
                               const char *filename,
                               guint line_number,
                               GError **error);

void pv_mtree_entry_clear (PvMtreeEntry *entry);
G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC (PvMtreeEntry, pv_mtree_entry_clear)

gboolean pv_mtree_apply (const char *mtree,
                         const char *sysroot,
                         int sysroot_fd,
                         const char *source_files,
                         PvMtreeApplyFlags flags,
                         GError **error);
