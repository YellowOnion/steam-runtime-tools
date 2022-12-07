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
import sys
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
    ):
        # type: (...) -> None
        self.name = name
        self.multiarch = multiarch


# Debian architecture => Debian multiarch tuple
X86_ARCHS = [
    Architecture(
        name='amd64',
        multiarch='x86_64-linux-gnu',
    ),
    Architecture(
        name='i386',
        multiarch='i386-linux-gnu',
    ),
]
# package to install from => source package for copyright information
DEPENDENCIES = {
    'libelf1': 'elfutils',
    'libwaffle-1-0': 'waffle',
    'zlib1g': 'zlib',
}
# same as DEPENDENCIES
PRIMARY_ARCH_DEPENDENCIES = {
    'libblkid1': 'util-linux',
    'libcap2': 'libcap2',
    'libffi6': 'libffi',
    'libglib2.0-0': 'glib2.0',
    'libjson-glib-1.0-0': 'json-glib',
    'libmount1': 'util-linux',
    'libpcre3': 'pcre3',
    'libselinux1': 'libselinux',
    'libxau6': 'libxau',
}
# Packages where different binary packages can have different copyright
# files
DIFFERENT_COPYRIGHT_FILES = [
    'util-linux',
]
SCRIPTS = [
    'pressure-vessel-locale-gen',
    'pressure-vessel-test-ui',
    'pressure-vessel-unruntime',
    'steam-runtime-launch-options',
]
EXECUTABLES = [
    'pressure-vessel-adverb',
    'pressure-vessel-try-setlocale',
    'pressure-vessel-wrap',
    'pv-bwrap',
    'steam-runtime-launch-client',
    'steam-runtime-launcher-interface-0',
    'steam-runtime-launcher-service',
    'steam-runtime-system-info',
]


def install(src, dst, mode=0o644):
    # type: (str, str, int) -> None

    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy(src, dst)

    if os.path.isdir(dst):
        dst = os.path.join(dst, os.path.basename(src))

    os.chmod(dst, mode)


def install_exe(src, dst, mode=0o755):
    # type: (str, str, int) -> None
    install(src, dst, mode)


def v_call(command, **kwargs):
    print('# {}'.format(command))
    return subprocess.call(command, **kwargs)


def v_check_call(command, **kwargs):
    print('# {}'.format(command))
    subprocess.check_call(command, **kwargs)


def v_check_output(command, **kwargs):
    print('# {}'.format(command))
    return subprocess.check_output(command, **kwargs)


def main():
    # type: () -> None

    architectures = []           # type: typing.List[Architecture]

    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--architecture-name', default=None,
        help=(
            'Debian dpkg architecture to use. Typical values are "amd64", '
            '"i386", "arm64" etc. [default: "amd64" and "i386"]'
        ),
    )
    parser.add_argument(
        '--architecture-multiarch', default=None,
        help=(
            'Debian multiarch tuple to use. Typical values are '
            '"x86_64-linux-gnu", "i386-linux-gnu", "aarch64-linux-gnu", etc.'
            '[default: "x86_64-linux-gnu" and "i386-linux-gnu"]'
        ),
    )
    parser.add_argument(
        '--destdir', default=os.getenv('DESTDIR'),
        help=(
            'Assume steam-runtime-tools is installed in DESTDIR instead of '
            'in the root directory'
        ),
    )
    parser.add_argument(
        '--prefix', default=None,
        help=(
            'Assume steam-runtime-tools is installed in PREFIX instead of '
            'in /usr'
        ),
    )
    parser.add_argument(
        '--pressure-vessel-dir', default='lib/pressure-vessel/relocatable',
        dest='pv_dir',
        metavar='PV_DIR',
        help=(
            'Assume pressure-vessel is installed in PREFIX/PV_DIR '
            'instead of in PREFIX/lib/pressure-vessel/relocatable'
        ),
    )
    parser.add_argument(
        '--srcdir', default=None,
        help=(
            'Assume steam-runtime-tools source code is installed in SRCDIR '
            '(relative to PREFIX/PV_DIR) instead of finding it automatically'
        ),
    )
    parser.add_argument(
        '--cache', default='', metavar='DIR',
        help='Cache downloaded source code in DIR',
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
        help='Assume that steam-runtime-tools is version VERSION',
    )
    parser.add_argument(
        '--archive-versions', action='store_true', default=True,
        help=(
            'Embed the version of steam-runtime-tools in the tarballs '
            '[default]'
        ),
    )
    parser.add_argument(
        '--no-archive-versions', dest='archive_versions',
        action='store_false', default=True,
        help='Do not embed the version of steam-runtime-tools in the tarballs',
    )
    args = parser.parse_args()

    if args.srcdir is None:
        args.srcdir = os.path.dirname(os.path.dirname(__file__))

    if args.prefix is None:
        args.prefix = '/usr'

    if args.pv_dir is None:
        args.pv_dir = 'lib/pressure-vessel/relocatable'

    args.pv_dir = args.prefix + '/' + args.pv_dir

    if not os.path.isabs(args.srcdir):
        args.srcdir = os.path.join(args.prefix, args.pv_dir, args.srcdir)

    if args.destdir:
        args.prefix = args.destdir + args.prefix
        args.pv_dir = args.destdir + args.pv_dir

        if os.path.exists(args.destdir + args.srcdir):
            args.srcdir = args.destdir + args.srcdir

    if args.archive is None and args.output is None:
        parser.error('Either --archive or --output is required')

    if args.architecture_name and args.architecture_multiarch is None:
        parser.error('When using --architecture-name, also '
                     '--architecture-multiarch is required')

    if args.architecture_multiarch and args.architecture_name is None:
        parser.error('When using --architecture-multiarch, also '
                     '--architecture-name is required')

    if args.architecture_name:
        architectures.append(
            Architecture(
                name=args.architecture_name,
                multiarch=args.architecture_multiarch,
            ),
        )
    else:
        architectures += X86_ARCHS

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
        os.makedirs(os.path.join(installation, 'libexec'), exist_ok=True)
        os.makedirs(os.path.join(installation, 'metadata'), exist_ok=True)

        for arch in architectures:
            os.makedirs(
                os.path.join(installation, 'lib', arch.multiarch),
                exist_ok=True,
            )

        for script in SCRIPTS:
            path = os.path.join(args.pv_dir, 'bin', script)

            if not os.path.exists(path):
                path = os.path.join(args.prefix, 'bin', script)

            install_exe(path, os.path.join(installation, 'bin'))

        for exe in EXECUTABLES:
            path = os.path.join(args.pv_dir, 'bin', exe)

            if not os.path.exists(path):
                path = os.path.join(args.prefix, 'bin', exe)

            if not os.path.exists(path):
                path = '/usr/bin/{}'.format(exe)

            install_exe(path, os.path.join(installation, 'bin'))

        install(
            os.path.join(args.srcdir, 'pressure-vessel', 'THIRD-PARTY.md'),
            os.path.join(installation, 'metadata', 'README.txt'),
            0o644,
        )

        inst_pkglibexecdir = os.path.join(
            installation,
            'libexec',
            'steam-runtime-tools-0',
        )

        path = os.path.join(
            args.prefix, 'libexec', 'steam-runtime-tools-0',
        )

        if not os.path.exists(path):
            path = '/usr/libexec/steam-runtime-tools-0'

        for tool in ['launch-options.py']:
            install_exe(
                os.path.join(path, tool),
                os.path.join(inst_pkglibexecdir, tool),
            )

        for arch in architectures:
            path = os.path.join(
                args.prefix, 'libexec', 'steam-runtime-tools-0',
            )

            if not os.path.exists(path):
                path = '/usr/libexec/steam-runtime-tools-0'

            if not os.path.exists(path):
                package = 'libsteam-runtime-tools-0-helpers'
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

            for tool in glob.glob(os.path.join(path, arch.multiarch + '-*')):
                install_exe(
                    tool,
                    os.path.join(inst_pkglibexecdir, os.path.basename(tool)),
                )

            for shader in glob.glob(os.path.join(path, 'shaders', '*.spv')):
                install(
                    shader,
                    os.path.join(
                        inst_pkglibexecdir,
                        'shaders', os.path.basename(shader),
                    )
                )

            shutil.copytree(
                os.path.join(path, arch.multiarch),
                os.path.join(inst_pkglibexecdir, arch.multiarch),
            )

        primary_architecture = subprocess.check_output([
            'dpkg', '--print-architecture',
        ]).decode('utf-8').strip()

        for arch in architectures:
            os.makedirs(
                os.path.join(tmpdir, 'build-relocatable', arch.name, 'lib'),
                exist_ok=True,
            )

            v_check_call([
                '{}/{}-capsule-capture-libs'.format(
                    inst_pkglibexecdir,
                    arch.multiarch,
                ),
                '--dest={}/build-relocatable/{}/lib'.format(
                    tmpdir,
                    arch.name,
                ),
                '--no-glibc',
                'soname:libelf.so.1',
                'soname:libz.so.1',
                'no-dependencies:soname:libwaffle-1.so.0',
            ])

            if arch.name == primary_architecture:
                v_check_call([
                    '{}/{}-capsule-capture-libs'.format(
                        inst_pkglibexecdir,
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
                    'soname:libjson-glib-1.0.so.0',
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
                install(
                    so,
                    os.path.join(
                        installation, 'lib', arch.multiarch,
                        'steam-runtime-tools-0',
                        os.path.basename(so)
                    )
                )

        source_to_download = set()      # type: typing.Set[str]
        installed_binaries = set()      # type: typing.Set[str]

        get_source = (
            list(DEPENDENCIES.items())
            + list(PRIMARY_ARCH_DEPENDENCIES.items())
        )
        get_source.append(
            ('pressure-vessel-relocatable', 'steam-runtime-tools'),
        )

        for package, source in get_source:
            if os.path.exists('/usr/share/doc/{}/copyright'.format(package)):
                installed_binaries.add(package)

                if source in DIFFERENT_COPYRIGHT_FILES:
                    install(
                        '/usr/share/doc/{}/copyright'.format(package),
                        os.path.join(
                            installation,
                            'metadata',
                            '{}.txt'.format(package),
                        ),
                    )
                else:
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
            writer.flush()
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

        with open(
            os.path.join(installation, 'metadata', 'sources.txt'), 'w'
        ) as writer:
            writer.write(
                '#Source\t#Version\n'
            )
            for source in sorted(source_to_download):
                writer.write(source.replace('=', '\t') + '\n')

        shutil.copytree(
            os.path.join(installation, 'metadata'),
            os.path.join(installation, 'sources'),
        )

        if args.check_source_directory is None:
            source_should_be_in = os.path.join(installation, 'sources')
        else:
            source_should_be_in = args.check_source_directory

        for source in sorted(source_to_download):
            package, version = source.split('=')

            if ':' in version:
                version = version.split(':', 1)[1]

            filename = os.path.join(
                source_should_be_in,
                '{}_{}.dsc'.format(package, version),
            )

            if args.cache:
                cache_filename = os.path.join(
                    args.cache,
                    '{}_{}.dsc'.format(package, version),
                )

                if (
                    os.path.exists(cache_filename)
                    and args.check_source_directory is None
                ):
                    if v_call([
                        'dcmd', 'cp', '-al', cache_filename,
                        source_should_be_in,
                    ]) != 0:
                        try:
                            os.remove(filename)
                        except FileNotFoundError:
                            pass

            if os.path.exists(filename) and v_call([
                'dscverify', '--no-sig-check', filename,
            ]) == 0:
                source_to_download.remove(source)
            elif args.check_source_directory is None:
                pass
            elif args.allow_missing_sources:
                logger.warning(
                    'Source code not found in %s', filename)
            else:
                raise RuntimeError(
                    'Source code not found in %s', filename)

        if args.check_source_directory is None and source_to_download:
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

            if args.cache:
                for source in source_to_download:
                    package, version = source.split('=')

                    if ':' in version:
                        version = version.split(':', 1)[1]

                    filename = os.path.join(
                        source_should_be_in,
                        '{}_{}.dsc'.format(package, version),
                    )

                    if os.path.exists(filename):
                        v_check_call([
                            'dcmd', 'cp', '-al', filename, args.cache + '/',
                        ])

        if args.archive:
            if args.archive_versions:
                tail = '-' + args.version
            else:
                tail = ''

            if args.architecture_name is None:
                bin_arch = 'bin'
            else:
                bin_arch = args.architecture_name

            bin_tar = os.path.join(
                args.archive,
                'pressure-vessel{}-{}.tar.gz'.format(tail, bin_arch),
            )

            if args.check_source_directory is None:
                src_tar = os.path.join(
                    args.archive,
                    'pressure-vessel{}-{}+src.tar.gz'.format(tail, bin_arch),
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
    assert sys.version_info >= (3, 5), \
        'Python 3.5+ is required (configure with -Dpython=python3.5 ' \
        'if necessary)'
    logging.basicConfig()
    main()

# vim:set sw=4 sts=4 et:
