#!/usr/bin/env python3

# Copyright © 2019-2020 Collabora Ltd.
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

"""
Build the steam-container-runtime (aka SteamLinuxRuntime) depot, either
from just-built files or by downloading a previous build.
"""

import argparse
import json
import logging
import os
import shlex
import shutil
import subprocess
import tempfile
import urllib.request
from contextlib import suppress
from typing import (
    Any,
    Dict,
    List,
    Optional,
    Sequence,
)


logger = logging.getLogger('populate-depot')


class InvocationError(Exception):
    pass


class Runtime:
    def __init__(
        self,
        name,
        *,
        suite: str,

        architecture: str = 'amd64,i386',
        include_sdk: bool = False,
        path: Optional[str] = None,
        version: str = 'latest',
    ) -> None:
        self.architecture = architecture
        self.include_sdk = include_sdk
        self.name = name
        self.path = path
        self.suite = suite
        self.version = version
        self.pinned_version = None      # type: Optional[str]

        self.prefix = 'com.valvesoftware.SteamRuntime'
        self.platform = self.prefix + '.Platform'
        self.sdk = self.prefix + '.Sdk'
        self.tarball = '{}-{}-{}-runtime.tar.gz'.format(
            self.platform,
            self.architecture,
            self.suite,
        )
        self.dockerfile = '{}-{}-{}-sysroot.Dockerfile'.format(
            self.sdk,
            self.architecture,
            self.suite,
        )
        self.sysroot_tarball = '{}-{}-{}-sysroot.tar.gz'.format(
            self.sdk,
            self.architecture,
            self.suite,
        )
        self.build_id_file = '{}-{}-{}-buildid.txt'.format(
            self.platform,
            self.architecture,
            self.suite,
        )

        self.runtime_files = [self.tarball]

        if self.include_sdk:
            self.runtime_files.append(self.sysroot_tarball)
            self.runtime_files.append(self.dockerfile)

    def __str__(self) -> str:
        return self.name

    @classmethod
    def from_details(
        cls,
        name: str,
        details: Dict[str, Any],
        default_architecture: str = 'amd64,i386',
        default_include_sdk: bool = False,
        default_suite: str = '',
        default_version: str = 'latest',
    ):
        return cls(
            name,
            architecture=details.get(
                'architecture', default_architecture,
            ),
            include_sdk=details.get('include_sdk', default_include_sdk),
            path=details.get('path', None),
            suite=details.get('suite', default_suite or name),
            version=details.get('version', default_version),
        )

    def get_uri(
        self,
        filename: str,
        version: Optional[str] = None,
    ) -> str:
        suite = self.suite
        v = version or self.pinned_version or self.version
        return (
            f'https://images.steamos.cloud/steamrt-{suite}/'
            f'snapshots/{v}/{filename}'
        )

    def get_ssh_path(
        self,
        filename: str,
        version: Optional[str] = None,
    ) -> str:
        suite = self.suite
        v = version or self.pinned_version or self.version
        return (
            f'/srv/images.internal.steamos.cloud/www/steamrt-{suite}/'
            f'snapshots/{v}/{filename}'
        )

    def fetch(
        self,
        filename: str,
        destdir: str,
        opener: urllib.request.OpenerDirector,
        ssh: bool = False,
        version: Optional[str] = None,
    ) -> None:
        if ssh:
            path = self.get_ssh_path(filename)
            logger.info('Downloading %r...', path)
            subprocess.run([
                'rsync',
                '--archive',
                '--partial',
                '--progress',
                'images.internal.steamos.cloud:' + path,
                os.path.join(destdir, filename),
            ], check=True)
        else:
            uri = self.get_uri(filename)
            logger.info('Downloading %r...', uri)

            with opener.open(uri) as response:
                with open(os.path.join(destdir, filename), 'wb') as writer:
                    shutil.copyfileobj(response, writer)

    def pin_version(
        self,
        opener: urllib.request.OpenerDirector,
        ssh: bool = False,
    ) -> str:
        pinned = self.pinned_version

        if pinned is None:
            if ssh:
                path = self.get_ssh_path(filename='VERSION.txt')
                logger.info('Determining version number from %r...', path)
                pinned = subprocess.run([
                    'ssh', 'images.internal.steamos.cloud',
                    'cat {}'.format(shlex.quote(path)),
                ], stdout=subprocess.PIPE).stdout.decode('utf-8').strip()
            else:
                uri = self.get_uri(filename='VERSION.txt')
                logger.info('Determining version number from %r...', uri)
                with opener.open(uri) as response:
                    pinned = response.read().decode('utf-8').strip()

            self.pinned_version = pinned

        return pinned


RUN_IN_WHATEVER_SOURCE = '''\
#!/bin/sh
# {source_for_generated_file}

set -eu

me="$(readlink -f "$0")"
here="${{me%/*}}"
me="${{me##*/}}"

exec "$here/run-in-steamrt" \\
    --arch={escaped_arch} \\
    --deploy \\
    --runtime={escaped_runtime} \\
    --suite={escaped_suite} \\
    {escaped_name} \\
    -- \\
    "$@"
'''


class Main:
    def __init__(
        self,
        architecture: str = 'amd64,i386',
        credential_envs: Sequence[str] = (),
        depot: str = 'depot',
        include_sdk: bool = False,
        pressure_vessel: str = 'scout',
        runtimes: Sequence[str] = (),
        ssh: bool = False,
        suite: str = '',
        unpack_ld_library_path: str = '',
        version: str = 'latest',
        **kwargs: Dict[str, Any],
    ) -> None:
        openers: List[urllib.request.BaseHandler] = []

        if not runtimes:
            runtimes = ('scout',)

        if credential_envs:
            password_manager = urllib.request.HTTPPasswordMgrWithDefaultRealm()

            for cred in credential_envs:
                if ':' in cred:
                    username_env, password_env = cred.split(':', 1)
                    logger.info(
                        'Using username from $%s and password from $%s',
                        username_env, password_env)
                    username = os.environ[username_env]
                    password = os.environ[password_env]
                else:
                    logger.info(
                        'Using username and password from $%s', cred)
                    username, password = os.environ[cred].split(':', 1)

                password_manager.add_password(
                    None,       # type: ignore
                    'https://images.steamos.cloud/',
                    username,
                    password,
                )

            openers.append(
                urllib.request.HTTPBasicAuthHandler(password_manager)
            )

        self.opener = urllib.request.build_opener(*openers)

        self.default_architecture = architecture
        self.default_include_sdk = include_sdk
        self.default_suite = suite
        self.default_version = version
        self.depot = os.path.abspath(depot)
        self.pressure_vessel = pressure_vessel
        self.runtimes = []      # type: List[Runtime]
        self.ssh = ssh
        self.unpack_ld_library_path = unpack_ld_library_path

        for runtime in runtimes:
            if '=' in runtime:
                name, rhs = runtime.split('=', 1)

                if rhs.startswith('{'):
                    details = json.loads(rhs)
                else:
                    with open(rhs, 'rb') as reader:
                        details = json.load(reader)
            else:
                name = runtime
                details = {}

            self.runtimes.append(self.new_runtime(name, details))

    def new_runtime(self, name: str, details: Dict[str, Any]) -> Runtime:
        return Runtime.from_details(
            name,
            details,
            default_architecture=self.default_architecture,
            default_include_sdk=self.default_include_sdk,
            default_suite=self.default_suite,
            default_version=self.default_version,
        )

    def run(self) -> None:
        for runtime in self.runtimes:
            if runtime.name == self.pressure_vessel:
                logger.info(
                    'Downloading pressure-vessel from %s', runtime.name)
                pressure_vessel_runtime = runtime
                self.download_pressure_vessel(pressure_vessel_runtime)
                break
        else:
            if self.pressure_vessel.startswith('{'):
                logger.info(
                    'Downloading pressure-vessel using JSON from command-line')
                pressure_vessel_runtime = self.new_runtime(
                    'scout', json.loads(self.pressure_vessel),
                )
                self.download_pressure_vessel(pressure_vessel_runtime)
            elif os.path.isdir(self.pressure_vessel):
                logger.info(
                    'Unpacking pressure-vessel from local directory %s',
                    self.pressure_vessel)
                self.use_local_pressure_vessel(self.pressure_vessel)
                pressure_vessel_runtime = self.new_runtime(
                    'scout', {'path': self.pressure_vessel},
                )
            else:
                logger.info(
                    'Downloading pressure-vessel using JSON from %r',
                    self.pressure_vessel)
                with open(self.pressure_vessel, 'rb') as reader:
                    details = json.load(reader)
                pressure_vessel_runtime = self.new_runtime('scout', details)
                self.download_pressure_vessel(pressure_vessel_runtime)

        if self.unpack_ld_library_path:
            logger.info(
                'Downloading LD_LIBRARY_PATH Steam Runtime from same place '
                'as pressure-vessel into %r',
                self.unpack_ld_library_path)
            self.download_scout_tarball(pressure_vessel_runtime)

        for runtime in self.runtimes:
            if runtime.path:
                logger.info(
                    'Using runtime from local directory %r',
                    runtime.path)
                self.use_local_runtime(runtime)
            else:
                logger.info(
                    'Downloading runtime from %s',
                    runtime)
                self.download_runtime(runtime)

            with open(
                os.path.join(self.depot, 'run-in-' + runtime.name), 'w'
            ) as writer:
                writer.write(
                    RUN_IN_WHATEVER_SOURCE.format(
                        escaped_arch=shlex.quote(runtime.architecture),
                        escaped_name=shlex.quote(runtime.name),
                        escaped_runtime=shlex.quote(runtime.platform),
                        escaped_suite=shlex.quote(runtime.suite),
                        source_for_generated_file=(
                            'Generated file, do not edit'
                        ),
                    )
                )
            os.chmod(os.path.join(self.depot, 'run-in-' + runtime.name), 0o755)

    def use_local_pressure_vessel(self, path: str = '.') -> None:
        os.makedirs(self.depot, exist_ok=True)
        argv = [
            'tar', '-C', self.depot, '-xvf',
            os.path.join(path, 'pressure-vessel-bin.tar.gz'),
        ]
        logger.info('%r', argv)
        subprocess.run(argv, check=True)

    def download_pressure_vessel(self, runtime: Runtime) -> None:
        filename = 'pressure-vessel-bin.tar.gz'
        runtime.pin_version(self.opener, ssh=self.ssh)

        with tempfile.TemporaryDirectory(prefix='populate-depot.') as tmp:
            runtime.fetch(filename, tmp, self.opener, ssh=self.ssh)

            os.makedirs(self.depot, exist_ok=True)
            subprocess.run(
                [
                    'tar', '-C', self.depot, '-xvf',
                    os.path.join(tmp, filename),
                ],
                check=True,
            )

    def use_local_runtime(self, runtime: Runtime) -> None:
        assert runtime.path

        for basename in runtime.runtime_files:
            src = os.path.join(runtime.path, basename)
            dest = os.path.join(self.depot, basename)
            logger.info('Hard-linking local runtime %r to %r', src, dest)

            with suppress(FileNotFoundError):
                os.unlink(dest)

            os.link(src, dest)

        with open(
            os.path.join(self.depot, runtime.build_id_file), 'w',
        ) as writer:
            writer.write(f'{runtime.version}\n')

    def download_runtime(self, runtime: Runtime) -> None:
        """
        Download a pre-prepared Platform from a previous container
        runtime build.
        """

        pinned = runtime.pin_version(self.opener, ssh=self.ssh)
        for basename in runtime.runtime_files:
            runtime.fetch(basename, self.depot, self.opener, ssh=self.ssh)

        with open(
            os.path.join(self.depot, runtime.build_id_file), 'w',
        ) as writer:
            writer.write(f'{pinned}\n')

    def download_scout_tarball(self, runtime: Runtime) -> None:
        """
        Download a pre-prepared LD_LIBRARY_PATH Steam Runtime from a
        previous scout build.
        """
        filename = 'steam-runtime.tar.xz'

        pinned = runtime.pin_version(self.opener, ssh=self.ssh)
        logger.info('Downloading steam-runtime build %s', pinned)
        os.makedirs(self.unpack_ld_library_path, exist_ok=True)

        with tempfile.TemporaryDirectory(prefix='populate-depot.') as tmp:
            runtime.fetch(filename, tmp, self.opener, ssh=self.ssh)
            subprocess.run(
                [
                    'tar', '-C', self.unpack_ld_library_path, '-xvf',
                    os.path.join(tmp, filename),
                ],
                check=True,
            )


def main() -> None:
    logging.basicConfig()
    logging.getLogger().setLevel(logging.DEBUG)

    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    parser.add_argument(
        '--architecture', default='amd64,i386',
        help=(
            'Default dpkg architecture or comma-separated list of '
            'architectures'
        )
    )
    parser.add_argument(
        '--suite', default='',
        help=(
            'Default suite to use if none is specified'
        )
    )
    parser.add_argument(
        '--version', default='latest',
        help=(
            'Default version to use if none is specified'
        )
    )

    parser.add_argument(
        '--credential-env',
        action='append',
        default=[],
        dest='credential_envs',
        help=(
            'Environment variable to be evaluated for login:password, '
            'or a pair of environment variables VAR1:VAR2 to be evaluated '
            'for login and password respectively'
        ),
    )

    parser.add_argument(
        '--ssh', default=False, action='store_true',
        help='Use ssh and rsync to download files',
    )
    parser.add_argument(
        '--depot', default='depot',
        help=(
            'Download runtimes into this existing directory'
        )
    )
    parser.add_argument(
        '--pressure-vessel', default='scout', metavar='NAME|PATH|DETAILS',
        help=(
            'Get pressure-vessel from the named runtime (default "scout"), '
            'or from a runtime version given as a JSON object, '
            'or from a given directory (use ./ to disambiguate if necessary).'
        )
    )
    parser.add_argument(
        '--include-sdk', default=False, action='store_true',
        help='Include a corresponding SDK',
    )
    parser.add_argument(
        '--unpack-ld-library-path', metavar='PATH', default='',
        help=(
            'Get the steam-runtime.tar.xz from the same place as '
            'pressure-vessel and unpack it into the given PATH, '
            'for use in regression testing.'
        )
    )
    parser.add_argument(
        'runtimes',
        default=[],
        metavar='NAME[="DETAILS"]',
        nargs='*',
        help=(
            'Runtimes to download, in the form NAME or NAME="DETAILS". '
            'DETAILS is a JSON object containing something like '
            '{"path": "../prebuilt", "suite: "scout", "version": "latest", '
            '"architecture": "amd64,i386", "include_sdk": true}, or the '
            'path to a file with the same JSON object in. All JSON fields '
            'are optional.'
        ),
    )

    try:
        args = parser.parse_args()
        Main(**vars(args)).run()
    except InvocationError as e:
        parser.error(str(e))


if __name__ == '__main__':
    main()
