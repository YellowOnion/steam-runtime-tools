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

#if !defined(_SRT_IN_SINGLE_HEADER) && !defined(_SRT_COMPILATION)
#error "Do not include directly, use <steam-runtime-tools/steam-runtime-tools.h>"
#endif

#include <glib.h>
#include <glib-object.h>

#include <steam-runtime-tools/architecture.h>
#include <steam-runtime-tools/macros.h>

typedef struct _SrtVirtualizationInfo SrtVirtualizationInfo;
typedef struct _SrtVirtualizationInfoClass SrtVirtualizationInfoClass;

#define SRT_TYPE_VIRTUALIZATION_INFO srt_virtualization_info_get_type ()
#define SRT_VIRTUALIZATION_INFO(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), SRT_TYPE_VIRTUALIZATION_INFO, SrtVirtualizationInfo))
#define SRT_VIRTUALIZATION_INFO_CLASS(cls) (G_TYPE_CHECK_CLASS_CAST ((cls), SRT_TYPE_VIRTUALIZATION_INFO, SrtVirtualizationInfoClass))
#define SRT_IS_VIRTUALIZATION_INFO(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SRT_TYPE_VIRTUALIZATION_INFO))
#define SRT_IS_VIRTUALIZATION_INFO_CLASS(cls) (G_TYPE_CHECK_CLASS_TYPE ((cls), SRT_TYPE_VIRTUALIZATION_INFO))
#define SRT_VIRTUALIZATION_INFO_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), SRT_TYPE_VIRTUALIZATION_INFO, SrtVirtualizationInfoClass)
_SRT_PUBLIC
GType srt_virtualization_info_get_type (void);

/**
 * SrtVirtualizationType:
 * @SRT_VIRTUALIZATION_TYPE_UNKNOWN: Unknown virtualization type
 * @SRT_VIRTUALIZATION_TYPE_NONE: No virtualization detected
 * @SRT_VIRTUALIZATION_TYPE_XEN: Xen hypervisor
 * @SRT_VIRTUALIZATION_TYPE_KVM: Linux KVM hypervisor (possibly via qemu)
 * @SRT_VIRTUALIZATION_TYPE_QEMU: qemu emulation without KVM, and perhaps
 *  older versions of qemu with KVM
 * @SRT_VIRTUALIZATION_TYPE_VMWARE: VMware virtual machine
 * @SRT_VIRTUALIZATION_TYPE_MICROSOFT: Microsoft Hyper-V virtual machine
 * @SRT_VIRTUALIZATION_TYPE_BHYVE: FreeBSD BHYVE
 * @SRT_VIRTUALIZATION_TYPE_QNX: QNX hypervisor
 * @SRT_VIRTUALIZATION_TYPE_ACRN: ACRN hypervisor
 * @SRT_VIRTUALIZATION_TYPE_AMAZON: Amazon EC2
 * @SRT_VIRTUALIZATION_TYPE_ORACLE: Oracle VirtualBox
 * @SRT_VIRTUALIZATION_TYPE_BOCHS: Bochs
 * @SRT_VIRTUALIZATION_TYPE_PARALLELS: Parallels
 * @SRT_VIRTUALIZATION_TYPE_FEX_EMU: FEX-Emu x86 emulation
 *
 * A type of virtualization.
 *
 * The vocabulary used here is chosen to be approximately compatible
 * with systemd's ConditionVirtualization.
 */
typedef enum
{
  SRT_VIRTUALIZATION_TYPE_NONE = 0,
  SRT_VIRTUALIZATION_TYPE_XEN,
  SRT_VIRTUALIZATION_TYPE_KVM,
  SRT_VIRTUALIZATION_TYPE_QEMU,
  SRT_VIRTUALIZATION_TYPE_VMWARE,
  SRT_VIRTUALIZATION_TYPE_MICROSOFT,
  SRT_VIRTUALIZATION_TYPE_BHYVE,
  SRT_VIRTUALIZATION_TYPE_QNX,
  SRT_VIRTUALIZATION_TYPE_ACRN,
  SRT_VIRTUALIZATION_TYPE_AMAZON,
  SRT_VIRTUALIZATION_TYPE_ORACLE,
  SRT_VIRTUALIZATION_TYPE_BOCHS,
  SRT_VIRTUALIZATION_TYPE_PARALLELS,
  SRT_VIRTUALIZATION_TYPE_FEX_EMU,
  SRT_VIRTUALIZATION_TYPE_UNKNOWN = -1
} SrtVirtualizationType;

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
G_DEFINE_AUTOPTR_CLEANUP_FUNC (SrtVirtualizationInfo, g_object_unref)
#endif

_SRT_PUBLIC
SrtVirtualizationType srt_virtualization_info_get_virtualization_type (SrtVirtualizationInfo *self);
_SRT_PUBLIC
SrtMachineType srt_virtualization_info_get_host_machine (SrtVirtualizationInfo *self);
_SRT_PUBLIC
const gchar *srt_virtualization_info_get_interpreter_root (SrtVirtualizationInfo *self);
