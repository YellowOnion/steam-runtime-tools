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
  'pressure-vessel', 'c',
  default_options: [
    'c_std=c99',
    'cpp_std=c++11',
    'prefix=/usr/lib/pressure-vessel/relocatable',
    'warning_level=2',
  ],
)

gnome = import('gnome')

dbus_run_session = find_program('dbus-run-session', required : false)
prove = find_program('prove', required : false)
sh = find_program('sh', required : true)

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

warning_cflags = [
    '-Wall',
    '-Wextra',
    '-Warray-bounds',
    '-Wcast-align',
    '-Wdouble-promotion',
    '-Wduplicated-branches',
    '-Wduplicated-cond',
    '-Wformat-nonliteral',
    '-Wformat-security',
    '-Wformat=2',
    '-Wimplicit-function-declaration',
    '-Winit-self',
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
    'cast-align',
    'declaration-after-statement',
    'missing-field-initializers',
    'pedantic',
    'sign-compare',
    'unused-local-typedefs',
    'unused-parameter',
    'variadic-macros',
]
# This trips some warnings in the libglnx subproject but not in
# pressure-vessel itself
project_warning_cflags = [
    '-Wsign-compare',
]

project_include_dirs = include_directories(
    '.',
    'src',
    'subprojects',
)

c_compiler = meson.get_compiler('c')
supported_warning_cflags = c_compiler.get_supported_arguments(warning_cflags)
add_global_arguments(supported_warning_cflags, language : 'c')

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

foreach flag : no_warning_cflags
  supported_no_warning_cflags = c_compiler.get_supported_arguments([
    '-Wno-error=' + flag,
    '-Wno-' + flag,
  ])
  add_global_arguments(supported_no_warning_cflags, language : 'c')
endforeach

supported_warning_cflags = c_compiler.get_supported_arguments(project_warning_cflags)
add_project_arguments(supported_warning_cflags, language : 'c')

libglnx = subproject('libglnx')

threads = dependency('threads')
steam_runtime_tools = dependency(
  'steam-runtime-tools-0',
  version : '>=0.20200331.1',
  required : true,
  fallback : ['steam-runtime-tools', 'libsteamrt_dep'],
  default_options : [
    'gtk_doc=false',
    'man=false',
    'installed_tests=false',
    'introspection=false',
  ],
)
gio_unix = dependency('gio-unix-2.0', required : true)
glib_tap_support = dependency(
  'glib-2.0',
  version : '>= 2.38',
  required : false,
)
xau = dependency('xau', required : true)
libelf = dependency('libelf', required : false)

if not libelf.found()
  libelf = declare_dependency(link_args : ['-lelf'])
endif

scripts = [
  'pressure-vessel-locale-gen',
  'pressure-vessel-test-ui',
  'pressure-vessel-unruntime',
  'pressure-vessel-unruntime-scout',
  'pressure-vessel-unruntime-test-ui',
]

foreach script : scripts
  install_data(
    script,
    install_dir : get_option('bindir'),
  )
endforeach

cc = meson.get_compiler('c')

conf_data = configuration_data()
conf_data.set_quoted('VERSION', version)

configure_file(
  input : 'config.h.in',
  output : '_pressure-vessel-config.h',
  configuration : conf_data,
)
add_project_arguments(
  ['-include', '_pressure-vessel-config.h'],
  language : 'c',
)

subdir('src')

if get_option('man')
  subdir('man')
endif

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

subdir('tests')

# vim:set sw=2 sts=2 et:
