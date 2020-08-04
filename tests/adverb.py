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


try:
    import typing
    typing      # placate pyflakes
except ImportError:
    pass

from testutils import (
    BaseTest,
    test_main,
)


logger = logging.getLogger('test-adverb')


class TestAdverb(BaseTest):
    def setUp(self) -> None:
        super().setUp()

        if 'PRESSURE_VESSEL_UNINSTALLED' in os.environ:
            self.adverb = self.command_prefix + [
                os.path.join(
                    self.top_builddir,
                    'src',
                    'pressure-vessel-adverb'
                ),
            ]
        else:
            self.skipTest('Not available as an installed-test')

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

    def tearDown(self) -> None:
        super().tearDown()


if __name__ == '__main__':
    assert sys.version_info >= (3, 4), \
        'Python 3.4+ is required'

    test_main()

# vi: set sw=4 sts=4 et:
