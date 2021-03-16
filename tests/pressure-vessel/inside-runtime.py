#!/usr/bin/python3
# Copyright 2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

import contextlib
import ctypes
import errno
import json
import logging
import os
import re
import shlex
import subprocess
import sys
import typing
from pathlib import Path

from testutils import (
    BaseTest,
    test_main,
)

"""
Test script intended to be run inside a SteamRT container
to assert that everything is as it should be.
"""

logger = logging.getLogger('test-inside-runtime')


class HostInfo:
    def __init__(self) -> None:
        self.path = Path('/run/host')

        self.os_release = {}   # type: typing.Dict[str, str]

        for osr in ('etc/os-release', 'usr/lib/os-release'):
            try:
                reader = (self.path / osr).open()
            except OSError:
                continue
            else:
                with reader:
                    for line in reader:
                        if '=' not in line:
                            logger.warning(
                                'Invalid line in %r: %r',
                                self.path / osr, line,
                            )
                            continue
                        key, value = line.split('=', 1)

                        try:
                            tokens = shlex.split(value)
                        except ValueError as e:
                            logger.warning(
                                'Invalid line in %r: %r: %s',
                                self.path / osr, line, e,
                            )
                            continue

                        if len(tokens) != 1:
                            logger.warning(
                                'Invalid line in %r: %r',
                                self.path / osr, line,
                            )
                            continue

                        self.os_release[key] = tokens[0]
                break


class TestInsideRuntime(BaseTest):
    def setUp(self) -> None:
        super().setUp()
        self.host = HostInfo()

        artifacts = os.getenv('TEST_INSIDE_RUNTIME_ARTIFACTS')

        if artifacts is not None:
            self.artifacts = Path(artifacts)
        else:
            self.artifacts = Path(self.tmpdir.name)

        self.artifacts.mkdir(exist_ok=True)

    def tearDown(self) -> None:
        super().tearDown()

    @contextlib.contextmanager
    def catch(
        self,
        msg,                # type: str
        diagnostic=None,    # type: typing.Any
        **kwargs            # type: typing.Any
    ):
        """
        Run a sub-test, with additional logging. If it fails, we still
        continue to test.
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

                if diagnostic is not None:
                    logger.error('%r', diagnostic)

                raise

    def test_os_release(self) -> None:
        """
        Assert that both /etc/os-release and /usr/lib/os-release have
        the contents we expect them to.
        """

        for osr in ('/etc/os-release', '/usr/lib/os-release'):
            data = {}   # type: typing.Dict[str, str]

            with open(osr) as reader:
                for line in reader:
                    assert '=' in line, line
                    key, value = line.split('=', 1)

                    logger.info('%s %r: %r', osr, key, value)

                    tokens = shlex.split(value)

                    assert len(tokens) == 1, tokens
                    data[key] = tokens[0]

            if os.environ.get('TEST_INSIDE_RUNTIME_IS_SCOUT'):
                self.assertEqual(data.get('VERSION_ID'), '1')
                self.assertEqual(data.get('ID'), 'steamrt')
                self.assertEqual(data.get('ID_LIKE'), 'ubuntu')

            elif os.environ.get('TEST_INSIDE_RUNTIME_IS_SOLDIER'):
                self.assertEqual(data.get('VERSION_ID'), '2')
                self.assertEqual(data.get('ID'), 'steamrt')
                self.assertEqual(data.get('ID_LIKE'), 'debian')

            self.assertIsNotNone(data.get('BUILD_ID'))

    def test_environ(self) -> None:
        logger.info('PATH: %r', os.environ.get('PATH'))
        logger.info(
            'LD_LIBRARY_PATH: %r', os.environ.get('LD_LIBRARY_PATH')
        )

        if os.environ.get('TEST_INSIDE_RUNTIME_IS_SCOUT'):
            self.assertEqual(os.environ.get('STEAM_RUNTIME'), '/')
        elif os.environ.get('TEST_INSIDE_RUNTIME_IS_SOLDIER'):
            self.assertEqual(os.environ.get('STEAM_RUNTIME'), None)

        self.assertEqual(os.environ.get('container'), 'pressure-vessel')

        with open('/run/host/container-manager', 'r') as reader:
            self.assertEqual(reader.read(), 'pressure-vessel\n')

    def test_overrides(self) -> None:
        if os.getenv('TEST_INSIDE_RUNTIME_IS_COPY'):
            target = os.readlink('/overrides')
            self.assertEqual(target, 'usr/lib/pressure-vessel/overrides')

        self.assertTrue(Path('/overrides').is_dir())
        self.assertTrue(Path('/overrides/lib').is_dir())

    def test_glibc(self) -> None:
        glibc = ctypes.cdll.LoadLibrary('libc.so.6')
        gnu_get_libc_version = glibc.gnu_get_libc_version
        gnu_get_libc_version.restype = ctypes.c_char_p
        glibc_version = gnu_get_libc_version().decode('ascii')
        logger.info('glibc version in use: %s', glibc_version)
        major, minor, *rest = glibc_version.split('.')

        if os.environ.get('TEST_INSIDE_RUNTIME_IS_SCOUT'):
            # Assert that we took the glibc version from the host OS.
            #
            # We assume this will always be true for scout, because scout
            # is based on Ubuntu 12.04, the oldest operating system we support;
            # and in cases where our glibc is the same version as the glibc of
            # the host OS, we prefer the host.

            if (
                'HOST_LD_LINUX_SO_REALPATH' in os.environ
                and Path('/usr/lib/i386-linux-gnu').is_dir()
            ):
                host_path = os.environ['HOST_LD_LINUX_SO_REALPATH']
                expected = self.host.path / host_path.lstrip('/')
                expected_stat = expected.stat()
                logger.info('host ld-linux.so.2: %s', host_path)

                for really in (
                    '/lib/ld-linux.so.2',
                    '/lib/i386-linux-gnu/ld-linux.so.2',
                    '/lib/i386-linux-gnu/ld-2.15.so',
                ):
                    really_stat = Path(really).stat()
                    # Either it's a symlink to the same file, or the same file
                    # was mounted over it
                    self.assertEqual(really_stat.st_dev, expected_stat.st_dev)
                    self.assertEqual(really_stat.st_ino, expected_stat.st_ino)

            if (
                'HOST_LD_LINUX_X86_64_SO_REALPATH' in os.environ
                and Path('/usr/lib/x86_64-linux-gnu').is_dir()
            ):
                host_path = os.environ['HOST_LD_LINUX_X86_64_SO_REALPATH']
                expected = self.host.path / host_path.lstrip('/')
                expected_stat = expected.stat()
                logger.info('host ld-linux-x86-64.so.2: %s', host_path)

                for really in (
                    '/lib64/ld-linux-x86-64.so.2',
                    '/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2',
                    '/lib/x86_64-linux-gnu/ld-2.15.so',
                ):
                    really_stat = Path(really).stat()
                    self.assertEqual(really_stat.st_dev, expected_stat.st_dev)
                    self.assertEqual(really_stat.st_ino, expected_stat.st_ino)

        elif os.environ.get('TEST_INSIDE_RUNTIME_IS_SOLDIER'):
            # We don't know whether it's soldier's glibc 2.28 or something
            # newer from the host, but it should definitely be at least
            # soldier's version
            self.assertGreaterEqual((int(major), int(minor)), (2, 28))

    def test_ld_so_cache(self) -> None:
        """
        For simplicity, we use symlinks to make all the known ld.so.cache
        paths equivalent.
        """

        cache = Path('/etc/ld.so.cache').resolve()
        # Clear Linux
        self.assertEqual(
            cache, Path('/var/cache/ldconfig/ld.so.cache').resolve()
        )
        # Exherbo
        self.assertEqual(
            cache, Path('/etc/ld-x86_64-pc-linux-gnu.cache').resolve()
        )
        self.assertEqual(
            cache, Path('/etc/ld-i686-pc-linux-gnu.cache').resolve()
        )

    def is_loadable_duplicated(self, srsi_parsed, key, sub_key) -> bool:
        if srsi_parsed is None:
            return False

        if key in srsi_parsed and sub_key in srsi_parsed[key]:
            for loadable in srsi_parsed[key][sub_key]:
                if "duplicated" in loadable.get('issues', ''):
                    return True

        return False

    def test_home_directory(self) -> None:
        if os.environ.get('TEST_INSIDE_RUNTIME_IS_HOME_UNSHARED'):
            # If we unshared the real home there are a few directories that
            # we always expect to have
            home_path = os.environ.get('HOME')
            members = set(os.listdir(home_path))

            self.assertIn('.cache', members)
            self.assertIn('.config', members)
            self.assertIn('.local', members)

    def test_srsi(self) -> None:
        overrides = Path('/overrides').resolve()

        if 'HOST_STEAM_RUNTIME_SYSTEM_INFO_JSON' in os.environ:
            with open(
                os.environ['HOST_STEAM_RUNTIME_SYSTEM_INFO_JSON'],
                'r',
            ) as reader:
                host_parsed = json.load(reader)
        else:
            host_parsed = {}

        with (self.artifacts / 'srsi.json').open('w') as writer:
            logger.info('steam-runtime-system-info --verbose...')
            completed = subprocess.run(
                # We specifically want the container's version, not
                # any other version that might have crept into PATH.
                ['/usr/bin/steam-runtime-system-info', '--verbose'],
                stdout=writer,
                stderr=subprocess.PIPE,
                check=True,
            )
            if completed.stderr:
                logger.info(
                    '(stderr) -> \n%s',
                    completed.stderr.decode('utf-8', 'backslashreplace')
                )
            else:
                logger.info('(no stderr)')

        with (self.artifacts / 'srsi.json').open('r') as reader:
            for line in reader:
                print(line.rstrip('\n'), file=sys.stderr)

        with (self.artifacts / 'srsi.json').open('r') as reader:
            parsed = json.load(reader)

        self.assertIsInstance(parsed, dict)

        if host_parsed:
            host_os_release = host_parsed.get('os-release', {})
        else:
            host_os_release = parsed.get(
                'container', {}
            ).get(
                'host', {}
            ).get(
                'os-release', {}
            )

        if host_os_release.get('id') == 'debian':
            logger.info('Host OS is Debian')
            host_is_debian_derived = True
        elif 'debian' in host_os_release.get('id_like', []):
            logger.info('Host OS is Debian-derived')
            host_is_debian_derived = True
        else:
            logger.info('Host OS is not Debian-derived')
            host_is_debian_derived = False

        if host_parsed:
            # If the Steam installation on the host system is sane, we expect
            # the same in the container too.

            self.assertIn('steam-installation', host_parsed)
            self.assertIn('steam-installation', parsed)
            host_issues = host_parsed['steam-installation'].get('issues', [])
            issues = parsed['steam-installation'].get('issues', [])

            self.assertEqual(
                'cannot-find' in host_issues,
                'cannot-find' in issues,
            )
            self.assertEqual(
                'dot-steam-steam-not-symlink' in host_issues,
                'dot-steam-steam-not-symlink' in issues,
            )
            self.assertEqual(
                'cannot-find-data' in host_issues,
                'cannot-find-data' in issues,
            )
            self.assertEqual(
                'dot-steam-steam-not-directory' in host_issues,
                'dot-steam-steam-not-directory' in issues,
            )
            self.assertEqual(
                'dot-steam-root-not-symlink' in host_issues,
                'dot-steam-root-not-symlink' in issues,
            )
            self.assertEqual(
                'dot-steam-root-not-directory' in host_issues,
                'dot-steam-root-not-directory' in issues,
            )

        with self.catch(
            'runtime information',
            diagnostic=parsed.get('runtime'),
        ):
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

        with self.catch(
            'os-release information',
            diagnostic=parsed.get('os-release'),
        ):
            self.assertIn('os-release', parsed)
            self.assertEqual('steamrt', parsed['os-release']['id'])
            self.assertNotIn(
                parsed['os-release']['id'],
                parsed['os-release'].get('id_like', [])
            )
            self.assertIn('name', parsed['os-release'])
            self.assertIn('pretty_name', parsed['os-release'])
            self.assertIn('version_id', parsed['os-release'])
            self.assertIn('build_id', parsed['os-release'])

            if os.environ.get('TEST_INSIDE_RUNTIME_IS_SCOUT'):
                self.assertEqual('1', parsed['os-release']['version_id'])
                self.assertEqual(
                    'scout',
                    parsed['os-release']['version_codename'],
                )
            elif os.environ.get('TEST_INSIDE_RUNTIME_IS_SOLDIER'):
                self.assertEqual('2', parsed['os-release']['version_id'])
                self.assertEqual(
                    'soldier',
                    parsed['os-release']['version_codename'],
                )

        with self.catch(
            'container info',
            diagnostic=parsed.get('container'),
        ):
            self.assertIn('container', parsed)
            self.assertEqual(parsed['container']['type'], 'pressure-vessel')
            self.assertEqual(parsed['container']['host']['path'], '/run/host')

        if host_parsed:
            self.assertEqual(
                parsed['container']['host']['os-release'],
                host_parsed['os-release'],
            )

            if os.environ.get('TEST_INSIDE_RUNTIME_LOCALES'):
                for locale, host_details in host_parsed.get(
                    'locales', {}
                ).items():
                    self.assertIn(locale, parsed['locales'])
                    details = parsed['locales'][locale]

                    # Every locale that worked on the host works in the
                    # container
                    if 'resulting-name' in host_details:
                        self.assertEqual(host_details, details)

                    # en_US.UTF-8 should also work in the container, because we
                    # make sure to generate it
                    if locale == 'en_US.UTF-8':
                        self.assertEqual(details.get('error'), None)

                # en_US.UTF-8 should also work in the container, because we
                # make sure to generate it
                if locale == 'en_US.UTF-8':
                    self.assertEqual(details.get('error'), None)

        self.assertIn('architectures', parsed)

        if host_parsed:
            # If we have an ICD or Layer flagged as duplicated in the
            # container, we expect that the same thing to happen in the
            # host too.
            self.assertEqual(
                self.is_loadable_duplicated(parsed, 'egl', 'icds'),
                self.is_loadable_duplicated(host_parsed, 'egl', 'icds'),
            )

            self.assertEqual(
                self.is_loadable_duplicated(parsed, 'vulkan', 'icds'),
                self.is_loadable_duplicated(host_parsed, 'vulkan', 'icds'),
            )

            self.assertEqual(
                self.is_loadable_duplicated(parsed, 'vulkan', 'explicit_layers'),
                self.is_loadable_duplicated(host_parsed, 'vulkan', 'explicit_layers'),
            )

            self.assertEqual(
                self.is_loadable_duplicated(parsed, 'vulkan', 'implicit_layers'),
                self.is_loadable_duplicated(host_parsed, 'vulkan', 'implicit_layers'),
            )

        for multiarch in parsed['architectures']:
            if not (Path('/usr/lib') / multiarch).is_dir():
                continue

            arch_info = parsed['architectures'][multiarch]
            host_info = host_parsed.get('architectures', {}).get(multiarch, {})

            with self.catch(
                'per-architecture information',
                diagnostic=arch_info,
                arch=multiarch,
            ):
                self.assertTrue(arch_info['can-run'])
                # Graphics driver support depends on the host system, so we
                # don't assert that everything is fine, only that we have
                # the information.
                self.assertIn('graphics-details', arch_info)
                self.assertIn('glx/gl', arch_info['graphics-details'])

            expect_library_issues = set()

            for soname, details in arch_info['library-details'].items():
                with self.catch(
                    'per-library information',
                    diagnostic=details,
                    arch=multiarch,
                    soname=soname,
                ):
                    if soname == 'libldap-2.4.so.2':
                        # On Debian, libldap-2.4.so.2 is really an alias
                        # for libldap_r-2.4.so.2; but on Arch Linux they
                        # are separate libraries, and this causes trouble
                        # for our library-loading. Ignore failure to load
                        # the former.
                        expect_library_issues |= set(details.get('issues', []))
                        continue

                    if soname == 'libOSMesa.so.8':
                        # T22540: C++ ABI issues around std::string in the
                        # ABI of libLLVM-7.so.1
                        expect_library_issues |= set(details.get('issues', []))
                        continue

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

            if os.environ.get('TEST_INSIDE_RUNTIME_IS_SCOUT'):
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
                    # These are from glibc, which is depended on by Mesa, and
                    # is at least as new as scout's version in every supported
                    # version of the Steam Runtime.
                    self.assertEqual(
                        arch_info['library-details'][soname]['path'],
                        '{}/lib/{}/{}'.format(overrides, multiarch, soname),
                    )

            if os.environ.get('TEST_INSIDE_RUNTIME_IS_SCOUT'):
                not_graphics_drivers = [
                    'libSDL-1.2.so.0',
                    'libfltk.so.1.1',
                ]
            elif os.environ.get('TEST_INSIDE_RUNTIME_IS_SOLDIER'):
                not_graphics_drivers = [
                    'libfltk.so.1.1',
                    'libgdk-3.so.0',
                    'libSDL2-2.0.so.0',
                ]
            else:
                not_graphics_drivers = []

            for soname in not_graphics_drivers:
                # These libraries are definitely not part of the graphics
                # driver stack
                if (
                    arch_info['library-details'][soname]['path']
                    != '/lib/{}/{}'.format(multiarch, soname)
                ):
                    self.assertEqual(
                        arch_info['library-details'][soname]['path'],
                        '/usr/lib/{}/{}'.format(multiarch, soname),
                    )

            if host_info:
                expect_symlinks = {
                }    # type: typing.Dict[str, typing.List[str]]
                for dri in host_info.get('dri_drivers', ()):
                    path = dri['library_path']

                    if path.startswith((    # any of:
                        '/usr/lib/dri/',
                        '/usr/lib32/dri/',
                        '/usr/lib64/dri/',
                        '/usr/lib/{}/dri/'.format(multiarch),
                    )) and not dri.get('is_extra', False):
                        # We don't make any assertion about the search
                        # order here.
                        host_path = '/run/host' + path

                        # Take the realpath() on non-Debian-derived hosts,
                        # because on Arch Linux, we find drivers in
                        # /usr/lib64 that are physically in /usr/lib.
                        # Be more strict on Debian because we know more
                        # about the canonical paths there.
                        if not host_is_debian_derived:
                            with contextlib.suppress(OSError):
                                host_path = os.path.realpath(host_path)

                        expect_symlinks.setdefault(
                            os.path.basename(path), []
                        ).append(host_path)

                for k, vs in expect_symlinks.items():
                    with self.subTest(dri_symlink=k):
                        link = '{}/lib/{}/dri/{}'.format(
                            overrides, multiarch, k,
                        )
                        logger.info('Target of %s should be in %s', link, vs)
                        target = os.readlink(link)

                        # Again, take the realpath() on non-Debian-derived
                        # hosts, but be more strict on Debian.
                        if not host_is_debian_derived:
                            with contextlib.suppress(OSError):
                                target = os.path.realpath(link)

                        self.assertIn(target, vs)

                for stack, host_details in (
                    host_info.get('graphics-details', {}).items()
                ):
                    if stack not in arch_info['graphics-details']:
                        continue

                    # On Debian hosts with SDK containers, this might
                    # not work because we get confused about whether
                    # our libedit.so.2 is older or newer than the
                    # host's. (T21954)
                    if (
                        host_is_debian_derived
                        and (
                            Path('/usr/lib') / multiarch / 'libedit.so.2'
                        ).exists()
                    ):
                        logger.info(
                            'libedit.so.2 exists, skipping graphics check'
                        )
                        continue

                    # Compounding the above, capsule-capture-libs 0.20190926.0
                    # will not capture the i386 libedit.so.2 from the host
                    # if it thinks the *x86_64* libedit.so.2 from the
                    # container is newer, due to a bug. We work around it
                    # for now, to get some test coverage going.
                    if (
                        host_is_debian_derived
                        and Path(
                            '/usr/lib/x86_64-linux-gnu/libedit.so.2'
                        ).exists()
                    ):
                        logger.info(
                            'x86_64 libedit.so.2 exists, skipping graphics '
                            'check'
                        )
                        continue

                    with self.subTest(stack=stack):
                        details = arch_info['graphics-details'][stack]

                        # If it works on the host, it should work in
                        # the container (modulo caveats above).
                        if not host_details.get('issues', ()):
                            self.assertFalse(details.get('issues', ()))

                        for key in (
                            'renderer',
                            'version',
                            'library-vendor',
                        ):
                            with self.subTest(key=key):
                                if key in host_details:
                                    self.assertEqual(
                                        host_details[key],
                                        details.get(key),
                                    )

            with self.catch(
                'per-architecture information',
                diagnostic=arch_info,
                arch=multiarch,
            ):
                self.assertEqual(
                    expect_library_issues,
                    set(arch_info['library-issues-summary']),
                )

    def test_read_only(self) -> None:
        for read_only_place in (
            '/bin',
            '/etc/ld.so.conf.d',
            '/lib',
            '/overrides',
            '/overrides/lib',
            '/run/host/bin',
            '/run/host/lib',
            '/run/host/usr',
            '/run/host/usr/bin',
            '/run/host/usr/lib',
            '/run/pressure-vessel/pv-from-host',
            '/run/pressure-vessel/pv-from-host/bin',
            '/sbin',
            '/usr',
            '/usr/lib',
            '/usr/lib/pressure-vessel/from-host/bin',
            '/usr/lib/pressure-vessel/overrides',
            '/usr/lib/pressure-vessel/overrides/lib',
        ):
            with self.subTest(read_only_place):
                if (
                    read_only_place.startswith('/overrides')
                    and not os.getenv('TEST_INSIDE_RUNTIME_IS_COPY')
                ):
                    # If we aren't working from a temporary copy of the
                    # runtime, /overrides is on a tmpfs
                    continue

                with self.assertRaises(OSError) as raised:
                    open(os.path.join(read_only_place, 'hello'), 'w')

                if isinstance(raised.exception, FileNotFoundError):
                    # Some of these paths don't exist under all
                    # circumstances
                    continue

                self.assertEqual(raised.exception.errno, errno.EROFS)

    def test_imported(self) -> None:
        mount_points = set()    # type: typing.Set[str]

        with open('/proc/self/mountinfo') as mountinfo:
            for line in mountinfo:
                _, _, _, _, mount_point, *_ = re.split(r'[ \t]', line)

                mount_point = re.sub(
                    r'\\([0-7][0-7][0-7])',
                    lambda m: chr(int(m.group(1), 8)),
                    mount_point,
                )
                mount_points.add(mount_point)

        for var, plural in (
            ('STEAM_COMPAT_APP_LIBRARY_PATH', False),
            ('STEAM_COMPAT_APP_LIBRARY_PATHS', True),
            ('STEAM_COMPAT_CLIENT_INSTALL_PATH', False),
            ('STEAM_COMPAT_DATA_PATH', False),
            ('STEAM_COMPAT_INSTALL_PATH', False),
            ('STEAM_COMPAT_LIBRARY_PATHS', True),
            ('STEAM_COMPAT_MOUNT_PATHS', True),
            ('STEAM_COMPAT_MOUNTS', True),
            ('STEAM_COMPAT_SHADER_PATH', False),
            ('STEAM_COMPAT_TOOL_PATH', False),
            ('STEAM_COMPAT_TOOL_PATHS', True),
        ):
            if plural:
                paths = os.environ[var].split(':')
            else:
                paths = [os.environ[var]]

            for path in paths:
                self.assertIn(path, mount_points)


if __name__ == '__main__':
    assert sys.version_info >= (3, 5), 'Python 3.5+ is required'

    test_main()

# vi: set sw=4 sts=4 et:
