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
    'architecture-internal.h',
    'architecture.c',
    'cpu-feature-internal.h',
    'cpu-feature.c',
    'desktop-entry-internal.h',
    'desktop-entry.c',
    'glib-backports.c',
    'glib-backports-internal.h',
    'graphics-internal.h',
    'graphics.c',
    'json-glib-backports.c',
    'json-glib-backports-internal.h',
    'library-internal.h',
    'library.c',
    'locale-internal.h',
    'locale.c',
    'os-internal.h',
    'os.c',
    'resolve-in-sysroot-internal.h',
    'resolve-in-sysroot.c',
    'runtime-internal.h',
    'runtime.c',
    'steam-internal.h',
    'steam.c',
    'system-info.c',
    'utils-internal.h',
    'utils.c',
    'xdg-portal.c',
]

libsteamrt_public_headers = [
    'architecture.h',
    'cpu-feature.h',
    'desktop-entry.h',
    'graphics.h',
    'library.h',
    'locale.h',
    'macros.h',
    'runtime.h',
    'steam-runtime-tools.h',
    'steam.h',
    'system-info.h',
    'utils.h',
    'xdg-portal.h',
]

enums = gnome.mkenums_simple(
  'enums',
  decorator : '_SRT_PUBLIC',
  header_prefix : '#include <steam-runtime-tools/macros.h>',
  install_dir : join_paths(get_option('includedir'), 'steam-runtime-tools-' + api_major, 'steam-runtime-tools'),
  install_header : true,
  sources : libsteamrt_public_headers,
)

install_headers(
  libsteamrt_public_headers,
  subdir : join_paths('steam-runtime-tools-' + api_major, 'steam-runtime-tools'),
)

libsteamrt_generated_headers_dep = declare_dependency(
  include_directories : project_include_dirs,
  sources : [enums[1]],
)
libsteamrt_static = static_library(
  'steam-runtime-tools-internal',
  libsteamrt_sources + libsteamrt_public_headers + enums,
  c_args : [
    '-DG_LOG_DOMAIN="' + meson.project_name() + '"',
    '-D_SRT_SONAME="libsteam-runtime-tools-' + api_major + '.so.' + abi_major + '"',
    '-D_SRT_MULTIARCH="' + multiarch + '"',
    '-D_SRT_API_MAJOR="' + api_major + '"',
  ],
  include_directories : project_include_dirs,
  dependencies : [
    libdl,
    threads,
    libelf,
    gio_unix,
    glib,
    gobject,
    libglnx_dep,
    json_glib,
    libsteamrt_generated_headers_dep,
  ],
  install : false,
)
libsteamrt = library(
  'steam-runtime-tools-' + api_major,
  dependencies : [
    libdl,
    threads,
    libelf,
    gio_unix,
    glib,
    gobject,
    libglnx_dep,
    json_glib,
    libsteamrt_generated_headers_dep,
  ],
  objects : [libsteamrt_static.extract_all_objects()],
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
  dependencies : libsteamrt_generated_headers_dep,
  include_directories : project_include_dirs,
  link_with : libsteamrt,
)
libsteamrt_static_dep = declare_dependency(
  dependencies : libsteamrt_generated_headers_dep,
  include_directories : project_include_dirs,
  link_with : libsteamrt_static,
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
  )
endif

# vim:set sw=2 sts=2 et:
