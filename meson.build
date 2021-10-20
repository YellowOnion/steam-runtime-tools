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

project(
  'steam-runtime-tools', 'c',
  version : '0.20210906.2',
  default_options: [
    'c_std=c99',
    'warning_level=2',
  ],
  meson_version : '>=0.49.0',
)

api_major = '0'
abi_major = '0'
abi_minor = '20210906.2'

pkg = import('pkgconfig')
gnome = import('gnome')

# We'd like to use import('python').find_installation(), but before
# Meson 0.50 there was a bug where it didn't have a path() method,
# making it useless to us here.
if get_option('python') == ''
  python = find_program('python3.5', 'python3', required : true)
else
  python = find_program(get_option('python'), required : true)
endif

if get_option('fully_featured_python') == ''
  full_python = find_program('python3', required : false)
else
  full_python = find_program(
    get_option('fully_featured_python'),
    required : false,
  )
endif

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

dbus_run_session = find_program('dbus-run-session', required : false)
glslang_validator = find_program('glslangValidator', required : true)
prove = find_program('prove', required : false)
sh = find_program('sh', required : true)

version = get_option('version')
if version == 'auto'
  git_version_gen = run_command(
    sh,
    files('build-aux/git-version-gen'),
    join_paths(meson.current_source_dir(), '.tarball-version'),
    check : true,
  )
  version = git_version_gen.stdout().strip()
endif

if version == ''
  version = meson.project_version()
endif

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

no_warning_flags = [
    'cast-align',
    'inline',
    'missing-field-initializers',
    'pedantic',
    'sign-compare',
    'unused-local-typedefs',
    'unused-parameter',
    'variadic-macros',
]
no_warning_cflags = no_warning_flags + [
    'declaration-after-statement',
]

c_compiler = meson.get_compiler('c')
supported_warning_cflags = c_compiler.get_supported_arguments(warning_cflags)
add_project_arguments(supported_warning_cflags, language : 'c')

foreach flag : no_warning_cflags
  supported_no_warning_cflags = c_compiler.get_supported_arguments([
    '-Wno-error=' + flag,
    '-Wno-' + flag,
  ])
  add_global_arguments(supported_no_warning_cflags, language : 'c')
endforeach

add_global_arguments('-fvisibility=hidden', language : ['c'])

# gdbus-codegen in scout generates code that has many warnings
silence_gdbus_codegen_warnings = c_compiler.get_supported_arguments([
  '-Wno-error',
  '-Wno-error=discarded-qualifiers',
  '-Wno-error=redundant-decls',
  '-Wno-error=shadow',
  '-Wno-error=write-strings',
  '-Wno-discarded-qualifiers',
  '-Wno-redundant-decls',
  '-Wno-shadow',
  '-Wno-write-strings',
])

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

libselinux_dep = dependency(
  'libselinux',
  version : '>= 2.1.9',
  required : false,
)
libcap_dep = dependency(
  'libcap',
  required : false,
)
if not libcap_dep.found()
  libcap_dep = c_compiler.find_library(
    'cap',
    required : get_option('pressure_vessel') and get_option('bin'),
  )
endif

conf_data = configuration_data()
conf_data.set_quoted('VERSION', version)
conf_data.set('HAVE_SELINUX', libselinux_dep.found())
conf_data.set(
  'HAVE_SELINUX_2_3',
  libselinux_dep.found() and libselinux_dep.version().version_compare('>=2.3'),
)
conf_data.set('api_major', api_major)
conf_data.set('abi_major', abi_major)

if get_option('multiarch_tuple') != ''
  multiarch = get_option('multiarch_tuple')
elif host_machine.cpu_family() == 'x86_64'
  multiarch = 'x86_64-linux-gnu'
elif host_machine.cpu_family() == 'x86'
  multiarch = 'i386-linux-gnu'
else
  multiarch = ''
endif

if multiarch != ''
  conf_data.set_quoted('_SRT_MULTIARCH', multiarch)
endif

configure_file(
  input : 'config.h.in',
  output : '_steam-runtime-tools-config.h',
  configuration : conf_data,
)

add_project_arguments(
  ['-include', '_steam-runtime-tools-config.h'],
  language : 'c',
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

xau = dependency('xau', required : true)

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

libglnx_dep = subproject('libglnx').get_variable('libglnx_dep')

project_include_dirs = include_directories(
  '.',
  'subprojects',
)

pv_include_dirs = include_directories(
    '.',
    'pressure-vessel',
    'subprojects',
)

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

subdir('steam-runtime-tools')

if multiarch != ''
  subdir('helpers')
endif

if get_option('bin')
  subdir('bin')
endif

if get_option('pressure_vessel') and get_option('bin')
  subdir('pressure-vessel')
endif

subdir('docs')
subdir('tests')

# vim:set sw=2 sts=2 et:
