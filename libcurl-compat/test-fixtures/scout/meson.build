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
    version : '4',
    soversion : '4',
    install : get_option('installed_tests'),
    install_dir : tests_dir / 'libcurl-compat' / 'scout' / multiarch,
  )
endforeach

if get_option('installed_tests')
  install_data(
    'libcurl3.symbols',
    'libcurl3-gnutls.symbols',
    install_dir : tests_dir / 'libcurl-compat' / 'scout',
  )
endif
