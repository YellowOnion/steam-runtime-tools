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
  dependencies : [glib, gio_unix, libglnx_dep, libsteamrt_dep, json_glib, vulkan, xcb],
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
  'check-gl.cpp',
  dependencies : [xlib, gl],
  include_directories : project_include_dirs,
  install : true,
  install_dir : join_paths(
    get_option('libexecdir'),
    'steam-runtime-tools-' + api_major,
  )
)

executable(
  multiarch + '-check-xdg-portal',
  'check-xdg-portal.c',
  include_directories : project_include_dirs,
  dependencies : [glib, gio_unix, libglnx_dep, libsteamrt_dep, json_glib],
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

# Due to some Meson's limitations it is not possible to build multiple times
# the same library, with the same name, and place it in a different directory.
# For this reason we generate the required meson.build files in subdirectories
# to avoid conflicts.
# https://github.com/mesonbuild/meson/issues/2320
# https://github.com/mesonbuild/meson/issues/4019

meson_library_template = '''
shared_module(
  'identify-lib',
  '@0@',
  c_args : ['-D_SRT_LIB_VALUE="@1@"',],
  install : true,
  install_dir : join_paths(
    pkglibexecdir,
    multiarch,
    '@1@',
  )
)
'''

# This was supposed to be a dictionary but meson < 0.53.0 doesn't support
# the usage of variables containing a string value as keys.
components = [['lib', '../identify-lib.c']]

if multiarch != ''
  components += [
    ['lib' / multiarch, '../../identify-lib.c'],
    # This is to support Ubuntu 12.04 where $LIB wrongly expanded to just
    # the multiarch
    [multiarch, '../identify-lib.c'],
  ]
endif

if multiarch == 'i386-linux-gnu'
  components += [['lib32', '../identify-lib.c']]
  platform_dirs = ['i386', 'i486', 'i586', 'i686']
elif multiarch == 'x86_64-linux-gnu'
  components += [['lib64', '../identify-lib.c']]
  platform_dirs = ['x86_64', 'haswell', 'xeon_phi']
endif

foreach component : components
  subdir = '_generated_' + component[0]
  meson_library = meson_library_template.format(component[1], component[0])
  run_command(
    python,
    'create-meson-in-subdir.py',
    meson.current_source_dir() / subdir,
    meson_library,
    check : true,
  )
  subdir(subdir)
endforeach

meson_platform_template = '''
shared_module(
  'identify-platform',
  '../identify-platform.c',
  c_args : ['-D_SRT_PLATFORM_VALUE="@0@"',],
  install : true,
  install_dir : join_paths(
    pkglibexecdir,
    multiarch,
    '@0@',
  )
)
'''

foreach platform_dir : platform_dirs
  generated_dir = '_generated_' + platform_dir
  meson_platform = meson_platform_template.format(platform_dir)
  run_command(
    python,
    'create-meson-in-subdir.py',
    meson.current_source_dir() / generated_dir,
    meson_platform,
    check : true,
  )
  subdir(generated_dir)
endforeach

# vim:set sw=2 sts=2 et:
