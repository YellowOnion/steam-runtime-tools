/*< internal_header >*/
/*
 * Copyright © 2020 Collabora Ltd.
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

#include "steam-runtime-tools/steam-runtime-tools.h"
#include "steam-runtime-tools/utils-internal.h"

/*
 * _srt_desktop_entry_new:
 * @id: (nullable)
 * @commandline: (nullable)
 * @filename: (nullable)
 *
 * Inline convenience function to create a new SrtDesktopEntry.
 * This is not part of the public API.
 *
 * Returns: (transfer full): A new #SrtDesktopEntry
 */
static inline SrtDesktopEntry *_srt_desktop_entry_new (const gchar *id,
                                                       const gchar *commandline,
                                                       const gchar *filename,
                                                       gboolean is_default_handler,
                                                       gboolean is_steam_handler);

#ifndef __GTK_DOC_IGNORE__

static inline SrtDesktopEntry *
_srt_desktop_entry_new (const gchar *id,
                        const gchar *commandline,
                        const gchar *filename,
                        gboolean is_default_handler,
                        gboolean is_steam_handler)
{
  return g_object_new (SRT_TYPE_DESKTOP_ENTRY,
                       "id", id,
                       "commandline", commandline,
                       "filename", filename,
                       "is-default-handler", is_default_handler,
                       "is-steam-handler", is_steam_handler,
                       NULL);
}

#endif

G_GNUC_INTERNAL
GList *_srt_list_steam_desktop_entries (void);
