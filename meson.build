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

scripts = [
  'pressure-vessel-wrap',
  'pressure-vessel-test-ui',
  'pressure-vessel-unruntime',
]

tests = [
  'mypy.sh',
  'pycodestyle.sh',
  'pyflakes.sh',
  'relocatable-install.py',
  'shellcheck.sh',
]

test_env = environment()
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())

foreach test_name : tests
  if prove.found()
    test(
      test_name, prove,
      args : ['-v', files('t/' + test_name)],
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

relocatabledir = get_option('relocatabledir')

if relocatabledir == 'auto'
  dsc_exists = run_command(
    sh,
    '-euc',
    '''
      cd "$MESON_SOURCE_ROOT"
      if test -e libcapsule_*.dsc
        then echo libcapsule_*.dsc
      else
        exit 1
      fi
    ''',
  )
  if dsc_exists.returncode() == 0
    libcapsule_dsc = dsc_exists.stdout().strip()
    relocatabledir = ''
  else
    libcapsule_dsc = ''
    relocatabledir = '/usr/lib/libcapsule/relocatable'
  endif
endif

if libcapsule_dsc != ''
  libcapsule_configure = custom_target(
    'libcapsule_configure',
    command: [
      sh,
      '-euc',
      '''
        rm -fr libcapsule
        dpkg-source -x "$1"
        mv libcapsule-* libcapsule
        ( cd libcapsule; NOCONFIGURE=1 ./autogen.sh )
        touch "$2"
      ''',
      'sh',
      '@INPUT@',
      '@OUTPUT@',
    ],
    input : files(libcapsule_dsc),
    output : 'libcapsule_configure.stamp',
    build_by_default : true,
  )

  foreach arch : ['amd64', 'i386']
    config_stamp = custom_target(
      'build-relocatable_' + arch + '_config.stamp',
      command : [
        sh,
        files('configure-libcapsule.sh'),
        arch,
        '@OUTPUT@',
      ],
      depends : libcapsule_configure,
      output : 'build-relocatable_' + arch + '_config.stamp',
      build_by_default : true,
    )

    custom_target(
      'build-relocatable_' + arch + '_build.stamp',
      command: [
        sh,
        '-euc',
        '''
          make -C "build-relocatable/$1/libcapsule"
          touch "$2"
        ''',
        'sh',
        arch,
        '@OUTPUT@',
      ],
      depends : config_stamp,
      output : 'build-relocatable_' + arch + '_build.stamp',
      build_by_default : true,
    )
  endforeach
endif

if host_machine.cpu_family() == 'x86_64'
  multiarch = 'x86_64-linux-gnu'
elif host_machine.cpu_family() == 'x86'
  multiarch = 'i386-linux-gnu'
else
  error('CPU family not supported')
endif

cc = meson.get_compiler('c')

conf_data = configuration_data()
conf_data.set('VERSION', version)

check_functions = [
  'renameat2',
  'memfd_create',
  'copy_file_range',
]
foreach check_function : check_functions
  if cc.compiles('''
    #include <sys/types.h>
    #include <unistd.h>
    #include <stdio.h>
    #include <sys/mount.h>
    #include <fcntl.h>
    #include <sched.h>
    #include <linux/loop.h>
    #include <linux/random.h>
    #include <sys/mman.h>

    int func (void) {
      (void) ''' + check_function + ''';
    }
    ''',
    args : '-D_GNU_SOURCE',
    name : check_function + '() is declared',
  )
    conf_data.set('HAVE_DECL_' + check_function.underscorify().to_upper(), 1)
  endif
endforeach

configure_file(
  input : 'config.h.in',
  output : 'config.h',
  configuration : conf_data,
)

executable(
  'pressure-vessel-wrap-c',
  sources : [
    'src/glib-backports.c',
    'src/glib-backports.h',
    'src/flatpak-bwrap.c',
    'src/flatpak-bwrap-private.h',
    'src/flatpak-utils.c',
    'src/flatpak-utils-private.h',
    'src/wrap.c',
  ],
  c_args : [
    '-Wno-unused-local-typedefs',
  ],
  dependencies : [
    dependency('gio-unix-2.0', required : true),
    dependency('xau', required : true),
    subproject('libglnx').get_variable('libglnx_dep'),
  ],
  build_rpath : '${ORIGIN}/../lib/' + multiarch,
  install : true,
  install_dir : get_option('bindir'),
  install_rpath : '${ORIGIN}/../lib/' + multiarch,
)

meson.add_install_script(
  python.path(),
  join_paths(meson.current_source_dir(), 'build-relocatable-install.py'),
  '--srcdir', meson.current_source_dir(),
  '--builddir', meson.current_build_dir(),
  '--relocatabledir', relocatabledir,
  '--prefix', get_option('prefix'),
  '--archive', meson.current_build_dir(),
  '--set-version', version,
)

# vim:set sw=2 sts=2 et:
