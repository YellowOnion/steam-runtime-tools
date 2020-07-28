#!/usr/bin/env python3
# Copyright 2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

import os
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
    run_subprocess,
    test_main,
)


class TestUtils(BaseTest):
    def setUp(self) -> None:
        super().setUp()
        self.helper = os.path.join(self.G_TEST_BUILDDIR, 'test-helper')

    def test_divert_stdout(self) -> None:
        completed = run_subprocess(
            [self.helper, 'divert-stdout'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self.assertRegex(
            completed.stderr.decode('utf-8'),
            (
                r'(?s)'    # make .* match newlines
                r'printed-with-g-print.*'
                r'logged-as-debug.*'
                r'logged-as-info'
            )
        )
        self.assertEqual(completed.stdout, b'printed-to-original-stdout')
        self.assertEqual(completed.returncode, 0)

    def tearDown(self) -> None:
        super().tearDown()


if __name__ == '__main__':
    assert sys.version_info >= (3, 5), \
        'Python 3.5+ is required (configure with -Dpython=python3.5 ' \
        'if necessary)'

    test_main()

# vi: set sw=4 sts=4 et:
