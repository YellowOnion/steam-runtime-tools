#!/bin/sh

set -eux

builddir="${1:-_build}"
PRESSURE_VESSEL="${2:-_build/production/pressure-vessel-bin.tar.gz}"

rm -fr "$builddir/depot"
rm -fr "$builddir/depot-template"
mkdir -p "$builddir/depot"
mkdir -p "$builddir/depot-template/common"

if [ -n "${IMAGES_DOWNLOAD_URL-}" ] && [ -n "${IMAGES_DOWNLOAD_CREDENTIAL-}" ]; then
    suites="scout soldier"
    set -- \
        --include-sdk \
        --credential-env IMAGES_DOWNLOAD_CREDENTIAL \
        --images-uri "${IMAGES_DOWNLOAD_URL}"/steamrt-SUITE/snapshots \
        ${NULL+}
else
    suites="scout"
    set -- \
        --version latest-steam-client-public-beta \
        ${NULL+}
fi

for suite in $suites; do
    time python3 ./pressure-vessel/populate-depot.py \
        --depot="$builddir/depots/$suite" \
        --include-archives \
        --no-versioned-directories \
        --pressure-vessel "${PRESSURE_VESSEL}" \
        --source-dir="$builddir/depot-template" \
        --unpack-runtimes \
        "$@" \
        "${suite}"
    for member in "$builddir/depots/$suite"/*; do
        rm -fr "$builddir/depot/${member##*/}"
    done
    mv "$builddir/depots/$suite"/* "$builddir/depot/"
done

# vim:set sw=4 sts=4 et:
