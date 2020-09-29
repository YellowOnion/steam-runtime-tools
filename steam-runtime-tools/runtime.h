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

#include <steam-runtime-tools/macros.h>

/* Backward compatibility with previous steam-runtime-tools naming */
#define SRT_RUNTIME_ISSUES_INTERNAL_ERROR SRT_RUNTIME_ISSUES_UNKNOWN

/**
 * SrtRuntimeIssues:
 * @SRT_RUNTIME_ISSUES_NONE: There are no problems
 * @SRT_RUNTIME_ISSUES_UNKNOWN: A generic internal error occurred while
 *  trying to detect the status of the `LD_LIBRARY_PATH`-based Steam Runtime,
 *  or, while reading a report, either an unknown issue flag was encountered
 *  or the runtime issues field was missing
 * @SRT_RUNTIME_ISSUES_DISABLED: The Steam Runtime has been disabled
 * @SRT_RUNTIME_ISSUES_NOT_RUNTIME: The Steam Runtime does not appear to
 *  have the correct structure
 * @SRT_RUNTIME_ISSUES_UNOFFICIAL: The Steam Runtime is an unofficial build
 * @SRT_RUNTIME_ISSUES_UNEXPECTED_LOCATION: The Steam Runtime is not in
 *  the location that was expected
 * @SRT_RUNTIME_ISSUES_UNEXPECTED_VERSION: The Steam Runtime is not the
 *  version that was expected
 * @SRT_RUNTIME_ISSUES_NOT_IN_LD_PATH: The Steam Runtime is not in the
 *  expected position in the `LD_LIBRARY_PATH`
 * @SRT_RUNTIME_ISSUES_NOT_IN_PATH: The Steam Runtime is not in the
 *  expected position in the `PATH`
 * @SRT_RUNTIME_ISSUES_NOT_IN_ENVIRONMENT: The environment variable
 *  `STEAM_RUNTIME` is not set to the absolute path to the Steam Runtime
 * @SRT_RUNTIME_ISSUES_NOT_USING_NEWER_HOST_LIBRARIES: The Steam Runtime has
 *  been configured to not use host libraries even if they are newer than
 *  the libraries in the Steam Runtime. This is likely to work
 *  acceptably with NVIDIA non-free graphics drivers, but is likely to
 *  break Mesa.
 *
 * A bitfield with flags representing problems with the Steam Runtime, or
 * %SRT_RUNTIME_ISSUES_NONE (which is numerically zero) if no problems
 * were detected.
 *
 * In general, more bits set means more problems.
 */
typedef enum
{
  SRT_RUNTIME_ISSUES_UNKNOWN = (1 << 0),
  SRT_RUNTIME_ISSUES_DISABLED = (1 << 1),
  SRT_RUNTIME_ISSUES_NOT_RUNTIME = (1 << 2),
  SRT_RUNTIME_ISSUES_UNOFFICIAL = (1 << 3),
  SRT_RUNTIME_ISSUES_UNEXPECTED_LOCATION = (1 << 4),
  SRT_RUNTIME_ISSUES_UNEXPECTED_VERSION = (1 << 5),
  SRT_RUNTIME_ISSUES_NOT_IN_LD_PATH = (1 << 6),
  SRT_RUNTIME_ISSUES_NOT_IN_PATH = (1 << 7),
  SRT_RUNTIME_ISSUES_NOT_IN_ENVIRONMENT = (1 << 8),
  SRT_RUNTIME_ISSUES_NOT_USING_NEWER_HOST_LIBRARIES = (1 << 9),
  SRT_RUNTIME_ISSUES_NONE = 0
} SrtRuntimeIssues;
