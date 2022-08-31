# Copyright 2021-2022 Collabora Ltd.
# SPDX-License-Identifier: MIT

foreach suffix : ['', '-gnutls']
  # A mock implementation of libcurl that exists only to give
  # libsteam-runtime-shim-libcurl.so.4 the desired dependency,
  # DT_NEEDED: libsteam-runtime-system-libcurl.so.4.
  # Not actually installed into the public library search path:
  # at runtime, we substitute a symlink to the system's real
  # libcurl[-gnutls].so.4, if any.
  mock_sys_lib = shared_library(
    'steam-runtime-system-libcurl' + suffix,
    'mock-curl.c',
    link_args : [
      '-Wl,--version-script,@0@/4@1@.versionscript'.format(
        meson.current_source_dir(),
        suffix,
      ),
    ],
    # We disable AddressSanitizer and all similar instrumentation for
    # all these mock libraries: it seems it can get in the way by changing
    # the libraries' dependencies.
    override_options : ['b_sanitize=none'],
    version : '4',
    soversion : '4',
    install : false,
  )

  # A mock implementation of libcurl that exists only to give
  # libsteam-runtime-shim-libcurl.so.4 the desired dependency,
  # DT_NEEDED: libsteam-runtime-our-libcurl.so.4.
  # Not actually installed into the public library search path:
  # at runtime, we substitute a symlink to the Steam Runtime's real
  # libcurl[-gnutls].so.4.
  mock_our_lib = shared_library(
    'steam-runtime-our-libcurl' + suffix,
    'mock-curl.c',
    link_args : [
      '-Wl,--version-script,@0@/3@1@.versionscript'.format(
        meson.current_source_dir(),
        suffix,
      ),
    ],
    override_options : ['b_sanitize=none'],
    version : '4',
    soversion : '4',
    install : false,
  )

  # Shim library that loads both -system and -our versions of libcurl,
  # in that order. We have to be rather specific about how we link this,
  # which is why it's open-coded as a custom target instead of using
  # the ordinary Meson shared_library:
  #
  # 1. It cannot be linked with -Wl,--as-needed, because if it was,
  #    the two copies of libcurl wouldn't generate DT_NEEDED headers
  #    (it doesn't really use either of them).
  #
  # 2. -lsteam-runtime-system-libcurl needs to come before
  #    -lsteam-runtime-our-libcurl, so that unversioned references to
  #    cURL symbols are resolved as pointing to the system libcurl;
  #    but Meson doesn't document any guarantees about link order,
  #    so we can't guarantee that a later version won't canonicalize
  #    the order.
  #
  # 3. We don't want the shim to ever get a RPATH or RUNPATH, but Meson
  #    normally adds a ${ORIGIN}-based RPATH/RUNPATH, then strips
  #    it during install.
  #
  # This is not how to do high-quality ABIs, but we're stuck with it
  # for historical and cross-distro compatibility reasons.
  custom_target(
    'steam-runtime-shim-libcurl' + suffix,
    output : 'libsteam-runtime-shim-libcurl@0@.so.4'.format(suffix),
    input : [
      'empty.c',
      'both@0@.versionscript'.format(suffix),
    ],
    command : [
      meson.get_compiler('c').cmd_array(),
      get_option('c_args'),
      '-shared',
      '-o', '@OUTPUT@',
      '@INPUT0@',
      '-Wl,-soname,libsteam-runtime-shim-libcurl@0@.so.4'.format(suffix),
      '-Wl,--no-as-needed',
      '-Wl,--version-script,@INPUT1@',
      '-L@0@'.format(meson.current_build_dir()),
      '-lsteam-runtime-system-libcurl' + suffix,
      '-lsteam-runtime-our-libcurl' + suffix,
    ],
    depends : [
      mock_sys_lib,
      mock_our_lib,
    ],
    install : true,
    install_dir : get_option('libdir'),
  )
endforeach

executable(
  'steam-runtime-libcurl-compat-setup',
  'compat-setup.c',
  c_args : srt_c_args,
  dependencies : [gio_unix, glib, libglnx_dep, libsteamrt_static_dep],
  install : true,
  # Use the adjacent GLib, ignoring LD_LIBRARY_PATH even if set
  build_rpath : bin_rpath,
  install_rpath : bin_rpath,
)

# The expected ABI happens to be the same for both architectures that
# we support, so we just install it twice
install_data(
  'libc6.symbols',
  'libcurl4-gnutls.symbols',
  'libcurl4.symbols',
  install_dir : 'lib/steamrt/libcurl-compat/expectations/i386-linux-gnu',
)
install_data(
  'libc6.symbols',
  'libcurl4-gnutls.symbols',
  'libcurl4.symbols',
  install_dir : 'lib/steamrt/libcurl-compat/expectations/x86_64-linux-gnu',
)
