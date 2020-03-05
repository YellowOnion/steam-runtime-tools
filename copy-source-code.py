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
import os
import subprocess

try:
    import typing
except ImportError:
    pass
else:
    typing      # silence pyflakes


def v_check_call(command, **kwargs):
    print('# {}'.format(command))
    subprocess.check_call(command, **kwargs)


def v_check_output(command, **kwargs):
    print('# {}'.format(command))
    return subprocess.check_output(command, **kwargs)


def main():
    # type: () -> None

    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--destdir', default=os.getenv('DESTDIR', ''))
    parser.add_argument(
        '--srcdir', default=os.getenv('MESON_SOURCE_ROOT', '.'))
    parser.add_argument('--set-version', default='0.0.0', dest='version')
    parser.add_argument('--prefix', default='/usr')
    parser.add_argument('output')
    args = parser.parse_args()

    args.srcdir = os.path.abspath(args.srcdir)
    args.prefix = os.path.abspath(args.prefix)

    if not os.path.isabs(args.output):
        args.output = os.path.join(args.prefix, args.output)

    if args.destdir:
        args.destdir = os.path.abspath(args.destdir)
        args.output = args.destdir + args.output

    os.makedirs(os.path.join(args.output), exist_ok=True)

    tar = subprocess.Popen([
        'tar',
        '-C', args.srcdir,
        '--exclude=.*.sw?',
        '--exclude=./.git',
        '--exclude=./.mypy_cache',
        '--exclude=./_build',
        '--exclude=./debian',
        '--exclude=./relocatable-install',
        '--exclude=./subprojects/steam-runtime-tools',
        '-cf-',
        '.',
    ], stdout=subprocess.PIPE)
    subprocess.check_call([
        'tar',
        '-C', os.path.join(
            args.output,
        ),
        '-xvf-',
    ], stdin=tar.stdout)

    if tar.wait() != 0:
        raise subprocess.CalledProcessError(
            returncode=tar.returncode,
            cmd=tar.args,
        )

    with open(
        os.path.join(
            args.output,
            '.tarball-version',
        ),
        'w',
    ) as writer:
        writer.write('{}\n'.format(args.version))


if __name__ == '__main__':
    main()

# vim:set sw=4 sts=4 et:
