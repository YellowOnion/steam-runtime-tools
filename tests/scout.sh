#!/bin/bash
# Copyright © 2021 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -e
set -u

if [ -n "${TESTS_ONLY-}" ]; then
    echo "1..0 # SKIP This distro is too old to run populate-depot.py"
    exit 0
fi

populate_depot_args=()

if [ -n "${IMAGES_DOWNLOAD_CREDENTIAL-}" ]; then
    populate_depot_args=( \
        "${populate_depot_args[@]}" \
        --credential-env IMAGES_DOWNLOAD_CREDENTIAL \
    )
fi

if [ -z "${PRESSURE_VESSEL_DOWNLOAD_URL-}" ]; then
    PRESSURE_VESSEL_DOWNLOAD_URL=https://repo.steampowered.com/pressure-vessel/snapshots/
fi

if [ -n "${IMAGES_DOWNLOAD_URL-}" ] && [ -n "${IMAGES_DOWNLOAD_CREDENTIAL-}" ]; then
    populate_depot_args=( \
        "${populate_depot_args[@]}" \
        --images-uri "${IMAGES_DOWNLOAD_URL}"/steamrt-SUITE/snapshots \
    )
elif [ -n "${IMAGES_SSH_HOST-}" ] && [ -n "${IMAGES_SSH_PATH-}" ]; then
    populate_depot_args=( \
        "${populate_depot_args[@]}" \
        --ssh-host "${IMAGES_SSH_HOST}" \
        --ssh-path "${IMAGES_SSH_PATH}" \
    )
else
    populate_depot_args=( \
        "${populate_depot_args[@]}" \
        --version latest-steam-client-public-beta \
    )
fi

if [ -n "${IMAGES_DOWNLOAD_CREDENTIAL-}" ]; then
    pressure_vessel_args=( \
        --pressure-vessel-uri="${PRESSURE_VESSEL_DOWNLOAD_URL}" \
        --pressure-vessel-version=latest \
    )
elif [ -n "${PRESSURE_VESSEL_SSH_HOST-"${IMAGES_SSH_HOST-}"}" ] && [ -n "${PRESSURE_VESSEL_SSH_PATH-}" ]; then
    pressure_vessel_args=( \
        --pressure-vessel-ssh-host="${PRESSURE_VESSEL_SSH_HOST-"${IMAGES_SSH_HOST}"}" \
        --pressure-vessel-ssh-path="${PRESSURE_VESSEL_SSH_PATH}" \
        --pressure-vessel-version=latest \
    )
else
    pressure_vessel_args=( \
        --pressure-vessel-uri="${PRESSURE_VESSEL_DOWNLOAD_URL}" \
        --pressure-vessel-version=latest \
    )
fi

echo "1..4"

rm -fr depots/test-scout-archives
mkdir -p depots/test-scout-archives
python3 ./populate-depot.py \
    --depot=depots/test-scout-archives \
    --include-archives \
    --no-unpack-runtime \
    --toolmanifest \
    "${populate_depot_args[@]}" \
    "${pressure_vessel_args[@]}" \
    scout \
    ${NULL+}
find depots/test-scout-archives -ls > depots/test-scout-archives.txt
echo "ok 1 - scout, deploying from archive"

rm -fr depots/test-scout-unpacked
mkdir -p depots/test-scout-unpacked
python3 ./populate-depot.py \
    --depot=depots/test-scout-unpacked \
    --no-include-archives \
    --toolmanifest \
    --unpack-runtime \
    --versioned-directories \
    "${populate_depot_args[@]}" \
    "${pressure_vessel_args[@]}" \
    scout \
    ${NULL+}
find depots/test-scout-unpacked -ls > depots/test-scout-unpacked.txt
echo "ok 2 - scout, running from unpacked directory"

rm -fr depots/test-scout-layered
mkdir -p depots/test-scout-layered
python3 ./populate-depot.py \
    --depot=depots/test-scout-layered \
    --layered \
    "${populate_depot_args[@]}" \
    "${pressure_vessel_args[@]}" \
    --version= \
    scout \
    ${NULL+}
find depots/test-scout-layered -ls > depots/test-scout-layered.txt
test -e depots/test-scout-layered/README.md
test -e depots/test-scout-layered/VERSIONS.txt
test -e depots/test-scout-layered/toolmanifest.vdf
test -x depots/test-scout-layered/scout-on-soldier-entry-point-v2
test -x depots/test-scout-layered/_v2-entry-point
test ! -e depots/test-scout-layered/steam-runtime

if ! grep $'^LD_LIBRARY_PATH\t-\tscout\t-\t#' depots/test-scout-layered/VERSIONS.txt >/dev/null; then
    echo "Bail out! LD_LIBRARY_PATH runtime's (lack of) version number not found"
    exit 1
fi

echo "ok 3 - scout, layered on soldier, reusing standard LDLP runtime"

rm -fr depots/test-scout-layered-beta
mkdir -p depots/test-scout-layered-beta
python3 ./populate-depot.py \
    --depot=depots/test-scout-layered-beta \
    --layered \
    "${populate_depot_args[@]}" \
    "${pressure_vessel_args[@]}" \
    --version=latest-steam-client-public-beta \
    scout \
    ${NULL+}
find depots/test-scout-layered-beta -ls > depots/test-scout-layered-beta.txt
test -e depots/test-scout-layered-beta/README.md
test -e depots/test-scout-layered-beta/VERSIONS.txt
test -e depots/test-scout-layered-beta/toolmanifest.vdf
test -x depots/test-scout-layered-beta/scout-on-soldier-entry-point-v2
test -x depots/test-scout-layered-beta/_v2-entry-point
test -e depots/test-scout-layered-beta/steam-runtime/version.txt
test -d depots/test-scout-layered-beta/steam-runtime/usr/

if ! grep -E $'^LD_LIBRARY_PATH\t[0-9.]+\tscout\t[0-9.]+\t#' depots/test-scout-layered-beta/VERSIONS.txt >/dev/null; then
    echo "Bail out! LD_LIBRARY_PATH runtime's version number not found"
    exit 1
fi

echo "ok 4 - scout, layered on soldier, with own copy of beta LDLP runtime"

# vim:set sw=4 sts=4 et:
