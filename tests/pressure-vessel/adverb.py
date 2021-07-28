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

        if 'PRESSURE_VESSEL_UNINSTALLED' in os.environ:
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

    def test_ld_preload(self) -> None:
        completed = run_subprocess(
            self.adverb + [
                '--ld-audit=/nonexistent/libaudit.so',
                '--ld-preload=/nonexistent/libpreload.so',
                '--ld-preload=/nonexistent/ubuntu12_32/gameoverlayrenderer.so',
                '--ld-preload=/nonexistent/ubuntu12_64/gameoverlayrenderer.so',
                '--',
                'sh', '-euc',
                # The hard-coded i686 and xeon_phi here must match up with
                # pv_multiarch_details[i].platforms[0], which is what is used
                # as a mock implementation under
                # PRESSURE_VESSEL_TEST_STANDARDIZE_PLATFORM=1.
                r'''
                ld_audit="$LD_AUDIT"
                ld_preload="$LD_PRELOAD"
                unset LD_AUDIT
                unset LD_PRELOAD

                echo "LD_AUDIT=$ld_audit"
                echo "LD_PRELOAD=$ld_preload"

                IFS=:
                for item in $ld_preload; do
                    case "$item" in
                        (*\$\{PLATFORM\}*)
                            i686="$(echo "$item" |
                                sed -e 's/[$]{PLATFORM}/i686/g')"
                            xeon_phi="$(echo "$item" |
                                sed -e 's/[$]{PLATFORM}/xeon_phi/g')"
                            printf "i686: symlink to "
                            readlink "$i686"
                            printf "xeon_phi: symlink to "
                            readlink "$xeon_phi"
                            ;;
                        (*)
                            echo "literal $item"
                            ;;
                    esac
                done
                ''',
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
        self.assertEqual(
            re.sub(r':/[^:]*?/pressure-vessel-libs-....../',
                   r':/tmp/pressure-vessel-libs-XXXXXX/',
                   lines[1]),
            ('LD_PRELOAD=/nonexistent/libpreload.so'
             ':/tmp/pressure-vessel-libs-XXXXXX/'
             '${PLATFORM}/gameoverlayrenderer.so')
        )
        self.assertEqual(lines[2], 'literal /nonexistent/libpreload.so')
        self.assertEqual(
            lines[3],
            'i686: symlink to /nonexistent/ubuntu12_32/gameoverlayrenderer.so',
        )
        self.assertEqual(
            lines[4],
            ('xeon_phi: symlink to '
             '/nonexistent/ubuntu12_64/gameoverlayrenderer.so'),
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

    def tearDown(self) -> None:
        super().tearDown()


if __name__ == '__main__':
    assert sys.version_info >= (3, 4), \
        'Python 3.4+ is required'

    test_main()

# vi: set sw=4 sts=4 et:
