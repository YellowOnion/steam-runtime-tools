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

test_env = environment()
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())
test_env.set(
  'PRESSURE_VESSEL_TEST_CONTAINERS',
  get_option('test_containers_dir'),
)
test_env.set('PRESSURE_VESSEL_UNINSTALLED', 'yes')

tests = [
  'cheap-copy.py',
  'containers.py',
  'invocation.py',
  'mypy.sh',
  'pycodestyle.sh',
  'pyflakes.sh',
  'shellcheck.sh',
  'test-locale-gen.sh',
]

compiled_tests = [
  'bwrap-lock',
  'resolve-in-sysroot',
  'utils',
]

foreach test_name : compiled_tests
  exe = executable(
    'test-' + test_name,
    files(test_name + '.c', 'test-utils.c'),
    dependencies : [gio_unix, libglnx.get_variable('libglnx_dep')],
    link_with : [pressure_vessel_utils],
    include_directories : project_include_dirs,
    install : false,
  )

  if glib_tap_support.found()
    if prove.found()
      test(
        test_name, prove,
        args : ['-v', '-e', '', exe, '::', '--tap'],
        env : test_env,
      )
    else
      test(
        test_name, exe,
        args : ['--tap'],
        env : test_env,
      )
    endif
  else
    test(
      test_name, exe,
      env : test_env,
    )
  endif

endforeach

foreach test_name : tests
  test_args = ['-v', files(test_name)]
  timeout = 30

  if test_name.endswith('.py')
    test_args += ['-e', python.path()]
  endif

  if test_name.endswith('containers.py')
    timeout = 300
  endif

  if prove.found()
    test(
      test_name, prove,
      args : test_args,
      env : test_env,
      timeout : timeout,
    )
  endif
endforeach

# Helper programs and manual tests
helpers = [
  'cheap-copy',
  'elf-get-soname',
]

foreach helper : helpers
  executable(
    'test-' + helper,
    sources : [
      helper + '.c',
    ],
    dependencies : [
      gio_unix,
      libglnx.get_variable('libglnx_dep'),
    ],
    link_with : [
      pressure_vessel_utils,
    ],
    include_directories : project_include_dirs,
    install : false,
  )
endforeach

# vim:set sw=2 sts=2 et:
