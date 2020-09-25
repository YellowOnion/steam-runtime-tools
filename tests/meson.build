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
test_env.set('SRT_HELPERS_PATH', join_paths(meson.current_build_dir(), '..', 'helpers'))
test_env.prepend('PATH', join_paths(meson.current_build_dir(), '..', 'bin'))

tests = [
  'architecture',
  'desktop-entry',
  'graphics',
  'library',
  'locale',
  'system-info',
  'utils',
]

if get_option('bin')
  tests += [
    'check-requirements-cli',
    'system-info-cli',
  ]
endif

test_utils = static_library(
  'test-utils',
  sources : [
    'fake-home.c',
    'test-utils.c',
  ],
  dependencies : [
    glib,
    gobject,
    gio_unix,
    json_glib,
    libglnx_dep,
    libsteamrt_dep,
  ],
  include_directories : project_include_dirs,
  install: false,
)
test_utils_dep = declare_dependency(
  dependencies : [
    glib,
    gobject,
    gio_unix,
    json_glib,
    libglnx_dep,
    libsteamrt_dep,
  ],
  link_with : test_utils,
)

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
install_subdir('json-report', install_dir : tests_dir)

meson.add_postconf_script(
  python.path(),
  join_paths(meson.current_source_dir(), 'generate-sysroots.py'),
  join_paths(meson.current_build_dir(), 'sysroots'),
)

meson.add_install_script(
  python.path(),
  join_paths(meson.current_source_dir(), 'generate-sysroots.py'),
  '--install',
  join_paths(tests_dir, 'sysroots'),
)

# These are all the same: they just exit 0.
foreach helper : [
  'mock-good-check-gl',
  'mock-good-check-vulkan',
  'mock-software-check-gl',
  'mock-true',
]
  executable(
    helper,
    join_paths('..', 'helpers', 'true.c'),
    include_directories : project_include_dirs,
    install : get_option('installed_tests'),
    install_dir : tests_dir,
  )
endforeach

executable(
  'adverb',
  'adverb.c',
  dependencies : [glib, gio, gio_unix, libglnx_dep, json_glib, libsteamrt_dep],
  include_directories : project_include_dirs,
  install : get_option('installed_tests'),
  install_dir : tests_dir,
)

foreach test_name : tests
  exe = executable(
    'test-' + test_name,
    files(test_name + '.c'),
    c_args : [
      '-D_SRT_MULTIARCH="' + multiarch + '"',
    ],
    dependencies : [
      glib,
      gobject,
      gio_unix,
      json_glib,
      libglnx_dep,
      libsteamrt_dep,
      test_utils_dep,
    ],
    include_directories : project_include_dirs,
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
  dependencies : [glib, gio_unix, libglnx_dep, json_glib],
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
  dependencies : [glib, gio_unix, libglnx_dep, json_glib],
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
  dependencies : [glib, gio_unix, libglnx_dep, json_glib],
  include_directories : project_include_dirs,
  install: true,
  install_dir : tests_dir
)

# Helpers with no dependencies and one source file of the same name
# as the helper itself.
foreach helper : [
  'mock-bad-check-va-api',
  'mock-bad-check-vdpau',
  'mock-bad-check-vulkan',
  'mock-bad-vulkaninfo',
  'mock-good-check-va-api',
  'mock-good-check-vdpau',
  'mock-good-vulkaninfo',
  'mock-hanging-wflinfo',
  'mock-sigusr-wflinfo',
]
  executable(
    helper,
    helper + '.c',
    include_directories : project_include_dirs,
    install: true,
    install_dir: tests_dir
  )
endforeach

# These need to be the same as their -good- counterparts, to exercise
# the case where the preliminary check succeeds, but actually rendering
# a window fails (hence "mixed").
executable(
  'mock-mixed-vulkaninfo',
  'mock-good-vulkaninfo.c',
  include_directories : project_include_dirs,
  install: true,
  install_dir: tests_dir
)
executable(
  'mock-mixed-wflinfo',
  'mock-good-wflinfo.c',
  include_directories : project_include_dirs,
  dependencies: [glib],
  install: true,
  install_dir: tests_dir
)

# These are all essentially the same as mock-bad-check-vulkan: they
# fail with a message on stderr.
foreach helper : [
  'mock-bad-check-gl',
  'mock-mixed-check-vulkan',
]
  executable(
    helper,
    'mock-bad-check-vulkan.c',
    include_directories : project_include_dirs,
    install: true,
    install_dir: tests_dir
  )
endforeach

# Helpers with a GLib dependency and one source file of the same name
# as the helper itself.
foreach helper : [
  'mock-abi-inspect-library',
  'mock-bad-wflinfo',
  'x86_64-mock-debian-inspect-library',
  'i386-mock-debian-inspect-library',
  'x86_64-mock-fedora-inspect-library',
  'i386-mock-fedora-inspect-library',
  'mock-good-wflinfo',
  'mock-mixed-check-gl',
  'mock-software-wflinfo',
  'mock-ubuntu-64-bit-inspect-library',
]
  executable(
    helper,
    helper + '.c',
    dependencies : [glib],
    include_directories : project_include_dirs,
    install: true,
    install_dir: tests_dir
  )
endforeach

# i386 and x86_64 helpers with GLib and Gio dependencies, and one source file of
# the same name as the helper itself (with the arch prefix).
foreach arch : [
  'i386',
  'x86_64',
]
  foreach helper : [
    'mock-container-capsule-capture-libs',
    'mock-debian-capsule-capture-libs',
    'mock-fedora-capsule-capture-libs',
  ]
    executable(
      arch + '-' + helper,
      helper + '.c',
      c_args : ['-DMOCK_ARCHITECTURE_' + arch],
      dependencies : [glib, gio, test_utils_dep],
      include_directories : project_include_dirs,
      install: true,
      install_dir: tests_dir
    )
  endforeach
endforeach

test_scripts = [
  'mypy.sh',
  'pycodestyle.sh',
  'pyflakes.sh',
  'shellcheck.sh',
]

foreach test_name : test_scripts
  test_args = ['-v', files(test_name)]
  timeout = 30

  if prove.found()
    if dbus_run_session.found()
      test(
        test_name, dbus_run_session,
        args : ['--', 'prove'] + test_args,
        env : test_env,
        timeout : timeout,
      )
    else
      test(
        test_name, prove,
        args : test_args,
        env : test_env,
        timeout : timeout,
      )
    endif
  endif
endforeach

if get_option('pressure_vessel') and get_option('bin')
  subdir('pressure-vessel')
endif

# vim:set sw=2 sts=2 et:
