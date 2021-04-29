#!/bin/sh
# Copyright © 2021 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -e
set -u

if [ -n "${TESTS_ONLY-}" ]; then
    echo "1..0 # SKIP This distro is too old to run populate-depot.py"
    exit 0
fi

if [ -n "${IMAGES_DOWNLOAD_URL-}" ] && [ -n "${IMAGES_DOWNLOAD_CREDENTIAL-}" ]; then
    set -- \
        --credential-env IMAGES_DOWNLOAD_CREDENTIAL \
        --images-uri "${IMAGES_DOWNLOAD_URL}"/steamrt-SUITE/snapshots \
        ${NULL+}
elif [ -n "${IMAGES_SSH_HOST-}" ] && [ -n "${IMAGES_SSH_PATH-}" ]; then
    set -- \
        --ssh-host "${IMAGES_SSH_HOST}" \
        --ssh-path "${IMAGES_SSH_PATH}" \
        ${NULL+}
else
    # There's no public release of sniper yet, so this won't actually work
    # without supplying a URL and credentials for the non-public version
    echo "1..0 # SKIP no public release of sniper available"
    exit 0

    # When there's a public release, this will probably work
    set -- \
        --pressure-vessel='{"version": "latest-container-runtime-public-beta"}' \
        --version latest-container-runtime-public-beta \
        ${NULL+}
fi

echo "1..1"

rm -fr depots/test-sniper-archives
mkdir -p depots/sniper-archives
python3 ./populate-depot.py \
    --depot=depots/test-sniper-archives \
    --toolmanifest \
    "$@" \
    sniper \
    ${NULL+}
find depots/test-sniper-archives -ls > depots/test-sniper-archives.txt
echo "ok 1 - sniper, deploying from archive"

# vim:set sw=4 sts=4 et:
