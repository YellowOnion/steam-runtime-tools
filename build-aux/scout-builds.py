#!/usr/bin/python3.5

# Convenience script to build a standalone copy of pressure-vessel.
# Must be run in a scout environment.

# Copyright © 2017-2021 Collabora Ltd.
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
import errno
import logging
import os
import shutil
import subprocess
import sys

try:
    import typing
except ImportError:
    pass
else:
    typing      # silence pyflakes


logger = logging.getLogger('scout-builds')


def setup(args):
    # type: (typing.Any) -> None

    common_options = [
        '-Dgtk_doc=disabled',
        '-Dlibcurl_compat=true',
        '-Dman=disabled',
        '-Dsrcdir=src',
        '--optimization=g',
        '--prefix=/usr',
        '--werror',
    ]

    subprocess.check_call(
        [
            'meson',
        ] + common_options + [
            '--native-file=build-aux/meson/scout.txt',
            os.path.join(args.abs_builddir_parent, 'scout-x86_64')
        ] + list(args.args),
        cwd=args.abs_srcdir,
    )
    subprocess.check_call(
        [
            'meson',
        ] + common_options + [
            '-Dbin=false',
            '-Dmultiarch_tuple=i386-linux-gnu',
            '-Dpressure_vessel=false',
            '--cross-file=build-aux/meson/scout-i386.txt',
            '--libdir=lib/i386-linux-gnu',
            os.path.join(args.abs_builddir_parent, 'scout-i386')
        ] + list(args.args),
        cwd=args.abs_srcdir,
    )


def clean(args):
    # type: (typing.Any) -> None

    for arch in ('i386', 'x86_64'):
        subprocess.check_call(
            [
                'ninja',
                '-C', os.path.join(args.abs_builddir_parent, 'scout-' + arch),
                'clean',
            ] + list(args.args),
            cwd=args.abs_srcdir,
        )


def build(args):
    # type: (typing.Any) -> None

    for arch in ('i386', 'x86_64'):
        subprocess.check_call(
            [
                'ninja',
                '-C', os.path.join(args.abs_builddir_parent, 'scout-' + arch),
            ] + list(args.args),
            cwd=args.abs_srcdir,
        )


def test(args):
    # type: (typing.Any) -> None

    for arch in ('i386', 'x86_64'):
        subprocess.check_call(
            [
                'meson',
                'test',
                '-v',
                '-C', os.path.join(args.abs_builddir_parent, 'scout-' + arch),
            ] + list(args.args),
            cwd=args.abs_srcdir,
        )


def install(args):
    # type: (typing.Any) -> None

    destdir = os.path.join(args.abs_builddir_parent, 'scout-DESTDIR')

    try:
        shutil.rmtree(destdir)
    except OSError as e:
        if e.errno != errno.ENOENT:
            raise

    for arch in ('i386', 'x86_64'):
        subprocess.check_call(
            [
                'env',
                'DESTDIR=' + destdir,
                'ninja',
                '-C', os.path.join(args.abs_builddir_parent, 'scout-' + arch),
                'install',
            ] + list(args.args),
            cwd=args.abs_srcdir,
        )

    relocatable = os.path.join(args.abs_builddir_parent, 'scout-relocatable')

    try:
        shutil.rmtree(relocatable)
    except OSError as e:
        if e.errno != errno.ENOENT:
            raise

    script = os.path.join(
        destdir, 'usr', 'lib', 'pressure-vessel', 'relocatable', 'bin',
        'pressure-vessel-build-relocatable-install',
    )

    subprocess.check_call([
        'env',
        'DESTDIR=' + destdir,
        script,
        '--archive', args.abs_builddir_parent,
        '--no-archive-versions',
        '--allow-missing-sources',
        '--output', relocatable,
    ])

    subprocess.check_call([
        'env',
        'DESTDIR=' + destdir,
        'python3.5',
        os.path.join(
            args.abs_srcdir, 'tests', 'pressure-vessel',
            'relocatable-install.py',
        ),
        relocatable,
    ])


def main():
    # type: () -> int

    parser = argparse.ArgumentParser()
    parser.add_argument('--srcdir', default='.')
    parser.add_argument('--builddir-parent', default='_build')
    parser.add_argument(
        'command',
        choices=('setup', 'clean', 'build', 'test', 'install', 'all'),
    )
    parser.add_argument('args', nargs=argparse.REMAINDER)
    args = parser.parse_args()

    args.abs_srcdir = os.path.abspath(args.srcdir)
    args.abs_builddir_parent = os.path.abspath(args.builddir_parent)

    if args.command == 'setup':
        setup(args)
    elif args.command == 'clean':
        clean(args)
    elif args.command == 'build':
        build(args)
    elif args.command == 'test':
        test(args)
    elif args.command == 'install':
        install(args)
    elif args.command == 'all':
        test(args)
        install(args)
    else:
        raise AssertionError

    return 0


if __name__ == '__main__':
    sys.exit(main())

# vim:set sw=4 sts=4 et:
