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
  'steam-runtime-system-info',
  'system-info.c',
  c_args : srt_c_args,
  dependencies : [gio_unix, glib, json_glib, libglnx_dep, libsteamrt_static_dep],
  install : true,
  # Use the adjacent json-glib, ignoring LD_LIBRARY_PATH even if set
  build_rpath : bin_rpath,
  install_rpath : bin_rpath,
)

executable(
  'steam-runtime-check-requirements',
  'check-requirements.c',
  c_args : srt_c_args,
  dependencies : [glib, gobject, libsteamrt_static_dep],
  install : true,
  # Use the adjacent GLib, ignoring LD_LIBRARY_PATH even if set
  build_rpath : bin_rpath,
  install_rpath : bin_rpath,
)

executable(
  'steam-runtime-identify-library-abi',
  'identify-library-abi.c',
  c_args : srt_c_args,
  dependencies : [glib, gio_unix, libglnx_dep, libsteamrt_static_dep],
  install : true,
  # Use GLib from the adjacent libdir, ignoring LD_LIBRARY_PATH
  build_rpath : bin_rpath,
  install_rpath : bin_rpath,
)

executable(
  'steam-runtime-input-monitor',
  'input-monitor.c',
  c_args : srt_c_args,
  dependencies : [gio_unix, glib, gobject, json_glib, libglnx_dep, libsteamrt_static_dep],
  install : true,
  # Use the adjacent json-glib, ignoring LD_LIBRARY_PATH even if set
  build_rpath : bin_rpath,
  install_rpath : bin_rpath,
)

executable(
  'steam-runtime-launch-client',
  sources: [
    'launch-client.c',
  ],
  c_args : srt_c_args,
  dependencies : [
    threads,
    gio_unix,
    libglnx_dep,
    libsteamrt_static_dep,
  ],
  # Needs pv_include_dirs for flatpak-*.h
  include_directories : pv_include_dirs,
  install : true,
  build_rpath : bin_rpath,
  install_rpath : bin_rpath,
)

executable(
  'steam-runtime-launcher-service',
  sources: [
    'launcher-service.c',
    '../pressure-vessel/flatpak-utils-base.c',
  ],
  c_args : srt_c_args,
  dependencies : [
    launcher_codegen_dep,
    threads,
    gio_unix,
    libglnx_dep,
    libsteamrt_static_dep,
  ],
  # Needs pv_include_dirs for flatpak-*.h
  include_directories : pv_include_dirs,
  install : true,
  build_rpath : bin_rpath,
  install_rpath : bin_rpath,
)

executable(
  'steam-runtime-steam-remote',
  'steam-remote.c',
  c_args : srt_c_args,
  dependencies : [glib, gio_unix, libglnx_dep, libsteamrt_static_dep],
  install : true,
  # Use GLib from the adjacent libdir, ignoring LD_LIBRARY_PATH
  build_rpath : bin_rpath,
  install_rpath : bin_rpath,
)

executable(
  'steam-runtime-urlopen',
  'urlopen.c',
  c_args : srt_c_args,
  dependencies : [glib, gio_unix, libglnx_dep, libsteamrt_static_dep],
  install : true,
  # Use GLib from the adjacent libdir, ignoring LD_LIBRARY_PATH
  build_rpath : bin_rpath,
  install_rpath : bin_rpath,
)

sh_scripts = [
  'launch-options',
]

foreach script : sh_scripts
  install_data(
    script + '.sh',
    rename : ['steam-runtime-' + script],
    install_dir : get_option('bindir'),
    install_mode : 'rwxr-xr-x',
  )
endforeach

libexec_scripts = [
  'launch-options.py',
]

foreach script : libexec_scripts
  install_data(
    script,
    install_dir : pkglibexecdir,
    install_mode : 'rwxr-xr-x',
  )
endforeach

if build_man_pages
  foreach bin_name : [
    'check-requirements',
    'identify-library-abi',
    'input-monitor',
    'launch-client',
    'launch-options',
    'launcher-service',
    'steam-remote',
    'system-info',
    'urlopen',
  ]
    custom_target(
      'steam-runtime-' + bin_name + '.1',
      build_by_default : true,
      command : [
        pandoc,
        '-s',
        '-o', '@OUTPUT@',
        '-f', pandoc_markdown_nosmart,
        '-t', 'man',
        '@INPUT@',
      ],
      input : bin_name + '.md',
      output : 'steam-runtime-' + bin_name + '.1',
      install : true,
      install_dir : join_paths(get_option('prefix'), get_option('mandir'), 'man1'),
    )
  endforeach
endif

# vim:set sw=2 sts=2 et:
