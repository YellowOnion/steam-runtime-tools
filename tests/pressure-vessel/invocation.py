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
            self.top_builddir, 'pressure-vessel', 'pressure-vessel-wrap',
        )

        artifacts = os.getenv('AUTOPKGTEST_ARTIFACTS')

        if artifacts is not None:
            self.artifacts = artifacts
        else:
            self.artifacts = self.tmpdir.name

    def tearDown(self) -> None:
        super().tearDown()

    def test_filesystem_home_rel_not_allowed(self) -> None:
        """
        We don't implement Flatpak's "~/" yet.
        """
        completed = run_subprocess(
            [self.pv_wrap, '--filesystem=~/Games', 'true'],
            stdout=2,
            stderr=2,
        )
        self.assertEqual(completed.returncode, 2)

    def test_filesystem_special_not_allowed(self) -> None:
        """
        We don't implement Flatpak's various special filesystems yet.
        """
        completed = run_subprocess(
            [self.pv_wrap, '--filesystem=xdg-download/Games', 'true'],
            stdout=2,
            stderr=2,
        )
        self.assertEqual(completed.returncode, 2)

    def test_filesystem_ro_not_allowed(self) -> None:
        """
        We don't implement :ro, :create suffixes yet.
        We reject all paths containing : so that their meaning will not
        change when we implement the Flatpak-style suffixes in future.
        """
        completed = run_subprocess(
            [self.pv_wrap, '--filesystem=/media:ro', 'true'],
            stdout=2,
            stderr=2,
        )
        self.assertEqual(completed.returncode, 2)

    def test_filesystem_backslash_not_allowed(self) -> None:
        """
        We don't implement escaped backslashes or backslash-escaped colons.
        We reject all paths containing backslashes so that their meaning
        will not change when we implement the Flatpak-style behaviour
        in future.
        """
        completed = run_subprocess(
            [self.pv_wrap, '--filesystem=/media/silly\\name', 'true'],
            stdout=2,
            stderr=2,
        )
        self.assertEqual(completed.returncode, 2)

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

    def test_only_prepare_exclusive(self) -> None:
        completed = run_subprocess(
            [self.pv_wrap, '--only-prepare', '--test'],
            stdout=2,
            stderr=2,
        )
        self.assertEqual(completed.returncode, 2)


if __name__ == '__main__':
    assert sys.version_info >= (3, 4), \
        'Python 3.4+ is required'

    test_main()

# vi: set sw=4 sts=4 et:
