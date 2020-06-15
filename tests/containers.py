#!/usr/bin/env python3
# Copyright 2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

"""
Test pressure-vessel against an out-of-band set of pre-prepared containers.

To run during build-time testing, build with
-Dtest_containers_dir=/path/to/containers, where /path/to/containers
ideally contains at least:

* pressure-vessel:
    A sufficiently recent copy of pressure-vessel, used for the parts
    that must run inside the container (because we cannot assume that
    an arbitrary build done on the host system is compatible with the
    libraries inside the container)
* steam-runtime:
    An LD_LIBRARY_PATH runtime, which we use to find versions of
    steam-runtime-system-info and capsule-capture-libs that can run
    on the host system
* scout/files:
    The Platform merged-/usr from the SteamLinuxRuntime depot
* scout_sysroot:
    An SDK sysroot like the one recommended for the Docker container

and run (for example) 'meson test -v -C _build' as usual.

The same test can also be run against a version of pressure-vessel
that was built against scout:

    export PRESSURE_VESSEL_TEST_CONTAINERS=/path/to/containers
    ./sysroot/run-in-sysroot.py --sysroot ../scout-sysroot -- \
        ninja -C _build-for-sysroot
    env DESTDIR="$(pwd)/_build-for-sysroot/DESTDIR" \
        ./sysroot/run-in-sysroot.py --sysroot ../scout-sysroot -- \
        ninja -C _build-for-sysroot install
    rm -fr "$PRESSURE_VESSEL_TEST_CONTAINERS/pressure-vessel"
    env DESTDIR="$(pwd)/_build-for-sysroot/DESTDIR" \
        ./sysroot/run-in-sysroot.py --sysroot ../scout-sysroot -- \
        python3.5 ./build-relocatable-install.py \
        --output="$PRESSURE_VESSEL_TEST_CONTAINERS/pressure-vessel" \
        --check-source-directory="$PRESSURE_VESSEL_TEST_CONTAINERS" \
        --allow-missing-sources \
        --srcdir="$(pwd)" --set-version "$(git describe)"
    ./tests/containers.py

or against the SteamLinuxRuntime depot that gets uploaded to the Steam CDN:

    export PRESSURE_VESSEL_TEST_CONTAINERS=../SteamLinuxRuntime/depot
    ./tests/containers.py

Influential environment variables:

* AUTOPKGTEST_ARTIFACTS:
    Write test artifacts to this directory (borrowed from Debian's
    autopkgtest framework) instead of a temporary directory. This makes
    debugging easier.
* BWRAP:
    A bubblewrap executable
* G_TEST_SRCDIR:
    The ./tests subdirectory of the source root, typically $(pwd)/tests
* G_TEST_BUILDDIR:
    The ./tests subdirectory of the build root, typically $(pwd)/_build/tests
* PRESSURE_VESSEL_LIBCAPSULE_TOOLS:
    Override the location of capsule-capture-libs etc.
* PRESSURE_VESSEL_TEST_CONTAINERS:
    A complete relocatable pressure-vessel installation, including its
    dependencies such as libsteam-runtime-tools
* PRESSURE_VESSEL_UNINSTALLED:
    Set when running from the source/build trees
* STEAM_RUNTIME_SYSTEM_INFO:
    Path to a steam-runtime-system-info executable for the host system

Please keep this script compatible with python3.4 so that it can be
run on the oldest platforms where pressure-vessel works:
SteamOS 2 'brewmaster', Debian 8 'jessie', Ubuntu 14.04 'trusty'.
"""

import contextlib
import fcntl
import json
import logging
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

try:
    import typing
    typing      # placate pyflakes
except ImportError:
    pass

from testutils import (
    BaseTest,
    run_subprocess,
    tee_file_and_stderr,
    test_main,
)


logger = logging.getLogger('test-containers')


