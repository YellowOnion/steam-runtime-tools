# Copyright © 2019 Collabora Ltd.
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
  'check-vulkan.cpp',
  dependencies : [vulkan, xcb],
  include_directories : project_include_dirs,
  install : true,
  install_dir : join_paths(
    get_option('libexecdir'),
    'steam-runtime-tools-' + api_major,
  )
  # Deliberately no RPATH/RUNPATH here: we want to determine whether
  # a game with no special linking would be able to use libvulkan
  # and its associated drivers successfully.
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

# vim:set sw=2 sts=2 et:
