#!/usr/bin/env python3
# Copyright 2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

import contextlib
import logging
import os
import shutil
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

DBUS_NAME_DBUS = "org.freedesktop.DBus"
DBUS_INTERFACE_DBUS = DBUS_NAME_DBUS
DBUS_PATH_DBUS = "/org/freedesktop/DBus"


@contextlib.contextmanager
def ensure_terminated(proc):
    try:
        yield proc
    finally:
        logger.debug('Cleaning up subprocess if necessary')

        with proc:
            for fh in proc.stdin, proc.stdout, proc.stderr:
                if fh is not None:
                    fh.close()

            if proc.returncode is None:
                logger.debug('Subprocess still running')
                proc.terminate()

                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    logger.debug('Waiting for subprocess timed out')
                    proc.kill()
            else:
                logger.debug('Subprocess already exited %d', proc.returncode)


class TestLauncher(BaseTest):
    def setUp(self) -> None:
        super().setUp()

        if 'PRESSURE_VESSEL_UNINSTALLED' in os.environ:
            self.launcher = self.command_prefix + [
                'env', '-u', 'SRT_LAUNCHER_SERVICE_STOP_ON_EXIT',
                os.path.join(
                    self.top_builddir,
                    'bin',
                    'steam-runtime-launcher-service'
                ),
            ]
            self.launch = self.command_prefix + [
                os.path.join(
                    self.top_builddir,
                    'bin',
                    'steam-runtime-launch-client',
                ),
            ]
        else:
            self.skipTest('Not available as an installed-test')

    @contextlib.contextmanager
    def show_location(self, location):
        logger.debug('enter: %s', location)
        yield
        logger.debug('exit: %s', location)

    def test_socket_directory(self) -> None:
        with contextlib.ExitStack() as stack:
            stack.enter_context(self.show_location('test_socket_directory'))
            temp = stack.enter_context(
                tempfile.TemporaryDirectory(prefix='test-'),
            )
            need_terminate = True
            printf_symlink = os.path.join(temp, 'printf=symlink')
            printf = shutil.which('printf')
            assert printf is not None
            os.symlink(printf, printf_symlink)

            logger.debug('Starting launcher with socket directory')
            proc = subprocess.Popen(
                [
                    'env',
                    'PV_TEST_VAR=from-launcher',
                ] + self.launcher + [
                    '--socket-directory', temp,
                ],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=2,
                universal_newlines=True,
            )
            stack.enter_context(ensure_terminated(proc))

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

                logger.debug('launch-client -- true should succeed')
                completed = run_subprocess(
                    self.launch + [
                        '--socket', socket,
                        '--',
                        'true',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                self.assertEqual(completed.stdout, b'')

                logger.debug('launch-client -- printf hello should succeed')
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

                logger.debug('launch-client -- sh -euc ... should succeed')
                completed = run_subprocess(
                    self.launch + [
                        '--dbus-address', dbus_address,
                        '--',
                        'sh', '-euc', 'printf hello; printf W"ORL"D >&2',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                self.assertEqual(completed.stdout, b'hello')
                self.assertIn(b'WORLD', completed.stderr)

                logger.debug('Checking env not inherited from launch-client')
                completed = run_subprocess(
                    [
                        'env',
                        'PV_TEST_VAR=not-inherited',
                    ] + self.launch + [
                        '--socket', socket,
                        '--',
                        'sh', '-euc', 'printf \'%s\' "$PV_TEST_VAR"',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                self.assertEqual(completed.stdout, b'from-launcher')

                logger.debug('Checking env propagation options')
                completed = run_subprocess(
                    [
                        'env',
                        '-u', 'PV_TEST_VAR',
                        'PV_TEST_VAR_ONE=one',
                        'PV_TEST_VAR_TWO=two',
                    ] + self.launch + [
                        '--pass-env=PV_TEST_VAR',
                        '--pass-env-matching=PV_TEST_VAR_*',
                        '--socket', socket,
                        '--',
                        'sh', '-euc',
                        'printf \'%s\' '
                        '"'
                        '${PV_TEST_VAR-cleared}'
                        '${PV_TEST_VAR_ONE}'
                        '${PV_TEST_VAR_TWO}'
                        '"',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                self.assertEqual(completed.stdout, b'clearedonetwo')

                logger.debug('Checking precedence of environment options')
                completed = run_subprocess(
                    [
                        'env', '-u', 'PV_TEST_VAR',
                    ] + self.launch + [
                        '--env=PV_TEST_VAR=nope',
                        '--pass-env=PV_TEST_VAR',
                        '--socket', socket,
                        '-c', 'printf \'%s\' "${PV_TEST_VAR-cleared}"',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                self.assertEqual(completed.stdout, b'cleared')

                logger.debug('Checking precedence of environment options (2)')
                completed = run_subprocess(
                    self.launch + [
                        '--env=PV_TEST_VAR=nope',
                        '--unset-env=PV_TEST_VAR',
                        '--socket', socket,
                        '--',
                        'sh', '-euc', 'printf \'%s\' "${PV_TEST_VAR-cleared}"',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                self.assertEqual(completed.stdout, b'cleared')

                logger.debug('Checking inadvisable executable name')
                completed = run_subprocess(
                    self.launch + [
                        '--unset-env=PV_TEST_VAR',
                        '--socket', socket,
                        '--',
                        printf_symlink, 'hello',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                self.assertEqual(completed.stdout, b'hello')

                logger.debug('Checking --clear-env')
                completed = run_subprocess(
                    self.launch + [
                        '--clear-env',
                        '--socket', socket,
                        '--',
                        'sh', '-euc', 'echo -n "${PV_TEST_VAR-cleared}"',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                self.assertEqual(completed.stdout, b'cleared')

                logger.debug('Checking --env precedence')
                completed = run_subprocess(
                    [
                        'env',
                        'PV_TEST_VAR=not-inherited',
                    ] + self.launch + [
                        '--pass-env=PV_TEST_VAR',
                        '--env=PV_TEST_VAR=nope',
                        '--env=PV_TEST_VAR=overridden',
                        '--socket', socket,
                        '--',
                        'sh', '-euc', 'printf \'%s\' "$PV_TEST_VAR"',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                self.assertEqual(completed.stdout, b'overridden')

                logger.debug('Checking we can deliver a signal')
                launch = subprocess.Popen(
                    self.launch + [
                        '--socket', socket,
                        '--',
                        'sleep', '600',
                    ],
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                stack.enter_context(ensure_terminated(launch))
                launch.send_signal(signal.SIGINT)
                self.assertIn(
                    launch.wait(timeout=10),
                    (128 + signal.SIGINT, -signal.SIGINT),
                )

                logger.debug('Checking we can terminate the server')
                completed = run_subprocess(
                    self.launch + [
                        '--shell-command=echo "$0 ($1)"',
                        '--socket', socket,
                        '--terminate',
                        '--',
                        'Goodbye',
                        'terminating',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                need_terminate = False
                self.assertEqual(completed.stdout, b'Goodbye (terminating)\n')
            finally:
                if need_terminate:
                    proc.terminate()

                proc.wait(timeout=10)
                self.assertEqual(proc.returncode, 0)

            logger.debug('Checking server really terminated (expect error)')
            completed = run_subprocess(
                self.launch + [
                    '--dbus-address', dbus_address,
                    '--',
                    'printf', 'hello',
                ],
                stdin=subprocess.DEVNULL,
                stdout=2,
                stderr=2,
            )
            self.assertEqual(completed.returncode, 125)

    def test_socket(self) -> None:
        with contextlib.ExitStack() as stack:
            stack.enter_context(self.show_location('test_socket'))
            temp = stack.enter_context(
                tempfile.TemporaryDirectory(prefix='test-'),
            )
            need_terminate = True
            logger.debug('Starting launcher with socket')
            proc = subprocess.Popen(
                self.launcher + [
                    '--socket', os.path.join(temp, 'socket'),
                ],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=2,
                universal_newlines=True,
            )
            stack.enter_context(ensure_terminated(proc))

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

                logger.debug('launch-client should succeed')
                completed = run_subprocess(
                    self.launch + [
                        '--socket', socket,
                        '--',
                        'printf', 'hello',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                self.assertEqual(completed.stdout, b'hello')

                logger.debug('stdout, stderr should work')
                completed = run_subprocess(
                    self.launch + [
                        '--dbus-address', dbus_address,
                        '--',
                        'sh', '-euc', 'printf hello; printf W"ORL"D >&2',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                self.assertEqual(completed.stdout, b'hello')
                self.assertIn(b'WORLD', completed.stderr)

                logger.debug('direct D-Bus connection should work')
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

                    conn.call_sync(
                        None,
                        LAUNCHER_PATH,
                        LAUNCHER_IFACE,
                        'Terminate',
                        GLib.Variant('()', ()),
                        GLib.VariantType('()'),
                        Gio.DBusCallFlags.NONE,
                        -1,
                        None,
                    )
                    need_terminate = False

            finally:
                if need_terminate:
                    proc.terminate()

                proc.wait(timeout=10)
                self.assertEqual(proc.returncode, 0)

    def test_wrap_stop_on_exit(self) -> None:
        self.test_wrap(stop_on_exit=True)

    def test_wrap(self, stop_on_exit=False) -> None:
        with contextlib.ExitStack() as stack:
            stack.enter_context(
                self.show_location('test_wrap(stop_on_exit=%r)' % stop_on_exit)
            )
            temp = stack.enter_context(
                tempfile.TemporaryDirectory(prefix='test-'),
            )
            socket = os.path.join(temp, 'socket')

            if stop_on_exit:
                stop_on_exit_arg = '--stop-on-exit'
            else:
                stop_on_exit_arg = '--no-stop-on-exit'

            logger.debug('Running launcher wrapping short-lived command')
            proc = subprocess.Popen(
                self.launcher + [
                    '--socket', socket,
                    stop_on_exit_arg,
                    '--',
                    'printf', 'wrapped printf',
                ],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=2,
                universal_newlines=True,
            )
            stack.enter_context(ensure_terminated(proc))

            stdout = proc.stdout
            assert stdout is not None
            # Wait for the wrapped process to have started, which only happens
            # when the launcher is also open for business
            output = stdout.read()

            if not stop_on_exit:
                # Check that the server did not stop
                logger.debug('Launcher should still be alive')
                run_subprocess(
                    self.launch + [
                        '--socket', socket,
                        '--',
                        'true',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=2,
                    stderr=2,
                )
                logger.debug('Launcher should exit after this')
                run_subprocess(
                    self.launch + [
                        '--socket', socket,
                        '--terminate',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=2,
                    stderr=2,
                )
            # else the process exits as soon as printf does, which is
            # almost immediately

            more_output, errors = proc.communicate(timeout=10)
            self.assertEqual(proc.returncode, 0)

            # With a wrapped command but no --info-fd, there is no
            # extraneous output on stdout
            self.assertEqual('wrapped printf', output)
            self.assertEqual('', more_output)

    def test_wrap_info_fd(self) -> None:
        with contextlib.ExitStack() as stack:
            stack.enter_context(self.show_location('test_wrap_info_fd'))
            temp = stack.enter_context(
                tempfile.TemporaryDirectory(prefix='test-'),
            )
            logger.debug('Starting launcher with --info-fd')
            proc = subprocess.Popen(
                [
                    # subprocess.Popen doesn't let us set arbitrary
                    # file descriptors, so use a shell to juggle them
                    'sh', '-euc', 'exec "$@" 3>&1 >&2 2>/dev/null', 'sh',
                ] + self.launcher + [
                    '--info-fd=3',
                    '--socket', os.path.join(temp, 'socket'),
                    '--',
                    'cat',
                ],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
            )
            stack.enter_context(ensure_terminated(proc))

            socket = ''
            dbus_address = ''

            # stdout is the launcher's fd 3, which is the info-fd
            stdout = proc.stdout
            assert stdout is not None
            for line in stdout:
                line = line.rstrip('\n')
                logger.debug('%s', line)

                if line.startswith('socket='):
                    socket = line[len('socket='):]
                elif line.startswith('dbus_address='):
                    dbus_address = line[len('dbus_address='):]

            self.assertTrue(socket)
            self.assertTrue(dbus_address)

            # The path has been canonicalized, so it might not
            # be equal to the input, but the basename will be the same
            self.assertEqual(os.path.basename(socket), 'socket')
            left = os.stat(socket)
            right = os.stat(os.path.join(temp, 'socket'))
            self.assertEqual(left.st_dev, right.st_dev)
            self.assertEqual(left.st_ino, right.st_ino)

            # The process exits as soon as cat does
            _, errors = proc.communicate(
                input='this goes to stderr',
                timeout=10,
            )
            self.assertEqual(proc.returncode, 0)
            # The wrapped process's stdout has ended up in stderr
            self.assertIn('this goes to stderr', errors)

    def test_wrap_mainpid(self) -> None:
        with contextlib.ExitStack() as stack:
            stack.enter_context(self.show_location('test_wrap_mainpid'))
            temp = stack.enter_context(
                tempfile.TemporaryDirectory(prefix='test-'),
            )
            socket = os.path.join(temp, 'socket')
            logger.debug('Starting launcher to test MAINPID')
            proc = subprocess.Popen(
                self.launcher + [
                    '--socket', socket,
                    '--',
                    'sh', '-euc', 'printf "%s" "$$"; exec cat >&2',
                ],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=2,
                universal_newlines=True,
            )
            stack.enter_context(ensure_terminated(proc))
            stdout = proc.stdout
            assert stdout is not None

            # Wait long enough for it to be listening: the wrapped command
            # runs after the socket is already up, and prints $$ first
            shell_pid = stdout.readline().rstrip('\n')

            # We can run commands
            logger.debug('Retrieving MAINPID')
            get_mainpid = run_subprocess(
                self.launch + [
                    '--socket', socket,
                    '--',
                    'sh', '-euc', 'printf "%s" "$MAINPID"',
                ],
                check=True,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=2,
                universal_newlines=True,
            )

            output, errors = proc.communicate(input='', timeout=10)
            self.assertEqual(proc.returncode, 0)
            self.assertEqual(shell_pid, get_mainpid.stdout)
            self.assertEqual(output, '')

    def test_wrap_exec_fallback(self) -> None:
        with contextlib.ExitStack() as stack:
            stack.enter_context(self.show_location('test_wrap_exec_fallback'))
            temp = stack.enter_context(
                tempfile.TemporaryDirectory(prefix='test-'),
            )
            socket = os.path.join(temp, 'nope', 'socket')

            logger.debug('Running a server that will fail fast')
            proc = subprocess.Popen(
                self.launcher + [
                    '--socket', socket,
                    '--',
                    'printf', 'this never happens',
                ],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=2,
                universal_newlines=True,
            )
            stack.enter_context(ensure_terminated(proc))
            output, errors = proc.communicate(timeout=10)
            self.assertEqual(output, '')

            logger.debug('Running a server that will fail but continue')
            proc = subprocess.Popen(
                self.launcher + [
                    '--info-fd=0',
                    '--socket', socket,
                    '--exec-fallback',
                    '--',
                    'printf', 'ran this anyway',
                ],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=2,
                universal_newlines=True,
            )
            stack.enter_context(ensure_terminated(proc))
            output, errors = proc.communicate(timeout=10)
            self.assertEqual(output, 'ran this anyway')

            logger.debug('Different info fd')
            read_end, write_end = os.pipe2(os.O_CLOEXEC)
            proc = subprocess.Popen(
                self.launcher + [
                    '--info-fd=%d' % write_end,
                    '--socket', socket,
                    '--exec-fallback',
                    '--',
                    'printf', 'ran this anyway',
                ],
                pass_fds=[write_end],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=2,
                universal_newlines=True,
            )
            stack.enter_context(ensure_terminated(proc))
            os.close(write_end)

            with open(read_end, 'r') as reader:
                output = reader.read()
                self.assertEqual(output, '')

            output, errors = proc.communicate(timeout=10)
            self.assertEqual(output, 'ran this anyway')

    def test_wrap_wait(self) -> None:
        with contextlib.ExitStack() as stack:
            stack.enter_context(self.show_location('test_wrap_wait'))
            temp = stack.enter_context(
                tempfile.TemporaryDirectory(prefix='test-'),
            )
            socket = os.path.join(temp, 'socket')
            need_terminate = True
            logger.debug('Starting launcher to wait for a main command')
            proc = subprocess.Popen(
                self.launcher + [
                    '--socket', socket,
                    '--',
                    'cat',
                ],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                universal_newlines=True,
            )
            stack.enter_context(ensure_terminated(proc))

            try:
                stdin = proc.stdin
                assert stdin is not None
                stdout = proc.stdout
                assert stdout is not None

                # Wait for cat(1) to have started, which only happens
                # when the launcher is also open for business
                logger.debug(
                    'Waiting to be able to get a newline back from cat(1)',
                )
                stdin.write('\n')
                stdin.flush()
                self.assertEqual(stdout.read(1), '\n')

                # We can run commands
                logger.debug('Sending command')
                completed = run_subprocess(
                    self.launch + [
                        '--socket', socket,
                        '--',
                        'printf', 'hello',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                self.assertEqual(completed.stdout, b'hello')

                # We can terminate the `cat` command
                logger.debug('Terminating main command')
                completed = run_subprocess(
                    self.launch + [
                        '--socket', socket,
                        '--terminate',
                        '--',
                        'printf', 'world',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE,
                    stderr=2,
                )
                self.assertEqual(completed.stdout, b'world')
                need_terminate = False

                # The `cat` command terminates promptly, even though it has
                # not seen EOF on its stdin
                logger.debug('Waiting for EOF on stdout...')
                stdout.read()
            finally:
                if need_terminate:
                    proc.terminate()

                proc.wait(timeout=10)
                self.assertEqual(proc.returncode, 0)

            # It really stopped
            logger.debug('Checking it really stopped (expect an error)...')
            completed = run_subprocess(
                self.launch + [
                    '--socket', socket,
                    '--',
                    'printf', 'hello',
                ],
                stdin=subprocess.DEVNULL,
                stdout=2,
                stderr=2,
            )
            self.assertEqual(completed.returncode, 125)

    def needs_dbus(self) -> None:
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
                stdin=subprocess.DEVNULL,
                stdout=2,
                stderr=2,
            )
        except Exception:
            self.skipTest('D-Bus session bus not available')

    def test_session_bus_invalid_name(self) -> None:
        self.test_session_bus(invalid_name=True)

    def test_session_bus(self, invalid_name=False) -> None:
        with contextlib.ExitStack() as stack:
            stack.enter_context(
                self.show_location(
                    'test_session_bus(invalid_name=%r)' % invalid_name
                )
            )
            self.needs_dbus()
            unique = str(uuid.uuid4())

            if invalid_name:
                unique = unique.replace('-', '.')
                well_known_name = 'com.steampowered.App' + unique.replace(
                    '.', '_',
                )
            else:
                well_known_name = 'com.steampowered.App' + unique

            logger.debug('Starting launcher on D-Bus name %s', well_known_name)
            proc = subprocess.Popen(
                [
                    'env', 'STEAM_COMPAT_APP_ID=' + unique,
                ] + self.launcher + [
                    '--session',
                ],
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=2,
                universal_newlines=True,
            )
            stack.enter_context(ensure_terminated(proc))

            bus_name = ''

            stdout = proc.stdout
            assert stdout is not None
            for line in stdout:
                line = line.rstrip('\n')
                logger.debug('%s', line)

                if line.startswith('bus_name='):
                    bus_name = line[len('bus_name='):]

            # With neither --stop-on-name-loss nor --no-stop-on-name-loss,
            # it is unspecified which behaviour we have
            # (implementation detail: there's an environment variable)
            if not bus_name.startswith(':'):
                self.assertEqual(bus_name, well_known_name)

            logger.debug('Should be able to ping %s', bus_name)
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
                stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,
                stderr=2,
            )

            logger.debug('Should be able to launch command via %s', bus_name)
            completed = run_subprocess(
                self.launch + [
                    '--bus-name', bus_name,
                    '--',
                    'printf', 'hello',
                ],
                check=True,
                stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE,
                stderr=2,
            )
            self.assertEqual(completed.stdout, b'hello')

            proc.terminate()
            proc.wait(timeout=10)
            self.assertEqual(proc.returncode, 0)

    def test_session_bus_replace_stop(self) -> None:
        self.test_session_bus_replace(stop_on_name_loss=True)

    def test_session_bus_replace(self, stop_on_name_loss=False) -> None:
        with contextlib.ExitStack() as stack:
            stack.enter_context(
                self.show_location(
                    'test_session_bus_replace(stop_on_name_loss=%r)'
                    % stop_on_name_loss
                )
            )
            self.needs_dbus()
            unique = '_' + uuid.uuid4().hex
            well_known_name = 'com.steampowered.PressureVessel.Test.' + unique
            only_first_well_known_name = well_known_name + '.First'
            only_second_well_known_name = well_known_name + '.Second'

            try:
                from gi.repository import GLib, Gio
            except ImportError as e:
                self.skipTest(str(e))

            if stop_on_name_loss:
                stop_on_name_loss_arg = '--stop-on-name-loss'
            else:
                stop_on_name_loss_arg = '--no-stop-on-name-loss'

            logger.debug('Starting first server on %s', well_known_name)
            first_server = subprocess.Popen(
                self.launcher + [
                    '--bus-name', well_known_name,
                    '--bus-name', only_first_well_known_name,
                    '--exit-on-readable=0',
                    stop_on_name_loss_arg,
                ],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=2,
                universal_newlines=True,
            )
            stack.enter_context(ensure_terminated(first_server))

            logger.debug('Waiting for its bus name')
            stdout = first_server.stdout
            assert stdout is not None
            for line in stdout:
                line = line.rstrip('\n')
                logger.debug('%s', line)

                if line.startswith('bus_name='):
                    bus_name = line[len('bus_name='):]

                    if stop_on_name_loss:
                        if not bus_name.startswith(':'):
                            self.assertEqual(bus_name, well_known_name)

                        session_bus = Gio.bus_get_sync(
                            Gio.BusType.SESSION, None,
                        )
                        reply = session_bus.call_sync(
                            DBUS_NAME_DBUS,
                            DBUS_PATH_DBUS,
                            DBUS_INTERFACE_DBUS,
                            'GetNameOwner',
                            GLib.Variant('(s)', (bus_name,)),
                            GLib.VariantType('(s)'),
                            Gio.DBusCallFlags.NONE,
                            -1,
                            None,
                        )
                        self.assertEqual(len(reply), 1)
                        first_unique_name = str(reply[0])
                    else:
                        self.assertTrue(bus_name.startswith(':'), bus_name)
                        first_unique_name = bus_name

            logger.debug('Starting cat(1) subprocess')
            cat = subprocess.Popen(
                self.launch + [
                    '--bus-name', bus_name,
                    '--',
                    'cat',
                ],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=2,
                universal_newlines=True,
            )
            stack.enter_context(ensure_terminated(cat))

            logger.debug('Secondary bus name should also work')
            run_subprocess(
                self.launch + [
                    '--bus-name', only_first_well_known_name,
                    '--',
                    'true',
                ],
                check=True,
                stdin=subprocess.DEVNULL,
                stdout=2,
                stderr=2,
            )

            logger.debug('Starting second server to --replace the first')
            second_server = subprocess.Popen(
                self.launcher + [
                    '--bus-name', well_known_name,
                    '--bus-name', only_second_well_known_name,
                    '--exit-on-readable=0',
                    '--replace',
                ],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=2,
                universal_newlines=True,
            )
            stack.enter_context(ensure_terminated(second_server))

            logger.debug('Waiting for second server to be ready')
            stdout = second_server.stdout
            assert stdout is not None
            # Read until EOF
            stdout.read()

            logger.debug('Checking all three processes are running')
            first_server.send_signal(0)
            second_server.send_signal(0)
            cat.send_signal(0)

            logger.debug('Secondary bus name should also work')
            run_subprocess(
                self.launch + [
                    '--bus-name', only_second_well_known_name,
                    '--',
                    'true',
                ],
                check=True,
                stdin=subprocess.DEVNULL,
                stdout=2,
                stderr=2,
            )

            if not stop_on_name_loss:
                logger.debug(
                    'First server secondary bus name should also work',
                )
                run_subprocess(
                    self.launch + [
                        '--bus-name', only_first_well_known_name,
                        '--',
                        'true',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=2,
                    stderr=2,
                )
                # else: because the cat(1) subprocess is still running, the
                # first server is still running, but it has released its
                # well-known names so we can no longer talk to it that way

            # We can still communicate with a subprocess of the
            # first, even after it has lost its name
            logger.debug('Checking cat(1) is still responding')
            output, _ = cat.communicate(input='nyan', timeout=10)
            self.assertEqual(output, 'nyan')
            self.assertEqual(cat.returncode, 0)

            if stop_on_name_loss:
                logger.debug('Waiting for first server to exit')
                first_server.wait(timeout=10)
                self.assertEqual(first_server.returncode, 0)
            else:
                logger.debug('Checking that first server did not stop')
                run_subprocess(
                    self.launch + [
                        '--bus-name', first_unique_name,
                        '--',
                        'true',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=2,
                    stderr=2,
                )
                run_subprocess(
                    self.launch + [
                        '--bus-name', only_first_well_known_name,
                        '--',
                        'true',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=2,
                    stderr=2,
                )

            # The second server shuts down on --exit-on-readable
            logger.debug('Stopping second server')
            second_server.communicate(input='', timeout=10)
            self.assertEqual(second_server.returncode, 0)

            if not stop_on_name_loss:
                # The first server is still in the queue for the name,
                # so it gets the well-known name back (atomically)
                # after the dbus-daemon has detected the change in
                # status.
                session_bus = Gio.bus_get_sync(
                    Gio.BusType.SESSION, None,
                )
                reply = None

                # It might take a short time for the dbus-daemon to
                # detect that the second service's connection has closed.
                # Until then, it will think the second service still
                # has the name.
                for attempt in range(50):
                    logger.debug(
                        'Waiting for first server to get name back',
                    )
                    reply = session_bus.call_sync(
                        DBUS_NAME_DBUS,
                        DBUS_PATH_DBUS,
                        DBUS_INTERFACE_DBUS,
                        'GetNameOwner',
                        GLib.Variant('(s)', (bus_name,)),
                        GLib.VariantType('(s)'),
                        Gio.DBusCallFlags.NONE,
                        -1,
                        None,
                    )
                    assert reply is not None
                    self.assertEqual(len(reply), 1)
                    owner = str(reply[0])

                    if owner == first_unique_name:
                        logger.debug(
                            'First server %s got its name back',
                            first_unique_name,
                        )
                        break
                    else:
                        logger.debug('Name owned by %s', owner)

                    time.sleep(0.1)
                else:
                    raise AssertionError(
                        'Timed out waiting for name to go back to '
                        'first service'
                    )

                logger.debug('Asking first server to exit')
                run_subprocess(
                    self.launch + [
                        '--bus-name', first_unique_name,
                        '--terminate',
                    ],
                    check=True,
                    stdin=subprocess.DEVNULL,
                    stdout=2,
                    stderr=2,
                )
                # The first server terminates when asked
                logger.debug('Waiting for it')
                first_server.communicate(input='', timeout=10)
                self.assertEqual(first_server.returncode, 0)

    def test_exit_on_readable(self, use_stdin=False) -> None:
        with contextlib.ExitStack() as stack:
            stack.enter_context(
                self.show_location('test_exit_on_readable(%r)' % use_stdin)
            )
            temp = stack.enter_context(
                tempfile.TemporaryDirectory(prefix='test-'),
            )

            if not use_stdin:
                read_end, write_end = os.pipe2(os.O_CLOEXEC)
                pass_fds = [read_end]
                fd = read_end
            else:
                pass_fds = []
                fd = 0
                read_end = -1
                write_end = -1

            logger.debug(
                'Starting service that will exit when fd %d becomes readable',
                fd,
            )
            proc = subprocess.Popen(
                self.launcher + [
                    '--socket', os.path.join(temp, 'socket'),
                    '--exit-on-readable=%d' % fd,
                ],
                pass_fds=pass_fds,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=2,
                universal_newlines=True,
            )
            stack.enter_context(ensure_terminated(proc))
            stdin = proc.stdin
            assert stdin is not None

            if not use_stdin:
                logger.debug('Closing pipe')
                os.close(read_end)
                os.close(write_end)

            # this closes stdin
            logger.debug('Waiting for exit')
            proc.communicate(input='', timeout=10)
            self.assertEqual(proc.returncode, 0)

    def test_exit_on_stdin(self) -> None:
        self.test_exit_on_readable(use_stdin=True)

    def tearDown(self) -> None:
        super().tearDown()


if __name__ == '__main__':
    assert sys.version_info >= (3, 4), \
        'Python 3.4+ is required'

    test_main()

# vi: set sw=4 sts=4 et:
