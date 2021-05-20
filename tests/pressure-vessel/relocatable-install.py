#!/usr/bin/env python3
# Copyright 2018-2019 Collabora Ltd.
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
import os
import subprocess
import sys

try:
    import typing
    typing      # silence pyflakes
except ImportError:
    pass


G_TEST_SRCDIR = os.getenv(
    'G_TEST_SRCDIR',
    os.path.abspath(
        os.path.join(os.path.dirname(__file__), os.pardir),
    ),
)
G_TEST_BUILDDIR = os.getenv('G_TEST_BUILDDIR', os.path.abspath('_build'))


class TapTest:
    def __init__(self):
        # type: () -> None
        self.test_num = 0
        self.failed = False

    def ok(self, desc):
        # type: (str) -> None
        self.test_num += 1
        print('ok %d - %s' % (self.test_num, desc))

    def skip(self, desc):
        # type: (str) -> None
        self.test_num += 1
        print('ok %d # SKIP %s' % (self.test_num, desc))

    def diag(self, text):
        # type: (str) -> None
        print('# %s' % text)

    def not_ok(self, desc):
        # type: (str) -> None
        self.test_num += 1
        print('not ok %d - %s' % (self.test_num, desc))
        self.failed = True

    def done_testing(self):
        # type: () -> None
        print('1..%d' % self.test_num)

        if self.failed:
            sys.exit(1)


EXES = [
    'pressure-vessel-adverb',
    'pressure-vessel-wrap',
]
WRAPPED = [
    'bwrap',
]
HELPERS = [
    'capsule-capture-libs',
]
SCRIPTS = [
    'pressure-vessel-unruntime',
]
LD_SO = [
    ('x86_64-linux-gnu', '/lib64/ld-linux-x86-64.so.2'),
    ('i386-linux-gnu', '/lib/ld-linux.so.2'),
]


def isexec(path):
    # type: (str) -> bool
    try:
        return (os.stat(path).st_mode & 0o111) != 0
    except OSError:
        return False


def check_dependencies(test, relocatable_install, path, is_wrapper=False):
    # type: (TapTest, str, str, bool) -> None

    if is_wrapper:
        argv = [
            'env',
            'RELOCATABLE_INSTALL_WRAPPER=env LD_TRACE_LOADED_OBJECTS=1',
            path,
        ]
    else:
        argv = ['ldd', path]

    subproc = subprocess.Popen(
        argv,
        stdout=subprocess.PIPE,
        universal_newlines=True,
    )
    stdout = subproc.stdout
    assert stdout is not None       # for mypy

    for line in stdout:
        line = line.strip()

        if (
            line.startswith('linux-gate.so.')
            or line.startswith('linux-vdso.so.')
            or line.startswith('/lib/ld-linux.so.2 ')
            or line.startswith('/lib64/ld-linux-x86-64.so.2 ')
        ):
            continue

        if ' => ' not in line:
            test.not_ok('Unknown ldd output {!r}'.format(line))
            continue

        soname, rest = line.split(' => ')
        lib = rest.split()[0]

        test.diag(
            '{} depends on {} found at {!r}'.format(
                path, soname, lib,
            )
        )

        if soname in (
            'libc.so.6',
            'libdl.so.2',
            'libpthread.so.0',
            'libresolv.so.2',
            'librt.so.1',
        ):
            test.ok('{} is part of glibc'.format(soname))
            continue

        if lib.startswith(relocatable_install + '/'):
            test.ok(
                '{} depends on {} in the relocatable install'.format(
                    path, lib
                )
            )
        else:
            test.not_ok(
                '{} depends on {} outside the relocatable install'.format(
                    path, lib
                )
            )


def main():
    # type: () -> None

    parser = argparse.ArgumentParser()
    parser.add_argument('path')
    args = parser.parse_args()

    relocatable_install = os.path.abspath(args.path)

    test = TapTest()

    for exe in EXES + WRAPPED:
        path = os.path.join(relocatable_install, 'bin', exe)

        if subprocess.call([path, '--help'], stdout=2) == 0:
            test.ok('{} --help'.format(path))
        else:
            test.not_ok('{} --help'.format(path))

    for exe in EXES:
        path = os.path.join(relocatable_install, 'bin', exe)
        check_dependencies(test, relocatable_install, path)

    for exe in WRAPPED:
        path = os.path.join(relocatable_install, 'bin', exe)
        check_dependencies(test, relocatable_install, path, is_wrapper=True)

    for basename in HELPERS:
        for multiarch, ld_so in LD_SO:
            exe = '{}-{}'.format(multiarch, basename)
            path = os.path.join(
                relocatable_install,
                'libexec',
                'steam-runtime-tools-0',
                exe,
            )

            if isexec(path):
                test.ok('{} exists and is executable'.format(path))
            else:
                test.not_ok('{} not executable'.format(path))

            if not isexec(ld_so):
                test.skip('{} not found'.format(ld_so))
            elif basename == 'capsule-symbols':
                test.skip('capsule-symbols has no --help yet')
            elif subprocess.call([path, '--help'], stdout=2) == 0:
                test.ok('{} --help'.format(path))
            else:
                test.not_ok('{} --help'.format(path))

            check_dependencies(test, relocatable_install, path)

    for exe in SCRIPTS:
        path = os.path.join(relocatable_install, 'bin', exe)
        if not isexec('/bin/bash'):
            test.skip('bash not found')
        elif subprocess.call([path, '--help'], stdout=2) == 0:
            test.ok('{} --help'.format(path))
        else:
            test.not_ok('{} --help'.format(path))

    test.done_testing()


if __name__ == '__main__':
    assert sys.version_info >= (3, 5), \
        'Python 3.5+ is required (configure with -Dpython=python3.5 ' \
        'if necessary)'

    main()

# vi: set sw=4 sts=4 et:
