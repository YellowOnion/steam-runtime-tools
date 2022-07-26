#!/usr/bin/env python3
# Copyright 2020 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

import os
import subprocess
import sys
import tempfile
from pathlib import Path


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
        os.environ['G_MESSAGES_DEBUG'] = 'all'
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
                stdout=2,       # >&2, i.e. stderr
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
                stdout=2,       # >&2, i.e. stderr
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
                stdout=2,       # >&2, i.e. stderr
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

    def test_usrmerge(self):
        with tempfile.TemporaryDirectory(
        ) as source, tempfile.TemporaryDirectory(
        ) as parent, tempfile.TemporaryDirectory(
        ) as expected:
            (Path(source) / 'bin').mkdir(parents=True)
            (Path(source) / 'lib').mkdir(parents=True)
            (Path(source) / 'lib/x86_64-linux-gnu').mkdir(parents=True)
            (Path(source) / 'lib32').mkdir(parents=True)
            (Path(source) / 'usr/bin').mkdir(parents=True)
            (Path(source) / 'usr/bin/which').touch()
            (Path(source) / 'bin/which').symlink_to('/usr/bin/which')
            (Path(source) / 'bin/less').touch()
            (Path(source) / 'usr/bin/less').symlink_to('/bin/less')
            (Path(source) / 'bin/more').touch()
            (Path(source) / 'usr/bin/more').symlink_to('../../bin/more')
            (Path(source) / 'usr/bin/env').touch()
            (Path(source) / 'bin/env').symlink_to('../usr/bin/env')
            (Path(source) / 'usr/bin/gcc').symlink_to('gcc-9')
            (Path(source) / 'usr/bin/foo').symlink_to('/bin/foo-1')
            (Path(source) / 'usr/bin/bar').symlink_to('../../bin/bar-2')
            (Path(source) / 'usr/lib/x86_64-linux-gnu').mkdir(parents=True)
            (Path(source) / 'bin/x').symlink_to('/usr/bin/x-1')
            (Path(source) / 'bin/y').symlink_to('../usr/bin/x-2')
            (Path(source) / 'lib/x86_64-linux-gnu/libpng12.so.0').symlink_to(
                'libpng12.so.0.46.0')
            (Path(source) / 'lib/x86_64-linux-gnu/libpng12.so.0.46.0').touch()
            (
                Path(source) / 'usr/lib/x86_64-linux-gnu/libpng12.so.0'
            ).symlink_to('/lib/x86_64-linux-gnu/libpng12.so.0')
            (
                Path(source) / 'usr/lib/x86_64-linux-gnu/libpng12.so'
            ).symlink_to('libpng12.so.0')

            (Path(expected) / 'bin').symlink_to('usr/bin')
            (Path(expected) / 'lib').symlink_to('usr/lib')
            (Path(expected) / 'lib32').symlink_to('usr/lib32')
            (Path(expected) / 'usr/lib').mkdir(parents=True)
            (Path(expected) / 'usr/lib/x86_64-linux-gnu').mkdir(parents=True)
            (Path(expected) / 'usr/lib32').mkdir(parents=True)
            (Path(expected) / 'usr/bin').mkdir(parents=True)
            (Path(expected) / 'usr/bin/which').touch()
            (Path(expected) / 'usr/bin/less').touch()
            (Path(expected) / 'usr/bin/more').touch()
            (Path(expected) / 'usr/bin/env').touch()
            (Path(expected) / 'usr/bin/gcc').symlink_to('gcc-9')
            (Path(expected) / 'usr/bin/foo').symlink_to('/bin/foo-1')
            (Path(expected) / 'usr/bin/bar').symlink_to('../../bin/bar-2')
            (Path(expected) / 'bin/x').symlink_to('/usr/bin/x-1')
            (Path(expected) / 'bin/y').symlink_to('../usr/bin/x-2')
            (
                Path(expected) / 'usr/lib/x86_64-linux-gnu/libpng12.so.0'
            ).symlink_to('libpng12.so.0.46.0')
            (
                Path(expected) / 'usr/lib/x86_64-linux-gnu/libpng12.so.0.46.0'
            ).touch()
            (
                Path(expected) / 'usr/lib/x86_64-linux-gnu/libpng12.so'
            ).symlink_to('libpng12.so.0')

            dest = os.path.join(parent, 'dest')
            subprocess.run(
                [
                    self.cheap_copy,
                    '--usrmerge',
                    source,
                    dest,
                ],
                check=True,
                stdout=2,       # >&2, i.e. stderr
            )
            self.assert_tree_is_same(expected, dest, require_hard_links=False)

    def tearDown(self) -> None:
        super().tearDown()


if __name__ == '__main__':
    assert sys.version_info >= (3, 5), \
        'Python 3.5+ is required (configure with -Dpython=python3.5 ' \
        'if necessary)'

    test_main()

# vi: set sw=4 sts=4 et:
