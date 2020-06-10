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
import logging
import os
import shutil
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
        else:
            os.environ.pop('HOST_STEAM_RUNTIME_SYSTEM_INFO_JSON', None)

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

            with tee_file_and_stderr(
                os.path.join(artifacts, 'inside-scout.log')
            ) as tee:
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
                members.discard('.ref')
                self.assertEqual(len(members), 1)
                tree = os.path.join(temp, members.pop())

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
            self._test_scout('scout_sysroot_copy', scout, copy=True)

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
