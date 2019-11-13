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
)

executable(
  multiarch + '-check-locale',
  'check-locale.c',
  include_directories : project_include_dirs,
  dependencies : [json_glib, glib],
  install : true,
  install_dir : pkglibexecdir,
)

executable(
  multiarch + '-inspect-library',
  'inspect-library.c',
  dependencies : [libdl],
  include_directories : project_include_dirs,
  install : true,
  install_dir : pkglibexecdir,
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
) 

custom_target(
  'frag.spv',
  build_by_default : true,
  command : [
    glslang_validator,
    '--target-env', 'vulkan1.0',
    '-o', '@OUTPUT@',
    '@INPUT@',
  ],
  input : 'shader.frag',
  output : 'frag.spv',
  install : true,
  install_dir : join_paths(
    get_option('prefix'),
    get_option('libexecdir'),
    'steam-runtime-tools-' + api_major,
    'shaders',
  )
)

custom_target(
  'vert.spv',
  build_by_default : true,
  command : [
    glslang_validator,
    '--target-env', 'vulkan1.0',
    '-o', '@OUTPUT@',
    '@INPUT@',
  ],
  input : 'shader.vert',
  output : 'vert.spv',
  install : true,
  install_dir : join_paths(
    get_option('prefix'),
    get_option('libexecdir'),
    'steam-runtime-tools-' + api_major,
    'shaders',
  )
)

# vim:set sw=2 sts=2 et:
