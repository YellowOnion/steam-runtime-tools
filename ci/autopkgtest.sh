#!/bin/sh
# Copyright 2022 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eux

apt-get install -y --no-install-recommends \
apt-utils \
build-essential \
ca-certificates \
debhelper \
devscripts \
dpkg-dev \
git \
libdpkg-perl \
procps \
rsync \
${BUILD_DEPENDENCIES} \
${NULL+}

# Optional
apt-get install -y --no-install-recommends eatmydata || :
dbus-uuidgen --ensure || :

tempdir="$(mktemp -d)"

case "$(. /usr/lib/os-release; echo "${VERSION_CODENAME-${VERSION}}")" in
    (scout)
        git clone --branch steam/for-ci https://gitlab-ci-token:${CI_JOB_TOKEN}@gitlab.steamos.cloud/packaging/autopkgtest.git "$tempdir/autopkgtest"
        git clone --branch debian/buster https://gitlab-ci-token:${CI_JOB_TOKEN}@gitlab.steamos.cloud/packaging/chardet.git "$tempdir/chardet"
        git clone --branch debian/buster https://gitlab-ci-token:${CI_JOB_TOKEN}@gitlab.steamos.cloud/packaging/python-debian.git "$tempdir/python-debian"
        git clone --branch debian/buster https://gitlab-ci-token:${CI_JOB_TOKEN}@gitlab.steamos.cloud/packaging/six.git "$tempdir/six"

        export PYTHONPATH="$tempdir/chardet:$tempdir/python-debian/lib:$tempdir/six"
        set -- python3.5 "$tempdir/autopkgtest/runner/autopkgtest"
        ;;

    (heavy)
        apt-get install -y python3-debian
        git clone --branch steam/for-ci https://gitlab-ci-token:${CI_JOB_TOKEN}@gitlab.steamos.cloud/packaging/autopkgtest.git "$tempdir/autopkgtest"
        set -- python3 "$tempdir/autopkgtest/runner/autopkgtest"
        ;;

    (*)
        apt-get install -y python3-debian autopkgtest
        set -- autopkgtest
        ;;
esac

# We need up-to-date packages for the relocatable install to
# be able to get its source code
apt-get -y dist-upgrade

# Install the packages under test. We're not too worried about
# minimal dependencies here
dpkg -i \
debian/tmp/artifacts/build/libsteam-runtime-tools-0-0_*.deb \
debian/tmp/artifacts/build/libsteam-runtime-tools-0-0-dbgsym_*_*.*deb \
debian/tmp/artifacts/build/libsteam-runtime-tools-0-dev_*.deb \
debian/tmp/artifacts/build/libsteam-runtime-tools-0-helpers_*.deb \
debian/tmp/artifacts/build/libsteam-runtime-tools-0-helpers-dbgsym_*_*.*deb \
debian/tmp/artifacts/build/libsteam-runtime-tools-0-tests_*_amd64.deb \
debian/tmp/artifacts/build/pressure-vessel-relocatable_*_amd64.deb \
debian/tmp/artifacts/build/pressure-vessel-relocatable-dbgsym_*_amd64.*deb \
debian/tmp/artifacts/build/pressure-vessel-libs*.deb \
debian/tmp/artifacts/build/steam-runtime-tools-bin_*_amd64.deb \
debian/tmp/artifacts/build/steam-runtime-tools-bin-dbgsym_*_amd64.*deb \
debian/tmp/artifacts/build/steam-runtime-tools-minimal_*_amd64.deb \
debian/tmp/artifacts/build/steam-runtime-tools-minimal-dbgsym_*_amd64.*deb \
${NULL+}
apt-get -y -f install

e=0

# autopkgtest doesn't like it if this is a pre-existing directory,
# so don't pre-create it!
set -- "$@" --output-dir="$(pwd)/debian/tmp/artifacts/autopkgtest"

set -- "$@" --no-built-binaries
set -- "$@" debian/tmp/artifacts/build/*.deb
set -- "$@" debian/tmp/artifacts/build/*.dsc

"$@" -- null || e=$?

case "$e" in
    (0|2|8)
        # OK: 0 means total success, 2 means at least one test
        # was skipped, 8 means all tests were skipped
        ;;
    (*)
        exit "$e"
        ;;
esac
