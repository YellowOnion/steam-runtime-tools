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

project('pressure-vessel', 'c')

prove = find_program('prove', required : false)
python = find_program(get_option('python'), required : true)
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
    'unused-parameter',
]
# This trips some warnings in the libglnx subproject but not in
# pressure-vessel itself
project_warning_cflags = [
    '-Wsign-compare',
]


c_compiler = meson.get_compiler('c')
supported_warning_cflags = c_compiler.get_supported_arguments(warning_cflags)
add_global_arguments(supported_warning_cflags, language : 'c')

foreach flag : no_warning_cflags
  supported_no_warning_cflags = c_compiler.get_supported_arguments([
    '-Wno-error=' + flag,
    '-Wno-' + flag,
  ])
  add_global_arguments(supported_no_warning_cflags, language : 'c')
endforeach

supported_warning_cflags = c_compiler.get_supported_arguments(project_warning_cflags)
add_project_arguments(supported_warning_cflags, language : 'c')

scripts = [
  'pressure-vessel-locale-gen',
  'pressure-vessel-test-ui',
  'pressure-vessel-unruntime',
  'pressure-vessel-unruntime-scout',
  'pressure-vessel-unruntime-test-ui',
]

tests = [
  'mypy.sh',
  'pycodestyle.sh',
  'pyflakes.sh',
  'shellcheck.sh',
  'test-locale-gen.sh',
]

test_env = environment()
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())
test_env.set('PRESSURE_VESSEL_UNINSTALLED', 'yes')

foreach test_name : tests
  if prove.found()
    test(
      test_name, prove,
      args : ['-v', files('tests/' + test_name)],
      env : test_env,
    )
  endif
endforeach

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
  output : 'config.h',
  configuration : conf_data,
)

executable(
  'pressure-vessel-wrap',
  sources : [
    'src/bwrap.c',
    'src/bwrap.h',
    'src/glib-backports.c',
    'src/glib-backports.h',
    'src/flatpak-bwrap.c',
    'src/flatpak-bwrap-private.h',
    'src/flatpak-run.c',
    'src/flatpak-run-private.h',
    'src/flatpak-utils.c',
    'src/flatpak-utils-private.h',
    'src/utils.c',
    'src/utils.h',
    'src/wrap.c',
    'src/wrap-interactive.c',
    'src/wrap-interactive.h',
  ],
  c_args : [
    '-Wno-unused-local-typedefs',
  ],
  dependencies : [
    dependency(
      'steam-runtime-tools-0',
      required : true,
      fallback : ['steam-runtime-tools', 'libsteamrt_dep'],
      default_options : ['gtk_doc=false'],
    ),
    dependency('gio-unix-2.0', required : true),
    dependency('xau', required : true),
    subproject('libglnx').get_variable('libglnx_dep'),
  ],
  install : true,
  install_dir : get_option('bindir'),
  build_rpath : '${ORIGIN}/../' + get_option('libdir'),
  install_rpath : '${ORIGIN}/../' + get_option('libdir'),
)

executable(
  'pressure-vessel-try-setlocale',
  sources : [
    'src/try-setlocale.c',
  ],
  install : true,
  install_dir : get_option('bindir'),
)

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

  foreach tool : ['locale-gen', 'try-setlocale']
    custom_target(
      tool + '.1',
      build_by_default : true,
      command : [
        pandoc,
        '-s',
        '-o', '@OUTPUT@',
        '-f', pandoc_markdown_nosmart,
        '-t', 'man',
        '@INPUT@',
      ],
      input : join_paths('man', tool + '.1.md'),
      output : 'pressure-vessel-' + tool + '.1',
      install : true,
      install_dir : join_paths(
        get_option('prefix'),
        get_option('mandir'),
        'man1',
      ),
    )
  endforeach
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

# vim:set sw=2 sts=2 et:
