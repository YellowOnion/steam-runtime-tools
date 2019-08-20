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
import logging
import os
import re
import shutil
import subprocess
import tempfile

try:
    import typing
except ImportError:
    pass
else:
    typing      # silence pyflakes

try:
    from shlex import quote
except ImportError:
    from pipes import quote     # noqa


logger = logging.getLogger('pressure-vessel-build-relocatable-install')


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
    'libblkid1': 'util-linux',
    'libcap2': 'libcap2',
    'libffi6': 'libffi',
    'libglib2.0-0': 'glib2.0',
    'libmount1': 'util-linux',
    'libpcre3': 'pcre3',
    'libxau6': 'libxau',
    'libselinux1': 'libselinux',
}
SCRIPTS = [
    'pressure-vessel-test-ui',
    'pressure-vessel-unruntime',
    'pressure-vessel-unruntime-test-ui',
]
EXECUTABLES = [
    'pressure-vessel-wrap'
]
LIBCAPSULE_TOOLS = [
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
    parser.add_argument(
        '--destdir', default=os.getenv('DESTDIR'),
        help=(
            'Assume pressure-vessel is installed in DESTDIR instead of '
            'in the root directory'
        ),
    )
    parser.add_argument(
        '--prefix', default=None,
        help=(
            'Assume pressure-vessel is installed in PREFIX instead of '
            'in /usr/lib/pressure-vessel-relocatable'
        ),
    )
    parser.add_argument(
        '--srcdir', default=None,
        help=(
            'Assume pressure-vessel source code is installed in SRCDIR '
            '(relative to PREFIX) instead of finding it automatically'
        ),
    )
    parser.add_argument(
        '--output', '-o', default=None,
        help='Write an unpacked binary tree to OUTPUT',
    )
    parser.add_argument(
        '--archive', default=None,
        help='Write packed source and binary tarballs into ARCHIVE directory',
    )
    parser.add_argument(
        '--apt-get-source', action='store_true',
        help=(
            "Use 'apt-get source pressure-vessel=VERSION' to download "
            "pressure-vessel's own source code"
        ),
    )
    parser.add_argument(
        '--check-source-directory', default=None, metavar='DIR',
        help=(
            'Instead of building a binary + source release tarball, check '
            'that all required source code is available in DIR'
        ),
    )
    parser.add_argument(
        '--allow-missing-sources', action='store_true',
        help='Missing source code is only a warning [default: error]',
    )
    parser.add_argument(
        '--set-version', dest='version', default=None,
        help='Assume that pressure-vessel is version VERSION',
    )
    parser.add_argument(
        '--archive-versions', action='store_true', default=True,
        help='Embed the version of pressure-vessel in the tarballs [default]',
    )
    parser.add_argument(
        '--no-archive-versions', dest='archive_versions',
        action='store_false', default=True,
        help='Do not embed the version of pressure-vessel in the tarballs',
    )
    args = parser.parse_args()

    if args.srcdir is None:
        args.srcdir = os.path.dirname(__file__)

    if args.prefix is None:
        args.prefix = '/usr/lib/pressure-vessel/relocatable'

    if not os.path.isabs(args.srcdir):
        args.srcdir = os.path.join(args.prefix, args.srcdir)

    if args.destdir:
        args.prefix = args.destdir + args.prefix
        args.srcdir = args.destdir + args.srcdir

    if args.archive is None and args.output is None:
        parser.error('Either --archive or --output is required')

    if args.version is None:
        with open(os.path.join(args.srcdir, '.tarball-version')) as reader:
            args.version = reader.read().strip()

    with tempfile.TemporaryDirectory(prefix='pressure-vessel-') as tmpdir:
        if args.output is None:
            installation = os.path.join(tmpdir, 'installation')
        else:
            installation = args.output

        if os.path.exists(installation):
            raise RuntimeError('--output directory must not already exist')

        os.makedirs(os.path.join(installation, 'bin'), exist_ok=True)
        os.makedirs(os.path.join(installation, 'metadata'), exist_ok=True)

        for arch in ARCHS:
            os.makedirs(
                os.path.join(installation, 'lib', arch.multiarch),
                exist_ok=True,
            )

        for script in SCRIPTS:
            install_exe(
                os.path.join(args.prefix, 'bin', script),
                os.path.join(installation, 'bin'),
            )

        for exe in EXECUTABLES:
            install_exe(
                os.path.join(args.prefix, 'bin', exe),
                os.path.join(installation, 'bin'),
            )

        install(
            os.path.join(args.srcdir, 'THIRD-PARTY.md'),
            os.path.join(installation, 'metadata', 'README.txt'),
            0o644,
        )

        for arch in ARCHS:
            path = '/usr/lib/libcapsule/relocatable/{}-{}'.format(
                arch.multiarch,
                LIBCAPSULE_TOOLS[0],
            )

            if not os.path.exists(path):
                package = 'libcapsule-tools-relocatable'
                v_check_call([
                    'apt-get',
                    'download',
                    package + ':' + arch.name,
                ], cwd=tmpdir)
                v_check_call(
                    'dpkg-deb -X {}_*_{}.deb build-relocatable'.format(
                        quote(package),
                        quote(arch.name),
                    ),
                    cwd=tmpdir,
                    shell=True,
                )
                path = '{}/build-relocatable/{}'.format(tmpdir, path)

            for tool in LIBCAPSULE_TOOLS:
                install_exe(
                    os.path.join(
                        os.path.dirname(path),
                        '{}-{}'.format(arch.multiarch, tool),
                    ),
                    os.path.join(installation, 'bin'),
                )

        primary_architecture = subprocess.check_output([
            'dpkg', '--print-architecture',
        ]).decode('utf-8').strip()

        for arch in ARCHS:
            os.makedirs(
                os.path.join(tmpdir, 'build-relocatable', arch.name, 'lib'),
                exist_ok=True,
            )

            v_check_call([
                '{}/bin/{}-capsule-capture-libs'.format(
                    installation,
                    arch.multiarch,
                ),
                '--dest={}/build-relocatable/{}/lib'.format(
                    tmpdir,
                    arch.name,
                ),
                '--no-glibc',
                'soname:libelf.so.1',
                'soname:libz.so.1',
            ])

            if arch.name == primary_architecture:
                v_check_call([
                    '{}/bin/{}-capsule-capture-libs'.format(
                        installation,
                        arch.multiarch,
                    ),
                    '--dest={}/build-relocatable/{}/lib'.format(
                        tmpdir,
                        arch.name,
                    ),
                    '--no-glibc',
                    'soname:libXau.so.6',
                    'soname:libcap.so.2',
                    'soname:libgio-2.0.so.0',
                    'soname:libpcre.so.3',
                    'soname:libselinux.so.1',
                ])

            for so in glob.glob(
                os.path.join(
                    tmpdir,
                    'build-relocatable',
                    arch.name,
                    'lib',
                    '*.so.*',
                ),
            ):
                install(so, os.path.join(installation, 'lib', arch.multiarch))

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
                ], cwd=tmpdir)
                v_check_call(
                    'dpkg-deb -x {}_*.deb build-relocatable'.format(
                        quote(package),
                    ),
                    cwd=tmpdir,
                    shell=True,
                )
                path = '{}/build-relocatable/usr/bin/{}'.format(tmpdir, exe)

            for arch in ARCHS:
                if arch.name != primary_architecture:
                    continue

                install_exe(
                    path,
                    os.path.join(installation, 'bin', exe + '.bin'),
                )

                with open(
                    os.path.join(tmpdir, 'build-relocatable', arch.name, exe),
                    'w',
                ) as writer:
                    writer.write('#!/bin/sh\n')
                    writer.write('set -eu\n')
                    writer.write('here="$(dirname "$0")"\n')
                    writer.write(
                        'exec ${{RELOCATABLE_INSTALL_WRAPPER-}} {} '
                        '--library-path "$here"/../lib/{} '
                        '"$here"/{}.bin "$@"\n'.format(
                            quote(arch.ld_so),
                            quote(arch.multiarch),
                            quote(exe),
                        )
                    )

                install_exe(
                    os.path.join(tmpdir, 'build-relocatable', arch.name, exe),
                    os.path.join(installation, 'bin', exe),
                )

        source_to_download = set()      # type: typing.Set[str]
        installed_binaries = set()      # type: typing.Set[str]

        get_source = (
            list(DEPENDENCIES.items())
            + list(PRIMARY_ARCH_DEPENDENCIES.items())
        )

        if args.apt_get_source or args.check_source_directory is not None:
            get_source.append(
                ('pressure-vessel-relocatable', 'pressure-vessel'),
            )

        for package, source in get_source:
            if os.path.exists('/usr/share/doc/{}/copyright'.format(package)):
                installed_binaries.add(package)

                install(
                    '/usr/share/doc/{}/copyright'.format(package),
                    os.path.join(
                        installation,
                        'metadata',
                        '{}.txt'.format(source),
                    ),
                )

                for expr in set(
                    v_check_output([
                        'dpkg-query',
                        '-W',
                        '-f', '${source:Package}=${source:Version}\n',
                        package,
                    ], universal_newlines=True).splitlines()
                ):
                    source_to_download.add(
                        re.sub(r'[+]srt[0-9a-z.]+$', '', expr))
            else:
                install(
                    '{}/build-relocatable/usr/share/doc/{}/copyright'.format(
                        tmpdir,
                        package,
                    ),
                    os.path.join(
                        installation,
                        'metadata',
                        '{}.txt'.format(source),
                    ),
                )
                source_to_download.add(source)

        with open(
            os.path.join(installation, 'metadata', 'packages.txt'), 'w'
        ) as writer:
            writer.write(
                '#Package[:Architecture]\t#Version\t#Source\t#Installed-Size\n'
            )
            v_check_call([
                'dpkg-query',
                '-W',
                '-f',
                (r'${binary:Package}\t${Version}\t'
                 r'${Source}\t${Installed-Size}\n'),
            ] + sorted(installed_binaries), stdout=writer)

        with open(
            os.path.join(installation, 'metadata', 'VERSION.txt'),
            'w',
        ) as writer:
            writer.write('{}\n'.format(args.version))

        shutil.copytree(
            os.path.join(installation, 'metadata'),
            os.path.join(installation, 'sources'),
        )

        with open(
            os.path.join(installation, 'sources', 'sources.txt'), 'w'
        ) as writer:
            writer.write(
                '#Source\t#Version\n'
            )
            for source in sorted(source_to_download):
                writer.write(source.replace('=', '\t') + '\n')

        if args.check_source_directory is None:
            try:
                v_check_call(
                    [
                        'apt-get',
                        '--download-only',
                        '--only-source',
                        'source',
                    ] + list(source_to_download),
                    cwd=os.path.join(installation, 'sources'),
                )
            except subprocess.CalledProcessError:
                if args.allow_missing_sources:
                    logger.warning(
                        'Some source packages could not be downloaded')
                    with open(
                        os.path.join(installation, 'sources', 'INCOMPLETE'),
                        'w',
                    ) as writer:
                        # nothing to write, just create the file
                        pass
                else:
                    raise

            if not args.apt_get_source:
                os.makedirs(
                    os.path.join(installation, 'sources', 'pressure-vessel'),
                    exist_ok=True,
                )

                tar = subprocess.Popen([
                    'tar',
                    '-C', args.srcdir,
                    '--exclude=.*.sw?',
                    '--exclude=./.git',
                    '--exclude=./.mypy_cache',
                    '--exclude=./_build',
                    '--exclude=./debian',
                    '--exclude=./relocatable-install',
                    '-cf-',
                    '.',
                ], stdout=subprocess.PIPE)
                subprocess.check_call([
                    'tar',
                    '-C', os.path.join(
                        installation,
                        'sources',
                        'pressure-vessel',
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
                        installation,
                        'sources',
                        'pressure-vessel',
                        '.tarball-version',
                    ),
                    'w',
                ) as writer:
                    writer.write('{}\n'.format(args.version))

        else:       # args.check_source_directory is not None
            for source in sorted(source_to_download):
                package, version = source.split('=')

                if ':' in version:
                    version = version.split(':', 1)[1]

                filename = os.path.join(
                    args.check_source_directory,
                    '{}_{}.dsc'.format(package, version),
                )
                if not os.path.exists(filename):
                    if args.allow_missing_sources:
                        logger.warning(
                            'Source code not found in %s', filename)
                    else:
                        raise RuntimeError(
                            'Source code not found in %s', filename)

        if args.archive:
            if args.archive_versions:
                tail = '-' + args.version
            else:
                tail = ''

            bin_tar = os.path.join(
                args.archive,
                'pressure-vessel{}-bin.tar.gz'.format(tail),
            )

            if args.check_source_directory is None:
                src_tar = os.path.join(
                    args.archive,
                    'pressure-vessel{}-bin+src.tar.gz'.format(tail),
                )
                subprocess.check_call([
                    'tar',
                    (r'--transform='
                     r's,^\(\.\(/\|$\)\)\?,pressure-vessel{}/,').format(
                        tail,
                    ),
                    # metadata/ is all duplicated in sources/
                    '--exclude=metadata',
                    '-zcvf', src_tar + '.tmp',
                    '-C', installation,
                    '.',
                ])
            else:
                src_tar = ''

            subprocess.check_call([
                'tar',
                (r'--transform='
                 r's,^\(\.\(/\|$\)\)\?,pressure-vessel{}/,').format(
                    tail,
                ),
                '--exclude=sources',
                '-zcvf', bin_tar + '.tmp',
                '-C', installation,
                '.',
            ])
            os.rename(bin_tar + '.tmp', bin_tar)
            print('Generated {}'.format(os.path.abspath(bin_tar)))

            if src_tar:
                os.rename(src_tar + '.tmp', src_tar)
                print('Generated {}'.format(os.path.abspath(src_tar)))


if __name__ == '__main__':
    logging.basicConfig()
    main()

# vim:set sw=4 sts=4 et:
