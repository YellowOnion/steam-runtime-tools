# Copyright 2021 Collabora Ltd.
# SPDX-License-Identifier: MIT

foreach suffix : ['', '-gnutls']
  # A mock implementation of libcurl with both CURL_OPENSSL_3 or
  # CURL_GNUTLS_3 and CURL_OPENSSL_4 or CURL_GNUTLS_4 symbols, used
  # during unit testing. No known distribution's libcurl actually
  # does this.
  shared_library(
    'curl' + suffix,
    'mock-both-curl@0@.c'.format(suffix),
    link_args : [
      '-Wl,--version-script,@0@/../../both@1@.versionscript'.format(
        meson.current_source_dir(),
        suffix,
      ),
    ],
    override_options : ['b_sanitize=none'],
    version : '4',
    soversion : '4',
    install : get_option('installed_tests'),
    install_dir : tests_dir / 'libcurl-compat' / 'both' / multiarch,
  )
endforeach
