/*< internal_header >*/
/*
 * Copyright © 2021 Collabora Ltd.
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

/*
 * SrtCheckFlags:
 * @SRT_CHECK_FLAGS_NONE: Behave normally
 * @SRT_CHECK_FLAGS_SKIP_SLOW_CHECKS: Don't spend time detecting potential problems
 *
 * A bitfield with flags representing behaviour changes,
 * or %SRT_CHECK_FLAGS_NONE (which is numerically zero) for normal
 * behaviour.
 */
typedef enum
{
  SRT_CHECK_FLAGS_SKIP_SLOW_CHECKS = (1 << 0),
  SRT_CHECK_FLAGS_NONE = 0
} SrtCheckFlags;

G_GNUC_INTERNAL
void _srt_system_info_set_check_flags (SrtSystemInfo *self,
                                       SrtCheckFlags flags);
