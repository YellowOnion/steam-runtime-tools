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

echo "1..4"

rm -fr depots/test-scout-archives
mkdir -p depots/test-scout-archives
python3 ./populate-depot.py \
    --depot=depots/test-scout-archives \
    --include-archives \
    --no-unpack-runtime \
    --toolmanifest \
    "$@" \
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
    "$@" \
    scout \
    ${NULL+}
find depots/test-scout-unpacked -ls > depots/test-scout-unpacked.txt
echo "ok 2 - scout, running from unpacked directory"

rm -fr depots/test-scout-layered
mkdir -p depots/test-scout-layered
python3 ./populate-depot.py \
    --depot=depots/test-scout-layered \
    --layered \
    "$@" \
    --version= \
    scout \
    ${NULL+}
find depots/test-scout-layered -ls > depots/test-scout-layered.txt
test -e depots/test-scout-layered/README.md
test -e depots/test-scout-layered/VERSIONS.txt
test -e depots/test-scout-layered/toolmanifest.vdf
test -x depots/test-scout-layered/run
test -x depots/test-scout-layered/run-in-scout
test -x depots/test-scout-layered/_v2-entry-point
test ! -e depots/test-scout-layered/steam-runtime
echo "ok 3 - scout, layered on soldier, reusing standard LDLP runtime"

rm -fr depots/test-scout-layered-beta
mkdir -p depots/test-scout-layered-beta
python3 ./populate-depot.py \
    --depot=depots/test-scout-layered-beta \
    --layered \
    "$@" \
    --version=latest-steam-client-public-beta \
    scout \
    ${NULL+}
find depots/test-scout-layered-beta -ls > depots/test-scout-layered-beta.txt
test -e depots/test-scout-layered-beta/README.md
test -e depots/test-scout-layered-beta/VERSIONS.txt
test -e depots/test-scout-layered-beta/toolmanifest.vdf
test -x depots/test-scout-layered-beta/run
test -x depots/test-scout-layered-beta/run-in-scout
test -x depots/test-scout-layered-beta/_v2-entry-point
test -e depots/test-scout-layered-beta/steam-runtime/version.txt
test -d depots/test-scout-layered-beta/steam-runtime/usr/
echo "ok 4 - scout, layered on soldier, with own copy of beta LDLP runtime"

# vim:set sw=4 sts=4 et:
