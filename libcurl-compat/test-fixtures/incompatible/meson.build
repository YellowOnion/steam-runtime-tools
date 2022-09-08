# Copyright 2022 Collabora Ltd.
# SPDX-License-Identifier: MIT

foreach suffix : ['', '-gnutls']
  # A mock implementation of libcurl with neither CURL_OPENSSL_4 nor
  # CURL_GNUTLS_4 symbols, used during unit testing.
  mock_libcurl = shared_library(
    'curl' + suffix,
    'mock-curl.c',
    link_args : [
      '-Wl,--version-script,@0@/incompatible.versionscript'.format(
        meson.current_source_dir(),
      ),
    ],
    override_options : ['b_sanitize=none'],
    version : '4',
    soversion : '4',
    install : get_option('installed_tests'),
    install_dir : tests_dir / 'libcurl-compat' / 'incompatible' / multiarch,
  )
endforeach
