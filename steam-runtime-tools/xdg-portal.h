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

#include <steam-runtime-tools/macros.h>

typedef struct _SrtXdgPortalInterface SrtXdgPortalInterface;
typedef struct _SrtXdgPortalInterfaceClass SrtXdgPortalInterfaceClass;

#define SRT_TYPE_XDG_PORTAL_INTERFACE srt_xdg_portal_interface_get_type ()
#define SRT_XDG_PORTAL_INTERFACE(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_XDG_PORTAL_INTERFACE, SrtXdgPortalInterface))
#define SRT_XDG_PORTAL_INTERFACE_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_XDG_PORTAL_INTERFACE, SrtXdgPortalInterfaceClass))
#define SRT_IS_XDG_PORTAL_INTERFACE(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_XDG_PORTAL_INTERFACE))
#define SRT_IS_XDG_PORTAL_INTERFACE_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_XDG_PORTAL_INTERFACE))
#define SRT_XDG_PORTAL_INTERFACE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_XDG_PORTAL_INTERFACE, SrtXdgPortalInterfaceClass)

_SRT_PUBLIC
GType srt_xdg_portal_interface_get_type (void);

typedef struct _SrtXdgPortalBackend SrtXdgPortalBackend;
typedef struct _SrtXdgPortalBackendClass SrtXdgPortalBackendClass;

#define SRT_TYPE_XDG_PORTAL_BACKEND srt_xdg_portal_backend_get_type ()
#define SRT_XDG_PORTAL_BACKEND(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_XDG_PORTAL_BACKEND, SrtXdgPortalBackend))
#define SRT_XDG_PORTAL_BACKEND_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_XDG_PORTAL_BACKEND, SrtXdgPortalBackendClass))
#define SRT_IS_XDG_PORTAL_BACKEND(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_XDG_PORTAL_BACKEND))
#define SRT_IS_XDG_PORTAL_BACKEND_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_XDG_PORTAL_BACKEND))
#define SRT_XDG_PORTAL_BACKEND_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_XDG_PORTAL_BACKEND, SrtXdgPortalBackendClass)

_SRT_PUBLIC
GType srt_xdg_portal_backend_get_type (void);

/**
 * SrtXdgPortalIssues:
 * @SRT_XDG_PORTAL_ISSUES_NONE: There are no problems
 * @SRT_XDG_PORTAL_ISSUES_UNKNOWN: A generic internal error occurred while
 *  trying to check the XDG portals support, or, while reading a report,
 *  either an unknown issue flag was encountered or the xdg portal issues
 *  field was missing
 * @SRT_XDG_PORTAL_ISSUES_TIMEOUT: The check for the XDG portals support
 *  took too long to run and was terminated. This is likely to indicate
 *  that there are issues that caused the process to hang.
 * @SRT_XDG_PORTAL_ISSUES_MISSING_INTERFACE: A certain required XDG portal
 *  interface is missing.
 * @SRT_XDG_PORTAL_ISSUES_NO_IMPLEMENTATION: There isn't a working XDG portal
 *  implementation.
 */
typedef enum
{
  SRT_XDG_PORTAL_ISSUES_NONE = 0,
  SRT_XDG_PORTAL_ISSUES_UNKNOWN = (1 << 0),
  SRT_XDG_PORTAL_ISSUES_TIMEOUT = (1 << 1),
  SRT_XDG_PORTAL_ISSUES_MISSING_INTERFACE = (1 << 2),
  SRT_XDG_PORTAL_ISSUES_NO_IMPLEMENTATION = (1 << 3),
} SrtXdgPortalIssues;

_SRT_PUBLIC
const char *srt_xdg_portal_backend_get_name (SrtXdgPortalBackend *self);
_SRT_PUBLIC
gboolean srt_xdg_portal_backend_is_available (SrtXdgPortalBackend *self);

_SRT_PUBLIC
const char *srt_xdg_portal_interface_get_name (SrtXdgPortalInterface *self);
_SRT_PUBLIC
gboolean srt_xdg_portal_interface_is_available (SrtXdgPortalInterface *self);
_SRT_PUBLIC
guint32 srt_xdg_portal_interface_get_version (SrtXdgPortalInterface *self);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtXdgPortalBackend, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtXdgPortalInterface, g_object_unref)
#endif
