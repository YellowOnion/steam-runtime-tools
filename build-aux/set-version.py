#!/usr/bin/python3
# Copyright © 2022 Collabora Ltd.
# SPDX-License-Identifier: MIT

import os
import os.path
import sys

for d in ('.', 'subprojects/container-runtime'):
    with open(
        os.path.join(os.environ['MESON_DIST_ROOT'], d, '.tarball-version'),
        'w'
    ) as writer:
        writer.write(sys.argv[1] + '\n')