class TestContainers(BaseTest):
    bwrap = None            # type: typing.Optional[str]
    containers_dir = ''
    host_srsi_parsed = {}   # type: typing.Dict[str, typing.Any]
    pv_dir = ''
    pv_wrap = ''

    @classmethod
    def setUpClass(cls) -> None:
        super().setUpClass()

        if not os.environ.get('PRESSURE_VESSEL_TEST_CONTAINERS', ''):
            raise unittest.SkipTest('Containers not available')

        bwrap = os.environ.get('BWRAP', shutil.which('bwrap'))

        if bwrap is not None and run_subprocess(
            [bwrap, '--dev-bind', '/', '/', 'sh', '-c', 'true'],
            stdout=2,
            stderr=2,
        ).returncode != 0:
            # We can only do tests that just do setup and don't actually
            # try to run the container.
            cls.bwrap = None
        else:
            cls.bwrap = bwrap

        cls.containers_dir = os.path.abspath(
            os.environ['PRESSURE_VESSEL_TEST_CONTAINERS']
        )

        cls.pv_dir = os.path.join(cls.tmpdir.name, 'pressure-vessel')
        os.makedirs(cls.pv_dir, exist_ok=True)

        if 'PRESSURE_VESSEL_UNINSTALLED' in os.environ:
            os.makedirs(os.path.join(cls.pv_dir, 'bin'))

            for exe in (
                'pressure-vessel-wrap',
            ):
                shutil.copy2(
                    os.path.join(cls.top_builddir, 'src', exe),
                    os.path.join(cls.pv_dir, 'bin', exe),
                )

            for exe in (
                'pressure-vessel-locale-gen',
            ):
                shutil.copy2(
                    os.path.join(cls.top_srcdir, exe),
                    os.path.join(cls.pv_dir, 'bin', exe),
                )

            for exe in (
                'pressure-vessel-try-setlocale',
                'pressure-vessel-with-lock',
            ):
                in_containers_dir = os.path.join(
                    cls.containers_dir,
                    'pressure-vessel',
                    'bin',
                    exe,
                )

                if os.path.exists(in_containers_dir):
                    # Asssume it's a close enough version that we can
                    # use it with the newer pressure-vessel-wrap.
                    # We don't necessarily want to use versions from
                    # the builddir because they can have dependencies
                    # that are newer than the container's libraries.
                    shutil.copy2(
                        in_containers_dir,
                        os.path.join(cls.pv_dir, 'bin', exe),
                    )
                else:
                    shutil.copy2(
                        os.path.join(cls.top_builddir, 'src', exe),
                        os.path.join(cls.pv_dir, 'bin', exe),
                    )

            for d in (
                'subprojects',
            ):
                shutil.copytree(
                    os.path.join(cls.top_builddir, d),
                    os.path.join(cls.pv_dir, d),
                    symlinks=True,
                )

            fallback_libcapsule_tools = os.path.join(
                cls.containers_dir,
                'steam-runtime', 'usr', 'lib', 'libcapsule', 'relocatable',
            )

            if not os.path.isdir(fallback_libcapsule_tools):
                fallback_libcapsule_tools = '/usr/lib/libcapsule/relocatable'

            for multiarch in ('i386-linux-gnu', 'x86_64-linux-gnu'):
                for tool in ('capsule-capture-libs', 'capsule-symbols'):
                    exe = multiarch + '-' + tool
                    tool_path = os.path.join(cls.pv_dir, 'bin', exe)

                    exe = multiarch + '-' + tool
                    tool_path = os.path.join(cls.pv_dir, 'bin', exe)
                    found = shutil.which(exe)
                    relocatable = os.path.join(
                        os.environ.get(
                            'PRESSURE_VESSEL_LIBCAPSULE_TOOLS',
                            fallback_libcapsule_tools,
                        ),
                        exe,
                    )

                    if found is not None:
                        shutil.copy2(found, tool_path)
                    elif os.path.isfile(relocatable):
                        shutil.copy2(relocatable, tool_path)
                    else:
                        raise unittest.SkipTest('{} not found'.format(exe))
        else:
            cls.pv_dir = os.path.join(cls.containers_dir, 'pressure-vessel')

            if not os.path.isdir(cls.pv_dir):
                raise unittest.SkipTest('{} not found'.format(cls.pv_dir))

        cls.pv_wrap = os.path.join(cls.pv_dir, 'bin', 'pressure-vessel-wrap')

        host_srsi = os.getenv('STEAM_RUNTIME_SYSTEM_INFO')

        if host_srsi is None:
            rt = os.path.join(cls.containers_dir, 'steam-runtime')
            cpu = os.uname().machine

            if cpu == 'x86_64':
                arch = 'amd64'
            elif cpu.startswith('i') and len(cpu) == 4 and cpu.endswith('86'):
                arch = 'i386'
            else:
                arch = cpu

            host_srsi = shutil.which(
                os.path.join(
                    rt, arch, 'usr', 'bin', 'steam-runtime-system-info',
                )
            )

        if host_srsi is None:
            host_srsi = shutil.which('steam-runtime-system-info')

        if host_srsi is not None:
            with open(
                os.path.join(cls.artifacts, 'host-srsi.json'),
                'w',
            ) as writer:
                run_subprocess(
                    [
                        host_srsi,
                        '--verbose',
                    ],
                    cwd=cls.artifacts,
                    stdout=writer,
                    stderr=2,
                    universal_newlines=True,
                )

            os.environ['HOST_STEAM_RUNTIME_SYSTEM_INFO_JSON'] = os.path.join(
                cls.artifacts, 'host-srsi.json',
            )

            with open(
                os.path.join(cls.artifacts, 'host-srsi.json'),
                'r',
            ) as reader:
                cls.host_srsi_parsed = json.load(reader)
        else:
            os.environ.pop('HOST_STEAM_RUNTIME_SYSTEM_INFO_JSON', None)
            cls.host_srsi_parsed = {}

        try:
            os.environ['HOST_LD_LINUX_SO_REALPATH'] = os.path.realpath(
                '/lib/ld-linux.so.2'
            )
        except OSError:
            os.environ.pop('HOST_LD_LINUX_SO_REALPATH', None)

        try:
            os.environ['HOST_LD_LINUX_X86_64_SO_REALPATH'] = os.path.realpath(
                '/lib64/ld-linux-x86-64.so.2'
            )
        except OSError:
            os.environ.pop('HOST_LD_LINUX_X86_64_SO_REALPATH', None)

    def setUp(self) -> None:
        super().setUp()
        cls = self.__class__
        self.bwrap = cls.bwrap
        self.containers_dir = cls.containers_dir
        self.host_srsi_parsed = cls.host_srsi_parsed
        self.pv_dir = cls.pv_dir
        self.pv_wrap = cls.pv_wrap

        # The artifacts directory is going to be the current working
        # directory inside the container, so we copy things we will
        # need into that directory.
        os.makedirs(os.path.join(cls.artifacts, 'tmp'), exist_ok=True)

        for f in ('testutils.py', 'inside-scout.py'):
            shutil.copy2(
                os.path.join(cls.G_TEST_SRCDIR, f),
                os.path.join(cls.artifacts, 'tmp', f),
            )

        # This parsing is sufficiently "cheap" that we repeat it for
        # each test-case rather than introducing more class variables.

        self.host_os_release = cls.host_srsi_parsed.get('os-release', {})

        if self.host_os_release.get('id') == 'debian':
            logger.info('Host OS is Debian')
            self.host_is_debian_derived = True
        elif 'debian' in self.host_os_release.get('id_like', []):
            logger.info('Host OS is Debian-derived')
            self.host_is_debian_derived = True
        else:
            logger.info('Host OS is not Debian-derived')
            self.host_is_debian_derived = False

    def tearDown(self) -> None:
        with contextlib.suppress(FileNotFoundError):
            shutil.rmtree(os.path.join(self.artifacts, 'tmp'))

        super().tearDown()

    @classmethod
    def tearDownClass(cls) -> None:
        super().tearDownClass()

    def run_subprocess(
        self,
        args,           # type: typing.Union[typing.List[str], str]
        check=False,
        input=None,     # type: typing.Optional[bytes]
        timeout=None,   # type: typing.Optional[int]
        **kwargs        # type: typing.Any
    ):
        logger.info('Running: %r', args)
        return run_subprocess(
            args, check=check, input=input, timeout=timeout, **kwargs
        )

    def _test_scout(
        self,
        test_name: str,
        scout: str,
        *,
        copy: bool = False,
        gc: bool = True,
        locales: bool = False,
        only_prepare: bool = False
    ) -> None:
        if self.bwrap is None and not only_prepare:
            self.skipTest('Unable to run bwrap (in a container?)')

        if not os.path.isdir(scout):
            self.skipTest('{} not found'.format(scout))

        artifacts = os.path.join(
            self.artifacts,
            test_name,
        )
        os.makedirs(artifacts, exist_ok=True)

        argv = [
            self.pv_wrap,
            '--runtime', scout,
            '--verbose',
        ]

        var = os.path.join(self.containers_dir, 'var')
        os.makedirs(var, exist_ok=True)

        if not locales:
            argv.append('--no-generate-locales')

        with tempfile.TemporaryDirectory(prefix='test-', dir=var) as temp:
            if copy:
                argv.extend(['--copy-runtime-into', temp])

                if not gc:
                    argv.append('--no-gc-runtimes')

            if only_prepare:
                argv.append('--only-prepare')
            else:
                argv.extend([
                    '--',
                    'env',
                    'TEST_INSIDE_SCOUT_ARTIFACTS=' + artifacts,
                    'TEST_INSIDE_SCOUT_LOCALES=' + ('1' if locales else ''),
                    'python3.5',
                    os.path.join(self.artifacts, 'tmp', 'inside-scout.py'),
                ])

            # Create directories representing previous runs of
            # pressure-vessel-wrap, so that we can assert that they are
            # GC'd (or not) as desired.

            # Do not delete because its name does not start with tmp-
            os.makedirs(os.path.join(temp, 'donotdelete'), exist_ok=True)
            # Delete
            os.makedirs(os.path.join(temp, 'tmp-deleteme'), exist_ok=True)
            # Delete, and assert that it is recursive
            os.makedirs(
                os.path.join(temp, 'tmp-deleteme2', 'usr', 'lib'),
                exist_ok=True,
            )
            # Do not delete because it has ./keep
            os.makedirs(os.path.join(temp, 'tmp-keep', 'keep'), exist_ok=True)
            # Do not delete because we will read-lock .ref
            os.makedirs(os.path.join(temp, 'tmp-rlock'), exist_ok=True)
            # Do not delete because we will write-lock .ref
            os.makedirs(os.path.join(temp, 'tmp-wlock'), exist_ok=True)

            with open(
                os.path.join(temp, 'tmp-rlock', '.ref'), 'w+'
            ) as rlock_writer, open(
                os.path.join(temp, 'tmp-wlock', '.ref'), 'w'
            ) as wlock_writer, tee_file_and_stderr(
                os.path.join(artifacts, 'inside-scout.log')
            ) as tee:
                lockdata = struct.pack('hhlli', fcntl.F_RDLCK, 0, 0, 0, 0)
                fcntl.fcntl(rlock_writer.fileno(), fcntl.F_SETLKW, lockdata)
                lockdata = struct.pack('hhlli', fcntl.F_WRLCK, 0, 0, 0, 0)
                fcntl.fcntl(wlock_writer.fileno(), fcntl.F_SETLKW, lockdata)

                # Put this in a subtest so that if it fails, we still get
                # to inspect the copied sysroot
                with self.subTest('run', copy=copy, scout=scout):
                    completed = self.run_subprocess(
                        argv,
                        cwd=self.artifacts,
                        stdout=tee.stdin,
                        stderr=tee.stdin,
                        universal_newlines=True,
                    )
                    self.assertEqual(completed.returncode, 0)

            if copy:
                members = set(os.listdir(temp))

                self.assertIn('.ref', members)
                self.assertIn('donotdelete', members)
                self.assertIn('tmp-keep', members)
                self.assertIn('tmp-rlock', members)
                self.assertIn('tmp-wlock', members)
                if gc:
                    self.assertNotIn('tmp-deleteme', members)
                    self.assertNotIn('tmp-deleteme2', members)
                else:
                    # These would have been deleted if not for --no-gc-runtimes
                    self.assertIn('tmp-deleteme', members)
                    self.assertIn('tmp-deleteme2', members)

                members.discard('.ref')
                members.discard('donotdelete')
                members.discard('tmp-deleteme')
                members.discard('tmp-deleteme2')
                members.discard('tmp-keep')
                members.discard('tmp-rlock')
                members.discard('tmp-wlock')
                # After discarding those, there should be exactly one left:
                # the one we just created
                self.assertEqual(len(members), 1)
                tree = os.path.join(temp, members.pop())

                with self.subTest('mutable sysroot'):
                    self._assert_mutable_sysroot(
                        tree,
                        artifacts,
                        is_scout=True,
                    )

    def _assert_mutable_sysroot(
        self,
        tree: str,
        artifacts: str,
        *,
        is_scout: bool = True
    ) -> None:
        with open(
            os.path.join(artifacts, 'contents.txt'),
            'w',
        ) as writer:
            self.run_subprocess([
                'find',
                '.',
                '-ls',
            ], cwd=tree, stderr=2, stdout=writer)

        self.assertTrue(os.path.isdir(os.path.join(tree, 'bin')))
        self.assertTrue(os.path.isdir(os.path.join(tree, 'etc')))
        self.assertTrue(os.path.isdir(os.path.join(tree, 'lib')))
        self.assertTrue(os.path.isdir(os.path.join(tree, 'usr')))
        self.assertFalse(
            os.path.isdir(os.path.join(tree, 'usr', 'usr'))
        )
        self.assertTrue(os.path.isdir(os.path.join(tree, 'sbin')))

        self.assertTrue(
            os.path.isdir(os.path.join(tree, 'overrides', 'lib')),
        )
        self.assertTrue(
            os.path.isfile(
                os.path.join(
                    tree, 'usr', 'lib', 'pressure-vessel', 'from-host',
                    'bin', 'pressure-vessel-with-lock',
                )
            )
        )

        for multiarch, arch_info in self.host_srsi_parsed.get(
            'architectures', {}
        ).items():
            libdir = os.path.join(tree, 'overrides', 'lib', multiarch)
            with self.subTest(arch=multiarch):

                self.assertTrue(os.path.isdir(libdir))

                if is_scout:
                    for soname in (
                        'libBrokenLocale.so.1',
                        'libanl.so.1',
                        'libc.so.6',
                        'libcrypt.so.1',
                        'libdl.so.2',
                        'libm.so.6',
                        'libnsl.so.1',
                        'libpthread.so.0',
                        'libresolv.so.2',
                        'librt.so.1',
                        'libutil.so.1',
                    ):
                        # These are from glibc, which is depended on by
                        # Mesa, and is at least as new as scout's version
                        # in every supported version of the Steam Runtime.
                        with self.subTest(soname=soname):
                            target = os.readlink(
                                os.path.join(libdir, soname)
                            )
                            self.assertRegex(target, r'^/run/host/')

                    for soname in (
                        'libSDL-1.2.so.0',
                        'libfltk.so.1.1',
                    ):
                        with self.subTest(soname=soname):
                            with self.assertRaises(FileNotFoundError):
                                os.readlink(os.path.join(libdir, soname))

                # Keys are the basename of a DRI driver.
                # Values are lists of paths to DRI drivers of that name
                # on the host. We assert that for each name, an arbitrary
                # one of the DRI drivers of that name appears in the
                # container.
                expect_symlinks = {
                }    # type: typing.Dict[str, typing.List[str]]

                for dri in arch_info.get('dri_drivers', ()):
                    path = dri['library_path']

                    if path.startswith((    # any of:
                        '/usr/lib/dri/',
                        '/usr/lib32/dri/',
                        '/usr/lib64/dri/',
                        '/usr/lib/{}/dri/'.format(multiarch),
                    )):
                        # We don't make any assertion about the search
                        # order here.

                        # Take the realpath() on non-Debian-derived hosts,
                        # because on Arch Linux, we find drivers in
                        # /usr/lib64 that are physically in /usr/lib.
                        # Be more strict on Debian because we know more
                        # about the canonical paths there.
                        if not self.host_is_debian_derived:
                            with contextlib.suppress(OSError):
                                path = os.path.realpath(path)

                        expect_symlinks.setdefault(
                            os.path.basename(path), []
                        ).append(path)

                for k, vs in expect_symlinks.items():
                    with self.subTest(dri_symlink=k):
                        link = os.path.join(libdir, 'dri', k)
                        target = os.readlink(link)
                        self.assertEqual(target[:10], '/run/host/')
                        target = target[9:]     # includes the / after host/

                        # Again, take the realpath() on non-Debian-derived
                        # hosts, but be more strict on Debian.
                        if not self.host_is_debian_derived:
                            with contextlib.suppress(OSError):
                                target = os.path.realpath(target)

                        self.assertIn(target, vs)

                if is_scout:
                    if self.host_is_debian_derived:
                        link = os.path.join(
                            tree, 'usr', 'lib', multiarch, 'gconv',
                        )
                        target = os.readlink(link)
                        self.assertEqual(
                            target,
                            '/run/host/usr/lib/{}/gconv'.format(multiarch),
                        )

                    if os.path.isdir('/usr/share/libdrm'):
                        link = os.path.join(
                            tree, 'usr', 'share', 'libdrm',
                        )
                        target = os.readlink(link)
                        self.assertEqual(target, '/run/host/usr/share/libdrm')

        if is_scout:
            if os.path.isdir('/usr/lib/locale'):
                link = os.path.join(tree, 'usr', 'lib', 'locale')
                target = os.readlink(link)
                self.assertEqual(target, '/run/host/usr/lib/locale')

            if os.path.isdir('/usr/share/i18n'):
                link = os.path.join(tree, 'usr', 'share', 'i18n')
                target = os.readlink(link)
                self.assertEqual(target, '/run/host/usr/share/i18n')

            link = os.path.join(tree, 'sbin', 'ldconfig')
            target = os.readlink(link)
            # Might not be /sbin/ldconfig, for example on non-Debian hosts
            self.assertRegex(target, r'^/run/host/')

            if os.path.isfile('/usr/bin/locale'):
                link = os.path.join(tree, 'usr', 'bin', 'locale')
                target = os.readlink(link)
                self.assertEqual(target, '/run/host/usr/bin/locale')

            if os.path.isfile('/usr/bin/localedef'):
                link = os.path.join(tree, 'usr', 'bin', 'localedef')
                target = os.readlink(link)
                self.assertEqual(target, '/run/host/usr/bin/localedef')

    def test_scout_sysroot(self) -> None:
        scout = os.path.join(self.containers_dir, 'scout_sysroot')

        if os.path.isdir(os.path.join(scout, 'files')):
            scout = os.path.join(scout, 'files')

        with self.subTest('only-prepare'):
            self._test_scout(
                'scout_sysroot_prep', scout,
                copy=True, only_prepare=True,
            )

        with self.subTest('copy'):
            self._test_scout('scout_sysroot_copy', scout, copy=True, gc=False)

        with self.subTest('transient'):
            self._test_scout('scout_sysroot', scout, locales=True)

    def test_scout_usr(self) -> None:
        scout = os.path.join(self.containers_dir, 'scout', 'files')

        with self.subTest('only-prepare'):
            self._test_scout('scout_prep', scout, copy=True, only_prepare=True)

        with self.subTest('copy'):
            self._test_scout('scout_copy', scout, copy=True, locales=True)

        with self.subTest('transient'):
            self._test_scout('scout', scout)


if __name__ == '__main__':
    assert sys.version_info >= (3, 4), \
        'Python 3.4+ is required'

    test_main()

# vi: set sw=4 sts=4 et:
