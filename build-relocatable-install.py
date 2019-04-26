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
import shlex
import shutil
import subprocess

try:
    import typing
except ImportError:
    pass
else:
    typing      # silence pyflakes


class Architecture:
    def __init__(
        self,
        name,           # type: str
        multiarch,      # type: str
        ld_so           # type: str
    ):
        # type: (...) -> None
        self.name = name
        self.multiarch = multiarch
        self.ld_so = ld_so


# Debian architecture => Debian multiarch tuple
ARCHS = [
    Architecture(
        name='amd64',
        multiarch='x86_64-linux-gnu',
        ld_so='/lib64/ld-linux-x86-64.so.2',
    ),
    Architecture(
        name='i386',
        multiarch='i386-linux-gnu',
        ld_so='/lib/ld-linux.so.2',
    ),
]
# package to install from => source package for copyright information
DEPENDENCIES = {
    'libcapsule-tools-relocatable': 'libcapsule',
    'libelf1': 'elfutils',
    'zlib1g': 'zlib',
}
# program to install => binary package
WRAPPED_PROGRAMS = {
    'bwrap': 'bubblewrap',
}
PRIMARY_ARCH_DEPENDENCIES = {
    'bubblewrap': 'bubblewrap',
    'libcap2': 'libcap2',
    'libselinux1': 'libselinux',
    'libpcre3': 'pcre3',
}
DESTDIR = 'relocatable-install'
SCRIPTS = [
    'pressure-vessel-unruntime',
    'pressure-vessel-wrap'
]
TOOLS = [
    'capsule-capture-libs',
    'capsule-symbols',
]


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

    for arch in ARCHS:
        os.makedirs(
            os.path.join(DESTDIR, 'lib', arch.multiarch),
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
        for arch in ARCHS:
            for tool in TOOLS:
                install_exe(
                    os.path.join(
                        args.relocatabledir,
                        '{}-{}'.format(arch.multiarch, tool),
                    ),
                    os.path.join(DESTDIR, 'bin'),
                )

        install(
            '/usr/share/doc/libcapsule-tools-relocatable/copyright',
            os.path.join(DESTDIR, 'sources', 'libcapsule.txt'),
        )
    else:
        v_check_call(['make', '-f', 'Makefile.libcapsule'])

        for arch in ARCHS:
            for tool in TOOLS:
                install_exe(
                    os.path.join('_build', arch.name, 'libcapsule', tool),
                    os.path.join(
                        '_build',
                        '{}-{}'.format(arch.multiarch, tool),
                    ),
                )
                v_check_call([
                    'chrpath', '-r',
                    '${ORIGIN}/../lib/' + arch.multiarch,
                    os.path.join(
                        '_build',
                        '{}-{}'.format(arch.multiarch, tool),
                    ),
                ])
                install_exe(
                    os.path.join(
                        '_build',
                        '{}-{}'.format(arch.multiarch, tool),
                    ),
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

    primary_architecture = subprocess.check_output([
        'dpkg', '--print-architecture',
    ]).decode('utf-8').strip()

    for arch in ARCHS:
        os.makedirs(os.path.join('_build', arch.name, 'lib'), exist_ok=True)

        v_check_call([
            '{}/bin/{}-capsule-capture-libs'.format(DESTDIR, arch.multiarch),
            '--dest=_build/{}/lib'.format(arch.name),
            '--no-glibc',
            'soname:libelf.so.1',
            'soname:libz.so.1',
        ])

        if arch.name == primary_architecture:
            v_check_call([
                '{}/bin/{}-capsule-capture-libs'.format(
                    DESTDIR,
                    arch.multiarch,
                ),
                '--dest=_build/{}/lib'.format(arch.name),
                '--no-glibc',
                'soname:libcap.so.2',
                'soname:libpcre.so.3',
                'soname:libselinux.so.1',
            ])

        for so in glob.glob(
            os.path.join('_build', arch.name, 'lib', '*.so.*'),
        ):
            install(so, os.path.join(DESTDIR, 'lib', arch.multiarch))

    # For bwrap (and possibly other programs in future) we don't have
    # a relocatable version with a RPATH/RUNPATH, so we wrap a script
    # around it instead. The script avoids setting LD_LIBRARY_PATH
    # because that would leak through to the programs invoked by bwrap.
    for exe, package in WRAPPED_PROGRAMS.items():
        path = '/usr/bin/{}'.format(exe)

        if not os.path.exists(path):
            v_check_call([
                'apt-get',
                'download',
                package,
            ])
            v_check_call(
                'dpkg-deb -x {}_*.deb _build'.format(shlex.quote(package)),
                shell=True,
            )
            path = '_build/usr/bin/{}'.format(exe)

        for arch in ARCHS:
            if arch.name != primary_architecture:
                continue

            install_exe(
                path,
                os.path.join(DESTDIR, 'bin', exe + '.bin'),
            )

            with open(
                os.path.join('_build', arch.name, exe),
                'w',
            ) as writer:
                writer.write('#!/bin/sh\n')
                writer.write('set -eu\n')
                writer.write('here="$(dirname "$0")"\n')
                writer.write(
                    'exec {} --library-path "$here"/../lib/{} '
                    '"$here"/{}.bin "$@"\n'.format(
                        shlex.quote(arch.ld_so),
                        shlex.quote(arch.multiarch),
                        shlex.quote(exe),
                    )
                )

            install_exe(
                os.path.join('_build', arch.name, exe),
                os.path.join(DESTDIR, 'bin', exe),
            )

    source_to_download = set()      # type: typing.Set[str]

    for package, source in (
        list(DEPENDENCIES.items()) + list(PRIMARY_ARCH_DEPENDENCIES.items())
    ):
        if args.relocatabledir is None and source == 'libcapsule':
            continue

        if os.path.exists('/usr/share/doc/{}/copyright'.format(package)):
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
        else:
            install(
                '_build/usr/share/doc/{}/copyright'.format(package),
                os.path.join(DESTDIR, 'sources', '{}.txt'.format(source)),
            )
            source_to_download.add(source)

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
