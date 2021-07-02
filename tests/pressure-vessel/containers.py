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
* soldier/files:
    The Platform merged-/usr from the SteamLinuxRuntime depot
* scout_sysroot, soldier_sysroot:
    An SDK sysroot like the one recommended for the Docker container
* scout_sysroot_usrmerge:
    The same, but /usr-merged

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
        python3.5 ./pressure-vessel/build-relocatable-install.py \
        --output="$PRESSURE_VESSEL_TEST_CONTAINERS/pressure-vessel" \
        --check-source-directory="$PRESSURE_VESSEL_TEST_CONTAINERS" \
        --allow-missing-sources \
        --srcdir="$(pwd)" --set-version "$(git describe)"
    ./tests/pressure-vessel/containers.py

or against the SteamLinuxRuntime depot that gets uploaded to the Steam CDN:

    export PRESSURE_VESSEL_TEST_CONTAINERS=../SteamLinuxRuntime/depot
    ./tests/pressure-vessel/containers.py

Influential environment variables:

* AUTOPKGTEST_ARTIFACTS:
    Write test artifacts to this directory (borrowed from Debian's
    autopkgtest framework) instead of a temporary directory. This makes
    debugging easier.
* BWRAP:
    A bubblewrap executable
* G_TEST_SRCDIR:
    The ./tests/pressure-vessel subdirectory of the source root,
    typically $(pwd)/tests/pressure-vessel
* G_TEST_BUILDDIR:
    The ./tests/pressure-vessel subdirectory of the build root,
    typically $(pwd)/_build/tests/pressure-vessel
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
import glob
import json
import logging
import os
import shutil
import struct
import sys
import tempfile
import time
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


class FilesystemEnvVar:
    def __init__(
        self,
        name,       # type: str
        *,
        plural=False,
        read_only=False
    ) -> None:
        self.name = name
        self.plural = plural
        self.read_only = read_only

    def __str__(self) -> str:
        return self.name


FILESYSTEM_ENV_VARS = [
    FilesystemEnvVar(
        'PRESSURE_VESSEL_FILESYSTEMS_RO', plural=True, read_only=True,
    ),
    FilesystemEnvVar('PRESSURE_VESSEL_FILESYSTEMS_RW', plural=True),
    FilesystemEnvVar('STEAM_COMPAT_APP_LIBRARY_PATH'),
    FilesystemEnvVar('STEAM_COMPAT_APP_LIBRARY_PATHS', plural=True),
    FilesystemEnvVar('STEAM_COMPAT_CLIENT_INSTALL_PATH'),
    FilesystemEnvVar('STEAM_COMPAT_DATA_PATH'),
    FilesystemEnvVar('STEAM_COMPAT_INSTALL_PATH'),
    FilesystemEnvVar('STEAM_COMPAT_LIBRARY_PATHS', plural=True),
    FilesystemEnvVar('STEAM_COMPAT_MOUNT_PATHS', plural=True),
    FilesystemEnvVar('STEAM_COMPAT_MOUNTS', plural=True),
    FilesystemEnvVar('STEAM_COMPAT_SHADER_PATH'),
    FilesystemEnvVar('STEAM_COMPAT_TOOL_PATH'),
    FilesystemEnvVar('STEAM_COMPAT_TOOL_PATHS', plural=True),
    FilesystemEnvVar('STEAM_EXTRA_COMPAT_TOOLS_PATHS', plural=True),
]


