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

#include <steam-runtime-tools/macros.h>

/**
 * SrtMachineType:
 * @SRT_MACHINE_TYPE_UNKNOWN: An unknown or unspecified CPU (`EM_NONE`)
 * @SRT_MACHINE_TYPE_386: i386 (IA-32, 32-bit x86; `EM_386`)
 * @SRT_MACHINE_TYPE_X86_64: x86_64 (amd64, x64, Intel 64, 64-bit x86; `EM_X86_64`)
 * @SRT_MACHINE_TYPE_AARCH64: AArch64 (64-bit ARM; `EM_AARCH64`)
 *
 * A type of machine.
 *
 * Values of this enum are numerically equal to ELF machine types, although
 * only a small subset of ELF machine types are represented here.
 */
typedef enum
{
  SRT_MACHINE_TYPE_UNKNOWN = 0,
  SRT_MACHINE_TYPE_386 = 3,
  SRT_MACHINE_TYPE_X86_64 = 62,
  SRT_MACHINE_TYPE_AARCH64 = 183,
} SrtMachineType;

/**
 * SRT_ABI_I386:
 *
 * The multiarch tuple for the i386 (IA-32) ABI normally used on
 * 32-bit x86 Linux.
 */
#define SRT_ABI_I386 "i386-linux-gnu"

/**
 * SRT_ABI_X86_64:
 *
 * The multiarch tuple for the x86_64 ABI normally used on
 * 64-bit x86 Linux.
 */
#define SRT_ABI_X86_64 "x86_64-linux-gnu"

/**
 * SRT_ABI_AARCH64:
 *
 * The multiarch tuple for the aarch64 ABI normally used on
 * 64-bit ARM Linux, used here as a proof-of-concept for
 * non-x86 support.
 */
#define SRT_ABI_AARCH64 "aarch64-linux-gnu"

_SRT_PUBLIC
gboolean srt_architecture_can_run_i386 (void);
_SRT_PUBLIC
gboolean srt_architecture_can_run_x86_64 (void);

/**
 * SrtArchitectureError:
 * @SRT_ARCHITECTURE_ERROR_FAILED: Generic error
 * @SRT_ARCHITECTURE_ERROR_INTERNAL_ERROR: An internal error occurred
 * @SRT_ARCHITECTURE_ERROR_NO_INFORMATION: It is unknown whether the
 *  given architecture, ld.so, etc. is available or not, for example
 *  because the interoperable ld.so path for the architecture is unknown,
 *  or because SrtSystemInfo is reading a JSON report that does not
 *  contain this information
 *
 * Errors raised when checking facts about an architecture.
 *
 * Errors in the #GIOErrorEnum domain can also be raised: for example,
 * if srt_system_info_check_runtime_linker() raises %G_IO_ERROR_NOT_FOUND,
 * it means the interoperable path for ld.so does not exist.
 */
typedef enum
{
  SRT_ARCHITECTURE_ERROR_FAILED = 0,
  SRT_ARCHITECTURE_ERROR_INTERNAL_ERROR,
  SRT_ARCHITECTURE_ERROR_NO_INFORMATION,
} SrtArchitectureError;

_SRT_PUBLIC
GQuark srt_architecture_error_quark (void);

#define SRT_ARCHITECTURE_ERROR (srt_architecture_error_quark ())

_SRT_PUBLIC
const char *srt_architecture_get_expected_runtime_linker (const char *multiarch_tuple);
