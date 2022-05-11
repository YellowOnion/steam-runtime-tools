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

#include "steam-runtime-tools/cpu-feature.h"
#include "steam-runtime-tools/cpu-feature-internal.h"
#include <steam-runtime-tools/enums.h>

#include <sys/stat.h>
#include <sys/types.h>
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

/**
* SECTION:cpu-feature
* @title: CPU features
* @short_description: Information about supported CPU features
* @include: steam-runtime-tools/steam-runtime-tools.h
*
* #SrtX86FeatureFlags represents the features that the CPU supports.
*/

#if defined(__x86_64__) || defined(__i386__)
/*
 * _srt_x86_cpuid:
 * @mock_cpuid: (nullable) (element-type guint SrtCpuidData): mock data
 *  to use instead of the real CPUID instruction
 * @force: if %TRUE, use low-level `__cpuid()`, a thin wrapper around the
 *  CPUID instruction. If %FALSE, use `__get_cpuid()`, which does
 *  capability checking but only works in the 0x0xxxxxxx and 0x8xxxxxxx
 *  ranges.
 * @leaf: CPUID leaf to return
 * @eax: (out) (not optional): used to return part of the given CPUID leaf
 * @ebx: (out) (not optional): used to return part of the given CPUID leaf
 * @ecx: (out) (not optional): used to return part of the given CPUID leaf
 * @edx: (out) (not optional): used to return part of the given CPUID leaf
 *
 * Like `__get_cpuid()`, but with support for using mock data in unit tests.
 *
 * Returns: %TRUE on success
 */
gboolean _srt_x86_cpuid (GHashTable *mock_cpuid,
                         gboolean force,
                         guint leaf,
                         guint *eax,
                         guint *ebx,
                         guint *ecx,
                         guint *edx)
{
  if (G_UNLIKELY (mock_cpuid != NULL))
    {
      SrtCpuidData *mock = g_hash_table_lookup (mock_cpuid,
                                                GUINT_TO_POINTER (leaf));

      if (mock == NULL)
        return FALSE;

      *eax = mock->registers[0];
      *ebx = mock->registers[1];
      *ecx = mock->registers[2];
      *edx = mock->registers[3];
      return TRUE;
    }
  else
    {
      *eax = *ebx = *ecx = *edx = 0;

      if (force)
        {
          /* __cpuid takes lvalues as parameters, and takes their addresses
           * internally, so this is right even though it looks wrong. */
          __cpuid (leaf, (*eax), (*ebx), (*ecx), (*edx));
          return TRUE;
        }
      else
        {
          return (__get_cpuid (leaf, eax, ebx, ecx, edx) == 1);
        }
    }
}
#endif

/**
 * _srt_feature_get_x86_flags:
 * @mock_cpuid: (nullable) (element-type guint SrtCpuidData): mock data
 *  to use instead of the real CPUID instruction
 * @known: (out) (not optional): Used to return the #SrtX86FeatureFlags
 *  that have been checked
 *
 * Returns: A #SrtX86FeatureFlags with the available X86 CPU flags.
 */
SrtX86FeatureFlags
_srt_feature_get_x86_flags (GHashTable *mock_cpuid,
                            SrtX86FeatureFlags *known)
{
  SrtX86FeatureFlags present = SRT_X86_FEATURE_NONE;

  g_return_val_if_fail (known != NULL, SRT_X86_FEATURE_NONE);

  *known = SRT_X86_FEATURE_NONE;

#if defined(__x86_64__) || defined(__i386__)
  guint eax = 0;
  guint ebx = 0;
  guint ecx = 0;
  guint edx = 0;

  /* Get the list of basic features (leaf 1) */
  if (_srt_x86_cpuid (mock_cpuid, FALSE, 1, &eax, &ebx, &ecx, &edx))
    {
      *known |= (SRT_X86_FEATURE_CMPXCHG16B | SRT_X86_FEATURE_SSE3);

      if (ecx & bit_CMPXCHG16B)
        present |= SRT_X86_FEATURE_CMPXCHG16B;

      if (ecx & bit_SSE3)
        present |= SRT_X86_FEATURE_SSE3;
    }
  else
    {
      g_debug ("Something went wrong trying to list supported x86 features");
      return present;
    }

  if (_srt_x86_cpuid (mock_cpuid, FALSE, 0x80000001, &eax, &ebx, &ecx, &edx))
    {
      *known |= SRT_X86_FEATURE_X86_64;

      /* Long mode, 64-bit capable */
      if (edx & bit_LM)
        present |= SRT_X86_FEATURE_X86_64;
    }
  else
    {
      g_debug ("Something went wrong trying to list extended supported x86 features");
      return present;
    }
#endif

  return present;
}
