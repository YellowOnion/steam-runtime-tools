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

tests = [
  'architecture',
  'library',
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

foreach test_name : tests
  exe = executable(
    'test-' + test_name,
    files(test_name + '.c'),
    c_args : [
      '-D_SRT_MULTIARCH="' + multiarch + '"',
    ],
    dependencies : [glib, gobject, libsteamrt_dep],
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

# vim:set sw=2 sts=2 et:
