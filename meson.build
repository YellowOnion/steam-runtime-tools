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

project(
  'steam-runtime-tools', 'c',
  version : '0.20200306.0',
  default_options: [
    'c_std=c99',
    'cpp_std=c++11',
    'warning_level=2',
  ],
  meson_version : '>=0.49.0',
)
add_languages('cpp')

api_major = '0'
abi_major = '0'
abi_minor = '20200306.0'

pkg = import('pkgconfig')
gnome = import('gnome')

if get_option('man')
  pandoc = find_program('pandoc', required : true)

  if run_command(pandoc, [
    '-f', 'markdown-smart',
    '-t', 'man',
    '/dev/null',
  ]).returncode() == 0
    pandoc_markdown_nosmart = 'markdown-smart'
  else
    pandoc_markdown_nosmart = 'markdown'
  endif
endif

glslang_validator = find_program('glslangValidator', required : true)

warning_flags = [
    '-Warray-bounds',
    '-Wcast-align',
    '-Wdouble-promotion',
    '-Wduplicated-branches',
    '-Wduplicated-cond',
    '-Wformat-nonliteral',
    '-Wformat-security',
    '-Wformat=2',
    '-Winit-self',
    '-Wlogical-op',
    '-Wmissing-declarations',
    '-Wmissing-format-attribute',
    '-Wmissing-include-dirs',
    '-Wmissing-noreturn',
    '-Wnull-dereference',
    '-Wpacked',
    '-Wpointer-arith',
    '-Wredundant-decls',
    '-Wrestrict',
    '-Wreturn-type',
    '-Wshadow',
    '-Wstrict-aliasing',
    '-Wswitch-default',
    '-Wswitch-enum',
    '-Wundef',
    '-Wunused-but-set-variable',
    '-Wwrite-strings',
]
warning_cflags = warning_flags + [
    '-Wimplicit-function-declaration',
    '-Wjump-misses-init',
    '-Wmissing-prototypes',
    '-Wnested-externs',
    '-Wold-style-definition',
    '-Wstrict-prototypes',
]
warning_cxxflags = warning_flags + [
]

no_warning_flags = [
    'inline',
    'missing-field-initializers',
    'pedantic',
    'sign-compare',
    'unused-local-typedefs',
    'unused-parameter',
]
no_warning_cflags = no_warning_flags + [
    'declaration-after-statement',
]
no_warning_cxxflags = no_warning_flags + [
]

c_compiler = meson.get_compiler('c')
cpp_compiler = meson.get_compiler('cpp')
supported_warning_cflags = c_compiler.get_supported_arguments(warning_cflags)
add_project_arguments(supported_warning_cflags, language : 'c')
supported_warning_cxxflags = cpp_compiler.get_supported_arguments(warning_cxxflags)
add_project_arguments(supported_warning_cxxflags, language : 'cpp')

foreach flag : no_warning_cflags
  supported_no_warning_cflags = c_compiler.get_supported_arguments([
    '-Wno-error=' + flag,
    '-Wno-' + flag,
  ])
  add_project_arguments(supported_no_warning_cflags, language : 'c')
endforeach

foreach flag : no_warning_cxxflags
  supported_no_warning_cxxflags = cpp_compiler.get_supported_arguments([
    '-Wno-error=' + flag,
    '-Wno-' + flag,
  ])
  add_project_arguments(supported_no_warning_cxxflags, language : 'cpp')
endforeach

add_project_link_arguments(
  c_compiler.get_supported_link_arguments(
    '-Wl,-z,origin',
    # Generate RPATH in preference to RUNPATH, even if RUNPATH is the
    # linker's default. We want RPATH here, because it's used recursively
    # for both direct and indirect dependencies: in particular we need
    # to pick up the whole GLib family (GLib, GObject, GIO) from the same
    # location. (It also bypasses LD_LIBRARY_PATH.)
    '-Wl,-z,--disable-new-dtags',
  ),
  language : 'c',
)

conf_data = configuration_data()
conf_data.set('_GNU_SOURCE', '1')
conf_data.set_quoted('VERSION', meson.project_version())

configure_file(
  output : '_steam-runtime-tools-config.h',
  configuration : conf_data,
)

add_project_arguments(
  ['-include', '_steam-runtime-tools-config.h'],
  language : 'c',
)
add_project_arguments(
  ['-include', '_steam-runtime-tools-config.h'],
  language : 'cpp',
)

glib = dependency(
  'glib-2.0',
  version : '>= 2.32',
)
glib_tap_support = dependency(
  'glib-2.0',
  version : '>= 2.38',
  required : false,
)
gobject = dependency(
  'gobject-2.0',
  version : '>= 2.32',
)
gio = dependency(
  'gio-2.0',
  version : '>= 2.32',
)
gio_unix = dependency(
  'gio-unix-2.0',
  version : '>= 2.32',
)
json_glib = dependency(
  'json-glib-1.0',
  version : '>= 1.0',
)
threads = dependency(
  'threads',
)

libelf = dependency(
  'libelf',
  required: false,
)
if not libelf.found()
  libelf = c_compiler.find_library('elf', required: true)
endif

libva = dependency(
  'libva',
)

libva_x11 = dependency(
  'libva-x11',
)

vdpau = dependency(
  'vdpau',
)

vulkan = dependency(
  'vulkan',
)

xcb = dependency(
  'xcb',
)

gl = dependency(
  'gl'
)

xlib = dependency(
  'x11'
)

libdl = c_compiler.find_library('dl', required : false)

project_include_dirs = include_directories('.')

# Relative path from get_option('prefix') to our private library directory
pkglibdir = join_paths(
  get_option('libdir'),
  'steam-runtime-tools-' + api_major,
)

# RPATH for executables installed in get_option('bindir').
# This assumes that bindir is a single directory component (normally
# 'bin'), so that ../ gets us from get_option('bindir') to the prefix,
# and that libdir is relative.
bin_rpath = '${ORIGIN}/../' + pkglibdir

# Relative path from get_option('prefix') to our private libexec directory
pkglibexecdir = join_paths(
  get_option('libexecdir'),
  'steam-runtime-tools-' + api_major,
)

# RPATH for executables installed in pkglibexecdir.
# This assumes that libexecdir is a single directory component (normally
# 'libexec'), so that ../../ gets us from pkglibexecdir to the prefix,
# and that libdir is relative
pkglibexec_rpath = '${ORIGIN}/../../' + pkglibdir

if get_option('multiarch_tuple') != ''
  multiarch = get_option('multiarch_tuple')
elif host_machine.cpu_family() == 'x86_64'
  multiarch = 'x86_64-linux-gnu'
elif host_machine.cpu_family() == 'x86'
  multiarch = 'i386-linux-gnu'
else
  multiarch = ''
endif

subdir('steam-runtime-tools')

if multiarch != ''
  subdir('helpers')
endif

subdir('bin')
subdir('docs')
subdir('tests')

# vim:set sw=2 sts=2 et:
