/*
 * Copyright © 2011-2022 many systemd contributors
 * Copyright © 2022 Collabora Ltd.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "steam-runtime-tools/virtualization.h"

#include <sys/stat.h>
#include <sys/types.h>

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#include <sys/utsname.h>
#endif

#include <glib.h>
#include <gio/gio.h>

#include "steam-runtime-tools/cpu-feature-internal.h"
#include "steam-runtime-tools/virtualization-internal.h"
#include "steam-runtime-tools/enums.h"
#include "steam-runtime-tools/glib-backports-internal.h"
#include "steam-runtime-tools/utils.h"
#include "steam-runtime-tools/utils-internal.h"

/**
 * SECTION:virtualization
 * @title: Virtualization and emulation info
 * @short_description: Information about virtualization, hypervisors and emulation
 * @include: steam-runtime-tools/steam-runtime-tools.h
 */

struct _SrtVirtualizationInfo
{
  /*< private >*/
  GObject parent;
  gchar *interpreter_root;
  SrtVirtualizationType type;
  SrtMachineType host_machine;
};

struct _SrtVirtualizationInfoClass
{
  /*< private >*/
  GObjectClass parent_class;
};

enum {
  PROP_0,
  PROP_HOST_MACHINE,
  PROP_INTERPRETER_ROOT,
  PROP_TYPE,
  N_PROPERTIES
};

G_DEFINE_TYPE (SrtVirtualizationInfo, srt_virtualization_info, G_TYPE_OBJECT)

static void
srt_virtualization_info_init (SrtVirtualizationInfo *self)
{
}

static void
srt_virtualization_info_get_property (GObject *object,
                                      guint prop_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
  SrtVirtualizationInfo *self = SRT_VIRTUALIZATION_INFO (object);

  switch (prop_id)
    {
      case PROP_INTERPRETER_ROOT:
        g_value_set_string (value, self->interpreter_root);
        break;

      case PROP_TYPE:
        g_value_set_enum (value, self->type);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_virtualization_info_set_property (GObject *object,
                                      guint prop_id,
                                      const GValue *value,
                                      GParamSpec *pspec)
{
  SrtVirtualizationInfo *self = SRT_VIRTUALIZATION_INFO (object);

  switch (prop_id)
    {
      case PROP_HOST_MACHINE:
        /* Construct-only */
        g_return_if_fail (self->host_machine == 0);
        self->host_machine = g_value_get_enum (value);
        break;

      case PROP_INTERPRETER_ROOT:
        /* Construct-only */
        g_return_if_fail (self->interpreter_root == NULL);
        self->interpreter_root = g_value_dup_string (value);
        break;

      case PROP_TYPE:
        /* Construct-only */
        g_return_if_fail (self->type == 0);
        self->type = g_value_get_enum (value);
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
srt_virtualization_info_finalize (GObject *object)
{
  SrtVirtualizationInfo *self = SRT_VIRTUALIZATION_INFO (object);

  g_free (self->interpreter_root);

  G_OBJECT_CLASS (srt_virtualization_info_parent_class)->finalize (object);
}

static GParamSpec *properties[N_PROPERTIES] = { NULL };

static void
srt_virtualization_info_class_init (SrtVirtualizationInfoClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->get_property = srt_virtualization_info_get_property;
  object_class->set_property = srt_virtualization_info_set_property;
  object_class->finalize = srt_virtualization_info_finalize;

  properties[PROP_TYPE] =
    g_param_spec_enum ("type", "Virtualization type",
                       "Which virtualization type is currently in use",
                       SRT_TYPE_VIRTUALIZATION_TYPE,
                       SRT_VIRTUALIZATION_TYPE_UNKNOWN,
                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  properties[PROP_HOST_MACHINE] =
    g_param_spec_enum ("host-machine", "Host machine",
                       "What machine the emulator is running on, if any",
                       SRT_TYPE_MACHINE_TYPE,
                       SRT_MACHINE_TYPE_UNKNOWN,
                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                       G_PARAM_STATIC_STRINGS);

  properties[PROP_INTERPRETER_ROOT] =
    g_param_spec_string ("interpreter-root", "Interpreter root",
                         "Absolute path where libraries for the emulated"
                         "architecture can be found",
                         NULL,
                         G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                         G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

#if defined(__x86_64__) || defined(__i386__)
/* Signatures mostly taken from systemd src/basic/virt.c */
static const struct
{
  const char signature[13];
  SrtVirtualizationType type;
} hypervisor_signatures[] =
{
  { "XenVMMXenVMM", SRT_VIRTUALIZATION_TYPE_XEN },
  { "KVMKVMKVM", SRT_VIRTUALIZATION_TYPE_KVM },
  { "Linux KVM Hv", SRT_VIRTUALIZATION_TYPE_KVM },
  { "TCGTCGTCGTCG", SRT_VIRTUALIZATION_TYPE_QEMU },
  { "VMWareVMWare", SRT_VIRTUALIZATION_TYPE_VMWARE },
  { "Microsoft Hv", SRT_VIRTUALIZATION_TYPE_MICROSOFT },
  { "bhyve bhyve ", SRT_VIRTUALIZATION_TYPE_BHYVE },
  { "QNXQVMBSQG", SRT_VIRTUALIZATION_TYPE_QNX },
  { "ACRNACRNACRN", SRT_VIRTUALIZATION_TYPE_ACRN },
  /* https://github.com/FEX-Emu/FEX/blob/HEAD/docs/CPUID.md */
  { "FEXIFEXIEMU", SRT_VIRTUALIZATION_TYPE_FEX_EMU },
};
#endif

/**
 * _srt_check_virtualization:
 * @mock_cpuid: (nullable) (element-type SrtCpuidKey SrtCpuidData): mock data
 *  to use instead of the real CPUID instruction
 * @sysroot_fd: Sysroot file descriptor
 *
 * Gather and return information about the hypervisor or emulator that
 * this code is running under.
 *
 * Returns: (transfer full): A new #SrtVirtualizationInfo object.
 *  Free with g_object_unref().
 */
SrtVirtualizationInfo *
_srt_check_virtualization (GHashTable *mock_cpuid,
                           const char *mock_uname_version,
                           int sysroot_fd)
{
  SrtVirtualizationType type = SRT_VIRTUALIZATION_TYPE_NONE;
  gsize i;
#if defined(__x86_64__) || defined(__i386__)
  g_autofree gchar *interpreter_root = NULL;
  guint32 eax = 0;
  guint32 ebx = 0;
  guint32 ecx = 0;
  guint32 edx = 0;
  gboolean hypervisor_present = FALSE;
  struct utsname uname_buf = {};
  SrtCpuidData signature = {};
  SrtMachineType host_machine = SRT_MACHINE_TYPE_UNKNOWN;

  /* CPUID leaf 1, bit 31 is the Hypervisor Present Bit.
   * https://lwn.net/Articles/301888/ */
  if (_srt_x86_cpuid (mock_cpuid, FALSE, _SRT_CPUID_LEAF_PROCESSOR_INFO, 0,
                      &eax, &ebx, &ecx, &edx))
    {
      if (ecx & _SRT_CPUID_FLAG_PROCESSOR_INFO_ECX_HYPERVISOR_PRESENT)
        {
          g_debug ("Hypervisor Present bit set in CPUID 0x1");
          hypervisor_present = TRUE;
          type = SRT_VIRTUALIZATION_TYPE_UNKNOWN;
        }
      else
        {
          g_debug ("Hypervisor Present bit not set in CPUID 0x1");
        }
    }
  else
    {
      g_debug ("Unable to query Hypervisor Present bit from CPUID 0x1");
    }

  /* FEX-Emu doesn't set Hypervisor Present: arguably this is wrong
   * because it implements the 0x4000_0000 leaf, but arguably it's
   * correct because it isn't technically a hypervisor. Either way... */

  if (G_UNLIKELY (mock_uname_version != NULL))
    strncpy (uname_buf.version, mock_uname_version, sizeof (uname_buf.version) - 1);

  if (mock_uname_version != NULL || uname (&uname_buf) == 0)
    {
      if (g_str_has_prefix (uname_buf.version, "#FEX-"))
        {
          g_debug ("This is probably FEX-Emu according to uname(2): %s",
                   uname_buf.version);
          type = SRT_VIRTUALIZATION_TYPE_FEX_EMU;
        }
    }

  /* https://lwn.net/Articles/301888/ */
  if (hypervisor_present || type == SRT_VIRTUALIZATION_TYPE_FEX_EMU)
    {
      if (_srt_x86_cpuid (mock_cpuid,
                          TRUE,
                          _SRT_CPUID_LEAF_HYPERVISOR_ID,
                          0,
                          &signature.registers[0],
                          &signature.registers[1],
                          &signature.registers[2],
                          &signature.registers[3])
          && signature.registers[0] >= _SRT_CPUID_LEAF_HYPERVISOR_ID)
        {
          /* The hypervisor signature appears in EBX, ECX, EDX, so skip
           * the first 4 bytes. */
          g_debug ("Highest supported hypervisor info leaf: 0x%x",
                   signature.registers[0]);
          g_debug ("Hypervisor signature from CPUID 0x4000_0000: \"%s\"",
                   &signature.text[4]);

          for (i = 0; i < G_N_ELEMENTS (hypervisor_signatures); i++)
            {
              if (strcmp (&signature.text[4], hypervisor_signatures[i].signature) == 0)
                {
                  type = hypervisor_signatures[i].type;
                  break;
                }
            }
        }
      else
        {
          g_debug ("Unable to query hypervisor signature from CPUID 0x4000_0000: "
                   "0x%x 0x%x 0x%x 0x%x",
                   signature.registers[0],
                   signature.registers[1],
                   signature.registers[2],
                   signature.registers[3]);
        }
    }

  if (type == SRT_VIRTUALIZATION_TYPE_FEX_EMU && signature.registers[0] >= _SRT_CPUID_LEAF_FEX_INFO)
    {
      /* https://github.com/FEX-Emu/FEX/blob/HEAD/docs/CPUID.md */
      if (_srt_x86_cpuid (mock_cpuid, TRUE, _SRT_CPUID_LEAF_FEX_INFO, 0,
                          &eax, &ebx, &ecx, &edx))
        {
          g_debug ("FEX-Emu host machine from CPUID 0x4000_0001: 0x%u", (eax & 0xF));

          switch (eax & 0xF)
            {
              case 1:
                host_machine = SRT_MACHINE_TYPE_X86_64;
                break;

              case 2:
                host_machine = SRT_MACHINE_TYPE_AARCH64;
                break;

              default:
                break;
            }
        }
    }
#endif

  /* We might be able to disambiguate exactly what KVM means by using
   * the DMI IDs */
  if ((type == SRT_VIRTUALIZATION_TYPE_UNKNOWN
       || type == SRT_VIRTUALIZATION_TYPE_NONE
       || type == SRT_VIRTUALIZATION_TYPE_KVM)
      && sysroot_fd >= 0)
    {
      static const char* const dmi_vendor_locations[] =
      {
        "/sys/class/dmi/id/product_name",
        "/sys/class/dmi/id/sys_vendor",
        "/sys/class/dmi/id/board_vendor",
        "/sys/class/dmi/id/bios_vendor",
        "/sys/class/dmi/id/product_version",
      };
      static const struct
      {
        const char *vendor;
        SrtVirtualizationType type;
      } dmi_vendor_table[] =
      {
        { "KVM", SRT_VIRTUALIZATION_TYPE_KVM },
        { "OpenStack", SRT_VIRTUALIZATION_TYPE_KVM },
        { "Amazon EC2", SRT_VIRTUALIZATION_TYPE_AMAZON },
        { "QEMU", SRT_VIRTUALIZATION_TYPE_QEMU },
        { "VMware", SRT_VIRTUALIZATION_TYPE_VMWARE },
        { "VMW", SRT_VIRTUALIZATION_TYPE_VMWARE },
        { "innotek GmbH", SRT_VIRTUALIZATION_TYPE_ORACLE },
        { "VirtualBox", SRT_VIRTUALIZATION_TYPE_ORACLE },
        { "Xen", SRT_VIRTUALIZATION_TYPE_XEN },
        { "Bochs", SRT_VIRTUALIZATION_TYPE_BOCHS },
        { "Parallels", SRT_VIRTUALIZATION_TYPE_PARALLELS },
        { "BHYVE", SRT_VIRTUALIZATION_TYPE_BHYVE },
        { "Hyper-V", SRT_VIRTUALIZATION_TYPE_MICROSOFT },
      };

      for (i = 0; i < G_N_ELEMENTS (dmi_vendor_locations); i++)
        {
          g_autofree gchar *contents = NULL;
          gsize j;

          if (_srt_file_get_contents_in_sysroot (sysroot_fd,
                                                 dmi_vendor_locations[i],
                                                 &contents, NULL, NULL))
            {
              for (j = 0; j < G_N_ELEMENTS (dmi_vendor_table); j++)
                {
                  if (g_str_has_prefix (contents, dmi_vendor_table[j].vendor))
                    {
                      g_debug ("Found DMI vendor \"%s\" in %s",
                               dmi_vendor_table[j].vendor,
                               dmi_vendor_locations[i]);

                      /* Don't allow overwriting KVM with the less
                       * specific QEMU */
                      if (type == SRT_VIRTUALIZATION_TYPE_KVM
                          && dmi_vendor_table[j].type == SRT_VIRTUALIZATION_TYPE_QEMU)
                        break;

                      type = dmi_vendor_table[j].type;
                      break;
                    }
                }
            }
        }
    }

#if defined(__x86_64__) || defined(__i386__)
  if (type == SRT_VIRTUALIZATION_TYPE_FEX_EMU)
    {
      glnx_autofd int rootfs_fd = -1;

      /* FEX-Emu special-cases "/" but not "/." (or "/usr/.."), so we can
       * use this as a trick to find the rootfs without forking a
       * subprocess. */

      if (glnx_opendirat (AT_FDCWD, "/.", TRUE, &rootfs_fd, NULL))
        {
          g_autofree gchar *proc_path = g_strdup_printf ("/proc/self/fd/%d", rootfs_fd);

          if (G_UNLIKELY (mock_cpuid != NULL))
            {
              interpreter_root = g_strdup ("/mock-rootfs");
            }
          else
            {
              /* Note that pressure-vessel assumes this is canonicalized. */
              interpreter_root = glnx_readlinkat_malloc (AT_FDCWD, proc_path,
                                                         NULL, NULL);
            }

          if (g_str_equal (interpreter_root, "/"))
            g_clear_pointer (&interpreter_root, g_free);
        }
    }
#endif

#if defined(__x86_64__) || defined(__i386__)
  return _srt_virtualization_info_new (host_machine, interpreter_root, type);
#else
  return _srt_virtualization_info_new (SRT_MACHINE_TYPE_UNKNOWN, NULL, type);
#endif
}

/**
 * srt_virtualization_info_get_virtualization_type:
 * @self: A SrtVirtualizationInfo object
 *
 * If the program appears to be running in a hypervisor or emulator,
 * return what type it is.
 *
 * Returns: A recognised virtualization type, or %SRT_VIRTUALIZATION_TYPE_NONE
 *  if a hypervisor cannot be detected, or %SRT_VIRTUALIZATION_TYPE_UNKNOWN
 *  if unsure.
 */
SrtVirtualizationType
srt_virtualization_info_get_virtualization_type (SrtVirtualizationInfo *self)
{
  g_return_val_if_fail (SRT_IS_VIRTUALIZATION_INFO (self),
                        SRT_VIRTUALIZATION_TYPE_UNKNOWN);
  return self->type;
}

/**
 * srt_virtualization_info_get_host_machine:
 * @self: A SrtVirtualizationInfo object
 *
 * If the program appears to be running in an emulator, try to return the
 * machine architecture of the host on which the emulator is running.
 * Otherwise return %SRT_MACHINE_TYPE_UNKNOWN.
 *
 * Returns: A machine type, or %SRT_MACHINE_TYPE_UNKNOWN
 *  if the machine type cannot be detected or is not applicable
 */
SrtMachineType
srt_virtualization_info_get_host_machine (SrtVirtualizationInfo *self)
{
  g_return_val_if_fail (SRT_IS_VIRTUALIZATION_INFO (self),
                        SRT_MACHINE_TYPE_UNKNOWN);
  return self->host_machine;
}

/**
 * srt_virtualization_info_get_interpreter_root:
 * @self: A SrtVirtualizationInfo object
 *
 * If the program appears to be running under user-space emulation with
 * an interpreter like FEX-Emu, which behaves as though emulated libraries
 * from a sysroot for the emulated architecture had been overlaid onto
 * the real root filesystem, then return the root directory of that
 * sysroot. Otherwise return %NULL.
 *
 * Returns: (type filename) (nullable): A path from which libraries for
 *  the emulated architecture can be loaded, or %NULL if unknown or
 *  unavailable.
 */
const gchar *
srt_virtualization_info_get_interpreter_root (SrtVirtualizationInfo *self)
{
  g_return_val_if_fail (SRT_IS_VIRTUALIZATION_INFO (self), NULL);
  return self->interpreter_root;
}
