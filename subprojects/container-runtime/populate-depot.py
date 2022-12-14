#!/usr/bin/env python3

# Copyright © 2019-2022 Collabora Ltd.
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

The oldest distribution we are currently testing with the CI is Ubuntu
18.04, that is shipping with Python 3.6.5.
In order to keep the compatibility with Ubuntu 18.04, this Python script
should not require a Python version newer than the 3.6.
"""

import argparse
import errno
import gzip
import hashlib
import json
import logging
import os
import re
import shlex
import shutil
import stat
import subprocess
import tempfile
import urllib.parse
import urllib.request
from contextlib import suppress
from pathlib import Path
from typing import (
    Any,
    Dict,
    List,
    Optional,
    Sequence,
    Set,
    Tuple,
)

from debian.deb822 import (
    Sources,
)


HERE = Path(__file__).resolve().parent


logger = logging.getLogger('populate-depot')


DEFAULT_PRESSURE_VESSEL_URI = (
    'https://repo.steampowered.com/pressure-vessel/snapshots'
)

DEFAULT_IMAGES_URI = (
    'https://repo.steampowered.com/steamrt-images-SUITE/snapshots'
)


class InvocationError(Exception):
    pass


class PressureVesselRelease:
    def __init__(
        self,
        *,
        cache: str = '.cache',
        ssh_host: str = '',
        ssh_path: str = '',
        uri: str = DEFAULT_PRESSURE_VESSEL_URI,
        version: str = ''
    ) -> None:
        self.cache = cache
        self.pinned_version = None      # type: Optional[str]
        self.ssh_host = ssh_host
        self.ssh_path = ssh_path
        self.uri = uri
        self.version = version

    def get_uri(
        self,
        filename: str,
        version: Optional[str] = None,
    ) -> str:
        uri = self.uri
        v = version or self.pinned_version or self.version or 'latest'
        return f'{uri}/{v}/{filename}'

    def get_ssh_path(
        self,
        filename: str,
        version: Optional[str] = None,
    ) -> str:
        ssh_host = self.ssh_host
        ssh_path = self.ssh_path
        v = version or self.pinned_version or self.version or 'latest'

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

        if pinned is None:
            if self.ssh_host and self.ssh_path:
                path = self.get_ssh_path(filename='VERSION.txt')
                logger.info('Determining version number from %r...', path)
                pinned = subprocess.run([
                    'ssh', self.ssh_host,
                    'cat {}'.format(shlex.quote(path)),
                ], stdout=subprocess.PIPE).stdout.decode('utf-8').strip()
            else:
                uri = self.get_uri(filename='VERSION.txt')
                logger.info('Determining version number from %r...', uri)
                with opener.open(uri) as response:
                    pinned = response.read().decode('utf-8').strip()

            self.pinned_version = pinned

        return pinned


class Runtime:
    def __init__(
        self,
        name,
        *,
        suite: str,

        architecture: str = 'amd64,i386',
        cache: str = '.cache',
        images_uri: str = DEFAULT_IMAGES_URI,
        path: Optional[str] = None,
        ssh_host: str = '',
        ssh_path: str = '',
        version: str = '',
    ) -> None:
        self.architecture = architecture
        self.cache = cache
        self.images_uri = images_uri
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

    def get_archives(
        self,
        include_sdk_debug=False,
        include_sdk_runtime=False,
        include_sdk_sysroot=False,
    ):
        archives = [self.tarball]

        if include_sdk_debug:
            archives.append(self.debug_tarball)

        if include_sdk_sysroot:
            archives.append(self.dockerfile)
            archives.append(self.sysroot_tarball)

        if include_sdk_runtime:
            archives.append(self.sdk_tarball)

        return archives

    def __str__(self) -> str:
        return self.name

    @classmethod
    def from_details(
        cls,
        name: str,
        details: Dict[str, Any],
        cache: str = '.cache',
        default_architecture: str = 'amd64,i386',
        default_suite: str = '',
        default_version: str = '',
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
        v = version or self.pinned_version or self.version or 'latest'
        return f'{uri}/{v}/{filename}'

    def get_ssh_path(
        self,
        filename: str,
        version: Optional[str] = None,
    ) -> str:
        ssh_host = self.ssh_host
        suite = self.suite
        ssh_path = self.ssh_path.replace('SUITE', suite)
        v = version or self.pinned_version or self.version or 'latest'

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

export PRESSURE_VESSEL_COPY_RUNTIME=1
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

export PRESSURE_VESSEL_COPY_RUNTIME=1
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
    def __init__(
        self,
        name: str = '',
        sort_weight: int = 0,
    ) -> None:
        self.name = name
        self.version = ''
        self.runtime = ''
        self.runtime_version = ''
        self.sort_weight = sort_weight
        self.comment = ''

    def __str__(self) -> str:
        ret = '{} version {!r}'.format(self.name, self.version)

        if self.runtime or self.runtime_version:
            ret = ret + ' (from {} version {})'.format(
                self.runtime or '(unknown runtime)',
                self.runtime_version or '(unknown)',
            )

        return ret

    def to_sort_key(self) -> Tuple[int, str]:
        return (self.sort_weight, self.to_tsv())

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
        depot_version: str = '',
        images_uri: str = DEFAULT_IMAGES_URI,
        include_archives: bool = False,
        include_sdk_debug: bool = False,
        include_sdk_runtime: bool = False,
        include_sdk_sysroot: bool = False,
        layered: bool = False,
        minimize: bool = False,
        pressure_vessel_archive: str = '',
        pressure_vessel_from_runtime: str = '',
        pressure_vessel_from_runtime_json: str = '',
        pressure_vessel_guess: str = '',
        pressure_vessel_ssh_host: str = '',
        pressure_vessel_ssh_path: str = '',
        pressure_vessel_uri: str = DEFAULT_PRESSURE_VESSEL_URI,
        pressure_vessel_version: str = '',
        runtime: str = 'scout',
        source_dir: str = str(HERE),
        ssh_host: str = '',
        ssh_path: str = '',
        steam_app_id: str = '',
        suite: str = '',
        toolmanifest: bool = False,
        unpack_ld_library_path: str = '',
        unpack_runtime: bool = True,
        unpack_sources: Sequence[str] = (),
        unpack_sources_into: str = '.',
        version: str = '',
        versioned_directories: bool = False,
        **kwargs: Dict[str, Any],
    ) -> None:
        openers: List[urllib.request.BaseHandler] = []

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
        self.default_suite = suite
        self.default_version = version
        self.depot = os.path.abspath(depot)
        self.depot_version = depot_version
        self.images_uri = images_uri
        self.include_archives = include_archives
        self.include_sdk_debug = include_sdk_debug
        self.include_sdk_runtime = include_sdk_runtime
        self.include_sdk_sysroot = include_sdk_sysroot
        self.layered = layered
        self.minimize = minimize
        self.pressure_vessel_ssh_host = pressure_vessel_ssh_host or ssh_host
        self.pressure_vessel_ssh_path = pressure_vessel_ssh_path
        self.pressure_vessel_uri = pressure_vessel_uri
        self.source_dir = source_dir
        self.ssh_host = ssh_host
        self.ssh_path = ssh_path
        self.steam_app_id = steam_app_id
        self.toolmanifest = toolmanifest
        self.unpack_ld_library_path = unpack_ld_library_path
        self.unpack_runtime = unpack_runtime
        self.unpack_sources = unpack_sources
        self.unpack_sources_into = unpack_sources_into
        self.versioned_directories = versioned_directories

        n_sources = 0

        for source in (
            pressure_vessel_archive,
            pressure_vessel_from_runtime,
            pressure_vessel_from_runtime_json,
            pressure_vessel_guess,
            pressure_vessel_version,
        ):
            if source:
                n_sources += 1

        if n_sources == 0:
            pressure_vessel_version = 'latest'
        elif n_sources > 1:
            raise RuntimeError(
                'Cannot combine more than one of '
                '--pressure-vessel, '
                '--pressure-vessel-archive, '
                '--pressure-vessel-from-runtime, '
                '--pressure-vessel-from-runtime-json and '
                '--pressure-vessel-version'
            )

        os.makedirs(self.cache, exist_ok=True)

        if not (self.include_archives or self.unpack_runtime):
            raise RuntimeError(
                'Cannot use both --no-include-archives and '
                '--no-unpack-runtime'
            )

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

        self.runtime = self.new_runtime(name, details)

        self.versions = []      # type: List[ComponentVersion]

        if pressure_vessel_guess:
            if self.runtime.name == pressure_vessel_guess:
                pressure_vessel_from_runtime = pressure_vessel_guess
            elif pressure_vessel_guess.startswith('{'):
                pressure_vessel_from_runtime_json = pressure_vessel_guess
            elif os.path.isdir(pressure_vessel_guess):
                pressure_vessel_archive = os.path.join(
                    pressure_vessel_guess, 'pressure-vessel-bin.tar.gz',
                )
            elif (
                os.path.isfile(pressure_vessel_guess)
                and pressure_vessel_guess.endswith('.tar.gz')
            ):
                pressure_vessel_archive = pressure_vessel_guess
            else:
                pressure_vessel_from_runtime = pressure_vessel_guess

        self.pressure_vessel_runtime = None     # type: Optional[Runtime]
        self.pressure_vessel_version = ''

        if pressure_vessel_version:
            self.pressure_vessel_version = pressure_vessel_version
        elif pressure_vessel_archive:
            self.pressure_vessel_runtime = self.new_runtime(
                'scout',
                {'path': pressure_vessel_archive},
                default_suite='local',
            )
        elif self.runtime.name == pressure_vessel_from_runtime:
            self.pressure_vessel_runtime = self.runtime
        elif pressure_vessel_from_runtime:
            self.pressure_vessel_runtime = self.new_runtime(
                pressure_vessel_from_runtime, {},
                default_suite=pressure_vessel_from_runtime,
            )
        elif pressure_vessel_from_runtime_json:
            self.pressure_vessel_runtime = self.new_runtime(
                'scout',
                json.loads(pressure_vessel_from_runtime_json),
                default_suite='scout',
            )

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
        if self.layered:
            self.do_layered_runtime()
        else:
            self.do_container_runtime()

    def do_layered_runtime(self) -> None:
        if self.runtime.name != 'scout':
            raise InvocationError('Can only layer scout onto soldier')

        if self.unpack_ld_library_path:
            raise InvocationError(
                'Cannot use --unpack-ld-library-path with --layered'
            )

        if self.include_archives:
            raise InvocationError(
                'Cannot use --include-archives with --layered'
            )

        if (
            self.include_sdk_debug
            or self.include_sdk_runtime
            or self.include_sdk_sysroot
        ):
            raise InvocationError(
                'Cannot use --include-sdk-* with --layered'
            )

        if self.unpack_sources:
            raise InvocationError(
                'Cannot use --unpack-source with --layered'
            )

        self.merge_dir_into_depot(
            os.path.join(self.source_dir, 'runtimes', 'scout-on-soldier')
        )

        if self.runtime.version:
            self.unpack_ld_library_path = self.depot
            self.download_scout_tarball(self.runtime)
            local_version = ComponentVersion('LD_LIBRARY_PATH')
            version = self.runtime.pinned_version
            assert version is not None
            local_version.version = version
            local_version.runtime = 'scout'
            local_version.runtime_version = version
            local_version.comment = 'steam-runtime/'
            self.versions.append(local_version)
        else:
            unspecified_version = ComponentVersion('LD_LIBRARY_PATH')
            unspecified_version.version = '-'
            unspecified_version.runtime = 'scout'
            unspecified_version.runtime_version = '-'
            unspecified_version.comment = (
                'see ~/.steam/root/ubuntu12_32/steam-runtime/version.txt'
            )
            self.versions.append(unspecified_version)

        self.write_component_versions()

    def ensure_ref(self, path: str) -> None:
        '''
        Create $path/files/.ref as an empty regular file.

        This is useful because pressure-vessel would create this file
        during processing. If it gets committed to the depot, then Steampipe
        will remove it when superseded.
        '''
        ref = os.path.join(path, 'files', '.ref')

        try:
            statinfo = os.stat(ref, follow_symlinks=False)
        except FileNotFoundError:
            with open(ref, 'x'):
                pass
        else:
            if statinfo.st_size > 0 or not stat.S_ISREG(statinfo.st_mode):
                raise RuntimeError(
                    'Expected {} to be an empty regular file'.format(path)
                )

    @staticmethod
    def prune_runtime(directory: Path) -> None:
        """
        Remove files that are considered to be unnecessary
        """

        usr_share = directory / 'files' / 'share'
        doc = usr_share / 'doc'

        # This is a fairly generic list of files that are safe to be removed.
        # Please keep it in sync with prune_files() of steam-runtime.git's
        # build-runtime.py
        paths: list[Path] = [
            # Nvidia cg toolkit manuals, tutorials and documentation
            doc / 'nvidia-cg-toolkit' / 'html',
            *doc.glob('nvidia-cg-toolkit/*.pdf.gz'),
            # Sample code
            *doc.glob('**/examples'),
            # Debian bug reporting scripts
            usr_share / 'bug',
            # Debian documentation metadata
            usr_share / 'doc-base',
            # Debian QA metadata
            usr_share / 'lintian',
            # Programs and utilities manuals
            usr_share / 'man',
            # Remove the localized messages that are likely never going to be
            # seen. Keep only "en", because that's the default language we are
            # using.
            *[x for x in usr_share.glob('locale/*') if x.name != 'en'],
        ]

        for path in paths:
            if path.is_dir():
                shutil.rmtree(path)
            else:
                with suppress(FileNotFoundError):
                    path.unlink()

    def do_container_runtime(self) -> None:
        pv_version = ComponentVersion('pressure-vessel')

        self.merge_dir_into_depot(os.path.join(self.source_dir, 'common'))

        root = os.path.join(self.source_dir, 'runtimes', self.runtime.name)

        if os.path.exists(root):
            self.merge_dir_into_depot(root)

        pressure_vessel_runtime = self.pressure_vessel_runtime

        if self.pressure_vessel_version:
            logger.info(
                'Downloading standalone pressure-vessel release'
            )
            pv_version.version = self.download_pressure_vessel_standalone(
                self.pressure_vessel_version
            )
        else:
            assert pressure_vessel_runtime is not None

            if pressure_vessel_runtime.path:
                self.use_local_pressure_vessel(pressure_vessel_runtime.path)
                pv_version.comment = 'from local file'
            else:
                pv_version.comment = (
                    self.download_pressure_vessel_from_runtime(
                        pressure_vessel_runtime
                    )
                )

        for path in ('metadata/VERSION.txt', 'sources/VERSION.txt'):
            full = os.path.join(self.depot, 'pressure-vessel', path)
            if os.path.exists(full):
                with open(full) as text_reader:
                    v = text_reader.read().rstrip('\n')
                    if pv_version.version:
                        if pv_version.version != v:
                            raise RuntimeError(
                                'Inconsistent version! '
                                '{} says {}, but expected {}'.format(
                                    path, v, pv_version.version,
                                )
                            )
                    else:
                        pv_version.version = v

                break

        if pressure_vessel_runtime is not None:
            pv_version.runtime = pressure_vessel_runtime.suite or ''
            pv_version.runtime_version = (
                pressure_vessel_runtime.pinned_version or ''
            )

        self.versions.append(pv_version)

        if self.unpack_ld_library_path and pressure_vessel_runtime is None:
            if self.runtime.name == 'scout':
                scout = self.runtime
            else:
                scout = self.new_runtime(
                    'scout',
                    dict(version='latest'),
                    default_suite='scout',
                )
            logger.info(
                'Downloading LD_LIBRARY_PATH Steam Runtime from scout into %r',
                self.unpack_ld_library_path)
            self.download_scout_tarball(scout)
        elif self.unpack_ld_library_path:
            assert pressure_vessel_runtime is not None
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

            os.makedirs(
                os.path.join(self.unpack_sources_into, self.runtime.name),
                exist_ok=True,
            )

        for runtime in (self.runtime,):     # too much to reindent right now
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
                runtime_files = set(
                    runtime.get_archives(
                        include_sdk_debug=self.include_sdk_debug,
                        include_sdk_runtime=self.include_sdk_runtime,
                        include_sdk_sysroot=self.include_sdk_sysroot,
                    )
                )
            else:
                runtime_files = set()

            if self.unpack_runtime:
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
                self.prune_runtime(Path(dest))
                self.write_lookaside(dest)

                if self.minimize:
                    self.minimize_runtime(dest)

                self.ensure_ref(dest)

                if self.include_sdk_runtime:
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
                    self.prune_runtime(Path(dest))
                    self.write_lookaside(dest)

                    if self.minimize:
                        self.minimize_runtime(dest)

                    self.ensure_ref(dest)

                    if self.include_sdk_debug:
                        argv = [
                            'tar',
                            '-C', os.path.join(dest, 'files', 'lib', 'debug'),
                            '--transform', r's,^\(\./\)\?files\(/\|$\),,',
                            '-xf',
                            os.path.join(self.cache, runtime.debug_tarball),
                        ]
                        logger.info('%r', argv)
                        subprocess.run(argv, check=True)

                if self.include_sdk_sysroot:
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

                    os.makedirs(
                        os.path.join(
                            sysroot, 'files', 'usr', 'lib', 'debug',
                        ),
                        exist_ok=True,
                    )

                    if self.include_sdk_debug:
                        if self.include_sdk_sysroot:
                            argv = [
                                'cp',
                                '-al',
                                os.path.join(dest, 'files', 'lib', 'debug'),
                                os.path.join(sysroot, 'files', 'usr', 'lib'),
                            ]
                            logger.info('%r', argv)
                            subprocess.run(argv, check=True)
                        else:
                            argv = [
                                'tar',
                                '-C', os.path.join(
                                    sysroot, 'files', 'usr', 'lib', 'debug'
                                ),
                                '--transform', r's,^\(\./\)\?files\(/\|$\),,',
                                '-xf',
                                os.path.join(
                                    self.cache, runtime.debug_tarball,
                                ),
                            ]
                            logger.info('%r', argv)
                            subprocess.run(argv, check=True)

            with open(
                os.path.join(self.depot, 'run-in-' + runtime.name), 'w'
            ) as writer:
                if self.unpack_runtime:
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

        for runtime in (self.runtime,):     # too much to reindent right now
            if not self.toolmanifest:
                continue

            with open(
                os.path.join(self.depot, 'toolmanifest.vdf'), 'w'
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

                if runtime.suite != 'scout':
                    content['manifest']['unlisted'] = '1'

                content['manifest']['compatmanager_layer_name'] = (
                    'container-runtime'
                )

                vdf.dump(content, writer, pretty=True, escaped=True)

            shutil.copy2(
                os.path.join(self.depot, 'run-in-' + runtime.name),
                os.path.join(self.depot, 'run'),
            )
            os.chmod(os.path.join(self.depot, 'run'), 0o755)

        self.write_component_versions()

    def write_component_versions(self) -> None:
        try:
            with open(HERE / '.tarball-version', 'r') as reader:
                version = reader.read().strip()
        except OSError:
            version = 'unknown'

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
                version = stdout.read().strip() or version
                # Deliberately ignoring exit status:
                # if git is missing or old we'll use 'unknown'
        except (OSError, subprocess.SubprocessError):
            pass

        if self.depot_version:
            component_version = ComponentVersion('depot', sort_weight=-1)
            component_version.version = self.depot_version
            component_version.comment = 'Overall version number'
            self.versions.append(component_version)

        component_version = ComponentVersion('scripts')
        component_version.version = version
        component_version.comment = 'Entry point scripts, etc.'
        self.versions.append(component_version)

        with open(os.path.join(self.depot, 'VERSIONS.txt'), 'w') as writer:
            writer.write(
                '#Name\tVersion\t\tRuntime\tRuntime_Version\tComment\n'
            )

            for entry in sorted(self.versions, key=lambda v: v.to_sort_key()):
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

    def download_pressure_vessel_standalone(
        self,
        version: str,
    ) -> str:
        pv = PressureVesselRelease(
            cache=self.cache,
            ssh_host=self.pressure_vessel_ssh_host,
            ssh_path=self.pressure_vessel_ssh_path,
            uri=self.pressure_vessel_uri,
            version=version,
        )
        pinned = pv.pin_version(self.opener)
        self.use_local_pressure_vessel(
            pv.fetch('pressure-vessel-bin.tar.gz', self.opener, pinned)
        )
        return pinned

    def download_pressure_vessel_from_runtime(self, runtime: Runtime) -> str:
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

        for basename in runtime.get_archives(
            include_sdk_debug=self.include_sdk_debug,
            include_sdk_runtime=self.include_sdk_runtime,
            include_sdk_sysroot=self.include_sdk_sysroot,
        ):
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

            if self.include_sdk_runtime or self.include_sdk_sysroot:
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

        for basename in runtime.get_archives(
            include_sdk_debug=self.include_sdk_debug,
            include_sdk_runtime=self.include_sdk_runtime,
            include_sdk_sysroot=self.include_sdk_sysroot,
        ):
            downloaded = runtime.fetch(basename, self.opener)

            if self.include_archives:
                dest = os.path.join(self.depot, basename)

                with suppress(FileNotFoundError):
                    os.unlink(dest)

                os.link(downloaded, dest)

        if self.include_archives:
            with open(
                os.path.join(self.depot, runtime.build_id_file), 'w',
            ) as writer:
                writer.write(f'{pinned}\n')

            if self.include_sdk_runtime or self.include_sdk_sysroot:
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

    def write_lookaside(self, runtime: str) -> None:
        with tempfile.TemporaryDirectory(prefix='slr-mtree-') as temp:
            lc_names = {}                   # type: Dict[str, str]
            differ_only_by_case = set()     # type: Set[str]
            not_windows_friendly = set()    # type: Set[str]
            sha256 = {}                     # type: Dict[Tuple[int, int], str]
            paths = {}                      # type: Dict[Tuple[int, int], str]

            writer = gzip.open(os.path.join(temp, 'usr-mtree.txt.gz'), 'wt')

            writer.write('#mtree\n')
            writer.write('. type=dir\n')

            for member in Path(runtime).rglob("*"):
                relative_path = member.relative_to(runtime)

                try:
                    name = str(relative_path.relative_to('files'))
                except ValueError:
                    continue

                if not self.filename_is_windows_friendly(name):
                    not_windows_friendly.add(name)

                if name.lower() in lc_names:
                    differ_only_by_case.add(lc_names[name.lower()])
                    differ_only_by_case.add(name)
                else:
                    lc_names[name.lower()] = name

                fields = ['./' + self.octal_escape(name)]

                stat_info = os.lstat(member)

                if stat.S_ISREG(stat_info.st_mode):
                    fields.append('type=file')
                    fields.append('mode=%o' % stat_info.st_mode)

                    # With sub-second precision, note that some versions
                    # of mtree use the part after the dot as integer
                    # nanoseconds, so "1.234" is actually 1 sec + 234 ns,
                    # or what normal people would write as 1.000000234.
                    # To be compatible with both, we always show the time
                    # with 9 digits after the decimal point.
                    fields.append(f'time={stat_info.st_mtime:.9f}')

                    fields.append(f'size={stat_info.st_size}')
                    file_id = (stat_info.st_dev, stat_info.st_ino)
                    if stat_info.st_size > 0:
                        if file_id not in sha256:
                            hasher = hashlib.sha256()
                            with open(member, 'rb') as f:
                                while True:
                                    blob = f.read(4096)

                                    if not blob:
                                        break

                                    hasher.update(blob)

                            sha256[file_id] = hasher.hexdigest()

                        fields.append(f'sha256={sha256[file_id]}')

                    if stat_info.st_nlink > 1:
                        if file_id in paths:
                            writer.write(
                                '# hard link to {}\n'.format(
                                    self.octal_escape(paths[file_id]),
                                ),
                            )
                        else:
                            paths[file_id] = str(relative_path)

                elif stat.S_ISLNK(stat_info.st_mode):
                    fields.append('type=link')
                    fields.append(
                        f'link={self.octal_escape(os.readlink(member))}')
                elif stat.S_ISDIR(stat_info.st_mode):
                    fields.append('type=dir')
                else:
                    writer.write(
                        '# unknown file type: {}\n'.format(
                            self.octal_escape(name),
                        ),
                    )
                    continue

                writer.write(' '.join(fields) + '\n')

            if '.ref' not in lc_names:
                writer.write('./.ref type=file size=0 mode=644\n')

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

            # We need to close the gzip before copying it, otherwise we
            # will end up with a corrupted file
            writer.close()
            shutil.copy2(writer.name, runtime)

    def minimize_runtime(self, root: str) -> None:
        '''
        Remove files that pressure-vessel can reconstitute from the manifest.

        This is the equivalent of:

        find $root/files -type l -delete
        find $root/files -empty -delete

        Note that this needs to be done before ensure_ref(), otherwise
        it will delete files/.ref too.
        '''
        for (dirpath, dirnames, filenames) in os.walk(
            os.path.join(root, 'files'),
            topdown=False,
        ):
            for f in filenames + dirnames:
                path = os.path.join(dirpath, f)

                try:
                    statinfo = os.lstat(path)
                except FileNotFoundError:
                    continue

                if stat.S_ISLNK(statinfo.st_mode) or statinfo.st_size == 0:
                    os.remove(path)
            try:
                os.rmdir(dirpath)
            except OSError as e:
                if e.errno != errno.ENOTEMPTY:
                    raise


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
        '--version', default='',
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
            'Download runtime into this existing directory'
        )
    )
    parser.add_argument(
        '--depot-version', default='',
        help=(
            'Set an overall version number for the depot contents'
        )
    )

    parser.add_argument(
        '--pressure-vessel-uri',
        default=DEFAULT_PRESSURE_VESSEL_URI,
        metavar='URI',
        help=(
            'Download pressure-vessel from a versioned subdirectory of URI'
        ),
    )
    parser.add_argument(
        '--pressure-vessel-ssh-host', default='', metavar='HOST',
        help=(
            'Use ssh and rsync to download pressure-vessel from HOST '
            '[default: same as --ssh-host]'
        ),
    )
    parser.add_argument(
        '--pressure-vessel-ssh-path', default='', metavar='PATH',
        help=(
            'Use ssh and rsync to download pressure-vessel from a versioned '
            'subdirectory of PATH on HOST'
        ),
    )
    parser.add_argument(
        '--pressure-vessel-version', default='', metavar='0.x.y|latest',
        help=(
            'Use this version of pressure-vessel from --pressure-vessel-uri '
            'or --pressure-vessel-ssh-path'
        )
    )
    parser.add_argument(
        '--pressure-vessel-archive', default='', metavar='PATH',
        help=(
            'Unpack pressure-vessel from the named archive'
        ),
    )
    parser.add_argument(
        '--pressure-vessel-from-runtime', default='', metavar='NAME',
        help=(
            'Get pressure-vessel from the named runtime (default "scout")'
        ),
    )
    parser.add_argument(
        '--pressure-vessel-from-runtime-json', default='', metavar='NAME',
        help=(
            'Get pressure-vessel from a separate runtime version given as a '
            'JSON object'
        ),
    )
    parser.add_argument(
        '--pressure-vessel', default='', metavar='NAME|PATH|DETAILS',
        dest='pressure_vessel_guess',
        help=(
            '--pressure-vessel-archive=ARCHIVE, '
            '--pressure-vessel-archive=DIRECTORY/pressure-vessel-bin.tar.gz, '
            '--pressure-vessel-from-runtime=NAME or '
            '--pressure-vessel-from-runtime-json=DETAILS, '
            'based on form of argument given '
            '(disambiguate with ./ if necessary)'
        ),
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
        '--include-sdk-debug', default=False, action='store_true',
        help='Include a corresponding SDK',
    )
    parser.add_argument(
        '--include-sdk-runtime', default=False, action='store_true',
        help='Include a corresponding SDK',
    )
    parser.add_argument(
        '--include-sdk-sysroot', default=False, action='store_true',
        help='Include a corresponding SDK',
    )
    parser.add_argument(
        '--layered', default=False, action='store_true',
        help='Produce a layered runtime that runs scout on soldier',
    )
    parser.add_argument(
        '--minimize', action='store_true', default=False,
        help=(
            'Omit empty files, empty directories and symlinks from '
            'runtime content, requiring pressure-vessel to fill them in '
            'from the mtree manifest'
        )
    )
    parser.add_argument(
        '--no-minimize', action='store_false', dest='minimize',
        help=(
            'Include empty files, empty directories and symlinks in '
            'runtime content [default]'
        )
    )
    parser.add_argument(
        '--source-dir', default=str(HERE),
        help=(
            'Source directory for files to include in the depot'
        )
    )
    # Not actually used for anything at the moment, but kept for
    # CLI backwards-compat. We could potentially use it to select
    # depot configuration in steampipe/
    parser.add_argument(
        '--steam-app-id', default='',
        help='Set Steam app ID for the depot',
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
        '--unpack-runtime', '--unpack-runtimes',
        action='store_true', default=True,
        help=(
            "Unpack the runtime into the --depot, for use with "
            "pressure-vessel's tests/containers.py. [default]"
        )
    )
    parser.add_argument(
        '--no-unpack-runtime', '--no-unpack-runtimes',
        action='store_false', dest='unpack_runtime',
        help=(
            "Don't unpack the runtime into the --depot"
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
        '--versioned-directories', action='store_true', default=True,
        help=(
            'Include version number in unpacked runtime directories '
            '[default]'
        )
    )
    parser.add_argument(
        '--no-versioned-directories', action='store_false',
        dest='versioned_directories',
        help=(
            'Do not include version number in unpacked runtime directories'
        )
    )
    parser.add_argument(
        'runtime',
        default='',
        metavar='NAME[="DETAILS"]',
        help=(
            'Runtime to download, in the form NAME or NAME="DETAILS". '
            'DETAILS is a JSON object containing something like '
            '{"path": "../prebuilt", "suite: "scout", "version": "latest", '
            '"architecture": "amd64,i386"}, or the '
            'path to a file with the same JSON object in. All JSON fields '
            'are optional.'
        ),
    )

    try:
        args = parser.parse_args()

        args.include_sdk_debug = args.include_sdk_debug or args.include_sdk
        args.include_sdk_runtime = args.include_sdk_runtime or args.include_sdk
        args.include_sdk_sysroot = args.include_sdk_sysroot or args.include_sdk

        Main(**vars(args)).run()
    except InvocationError as e:
        parser.error(str(e))


if __name__ == '__main__':
    main()
