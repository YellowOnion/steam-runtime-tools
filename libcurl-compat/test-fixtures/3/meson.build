# Copyright 2021 Collabora Ltd.
# SPDX-License-Identifier: MIT

foreach suffix : ['', '-gnutls']
  # A mock implementation of libcurl with CURL_OPENSSL_3 or
  # CURL_GNUTLS_3 symbols, used during unit testing.
  mock_libcurl = shared_library(
    'curl' + suffix,
    'mock-curl.c',
    link_args : [
      '-Wl,--version-script,@0@/../../3@1@.versionscript'.format(
        meson.current_source_dir(),
        suffix,
      ),
    ],
    override_options : ['b_sanitize=none'],
    version : '3',
    soversion : '3',
    install : get_option('installed_tests'),
    install_dir : tests_dir / 'libcurl-compat' / '3' / multiarch,
  )

  # Test executable that calls curl_version@CURL_OPENSSL_3
  # or curl_version@CURL_GNUTLS_3, emulating a game compiled
  # against the real libcurl on scout (or a host OS with compatible ABI)
  executable(
    multiarch + '-steamrt-print-libcurl3@0@-version'.format(suffix),
    '../print-version.c',
    link_with : mock_libcurl,
    override_options : ['b_sanitize=none'],
    install : get_option('installed_tests'),
    install_dir : tests_dir / 'libcurl-compat' / '3',
  )
endforeach
