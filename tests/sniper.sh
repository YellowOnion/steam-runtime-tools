#!/bin/bash
# Copyright © 2021 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -e
set -u

if [ -n "${TESTS_ONLY-}" ]; then
    echo "1..0 # SKIP This distro is too old to run populate-depot.py"
    exit 0
fi

if [ -n "${IMAGES_DOWNLOAD_URL-}" ] && [ -n "${IMAGES_DOWNLOAD_CREDENTIAL-}" ]; then
    populate_depot_args=( \
        --credential-env IMAGES_DOWNLOAD_CREDENTIAL \
        --images-uri "${IMAGES_DOWNLOAD_URL}"/steamrt-SUITE/snapshots \
    )
    pressure_vessel_args=( \
        --pressure-vessel=scout \
    )
elif [ -n "${IMAGES_SSH_HOST-}" ] && [ -n "${IMAGES_SSH_PATH-}" ]; then
    populate_depot_args=( \
        --ssh-host "${IMAGES_SSH_HOST}" \
        --ssh-path "${IMAGES_SSH_PATH}" \
    )
    pressure_vessel_args=()
else
    # There's no public release of sniper yet, so this won't actually work
    # without supplying a URL and credentials for the non-public version
    echo "1..0 # SKIP no public release of sniper available"
    exit 0

    # When there's a public release, this will probably work
    populate_depot_args=( \
        --version latest-container-runtime-public-beta \
    )
    pressure_vessel_args=( \
        --pressure-vessel-from-runtime-json='{"version": "latest-container-runtime-public-beta"}' \
    )
fi

echo "1..2"

rm -fr depots/test-sniper-archives
mkdir -p depots/sniper-archives
python3 ./populate-depot.py \
    --depot=depots/test-sniper-archives \
    --include-archives \
    --no-unpack-runtime \
    --toolmanifest \
    "${populate_depot_args[@]}" \
    "${pressure_vessel_args[@]}" \
    sniper \
    ${NULL+}
find depots/test-sniper-archives -ls > depots/test-sniper-archives.txt
echo "ok 1 - sniper, deploying from archive"

rm -fr depots/test-sniper-unpacked
mkdir -p depots/test-sniper-unpacked
python3 ./populate-depot.py \
    --depot=depots/test-sniper-unpacked \
    --no-include-archives \
    --toolmanifest \
    --unpack-runtime \
    --versioned-directories \
    "${populate_depot_args[@]}" \
    "${pressure_vessel_args[@]}" \
    sniper \
    ${NULL+}
find depots/test-sniper-unpacked -ls > depots/test-sniper-unpacked.txt
echo "ok 2 - sniper, running from unpacked directory"

# vim:set sw=4 sts=4 et:
