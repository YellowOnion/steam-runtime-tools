#!/usr/bin/env python3

# Copyright © 2017-2019 Collabora Ltd.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import argparse
import logging
import os
import subprocess
import sys

try:
    import typing
except ImportError:
    pass
else:
    typing      # silence pyflakes


logger = logging.getLogger('run-in-sysroot')


def main():
    # type: () -> int

    parser = argparse.ArgumentParser()
    parser.add_argument('--srcdir', default='.')
    parser.add_argument('--builddir', default='_build')
    parser.add_argument('--sysroot', default=None)
    parser.add_argument('--tarball', default=None)
    parser.add_argument('command')
    parser.add_argument('args', nargs=argparse.REMAINDER)
    args = parser.parse_args()

    abs_srcdir = os.path.abspath(args.srcdir)
    abs_builddir = os.path.abspath(args.builddir)

    if args.sysroot is None:
        args.sysroot = os.path.join(abs_builddir, 'sysroot')

    if args.tarball is None:
        args.tarball = os.path.join(abs_builddir, 'sysroot.tar.gz')

    abs_sysroot = os.path.abspath(args.sysroot)

    if not os.path.exists(os.path.join(abs_sysroot, 'usr', 'bin', 'env')):
        if not os.path.exists(args.tarball):
            logger.error(
                'A sysroot tarball (--tarball or %s) is required)',
                args.tarball,
            )
            return 1

        os.makedirs(abs_sysroot, exist_ok=True)
        subprocess.check_call([
            'tar',
            '-xvf',
            args.tarball,
            '--exclude=./dev/*',
            '--exclude=dev/*',
            '-C', abs_sysroot,
        ])

    os.makedirs(os.path.join(abs_sysroot, 'tmp'), exist_ok=True)
    os.makedirs(os.path.join(abs_sysroot, 'home'), exist_ok=True)
    os.makedirs(
        os.path.join(
            abs_sysroot,
            'var', 'cache', 'apt', 'archives', 'partial',
        ), exist_ok=True,
    )
    os.makedirs(
        os.path.join(abs_sysroot, './' + abs_srcdir), exist_ok=True)
    os.makedirs(
        os.path.join(abs_sysroot, './' + abs_builddir), exist_ok=True)

    os.execvp(
        'bwrap',
        [
            'bwrap',
            '--ro-bind', abs_sysroot, '/',
            '--dev-bind', '/dev', '/dev',
            '--ro-bind', '/etc/resolv.conf', '/etc/resolv.conf',
            '--proc', '/proc',
            '--tmpfs', '/tmp',
            '--tmpfs', '/var/tmp',
            '--tmpfs', '/home',
            '--bind', abs_srcdir, abs_srcdir,
            '--bind', abs_builddir, abs_builddir,
            '--chdir', os.getcwd(),
            '--setenv', 'LC_ALL', 'C.UTF-8',
            args.command,
        ] + args.args,
    )


if __name__ == '__main__':
    sys.exit(main())

# vim:set sw=4 sts=4 et:
