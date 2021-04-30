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
        --pressure-vessel=scout \
        ${NULL+}
elif [ -n "${IMAGES_SSH_HOST-}" ] && [ -n "${IMAGES_SSH_PATH-}" ]; then
    set -- \
        --ssh-host "${IMAGES_SSH_HOST}" \
        --ssh-path "${IMAGES_SSH_PATH}" \
        ${NULL+}
else
    set -- \
        --pressure-vessel='{"version": "latest-container-runtime-public-beta"}' \
        --version latest-container-runtime-public-beta \
        ${NULL+}
fi

echo "1..2"

rm -fr depots/test-soldier-archives
mkdir -p depots/test-soldier-archives
python3 ./populate-depot.py \
    --depot=depots/test-soldier-archives \
    --toolmanifest \
    --include-archives \
    --no-unpack-runtimes \
    "$@" \
    soldier \
    ${NULL+}
find depots/test-soldier-archives -ls > depots/test-soldier-archives.txt
echo "ok 1 - soldier, deploying from archive"

rm -fr depots/test-soldier-unpacked
mkdir -p depots/test-soldier-unpacked
python3 ./populate-depot.py \
    --depot=depots/test-soldier-unpacked \
    --no-include-archives \
    --toolmanifest \
    --unpack-runtimes \
    --versioned-directories \
    "$@" \
    soldier \
    ${NULL+}
find depots/test-soldier-unpacked -ls > depots/test-soldier-unpacked.txt
echo "ok 2 - soldier, running from unpacked directory"

# vim:set sw=4 sts=4 et:
