#!/usr/bin/env python3
# Copyright 2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

import os
import subprocess
import sys
import tempfile
import unittest


try:
    import typing
    typing      # placate pyflakes
except ImportError:
    pass


class TestCheapCopy(unittest.TestCase):
    def setUp(self) -> None:
        self.G_TEST_SRCDIR = os.getenv(
            'G_TEST_SRCDIR',
            os.path.abspath(
                os.path.join(os.path.dirname(__file__), os.pardir),
            ),
        )
        self.G_TEST_BUILDDIR = os.getenv(
            'G_TEST_BUILDDIR',
            os.path.abspath('_build'),
        )
        self.cheap_copy = os.path.join(self.G_TEST_BUILDDIR, 'test-cheap-copy')

    def assert_tree_is_superset(self, superset, subset):
        for path, dirs, files in os.walk(subset):
            equivalent = os.path.join(superset, os.path.relpath(path, subset))

            for d in dirs:
                if not os.path.isdir(os.path.join(equivalent, d)):
                    raise AssertionError(
                        '%r should be a directory', equivalent)

            for f in files:
                in_subset = os.path.join(path, f)
                if (
                    os.path.islink(in_subset)
                    or not os.path.exists(in_subset)
                ):
                    target = os.readlink(in_subset)
                    target2 = os.readlink(os.path.join(equivalent, f))
                    self.assertEqual(target, target2)
                else:
                    info = os.stat(in_subset)
                    info2 = os.stat(os.path.join(equivalent, f))
                    # they should be hard links
                    self.assertEqual(info.st_ino, info2.st_ino)
                    self.assertEqual(info.st_dev, info2.st_dev)

    def assert_tree_is_same(self, left, right):
        self.assert_tree_is_superset(left, right)
        self.assert_tree_is_superset(right, left)

    def test_empty(self) -> None:
        with tempfile.TemporaryDirectory(
        ) as source, tempfile.TemporaryDirectory(
        ) as dest:
            subprocess.run(
                [
                    self.cheap_copy,
                    source,
                    dest,
                ],
                check=True,
            )
            self.assert_tree_is_same(source, dest)

    def test_create(self) -> None:
        with tempfile.TemporaryDirectory(
        ) as source, tempfile.TemporaryDirectory(
        ) as parent:
            dest = os.path.join(parent, 'dest')
            subprocess.run(
                [
                    self.cheap_copy,
                    source,
                    dest,
                ],
                check=True,
            )
            self.assert_tree_is_same(source, dest)

    def test_populated(self) -> None:
        with tempfile.TemporaryDirectory(
        ) as source, tempfile.TemporaryDirectory(
        ) as parent:
            os.makedirs(os.path.join(source, 'a', 'b', 'c'))
            os.makedirs(os.path.join(source, 'files'))

            with open(os.path.join(source, 'x'), 'w') as writer:
                writer.write('hello')

            with open(os.path.join(source, 'files', 'y'), 'w') as writer:
                writer.write('hello')

            os.symlink('y', os.path.join(source, 'files', 'exists'))
            os.symlink('/dev', os.path.join(source, 'files', 'dev'))
            os.symlink('no', os.path.join(source, 'files', 'not here'))

            dest = os.path.join(parent, 'dest')
            subprocess.run(
                [
                    self.cheap_copy,
                    source,
                    dest,
                ],
                check=True,
            )
            self.assert_tree_is_same(source, dest)

    def tearDown(self) -> None:
        pass


if __name__ == '__main__':
    assert sys.version_info >= (3, 5), \
        'Python 3.5+ is required (configure with -Dpython=python3.5 ' \
        'if necessary)'

    try:
        from tap.runner import TAPTestRunner
    except ImportError:
        TAPTestRunner = None    # type: ignore

    if TAPTestRunner is not None:
        runner = TAPTestRunner()
        runner.set_stream(True)
        unittest.main(testRunner=runner)
    else:
        print('1..1')
        program = unittest.main(exit=False)
        if program.result.wasSuccessful():
            print(
                'ok 1 - %r (tap module not available)'
                % program.result
            )
        else:
            print(
                'not ok 1 - %r (tap module not available)'
                % program.result
            )

# vi: set sw=4 sts=4 et:
