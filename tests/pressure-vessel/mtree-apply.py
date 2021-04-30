#!/usr/bin/env python3
# Copyright 2021 Collabora Ltd.
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


class TestMtreeApply(BaseTest):
    def setUp(self) -> None:
        super().setUp()
        os.environ['G_MESSAGES_DEBUG'] = 'all'
        self.mtree_apply = os.path.join(
            self.G_TEST_BUILDDIR,
            'test-mtree-apply',
        )

    def assert_tree_is_superset(
        self,
        superset,
        subset,
        require_hard_links: bool = True,
        require_permissions: bool = True,
        require_times: bool = True,
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
                self.assertEqual(oct(info.st_mode), oct(info2.st_mode))

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

                    if require_permissions:
                        self.assertEqual(oct(info.st_mode), oct(info2.st_mode))
                    else:
                        self.assertEqual(
                            oct(info.st_mode & ~0o7777),
                            oct(info2.st_mode & ~0o7777),
                        )

                    self.assertEqual(info.st_size, info2.st_size)

                    if require_times:
                        self.assertEqual(
                            int(info.st_mtime),
                            int(info2.st_mtime),
                        )

    def assert_tree_is_same(
        self,
        left,
        right,
        require_hard_links: bool = True,
        require_permissions: bool = True,
        require_times: bool = True,
    ):
        self.assert_tree_is_superset(
            left, right,
            require_hard_links=require_hard_links,
            require_permissions=require_permissions,
            require_times=require_times,
        )
        self.assert_tree_is_superset(
            right,
            left,
            require_hard_links=require_hard_links,
            require_permissions=require_permissions,
            require_times=require_times,
        )

    def test_empty(self) -> None:
        content = b''

        with tempfile.NamedTemporaryFile(
        ) as source, tempfile.TemporaryDirectory(
        ) as expected, tempfile.TemporaryDirectory(
        ) as dest:
            source.write(content)
            source.flush()

            subprocess.run(
                [
                    self.mtree_apply,
                    source.name,
                    dest,
                ],
                check=True,
                stdout=2,
            )
            self.assert_tree_is_same(
                dest,
                expected,
                require_hard_links=False,
                require_permissions=False,
                require_times=False,
            )

    def test_populate(self) -> None:
        content = b'''\
        . type=dir
        ./foo/bar/\302\251 type=dir
        ./sym/link type=link link=/dev/null
        ./create type=file size=0
        ./make-executable type=file mode=755
        ./make-non-executable type=file mode=644 time=1597415889
        '''

        with tempfile.NamedTemporaryFile(
        ) as source, tempfile.TemporaryDirectory(
        ) as expected, tempfile.TemporaryDirectory(
        ) as dest:
            source.write(content)
            source.flush()

            os.umask(0o077)
            bar = (Path(expected) / 'foo' / 'bar')
            bar.mkdir(parents=True)
            non_ascii_filename = str(bar).encode('ascii') + b'/\302\251'
            os.mkdir(non_ascii_filename)
            (Path(expected) / 'sym').mkdir()
            (Path(expected) / 'sym' / 'link').symlink_to('/dev/null')

            for filename in 'make-executable', 'make-non-executable':
                for d in Path(expected), Path(dest):
                    with open(str(d / filename), 'w') as writer:
                        writer.write('#!/bin/sh\n')

            with open(str(Path(expected) / 'create'), 'w') as writer:
                pass

            # Paths that are not explicitly created get their permissions
            # from umask
            (Path(expected)).chmod(0o755)
            os.chmod(non_ascii_filename, 0o755)
            (Path(expected) / 'create').chmod(0o644)
            (Path(expected) / 'make-executable').chmod(0o755)
            (Path(expected) / 'make-non-executable').chmod(0o644)
            os.utime(
                str(Path(expected) / 'make-non-executable'),
                times=(1597415889, 1597415889),
            )

            subprocess.run(
                [
                    self.mtree_apply,
                    source.name,
                    dest,
                ],
                check=True,
                stdout=2,
            )
            subprocess.run(
                [
                    'find',
                    dest,
                    '-ls',
                ],
                check=True,
                stdout=2,
            )
            self.assert_tree_is_same(
                dest,
                expected,
                require_hard_links=False,
                require_permissions=True,
                require_times=False,
            )

    def test_populate_copy(self) -> None:
        content = b'''\
# Content-addressed storage indexed by a truncated sha256
./make-executable type=file mode=755 contents=a8/076d3d28d21e02012b20eaf7dbf754
./make-non-executable type=file mode=644 time=1597415889
'''

        with tempfile.NamedTemporaryFile(
        ) as source, tempfile.TemporaryDirectory(
        ) as reference, tempfile.TemporaryDirectory(
        ) as expected, tempfile.TemporaryDirectory(
        ) as dest:
            source.write(content)
            source.flush()

            os.umask(0o077)

            for filename in 'make-executable', 'make-non-executable':
                with open(str(Path(expected) / filename), 'w') as writer:
                    writer.write('#!/bin/sh\n')

            # Avoid Path.link_to() which wasn't in Python 3.5
            os.link(
                str(Path(expected) / 'make-non-executable'),
                str(Path(reference) / 'make-non-executable'),
            )
            (Path(reference) / 'a8').mkdir()
            os.link(
                str(Path(expected) / 'make-executable'),
                str(
                    Path(reference) / 'a8'
                    / '076d3d28d21e02012b20eaf7dbf754'
                ),
            )

            subprocess.run(
                [
                    self.mtree_apply,
                    source.name,
                    dest,
                    reference,
                ],
                check=True,
                stdout=2,
            )

            subprocess.run(
                [
                    'find',
                    dest,
                    '-ls',
                ],
                check=True,
                stdout=2,
            )
            self.assert_tree_is_same(
                dest,
                expected,
                require_hard_links=True,
                require_permissions=True,
                require_times=True,
            )

            info = (Path(dest) / 'make-executable').stat()
            self.assertEqual(info.st_mode & 0o7777, 0o755)

            info = (Path(dest) / 'make-non-executable').stat()
            self.assertEqual(info.st_mode & 0o7777, 0o644)
            self.assertEqual(info.st_mtime, 1597415889)

    def tearDown(self) -> None:
        super().tearDown()


if __name__ == '__main__':
    assert sys.version_info >= (3, 5), \
        'Python 3.5+ is required (configure with -Dpython=python3.5 ' \
        'if necessary)'

    test_main()

# vi: set sw=4 sts=4 et:
