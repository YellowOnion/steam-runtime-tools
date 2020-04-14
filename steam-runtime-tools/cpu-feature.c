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

#include <sys/stat.h>
#include <sys/types.h>
#include <cpuid.h>

#include "steam-runtime-tools/glib-compat.h"
#include "steam-runtime-tools/utils.h"

/**
* SECTION:cpu-feature
* @title: CPU features
* @short_description: Information about supported CPU features
* @include: steam-runtime-tools/steam-runtime-tools.h
*
* #SrtX86FeatureFlags represents the features that the CPU supports.
*/

SrtX86FeatureFlags
_srt_feature_get_x86_flags (void)
{
  guint eax = 0;
  guint ebx = 0;
  guint ecx = 0;
  guint edx = 0;
  int result;
  SrtX86FeatureFlags features = SRT_X86_FEATURE_NONE;

  /* Get the list of basic features (leaf 1) */
  result = __get_cpuid (1, &eax, &ebx, &ecx, &edx);
  if (result != 1)
    {
      g_debug ("Something went wrong trying to list supported x86 features");
      return features;
    }
  
  if (ecx & bit_CMPXCHG16B)
    features |= SRT_X86_FEATURE_CMPXCHG16B;
  
  if (ecx & bit_SSE3)
    features |= SRT_X86_FEATURE_SSE3;

  result = __get_cpuid (0x80000001, &eax, &ebx, &ecx, &edx);
  if (result != 1)
    {
      g_debug ("Something went wrong trying to list extended supported x86 features");
      return features;
    }

  /* Long mode, 64-bit capable */
  if (edx & bit_LM)
    features |= SRT_X86_FEATURE_X86_64;

  return features;
}