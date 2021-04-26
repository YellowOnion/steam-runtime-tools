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
#include "steam-runtime-tools/container-internal.h"

#include <json-glib/json-glib.h>

#ifndef __GTK_DOC_IGNORE__
static inline SrtXdgPortalInterface *_srt_xdg_portal_interface_new (const char *name,
                                                                    gboolean is_available,
                                                                    guint version);

static inline SrtXdgPortalInterface *
_srt_xdg_portal_interface_new (const char *name,
                               gboolean is_available,
                               guint version)
{
  g_return_val_if_fail (name != NULL, NULL);

  return g_object_new (SRT_TYPE_XDG_PORTAL_INTERFACE,
                       "name", name,
                       "is-available", is_available,
                       "version", version,
                       NULL);
}

static inline SrtXdgPortalBackend *_srt_xdg_portal_backend_new (const char *name,
                                                                gboolean is_available);

static inline SrtXdgPortalBackend *
_srt_xdg_portal_backend_new (const char *name,
                             gboolean is_available)
{
  g_return_val_if_fail (name != NULL, NULL);

  return g_object_new (SRT_TYPE_XDG_PORTAL_BACKEND,
                       "name", name,
                       "is-available", is_available,
                       NULL);
}

typedef struct _SrtXdgPortal SrtXdgPortal;
typedef struct _SrtXdgPortalClass SrtXdgPortalClass;

#define SRT_TYPE_XDG_PORTAL srt_xdg_portal_get_type ()
#define SRT_XDG_PORTAL(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_XDG_PORTAL, SrtXdgPortal))
#define SRT_XDG_PORTAL_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_XDG_PORTAL, SrtXdgPortalClass))
#define SRT_IS_XDG_PORTAL(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_XDG_PORTAL))
#define SRT_IS_XDG_PORTAL_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_XDG_PORTAL))
#define SRT_XDG_PORTAL_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_XDG_PORTAL, SrtXdgPortalClass)

G_GNUC_INTERNAL
GType srt_xdg_portal_get_type (void);

/*
 * @backends: (transfer none) (element-type SrtXdgPortalBackend):
 * @interfaces: (transfer none) (element-type SrtXdgPortalInterface):
 */
static inline SrtXdgPortal *_srt_xdg_portal_new (const char *messages,
                                                 SrtXdgPortalIssues issues,
                                                 GPtrArray *backends,
                                                 GPtrArray *interfaces);

static inline SrtXdgPortal *
_srt_xdg_portal_new (const char *messages,
                     SrtXdgPortalIssues issues,
                     GPtrArray *backends,
                     GPtrArray *interfaces)
{
  return g_object_new (SRT_TYPE_XDG_PORTAL,
                       "messages", messages,
                       "issues", issues,
                       "backends", backends,
                       "interfaces", interfaces,
                       NULL);
}

G_GNUC_INTERNAL
SrtXdgPortalIssues _srt_check_xdg_portals (gchar **envp,
                                           const char *helpers_path,
                                           SrtTestFlags test_flags,
                                           SrtContainerType container_type,
                                           const char *multiarch_tuple,
                                           SrtXdgPortal **details_out);
#endif

G_GNUC_INTERNAL
const char *srt_xdg_portal_get_messages (SrtXdgPortal *self);
G_GNUC_INTERNAL
SrtXdgPortalIssues srt_xdg_portal_get_issues (SrtXdgPortal *self);
G_GNUC_INTERNAL
GList *srt_xdg_portal_get_backends (SrtXdgPortal *self);
G_GNUC_INTERNAL
GList *srt_xdg_portal_get_interfaces (SrtXdgPortal *self);

G_GNUC_INTERNAL
SrtXdgPortal *_srt_xdg_portal_get_info_from_report (JsonObject *json_obj);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtXdgPortal, g_object_unref)
#endif
