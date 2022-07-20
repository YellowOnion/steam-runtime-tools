/*<private_header>*/
/*
 * Copyright © 2022 Collabora Ltd.
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

#include <glib-object.h>

typedef struct _SrtPtyBridge SrtPtyBridge;
typedef struct _SrtPtyBridgeClass SrtPtyBridgeClass;

#define SRT_TYPE_PTY_BRIDGE _srt_pty_bridge_get_type ()
#define SRT_PTY_BRIDGE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_PTY_BRIDGE, SrtPtyBridge))
#define SRT_PTY_BRIDGE_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_PTY_BRIDGE, SrtPtyBridgeClass))
#define SRT_IS_PTY_BRIDGE(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_PTY_BRIDGE))
#define SRT_IS_PTY_BRIDGE_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_PTY_BRIDGE))
#define SRT_PTY_BRIDGE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_PTY_BRIDGE, SrtPtyBridgeClass)

GType _srt_pty_bridge_get_type (void);
SrtPtyBridge *_srt_pty_bridge_new (int input_source_fd,
                                   int output_dest_fd,
                                   GError **error);

int _srt_pty_bridge_get_terminal_fd (SrtPtyBridge *self);
gboolean _srt_pty_bridge_handle_signal (SrtPtyBridge *self,
                                        int sig,
                                        gboolean *handled,
                                        GError **error);
void _srt_pty_bridge_close_terminal_fd (SrtPtyBridge *self);
gboolean _srt_pty_bridge_is_active (SrtPtyBridge *self);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtPtyBridge, g_object_unref)
#endif
