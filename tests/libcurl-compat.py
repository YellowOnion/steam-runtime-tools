#!/usr/bin/env python3
# Copyright 2020-2022 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

import ctypes
import os
import platform
import shutil
import subprocess
import sys


try:
    import typing
    typing      # placate pyflakes
except ImportError:
    pass

from testutils import (
    BaseTest,
    MyCompletedProcess,
    run_subprocess,
    test_main,
)


class KnownArchitecture:
    def __init__(
        self,
        multiarch_tuple: str,
        word_size: int,
    ):
        self.multiarch_tuple = multiarch_tuple
        self.word_size = word_size


KNOWN_ARCHITECTURES = {}    # type: typing.Dict[str,KnownArchitecture]

for a in (
    KnownArchitecture('i386-linux-gnu', 32),
    KnownArchitecture('x86_64-linux-gnu', 64),
):
    KNOWN_ARCHITECTURES[a.multiarch_tuple] = a


class TestLibcurlCompat(BaseTest):
    def setUp(self) -> None:
        super().setUp()

        self.original_ld_library_path = os.environ.get(
            'LD_LIBRARY_PATH', ''
        )
        print(
            '# LD_LIBRARY_PATH initially %r'
            % self.original_ld_library_path,
            file=sys.stderr,
        )

        if 'SRT_TEST_UNINSTALLED' in os.environ:
            if 'SRT_TEST_MULTIARCH' not in os.environ:
                self.skipTest('Unknown host architecture')

            multiarch_tuple = os.environ['SRT_TEST_MULTIARCH']

            if multiarch_tuple not in KNOWN_ARCHITECTURES:
                self.skipTest('Unsupported host architecture for libcurl shim')

            self.architectures = {
                multiarch_tuple: KNOWN_ARCHITECTURES[multiarch_tuple],
            }

            self.test_fixtures_builddir = os.path.join(
                self.top_builddir, 'libcurl-compat', 'test-fixtures',
            )
            self.test_fixtures_srcdir = os.path.join(
                self.top_srcdir, 'libcurl-compat', 'test-fixtures',
            )
            self.libcurl_compat_srcdir = os.path.join(
                self.top_srcdir, 'libcurl-compat',
            )
            self.compat_setup = os.path.join(
                self.top_builddir, 'libcurl-compat',
                'steam-runtime-libcurl-compat-setup',
            )
        elif 'STEAM_RUNTIME' in os.environ:
            if platform.machine() != 'x86_64':
                self.skipTest('Not x86_64')

            self.test_fixtures_builddir = os.path.join(
                self.G_TEST_BUILDDIR, 'libcurl-compat',
            )
            self.test_fixtures_srcdir = self.test_fixtures_builddir
            self.libcurl_compat_srcdir = self.test_fixtures_builddir
            self.compat_setup = 'steam-runtime-libcurl-compat-setup'
            self.architectures = KNOWN_ARCHITECTURES

            if 'SYSTEM_LD_LIBRARY_PATH' in os.environ:
                os.environ['LD_LIBRARY_PATH'] = (
                    os.environ['SYSTEM_LD_LIBRARY_PATH']
                )
        else:
            self.skipTest('Not running in the Steam Runtime')

        libc = ctypes.CDLL('libc.so.6')
        libc.gnu_get_libc_version.restype = ctypes.c_char_p
        libc.strverscmp.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
        libc.strverscmp.restype = ctypes.c_int
        glibc_version = libc.gnu_get_libc_version()

        # We assume here that the glibc for each supported architecture is
        # approximately the same version as glibc for Python's architecture
        if libc.strverscmp(glibc_version, b'2.30') < 0:
            self.skipTest('glibc version {} is too old'.format(glibc_version))

        self.suffixes = ['', '-gnutls']

        for arch in self.architectures.values():
            multiarch_tuple = arch.multiarch_tuple

            os.makedirs(
                os.path.join(
                    self.tmpdir.name, 'os-lib', multiarch_tuple,
                ),
                exist_ok=True,
            )
            os.makedirs(
                os.path.join(
                    self.tmpdir.name, 'steam-runtime',
                    'usr', 'lib', 'steamrt', 'expectations', multiarch_tuple,
                ),
                exist_ok=True,
            )
            os.makedirs(
                os.path.join(
                    self.tmpdir.name, 'steam-runtime',
                    'usr', 'lib', 'steamrt', 'libcurl-compat', 'expectations',
                    multiarch_tuple,
                ),
                exist_ok=True,
            )
            os.makedirs(
                os.path.join(
                    self.tmpdir.name, 'steam-runtime',
                    'usr', 'lib', multiarch_tuple,
                ),
                exist_ok=True,
            )

            src = os.path.join(
                self.libcurl_compat_srcdir, 'libc6.symbols',
            )
            dest = os.path.join(
                self.tmpdir.name, 'steam-runtime',
                'usr', 'lib', 'steamrt', 'libcurl-compat', 'expectations',
                multiarch_tuple, 'libc6.symbols',
            )
            shutil.copy2(src, dest)

            for suffix in self.suffixes:
                for version in ('3', '4', 'none'):
                    if version == 'none':
                        version_or_empty = ''
                    else:
                        version_or_empty = version

                    src = os.path.join(
                        self.test_fixtures_builddir, version,
                        '{}-steamrt-print-libcurl{}{}-version'.format(
                            multiarch_tuple, version_or_empty, suffix,
                        ),
                    )
                    dest = os.path.join(
                        self.tmpdir.name,
                        '{}-steamrt-print-libcurl{}{}-version'.format(
                            multiarch_tuple, version_or_empty, suffix,
                        ),
                    )

                    if os.path.exists(src):
                        # Meson often sets a RPATH/RUNPATH, but we don't
                        # want those. Copy the built file into our temporary
                        # directory so we don't make non-reversible changes.
                        # If chrpath is unavailable, copying it into the
                        # top-level temporary directory means that the
                        # RPATH/RUNPATH has no practical effect.
                        print(
                            '# Setting up {} as {}'.format(src, dest),
                            file=sys.stderr,
                        )
                        shutil.copy2(src, dest)

                        if shutil.which('chrpath') is not None:
                            subprocess.run(
                                ['chrpath', '-d', dest],
                                check=True,
                                stdout=2,       # i.e. >&2
                            )

                        if (
                            'SRT_DEBUG_LIBCURL_COMPAT' in os.environ
                            and shutil.which('objdump') is not None
                        ):
                            subprocess.run(
                                ['objdump', '-T', '-x', dest],
                                check=True,
                                stdout=2,
                            )

                if 'SRT_TEST_UNINSTALLED' in os.environ:
                    real_lib_dir = os.path.join(
                        self.top_builddir, 'libcurl-compat',
                    )
                else:
                    real_lib_dir = os.path.join(
                        os.environ['STEAM_RUNTIME'], 'usr', 'lib',
                        multiarch_tuple,
                    )

                shutil.copy2(
                    os.path.join(
                        real_lib_dir,
                        'libsteam-runtime-shim-libcurl{}.so.4'.format(suffix),
                    ),
                    os.path.join(
                        self.tmpdir.name, 'steam-runtime',
                        'usr', 'lib', multiarch_tuple,
                        'libsteam-runtime-shim-libcurl{}.so.4'.format(suffix),
                    )
                )

                if 'SRT_TEST_UNINSTALLED' in os.environ:
                    mock_scout_lib_dir = os.path.join(
                        self.test_fixtures_builddir, 'scout',
                    )
                else:
                    mock_scout_lib_dir = os.path.join(
                        self.test_fixtures_builddir, 'scout', multiarch_tuple,
                    )

                src = os.path.join(
                    mock_scout_lib_dir, 'libcurl{}.so.4'.format(suffix),
                )
                dest = os.path.join(
                    self.tmpdir.name, 'steam-runtime',
                    'usr', 'lib', multiarch_tuple,
                    'libcurl{}.so.4'.format(suffix),
                )

                if os.path.exists(src):
                    print(
                        '# Setting up mock scout library {} as {}'.format(
                            src, dest,
                        ),
                        file=sys.stderr,
                    )
                    shutil.copy2(src, dest)

                    if (
                        'SRT_DEBUG_LIBCURL_COMPAT' in os.environ
                        and shutil.which('objdump') is not None
                    ):
                        subprocess.run(
                            ['objdump', '-T', '-x', dest],
                            check=False,
                            stdout=2,
                        )

                    # The "official" SONAME is libcurl(,-gnutls).so.4,
                    # but it's also symlinked as libcurl(,-gnutls).so.3
                    # and libsteam-runtime-our-libcurl(,-gnutls).so.4
                    shutil.copy2(src, dest)
                    os.symlink(
                        'libcurl{}.so.4'.format(suffix),
                        os.path.join(
                            self.tmpdir.name, 'steam-runtime',
                            'usr', 'lib', multiarch_tuple,
                            'libcurl{}.so.3'.format(suffix),
                        )
                    )
                    os.symlink(
                        'libcurl{}.so.4'.format(suffix),
                        os.path.join(
                            self.tmpdir.name, 'steam-runtime',
                            'usr', 'lib', multiarch_tuple,
                            'libsteam-runtime-our-libcurl{}.so.4'.format(
                                suffix,
                            ),
                        )
                    )

                src = os.path.join(
                    self.test_fixtures_srcdir, 'scout',
                    'libcurl3{}.symbols'.format(suffix),
                )
                dest = os.path.join(
                    self.tmpdir.name, 'steam-runtime',
                    'usr', 'lib', 'steamrt', 'expectations',
                    multiarch_tuple,
                    'libcurl3{}.symbols'.format(suffix),
                )
                shutil.copy2(src, dest)

                src = os.path.join(
                    self.test_fixtures_srcdir, '4',
                    'libcurl4{}.symbols'.format(suffix),
                )
                dest = os.path.join(
                    self.tmpdir.name, 'steam-runtime',
                    'usr', 'lib', 'steamrt', 'libcurl-compat', 'expectations',
                    multiarch_tuple, 'libcurl4{}.symbols'.format(suffix),
                )
                shutil.copy2(src, dest)

    def install_mock_system_library(
        self,
        version: str,
    ) -> None:
        '''
        Install one of our mock system libraries into
        ${tmpdir}/os-lib/${multiarch}/.
        '''
        for multiarch_tuple in self.architectures:
            for suffix in self.suffixes:
                if 'SRT_TEST_UNINSTALLED' in os.environ:
                    mock_os_lib_dir = os.path.join(
                        self.test_fixtures_builddir, version,
                    )
                else:
                    mock_os_lib_dir = os.path.join(
                        self.test_fixtures_builddir, version, multiarch_tuple,
                    )

                if version == '3':
                    soversion = '3'
                else:
                    soversion = '4'

                src = os.path.join(
                    mock_os_lib_dir, 'libcurl{}.so.{}'.format(
                        suffix, soversion,
                    ),
                )
                dest = os.path.join(
                    self.tmpdir.name, 'os-lib', multiarch_tuple,
                    'libcurl{}.so.4'.format(suffix),
                )

                print(
                    '# Setting up mock system library {} as {}'.format(
                        src, dest,
                    ),
                    file=sys.stderr,
                )
                shutil.copy2(src, dest)

                if (
                    'SRT_DEBUG_LIBCURL_COMPAT' in os.environ
                    and shutil.which('objdump') is not None
                ):
                    subprocess.run(
                        ['objdump', '-T', '-x', dest],
                        check=False,
                        stdout=2,
                    )

                # The "official" SONAME is libcurl(,-gnutls).so.4,
                # but libcurl(,-gnutls).so.3 is a symlink to that.
                shutil.copy2(src, dest)
                os.symlink(
                    'libcurl{}.so.4'.format(suffix),
                    os.path.join(
                        self.tmpdir.name, 'os-lib', multiarch_tuple,
                        'libcurl{}.so.3'.format(suffix),
                    ),
                )

    def run_compat_setup(
        self,
        runtime_optional: bool = False,
    ) -> None:
        '''
        Run steam-runtime-libcurl-compat-setup to set up pinned libraries.
        In real life this would be run by setup.sh, normally from outside
        the LD_LIBRARY_PATH runtime, but here we use SYSTEM_LD_LIBRARY_PATH
        to ensure that the Steam Runtime's LD_LIBRARY_PATH gets overridden.
        '''
        argv = [self.compat_setup]

        if runtime_optional:
            argv.append('--runtime-optional')

        argv.append(os.path.join(self.tmpdir.name, 'steam-runtime'))
        run_subprocess(
            [
                'env',
                'SYSTEM_LD_LIBRARY_PATH={}'.format(
                    ':'.join(
                        os.path.join(self.tmpdir.name, 'os-lib', m)
                        for m in self.architectures
                    ),
                ),
                'G_MESSAGES_DEBUG=all',
            ] + argv,
            check=True,
            stdout=2,
        )
        run_subprocess(['find', '.', '-ls'], cwd=self.tmpdir.name, stdout=2)

    def run_print_version_in_mock_runtime(
        self,
        arch: KnownArchitecture,
        suffix: str,
        version: str
    ) -> MyCompletedProcess:
        '''
        Run a libcurl user that will print which libcurl it found.
        This implicitly asserts that the libcurl user succeeds.

        In real life this would be a game or a tool like valgrind, and
        it would be run via run.sh. The LD_LIBRARY_PATH here is a mockup
        of run.sh.
        '''
        paths = []      # type: typing.List[str]

        for a in self.architectures.values():
            for subdir_prefix in ('libcurl_compat', 'pinned_libs'):
                paths.append(
                    os.path.join(
                        self.tmpdir.name, 'steam-runtime',
                        '{}_{}'.format(subdir_prefix, a.word_size),
                    ),
                )

        for a in self.architectures.values():
            paths.append(
                os.path.join(
                    self.tmpdir.name, 'os-lib', a.multiarch_tuple,
                ),
            )

        for a in self.architectures.values():
            paths.append(
                os.path.join(
                    self.tmpdir.name, 'steam-runtime',
                    'usr', 'lib', a.multiarch_tuple,
                ),
            )

        return run_subprocess(
            [
                'env', 'LD_LIBRARY_PATH={}'.format(':'.join(paths)),
                os.path.join(
                    self.tmpdir.name,
                    '{}-steamrt-print-libcurl{}{}-version'.format(
                        arch.multiarch_tuple,
                        version,
                        suffix,
                    ),
                )
            ],
            check=True,
            stdout=subprocess.PIPE,
        )

    def run_print_version_in_real_runtime(
        self,
        arch: KnownArchitecture,
        suffix: str,
        version: str
    ) -> MyCompletedProcess:
        '''
        Run a libcurl user that will print which libcurl it found.
        This implicitly asserts that the libcurl user succeeds.
        '''
        if not self.original_ld_library_path:
            self.skipTest('This test must be run in the Steam Runtime')

        return run_subprocess(
            [
                'env', 'LD_LIBRARY_PATH={}'.format(
                    self.original_ld_library_path
                ),
                os.path.join(
                    self.tmpdir.name,
                    '{}-steamrt-print-libcurl{}{}-version'.format(
                        arch.multiarch_tuple,
                        version,
                        suffix,
                    ),
                )
            ],
            check=True,
            stdout=subprocess.PIPE,
        )

    def assert_using_system_libcurl(self, runtime_optional=False):
        if runtime_optional:
            # If we want the shim to be runtime-optional, we create the
            # symlink in libcurl_compat_{word_size} and do not create any
            # symlink in pinned_libs_{word_size}.
            subdir_prefix = 'libcurl_compat'
            other_subdir_prefix = 'pinned_libs'
        else:
            # If we want the shim to be used all the time, we do the reverse.
            subdir_prefix = 'pinned_libs'
            other_subdir_prefix = 'libcurl_compat'

        for arch in self.architectures.values():
            for suffix in self.suffixes:
                for v in ('3', '4'):
                    # libcurl.so.4, libcurl-gnutls.so.3, etc. all point to the
                    # mock "system" library, which is compatible with both the
                    # OS and the scout ABI, even if those are not compatible
                    path = os.path.join(
                        self.tmpdir.name, 'steam-runtime',
                        '{}_{}'.format(subdir_prefix, arch.word_size),
                        'libcurl{}.so.{}'.format(suffix, v),
                    )
                    self.assertEqual(
                        os.readlink(path),
                        os.path.join(
                            self.tmpdir.name, 'os-lib', arch.multiarch_tuple,
                            'libcurl{}.so.4'.format(suffix),
                        ),
                    )
                    path = os.path.join(
                        self.tmpdir.name, 'steam-runtime',
                        '{}_{}'.format(other_subdir_prefix, arch.word_size),
                        'libcurl{}.so.{}'.format(suffix, v),
                    )
                    self.assertFalse(os.path.islink(path))
                    self.assertFalse(os.path.exists(path))

    def assert_using_shim_libcurl(self, runtime_optional=False):
        if runtime_optional:
            # Same as in assert_using_system_libcurl()
            subdir_prefix = 'libcurl_compat'
            other_subdir_prefix = 'pinned_libs'
        else:
            subdir_prefix = 'pinned_libs'
            other_subdir_prefix = 'libcurl_compat'

        for arch in self.architectures.values():
            for suffix in self.suffixes:
                for v in ('3', '4'):
                    # libcurl.so.4, libcurl-gnutls.so.3, etc. all point to the
                    # shim library, which is compatible with both the OS and
                    # the scout ABI, even if those are not compatible
                    path = os.path.join(
                        self.tmpdir.name, 'steam-runtime',
                        '{}_{}'.format(subdir_prefix, arch.word_size),
                        'libcurl{}.so.{}'.format(suffix, v),
                    )
                    self.assertEqual(
                        os.readlink(path),
                        (
                            '../usr/lib/{}/'
                            'libsteam-runtime-shim-libcurl{}.so.4'
                        ).format(
                            arch.multiarch_tuple,
                            suffix,
                        ),
                    )
                    path = os.path.join(
                        self.tmpdir.name, 'steam-runtime',
                        '{}_{}'.format(other_subdir_prefix, arch.word_size),
                        'libcurl{}.so.{}'.format(suffix, v),
                    )
                    self.assertFalse(os.path.islink(path))
                    self.assertFalse(os.path.exists(path))

                # There's a symlink to the system library for the shim
                # to open
                self.assertEqual(
                    os.readlink(
                        os.path.join(
                            self.tmpdir.name, 'steam-runtime',
                            '{}_{}'.format(subdir_prefix, arch.word_size),
                            'libsteam-runtime-system-libcurl{}.so.4'.format(
                                suffix,
                            ),
                        ),
                    ),
                    os.path.join(
                        self.tmpdir.name, 'os-lib', arch.multiarch_tuple,
                        'libcurl{}.so.4'.format(suffix),
                    )
                )
                path = os.path.join(
                    self.tmpdir.name, 'steam-runtime',
                    '{}_{}'.format(other_subdir_prefix, arch.word_size),
                    'libcurl{}.so.{}'.format(suffix, v),
                )
                self.assertFalse(os.path.islink(path))
                self.assertFalse(os.path.exists(path))

    def assert_nothing_pinned(self):
        for arch in self.architectures.values():
            for suffix in self.suffixes:
                for v in ('3', '4'):
                    # libcurl.so.4, libcurl-gnutls.so.3, etc. have not
                    # been pinned. setup.sh will fall back to its usual
                    # pinning logic
                    for prefix in ('', 'system_'):
                        for subdir_prefix in ('pinned_libs', 'libcurl_compat'):
                            path = os.path.join(
                                self.tmpdir.name, 'steam-runtime',
                                '{}_{}'.format(subdir_prefix, arch.word_size),
                                '{}libcurl{}.so.{}'.format(prefix, suffix, v),
                            )
                            self.assertFalse(os.path.islink(path))
                            self.assertFalse(os.path.exists(path))

    def test_mock_system_libcurl4(
        self,
        runtime_optional: bool = False,
    ) -> None:
        '''
        System libcurl4 is CURL_OPENSSL_4, etc.

        This simulates modern Debian libcurl4, Arch etc.
        '''
        # Mock up an OS library directory in which libcurl*.so.{3,4} is the
        # version that implements only the new ABI
        self.install_mock_system_library('4')

        self.run_compat_setup(runtime_optional=runtime_optional)
        self.assert_using_shim_libcurl(runtime_optional=runtime_optional)

        for multiarch_tuple, arch in self.architectures.items():
            for suffix in self.suffixes:
                helper = os.path.join(
                    self.tmpdir.name,
                    '{}-steamrt-print-libcurl{}-version'.format(
                        multiarch_tuple, suffix,
                    ),
                )

                if os.path.exists(helper):
                    # OS programs use the system library via the shim.
                    # If they are old programs using non-versioned symbols,
                    # the shim's link order has been carefully chosen to
                    # make sure they get the system library, which we expect
                    # to be better than scout's.
                    for v in ('', '4'):
                        completed = self.run_print_version_in_mock_runtime(
                            arch=arch, suffix=suffix, version=v,
                        )
                        self.assertEqual(
                            completed.stdout,
                            ('mockup of libcurl from host OS with '
                             'CURL_*_4 symbols\n').encode('ascii'),
                        )

                    # scout-compatible programs use the scout library
                    completed = self.run_print_version_in_mock_runtime(
                        arch=arch, suffix=suffix, version='3',
                    )
                    self.assertEqual(
                        completed.stdout,
                        b'mockup of libcurl from scout\n',
                    )
                else:
                    print('# {} not found'.format(helper), file=sys.stderr)

    def test_mock_system_libcurl4_runtime_optional(self) -> None:
        self.test_mock_system_libcurl4(runtime_optional=True)

    def test_mock_system_libcurl3(
        self,
        runtime_optional: bool = False,
    ) -> None:
        '''
        System libcurl3 is CURL_OPENSSL_3, etc.

        This simulates Debian libcurl3-gnutls
        '''
        # Mock up an OS library directory in which libcurl*.so.{3,4} is the
        # version that implements only the old ABI
        self.install_mock_system_library('3')

        self.run_compat_setup(runtime_optional=runtime_optional)
        self.assert_using_system_libcurl(runtime_optional=runtime_optional)

        # Both system and scout programs use the system libcurl, which
        # in this case is scout-compatible
        for multiarch_tuple, arch in self.architectures.items():
            for suffix in self.suffixes:
                helper = os.path.join(
                    self.tmpdir.name,
                    '{}-steamrt-print-libcurl{}-version'.format(
                        multiarch_tuple, suffix,
                    ),
                )

                if os.path.exists(helper):
                    for v in ('', '3'):
                        completed = self.run_print_version_in_mock_runtime(
                            arch=arch, suffix=suffix, version=v,
                        )
                        self.assertEqual(
                            completed.stdout,
                            ('mockup of libcurl from host OS with CURL_*_3 '
                             'symbols\n').encode('ascii'),
                        )
                else:
                    print('# {} not found'.format(helper), file=sys.stderr)

        # Don't assert that we can run steamrt-print-libcurl4-version:
        # we expect that to fail in this case, because we do not have
        # anything with the CURL_OPENSSL_4 ABI in our runtime

    def test_mock_system_libcurl3_runtime_optional(self) -> None:
        self.test_mock_system_libcurl3(runtime_optional=True)

    def test_mock_system_both(
        self,
        runtime_optional: bool = False,
    ) -> None:
        '''
        System libcurl4 is both CURL_OPENSSL_4 and CURL_OPENSSL_3, etc.

        This simulates what Debian libcurl3-gnutls should arguably do
        in future.
        '''
        # Mock up an OS library directory in which libcurl*.so.{3,4} is the
        # version that implements both ABIs
        self.install_mock_system_library('both')

        self.run_compat_setup(runtime_optional=runtime_optional)
        self.assert_using_system_libcurl(runtime_optional=runtime_optional)

        for multiarch_tuple, arch in self.architectures.items():
            for suffix in self.suffixes:
                # Both OS and scout programs use the system library,
                # because it claims to be compatible with both ABIs
                helper = os.path.join(
                    self.tmpdir.name,
                    '{}-steamrt-print-libcurl{}-version'.format(
                        multiarch_tuple, suffix,
                    ),
                )

                if os.path.exists(helper):
                    for v in ('', '3', '4'):
                        if suffix == '-gnutls':
                            expected_verdef = 'CURL_GNUTLS_{}'.format(v)
                        else:
                            expected_verdef = 'CURL_OPENSSL_{}'.format(v)

                        completed = self.run_print_version_in_mock_runtime(
                            arch=arch, suffix=suffix, version=v,
                        )

                        if v:
                            self.assertEqual(
                                completed.stdout.decode('ascii'),
                                'mock system libcurl ({} ABI)\n'.format(
                                    expected_verdef,
                                ),
                            )
                        else:
                            # It is unspecified whether the executable linked
                            # with unversioned symbols will see them resolving
                            # to version 3 or version 4, so use a regex that
                            # will match either way.
                            self.assertRegex(
                                completed.stdout.decode('ascii'),
                                r'^mock system libcurl \({}[34] ABI\)$'.format(
                                    expected_verdef,
                                ),
                            )
                else:
                    print('# {} not found'.format(helper), file=sys.stderr)

    def test_mock_system_both_runtime_optional(self) -> None:
        self.test_mock_system_both(runtime_optional=True)

    def test_mock_system_incompatible(
        self,
        runtime_optional: bool = False,
    ) -> None:
        '''
        System libcurl4 is incompatible with everything
        '''
        # Mock up an OS library directory in which libcurl*.so.{3,4} is
        # some hypothetical future version with different symbol-versions
        self.install_mock_system_library('incompatible')

        self.run_compat_setup(runtime_optional=runtime_optional)
        # Nothing has been pinned. This will result in fallback to
        # what setup.sh normally does, which involves a special case for
        # libcurl that is out-of-scope here.
        self.assert_nothing_pinned()

    def test_mock_system_incompatible_runtime_optional(self) -> None:
        self.test_mock_system_incompatible(runtime_optional=True)

    def test_mock_system_unversioned(
        self,
        runtime_optional: bool = False,
    ) -> None:
        '''
        System libcurl4 does not have versioned symbols
        '''
        # Mock up an OS library directory in which libcurl*.so.{3,4} is the
        # version that implements no symbol-versions at all
        self.install_mock_system_library('none')

        self.run_compat_setup(runtime_optional=runtime_optional)
        # Nothing has been pinned. This will result in fallback to
        # what setup.sh normally does, which involves a special case for
        # libcurl that is out-of-scope here.
        self.assert_nothing_pinned()

    def test_mock_system_unversioned_runtime_optional(self) -> None:
        self.test_mock_system_unversioned(runtime_optional=True)

    def test_real_system_libcurl(self) -> None:
        if 'SRT_TEST_UNINSTALLED' in os.environ:
            self.skipTest(
                'This test must be run in the Steam Runtime, not uninstalled'
            )

        if 'STEAM_RUNTIME' not in os.environ:
            self.skipTest(
                'This test must be run in the Steam Runtime'
            )

        if not self.original_ld_library_path:
            self.skipTest('This test must be run in the Steam Runtime')

        for arch in self.architectures.values():
            for suffix in self.suffixes:
                completed = run_subprocess(
                    [
                        os.path.join(
                            self.tmpdir.name,
                            '{}-steamrt-print-libcurl4{}-version'.format(
                                arch.multiarch_tuple,
                                suffix,
                            ),
                        )
                    ],
                    check=False,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )

                if completed.returncode == 0:
                    print(
                        ('# OS {} libcurl{} version {!r} is compatible with '
                         'upstream ABI').format(
                            arch.multiarch_tuple, suffix,
                            completed.stdout.strip(),
                        ),
                        file=sys.stderr,
                    )
                    os_version = completed.stdout

                    if 'STEAM_RUNTIME_USE_LIBCURL_SHIM' in os.environ:
                        completed = self.run_print_version_in_real_runtime(
                            arch=arch, suffix=suffix, version='4',
                        )
                        print(
                            '# Upstream-compatible {} libcurl{}: {!r}'.format(
                                arch.multiarch_tuple, suffix, completed.stdout,
                            ),
                            file=sys.stderr,
                        )
                        self.assertEqual(completed.stdout, os_version)
                    else:
                        print(
                            ('# Assuming we are intentionally not using '
                             'the libcurl shim? Set '
                             'STEAM_RUNTIME_USE_LIBCURL_SHIM if we are'),
                            file=sys.stderr,
                        )
                else:
                    print(
                        ('# OS {} libcurl{} is not compatible with '
                         'upstream ABI:\n{}').format(
                            arch.multiarch_tuple, suffix, completed.stderr,
                        ),
                        file=sys.stderr,
                    )

                # The scout ABI is always available, but it can be either
                # the OS version or the scout version, depending
                completed = self.run_print_version_in_real_runtime(
                    arch=arch, suffix=suffix, version='3',
                )
                print(
                    '# scout-compatible {} libcurl{}: {!r}'.format(
                        arch.multiarch_tuple, suffix, completed.stdout,
                    )
                )

                # Programs linked with unversioned symbols also work
                completed = self.run_print_version_in_real_runtime(
                    arch=arch, suffix=suffix, version='',
                )
                print(
                    '# legacy unversioned {} libcurl{}: {!r}'.format(
                        arch.multiarch_tuple, suffix, completed.stdout,
                    )
                )

    def tearDown(self) -> None:
        os.environ['LD_LIBRARY_PATH'] = self.original_ld_library_path
        super().tearDown()


if __name__ == '__main__':
    assert sys.version_info >= (3, 4), \
        'Python 3.4+ is required (configure with -Dpython=python3.5 ' \
        'if necessary)'

    test_main()

# vi: set sw=4 sts=4 et:
