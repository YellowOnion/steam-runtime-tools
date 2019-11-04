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

project('steam-runtime-tools', 'c', 
        version : '0.20191024.0',
        default_options: ['cpp_std=c++11'])
add_languages('cpp')

api_major = '0'
abi_major = '0'
abi_minor = '20191024.0'

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

warning_cflags = [
    '-Wall',
    '-Wextra',
    '-Warray-bounds',
    '-Wcast-align',
    '-Wdeclaration-after-statement',
    '-Wdouble-promotion',
    '-Wduplicated-branches',
    '-Wduplicated-cond',
    '-Wformat-nonliteral',
    '-Wformat-security',
    '-Wformat=2',
    '-Wimplicit-function-declaration',
    '-Winit-self',
    '-Winline',
    '-Wjump-misses-init',
    '-Wlogical-op',
    '-Wmissing-declarations',
    '-Wmissing-format-attribute',
    '-Wmissing-include-dirs',
    '-Wmissing-noreturn',
    '-Wmissing-prototypes',
    '-Wnested-externs',
    '-Wnull-dereference',
    '-Wold-style-definition',
    '-Wpacked',
    '-Wpointer-arith',
    '-Wredundant-decls',
    '-Wrestrict',
    '-Wreturn-type',
    '-Wshadow',
    '-Wstrict-aliasing',
    '-Wstrict-prototypes',
    '-Wswitch-default',
    '-Wswitch-enum',
    '-Wundef',
    '-Wunused-but-set-variable',
    '-Wwrite-strings',
]
no_warning_cflags = [
    'declaration-after-statement',
    'missing-field-initializers',
    'sign-compare',
    'unused-local-typedefs',
    'unused-parameter',
]

c_compiler = meson.get_compiler('c')
supported_warning_cflags = c_compiler.get_supported_arguments(warning_cflags)
add_project_arguments(supported_warning_cflags, language : 'c')

foreach flag : no_warning_cflags
  supported_no_warning_cflags = c_compiler.get_supported_arguments([
    '-Wno-error=' + flag,
    '-Wno-' + flag,
  ])
  add_project_arguments(supported_no_warning_cflags, language : 'c')
endforeach

conf_data = configuration_data()
conf_data.set_quoted('VERSION', meson.project_version())

configure_file(
  output : 'config.h',
  configuration : conf_data,
)

add_project_arguments(['-std=c99'], language : 'c')
add_project_arguments(['-D_GNU_SOURCE'], language : 'c')
add_project_arguments(['-include', 'config.h'], language : 'c')
add_project_arguments(['-include', 'config.h'], language : 'cpp')

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
json_glib = dependency(
  'json-glib-1.0',
  version : '>= 1.0',
)
vulkan = dependency(
  'vulkan',
)

xcb = dependency(
  'xcb',
)

libdl = c_compiler.find_library('dl', required : false)

project_include_dirs = include_directories('.')

pkglibexecdir = join_paths(
  get_option('libexecdir'),
  'steam-runtime-tools-' + api_major,
)

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
