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

#if !defined(_SRT_IN_SINGLE_HEADER) && !defined(_SRT_COMPILATION)
#error "Do not include directly, use <steam-runtime-tools/steam-runtime-tools.h>"
#endif

#include <glib.h>
#include <glib-object.h>

typedef struct _SrtDesktopEntry SrtDesktopEntry;
typedef struct _SrtDesktopEntryClass SrtDesktopEntryClass;

#define SRT_TYPE_DESKTOP_ENTRY srt_desktop_entry_get_type ()
#define SRT_DESKTOP_ENTRY(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_DESKTOP_ENTRY, SrtDesktopEntry))
#define SRT_DESKTOP_ENTRY_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_DESKTOP_ENTRY, SrtDesktopEntryClass))
#define SRT_IS_DESKTOP_ENTRY(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_DESKTOP_ENTRY))
#define SRT_IS_DESKTOP_ENTRY_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_DESKTOP_ENTRY))
#define SRT_DESKTOP_ENTRY_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_DESKTOP_ENTRY, SrtDesktopEntryClass)
GType srt_desktop_entry_get_type (void);

const char *srt_desktop_entry_get_id (SrtDesktopEntry *self);
const char *srt_desktop_entry_get_commandline (SrtDesktopEntry *self);
const char *srt_desktop_entry_get_filename (SrtDesktopEntry *self);
gboolean srt_desktop_entry_is_default_handler (SrtDesktopEntry *self);
gboolean srt_desktop_entry_is_steam_handler (SrtDesktopEntry *self);
