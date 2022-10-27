#!/usr/bin/env python3
# Copyright 2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

import logging
import os
import re
import signal
import subprocess
import sys


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


logger = logging.getLogger('test-adverb')


EX_USAGE = 64


class TestAdverb(BaseTest):
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

    def setUp(self) -> None:
        super().setUp()
        self.uname = os.uname()

        if 'SRT_TEST_UNINSTALLED' in os.environ:
            self.adverb = self.command_prefix + [
                'env',
                '-u', 'LD_AUDIT',
                '-u', 'LD_PRELOAD',
                # In unit tests it isn't straightforward to find the real
                # ${PLATFORM}, so we use a predictable mock implementation
                # that always uses PvMultiarchDetails.platforms[0].
                'PRESSURE_VESSEL_TEST_STANDARDIZE_PLATFORM=1',
                os.path.join(
                    self.top_builddir,
                    'pressure-vessel',
                    'pressure-vessel-adverb'
                ),
            ]
            self.helper = self.command_prefix + [
                os.path.join(
                    self.top_builddir,
                    'tests',
                    'pressure-vessel',
                    'test-helper'
                ),
            ]
        else:
            self.skipTest('Not available as an installed-test')

        self.multiarch = os.environ.get('SRT_TEST_MULTIARCH')
        if not self.multiarch:
            self.skipTest('No multiarch tuple has been set')

    def test_ld_preload(self) -> None:
        if (
            self.multiarch == 'x86_64-linux-gnu'
            or self.multiarch == 'i386-linux-gnu'
        ):
            preloads = [
                '--ld-preload=/nonexistent/libpreload.so',
                '--ld-preload=/nonexistent/ubuntu12_32/gameoverlayrenderer.so',
                '--ld-preload=/nonexistent/ubuntu12_64/gameoverlayrenderer.so',
                ('--ld-preload'
                 '=/nonexistent/lib32/libMangoHud.so'
                 ':abi=i386-linux-gnu'),
                ('--ld-preload'
                 '=/nonexistent/lib64/libMangoHud.so'
                 ':abi=x86_64-linux-gnu'),
                ('--ld-preload'
                 '=/nonexistent/lib64/64-bit-only.so'
                 ':abi=x86_64-linux-gnu'),
            ]
            # The hard-coded i686 and xeon_phi here must match up with
            # pv_multiarch_details[i].platforms[0], which is what is used
            # as a mock implementation under
            # PRESSURE_VESSEL_TEST_STANDARDIZE_PLATFORM=1.
            script_for_loop = r'''
                for item in $ld_preload; do
                    case "$item" in
                        (*\$\{PLATFORM\}*)
                            i686="$(echo "$item" |
                                sed -e 's/[$]{PLATFORM}/i686/g')"
                            xeon_phi="$(echo "$item" |
                                sed -e 's/[$]{PLATFORM}/xeon_phi/g')"
                            printf "i686: symlink to "
                            readlink "$i686" || echo "(nothing)"
                            printf "xeon_phi: symlink to "
                            readlink "$xeon_phi" || echo "(nothing)"
                            ;;
                        (*)
                            echo "literal $item"
                            ;;
                    esac
                done
            '''
        else:
            multiarch = self.multiarch
            # If it was None, we would have skipped the test during setup
            assert multiarch is not None
            preloads = [
                '--ld-preload=/nonexistent/libpreload.so',
                ('--ld-preload=/nonexistent/lib64/libMangoHud.so:abi='
                 + multiarch),
                ('--ld-preload=/nonexistent/lib64/64-bit-only.so:abi='
                 + multiarch),
            ]
            script_for_loop = r'''
                for item in $ld_preload; do
                    case "$item" in
                        (*\$\{PLATFORM\}*)
                            mock="$(echo "$item" |
                                sed -e 's/[$]{PLATFORM}/mock/g')"
                            printf "mock: symlink to "
                            readlink "$mock" || echo "(nothing)"
                            ;;
                        (*)
                            echo "literal $item"
                            ;;
                    esac
                done
            '''

        completed = run_subprocess(
            self.adverb + [
                '--ld-audit=/nonexistent/libaudit.so',
            ] + preloads + [
                '--',
                'sh', '-euc',
                r'''
                ld_audit="$LD_AUDIT"
                ld_preload="$LD_PRELOAD"
                unset LD_AUDIT
                unset LD_PRELOAD

                echo "LD_AUDIT=$ld_audit"
                echo "LD_PRELOAD=$ld_preload"

                IFS=:
                ''' + script_for_loop,
            ],
            check=True,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=2,
            universal_newlines=True,
        )

        stdout = completed.stdout
        assert stdout is not None
        lines = stdout.splitlines()
        self.assertEqual(
            lines[0],
            'LD_AUDIT=/nonexistent/libaudit.so',
        )

        if (
            self.multiarch == 'x86_64-linux-gnu'
            or self.multiarch == 'i386-linux-gnu'
        ):
            self.assertEqual(
                re.sub(r':/[^:]*?/pressure-vessel-libs-....../',
                       r':/tmp/pressure-vessel-libs-XXXXXX/',
                       lines[1]),
                ('LD_PRELOAD=/nonexistent/libpreload.so'
                 ':/tmp/pressure-vessel-libs-XXXXXX/'
                 '${PLATFORM}/gameoverlayrenderer.so'
                 ':/tmp/pressure-vessel-libs-XXXXXX/'
                 '${PLATFORM}/libMangoHud.so'
                 ':/tmp/pressure-vessel-libs-XXXXXX/'
                 '${PLATFORM}/64-bit-only.so')
            )
            self.assertEqual(lines[2], 'literal /nonexistent/libpreload.so')
            self.assertEqual(
                lines[3],
                ('i686: symlink to '
                 '/nonexistent/ubuntu12_32/gameoverlayrenderer.so'),
            )
            self.assertEqual(
                lines[4],
                ('xeon_phi: symlink to '
                 '/nonexistent/ubuntu12_64/gameoverlayrenderer.so'),
            )
            self.assertEqual(
                lines[5],
                'i686: symlink to /nonexistent/lib32/libMangoHud.so',
            )
            self.assertEqual(
                lines[6],
                'xeon_phi: symlink to /nonexistent/lib64/libMangoHud.so',
            )
            self.assertEqual(
                lines[7],
                'i686: symlink to (nothing)',
            )
            self.assertEqual(
                lines[8],
                'xeon_phi: symlink to /nonexistent/lib64/64-bit-only.so',
            )
        else:
            self.assertEqual(
                re.sub(r':/[^:]*?/pressure-vessel-libs-....../',
                       r':/tmp/pressure-vessel-libs-XXXXXX/',
                       lines[1]),
                ('LD_PRELOAD=/nonexistent/libpreload.so'
                 ':/tmp/pressure-vessel-libs-XXXXXX/'
                 '${PLATFORM}/libMangoHud.so'
                 ':/tmp/pressure-vessel-libs-XXXXXX/'
                 '${PLATFORM}/64-bit-only.so')
            )
            self.assertEqual(lines[2], 'literal /nonexistent/libpreload.so')
            self.assertEqual(
                lines[3],
                'mock: symlink to /nonexistent/lib64/libMangoHud.so',
            )
            self.assertEqual(
                lines[4],
                'mock: symlink to /nonexistent/lib64/64-bit-only.so',
            )

    def test_stdio_passthrough(self) -> None:
        proc = subprocess.Popen(
            self.adverb + [
                '--',
                'sh', '-euc',
                '''
                if [ "$(cat)" != "hello, world!" ]; then
                    exit 1
                fi

                echo $$
                exec >/dev/null
                exec sleep infinity
                ''',
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=2,
            universal_newlines=True,
        )
        pid = 0

        try:
            stdin = proc.stdin
            assert stdin is not None
            stdin.write('hello, world!')
            stdin.close()

            stdout = proc.stdout
            assert stdout is not None
            pid = int(stdout.read().strip())
        finally:
            if pid:
                os.kill(pid, signal.SIGTERM)
            else:
                proc.terminate()

            self.assertIn(
                proc.wait(),
                (128 + signal.SIGTERM, -signal.SIGTERM),
            )

    def test_fd_passthrough(self) -> None:
        read_end, write_end = os.pipe2(os.O_CLOEXEC)

        proc = subprocess.Popen(
            self.adverb + [
                '--pass-fd=%d' % write_end,
                '--',
                'sh', '-euc', 'echo hello >&%d' % write_end,
            ],
            pass_fds=[write_end],
            stdout=2,
            stderr=2,
            universal_newlines=True,
        )

        try:
            os.close(write_end)

            with os.fdopen(read_end, 'rb') as reader:
                self.assertEqual(reader.read(), b'hello\n')
        finally:
            proc.wait()
            self.assertEqual(proc.returncode, 0)

    def test_wrong_options(self) -> None:
        for option in (
            '--an-unknown-option',
            '--ld-preload=/nonexistent/libfoo.so:abi=hal9000-movieos',
            '--ld-preload=/nonexistent/libfoo.so:abi=i386-linux-gnu:foo',
            '--ld-preload=/nonexistent/libfoo.so:foo=bar',
            '--pass-fd=-1',
            '--shell=wrong',
            '--terminal=wrong',
        ):
            proc = subprocess.Popen(
                self.adverb + [
                    option,
                    '--',
                    'sh', '-euc', 'exit 42',
                ],
                stdout=2,
                stderr=2,
                universal_newlines=True,
            )
            proc.wait()
            self.assertEqual(proc.returncode, EX_USAGE)

    def tearDown(self) -> None:
        super().tearDown()


if __name__ == '__main__':
    assert sys.version_info >= (3, 4), \
        'Python 3.4+ is required'

    test_main()

# vi: set sw=4 sts=4 et:
