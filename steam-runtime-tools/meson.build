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

libsteamrt_sources = [
    'architecture.c',
    'library-internal.h',
    'library.c',
    'utils-internal.h',
    'utils.c',
]

libsteamrt_public_headers = [
    'architecture.h',
    'library.h',
    'steam-runtime-tools.h',
]

enums = gnome.mkenums_simple(
  'enums',
  install_dir : join_paths(get_option('includedir'), 'steam-runtime-tools-' + api_major, 'steam-runtime-tools'),
  install_header : true,
  sources : libsteamrt_public_headers,
)

install_headers(
  libsteamrt_public_headers,
  subdir : join_paths('steam-runtime-tools-' + api_major, 'steam-runtime-tools'),
)

libsteamrt = library(
  'steam-runtime-tools-' + api_major,
  libsteamrt_sources + libsteamrt_public_headers + enums,
  c_args : [
    '-DG_LOG_DOMAIN="' + meson.project_name() + '"',
    '-D_SRT_COMPILATION',
    '-D_GNU_SOURCE',
    '-D_SRT_SONAME="libsteam-runtime-tools-' + api_major + '.so.' + abi_major + '"',
    '-D_SRT_MULTIARCH="' + multiarch + '"',
    '-D_SRT_API_MAJOR="' + api_major + '"',
  ],
  include_directories : project_include_dirs,
  dependencies : [glib, gobject, libdl, json_glib],
  soversion : abi_major,
  version : abi_major + '.' + abi_minor,
  install : true,
)
pkg.generate(
  description : 'Steam runtime tools library',
  libraries : [libsteamrt],
  name : 'steam-runtime-tools-' + api_major,
  requires_private : ['glib-2.0', 'gobject-2.0'],
  subdirs : 'steam-runtime-tools-' + api_major,
  version : meson.project_version(),
)
libsteamrt_dep = declare_dependency(
  include_directories : project_include_dirs,
  link_with : libsteamrt,
  sources : libsteamrt_public_headers + [enums[1]],
)

if get_option('introspection')
  gnome.generate_gir(
    libsteamrt,
    sources : libsteamrt_sources + libsteamrt_public_headers,
    namespace : 'SteamRuntimeTools',
    nsversion : api_major,
    identifier_prefix : 'Srt',
    symbol_prefix : 'srt',
    export_packages : 'steam-runtime-tools-' + api_major,
    includes : ['GLib-2.0', 'GObject-2.0'],
    install : true,
    extra_args : ['-D_SRT_COMPILATION'],
  )
endif

# vim:set sw=2 sts=2 et:
