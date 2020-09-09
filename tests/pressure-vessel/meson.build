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
  'adverb.py',
  'cheap-copy.py',
  'containers.py',
  'invocation.py',
  'launcher.py',
  'test-locale-gen.sh',
  'utils.py',
]

compiled_tests = [
  'bwrap-lock',
  'resolve-in-sysroot',
  'wait-for-child-processes',
  'utils',
]

foreach test_name : compiled_tests
  exe = executable(
    'test-' + test_name,
    files(test_name + '.c', 'test-utils.c'),
    dependencies : [gio_unix, libglnx.get_variable('libglnx_dep')],
    link_with : [pressure_vessel_utils],
    include_directories : pv_include_dirs,
    install : false,
  )

  if glib_tap_support.found()
    if prove.found()
      test(
        test_name, prove,
        args : ['-v', '-e', '', exe, '::', '--tap'],
        env : test_env,
        suite : ['pressure-vessel'],
      )
    else
      test(
        test_name, exe,
        args : ['--tap'],
        env : test_env,
        suite : ['pressure-vessel'],
      )
    endif
  else
    test(
      test_name, exe,
      env : test_env,
      suite : ['pressure-vessel'],
    )
  endif

endforeach

foreach test_name : tests
  test_args = ['-v', files(test_name)]
  timeout = 30

  if test_name.endswith('.py')
    test_args += ['-e', python.path()]
  endif

  if test_name.endswith('launcher.py')
    timeout = 60
  endif

  if test_name.endswith('containers.py')
    timeout = 300
  endif

  if prove.found()
    if dbus_run_session.found()
      test(
        test_name, dbus_run_session,
        args : ['--', 'prove'] + test_args,
        env : test_env,
        suite : ['pressure-vessel'],
        timeout : timeout,
      )

      # Re-run launcher.py with the default python3 version (if any)
      # to try the code path involving gi.repository.Gio
      if test_name == 'launcher.py' and full_python.found()
        test(
          'full-python-' + test_name,
          dbus_run_session,
          args : [
            '--',
            'prove',
            '-v', files(test_name),
            '-e', full_python.path(),
          ],
          env : test_env,
          suite : ['pressure-vessel'],
          timeout : timeout,
        )
      endif
    else
      test(
        test_name, prove,
        args : test_args,
        env : test_env,
        suite : ['pressure-vessel'],
        timeout : timeout,
      )
    endif
  endif
endforeach

# Helper programs and manual tests
helpers = [
  'cheap-copy',
  'elf-get-soname',
  'helper',
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
    include_directories : pv_include_dirs,
    install : false,
  )
endforeach

# vim:set sw=2 sts=2 et:
