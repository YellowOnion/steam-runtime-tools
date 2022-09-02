#!/usr/bin/env python3
# Copyright 2022 Collabora Ltd.
#
# SPDX-License-Identifier: MIT

# This doesn't really test anything, it's only here to check our
# infrastructure for running Python scripts as build-time and as-installed
# tests.

import sys

from testutils import BaseTest, test_main


class TestUtils(BaseTest):
    def setUp(self) -> None:
        super().setUp()

    def test_suitable_python(self) -> None:
        self.assertGreaterEqual(
            sys.version_info,
            (3, 4),
            ('Python 3.4+ is required (configure with -Dpython=python3.5 '
             'if necessary)'),
        )

    def tearDown(self) -> None:
        super().tearDown()


if __name__ == '__main__':
    test_main()
