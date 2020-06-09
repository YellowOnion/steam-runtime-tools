#!/usr/bin/env python3
# Copyright 2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

"""
Test aspects of pressure-vessel-wrap that do not rely on actually
creating a container.
"""

import logging
import os
import subprocess
import sys

try:
    import typing
except ImportError:
    pass
else:
    typing      # placate pyflakes

from testutils import (
    BaseTest,
    run_subprocess,
    test_main,
)


logger = logging.getLogger('test-invocation')


class TestInvocation(BaseTest):
    def setUp(self) -> None:
        super().setUp()

        self.pv_wrap = os.path.join(
            self.top_builddir, 'src', 'pressure-vessel-wrap',
        )

        artifacts = os.getenv('AUTOPKGTEST_ARTIFACTS')

        if artifacts is not None:
            self.artifacts = artifacts
        else:
            self.artifacts = self.tmpdir.name

    def tearDown(self) -> None:
        super().tearDown()

    def test_help(self) -> None:
        completed = run_subprocess(
            [self.pv_wrap, '--help'],
            stdout=subprocess.PIPE,
            stderr=2,
        )
        self.assertIn('--share-home', completed.stdout.decode('utf-8'))

    def test_invalid_env_if_host(self) -> None:
        completed = run_subprocess(
            [self.pv_wrap, '--env-if-host=bees', 'true'],
            stdout=2,
            stderr=2,
        )
        self.assertEqual(completed.returncode, 2)


if __name__ == '__main__':
    assert sys.version_info >= (3, 4), \
        'Python 3.4+ is required'

    test_main()

# vi: set sw=4 sts=4 et:
