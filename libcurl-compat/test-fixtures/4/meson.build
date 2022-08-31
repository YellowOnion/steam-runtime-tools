# Copyright 2021 Collabora Ltd.
# SPDX-License-Identifier: MIT

foreach suffix : ['', '-gnutls']
  # A mock implementation of libcurl with CURL_OPENSSL_4 or
  # CURL_GNUTLS_4 symbols, used during unit testing.
  mock_libcurl = shared_library(
    'curl' + suffix,
    'mock-curl.c',
    link_args : [
      '-Wl,--version-script,@0@/../../4@1@.versionscript'.format(
        meson.current_source_dir(),
        suffix,
      ),
    ],
    override_options : ['b_sanitize=none'],
    version : '4',
    soversion : '4',
    install : get_option('installed_tests'),
    install_dir : tests_dir / 'libcurl-compat' / '4' / multiarch,
  )

  # Test executable that calls curl_version@CURL_OPENSSL_4
  # or curl_version@CURL_GNUTLS_4, emulating a tool like valgrind
  # compiled against system libcurl, e.g. on Arch Linux
  executable(
    multiarch + '-steamrt-print-libcurl4@0@-version'.format(suffix),
    '../print-version.c',
    link_with : mock_libcurl,
    override_options : ['b_sanitize=none'],
    install : get_option('installed_tests'),
    install_dir : tests_dir / 'libcurl-compat' / '4',
  )
endforeach

if get_option('installed_tests')
  install_data(
    'libcurl4.symbols',
    'libcurl4-gnutls.symbols',
    install_dir : tests_dir / 'libcurl-compat' / '4',
  )
endif