class TestContainers(BaseTest):
    bwrap = None            # type: typing.Optional[str]
    containers_dir = ''
    host_srsi = None        # type: typing.Optional[str]
    host_srsi_parsed = {}   # type: typing.Dict[str, typing.Any]
    pv_dir = ''
    pv_wrap = ''

    @staticmethod
    def copy2(src, dest):
        logger.info('Copying %r to %r', src, dest)
        shutil.copy2(src, dest)

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

        for var in FILESYSTEM_ENV_VARS:
            paths = []

            if var.plural:
                names = [var.name + '_n1', var.name + '_n2']
            else:
                # Use an inconvenient name with colon and space
                # to check that we handle those correctly
                names = [var.name + ': .d']

            for name in names:
                path = os.path.join(cls.tmpdir.name, name)
                os.makedirs(path, exist_ok=True)
                paths.append(path)

            os.environ[var.name] = ':'.join(paths)

        if 'PRESSURE_VESSEL_UNINSTALLED' in os.environ:
            os.makedirs(os.path.join(cls.pv_dir, 'bin'))
            os.makedirs(
                os.path.join(cls.pv_dir, 'libexec', 'steam-runtime-tools-0'),
            )

            for exe in (
                'pressure-vessel-wrap',
            ):
                cls.copy2(
                    os.path.join(cls.top_builddir, 'pressure-vessel', exe),
                    os.path.join(cls.pv_dir, 'bin', exe),
                )

            for exe in (
                'pressure-vessel-locale-gen',
            ):
                cls.copy2(
                    os.path.join(cls.top_srcdir, 'pressure-vessel', exe),
                    os.path.join(cls.pv_dir, 'bin', exe),
                )

            for exe in (
                'pressure-vessel-adverb',
                'pressure-vessel-try-setlocale',
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
                    logger.info(
                        'Copying pre-existing %s from %s',
                        exe, in_containers_dir,
                    )
                    cls.copy2(
                        in_containers_dir,
                        os.path.join(cls.pv_dir, 'bin', exe),
                    )
                else:
                    logger.info(
                        'Copying just-built %s from %s/pressure-vessel',
                        exe, cls.top_builddir,
                    )
                    cls.copy2(
                        os.path.join(cls.top_builddir, 'pressure-vessel', exe),
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

            for multiarch in ('i386-linux-gnu', 'x86_64-linux-gnu'):
                for tool in ('inspect-library', 'capsule-capture-libs'):
                    exe = multiarch + '-' + tool
                    tool_path = os.path.join(
                        cls.pv_dir,
                        'libexec',
                        'steam-runtime-tools-0',
                        exe,
                    )
                    in_containers_dir = os.path.join(
                        cls.containers_dir,
                        'pressure-vessel',
                        'libexec',
                        'steam-runtime-tools-0',
                        exe,
                    )

                    if os.path.exists(in_containers_dir):
                        # Asssume it's a close enough version that we can
                        # use it with the newer pressure-vessel-wrap.
                        logger.info(
                            'Copying pre-existing %s from %s',
                            exe, in_containers_dir,
                        )
                        cls.copy2(
                            in_containers_dir,
                            tool_path,
                        )
                    else:
                        logger.info(
                            'Copying just-built %s from %s/pressure-vessel',
                            exe, cls.top_builddir,
                        )
                        cls.copy2(
                            os.path.join(
                                cls.top_builddir, 'helpers', exe,
                            ),
                            tool_path,
                        )
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

        cls.host_srsi = host_srsi

        if host_srsi is not None:
            logger.info("We have the host srsi %s", host_srsi)
            with open(
                os.path.join(cls.artifacts, 'host-srsi.json'),
                'w',
            ) as writer:
                run_subprocess(
                    [
                        'env',
                        'LD_BIND_NOW=1',
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
            logger.info("The host srsi is missing")
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
        self.host_srsi = cls.host_srsi
        self.host_srsi_parsed = cls.host_srsi_parsed
        self.pv_dir = cls.pv_dir
        self.pv_wrap = cls.pv_wrap

        # The artifacts directory is going to be the current working
        # directory inside the container, so we copy things we will
        # need into that directory.
        os.makedirs(os.path.join(cls.artifacts, 'tmp'), exist_ok=True)

        for f in ('testutils.py', 'inside-runtime.py'):
            self.copy2(
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
        runtime: str,
        *,
        copy: bool = False,
        fake_home: bool = False,
        gc: bool = True,
        gc_legacy: bool = True,
        locales: bool = False,
        only_prepare: bool = False
    ) -> None:
        self._test_container(
            test_name,
            runtime,
            copy=copy,
            fake_home=fake_home,
            gc=gc,
            gc_legacy=gc_legacy,
            is_scout=True,
            locales=locales,
            only_prepare=only_prepare,
        )

    def _test_soldier(
        self,
        test_name: str,
        runtime: str,
        *,
        copy: bool = False,
        fake_home: bool = False,
        gc: bool = True,
        gc_legacy: bool = True,
        locales: bool = False,
        only_prepare: bool = False
    ) -> None:
        self._test_container(
            test_name,
            runtime,
            copy=copy,
            gc=gc,
            gc_legacy=gc_legacy,
            is_soldier=True,
            locales=locales,
            only_prepare=only_prepare,
        )

    def _test_container(
        self,
        test_name: str,
        runtime: str,
        *,
        archive: str = '',
        copy: bool = False,
        fake_home: bool = False,
        fast_path: bool = False,
        gc: bool = True,
        gc_legacy: bool = True,
        is_scout: bool = False,
        is_soldier: bool = False,
        locales: bool = False,
        only_prepare: bool = False
    ) -> None:
        if self.bwrap is None and not only_prepare:
            self.skipTest('Unable to run bwrap (in a container?)')

        if archive:
            if not os.path.isfile(archive):
                self.skipTest('{} not found'.format(archive))

            if fast_path and not os.path.isdir(runtime):
                self.skipTest('{} not found'.format(runtime))
        else:
            if not os.path.isdir(runtime):
                self.skipTest('{} not found'.format(runtime))

        start_time = time.time()
        logger.info('Testing: %s', test_name)

        artifacts = os.path.join(
            self.artifacts,
            test_name,
        )
        os.makedirs(artifacts, exist_ok=True)

        final_argv_temp = tempfile.NamedTemporaryFile()

        argv = [
            self.pv_wrap,
            '--verbose',
            '--write-final-argv', final_argv_temp.name,
            '--filesystem', self.artifacts,
        ]

        if archive and (fast_path or copy):
            argv.extend([
                '--runtime-archive', archive,
                # For simplicity, we rely on this below
                '--runtime-id', 'myruntime_0.1.2',
            ])
        elif archive:
            # Assume the archive is accompanied by a -buildid.txt file
            argv.extend([
                '--runtime-archive', archive,
            ])
        else:
            argv.extend(['--runtime', runtime])

        var_dir = os.path.join(self.containers_dir, 'var')
        os.makedirs(var_dir, exist_ok=True)

        if not locales:
            argv.append('--no-generate-locales')

        with tempfile.TemporaryDirectory(
            prefix='test-', dir=var_dir
        ) as temp, tempfile.TemporaryDirectory(
            prefix='test-mock-base-', dir=var_dir
        ) as mock_base, tempfile.TemporaryDirectory(
            prefix='test-fake-home-', dir=var_dir
        ) as fake_home_temp:
            argv.extend(['--runtime-base', mock_base])
            argv.extend(['--variable-dir', temp])

            if fast_path:
                # Pretend we had already run this runtime. Rather than
                # using the real runtime's name, for simplicity this is
                # a fake name.
                old_dir = os.path.join(temp, 'deploy-myruntime_0.1.2')

                run_subprocess(
                    ['cp', '-al', runtime, old_dir],
                    check=True,
                )

                # Exercise the code path where we don't have a mtree manifest:
                # this is important here because we're editing the runtime
                # in-place to have the OLD-DEPLOYMENT flag-file, but the
                # manifest doesn't include that, so if we're using a runtime
                # with a manifest, that part will fail.
                for manifest in ('usr-mtree.txt', 'usr-mtree.txt.gz'):
                    with contextlib.suppress(FileNotFoundError):
                        os.remove(os.path.join(old_dir, manifest))

                if os.path.isdir(os.path.join(old_dir, 'files')):
                    old_dir = os.path.join(old_dir, 'files')

                if os.path.isdir(os.path.join(old_dir, 'usr')):
                    old_dir = os.path.join(old_dir, 'usr')

                # Touch a flag file so we can detect that we reused the
                # old deployment
                with open(os.path.join(old_dir, 'OLD-DEPLOYMENT'), 'w'):
                    pass

            if copy:
                argv.append('--copy-runtime')
            else:
                argv.append('--no-copy-runtime')

            if gc:
                argv.append('--gc-runtimes')
            else:
                argv.append('--no-gc-runtimes')

            if gc_legacy:
                argv.append('--gc-legacy-runtimes')
            else:
                argv.append('--no-gc-legacy-runtimes')

            if fake_home:
                argv.append('--home=' + fake_home_temp)
            else:
                argv.append('--share-home')

            if is_scout:
                python = 'python3.5'
            else:
                python = 'python3'

            if only_prepare:
                argv.append('--only-prepare')
            else:
                argv.extend([
                    '--',
                    'env',
                    'TEST_INSIDE_RUNTIME_ARTIFACTS=' + artifacts,
                    'TEST_INSIDE_RUNTIME_IS_COPY=' + ('1' if copy else ''),
                    'TEST_INSIDE_RUNTIME_IS_HOME_UNSHARED=' + (
                        '1' if fake_home else ''
                    ),
                    'TEST_INSIDE_RUNTIME_IS_SCOUT=' + (
                        '1' if is_scout else ''
                    ),
                    'TEST_INSIDE_RUNTIME_IS_SOLDIER=' + (
                        '1' if is_soldier else ''
                    ),
                    'TEST_INSIDE_RUNTIME_LOCALES=' + ('1' if locales else ''),
                    python,
                    os.path.join(self.artifacts, 'tmp', 'inside-runtime.py'),
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
                os.path.join(temp, 'deploy-deleteme', 'usr', 'lib'),
                exist_ok=True,
            )
            # Do not delete because it has ./keep
            os.makedirs(os.path.join(temp, 'tmp-keep', 'keep'), exist_ok=True)
            # Do not delete because we will read-lock .ref
            os.makedirs(os.path.join(temp, 'tmp-rlock'), exist_ok=True)
            # Do not delete because we will write-lock .ref
            os.makedirs(os.path.join(temp, 'tmp-wlock'), exist_ok=True)

            for d in [mock_base, temp]:
                os.makedirs(
                    os.path.join(d, 'scout_before_0.20200101.0'),
                    exist_ok=True,
                )
                os.makedirs(
                    os.path.join(d, 'soldier_0.20200101.0'),
                    exist_ok=True,
                )
                os.makedirs(
                    os.path.join(d, '.scout_0.20200202.0_unpack-temp'),
                    exist_ok=True,
                )
                os.makedirs(
                    os.path.join(d, '.soldier_dontdelete'),
                    exist_ok=True,
                )
                os.makedirs(
                    os.path.join(d, 'scout_dontdelete'),
                    exist_ok=True,
                )
                os.makedirs(
                    os.path.join(d, 'soldier_0.20200101.0_keep', 'keep'),
                    exist_ok=True,
                )
                os.symlink('soldier_0.20200101.0', os.path.join(d, 'soldier'))
                os.symlink('scout_dontdelete', os.path.join(d, 'scout'))

            with open(
                os.path.join(temp, 'tmp-rlock', '.ref'), 'w+'
            ) as rlock_writer, open(
                os.path.join(temp, 'tmp-wlock', '.ref'), 'w'
            ) as wlock_writer, tee_file_and_stderr(
                os.path.join(artifacts, 'pressure-vessel.log')
            ) as tee:
                lockdata = struct.pack('hhlli', fcntl.F_RDLCK, 0, 0, 0, 0)
                fcntl.fcntl(rlock_writer.fileno(), fcntl.F_SETLKW, lockdata)
                lockdata = struct.pack('hhlli', fcntl.F_WRLCK, 0, 0, 0, 0)
                fcntl.fcntl(wlock_writer.fileno(), fcntl.F_SETLKW, lockdata)

                # Put this in a subtest so that if it fails, we still get
                # to inspect the copied sysroot
                with self.subTest('run', copy=copy, runtime=runtime):
                    completed = self.run_subprocess(
                        argv,
                        cwd=self.artifacts,
                        stdout=tee.stdin,
                        stderr=tee.stdin,
                        universal_newlines=True,
                    )
                    self.assertEqual(completed.returncode, 0)

            final_argv = final_argv_temp.read().decode()
            if locales:
                self.assertIn("\0--generate-locales\0", final_argv)
            else:
                self.assertNotIn("\0--generate-locales\0", final_argv)

            if only_prepare:
                for var in FILESYSTEM_ENV_VARS:
                    if var.plural:
                        paths = os.environ[var.name].split(':')
                    else:
                        paths = [os.environ[var.name]]

                    if var.read_only:
                        bind_mode = 'ro-bind'
                    else:
                        bind_mode = 'bind'

                    for path in paths:
                        self.assertIn(
                            "\0--{}\0{}\0{}".format(bind_mode, path, path),
                            final_argv,
                        )

            final_argv_temp.close()

            for d in [mock_base, temp]:
                members = set(os.listdir(d))

                if gc_legacy:
                    self.assertNotIn('scout_before_0.20200101.0', members)
                    self.assertNotIn('soldier', members)
                    self.assertNotIn('soldier_0.20200101.0', members)
                    self.assertNotIn(
                        '.scout_0.20200202.0_unpack-temp', members,
                    )
                else:
                    self.assertIn('.scout_0.20200202.0_unpack-temp', members)
                    self.assertIn('scout_before_0.20200101.0', members)
                    self.assertIn('soldier', members)
                    self.assertIn('soldier_0.20200101.0', members)

                self.assertIn('.soldier_dontdelete', members)
                self.assertIn('scout', members)
                self.assertIn('scout_dontdelete', members)
                self.assertIn('soldier_0.20200101.0_keep', members)

            if fake_home:
                members = set(os.listdir(fake_home_temp))

                self.assertIn('.cache', members)
                self.assertIn('.config', members)
                self.assertIn('.local', members)

            if copy:
                members = set(os.listdir(temp))

                self.assertIn('.ref', members)

                if fast_path:
                    self.assertIn('deploy-myruntime_0.1.2', members)

                self.assertIn('donotdelete', members)
                self.assertIn('tmp-keep', members)
                self.assertIn('tmp-rlock', members)
                self.assertIn('tmp-wlock', members)
                if gc:
                    if archive:
                        self.assertNotIn('deploy-deleteme', members)

                    self.assertNotIn('tmp-deleteme', members)
                else:
                    # These would have been deleted if not for --no-gc-runtimes
                    self.assertIn('deploy-deleteme', members)
                    self.assertIn('tmp-deleteme', members)

                members.discard('.ref')
                members.discard('.scout_0.20200202.0_unpack-temp')
                members.discard('.soldier_dontdelete')
                members.discard('donotdelete')
                members.discard('scout')
                members.discard('scout_before_0.20200101.0')
                members.discard('scout_dontdelete')
                members.discard('soldier')
                members.discard('soldier_0.20200101.0')
                members.discard('soldier_0.20200101.0_keep')
                members.discard('tmp-deleteme')
                members.discard('tmp-keep')
                members.discard('tmp-rlock')
                members.discard('tmp-wlock')
                members.discard('deploy-deleteme')
                members.discard('deploy-myruntime_0.1.2')
                # After discarding those, there should be exactly one left:
                # the one we just created
                self.assertEqual(len(members), 1)
                tree = os.path.join(temp, members.pop())

                if fast_path:
                    require_flag_file = 'OLD-DEPLOYMENT'
                else:
                    require_flag_file = ''

                with self.subTest('mutable sysroot'):
                    self._assert_mutable_sysroot(
                        tree,
                        artifacts,
                        is_scout=is_scout,
                        is_soldier=is_soldier,
                        require_flag_file=require_flag_file,
                    )

        logger.info(
            'Time elapsed in %s: %.1f',
            test_name, time.time() - start_time,
        )

    def _assert_mutable_sysroot(
        self,
        tree: str,
        artifacts: str,
        *,
        is_scout: bool = False,
        is_soldier: bool = False,
        require_flag_file: str = ''
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

        if require_flag_file:
            self.assertTrue(
                os.path.exists(os.path.join(tree, 'usr', require_flag_file)),
            )

        target = os.readlink(os.path.join(tree, 'overrides'))
        self.assertEqual(target, 'usr/lib/pressure-vessel/overrides')
        self.assertTrue(
            os.path.isdir(os.path.join(tree, 'overrides', 'lib')),
        )
        self.assertTrue(
            os.path.isfile(
                os.path.join(
                    tree, 'usr', 'lib', 'pressure-vessel', 'from-host',
                    'bin', 'pressure-vessel-adverb',
                )
            )
        )
        self.assertTrue(
            os.path.isdir(
                os.path.join(
                    tree, 'usr', 'lib', 'pressure-vessel', 'overrides', 'lib',
                )
            )
        )

        for multiarch, arch_info in self.host_srsi_parsed.get(
            'architectures', {}
        ).items():
            overrides_libdir = os.path.join(
                tree, 'overrides', 'lib', multiarch,
            )
            root_libdir = os.path.join(
                tree, 'lib', multiarch,
            )
            usr_libdir = os.path.join(
                tree, 'usr', 'lib', multiarch,
            )
            host_root_libdir = os.path.join(
                '/', 'lib', multiarch,
            )
            host_usr_libdir = os.path.join(
                '/', 'usr', 'lib', multiarch,
            )

            if multiarch == 'i386-linux-gnu':
                libqualdir = os.path.join(tree, 'lib32')
                usr_libqualdir = os.path.join(tree, 'usr', 'lib32')
            elif multiarch == 'x86_64-linux-gnu':
                libqualdir = os.path.join(tree, 'lib64')
                usr_libqualdir = os.path.join(tree, 'usr', 'lib64')
            else:
                libqualdir = ''
                usr_libqualdir = ''

            with self.subTest(arch=multiarch):

                self.assertTrue(os.path.isdir(overrides_libdir))
                self.assertTrue(os.path.isdir(root_libdir))
                self.assertTrue(os.path.isdir(usr_libdir))

                if is_scout:
                    for soname in (
                        'libBrokenLocale.so.1',
                        'libanl.so.1',
                        'libc.so.6',
                        'libcrypt.so.1',
                        'libdl.so.2',
                        'libm.so.6',
                        'libnsl.so.1',
                        'libnss_dns.so.2',
                        'libnss_files.so.2',
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
                                os.path.join(overrides_libdir, soname)
                            )
                            self.assertRegex(target, r'^/run/host/')

                            devlib = soname.split('.so.', 1)[0] + '.so'

                            # It was deleted from /lib, if present...
                            with self.assertRaises(FileNotFoundError):
                                print(
                                    '#',
                                    os.path.join(root_libdir, soname),
                                    '->',
                                    os.readlink(
                                        os.path.join(root_libdir, soname)
                                    ),
                                )
                            # ... and /usr/lib, if present...
                            with self.assertRaises(FileNotFoundError):
                                print(
                                    '#',
                                    os.path.join(usr_libdir, soname),
                                    '->',
                                    os.readlink(
                                        os.path.join(usr_libdir, soname)
                                    ),
                                )
                            # ... and /libQUAL and /usr/libQUAL, if present
                            if libqualdir:
                                with self.assertRaises(FileNotFoundError):
                                    print(
                                        '#',
                                        os.path.join(libqualdir, soname),
                                        '->',
                                        os.readlink(
                                            os.path.join(libqualdir, soname)
                                        ),
                                    )
                                with self.assertRaises(FileNotFoundError):
                                    print(
                                        '#',
                                        os.path.join(usr_libqualdir, soname),
                                        '->',
                                        os.readlink(
                                            os.path.join(
                                                usr_libqualdir, soname,
                                            )
                                        ),
                                    )

                            # In most cases we expect the development
                            # symlink to be removed, too - but some of
                            # them are actually linker scripts or other
                            # non-ELF things
                            if soname not in (
                                'libc.so.6',
                                'libpthread.so.0',
                            ):
                                with self.assertRaises(FileNotFoundError):
                                    print(
                                        '#',
                                        os.path.join(usr_libdir, devlib),
                                        '->',
                                        os.readlink(
                                            os.path.join(usr_libdir, devlib)
                                        ),
                                    )
                                with self.assertRaises(FileNotFoundError):
                                    print(
                                        '#',
                                        os.path.join(root_libdir, devlib),
                                        '->',
                                        os.readlink(
                                            os.path.join(root_libdir, devlib)
                                        ),
                                    )

                    for soname in (
                        # These are some examples of libraries in the
                        # graphics stack that we don't upgrade, so we
                        # expect the host version to be newer (or possibly
                        # absent if the host graphics stack doesn't use
                        # them, for example static linking or something).
                        'libdrm.so.2',
                        'libudev.so.0',
                    ):
                        with self.subTest(soname=soname):
                            if (
                                not os.path.exists(
                                    os.path.join(host_usr_libdir, soname)
                                )
                                and not os.path.exists(
                                    os.path.join(host_root_libdir, soname)
                                )
                            ):
                                self.assertTrue(
                                    os.path.exists(
                                        os.path.join(usr_libdir, soname)
                                    )
                                    or os.path.exists(
                                        os.path.join(root_libdir, soname)
                                    )
                                )
                                continue

                            devlib = soname.split('.so.', 1)[0] + '.so'
                            pattern = soname + '.*'

                            target = os.readlink(
                                os.path.join(overrides_libdir, soname)
                            )
                            self.assertRegex(target, r'^/run/host/')

                            # It was deleted from /lib, if present...
                            with self.assertRaises(FileNotFoundError):
                                os.readlink(
                                    os.path.join(root_libdir, soname)
                                )
                            with self.assertRaises(FileNotFoundError):
                                os.readlink(
                                    os.path.join(root_libdir, devlib)
                                )
                            self.assertEqual(
                                glob.glob(os.path.join(root_libdir, pattern)),
                                [],
                            )

                            # ... and /usr/lib, if present
                            with self.assertRaises(FileNotFoundError):
                                os.readlink(
                                    os.path.join(usr_libdir, soname)
                                )
                            with self.assertRaises(FileNotFoundError):
                                os.readlink(
                                    os.path.join(usr_libdir, devlib)
                                )
                            self.assertEqual(
                                glob.glob(os.path.join(usr_libdir, pattern)),
                                [],
                            )

                    for soname in (
                        'libSDL-1.2.so.0',
                        'libfltk.so.1.1',
                    ):
                        with self.subTest(soname=soname):
                            with self.assertRaises(FileNotFoundError):
                                os.readlink(
                                    os.path.join(overrides_libdir, soname),
                                )

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
                    )) and not dri.get('is_extra', False):
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
                        link = os.path.join(overrides_libdir, 'dri', k)
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
                # Might be either /usr/bin/locale or /usr/sbin/locale
                self.assertRegex(target, r'^/run/host/usr/')

            if os.path.isfile('/usr/bin/localedef'):
                link = os.path.join(tree, 'usr', 'bin', 'localedef')
                target = os.readlink(link)
                # Might be either /usr/bin/localedef or /usr/sbin/localedef
                self.assertRegex(target, r'^/run/host/usr/')

            for ldso, scout_impl in (
                (
                    '/lib/ld-linux.so.2',
                    '/lib/i386-linux-gnu/ld-2.15.so',
                ),
                (
                    '/lib64/ld-linux-x86-64.so.2',
                    '/lib/x86_64-linux-gnu/ld-2.15.so',
                ),
            ):
                try:
                    host_path = os.path.realpath(ldso)
                except OSError:
                    pass
                else:
                    link = os.path.join(tree, './' + ldso)
                    target = os.readlink(link)
                    self.assertEqual(target, '/run/host' + host_path)
                    link = os.path.join(tree, './' + scout_impl)
                    target = os.readlink(link)
                    self.assertEqual(target, '/run/host' + host_path)

    def test_scout_sysroot(self) -> None:
        scout = os.path.join(self.containers_dir, 'scout_sysroot')

        with self.subTest('only-prepare'):
            self._test_scout(
                'scout_sysroot_prep', scout,
                copy=True, only_prepare=True,
            )

        with self.subTest('copy'):
            self._test_scout('scout_sysroot_copy', scout, copy=True, gc=False)

        with self.subTest('fake-home'):
            self._test_scout(
                'scout_sysroot_fake_home', scout, copy=True, fake_home=True,
            )

        with self.subTest('transient'):
            self._test_scout('scout_sysroot', scout, locales=True)

    def test_scout_sysroot_usrmerge(self) -> None:
        scout = os.path.join(self.containers_dir, 'scout_sysroot_usrmerge')

        with self.subTest('only-prepare'):
            self._test_scout(
                'scout_sysroot_prep_usrmerge', scout,
                copy=True, only_prepare=True,
            )

        with self.subTest('copy'):
            self._test_scout(
                'scout_sysroot_copy_usrmerge', scout,
                copy=True, gc=False, gc_legacy=False,
            )

        with self.subTest('fake-home'):
            self._test_scout(
                'scout_sysroot_fake_home', scout, copy=True, fake_home=True,
            )

        with self.subTest('transient'):
            self._test_scout('scout_sysroot_usrmerge', scout, locales=True)

    def test_scout_usr(self) -> None:
        scout = os.path.join(self.containers_dir, 'scout')

        with self.subTest('only-prepare'):
            self._test_scout('scout_prep', scout, copy=True, only_prepare=True)

        with self.subTest('copy'):
            self._test_scout('scout_copy', scout, copy=True, locales=True)

        with self.subTest('fake-home'):
            self._test_scout(
                'scout_fake_home', scout, copy=True, fake_home=True,
            )

        with self.subTest('transient'):
            self._test_scout('scout', scout)

    def test_unpack(self) -> None:
        scout = os.path.join(
            self.containers_dir,
            'scout',
        )
        archive = os.path.join(
            self.containers_dir,
            ('com.valvesoftware.SteamRuntime.Platform-amd64,i386-'
             'scout-runtime.tar.gz'),
        )

        self._test_container(
            'scout_unpack',
            scout,
            archive=archive,
            copy=True,
            is_scout=True,
            locales=False,
            only_prepare=True,
        )

        self._test_container(
            'scout_skip_unpack',
            scout,
            archive=archive,
            copy=True,
            fast_path=True,
            is_scout=True,
            locales=False,
            only_prepare=True,
        )

    def test_soldier_sysroot(self) -> None:
        soldier = os.path.join(self.containers_dir, 'soldier_sysroot')

        with self.subTest('only-prepare'):
            self._test_soldier(
                'soldier_sysroot_prep', soldier,
                copy=True, only_prepare=True,
            )

        with self.subTest('copy'):
            self._test_soldier(
                'soldier_sysroot_copy', soldier, copy=True, gc=False,
            )

        with self.subTest('fake-home'):
            self._test_soldier(
                'soldier_sysroot_fake_home', soldier, copy=True,
                fake_home=True,
            )

        with self.subTest('transient'):
            self._test_soldier('soldier_sysroot', soldier, locales=True)

    def test_soldier_usr(self) -> None:
        soldier = os.path.join(self.containers_dir, 'soldier')

        with self.subTest('only-prepare'):
            self._test_soldier(
                'soldier_prep', soldier, copy=True, only_prepare=True,
            )

        with self.subTest('copy'):
            self._test_soldier(
                'soldier_copy', soldier, copy=True, locales=True,
            )

        with self.subTest('fake-home'):
            self._test_soldier(
                'soldier_fake_home', soldier, copy=True, fake_home=True,
            )

        with self.subTest('transient'):
            self._test_soldier('soldier', soldier)

    def test_no_runtime(self) -> None:
        if self.bwrap is None:
            self.skipTest('Unable to run bwrap (in a container?)')

        artifacts = os.path.join(
            self.artifacts,
            'no-runtime',
        )
        os.makedirs(artifacts, exist_ok=True)

        final_argv_path = os.path.join(self.artifacts, 'final-argv')

        argv = [
            self.pv_wrap,
            '--verbose',
            '--write-final-argv', final_argv_path,
        ]

        var = os.path.join(self.containers_dir, 'var')
        os.makedirs(var, exist_ok=True)

        with open(os.path.join(artifacts, 'srsi.json'), 'w') as writer:
            completed = self.run_subprocess(
                argv + [
                    '--',
                    'env',
                    'LD_BIND_NOW=1',
                    self.host_srsi or 'true',
                    '--verbose',
                ],
                cwd=artifacts,
                stdout=writer,
                stderr=2,
                universal_newlines=True,
            )

        self.assertEqual(completed.returncode, 0)


if __name__ == '__main__':
    assert sys.version_info >= (3, 4), \
        'Python 3.4+ is required'

    test_main()

# vi: set sw=4 sts=4 et:
