#!/bin/sh
# Copyright 2022 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eux

apt-get install -y --no-install-recommends \
    build-essential \
    debhelper \
    devscripts \
    dpkg-dev \
    rsync \
    "$@"

case "$(. /usr/lib/os-release; echo "${VERSION_CODENAME-${VERSION}}")" in
    (scout)
        apt-get -y install pkg-create-dbgsym
        ;;
esac

set --

if [ -n "${CI_COMMIT_TAG-}" ]; then
    set -- "$@" --release
fi

mkdir -p debian/tmp/artifacts/build
set -- "$@" --download "$(pwd)/debian/tmp/artifacts/build"

case "$STEAM_CI_DEB_BUILD" in
    (any)
        set -- "$@" --dpkg-buildpackage-option=-B
        ;;

    (full)
        set -- "$@" --source
        ;;
esac

deb-build-snapshot "$@" localhost
