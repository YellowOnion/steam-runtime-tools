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
    test_main,
)


class TestCheapCopy(BaseTest):
    def setUp(self) -> None:
        super().setUp()
        self.cheap_copy = os.path.join(self.G_TEST_BUILDDIR, 'test-cheap-copy')

    def assert_tree_is_superset(
        self,
        superset,
        subset,
        require_hard_links: bool = True,
    ):
        for path, dirs, files in os.walk(subset):
            equivalent = os.path.join(superset, os.path.relpath(path, subset))

            for d in dirs:
                in_subset = os.path.join(path, d)
                in_superset = os.path.join(equivalent, d)

                if not os.path.isdir(in_superset):
                    raise AssertionError(
                        '%r should be a directory', in_superset)

                info = os.stat(in_subset)
                info2 = os.stat(in_superset)
                self.assertEqual(info.st_mode, info2.st_mode)

            for f in files:
                in_subset = os.path.join(path, f)
                in_superset = os.path.join(equivalent, f)

                if (
                    os.path.islink(in_subset)
                    or not os.path.exists(in_subset)
                ):
                    target = os.readlink(in_subset)
                    target2 = os.readlink(in_superset)
                    self.assertEqual(target, target2)
                else:
                    info = os.stat(in_subset)
                    info2 = os.stat(in_superset)

                    if require_hard_links:
                        self.assertEqual(info.st_ino, info2.st_ino)
                        self.assertEqual(info.st_dev, info2.st_dev)

                    self.assertEqual(info.st_mode, info2.st_mode)
                    self.assertEqual(info.st_size, info2.st_size)
                    self.assertEqual(int(info.st_mtime), int(info2.st_mtime))
                    self.assertEqual(int(info.st_ctime), int(info2.st_ctime))

    def assert_tree_is_same(self, left, right, require_hard_links=True):
        self.assert_tree_is_superset(left, right, require_hard_links)
        self.assert_tree_is_superset(right, left, require_hard_links)

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

    def test_populated(
        self,
        dir1=None,      # type: typing.Optional[str]
        dir2=None,      # type: typing.Optional[str]
        require_hard_links=True,
    ) -> None:
        with tempfile.TemporaryDirectory(
            dir=dir1,
        ) as source, tempfile.TemporaryDirectory(
            dir=dir2,
        ) as parent:
            os.makedirs(os.path.join(source, 'a', 'b', 'c'))
            os.makedirs(os.path.join(source, 'files'))

            with open(os.path.join(source, 'x'), 'w') as writer:
                writer.write('hello')

            # Use unusual (but safe) permissions to assert that they get
            # copied
            os.chmod(source, 0o2740)
            os.chmod(os.path.join(source, 'x'), 0o651)

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
            self.assert_tree_is_same(source, dest, require_hard_links)

    def test_cannot_hard_link(self):
        """
        Assert that we can copy a directory hierarchy between directories
        that might be on different filesystems.

        If /tmp and /var/tmp are both on the same mount point, this is
        equivalent to test_populated(), but if they are on different
        mount points (/tmp is often a tmpfs) then this exercises different
        code paths.
        """
        self.test_populated('/tmp', '/var/tmp', require_hard_links=False)

    def tearDown(self) -> None:
        super().tearDown()


if __name__ == '__main__':
    assert sys.version_info >= (3, 5), \
        'Python 3.5+ is required (configure with -Dpython=python3.5 ' \
        'if necessary)'

    test_main()

# vi: set sw=4 sts=4 et:
