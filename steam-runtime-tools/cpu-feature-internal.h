/*<private_header>*/
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

#include "steam-runtime-tools/cpu-feature.h"

#include <string.h>

#include <glib.h>
#include <glib-object.h>

/* All real features (not including UNKNOWN) */
#define _SRT_X86_FEATURE_ALL (SRT_X86_FEATURE_X86_64 \
                              | SRT_X86_FEATURE_SSE3 \
                              | SRT_X86_FEATURE_CMPXCHG16B)

#if defined(__x86_64__) || defined(__i386__)
typedef union
{
  char text[17];
  guint32 registers[4];
} SrtCpuidData;

static inline SrtCpuidData *
_srt_cpuid_data_new (guint32 eax,
                     guint32 ebx,
                     guint32 ecx,
                     guint32 edx)
{
  SrtCpuidData *ret = g_new0 (SrtCpuidData, 1);

  ret->registers[0] = eax;
  ret->registers[1] = ebx;
  ret->registers[2] = ecx;
  ret->registers[3] = edx;
  return ret;
}

static inline SrtCpuidData *
_srt_cpuid_data_new_for_signature (const char *text)
{
  SrtCpuidData *ret = g_new0 (SrtCpuidData, 1);

  strncpy (ret->text, text, sizeof (ret->text) - 1);
  return ret;
}
#endif

static inline void
_srt_cpuid_data_free (gpointer self)
{
  g_free (self);
}

#if defined(__x86_64__) || defined(__i386__)
G_GNUC_INTERNAL
gboolean _srt_x86_cpuid (GHashTable *mock_cpuid,
                         gboolean force,
                         guint leaf,
                         guint *eax,
                         guint *ebx,
                         guint *ecx,
                         guint *edx);

gboolean _srt_x86_cpuid_count (GHashTable *mock_cpuid,
                               guint leaf,
                               guint subleaf,
                               guint *eax,
                               guint *ebx,
                               guint *ecx,
                               guint *edx);

#endif

G_GNUC_INTERNAL
SrtX86FeatureFlags _srt_feature_get_x86_flags (GHashTable *mock_cpuid,
                                               SrtX86FeatureFlags *known);
