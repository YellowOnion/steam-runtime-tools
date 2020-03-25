#!/usr/bin/env python3
# Copyright 2019-2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

import contextlib
import json
import logging
import os
import os.path
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


logger = logging.getLogger('test-pressure-vessel')


class MyCompletedProcess:
    """
    A minimal reimplementation of subprocess.CompletedProcess from
    Python 3.5+, so that this test can be run on the Python 3.4
    interpreter in Debian 8 'jessie', SteamOS 2 'brewmaster' and
    Ubuntu 14.04 'trusty'.
    """

    def __init__(
        self,
        args='',                # type: typing.Union[typing.List[str], str]
        returncode=-1,          # type: int
        stdout=None,            # type: typing.Optional[bytes]
        stderr=None             # type: typing.Optional[bytes]
    ) -> None:
        self.args = args
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr

    def check_returncode(self) -> None:
        if self.returncode != 0:
            raise subprocess.CalledProcessError(
                self.returncode,
                str(self.args),
                output=self.stdout,
            )


class TestPressureVessel(unittest.TestCase):
    def setUp(self) -> None:
        # Absolute path to the directory containing pressure-vessel
        # and the runtime(s).
        self.depot = os.path.abspath('depot')

        # Apt suite used for the runtime (scout, heavy or soldier).
        # Default: scout
        self.runtime_suite = os.getenv('TEST_CONTAINER_RUNTIME_SUITE', 'scout')

        # dpkg architectures in the runtime, with primary architecture
        # first. Default: amd64, i386
        self.dpkg_architectures = os.getenv(
            'TEST_CONTAINER_RUNTIME_ARCHITECTURES',
            'amd64,i386'
        ).split(',')

        # Path to an unpacked LD_LIBRARY_PATH runtime, or None.
        self.ld_library_path_runtime = os.getenv(
            'TEST_CONTAINER_RUNTIME_LD_LIBRARY_PATH_RUNTIME',
            None,
        )       # type: typing.Optional[str]

        # Path to an unpacked steamrt source package for the target
        # suite, or None.
        self.steamrt_source = os.getenv(
            'TEST_CONTAINER_RUNTIME_STEAMRT_SOURCE',
            None,
        )       # type: typing.Optional[str]

        self.tmpdir = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmpdir.cleanup)

        if self.ld_library_path_runtime is not None:
            if os.access(self.ld_library_path_runtime, os.W_OK):
                self.ld_library_path_runtime = os.path.abspath(
                    self.ld_library_path_runtime,
                )
            else:
                old = self.ld_library_path_runtime
                new = os.path.join(self.tmpdir.name, 'ldlp')
                shutil.copytree(old, new, symlinks=True)
                self.ld_library_path_runtime = new

        artifacts = os.getenv('AUTOPKGTEST_ARTIFACTS')

        if artifacts is not None:
            self.artifacts = artifacts
        else:
            self.artifacts = self.tmpdir.name

        self.runtime_build_id = '(unknown)'

    @contextlib.contextmanager
    def catch(
        self,
        msg,        # type: str
        **kwargs    # type: typing.Any
    ):
        """
        Run a sub-test, with additional logging.
        """

        if kwargs:
            logger.info('Starting: %r (%r)', msg, kwargs)
        else:
            logger.info('Starting: %r', msg)
        with self.subTest(msg, **kwargs):
            try:
                yield
            except Exception:
                logger.error(msg, exc_info=True)
                raise

    def get_runtime_build_id(self):
        filename = (
            'com.valvesoftware.SteamRuntime.Platform-'
            '{}-{}-buildid.txt'
        ).format(
            ','.join(self.dpkg_architectures),
            self.runtime_suite,
        )

        with self.catch('get build ID'):
            with open(os.path.join(self.depot, filename)) as reader:
                self.runtime_build_id = reader.read().strip()

        logger.info('Build ID: %s', self.runtime_build_id)
        return self.runtime_build_id

    def run_subprocess(
        self,
        args,           # type: typing.Union[typing.List[str], str]
        check=False,
        input=None,     # type: typing.Optional[bytes]
        timeout=None,   # type: typing.Optional[int]
        **kwargs        # type: typing.Any
    ):
        """
        This is basically a reimplementation of subprocess.run()
        from Python 3.5+, so that this test can be run on the Python
        3.4 interpreter in Debian 8 'jessie', SteamOS 2 'brewmaster'
        and Ubuntu 14.04 'trusty'. It also adds extra logging.
        """

        logger.info('Running: %r', args)

        popen = subprocess.Popen(args, **kwargs)    # type: ignore
        out, err = popen.communicate(input=input, timeout=timeout)
        completed = MyCompletedProcess(
            args=args,
            returncode=popen.returncode,
            stdout=out,
            stderr=err,
        )

        if check:
            completed.check_returncode()

        return completed

    def test_general_info(self) -> None:
        with open(
            os.path.join(self.artifacts, 'depot-contents.txt'), 'w'
        ) as writer:
            with self.catch('List contents of depot'):
                completed = self.run_subprocess(
                    ['find', '.', '-ls'],
                    check=True,
                    cwd=self.depot,
                    stdout=writer,
                    stderr=subprocess.PIPE,
                )

                if completed.stderr:
                    logger.info(
                        '(stderr) ->\n%s',
                        completed.stderr.decode('utf-8', 'backslashreplace'),
                    )
                else:
                    logger.info('(no stderr)')

        with self.catch('Read VERSION.txt'):
            with open(
                os.path.join(
                    'depot', 'pressure-vessel', 'metadata', 'VERSION.txt',
                ),
                'r'
            ) as reader:
                logger.info(
                    'pressure-vessel version %s',
                    reader.read().strip(),
                )

        for exe in (
            'bwrap',
            'i386-linux-gnu-capsule-capture-libs',
            'x86_64-linux-gnu-capsule-capture-libs',
            'pressure-vessel-wrap',
            'steam-runtime-system-info',
        ):
            with self.catch('--version', exe=exe):
                completed = self.run_subprocess(
                    [
                        os.path.join('pressure-vessel', 'bin', exe),
                        '--version',
                    ],
                    check=True,
                    cwd=self.depot,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                logger.info(
                    '%s --version ->\n%s',
                    exe,
                    completed.stdout.decode('utf-8').strip(),
                )

                if completed.stderr:
                    logger.info(
                        '(stderr) ->\n%s',
                        completed.stderr.decode('utf-8', 'backslashreplace'),
                    )
                else:
                    logger.info('(no stderr)')

        with open(
            os.path.join(self.artifacts, 's-r-s-i-outside.json'),
            'w',
        ) as writer:
            with self.catch('steam-runtime-system-info outside container'):
                completed = self.run_subprocess(
                    [
                        os.path.join(
                            'pressure-vessel', 'bin',
                            'steam-runtime-system-info',
                        ),
                        '--verbose',
                    ],
                    check=True,
                    cwd=self.depot,
                    stdout=writer,
                    stderr=subprocess.PIPE,
                )

                if completed.stderr:
                    logger.info(
                        '%s --version (stderr) -> %s',
                        exe, completed.stderr,
                    )
                else:
                    logger.info('(no stderr)')

        self.get_runtime_build_id()

    def get_pressure_vessel_adverb(
        self,
        ld_library_path_runtime=None        # type: typing.Optional[str]
    ):
        # type: (...) -> typing.List[str]
        if self.runtime_suite == 'scout':
            adverb = [
                os.path.join(self.depot, 'run-in-scout'),
                '--verbose',
                '--',
            ]
        else:
            if ld_library_path_runtime is None:
                exe = 'pressure-vessel-wrap'
            else:
                exe = 'pressure-vessel-unruntime'

            adverb = [
                os.path.join(
                    self.depot,
                    'pressure-vessel',
                    'bin',
                    exe,
                ),
                '--verbose',
                '--runtime-base={}'.format(self.depot),
                '--runtime={}/files'.format(self.runtime_suite),
                '--',
            ]

        if ld_library_path_runtime is not None:
            adverb = [
                os.path.join(ld_library_path_runtime, 'run.sh'),
            ] + adverb

        return adverb

    def test_pressure_vessel(
        self,
        artifact_prefix='s-r-s-i-inside',
        ld_library_path_runtime=None        # type: typing.Optional[str]
    ) -> None:
        adverb = self.get_pressure_vessel_adverb(ld_library_path_runtime)

        with self.catch('cat /etc/os-release in container'):
            completed = self.run_subprocess(
                adverb + ['cat', '/etc/os-release'],
                cwd=self.tmpdir.name,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=True,
            )

            logger.info(
                'os-release:\n%s',
                completed.stdout.decode('utf-8'),
            )

            if completed.stderr:
                logger.info(
                    '(stderr) ->\n%s',
                    completed.stderr.decode('utf-8', 'backslashreplace'),
                )
            else:
                logger.info('(no stderr)')

        if self.runtime_suite == 'heavy':
            # heavy doesn't have (Meson or) steam-runtime-tools yet
            return

        with open(
            os.path.join(self.artifacts, artifact_prefix + '.json'),
            'w',
        ) as writer:
            with self.catch('run s-r-s-i in container'):
                completed = self.run_subprocess(
                    adverb + ['steam-runtime-system-info', '--verbose'],
                    cwd=self.tmpdir.name,
                    stdout=writer,
                    stderr=subprocess.PIPE,
                    check=True,
                )

                if completed.stderr:
                    logger.info(
                        '(stderr) ->\n%s',
                        completed.stderr.decode('utf-8', 'backslashreplace'),
                    )
                else:
                    logger.info('(no stderr)')

        with open(
            os.path.join(self.artifacts, artifact_prefix + '.json'),
            'r',
        ) as reader:
            parsed = json.load(reader)

        self.get_runtime_build_id()

        self.assertIsInstance(parsed, dict)
        self.assertIn('can-write-uinput', parsed)
        self.assertIn('steam-installation', parsed)

        with self.catch('runtime information'):
            self.assertIn('runtime', parsed)
            self.assertEqual('/', parsed['runtime'].get('path'))
            self.assertIn('version', parsed['runtime'])
            issues = parsed['runtime'].get('issues', [])
            self.assertNotIn('disabled', issues)
            self.assertNotIn('internal-error', issues)
            self.assertNotIn('not-in-environment', issues)
            self.assertNotIn('not-in-ld-path', issues)
            self.assertNotIn('not-in-path', issues)
            self.assertNotIn('not-runtime', issues)
            self.assertNotIn('not-using-newer-host-libraries', issues)
            self.assertNotIn('unexpected-location', issues)
            self.assertNotIn('unexpected-version', issues)
            # Don't assert whether it contains 'unofficial':
            # we want to be able to test unofficial runtimes too

            self.assertIn('overrides', parsed['runtime'])
            self.assertNotIn('pinned_libs_32', parsed['runtime'])
            self.assertNotIn('pinned_libs_64', parsed['runtime'])

        with self.catch('os-release information'):
            self.assertIn('os-release', parsed)
            self.assertEqual('steamrt', parsed['os-release']['id'])
            self.assertNotIn(
                parsed['os-release']['id'],
                parsed['os-release'].get('id_like', [])
            )
            self.assertIn('name', parsed['os-release'])
            self.assertIn('pretty_name', parsed['os-release'])
            self.assertIn('version_id', parsed['os-release'])

            if self.runtime_suite == 'scout':
                self.assertEqual('1', parsed['os-release']['version_id'])
            elif self.runtime_suite == 'heavy':
                self.assertEqual('1.5', parsed['os-release']['version_id'])
            elif self.runtime_suite == 'soldier':
                self.assertEqual('2', parsed['os-release']['version_id'])

            self.assertEqual(
                self.runtime_suite,
                parsed['os-release']['version_codename'],
            )
            self.assertEqual(
                self.runtime_build_id,
                parsed['os-release']['build_id'],
            )

        self.assertIn('architectures', parsed)

        for arch in self.dpkg_architectures:
            if arch == 'i386':
                multiarch = 'i386-linux-gnu'
            elif arch == 'amd64':
                multiarch = 'x86_64-linux-gnu'
            else:
                continue

            self.assertIn(multiarch, parsed['architectures'])
            arch_info = parsed['architectures'][multiarch]

            with self.catch('per-architecture information', arch=arch):
                self.assertTrue(arch_info['can-run'])
                self.assertEqual([], arch_info['library-issues-summary'])
                # Graphics driver support depends on the host system, so we
                # don't assert that everything is fine, only that we have
                # the information.
                self.assertIn('graphics-details', arch_info)
                self.assertIn('glx/gl', arch_info['graphics-details'])

            for soname, details in arch_info['library-details'].items():
                with self.catch(
                    'per-library information',
                    arch=arch,
                    soname=soname,
                ):
                    self.assertIn('path', details)
                    self.assertEqual(
                        [],
                        details.get('missing-symbols', []),
                    )
                    self.assertEqual(
                        [],
                        details.get('misversioned-symbols', []),
                    )
                    self.assertEqual([], details.get('issues', []))

        # Locale support depends on the host system, so we don't assert
        # that everything is fine, only that we have the information.
        self.assertIn('locale-issues', parsed)
        self.assertIn('locales', parsed)

        # Graphics driver support depends on the host system, so we
        # don't assert that everything is fine, only that we have
        # the information.
        self.assertIn('egl', parsed)
        self.assertIn('vulkan', parsed)

    def test_unruntime(
        self,
    ) -> None:
        if self.ld_library_path_runtime is not None:
            self.run_subprocess(
                os.path.join(self.ld_library_path_runtime, 'setup.sh'),
                check=True,
                stdout=2,
                stderr=2,
            )

            self.test_pressure_vessel(
                artifact_prefix='s-r-s-i-inside-unruntime',
                ld_library_path_runtime=self.ld_library_path_runtime,
            )
        else:
            self.skipTest(
                'TEST_CONTAINER_RUNTIME_LD_LIBRARY_PATH_RUNTIME not provided'
            )

    def test_steamrt_platform(
        self,
    ) -> None:
        if self.steamrt_source is None:
            self.skipTest(
                'TEST_CONTAINER_RUNTIME_STEAMRT_SOURCE not provided'
            )
            return

        if not os.path.exists(
            os.path.join(self.steamrt_source, 'debian', 'tests', 'platform')
        ):
            self.skipTest(
                'No script at '
                '$TEST_CONTAINER_RUNTIME_STEAMRT_SOURCE/debian/tests/platform'
            )
            return

        adverb = self.get_pressure_vessel_adverb()

        with open(
            os.path.join(self.artifacts, 'platform.log'),
            'w',
        ) as writer:
            with self.catch('run steamrt platform test in container'):
                self.run_subprocess(
                    adverb + ['debian/tests/platform'],
                    cwd=self.steamrt_source,
                    stdout=writer,
                    stderr=subprocess.STDOUT,
                    check=True,
                )

    def tearDown(self) -> None:
        pass


if __name__ == '__main__':
    logging.basicConfig(level=logging.DEBUG)

    sys.path[:0] = [os.path.join(
        os.path.dirname(os.path.dirname(__file__)),
        'third-party',
    )]

    import pycotap
    unittest.main(
        buffer=False,
        testRunner=pycotap.TAPTestRunner,
    )

# vi: set sw=4 sts=4 et:
