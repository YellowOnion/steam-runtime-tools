#!/usr/bin/env python3
# Copyright 2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

import logging
import os
import signal
import subprocess
import sys
import tempfile
import time
import uuid


try:
    import typing
    typing      # placate pyflakes
except ImportError:
    pass

from testutils import (
    BaseTest,
    run_subprocess,
    test_main,
)


logger = logging.getLogger('test-launcher')

LAUNCHER_IFACE = "com.steampowered.PressureVessel.Launcher1"
LAUNCHER_PATH = "/com/steampowered/PressureVessel/Launcher1"


class TestLauncher(BaseTest):
    def setUp(self) -> None:
        super().setUp()

        if 'PRESSURE_VESSEL_UNINSTALLED' in os.environ:
            self.launcher = self.command_prefix + [
                os.path.join(
                    self.top_builddir,
                    'src',
                    'pressure-vessel-launcher'
                ),
            ]
            self.launch = self.command_prefix + [
                os.path.join(
                    self.top_builddir,
                    'src',
                    'pressure-vessel-launch',
                ),
            ]
        else:
            self.skipTest('Not available as an installed-test')

    def test_socket_directory(self) -> None:
        with tempfile.TemporaryDirectory(prefix='test-') as temp:
            proc = subprocess.Popen(
                [
                    'env',
                    'PV_TEST_VAR=from-launcher',
                ] + self.launcher + [
                    '--socket-directory', temp,
                ],
                stdout=subprocess.PIPE,
                stderr=2,
                universal_newlines=True,
            )

            try:
                socket = ''
                dbus_address = ''

                stdout = proc.stdout
                assert stdout is not None
                for line in stdout:
                    line = line.rstrip('\n')
                    logger.debug('%s', line)

                    if line.startswith('socket='):
                        socket = line[len('socket='):]
                    elif line.startswith('dbus_address='):
                        dbus_address = (
                            line[len('dbus_address='):]
                        )

                self.assertTrue(socket)
                self.assertTrue(dbus_address)

                left = os.stat(socket)
                right = os.stat(os.path.join(temp, os.path.basename(socket)))
                self.assertEqual(left.st_dev, right.st_dev)
                self.assertEqual(left.st_ino, right.st_ino)

                completed = run_subprocess(
                    self.launch + [
                        '--socket', socket,
                        '--',
                        'true',
                    ],
                    check=True,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                self.assertEqual(completed.stdout, b'')

                completed = run_subprocess(
                    self.launch + [
                        '--socket', socket,
                        '--',
                        'printf', 'hello',
                    ],
                    check=True,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                self.assertEqual(completed.stdout, b'hello')

                completed = run_subprocess(
                    self.launch + [
                        '--dbus-address', dbus_address,
                        '--',
                        'sh', '-euc', 'printf hello; printf W"ORL"D >&2',
                    ],
                    check=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                self.assertEqual(completed.stdout, b'hello')
                self.assertIn(b'WORLD', completed.stderr)

                completed = run_subprocess(
                    [
                        'env', 'PV_TEST_VAR=not-inherited',
                    ] + self.launch + [
                        '--socket', socket,
                        '--',
                        'sh', '-euc', 'printf \'%s\' "$PV_TEST_VAR"',
                    ],
                    check=True,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                self.assertEqual(completed.stdout, b'from-launcher')

                completed = run_subprocess(
                    self.launch + [
                        '--clear-env',
                        '--socket', socket,
                        '--',
                        'sh', '-euc', 'echo -n "${PV_TEST_VAR-cleared}"',
                    ],
                    check=True,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                self.assertEqual(completed.stdout, b'cleared')

                completed = run_subprocess(
                    self.launch + [
                        '--env=PV_TEST_VAR=overridden',
                        '--socket', socket,
                        '--',
                        'sh', '-euc', 'printf \'%s\' "$PV_TEST_VAR"',
                    ],
                    check=True,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                self.assertEqual(completed.stdout, b'overridden')

                launch = subprocess.Popen(
                    self.launch + [
                        '--socket', socket,
                        '--',
                        'sleep', '600',
                    ],
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                launch.send_signal(signal.SIGINT)
                self.assertIn(
                    launch.wait(),
                    (128 + signal.SIGINT, -signal.SIGINT),
                )
            finally:
                proc.terminate()
                proc.wait()
                self.assertEqual(proc.returncode, 0)

            completed = run_subprocess(
                self.launch + [
                    '--dbus-address', dbus_address,
                    '--',
                    'printf', 'hello',
                ],
                stdout=2,
                stderr=2,
            )
            self.assertEqual(completed.returncode, 125)

    def test_socket(self) -> None:
        with tempfile.TemporaryDirectory(prefix='test-') as temp:
            proc = subprocess.Popen(
                self.launcher + [
                    '--socket', os.path.join(temp, 'socket'),
                ],
                stdout=subprocess.PIPE,
                stderr=2,
                universal_newlines=True,
            )

            try:
                socket = ''
                dbus_address = ''

                stdout = proc.stdout
                assert stdout is not None
                for line in stdout:
                    line = line.rstrip('\n')
                    logger.debug('%s', line)

                    if line.startswith('socket='):
                        socket = line[len('socket='):]
                    elif line.startswith('dbus_address='):
                        dbus_address = (
                            line[len('dbus_address='):]
                        )

                self.assertTrue(socket)
                self.assertTrue(dbus_address)

                # The path has been canonicalized, so it might not
                # be equal to the input, but the basename will be the same
                self.assertEqual(os.path.basename(socket), 'socket')
                left = os.stat(socket)
                right = os.stat(os.path.join(temp, 'socket'))
                self.assertEqual(left.st_dev, right.st_dev)
                self.assertEqual(left.st_ino, right.st_ino)

                completed = run_subprocess(
                    self.launch + [
                        '--socket', socket,
                        '--',
                        'printf', 'hello',
                    ],
                    check=True,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                self.assertEqual(completed.stdout, b'hello')

                completed = run_subprocess(
                    self.launch + [
                        '--dbus-address', dbus_address,
                        '--',
                        'sh', '-euc', 'printf hello; printf W"ORL"D >&2',
                    ],
                    check=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                self.assertEqual(completed.stdout, b'hello')
                self.assertIn(b'WORLD', completed.stderr)

                with self.subTest('direct D-Bus connection'):
                    try:
                        from gi.repository import GLib, Gio
                    except ImportError as e:
                        self.skipTest(str(e))

                    conn = Gio.DBusConnection.new_for_address_sync(
                        dbus_address,
                        Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT,
                        None,
                        None,
                    )

                    conn2 = Gio.DBusConnection.new_for_address_sync(
                        dbus_address,
                        Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT,
                        None,
                        None,
                    )

                    reply, out_fd_list = conn.call_with_unix_fd_list_sync(
                        None,
                        LAUNCHER_PATH,
                        LAUNCHER_IFACE,
                        'Launch',
                        GLib.Variant(
                            '(ayaaya{uh}a{ss}ua{sv})',
                            (
                                os.getcwdb() + b'\0',
                                [b'sleep\0', b'3600\0'],
                                {},     # no fds
                                {},     # environment
                                0,      # no flags
                                {},     # no options
                            ),
                        ),
                        GLib.VariantType('(u)'),
                        Gio.DBusCallFlags.NONE,
                        -1,
                        None,   # GUnixFDList is not bound
                        None,
                    )

                    if out_fd_list is not None:
                        self.assertEqual(out_fd_list.get_length(), 0)

                    self.assertEqual(len(reply), 1)
                    pid = int(reply[0])
                    os.kill(pid, 0)

                    with self.assertRaises(Exception) as catcher:
                        conn2.call_sync(
                            None,
                            LAUNCHER_PATH,
                            LAUNCHER_IFACE,
                            'SendSignal',
                            GLib.Variant('(uub)', (pid, signal.SIGTERM, True)),
                            GLib.VariantType('()'),
                            Gio.DBusCallFlags.NONE,
                            -1,
                            None,
                        )

                    logger.debug(
                        'When trying to kill process from wrong '
                        'connection: %s',
                        catcher.exception,
                    )
                    # still not dead
                    os.kill(pid, 0)

                    conn.call_sync(
                        None,
                        LAUNCHER_PATH,
                        LAUNCHER_IFACE,
                        'SendSignal',
                        GLib.Variant('(uub)', (pid, signal.SIGTERM, True)),
                        GLib.VariantType('()'),
                        Gio.DBusCallFlags.NONE,
                        -1,
                        None,
                    )

                    # This is stubbed out, because the pid could conceivably
                    # get reused for an unrelated process that doesn't exit -
                    # but in practice it does work
                    if 'PRESSURE_VESSEL_TEST_NO_STRAY_PROCESSES' in os.environ:
                        for i in range(20):
                            try:
                                os.kill(pid, 0)
                            except OSError:
                                break
                            else:
                                logger.debug('Process %d still alive', pid)
                                time.sleep(0.5)
                                continue
                        else:
                            raise AssertionError(
                                'Process %d did not exit' % pid
                            )

            finally:
                proc.terminate()
                proc.wait()
                self.assertEqual(proc.returncode, 0)

    def test_session_bus(self) -> None:
        try:
            run_subprocess(
                [
                    'dbus-send',
                    '--session',
                    '--dest=org.freedesktop.DBus',
                    '--print-reply',
                    '/org/freedesktop/DBus',
                    'org.freedesktop.DBus.Peer.Ping',
                ],
                check=True,
            )
        except Exception:
            self.skipTest('D-Bus session bus not available')

        unique = '_' + uuid.uuid4().hex

        proc = subprocess.Popen(
            self.launcher + [
                '--bus-name', 'com.steampowered.PressureVessel.Test.' + unique,
            ],
            stdout=subprocess.PIPE,
            stderr=2,
            universal_newlines=True,
        )

        try:
            bus_name = ''

            stdout = proc.stdout
            assert stdout is not None
            for line in stdout:
                line = line.rstrip('\n')
                logger.debug('%s', line)

                if line.startswith('bus_name='):
                    bus_name = line[len('bus_name='):]

            self.assertEqual(
                bus_name,
                'com.steampowered.PressureVessel.Test.' + unique,
            )

            run_subprocess(
                [
                    'dbus-send',
                    '--session',
                    '--dest=' + bus_name,
                    '--print-reply',
                    '/',
                    'org.freedesktop.DBus.Peer.Ping',
                ],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=2,
            )

            completed = run_subprocess(
                self.launch + [
                    '--bus-name', bus_name,
                    '--',
                    'printf', 'hello',
                ],
                check=True,
                stdout=subprocess.PIPE,
                stderr=2,
            )
            self.assertEqual(completed.stdout, b'hello')

        finally:
            proc.terminate()
            proc.wait()
            self.assertEqual(proc.returncode, 0)

    def tearDown(self) -> None:
        super().tearDown()


if __name__ == '__main__':
    assert sys.version_info >= (3, 4), \
        'Python 3.4+ is required'

    test_main()

# vi: set sw=4 sts=4 et:
