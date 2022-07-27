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

#include <libglnx.h>
#include <steam-runtime-tools/glib-backports-internal.h>

#include <steam-runtime-tools/steam-runtime-tools.h>

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

#include "steam-runtime-tools/cpu-feature-internal.h"
#include "steam-runtime-tools/virtualization-internal.h"
#include "test-utils.h"

static const char *argv0;
static gchar *global_sysroots;

typedef struct
{
  gchar *srcdir;
  gchar *builddir;
  const gchar *sysroots;
} Fixture;

typedef struct
{
  int unused;
} Config;

static void
setup (Fixture *f,
       gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  f->srcdir = g_strdup (g_getenv ("G_TEST_SRCDIR"));
  f->builddir = g_strdup (g_getenv ("G_TEST_BUILDDIR"));

  if (f->srcdir == NULL)
    f->srcdir = g_path_get_dirname (argv0);

  if (f->builddir == NULL)
    f->builddir = g_path_get_dirname (argv0);

  f->sysroots = global_sysroots;
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;

  g_free (f->srcdir);
  g_free (f->builddir);
}

static void
test_cpuid (Fixture *f,
            gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;
  g_autoptr(GError) error = NULL;
  g_autoptr(GHashTable) mock_cpuid = g_hash_table_new_full (_srt_cpuid_key_hash,
                                                            _srt_cpuid_key_equals,
                                                            _srt_cpuid_key_free,
                                                            _srt_cpuid_data_free);
  /* We use the debian10 mock sysroot, which doesn't have a /sys,
   * to ensure that only CPUID gets used. Initially, there is no CPUID
   * information either. */
  g_autofree gchar *sysroot = g_build_filename (f->sysroots, "debian10", NULL);
  glnx_autofd int sysroot_fd = -1;

  glnx_opendirat (AT_FDCWD, sysroot, TRUE, &sysroot_fd, &error);
  g_assert_no_error (error);

    {
      g_autoptr(SrtVirtualizationInfo) virt = NULL;

      virt = _srt_check_virtualization (mock_cpuid, sysroot_fd);
      g_assert_nonnull (virt);
      g_assert_cmpint (srt_virtualization_info_get_virtualization_type (virt), ==,
                       SRT_VIRTUALIZATION_TYPE_NONE);
      g_assert_cmpint (srt_virtualization_info_get_host_machine (virt), ==,
                       SRT_MACHINE_TYPE_UNKNOWN);
      g_assert_cmpstr (srt_virtualization_info_get_interpreter_root (virt), ==, NULL);
    }

#if defined(__x86_64__) || defined(__i386__)
    {
      g_autoptr(SrtVirtualizationInfo) virt = NULL;

      g_hash_table_remove_all (mock_cpuid);
      g_hash_table_replace (mock_cpuid,
                            _srt_cpuid_key_new (_SRT_CPUID_LEAF_PROCESSOR_INFO, 0),
                            _srt_cpuid_data_new (0, 0, _SRT_CPUID_FLAG_PROCESSOR_INFO_ECX_HYPERVISOR_PRESENT, 0));
      virt = _srt_check_virtualization (mock_cpuid, sysroot_fd);
      g_assert_nonnull (virt);
      g_assert_cmpint (srt_virtualization_info_get_virtualization_type (virt), ==,
                       SRT_VIRTUALIZATION_TYPE_UNKNOWN);
      g_assert_cmpint (srt_virtualization_info_get_host_machine (virt), ==,
                       SRT_MACHINE_TYPE_UNKNOWN);
      g_assert_cmpstr (srt_virtualization_info_get_interpreter_root (virt), ==, NULL);
    }

    {
      g_autoptr(SrtVirtualizationInfo) virt = NULL;

      g_hash_table_remove_all (mock_cpuid);
      g_hash_table_replace (mock_cpuid,
                            _srt_cpuid_key_new (_SRT_CPUID_LEAF_PROCESSOR_INFO, 0),
                            _srt_cpuid_data_new (0, 0, _SRT_CPUID_FLAG_PROCESSOR_INFO_ECX_HYPERVISOR_PRESENT, 0));
      g_hash_table_replace (mock_cpuid,
                            _srt_cpuid_key_new (_SRT_CPUID_LEAF_HYPERVISOR_ID, 0),
                            _srt_cpuid_data_new_for_signature ("xxxxKVMKVMKVM"));
      virt = _srt_check_virtualization (mock_cpuid, sysroot_fd);
      g_assert_nonnull (virt);
      g_assert_cmpint (srt_virtualization_info_get_virtualization_type (virt), ==,
                       SRT_VIRTUALIZATION_TYPE_KVM);
      g_assert_cmpint (srt_virtualization_info_get_host_machine (virt), ==,
                       SRT_MACHINE_TYPE_UNKNOWN);
      g_assert_cmpstr (srt_virtualization_info_get_interpreter_root (virt), ==, NULL);
    }

    {
      g_autoptr(SrtVirtualizationInfo) virt = NULL;

      g_hash_table_remove_all (mock_cpuid);
      g_hash_table_replace (mock_cpuid,
                            _srt_cpuid_key_new (_SRT_CPUID_LEAF_PROCESSOR_INFO, 0),
                            _srt_cpuid_data_new (0, 0, _SRT_CPUID_FLAG_PROCESSOR_INFO_ECX_HYPERVISOR_PRESENT, 0));
      g_hash_table_replace (mock_cpuid,
                            _srt_cpuid_key_new (_SRT_CPUID_LEAF_HYPERVISOR_ID, 0),
                            _srt_cpuid_data_new_for_signature ("xxxxFEXIFEXIEMU"));
      g_hash_table_replace (mock_cpuid,
                            _srt_cpuid_key_new (_SRT_CPUID_LEAF_FEX_INFO, 0),
                            _srt_cpuid_data_new (_SRT_CPUID_FEX_HOST_MACHINE_AARCH64,
                                                 0, 0, 0));
      virt = _srt_check_virtualization (mock_cpuid, sysroot_fd);
      g_assert_nonnull (virt);
      g_assert_cmpint (srt_virtualization_info_get_virtualization_type (virt), ==,
                       SRT_VIRTUALIZATION_TYPE_FEX_EMU);
      g_assert_cmpint (srt_virtualization_info_get_host_machine (virt), ==,
                       SRT_MACHINE_TYPE_AARCH64);
      g_assert_cmpstr (srt_virtualization_info_get_interpreter_root (virt),
                       ==, "/mock-rootfs");
    }
#endif
}

static void
test_dmi_id (Fixture *f,
             gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;
  g_autoptr(GError) error = NULL;
  /* Empty CPUID data, so that we only use the DMI IDs */
  g_autoptr(GHashTable) mock_cpuid = g_hash_table_new_full (_srt_cpuid_key_hash,
                                                            _srt_cpuid_key_equals,
                                                            _srt_cpuid_key_free,
                                                            _srt_cpuid_data_free);

  /* The fedora sysroot is set up by tests/generate-sysroots.py to identify
   * as VirtualBox */
    {
      g_autoptr(SrtVirtualizationInfo) virt = NULL;
      g_autofree gchar *sysroot = g_build_filename (f->sysroots, "fedora", NULL);
      glnx_autofd int sysroot_fd = -1;

      glnx_opendirat (AT_FDCWD, sysroot, TRUE, &sysroot_fd, &error);
      g_assert_no_error (error);

      virt = _srt_check_virtualization (mock_cpuid, sysroot_fd);
      g_assert_nonnull (virt);
      g_assert_cmpint (srt_virtualization_info_get_virtualization_type (virt), ==,
                       SRT_VIRTUALIZATION_TYPE_ORACLE);
      g_assert_cmpint (srt_virtualization_info_get_host_machine (virt), ==,
                       SRT_MACHINE_TYPE_UNKNOWN);
      g_assert_cmpstr (srt_virtualization_info_get_interpreter_root (virt), ==, NULL);
    }

  /* The ubuntu16 sysroot is set up by tests/generate-sysroots.py to identify
   * as QEMU */
    {
      g_autoptr(SrtVirtualizationInfo) virt = NULL;
      g_autofree gchar *sysroot = g_build_filename (f->sysroots, "ubuntu16", NULL);
      glnx_autofd int sysroot_fd = -1;

      glnx_opendirat (AT_FDCWD, sysroot, TRUE, &sysroot_fd, &error);
      g_assert_no_error (error);

      virt = _srt_check_virtualization (mock_cpuid, sysroot_fd);
      g_assert_nonnull (virt);
      g_assert_cmpint (srt_virtualization_info_get_virtualization_type (virt), ==,
                       SRT_VIRTUALIZATION_TYPE_QEMU);
      g_assert_cmpint (srt_virtualization_info_get_host_machine (virt), ==,
                       SRT_MACHINE_TYPE_UNKNOWN);
      g_assert_cmpstr (srt_virtualization_info_get_interpreter_root (virt), ==, NULL);
      g_clear_object (&virt);

#if defined(__x86_64__) || defined(__i386__)
      /* KVM from CPUID is not overwritten by QEMU from DMI ID */
      g_hash_table_remove_all (mock_cpuid);
      g_hash_table_replace (mock_cpuid,
                            _srt_cpuid_key_new (_SRT_CPUID_LEAF_PROCESSOR_INFO, 0),
                            _srt_cpuid_data_new (0, 0, _SRT_CPUID_FLAG_PROCESSOR_INFO_ECX_HYPERVISOR_PRESENT, 0));
      g_hash_table_replace (mock_cpuid,
                            _srt_cpuid_key_new (_SRT_CPUID_LEAF_HYPERVISOR_ID, 0),
                            _srt_cpuid_data_new_for_signature ("xxxxKVMKVMKVM"));
      virt = _srt_check_virtualization (mock_cpuid, sysroot_fd);
      g_assert_nonnull (virt);
      g_assert_cmpint (srt_virtualization_info_get_virtualization_type (virt), ==,
                       SRT_VIRTUALIZATION_TYPE_KVM);
      g_assert_cmpint (srt_virtualization_info_get_host_machine (virt), ==,
                       SRT_MACHINE_TYPE_UNKNOWN);
      g_assert_cmpstr (srt_virtualization_info_get_interpreter_root (virt), ==, NULL);
      g_clear_object (&virt);
      g_hash_table_remove_all (mock_cpuid);
#endif
    }
}

int
main (int argc,
      char **argv)
{
  int status;

  argv0 = argv[0];

  _srt_tests_init (&argc, &argv, NULL);
  global_sysroots = _srt_global_setup_sysroots (argv0);

  g_test_add ("/cpuid", Fixture, NULL, setup, test_cpuid, teardown);
  g_test_add ("/dmi-id", Fixture, NULL, setup, test_dmi_id, teardown);

  status = g_test_run ();

  _srt_global_teardown_sysroots ();
  g_clear_pointer (&global_sysroots, g_free);

  return status;
}
