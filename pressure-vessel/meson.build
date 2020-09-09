# Copyright © 2019-2020 Collabora Ltd.
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

conf_data = configuration_data()
conf_data.set_quoted('VERSION', version)

configure_file(
  input : 'config.h.in',
  output : '_pressure-vessel-config.h',
  configuration : conf_data,
)
pv_c_args = ['-include', '_pressure-vessel-config.h']

scripts = [
  'pressure-vessel-locale-gen',
  'pressure-vessel-test-ui',
  'pressure-vessel-unruntime',
]

foreach script : scripts
  install_data(
    script,
    install_dir : get_option('bindir'),
  )
endforeach

# Headers to scan for enum/flags types.
headers = [
  'resolve-in-sysroot.h',
  'runtime.h',
]

launcher1 = gnome.gdbus_codegen(
  'launcher1',
  sources : 'com.steampowered.PressureVessel.Launcher1.xml',
  interface_prefix : 'com.steampowered.PressureVessel.',
  namespace : 'Pv',
)

enums = gnome.mkenums_simple(
  'enumtypes',
  sources : headers,
)

pressure_vessel_utils = static_library(
  'pressure-vessel-utils',
  sources : [
    'bwrap-lock.c',
    'bwrap-lock.h',
    'elf-utils.c',
    'elf-utils.h',
    'flatpak-bwrap.c',
    'flatpak-bwrap-private.h',
    'flatpak-utils-base.c',
    'flatpak-utils-base-private.h',
    'flatpak-utils.c',
    'flatpak-utils-private.h',
    'glib-backports.c',
    'glib-backports.h',
    'resolve-in-sysroot.c',
    'resolve-in-sysroot.h',
    'tree-copy.c',
    'tree-copy.h',
    'utils.c',
    'utils.h',
  ],
  c_args : pv_c_args,
  dependencies : [
    threads,
    gio_unix,
    libelf,
    libglnx.get_variable('libglnx_dep'),
  ],
  include_directories : project_include_dirs,
  install: false,
)

launcher_codegen = static_library(
  'pressure-vessel-launcher-codegen',
  sources : launcher1,
  dependencies : [
    gio_unix,
  ],
  c_args : silence_gdbus_codegen_warnings + pv_c_args,
  include_directories : project_include_dirs,
  install: false,
)

executable(
  'pressure-vessel-adverb',
  sources : [
    'adverb.c',
    'wrap-interactive.c',
    'wrap-interactive.h',
  ],
  c_args : pv_c_args,
  dependencies : [
    threads,
    gio_unix,
    libglnx.get_variable('libglnx_dep'),
  ],
  link_with : [
    pressure_vessel_utils,
  ],
  include_directories : project_include_dirs,
  install : true,
  install_dir : get_option('bindir'),
  build_rpath : '${ORIGIN}/../' + get_option('libdir'),
  install_rpath : '${ORIGIN}/../' + get_option('libdir'),
)

executable(
  'pressure-vessel-launch',
  sources: [
    'launch.c',
  ],
  c_args : pv_c_args,
  dependencies : [
    threads,
    gio_unix,
    libglnx.get_variable('libglnx_dep'),
  ],
  link_with : [
    pressure_vessel_utils,
  ],
  include_directories : project_include_dirs,
  install : true,
  install_dir : get_option('bindir'),
  build_rpath : '${ORIGIN}/../' + get_option('libdir'),
  install_rpath : '${ORIGIN}/../' + get_option('libdir'),
)

executable(
  'pressure-vessel-launcher',
  sources: [
    'launcher.c',
  ],
  c_args : pv_c_args,
  dependencies : [
    threads,
    gio_unix,
    libglnx.get_variable('libglnx_dep'),
  ],
  link_with : [
    launcher_codegen,
    pressure_vessel_utils,
  ],
  include_directories : project_include_dirs,
  install : true,
  install_dir : get_option('bindir'),
  build_rpath : '${ORIGIN}/../' + get_option('libdir'),
  install_rpath : '${ORIGIN}/../' + get_option('libdir'),
)

executable(
  'pressure-vessel-wrap',
  sources : [
    'bwrap.c',
    'bwrap.h',
    'flatpak-common-types-private.h',
    'flatpak-context.c',
    'flatpak-context-private.h',
    'flatpak-exports.c',
    'flatpak-exports-private.h',
    'flatpak-run.c',
    'flatpak-run-private.h',
    'runtime.c',
    'runtime.h',
    'wrap.c',
  ] + enums,
  c_args : pv_c_args,
  dependencies : [
    steam_runtime_tools,
    gio_unix,
    xau,
    libglnx.get_variable('libglnx_dep'),
  ],
  link_with : [
    pressure_vessel_utils,
  ],
  include_directories : project_include_dirs,
  install : true,
  install_dir : get_option('bindir'),
  build_rpath : '${ORIGIN}/../' + get_option('libdir'),
  install_rpath : '${ORIGIN}/../' + get_option('libdir'),
)

executable(
  'pressure-vessel-try-setlocale',
  sources : [
    'try-setlocale.c',
  ],
  c_args : pv_c_args,
  include_directories : project_include_dirs,
  install : true,
  install_dir : get_option('bindir'),
)

if get_option('srcdir') != ''
  conf_data = configuration_data()
  conf_data.set('prefix', get_option('prefix'))
  conf_data.set('python', python.path())
  conf_data.set('sh', sh.path())
  conf_data.set('srcdir', get_option('srcdir'))
  conf_data.set('version', version)
  install_data(
    configure_file(
      input : 'build-relocatable-install.in',
      output : 'pressure-vessel-build-relocatable-install',
      configuration : conf_data,
    ),
    install_dir : get_option('bindir'),
    install_mode : 'rwxr-xr-x',
  )
  meson.add_install_script(
    python.path(),
    meson.current_source_dir() / 'copy-source-code.py',
    '--srcdir', meson.current_source_dir(),
    '--prefix', get_option('prefix'),
    '--set-version', version,
    get_option('srcdir'),
  )
endif

# vim:set sw=2 sts=2 et:
