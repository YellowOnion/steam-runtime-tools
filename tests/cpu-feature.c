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
#include "test-utils.h"

typedef struct
{
  int unused;
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
}

static void
teardown (Fixture *f,
          gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;
}

static void
test_cpu_feature (Fixture *f,
                  gconstpointer context)
{
  G_GNUC_UNUSED const Config *config = context;
#if defined(__x86_64__) || defined(__i386__)
  SrtX86FeatureFlags known;
  SrtX86FeatureFlags present;
  g_autoptr(GHashTable) mock_cpuid = g_hash_table_new_full (_srt_cpuid_key_hash,
                                                            _srt_cpuid_key_equals,
                                                            _srt_cpuid_key_free,
                                                            _srt_cpuid_data_free);

  present = _srt_feature_get_x86_flags (NULL, &known);
  /* We don't know whether SSE3 etc. are supported by the real CPU, but we
   * do know that the bits that are supported should be a subset of the
   * bits that were discovered */
  g_assert_cmpint (known, ==, present | known);
#if defined(__x86_64__)
  /* If we're compiled for x86_64 then we'd better be in long mode */
  g_assert_cmpint (present & SRT_X86_FEATURE_X86_64, ==, SRT_X86_FEATURE_X86_64);
#endif

  /* If we mock up a failing CPUID, then we know nothing */
  present = _srt_feature_get_x86_flags (mock_cpuid, &known);
  g_assert_cmpint (known, ==, SRT_X86_FEATURE_NONE);
  g_assert_cmpint (present, ==, SRT_X86_FEATURE_NONE);

  g_hash_table_replace (mock_cpuid,
                        _srt_cpuid_key_new (_SRT_CPUID_LEAF_PROCESSOR_INFO, 0),
                        _srt_cpuid_data_new (0, 0, 0, 0));
  g_hash_table_replace (mock_cpuid,
                        _srt_cpuid_key_new (_SRT_CPUID_LEAF_EXT_PROCESSOR_INFO, 0),
                        _srt_cpuid_data_new (0, 0, 0, 0));
  present = _srt_feature_get_x86_flags (mock_cpuid, &known);
  g_assert_cmpint (known, ==, _SRT_X86_FEATURE_ALL);
  g_assert_cmpint (present, ==, SRT_X86_FEATURE_NONE);

  g_hash_table_replace (mock_cpuid,
                        _srt_cpuid_key_new (_SRT_CPUID_LEAF_PROCESSOR_INFO, 0),
                        _srt_cpuid_data_new (0,
                                             0,
                                             (bit_CMPXCHG16B | bit_SSE3),
                                             0));
  g_hash_table_replace (mock_cpuid,
                        _srt_cpuid_key_new (_SRT_CPUID_LEAF_EXT_PROCESSOR_INFO, 0),
                        _srt_cpuid_data_new (0, 0, 0, bit_LM));
  present = _srt_feature_get_x86_flags (mock_cpuid, &known);
  g_assert_cmpint (known, ==, _SRT_X86_FEATURE_ALL);
  g_assert_cmpint (present, ==, _SRT_X86_FEATURE_ALL);

  g_hash_table_replace (mock_cpuid,
                        _srt_cpuid_key_new (_SRT_CPUID_LEAF_PROCESSOR_INFO, 0),
                        _srt_cpuid_data_new (0, 0, bit_CMPXCHG16B, 0));
  g_hash_table_replace (mock_cpuid,
                        _srt_cpuid_key_new (_SRT_CPUID_LEAF_EXT_PROCESSOR_INFO, 0),
                        _srt_cpuid_data_new (0, 0, 0, 0));
  present = _srt_feature_get_x86_flags (mock_cpuid, &known);
  g_assert_cmpint (known, ==, _SRT_X86_FEATURE_ALL);
  g_assert_cmpint (present, ==, SRT_X86_FEATURE_CMPXCHG16B);

  g_hash_table_replace (mock_cpuid,
                        _srt_cpuid_key_new (_SRT_CPUID_LEAF_PROCESSOR_INFO, 0),
                        _srt_cpuid_data_new (0, 0, bit_SSE3, 0));
  g_hash_table_replace (mock_cpuid,
                        _srt_cpuid_key_new (_SRT_CPUID_LEAF_EXT_PROCESSOR_INFO, 0),
                        _srt_cpuid_data_new (0, 0, 0, 0));
  present = _srt_feature_get_x86_flags (mock_cpuid, &known);
  g_assert_cmpint (known, ==, _SRT_X86_FEATURE_ALL);
  g_assert_cmpint (present, ==, SRT_X86_FEATURE_SSE3);

  g_hash_table_replace (mock_cpuid,
                        _srt_cpuid_key_new (_SRT_CPUID_LEAF_PROCESSOR_INFO, 0),
                        _srt_cpuid_data_new (0, 0, 0, 0));
  g_hash_table_replace (mock_cpuid,
                        _srt_cpuid_key_new (_SRT_CPUID_LEAF_EXT_PROCESSOR_INFO, 0),
                        _srt_cpuid_data_new (0, 0, 0, bit_LM));
  present = _srt_feature_get_x86_flags (mock_cpuid, &known);
  g_assert_cmpint (known, ==, _SRT_X86_FEATURE_ALL);
  g_assert_cmpint (present, ==, SRT_X86_FEATURE_X86_64);

#else /* !x86 */
  SrtX86FeatureFlags known;
  SrtX86FeatureFlags present = _srt_feature_get_x86_flags (NULL, &known);

  g_assert_cmpint (known, ==, SRT_X86_FEATURE_NONE);
  g_assert_cmpint (present, ==, SRT_X86_FEATURE_NONE);
#endif /* !x86 */
}

int
main (int argc,
      char **argv)
{
  g_test_init (&argc, &argv, NULL);

  g_test_add ("/cpu-feature", Fixture, NULL, setup,
              test_cpu_feature, teardown);

  return g_test_run ();
}
