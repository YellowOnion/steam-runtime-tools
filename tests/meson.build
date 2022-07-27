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

no_activation_session_conf = files('session.conf')

tests = [
  {'name': 'architecture'},
  {'name': 'container'},
  {'name': 'cpu-feature', 'static': true},
  {'name': 'desktop-entry'},
  {'name': 'graphics'},
  {
    'name': 'input-device',
    'static': true,
    'sources': [
      'input-device.c',
      'mock-input-device.c',
      'mock-input-device.h',
    ],
  },
  {'name': 'json-utils', 'static': true},
  {'name': 'libdl', 'static': true},
  {'name': 'library'},
  {'name': 'locale'},
  {'name': 'system-info'},
  {'name': 'utils', 'static': true},
  {'name': 'virtualization', 'static': true},
  {'name': 'xdg-portal'},
]

if get_option('bin')
  tests += [
    {'name': 'check-requirements-cli'},
    {'name': 'identify-library-abi-cli'},
    {'name': 'system-info-cli', 'static': true, 'slow': true},
  ]
endif

test_utils = static_library(
  'test-utils',
  sources : [
    'fake-home.c',
    'test-init.c',
    'test-utils.c',
  ],
  c_args : [
    '-D_SRT_PYTHON="' + python.path() + '"',
  ],
  dependencies : [
    glib,
    gobject,
    gio_unix,
    json_glib,
    libglnx_dep,
    libsteamrt_generated_headers_dep,
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
test_utils_static_libsteamrt_dep = declare_dependency(
  dependencies : [
    glib,
    gobject,
    gio_unix,
    json_glib,
    libglnx_dep,
    libsteamrt_static_dep,
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
install_subdir('input-monitor-outputs', install_dir : tests_dir)
install_subdir('json-report', install_dir : tests_dir)

install_data('generate-sysroots.py', install_dir : tests_dir)

# These are all the same: they just exit 0.
foreach helper : [
  'mock-good-check-gl',
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

foreach test_info : tests
  test_name = test_info['name']

  if test_info.get('static', false)
    deps = [libsteamrt_static_dep, test_utils_static_libsteamrt_dep]
  else
    deps = [libsteamrt_dep, test_utils_dep]
  endif

  if test_info.get('slow', false)
    timeout = 60
  else
    timeout = 30
  endif

  sources = test_info.get('sources', files(test_name + '.c'))

  exe = executable(
    'test-' + test_name,
    sources,
    dependencies : deps,
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
    test(test_name, exe, args : ['--tap'], env : test_env, timeout : timeout)
  else
    test(test_name, exe, env : test_env, timeout : timeout)
  endif
endforeach

# Helpers and manual tests statically linked to libsteam-r-t
foreach helper : [
  'find-myself',
]
  executable(
    helper,
    files(helper + '.c'),
    dependencies : [
      glib,
      gobject,
      gio_unix,
      json_glib,
      libglnx_dep,
      libsteamrt_static_dep,
      test_utils_static_libsteamrt_dep,
    ],
    include_directories : project_include_dirs,
    install : get_option('installed_tests'),
    install_dir : tests_dir,
  )
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
  'mock-bad-check-xdg-portal',
  'mock-good-check-va-api',
  'mock-good-check-vdpau',
  'mock-good-check-xdg-portal',
  'mock-good-flatpak-check-xdg-portal',
  'mock-hanging-check-xdg-portal',
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

foreach helper : [
  'mock-good-check-vulkan',
  'mock-mixed-check-vulkan',
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

foreach token : [
  'LIB',
  'PLATFORM',
]
  foreach helper : [
    'mock-good-detect',
    'mock-bad-detect',
  ]
    executable(
      helper + '-' + token.to_lower(),
      helper + '.c',
      c_args : ['-DMOCK_LIBDL_TOKEN_' + token],
      include_directories : project_include_dirs,
      install: true,
      install_dir: tests_dir
    )
  endforeach
endforeach

# These need to be the same as their -good- counterparts, to exercise
# the case where the preliminary check succeeds, but actually rendering
# a window fails (hence "mixed").
executable(
  'mock-mixed-wflinfo',
  'mock-good-wflinfo.c',
  include_directories : project_include_dirs,
  dependencies: [glib],
  install: true,
  install_dir: tests_dir
)

# mock-bad-check-gl is essentially the same as mock-bad-check-vulkan:
# it fails with a message on stderr.
executable(
  'mock-bad-check-gl',
  'mock-bad-check-vulkan.c',
  include_directories : project_include_dirs,
  install: true,
  install_dir: tests_dir
)

# Helpers with a GLib dependency and one source file of the same name
# as the helper itself.
foreach helper : [
  'mock-bad-wflinfo',
  'x86_64-mock-debian-inspect-library',
  'i386-mock-debian-inspect-library',
  'x86_64-mock-fedora-inspect-library',
  'x86_64-mock-ubuntu-inspect-library',
  'i386-mock-fedora-inspect-library',
  'mock-good-wflinfo',
  'mock-mixed-check-gl',
  'mock-software-wflinfo',
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
    'mock-abi-inspect-library',
    'mock-container-capsule-capture-libs',
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

# i386 and x86_64 capsule-capture-libs helpers that differ only by
# compile time args
foreach arch : [
  'i386',
  'x86_64',
]
  foreach distro : [
    'abi',
    'debian',
    'fedora',
    'ubuntu',
  ]
    executable(
      arch + '-mock-' + distro + '-capsule-capture-libs',
      'mock-capsule-capture-libs.c',
      c_args : [
        '-DMOCK_ARCHITECTURE_' + arch,
        '-DMOCK_DISTRO_' + distro.to_upper(),
      ],
      dependencies : [glib, gio, test_utils_dep],
      include_directories : project_include_dirs,
      install: true,
      install_dir: tests_dir
    )
  endforeach
endforeach

lint_scripts = [
  'mypy.sh',
  'pycodestyle.sh',
  'pyflakes.sh',
  'shellcheck.sh',
]

foreach test_name : lint_scripts
  timeout = 30

  if meson.version().version_compare('>=0.50')
    test(
      test_name, files(test_name),
      env : test_env,
      suite : 'lint',
      protocol : 'tap',
      timeout : timeout,
    )
  else
    test(
      test_name, files(test_name),
      env : test_env,
      suite : 'lint',
      timeout : timeout,
    )
  endif
endforeach

if get_option('pressure_vessel') and get_option('bin')
  subdir('pressure-vessel')
endif

# vim:set sw=2 sts=2 et:
