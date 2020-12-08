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

#if !defined(_SRT_IN_SINGLE_HEADER) && !defined(_SRT_COMPILATION)
#error "Do not include directly, use <steam-runtime-tools/steam-runtime-tools.h>"
#endif

#include <glib.h>
#include <glib-object.h>

#include <steam-runtime-tools/macros.h>

typedef struct _SrtSteam SrtSteam;
typedef struct _SrtSteamClass SrtSteamClass;

#define SRT_TYPE_STEAM srt_steam_get_type ()
#define SRT_STEAM(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_STEAM, SrtSteam))
#define SRT_STEAM_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_STEAM, SrtSteamClass))
#define SRT_IS_STEAM(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_STEAM))
#define SRT_IS_STEAM_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_STEAM))
#define SRT_STEAM_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_STEAM, SrtSteamClass)
_SRT_PUBLIC
GType srt_steam_get_type (void);

/* Backward compatibility with previous steam-runtime-tools naming */
#define SRT_STEAM_ISSUES_INTERNAL_ERROR SRT_STEAM_ISSUES_UNKNOWN

/**
 * SrtSteamIssues:
 * @SRT_STEAM_ISSUES_NONE: There are no problems
 * @SRT_STEAM_ISSUES_UNKNOWN: A generic internal error occurred while trying
 *  to detect the status of the Steam installation, or, while reading a
 *  report, either an unknown issue flag was encountered or the Steam issues
 *  field was missing
 * @SRT_STEAM_ISSUES_CANNOT_FIND: Unable to find the Steam installation,
 *  either via its canonical symlink `~/.steam/root` or various fallback
 *  methods. See srt_system_info_dup_steam_installation_path().
 * @SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_SYMLINK: `~/.steam/steam` is not
 *  a symbolic link to Steam data, which for example can happen
 *  if Steam was installed on a system with <https://bugs.debian.org/916303>.
 *  See srt_system_info_dup_steam_data_path().
 * @SRT_STEAM_ISSUES_CANNOT_FIND_DATA: Unable to find the Steam data,
 *  either via its canonical symlink `~/.steam/steam` or various fallback
 *  methods. Steam is unlikely to work in this situation.
 *  See srt_system_info_dup_steam_data_path().
 * @SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_DIRECTORY: `~/.steam/steam` is
 *  neither a directory nor a symbolic link to a directory.
 *  Steam is unlikely to work in this situation.
 *  See srt_system_info_dup_steam_data_path().
 * @SRT_STEAM_ISSUES_DOT_STEAM_ROOT_NOT_SYMLINK: `~/.steam/root` is not
 *  a symbolic link to the Steam installation.
 *  See srt_system_info_dup_steam_installation_path().
 * @SRT_STEAM_ISSUES_DOT_STEAM_ROOT_NOT_DIRECTORY: `~/.steam/root` is
 *  neither a directory nor a symbolic link to a directory.
 *  Steam is unlikely to work in this situation.
 *  See srt_system_info_dup_steam_installation_path().
 * @SRT_STEAM_ISSUES_STEAMSCRIPT_NOT_IN_ENVIRONMENT: The environment
 *  `STEAMSCRIPT` is not set. Probably it's safe to be considered a minor
 *  issue.
 * @SRT_STEAM_ISSUES_MISSING_STEAM_URI_HANDLER: There isn't a default desktop
 *  application that can handle `steam:` URIs.
 * @SRT_STEAM_ISSUES_UNEXPECTED_STEAM_URI_HANDLER: The default Steam URI
 *  handler executable is either not what we expected or is different from the
 *  one `STEAMSCRIPT` points to.
 * @SRT_STEAM_ISSUES_UNEXPECTED_STEAM_DESKTOP_ID: The default Steam desktop
 *  application ID is not what we expected.
 * @SRT_STEAM_ISSUES_UNEXPECTED_STEAM_COMPAT_CLIENT_INSTALL_PATH: If the
 *  environment `STEAM_COMPAT_CLIENT_INSTALL_PATH` is set, its realpath() is
 *  not the equivalent of `~/.steam/root`.
 * @SRT_STEAM_ISSUES_UNKNOWN: The Steam problems are not known
 *
 * A bitfield with flags representing problems with the Steam
 * installation, or %SRT_STEAM_ISSUES_NONE (which is numerically zero)
 * if no problems were detected.
 *
 * In general, more bits set means more problems.
 */
typedef enum
{
  SRT_STEAM_ISSUES_UNKNOWN = (1 << 0),
  SRT_STEAM_ISSUES_CANNOT_FIND = (1 << 1),
  SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_SYMLINK = (1 << 2),
  SRT_STEAM_ISSUES_CANNOT_FIND_DATA = (1 << 3),
  SRT_STEAM_ISSUES_DOT_STEAM_STEAM_NOT_DIRECTORY = (1 << 4),
  SRT_STEAM_ISSUES_DOT_STEAM_ROOT_NOT_SYMLINK = (1 << 5),
  SRT_STEAM_ISSUES_DOT_STEAM_ROOT_NOT_DIRECTORY = (1 << 6),
  SRT_STEAM_ISSUES_STEAMSCRIPT_NOT_IN_ENVIRONMENT = (1 << 7),
  SRT_STEAM_ISSUES_MISSING_STEAM_URI_HANDLER = (1 << 8),
  SRT_STEAM_ISSUES_UNEXPECTED_STEAM_URI_HANDLER = (1 << 9),
  SRT_STEAM_ISSUES_UNEXPECTED_STEAM_DESKTOP_ID = (1 << 10),
  SRT_STEAM_ISSUES_UNEXPECTED_STEAM_COMPAT_CLIENT_INSTALL_PATH = (1 << 11),
  SRT_STEAM_ISSUES_NONE = 0
} SrtSteamIssues;

_SRT_PUBLIC
SrtSteamIssues srt_steam_get_issues (SrtSteam *self);
_SRT_PUBLIC
const char *srt_steam_get_install_path (SrtSteam *self);
_SRT_PUBLIC
const char *srt_steam_get_data_path (SrtSteam *self);
_SRT_PUBLIC
const char *srt_steam_get_bin32_path (SrtSteam *self);
_SRT_PUBLIC
const char *srt_steam_get_steamscript_path (SrtSteam *self);
_SRT_PUBLIC
const char *srt_steam_get_steamscript_version (SrtSteam *self);

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtSteam, g_object_unref)
#endif
