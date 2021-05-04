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
import gzip
import hashlib
import json
import logging
import os
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.parse
import urllib.request
from contextlib import suppress
from typing import (
    Any,
    Dict,
    List,
    Optional,
    Sequence,
    Set,
)

from debian.deb822 import (
    Sources,
)


HERE = os.path.dirname(os.path.abspath(__file__))


# git remote add --no-tags python-vdf https://github.com/ValvePython/vdf
# Update with:
# git subtree merge -P subprojects/python-vdf python-vdf/master
sys.path[:0] = [
    os.path.join(
        HERE,
        'subprojects',
        'python-vdf'
    ),
]


logger = logging.getLogger('populate-depot')


DEFAULT_IMAGES_URI = (
    'https://repo.steampowered.com/steamrt-images-SUITE/snapshots'
)


class InvocationError(Exception):
    pass


class Runtime:
    def __init__(
        self,
        name,
        *,
        suite: str,

        architecture: str = 'amd64,i386',
        cache: str = '.cache',
        images_uri: str = DEFAULT_IMAGES_URI,
        include_sdk: bool = False,
        path: Optional[str] = None,
        ssh_host: str = '',
        ssh_path: str = '',
        version: str = 'latest',
    ) -> None:
        self.architecture = architecture
        self.cache = cache
        self.images_uri = images_uri
        self.include_sdk = include_sdk
        self.name = name
        self.path = path
        self.suite = suite
        self.ssh_host = ssh_host
        self.ssh_path = ssh_path
        self.version = version
        self.pinned_version = None      # type: Optional[str]
        self.sha256 = {}                # type: Dict[str, str]

        os.makedirs(self.cache, exist_ok=True)

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
        self.sdk_tarball = '{}-{}-{}-runtime.tar.gz'.format(
            self.sdk,
            self.architecture,
            self.suite,
        )
        self.debug_tarball = '{}-{}-{}-debug.tar.gz'.format(
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
        self.sdk_build_id_file = '{}-{}-{}-buildid.txt'.format(
            self.sdk,
            self.architecture,
            self.suite,
        )
        self.sources = '{}-{}-{}-sources.deb822.gz'.format(
            self.sdk,
            self.architecture,
            self.suite,
        )

        self.runtime_files = [self.tarball]

        if self.include_sdk:
            self.runtime_files.append(self.debug_tarball)
            self.runtime_files.append(self.dockerfile)
            self.runtime_files.append(self.sdk_tarball)
            self.runtime_files.append(self.sysroot_tarball)

    def __str__(self) -> str:
        return self.name

    @classmethod
    def from_details(
        cls,
        name: str,
        details: Dict[str, Any],
        cache: str = '.cache',
        default_architecture: str = 'amd64,i386',
        default_include_sdk: bool = False,
        default_suite: str = '',
        default_version: str = 'latest',
        images_uri: str = DEFAULT_IMAGES_URI,
        ssh_host: str = '',
        ssh_path: str = '',
    ):
        return cls(
            name,
            architecture=details.get(
                'architecture', default_architecture,
            ),
            cache=cache,
            images_uri=images_uri,
            include_sdk=details.get('include_sdk', default_include_sdk),
            path=details.get('path', None),
            ssh_host=ssh_host,
            ssh_path=ssh_path,
            suite=details.get('suite', default_suite or name),
            version=details.get('version', default_version),
        )

    def get_uri(
        self,
        filename: str,
        version: Optional[str] = None,
    ) -> str:
        suite = self.suite
        uri = self.images_uri.replace('SUITE', suite)
        v = version or self.pinned_version or self.version
        return f'{uri}/{v}/{filename}'

    def get_ssh_path(
        self,
        filename: str,
        version: Optional[str] = None,
    ) -> str:
        ssh_host = self.ssh_host
        suite = self.suite
        ssh_path = self.ssh_path.replace('SUITE', suite)
        v = version or self.pinned_version or self.version

        if not ssh_host or not ssh_path:
            raise RuntimeError('ssh host/path not configured')

        return f'{ssh_path}/{v}/{filename}'

    def fetch(
        self,
        filename: str,
        opener: urllib.request.OpenerDirector,
        version: Optional[str] = None,
    ) -> str:
        dest = os.path.join(self.cache, filename)

        if filename in self.sha256:
            try:
                with open(dest, 'rb') as reader:
                    hasher = hashlib.sha256()

                    while True:
                        blob = reader.read(4096)

                        if not blob:
                            break

                        hasher.update(blob)

                    digest = hasher.hexdigest()
            except OSError:
                pass
            else:
                if digest == self.sha256[filename]:
                    logger.info('Using cached %r', dest)
                    return dest

        if self.ssh_host and self.ssh_path:
            path = self.get_ssh_path(filename)
            logger.info('Downloading %r...', path)
            subprocess.run([
                'rsync',
                '--archive',
                '--partial',
                '--progress',
                self.ssh_host + ':' + path,
                dest,
            ], check=True)
        else:
            uri = self.get_uri(filename)
            logger.info('Downloading %r...', uri)

            with opener.open(uri) as response:
                with open(dest + '.new', 'wb') as writer:
                    shutil.copyfileobj(response, writer)

                os.rename(dest + '.new', dest)

        return dest

    def pin_version(
        self,
        opener: urllib.request.OpenerDirector,
    ) -> str:
        pinned = self.pinned_version
        sha256 = {}     # type: Dict[str, str]

        if pinned is None:
            if self.ssh_host and self.ssh_path:
                path = self.get_ssh_path(filename='VERSION.txt')
                logger.info('Determining version number from %r...', path)
                pinned = subprocess.run([
                    'ssh', self.ssh_host,
                    'cat {}'.format(shlex.quote(path)),
                ], stdout=subprocess.PIPE).stdout.decode('utf-8').strip()

                path = self.get_ssh_path(filename='SHA256SUMS')

                sha256sums = subprocess.run([
                    'ssh', self.ssh_host,
                    'cat {}'.format(shlex.quote(path)),
                ], stdout=subprocess.PIPE).stdout
                assert sha256sums is not None

            else:
                uri = self.get_uri(filename='VERSION.txt')
                logger.info('Determining version number from %r...', uri)
                with opener.open(uri) as response:
                    pinned = response.read().decode('utf-8').strip()

                uri = self.get_uri(filename='SHA256SUMS')

                with opener.open(uri) as response:
                    sha256sums = response.read()

            for line in sha256sums.splitlines():
                sha256_bytes, name_bytes = line.split(maxsplit=1)
                name = name_bytes.decode('utf-8')

                if name.startswith('*'):
                    name = name[1:]

                sha256[name] = sha256_bytes.decode('ascii')

            self.sha256 = sha256
            self.pinned_version = pinned

        return pinned


RUN_IN_DIR_SOURCE = '''\
#!/bin/sh
# {source_for_generated_file}

set -eu

me="$(readlink -f "$0")"
here="${{me%/*}}"
me="${{me##*/}}"

dir={escaped_dir}
pressure_vessel="${{PRESSURE_VESSEL_PREFIX:-"${{here}}/pressure-vessel"}}"

export PRESSURE_VESSEL_GC_LEGACY_RUNTIMES=1
export PRESSURE_VESSEL_RUNTIME="${{dir}}"
unset PRESSURE_VESSEL_RUNTIME_ARCHIVE
export PRESSURE_VESSEL_RUNTIME_BASE="${{here}}"

if [ -z "${{PRESSURE_VESSEL_VARIABLE_DIR-}}" ]; then
    export PRESSURE_VESSEL_VARIABLE_DIR="${{here}}/var"
fi

exec "${{pressure_vessel}}/bin/pressure-vessel-unruntime" "$@"
'''

RUN_IN_ARCHIVE_SOURCE = '''\
#!/bin/sh
# {source_for_generated_file}

set -eu

me="$(readlink -f "$0")"
here="${{me%/*}}"
me="${{me##*/}}"

archive={escaped_runtime}-{escaped_arch}-{escaped_suite}-runtime.tar.gz
pressure_vessel="${{PRESSURE_VESSEL_PREFIX:-"${{here}}/pressure-vessel"}}"

export PRESSURE_VESSEL_GC_LEGACY_RUNTIMES=1
unset PRESSURE_VESSEL_RUNTIME
export PRESSURE_VESSEL_RUNTIME_ARCHIVE="${{archive}}"
export PRESSURE_VESSEL_RUNTIME_BASE="${{here}}"

if [ -z "${{PRESSURE_VESSEL_VARIABLE_DIR-}}" ]; then
    export PRESSURE_VESSEL_VARIABLE_DIR="${{here}}/var"
fi

exec "${{pressure_vessel}}/bin/pressure-vessel-unruntime" "$@"
'''


class ComponentVersion:
    def __init__(self, name: str = '') -> None:
        self.name = name
        self.version = ''
        self.runtime = ''
        self.runtime_version = ''
        self.comment = ''

    def __str__(self) -> str:
        ret = '{} version {!r}'.format(self.name, self.version)

        if self.runtime or self.runtime_version:
            ret = ret + ' (from {} version {})'.format(
                self.runtime or '(unknown runtime)',
                self.runtime_version or '(unknown)',
            )

        return ret

    def to_tsv(self) -> str:
        if self.comment:
            comment = '# ' + self.comment
        else:
            comment = ''

        return '\t'.join((
            self.name, self.version,
            self.runtime, self.runtime_version,
            comment,
        )) + '\n'


class Main:
    def __init__(
        self,
        architecture: str = 'amd64,i386',
        cache: str = '.cache',
        credential_envs: Sequence[str] = (),
        credential_hosts: Sequence[str] = (),
        depot: str = 'depot',
        images_uri: str = DEFAULT_IMAGES_URI,
        include_archives: bool = False,
        include_sdk: bool = False,
        pressure_vessel: str = 'scout',
        runtimes: Sequence[str] = (),
        source_dir: str = HERE,
        ssh_host: str = '',
        ssh_path: str = '',
        suite: str = '',
        toolmanifest: bool = False,
        unpack_ld_library_path: str = '',
        unpack_runtimes: bool = True,
        unpack_sources: Sequence[str] = (),
        unpack_sources_into: str = '.',
        version: str = 'latest',
        versioned_directories: bool = False,
        **kwargs: Dict[str, Any],
    ) -> None:
        openers: List[urllib.request.BaseHandler] = []

        if not runtimes:
            runtimes = ('scout',)

        if not credential_hosts:
            credential_hosts = []
            host = urllib.parse.urlparse(images_uri).hostname

            if host is not None:
                credential_hosts.append(host)

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

                for host in credential_hosts:
                    password_manager.add_password(
                        None,       # type: ignore
                        host,
                        username,
                        password,
                    )

            openers.append(
                urllib.request.HTTPBasicAuthHandler(password_manager)
            )

        self.opener = urllib.request.build_opener(*openers)

        self.cache = cache
        self.default_architecture = architecture
        self.default_include_sdk = include_sdk
        self.default_suite = suite
        self.default_version = version
        self.depot = os.path.abspath(depot)
        self.images_uri = images_uri
        self.include_archives = include_archives
        self.pressure_vessel = pressure_vessel
        self.runtimes = []      # type: List[Runtime]
        self.source_dir = source_dir
        self.ssh_host = ssh_host
        self.ssh_path = ssh_path
        self.toolmanifest = toolmanifest
        self.unpack_ld_library_path = unpack_ld_library_path
        self.unpack_runtimes = unpack_runtimes
        self.unpack_sources = unpack_sources
        self.unpack_sources_into = unpack_sources_into
        self.versioned_directories = versioned_directories

        os.makedirs(self.cache, exist_ok=True)

        if not (self.include_archives or self.unpack_runtimes):
            raise RuntimeError(
                'Cannot use both --no-include-archives and '
                '--no-unpack-runtimes'
            )

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

        self.versions = []      # type: List[ComponentVersion]

    def new_runtime(
        self,
        name: str,
        details: Dict[str, Any],
        default_suite: str = '',
    ) -> Runtime:
        return Runtime.from_details(
            name,
            details,
            cache=self.cache,
            default_architecture=self.default_architecture,
            default_include_sdk=self.default_include_sdk,
            default_suite=default_suite or self.default_suite,
            default_version=self.default_version,
            images_uri=self.images_uri,
            ssh_host=self.ssh_host,
            ssh_path=self.ssh_path,
        )

    def merge_dir_into_depot(
        self,
        source_root: str,
    ):
        for (dirpath, dirnames, filenames) in os.walk(source_root):
            relative_path = os.path.relpath(dirpath, source_root)

            for member in dirnames:
                os.makedirs(
                    os.path.join(self.depot, relative_path, member),
                    exist_ok=True,
                )

            for member in filenames:
                source = os.path.join(dirpath, member)
                merged = os.path.join(self.depot, relative_path, member)

                with suppress(FileNotFoundError):
                    os.unlink(merged)

                os.makedirs(os.path.dirname(merged), exist_ok=True)
                shutil.copy(source, merged)

    def run(self) -> None:
        pv_version = ComponentVersion('pressure-vessel')

        self.merge_dir_into_depot(os.path.join(self.source_dir, 'common'))

        for runtime in self.runtimes:
            root = os.path.join(self.source_dir, runtime.name)

            if os.path.exists(root):
                self.merge_dir_into_depot(root)

        for runtime in self.runtimes:
            if runtime.name == self.pressure_vessel:
                logger.info(
                    'Downloading pressure-vessel from %s', runtime.name)
                pressure_vessel_runtime = runtime
                pv_version.comment = self.download_pressure_vessel(
                    pressure_vessel_runtime
                )
                break
        else:
            if self.pressure_vessel.startswith('{'):
                logger.info(
                    'Downloading pressure-vessel using JSON from command-line')
                pressure_vessel_runtime = self.new_runtime(
                    'scout',
                    json.loads(self.pressure_vessel),
                    default_suite='scout',
                )
                pv_version.comment = self.download_pressure_vessel(
                    pressure_vessel_runtime
                )
            elif (
                os.path.isdir(self.pressure_vessel)
                or (
                    os.path.isfile(self.pressure_vessel)
                    and self.pressure_vessel.endswith('.tar.gz')
                )
            ):
                logger.info(
                    'Unpacking pressure-vessel from local file or '
                    'directory %s',
                    self.pressure_vessel)
                self.use_local_pressure_vessel(self.pressure_vessel)
                pressure_vessel_runtime = self.new_runtime(
                    'scout', {'path': self.pressure_vessel},
                    default_suite='local',
                )
                pv_version.comment = 'from local file'
            elif os.path.isfile(self.pressure_vessel):
                logger.info(
                    'Downloading pressure-vessel using JSON from %r',
                    self.pressure_vessel)
                with open(self.pressure_vessel, 'rb') as reader:
                    details = json.load(reader)
                pressure_vessel_runtime = self.new_runtime(
                    'scout',
                    details,
                    default_suite='scout',
                )
                pv_version.comment = self.download_pressure_vessel(
                    pressure_vessel_runtime
                )
            else:
                logger.info(
                    'Assuming %r is a suite containing pressure-vessel',
                    self.pressure_vessel)
                pressure_vessel_runtime = self.new_runtime(
                    self.pressure_vessel, {},
                    default_suite=self.pressure_vessel,
                )
                pv_version.comment = self.download_pressure_vessel(
                    pressure_vessel_runtime
                )

        for path in ('metadata/VERSION.txt', 'sources/VERSION.txt'):
            full = os.path.join(self.depot, 'pressure-vessel', path)
            if os.path.exists(full):
                with open(full) as text_reader:
                    pv_version.version = text_reader.read().rstrip('\n')

                break

        pv_version.runtime = pressure_vessel_runtime.suite or ''
        pv_version.runtime_version = (
            pressure_vessel_runtime.pinned_version or ''
        )
        self.versions.append(pv_version)

        if self.unpack_ld_library_path:
            logger.info(
                'Downloading LD_LIBRARY_PATH Steam Runtime from same place '
                'as pressure-vessel into %r',
                self.unpack_ld_library_path)
            self.download_scout_tarball(pressure_vessel_runtime)

        if self.unpack_sources:
            logger.info(
                'Will download %s source code into %r',
                ', '.join(self.unpack_sources), self.unpack_sources_into)
            os.makedirs(self.unpack_sources_into, exist_ok=True)

            for runtime in self.runtimes:
                os.makedirs(
                    os.path.join(self.unpack_sources_into, runtime.name),
                    exist_ok=True,
                )

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

            component_version = ComponentVersion(runtime.name)

            if runtime.path:
                with open(
                    os.path.join(runtime.path, runtime.build_id_file), 'r',
                ) as text_reader:
                    version = text_reader.read().strip()
            else:
                version = runtime.pinned_version or ''
                assert version

            if self.include_archives:
                runtime_files = set(runtime.runtime_files)
            else:
                runtime_files = set()

            if self.unpack_runtimes:
                if self.versioned_directories:
                    subdir = '{}_platform_{}'.format(runtime.name, version)
                else:
                    subdir = runtime.name

                dest = os.path.join(self.depot, subdir)
                runtime_files.add(subdir + '/')

                with suppress(FileNotFoundError):
                    shutil.rmtree(dest)

                os.makedirs(dest, exist_ok=True)
                argv = [
                    'tar',
                    '-C', dest,
                    '-xf',
                    os.path.join(self.cache, runtime.tarball),
                ]
                logger.info('%r', argv)
                subprocess.run(argv, check=True)
                self.write_lookaside(
                    os.path.join(self.cache, runtime.tarball),
                    dest,
                )

                if runtime.include_sdk:
                    if self.versioned_directories:
                        sdk_subdir = '{}_sdk_{}'.format(runtime.name, version)
                    else:
                        sdk_subdir = '{}_sdk'.format(runtime.name)

                    dest = os.path.join(self.depot, sdk_subdir)
                    runtime_files.add(sdk_subdir + '/')

                    with suppress(FileNotFoundError):
                        shutil.rmtree(os.path.join(dest, 'files'))

                    with suppress(FileNotFoundError):
                        os.remove(os.path.join(dest, 'metadata'))

                    os.makedirs(
                        os.path.join(dest, 'files', 'lib', 'debug'),
                        exist_ok=True,
                    )
                    argv = [
                        'tar',
                        '-C', dest,
                        '-xf', os.path.join(self.cache, runtime.sdk_tarball),
                    ]
                    logger.info('%r', argv)
                    subprocess.run(argv, check=True)
                    self.write_lookaside(
                        os.path.join(self.cache, runtime.sdk_tarball),
                        dest,
                    )

                    argv = [
                        'tar',
                        '-C', os.path.join(dest, 'files', 'lib', 'debug'),
                        '--transform', r's,^\(\./\)\?files\(/\|$\),,',
                        '-xf',
                        os.path.join(self.cache, runtime.debug_tarball),
                    ]
                    logger.info('%r', argv)
                    subprocess.run(argv, check=True)

                    if self.versioned_directories:
                        sysroot_subdir = '{}_sysroot_{}'.format(
                            runtime.name, version,
                        )
                    else:
                        sysroot_subdir = '{}_sysroot'.format(runtime.name)

                    sysroot = os.path.join(self.depot, sysroot_subdir)
                    runtime_files.add(sysroot_subdir + '/')

                    with suppress(FileNotFoundError):
                        shutil.rmtree(sysroot)

                    os.makedirs(os.path.join(sysroot, 'files'), exist_ok=True)
                    argv = [
                        'tar',
                        '-C', os.path.join(sysroot, 'files'),
                        '--exclude', 'dev/*',
                        '-xf',
                        os.path.join(self.cache, runtime.sysroot_tarball),
                    ]
                    logger.info('%r', argv)
                    subprocess.run(argv, check=True)
                    argv = [
                        'cp',
                        '-al',
                        os.path.join(dest, 'files', 'lib', 'debug'),
                        os.path.join(sysroot, 'files', 'usr', 'lib'),
                    ]
                    logger.info('%r', argv)
                    subprocess.run(argv, check=True)

            with open(
                os.path.join(self.depot, 'run-in-' + runtime.name), 'w'
            ) as writer:
                if self.unpack_runtimes:
                    writer.write(
                        RUN_IN_DIR_SOURCE.format(
                            escaped_dir=shlex.quote(subdir),
                            source_for_generated_file=(
                                'Generated file, do not edit'
                            ),
                        )
                    )
                else:
                    writer.write(
                        RUN_IN_ARCHIVE_SOURCE.format(
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

            comment = ', '.join(sorted(runtime_files))

            if runtime.path:
                comment += ' (from local build)'

            component_version.version = version
            component_version.runtime = runtime.suite
            component_version.runtime_version = version
            component_version.comment = comment
            self.versions.append(component_version)

        for runtime in self.runtimes[0:]:
            if not self.toolmanifest:
                continue

            with open(
                os.path.join(self.depot, 'toolmanifest.v2.vdf'), 'w'
            ) as writer:
                import vdf      # noqa

                writer.write('// Generated file, do not edit\n')
                words = [
                    '/_v2-entry-point',
                    '--verb=%verb%',
                    '--',
                ]
                content = dict(
                    manifest=dict(
                        commandline=' '.join(words),
                        version='2',
                        use_tool_subprocess_reaper='1',
                    )
                )       # type: Dict[str, Any]
                if runtime.suite == 'soldier':
                    content['manifest']['unlisted'] = '1'
                vdf.dump(content, writer, pretty=True, escaped=True)

            shutil.copy2(
                os.path.join(self.depot, 'run-in-' + runtime.name),
                os.path.join(self.depot, 'run'),
            )
            os.chmod(os.path.join(self.depot, 'run'), 0o755)

            if self.toolmanifest:
                shutil.copy2(
                    os.path.join(self.depot, 'toolmanifest.v2.vdf'),
                    os.path.join(self.depot, 'toolmanifest.vdf'),
                )

        try:
            with subprocess.Popen(
                [
                    'git', 'describe',
                    '--always',
                    '--dirty',
                    '--long',
                ],
                cwd=os.path.dirname(__file__),
                stdout=subprocess.PIPE,
                universal_newlines=True,
            ) as describe:
                stdout = describe.stdout
                assert stdout is not None
                version = stdout.read().strip()
                # Deliberately ignoring exit status:
                # if git is missing or old we'll use 'unknown'
        except (OSError, subprocess.SubprocessError):
            version = 'unknown'

        component_version = ComponentVersion('SteamLinuxRuntime')
        component_version.version = version
        component_version.comment = 'Entry point scripts, etc.'
        self.versions.append(component_version)

        with open(os.path.join(self.depot, 'VERSIONS.txt'), 'w') as writer:
            writer.write(
                '#Name\tVersion\t\tRuntime\tRuntime_Version\tComment\n'
            )

            for entry in sorted(self.versions, key=lambda v: v.to_tsv()):
                logger.info('Component version: %s', entry)
                writer.write(entry.to_tsv())

    def use_local_pressure_vessel(self, path: str = '.') -> None:
        pv_dir = os.path.join(self.depot, 'pressure-vessel')
        os.makedirs(pv_dir, exist_ok=True)
        argv = ['tar', '-C', pv_dir, '--strip-components=1', '-xf']

        if os.path.isfile(path):
            argv.append(path)
        else:
            argv.append(os.path.join(path, 'pressure-vessel-bin.tar.gz'))

        logger.info('%r', argv)
        subprocess.run(argv, check=True)

    def download_pressure_vessel(self, runtime: Runtime) -> str:
        filename = 'pressure-vessel-bin.tar.gz'
        runtime.pin_version(self.opener)

        downloaded = runtime.fetch(
            filename,
            self.opener,
        )

        os.makedirs(self.depot, exist_ok=True)
        subprocess.run(
            [
                'tar', '-C', self.depot, '-xf', downloaded,
            ],
            check=True,
        )

        return filename

    def use_local_runtime(self, runtime: Runtime) -> None:
        assert runtime.path

        for basename in runtime.runtime_files:
            src = os.path.join(runtime.path, basename)
            dest = os.path.join(self.cache, basename)
            logger.info('Hard-linking local runtime %r to %r', src, dest)

            with suppress(FileNotFoundError):
                os.unlink(dest)

            os.link(src, dest)

            if self.include_archives:
                dest = os.path.join(self.depot, basename)
                logger.info('Hard-linking local runtime %r to %r', src, dest)

                with suppress(FileNotFoundError):
                    os.unlink(dest)

                os.link(src, dest)

        if self.include_archives:
            with open(
                os.path.join(self.depot, runtime.build_id_file), 'w',
            ) as writer:
                writer.write(f'{runtime.version}\n')

            if runtime.include_sdk:
                with open(
                    os.path.join(self.depot, runtime.sdk_build_id_file), 'w',
                ) as writer:
                    writer.write(f'{runtime.version}\n')

        if self.unpack_sources:
            with open(
                os.path.join(runtime.path, runtime.sources), 'rb',
            ) as reader:
                for stanza in Sources.iter_paragraphs(
                    sequence=reader,
                    use_apt_pkg=True,
                ):
                    if stanza['package'] in self.unpack_sources:
                        for f in stanza['files']:
                            name = f['name']

                            if name.endswith('.dsc'):
                                dest = os.path.join(
                                    self.unpack_sources_into,
                                    runtime.name,
                                    stanza['package'],
                                )

                                with suppress(FileNotFoundError):
                                    logger.info('Removing %r', dest)
                                    shutil.rmtree(dest)

                                subprocess.run(
                                    [
                                        'dpkg-source',
                                        '-x',
                                        dest,
                                        os.path.join(
                                            runtime.path,
                                            'sources',
                                            f['name'],
                                        ),
                                    ],
                                    check=True,
                                )

    def download_runtime(self, runtime: Runtime) -> None:
        """
        Download a pre-prepared Platform from a previous container
        runtime build.
        """

        pinned = runtime.pin_version(self.opener)

        for basename in runtime.runtime_files:
            downloaded = runtime.fetch(basename, self.opener)

            if self.include_archives:
                os.link(downloaded, os.path.join(self.depot, basename))

        if self.include_archives:
            with open(
                os.path.join(self.depot, runtime.build_id_file), 'w',
            ) as writer:
                writer.write(f'{pinned}\n')

            if runtime.include_sdk:
                with open(
                    os.path.join(self.depot, runtime.sdk_build_id_file), 'w',
                ) as writer:
                    writer.write(f'{pinned}\n')

        if self.unpack_sources:
            with tempfile.TemporaryDirectory(prefix='populate-depot.') as tmp:
                want = set(self.unpack_sources)
                downloaded = runtime.fetch(
                    runtime.sources,
                    self.opener,
                )
                with open(downloaded, 'rb') as reader:
                    for stanza in Sources.iter_paragraphs(
                        sequence=reader,
                        use_apt_pkg=True,
                    ):
                        if stanza['package'] in self.unpack_sources:
                            logger.info(
                                'Found %s in %s',
                                stanza['package'], runtime.name,
                            )
                            want.discard(stanza['package'])
                            os.makedirs(
                                os.path.join(self.cache or tmp, 'sources'),
                                exist_ok=True,
                            )

                            file_path = {}    # type: Dict[str, str]

                            for f in stanza['files']:
                                name = f['name']
                                file_path[name] = runtime.fetch(
                                    os.path.join('sources', name),
                                    self.opener,
                                )

                            for f in stanza['files']:
                                name = f['name']

                                if name.endswith('.dsc'):
                                    dest = os.path.join(
                                        self.unpack_sources_into,
                                        runtime.name,
                                        stanza['package'],
                                    )

                                    with suppress(FileNotFoundError):
                                        logger.info('Removing %r', dest)
                                        shutil.rmtree(dest)

                                    subprocess.run(
                                        [
                                            'dpkg-source',
                                            '-x',
                                            file_path[name],
                                            dest,
                                        ],
                                        check=True,
                                    )

                if want:
                    logger.warning(
                        'Did not find source package(s) %s in %s',
                        ', '.join(want), runtime.name,
                    )

    def download_scout_tarball(self, runtime: Runtime) -> None:
        """
        Download a pre-prepared LD_LIBRARY_PATH Steam Runtime from a
        previous scout build.
        """
        filename = 'steam-runtime.tar.xz'

        pinned = runtime.pin_version(self.opener)
        logger.info('Downloading steam-runtime build %s', pinned)
        os.makedirs(self.unpack_ld_library_path, exist_ok=True)

        downloaded = runtime.fetch(
            filename,
            self.opener,
        )
        subprocess.run(
            [
                'tar', '-C', self.unpack_ld_library_path, '-xf',
                downloaded,
            ],
            check=True,
        )

    def octal_escape_char(self, match: 're.Match') -> str:
        ret = []    # type: List[str]

        for byte in match.group(0).encode('utf-8', 'surrogateescape'):
            ret.append('\\%03o' % byte)

        return ''.join(ret)

    _NEEDS_OCTAL_ESCAPE = re.compile(r'[^-A-Za-z0-9+,./:@_]')

    def octal_escape(self, s: str) -> str:
        return self._NEEDS_OCTAL_ESCAPE.sub(self.octal_escape_char, s)

    def filename_is_windows_friendly(self, s: str) -> bool:
        for c in s:
            # This is the set of characters that are reserved in Windows
            # filenames, excluding '/' which obviously we're fine with
            # using as a directory separator.
            if c in r'<>:"\|?*':
                return False

            if c >= '\uDC80' and c <= '\uDCFF':
                # surrogate escape, not Unicode
                return False

        return True

    def write_lookaside(self, archive: str, dest: str) -> None:
        with tarfile.open(
            archive, mode='r'
        ) as unarchiver, gzip.open(
            os.path.join(dest, 'usr-mtree.txt.gz'), 'wt'
        ) as writer:
            lc_names = {}                   # type: Dict[str, str]
            differ_only_by_case = set()     # type: Set[str]
            not_windows_friendly = set()    # type: Set[str]
            sha256 = {}                     # type: Dict[str, str]
            sizes = {}                      # type: Dict[str, int]

            writer.write('#mtree\n')
            writer.write('. type=dir\n')

            for member in unarchiver:
                name = member.name

                if name.startswith('./'):
                    name = name[len('./'):]

                if not name.startswith('files/'):
                    continue

                name = name[len('files/'):]

                if not self.filename_is_windows_friendly(name):
                    not_windows_friendly.add(name)

                if name.lower() in lc_names:
                    differ_only_by_case.add(lc_names[name.lower()])
                    differ_only_by_case.add(name)
                else:
                    lc_names[name.lower()] = name

                fields = ['./' + self.octal_escape(name)]

                if member.isfile() or member.islnk():
                    fields.append('type=file')
                    fields.append('mode=%o' % member.mode)
                    fields.append('time=%d' % member.mtime)

                    if member.islnk():
                        writer.write(
                            '# hard link to {}\n'.format(
                                self.octal_escape(member.linkname),
                            ),
                        )
                        assert member.linkname in sizes
                        fields.append('size=%d' % sizes[member.linkname])

                        if sizes[member.linkname] > 0:
                            assert member.linkname in sha256
                            fields.append('sha256=' + sha256[member.linkname])
                    else:
                        fields.append('size=%d' % member.size)
                        sizes[member.name] = member.size

                        if member.size > 0:
                            hasher = hashlib.sha256()
                            extract = unarchiver.extractfile(member)
                            assert extract is not None
                            with extract:
                                while True:
                                    blob = extract.read(4096)

                                    if not blob:
                                        break

                                    hasher.update(blob)

                            fields.append('sha256=' + hasher.hexdigest())
                            sha256[member.name] = hasher.hexdigest()

                elif member.issym():
                    fields.append('type=link')
                    fields.append('link=' + self.octal_escape(member.linkname))
                elif member.isdir():
                    fields.append('type=dir')
                else:
                    writer.write(
                        '# unknown file type: {}\n'.format(
                            self.octal_escape(member.name),
                        ),
                    )
                    continue

                writer.write(' '.join(fields) + '\n')

            if differ_only_by_case:
                writer.write('\n')
                writer.write('# Files whose names differ only by case:\n')

                for name in sorted(differ_only_by_case):
                    writer.write('# {}\n'.format(self.octal_escape(name)))

            if not_windows_friendly:
                writer.write('\n')
                writer.write('# Files whose names are not Windows-friendly:\n')

                for name in sorted(not_windows_friendly):
                    writer.write('# {}\n'.format(self.octal_escape(name)))


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
        '--cache', default='.cache',
        help=(
            'Cache downloaded files that are not in --depot here'
        ),
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
        '--credential-host',
        action='append',
        default=[],
        dest='credential_hosts',
        metavar='HOST',
        help=(
            'Use --credential-env when downloading from the given HOST'
            '(default: hostname of --images-uri)'
        ),
    )
    parser.add_argument(
        '--images-uri',
        default=DEFAULT_IMAGES_URI,
        metavar='URI',
        help=(
            'Download files from the given URI. '
            '"SUITE" will be replaced with the suite name.'
        ),
    )

    parser.add_argument(
        '--ssh-host', default='', metavar='HOST',
        help='Use ssh and rsync to download files from HOST',
    )
    parser.add_argument(
        '--ssh-path', default='', metavar='PATH',
        help=(
            'Use ssh and rsync to download files from PATH on HOST. '
            '"SUITE" will be replaced with the suite name.'
        ),
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
        '--include-archives', action='store_true', default=False,
        help=(
            'Provide the runtime as an archive to be unpacked'
        )
    )
    parser.add_argument(
        '--no-include-archives', action='store_false', dest='include_archives',
        help=(
            'Do not provide the runtime as an archive to be unpacked '
            '[default]'
        )
    )
    parser.add_argument(
        '--include-sdk', default=False, action='store_true',
        help='Include a corresponding SDK',
    )
    parser.add_argument(
        '--source-dir', default=HERE,
        help=(
            'Source directory for files to include in the depot'
        )
    )
    parser.add_argument(
        '--toolmanifest', default=False, action='store_true',
        help='Generate toolmanifest.vdf',
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
        '--unpack-runtimes', action='store_true', default=True,
        help=(
            "Unpack the runtimes into the --depot, for use with "
            "pressure-vessel's tests/containers.py. [default]"
        )
    )
    parser.add_argument(
        '--no-unpack-runtimes', action='store_false', dest='unpack_runtimes',
        help=(
            "Don't unpack the runtimes into the --depot"
        )
    )
    parser.add_argument(
        '--unpack-source', metavar='PACKAGE', action='append', default=[],
        dest='unpack_sources',
        help=(
            'Download and unpack the given source package from each runtime '
            'if it exists, for use in regression testing. May be repeated.'
        )
    )
    parser.add_argument(
        '--unpack-sources-into', metavar='PATH', default='.',
        help=(
            'Unpack any source packages specified by --unpack-source '
            'into PATH/RUNTIME/SOURCE (default: ./RUNTIME/SOURCE).'
        )
    )
    parser.add_argument(
        '--versioned-directories', action='store_true', default=False,
        help=(
            'Include version number in unpacked runtime directories'
        )
    )
    parser.add_argument(
        '--no-versioned-directories', action='store_false',
        dest='versioned_directories',
        help=(
            'Do not include version number in unpacked runtime directories '
            '[default]'
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
