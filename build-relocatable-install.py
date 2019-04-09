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
import glob
import os
import re
import shutil
import subprocess

try:
    import typing
except ImportError:
    pass
else:
    typing      # silence pyflakes


# Debian architecture => Debian multiarch tuple
ARCHS = {
    'amd64': 'x86_64-linux-gnu',
    'i386': 'i386-linux-gnu',
}
# package to install from => source package for copyright information
DEPENDENCIES = {
    'libcapsule-tools-relocatable': 'libcapsule',
    'libelf1': 'elfutils',
    'zlib1g': 'zlib',
}
DESTDIR = 'relocatable-install'
SCRIPTS = ('pressure-vessel-unruntime', 'pressure-vessel-wrap')
TOOLS = ('capsule-capture-libs', 'capsule-symbols')


def install(src, dst, mode=0o644):
    # type: (str, str, int) -> None
    shutil.copy(src, dst)

    if os.path.isdir(dst):
        dst = os.path.join(dst, os.path.basename(src))

    os.chmod(dst, mode)


def install_exe(src, dst, mode=0o755):
    # type: (str, str, int) -> None
    install(src, dst, mode)


def v_check_call(command, **kwargs):
    print('# {}'.format(command))
    subprocess.check_call(command, **kwargs)


def v_check_output(command, **kwargs):
    print('# {}'.format(command))
    return subprocess.check_output(command, **kwargs)


def main():
    # type: () -> None

    parser = argparse.ArgumentParser()
    parser.add_argument('relocatabledir', default=None, nargs='?')
    args = parser.parse_args()

    if os.path.exists(DESTDIR):
        shutil.rmtree(DESTDIR)

    os.makedirs(os.path.join(DESTDIR, 'bin'), exist_ok=True)
    os.makedirs(os.path.join(DESTDIR, 'sources'), exist_ok=True)

    for ma in ARCHS.values():
        os.makedirs(
            os.path.join(DESTDIR, 'lib', ma),
            exist_ok=True,
        )

    for script in SCRIPTS:
        install_exe(script, os.path.join(DESTDIR, 'bin'))

    install(
        'THIRD-PARTY.md',
        os.path.join(DESTDIR, 'sources', 'README.txt'),
        0o644,
    )

    if args.relocatabledir is not None:
        for arch, ma in ARCHS.items():
            for tool in TOOLS:
                install_exe(
                    os.path.join(
                        args.relocatabledir,
                        '{}-{}'.format(ma, tool),
                    ),
                    os.path.join(DESTDIR, 'bin'),
                )

        install(
            '/usr/share/doc/libcapsule-tools-relocatable/copyright',
            os.path.join(DESTDIR, 'sources', 'libcapsule.txt'),
        )
    else:
        v_check_call(['make', '-f', 'Makefile.libcapsule'])

        for arch, ma in ARCHS.items():
            for tool in TOOLS:
                install_exe(
                    os.path.join('_build', arch, 'libcapsule', tool),
                    os.path.join('_build', '{}-{}'.format(ma, tool)),
                )
                v_check_call([
                    'chrpath', '-r',
                    '${ORIGIN}/../lib/' + ma,
                    os.path.join('_build', '{}-{}'.format(ma, tool)),
                ])
                install_exe(
                    os.path.join('_build', '{}-{}'.format(ma, tool)),
                    os.path.join(DESTDIR, 'bin'),
                )

            install(
                os.path.join('libcapsule', 'debian', 'copyright'),
                os.path.join(DESTDIR, 'sources', 'libcapsule.txt'),
            )

            for dsc in glob.glob('libcapsule*.dsc'):
                v_check_call([
                    'dcmd', 'install', '-m644', dsc,
                    os.path.join(DESTDIR, 'sources'),
                ])

    for arch, ma in ARCHS.items():
        os.makedirs(os.path.join('_build', arch, 'lib'), exist_ok=True)
        v_check_call([
            '{}/bin/{}-capsule-capture-libs'.format(DESTDIR, ma),
            '--dest=_build/{}/lib'.format(arch),
            '--no-glibc',
            'soname:libelf.so.1',
            'soname:libz.so.1',
        ])

        for so in glob.glob(
            os.path.join('_build', arch, 'lib', '*.so.*'),
        ):
            install(so, os.path.join(DESTDIR, 'lib', ma))

    source_to_download = set()      # type: typing.Set[str]

    for package, source in DEPENDENCIES.items():
        if args.relocatabledir is None and source == 'libcapsule':
            continue

        install(
            '/usr/share/doc/{}/copyright'.format(package),
            os.path.join(DESTDIR, 'sources', '{}.txt'.format(source)),
        )
        install(
            '/usr/share/doc/{}/copyright'.format(package),
            os.path.join(DESTDIR, 'sources', '{}.txt'.format(source)),
        )

        for expr in set(
            v_check_output([
                'dpkg-query',
                '-W',
                '-f', '${source:Package}=${source:Version}\n',
                package,
            ], universal_newlines=True).splitlines()
        ):
            source_to_download.add(re.sub(r'[+]srt[0-9a-z.]+$', '', expr))

    v_check_call(
        [
            'apt-get',
            '--download-only',
            '--only-source',
            'source',
        ] + list(source_to_download),
        cwd=os.path.join(DESTDIR, 'sources'),
    )


if __name__ == '__main__':
    main()

# vim:set sw=4 sts=4 et:
