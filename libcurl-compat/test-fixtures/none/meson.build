# Copyright 2021 Collabora Ltd.
# SPDX-License-Identifier: MIT

foreach suffix : ['', '-gnutls']
  # A mock implementation of libcurl built without versioned symbols.
  # This resembles Arch Linux's libcurl-compat and libcurl-gnutls,
  # and is used during unit testing.
  mock_libcurl = shared_library(
    'curl' + suffix,
    'mock-curl.c',
    override_options : ['b_sanitize=none'],
    version : '4',
    soversion : '4',
    install : get_option('installed_tests'),
    install_dir : tests_dir / 'libcurl-compat' / 'none' / multiarch,
  )

  # Test executable that calls curl_version() with an unspecified
  # version, emulating a legacy program compiled against a very old
  # system libcurl
  executable(
    multiarch + '-steamrt-print-libcurl@0@-version'.format(suffix),
    '../print-version.c',
    link_with : mock_libcurl,
    override_options : ['b_sanitize=none'],
    install : get_option('installed_tests'),
    install_dir : tests_dir / 'libcurl-compat' / 'none',
  )
endforeach
