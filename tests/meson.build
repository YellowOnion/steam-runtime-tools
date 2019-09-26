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

test_env = environment()
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())
test_env.set('SRT_HELPERS_PATH', join_paths(meson.current_build_dir(), '..', 'helpers'))
test_env.prepend('PATH', join_paths(meson.current_build_dir(), '..', 'bin'))

tests = [
  'architecture',
  'graphics',
  'library',
  'locale',
  'system-info',
  'system-info-cli',
  'utils',
]

tests_utils = [
  'fake-home.c',
  'test-utils.c',
]

tests_dir = join_paths(
  get_option('libexecdir'),
  'installed-tests',
  meson.project_name() + '-' + api_major,
)
tests_metadir = join_paths(
  get_option('datadir'),
  'installed-tests',
  meson.project_name() + '-' + api_major,
)

install_subdir('expectations', install_dir : tests_dir)
install_subdir('expectations_with_missings', install_dir : tests_dir)
# To avoid being dependent on readdir() order, don't include more
# than one *.json file in any directory that we are going to scan for
# Vulkan *.json files. (The order of EGL *.json files is well-defined.)
install_subdir('fake-icds', install_dir : tests_dir)
install_subdir('fake-steam-runtime', install_dir : tests_dir)
install_subdir('sysroots', install_dir : tests_dir)

executable(
  'mock-true',
  join_paths('..', 'helpers', 'true.c'),
  install : get_option('installed_tests'),
  install_dir : tests_dir,
)

foreach test_name : tests
  exe = executable(
    'test-' + test_name,
    files(test_name + '.c') + tests_utils,
    c_args : [
      '-D_SRT_MULTIARCH="' + multiarch + '"',
    ],
    dependencies : [glib, gobject, libsteamrt_dep, json_glib],
    install : get_option('installed_tests'),
    install_dir : tests_dir,
  )

  if get_option('installed_tests')
    test_conf = configuration_data()
    test_conf.set('tests_dir', join_paths(get_option('prefix'), tests_dir))
    test_conf.set('program', 'test-' + test_name)
    configure_file(
      input: files('template.test.in'),
      output: test_name + '.test',
      install_dir: tests_metadir,
      configuration: test_conf
    )
  endif

  if glib_tap_support.found()
    test(test_name, exe, args : ['--tap'], env : test_env)
  else
    test(test_name, exe, env : test_env)
  endif
endforeach

# A mock implementation of check-locale that offers lots of locales
executable(
  'mock-check-locale',
  join_paths('..', 'helpers', 'check-locale.c'),
  c_args : [
    '-DMOCK_CHECK_LOCALE',
    '-DMOCK_CHECK_LOCALE_HAVE_C_UTF8',
    '-DMOCK_CHECK_LOCALE_HAVE_EN_US',
  ],
  dependencies : [glib, json_glib],
  include_directories : project_include_dirs,
  install: true,
  install_dir : tests_dir
)

# A mock implementation of check-locale that emulates a system where
# the current locale is not UTF-8 and the C.UTF-8 locale is missing
executable(
  'mock-legacy-check-locale',
  join_paths('..', 'helpers', 'check-locale.c'),
  c_args : [
    '-DMOCK_CHECK_LOCALE',
    '-DMOCK_CHECK_LOCALE_HAVE_EN_US',
    '-DMOCK_CHECK_LOCALE_LEGACY',
  ],
  dependencies : [glib, json_glib],
  include_directories : project_include_dirs,
  install: true,
  install_dir : tests_dir
)

# A mock implementation of check-locale that emulates a system where
# the en_US.UTF-8 and en_US locales are not available
executable(
  'mock-unamerican-check-locale',
  join_paths('..', 'helpers', 'check-locale.c'),
  c_args : ['-DMOCK_CHECK_LOCALE', '-DMOCK_CHECK_LOCALE_HAVE_C_UTF8'],
  dependencies : [glib, json_glib],
  include_directories : project_include_dirs,
  install: true,
  install_dir : tests_dir
)

executable(
  'mock-good-wflinfo',
  'mock-good-wflinfo.c',
  install: true,
  install_dir: tests_dir
)

executable(
  'mock-hanging-wflinfo',
  'mock-hanging-wflinfo.c',
  install: true,
  install_dir: tests_dir
)

executable(
  'mock-bad-wflinfo',
  'mock-bad-wflinfo.c',
  install: true,
  install_dir: tests_dir
)

executable(
  'mock-software-wflinfo',
  'mock-software-wflinfo.c',
  install: true,
  install_dir: tests_dir
)

executable(
  'mock-good-vulkaninfo',
  'mock-good-vulkaninfo.c',
  install: true,
  install_dir: tests_dir
)

executable(
  'mock-bad-vulkaninfo',
  'mock-bad-vulkaninfo.c',
  install: true,
  install_dir: tests_dir
)


# vim:set sw=2 sts=2 et:
