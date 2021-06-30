#!/usr/bin/env python3

# Copyright © 2018-2021 Collabora Ltd.
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
import contextlib
import hashlib
import logging
import os
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import textwrap
import typing
from pathlib import Path


logger = logging.getLogger('pressure-vessel.deploy')

COMMAND = typing.Union[str, typing.List[str]]


@contextlib.contextmanager
def RemoteTemporaryDirectory(
    ssh: typing.List[str],
    parent: typing.Optional[str] = None,
):
    argv = ssh + [
        'mktemp', '-d',
    ]

    if parent is not None:
        argv.append('-p')
        argv.append(parent)

    tmpdir = subprocess.check_output(
        argv,
        universal_newlines=True,
    ).strip('\n')

    try:
        yield tmpdir
    finally:
        subprocess.call(ssh + [
            'rm', '-fr', tmpdir,
        ])


@contextlib.contextmanager
def SshMaster(
    ssh: typing.List[str],
):
    logger.debug('Opening persistent ssh connection...')

    env = dict(os.environ)

    process = subprocess.Popen(
        ssh + ['-M', 'cat'],
        env=env,
        stdin=subprocess.PIPE,
        stdout=subprocess.DEVNULL,
    )

    try:
        assert process.stdin is not None
        with process.stdin:
            yield
    finally:
        logger.debug('Closing persistent ssh connection...')
        process.wait()
        logger.debug('Closed persistent ssh connection')


class Uploader:
    def __init__(
        self,
        host: str,
        path: str,
        login: str,
        dry_run: bool,
        ssh_known_hosts: typing.Optional[str] = None,
        ssh_private_key: typing.Optional[str] = None,
    ) -> None:
        self.host = host
        self.basedir = path
        self.login = login
        self.dry_run = dry_run

        self.ssh_target = '{}@{}'.format(self.login, self.host)
        self.ssh_known_hosts = ssh_known_hosts
        self.ssh_private_key = ssh_private_key

        self.stack = contextlib.ExitStack()
        self.local_tmpdir = None    # type: typing.Optional[str]
        self.ssh = ['false']        # type: typing.List[str]
        self.remote_tmpdir = None   # type: typing.Optional[str]

    def __enter__(self) -> 'Uploader':
        self.local_tmpdir = self.stack.enter_context(
            tempfile.TemporaryDirectory()
        )
        assert self.local_tmpdir is not None
        self.ssh = [
            'ssh',
            '-oControlPath={}/socket'.format(self.local_tmpdir),
        ]

        if self.ssh_known_hosts is not None:
            self.ssh.append('-oUserKnownHostsFile=' + self.ssh_known_hosts)

        if self.ssh_private_key is not None:
            self.ssh.extend(['-i', self.ssh_private_key])

        self.ssh.append(self.ssh_target)
        self.stack.enter_context(SshMaster(self.ssh))
        self.remote_tmpdir = self.stack.enter_context(
            RemoteTemporaryDirectory(self.ssh)
        )
        return self

    def __exit__(self, *exc) -> None:
        self.stack.__exit__(*exc)

    def remote_command(
        self,
        command: COMMAND,
        chdir=True,
        shell=False,
    ) -> str:
        preamble = textwrap.dedent('''\
            set -eu;
            umask 0022;
        ''')

        if chdir:
            preamble = preamble + 'cd {};\n'.format(shlex.quote(self.basedir))

        if shell:
            assert isinstance(command, str)
            return preamble + command
        else:
            assert isinstance(command, list)
            return preamble + ' '.join(map(shlex.quote, command))

    def popen(self, command: COMMAND, chdir=True, shell=False, **kwargs):
        logger.debug('remote: %s', command)
        if self.dry_run:
            return None
        else:
            return subprocess.Popen(self.ssh + [
                self.remote_command(command, chdir=chdir, shell=shell),
            ], **kwargs)

    def check_call(
        self,
        command: COMMAND,
        chdir=True,
        shell=False,
        **kwargs
    ) -> None:
        logger.debug('remote: %s', command)
        if not self.dry_run:
            subprocess.check_call(self.ssh + [
                self.remote_command(command, chdir=chdir, shell=shell),
            ], **kwargs)

    def check_output(
        self,
        command: COMMAND,
        chdir=True,
        shell=False,
        **kwargs
    ):
        logger.debug('remote: %s', command)
        if self.dry_run:
            return None
        else:
            return subprocess.check_output(self.ssh + [
                self.remote_command(command, chdir=chdir, shell=shell),
            ], **kwargs)

    def call(self, command: COMMAND, chdir=True, shell=False, **kwargs):
        logger.debug('remote: %s', command)
        return subprocess.call(self.ssh + [
            self.remote_command(command, chdir=chdir, shell=shell),
        ], **kwargs)

    def run(self):
        with self:
            self.check_call([
                'mkdir', '-p', self.basedir,
            ], chdir=False)

            self.upload()

    def upload(self) -> None:
        assert self.local_tmpdir is not None
        assert self.remote_tmpdir is not None

        upload = Path('_build', 'upload')

        with contextlib.suppress(FileNotFoundError):
            shutil.rmtree(str(upload))

        upload.mkdir()
        packages = Path('_build', 'upload', 'packages')
        packages.mkdir()
        sources = Path('_build', 'upload', 'sources')
        sources.mkdir()

        for a in Path('debian', 'tmp', 'artifacts', 'build').iterdir():
            if str(a).endswith('.dsc'):
                subprocess.check_call([
                    'dcmd', 'ln', str(a), str(sources),
                ])
            elif str(a).endswith(('.deb', '.ddeb')):
                subprocess.check_call([
                    'dcmd', 'ln', str(a), str(packages),
                ])

        a = Path('_build', 'production', 'pressure-vessel-bin.tar.gz')
        os.link(str(a), upload / a.name)

        a = Path('_build', 'production', 'pressure-vessel-bin+src.tar.gz')

        # Unpack sources/*.{dsc,tar.*,txt,...} into sources/
        with tarfile.open(str(a), 'r') as unarchiver:
            for member in unarchiver:
                parts = member.name.split('/')

                if (
                    member.isfile()
                    and len(parts) >= 2
                    and parts[-2] == 'sources'
                ):
                    extract = unarchiver.extractfile(member)
                    assert extract is not None
                    with extract:
                        with open(
                            str(sources / parts[-1]), 'wb'
                        ) as writer:
                            shutil.copyfileobj(extract, writer)

        os.link(str(sources / 'VERSION.txt'), str(upload / 'VERSION.txt'))

        to_hash: typing.List[str] = []

        for dirpath, dirnames, filenames in os.walk(str(upload)):
            relpath = Path(dirpath).relative_to(upload)

            for f in filenames:
                to_hash.append(str(Path(relpath, f)))

        with open(str(upload / 'SHA256SUMS'), 'w') as text_writer:
            for f in sorted(to_hash):
                hasher = hashlib.sha256()

                with open(str(upload / f), 'rb') as binary_reader:
                    while True:
                        blob = binary_reader.read(4096)

                        if not blob:
                            break

                        hasher.update(blob)

                text_writer.write('{} *{}\n'.format(hasher.hexdigest(), f))

        with open(str(upload / 'VERSION.txt')) as reader:
            version = reader.read().strip()

        self.check_call([
            'mkdir', version,
        ])
        self.check_call(
            'if [ -d latest ]; then cp -al latest/sources {}; fi'.format(
                shlex.quote(version)
            ),
            shell=True,
        )

        logger.info('Uploading artifacts using rsync...')

        for argv in [
            # First pass: upload with --size-only to preserve hard-links
            # among source tarballs, excluding *.txt because they might
            # legitimately change without their size changing
            [
                'rsync',
                '--rsh', ' '.join(map(shlex.quote, self.ssh[:-1])),
                '--chmod=a+rX,og-w',
                '--delete',
                '--exclude=*.txt',
                '--links',
                '--partial',
                '--perms',
                '--recursive',
                '--size-only',
                '--verbose',
                '_build/upload/sources/',
                '{}:{}/{}/sources/'.format(
                    self.ssh_target, self.basedir, version,
                ),
            ],
            # Second pass: upload everything except Debian source packages
            # without --size-only
            [
                'rsync',
                '--rsh', ' '.join(map(shlex.quote, self.ssh[:-1])),
                '--chmod=a+rX,og-w',
                '--delete',
                '--exclude=*.debian.tar.*',
                '--exclude=*.diff.gz',
                '--exclude=*.dsc',
                '--exclude=*.orig.tar.*',
                '--links',
                '--partial',
                '--perms',
                '--recursive',
                '--verbose',
                '_build/upload/',
                '{}:{}/{}/'.format(self.ssh_target, self.basedir, version),
            ]
        ]:
            if self.dry_run:
                logger.info('Would run: %r', argv)
            else:
                logger.info('%r', argv)
                subprocess.check_call(argv)

        # Check that our rsync options didn't optimize away a change that
        # should have happened
        with open(str(upload / 'SHA256SUMS')) as reader:
            self.check_call([
                'env', '--chdir', '{}'.format(version),
                'sha256sum', '--strict', '--quiet', '-c',
            ], stdin=reader)

        self.check_call([
            'ln', '-fns', version, 'latest',
        ])


def main() -> None:
    if sys.stderr.isatty():
        try:
            import colorlog
        except ImportError:
            logging.basicConfig()
        else:
            formatter = colorlog.ColoredFormatter(
                '%(log_color)s%(levelname)s:%(name)s:%(reset)s %(message)s')
            handler = logging.StreamHandler()
            handler.setFormatter(formatter)
            logging.getLogger().addHandler(handler)
    else:
        logging.basicConfig()

    logging.getLogger().setLevel(logging.DEBUG)

    parser = argparse.ArgumentParser(
        description='Upload a pressure-vessel release'
    )

    parser.add_argument('--host', required=True)
    parser.add_argument('--path', required=True)
    parser.add_argument('--login', required=True)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--ssh-known-hosts', default=None)
    parser.add_argument('--ssh-private-key', default=None)
    args = parser.parse_args()
    Uploader(**vars(args)).run()


if __name__ == '__main__':
    main()
