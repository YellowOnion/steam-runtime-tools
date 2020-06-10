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

/**
 * SrtX86FeatureFlags:
 * @SRT_X86_FEATURE_NONE: None of the features listed here are supported
 * @SRT_X86_FEATURE_X86_64: The CPU supports the "Long mode", where an OS can
 *  access 64-bit instructions and registers (i.e. x86-64 architecture),
 *  indicated by `lm` in Linux `/proc/cpuinfo`
 * @SRT_X86_FEATURE_SSE3: The CPU supports the SSE3 extension (Streaming SIMD
 *  Extensions 3, also known as Prescott New Instructions), indicated by
 *  `pni` in Linux `/proc/cpuinfo`
 * @SRT_X86_FEATURE_CMPXCHG16B: The CPU supports the CMPXCHG16B instruction,
 *  indicated by `cx16` in Linux `/proc/cpuinfo`
 * @SRT_X86_FEATURE_UNKNOWN: An unknown CPU feature was encountered when
 *  loading a report
 *
 * A bitfield with flags representing the features that the CPU supports, or
 * %SRT_X86_FEATURE_NONE (which is numerically zero) if none of the features
 * we checked are supported.
 *
 * In general, more bits set means more instructions are supported, with the
 * only exception for %SRT_X86_FEATURE_UNKNOWN.
 *
 * At the time of writing, the Steam client requires %SRT_X86_FEATURE_X86_64,
 * %SRT_X86_FEATURE_SSE3 and %SRT_X86_FEATURE_CMPXCHG16B.
 */
typedef enum
{
  SRT_X86_FEATURE_X86_64 = (1 << 0),
  SRT_X86_FEATURE_SSE3 = (1 << 1),
  SRT_X86_FEATURE_CMPXCHG16B = (1 << 2),
  SRT_X86_FEATURE_UNKNOWN = (1 << 3),
  SRT_X86_FEATURE_NONE = 0
} SrtX86FeatureFlags;
