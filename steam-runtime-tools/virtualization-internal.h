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

#include "steam-runtime-tools/virtualization.h"

/*
 * _srt_virtualization_info_new:
 * @host_machine: Machine that emulation is running on
 * @interpreter_root: (nullable) (type filename): Sysroot with libraries
 *  for the emulated architecture
 * @type: Type of virtualization or emulation
 *
 * Inline convenience function to create a new SrtVirtualizationInfo.
 * This is not part of the public API.
 *
 * Returns: (transfer full): A new #SrtVirtualizationInfo
 */
static inline SrtVirtualizationInfo *_srt_virtualization_info_new (SrtMachineType host_machine,
                                                                   const gchar *interpreter_root,
                                                                   SrtVirtualizationType type);

/*
 * _srt_virtualization_info_new_empty:
 *
 * Inline convenience function to create a new empty SrtVirtualizationInfo.
 * This is not part of the public API.
 *
 * Returns: (transfer full): A new #SrtVirtualizationInfo
 */
static inline SrtVirtualizationInfo *_srt_virtualization_info_new_empty (void);

#ifndef __GTK_DOC_IGNORE__
static inline SrtVirtualizationInfo *
_srt_virtualization_info_new (SrtMachineType host_machine,
                              const gchar *interpreter_root,
                              SrtVirtualizationType type)
{
  return g_object_new (SRT_TYPE_VIRTUALIZATION_INFO,
                       "host-machine", host_machine,
                       "interpreter-root", interpreter_root,
                       "type", type,
                       NULL);
}

static inline SrtVirtualizationInfo *
_srt_virtualization_info_new_empty (void)
{
  return g_object_new (SRT_TYPE_VIRTUALIZATION_INFO, NULL);
}
#endif

SrtVirtualizationInfo *_srt_check_virtualization (GHashTable *mock_cpuid,
                                                  int sysroot_fd);
