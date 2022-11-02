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

#include <glib.h>

#include <steam-runtime-tools/macros.h>

typedef enum
{
  SRT_LOG_FLAGS_DEBUG = (1 << 0),
  SRT_LOG_FLAGS_INFO = (1 << 1),
  SRT_LOG_FLAGS_TIMESTAMP = (1 << 2),
  SRT_LOG_FLAGS_NONE = 0
} SrtLogFlags;

#define SRT_LOG_LEVEL_FAILURE (1 << G_LOG_LEVEL_USER_SHIFT)

#define _srt_log_failure(...) \
  g_log (G_LOG_DOMAIN, SRT_LOG_LEVEL_FAILURE, __VA_ARGS__)

void _srt_util_set_glib_log_handler (const char *extra_log_domain,
                                     SrtLogFlags flags);
void _srt_util_set_up_logging (const char *identifier);
