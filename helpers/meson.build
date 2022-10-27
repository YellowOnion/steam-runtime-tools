# Copyright © 2019-2021 Collabora Ltd.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

common_test_env_vars += {'SRT_HELPERS_PATH': meson.current_build_dir()}

executable(
  multiarch + '-true',
  'true.c',
  include_directories : project_include_dirs,
  install : true,
  install_dir : pkglibexecdir,
  # Deliberately no RPATH/RUNPATH here: it's unnecessary because this
  # executable has no dependencies.
)

executable(
  multiarch + '-check-locale',
  'check-locale.c',
  include_directories : project_include_dirs,
  dependencies : [glib, gio_unix, libglnx_dep, json_glib],
  install : true,
  install_dir : pkglibexecdir,
  # Use json-glib and GLib from the adjacent libdir, ignoring LD_LIBRARY_PATH
  build_rpath : pkglibexec_rpath,
  install_rpath : pkglibexec_rpath,
)

executable(
  multiarch + '-inspect-library',
  'inspect-library.c',
  dependencies : [libdl],
  include_directories : project_include_dirs,
  install : true,
  install_dir : pkglibexecdir,
  # Deliberately no RPATH/RUNPATH here: we want to determine whether
  # a game with no special linking would be able to load a library.
)

executable(
  multiarch + '-check-va-api',
  'check-va-api.c',
  dependencies : [libva, libva_x11, xlib],
  include_directories : project_include_dirs,
  install : true,
  install_dir : pkglibexecdir,
  # Deliberately no RPATH/RUNPATH here: we want to determine whether
  # a game with no special linking would be able to use VA-API.
)

executable(
  multiarch + '-check-vdpau',
  'check-vdpau.c',
  dependencies : [xlib, vdpau],
  include_directories : project_include_dirs,
  install : true,
  install_dir : pkglibexecdir,
  # Deliberately no RPATH/RUNPATH here: we want to determine whether
  # a game with no special linking would be able to use VDPAU.
)

executable(
  multiarch + '-check-vulkan',
  'check-vulkan.c',
  dependencies : [glib, gio_unix, libglnx_dep, libsteamrt_static_dep, json_glib, vulkan, xcb],
  include_directories : project_include_dirs,
  install : true,
  install_dir : pkglibexecdir,
  # Use the adjacent libsteam-runtime-tools and json-glib, ignoring
  # LD_LIBRARY_PATH even if set
  build_rpath : pkglibexec_rpath,
  install_rpath : pkglibexec_rpath,
)

foreach shader : ['frag', 'vert']
  custom_target(
    shader + '.spv',
    build_by_default : true,
    command : [
      glslang_validator,
      '--target-env', 'vulkan1.0',
      '-o', '@OUTPUT@',
      '@INPUT@',
    ],
    input : 'shader.' + shader,
    output : shader + '.spv',
    install : true,
    install_dir : join_paths(
      get_option('prefix'),
      get_option('libexecdir'),
      'steam-runtime-tools-' + api_major,
      'shaders',
    )
  )
endforeach

executable(
  multiarch + '-check-gl',
  'check-gl.c',
  dependencies : [xlib, gl],
  include_directories : project_include_dirs,
  install : true,
  install_dir : join_paths(
    get_option('libexecdir'),
    'steam-runtime-tools-' + api_major,
  )
)

executable(
  multiarch + '-wflinfo',
  'wflinfo.c',
  c_args : silence_wflinfo_warnings,
  dependencies : [waffle_dep],
  include_directories : project_include_dirs,
  install : true,
  install_dir : pkglibexecdir,
  # Use the adjacent libwaffle
  build_rpath : pkglibexec_rpath,
  install_rpath : pkglibexec_rpath,
)

executable(
  multiarch + '-check-xdg-portal',
  'check-xdg-portal.c',
  include_directories : project_include_dirs,
  dependencies : [glib, gio_unix, libglnx_dep, libsteamrt_static_dep, json_glib],
  install : true,
  install_dir : pkglibexecdir,
  # Use the adjacent libsteam-runtime-tools and json-glib, ignoring
  # LD_LIBRARY_PATH even if set
  build_rpath : pkglibexec_rpath,
  install_rpath : pkglibexec_rpath,
)

executable(
  multiarch + '-detect-lib',
  'detect.c',
  c_args : [
    '-D_SRT_LIB_PATH="${ORIGIN}/' + multiarch + '/${LIB}/libidentify-lib.so"',
    '-D_SRT_FUNCTION="_srt_identify_lib"',
  ],
  dependencies : [libdl],
  include_directories : project_include_dirs,
  install : true,
  install_dir : pkglibexecdir,
)

executable(
  multiarch + '-detect-platform',
  'detect.c',
  c_args : [
    '-D_SRT_LIB_PATH="${ORIGIN}/' + multiarch + '/${PLATFORM}/libidentify-platform.so"',
    '-D_SRT_FUNCTION="_srt_identify_platform"',
  ],
  dependencies : [libdl],
  include_directories : project_include_dirs,
  install : true,
  install_dir : pkglibexecdir,
)

# Detect $LIB expanding to just lib (common case)
subdir('lib')

sizeof_pointer = c_compiler.sizeof('void*')

# Detect $LIB expanding to lib32 or lib64 (semi-common case)
if sizeof_pointer == 4
  subdir('lib32')
elif sizeof_pointer == 8
  subdir('lib64')
endif

if multiarch != ''
  # Detect $LIB expanding to lib/MULTIARCH (Debian, Ubuntu, fd.o SDK)
  subdir('lib/multiarch')

  # This is to support Ubuntu 12.04 where $LIB wrongly expanded to just
  # the multiarch
  subdir('multiarch')
endif

if host_machine.cpu_family() == 'x86'
  expected_platform = 'i686'
else
  # This is correct for at least aarch64 and x86_64, and a reasonable guess
  # everywhere else
  expected_platform = host_machine.cpu_family()
endif

# We can inline one instance of identify-platform here. All the others
# must be in a subdirectory due to Meson limitations.
shared_module(
  'identify-platform',
  'identify-platform.c',
  c_args : ['-D_SRT_PLATFORM_VALUE="' + expected_platform + '"',],
  install : true,
  install_dir : join_paths(
    pkglibexecdir,
    multiarch,
    expected_platform,
  )
)

# Detect special-case $PLATFORM values on x86
if multiarch == 'i386-linux-gnu'
  subdir('i386')
  subdir('i486')
  subdir('i586')
elif multiarch == 'x86_64-linux-gnu'
  subdir('haswell')
  subdir('xeon_phi')
endif

if multiarch != ''
  # libcapsule doesn't currently have a Meson build system, so instead
  # of building it as a subproject, we build our own source file that
  # #includes everything we need from it.
  executable(
    multiarch + '-capsule-capture-libs',
    'capture-libs.c',
    c_args : [
      '-DHOST_PREFIX="' + multiarch + '-"',
      '-DPACKAGE_TARNAME="' + meson.project_name() + '"',
      '-DPACKAGE_VERSION="' + meson.project_version() + '"',
    ],
    dependencies: [libdl, libelf],
    include_directories : project_include_dirs,
    install : true,
    install_dir : pkglibexecdir,
    # Use libelf from the adjacent libdir, ignoring LD_LIBRARY_PATH
    build_rpath : pkglibexec_rpath,
    install_rpath : pkglibexec_rpath,
  )
endif

# vim:set sw=2 sts=2 et:
