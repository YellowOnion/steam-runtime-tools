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
  install : true,
  install_dir : join_paths(
    get_option('libexecdir'),
    'steam-runtime-tools-' + api_major,
  )
)

executable(
  multiarch + '-inspect-library',
  'inspect-library.c',
  dependencies : [libdl],
  c_args : [
      '-D_GNU_SOURCE',
    ],
  install : true,
  install_dir : join_paths(
    get_option('libexecdir'),
    'steam-runtime-tools-' + api_major,
  )
)

glib_dep = dependency('glib-2.0')
x11_dep = dependency('x11')

executable(
  multiarch + '-check-gl',
  'check-gl.c',
  include_directories : project_include_dirs,
  install : true,
  install_dir : join_paths(
    get_option('libexecdir'),
    'steam-runtime-tools-' + api_major,
  ),
  dependencies : [ glib_dep, 
    x11_dep, 
    dependency('gl'), 
    dependency('xcomposite')
  ]
)

executable(
  multiarch + '-check-gles',
  'check-gles.c',
  include_directories : project_include_dirs,
  install : true,
  install_dir : join_paths(
    get_option('libexecdir'),
    'steam-runtime-tools-' + api_major,
  ),
  dependencies: [ glib_dep,
    x11_dep,
    dependency('egl'),
    dependency('glesv2')
  ]
)

# vim:set sw=2 sts=2 et:
