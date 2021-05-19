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

archive=com.valvesoftware.SteamRuntime.Platform-amd64,i386-soldier

rm -fr depots/test-soldier-archives
mkdir -p depots/test-soldier-archives
python3 ./populate-depot.py \
    --depot=depots/test-soldier-archives \
    --toolmanifest \
    --include-archives \
    --no-unpack-runtime \
    "$@" \
    soldier \
    ${NULL+}
find depots/test-soldier-archives -ls > depots/test-soldier-archives.txt
tar -tf "depots/test-soldier-archives/${archive}-runtime.tar.gz" \
    > depots/test-soldier-archives-tar.txt

buildid="$(cat "depots/test-soldier-archives/$archive-buildid.txt")"
soldier_version="$(
    IFS="$(printf '\t')"
    while read -r component version runtime comment; do
        : "$runtime"    # unused
        : "$comment"    # unused
        if [ "$component" = "soldier" ]; then
            printf '%s' "$version"
        fi
    done < depots/test-soldier-archives/VERSIONS.txt
)"
echo "# In -buildid.txt: $buildid"
echo "# In VERSIONS.txt: $soldier_version"

if [ "$buildid" != "$soldier_version" ]; then
    echo "Bail out! Version mismatch"
    exit 1
fi

run_archive="$(sed -ne 's/^archive=//p' depots/test-soldier-archives/run)"
echo "# Expected: ${archive}-runtime.tar.gz"
echo "# In ./run: $run_archive"

if [ "$run_archive" != "${archive}-runtime.tar.gz" ]; then
    echo "Bail out! Unexpected archive"
    exit 1
fi

run_archive="$(sed -ne 's/^archive=//p' depots/test-soldier-archives/run-in-soldier)"
echo "# Expected: ${archive}-runtime.tar.gz"
echo "# In ./run-in-soldier: $run_archive"

if [ "$run_archive" != "${archive}-runtime.tar.gz" ]; then
    echo "Bail out! Unexpected archive"
    exit 1
fi

for dir in depots/test-soldier-archives/soldier*; do
    if [ -d "$dir" ]; then
        echo "Bail out! Unexpected directory $dir"
        exit 1
    fi
done

echo "ok 1 - soldier, deploying from archive"

rm -fr depots/test-soldier-unpacked
mkdir -p depots/test-soldier-unpacked
python3 ./populate-depot.py \
    --depot=depots/test-soldier-unpacked \
    --no-include-archives \
    --toolmanifest \
    --unpack-runtime \
    --versioned-directories \
    "$@" \
    soldier \
    ${NULL+}
find depots/test-soldier-unpacked -ls > depots/test-soldier-unpacked.txt

soldier_version="$(
    IFS="$(printf '\t')"
    while read -r component version runtime comment; do
        : "$runtime"    # unused
        : "$comment"    # unused
        if [ "$component" = "soldier" ]; then
            printf '%s' "$version"
        fi
    done < depots/test-soldier-unpacked/VERSIONS.txt
)"
if [ -z "$soldier_version" ]; then
    echo "Bail out! Unable to determine version"
    exit 1
fi

run_dir="$(sed -ne 's/^dir=//p' depots/test-soldier-unpacked/run)"
echo "# Expected: soldier_platform_$soldier_version"
echo "# In ./run: $run_dir"

if [ "$run_dir" != "soldier_platform_$soldier_version" ]; then
    exit 1
fi

run_dir="$(sed -ne 's/^dir=//p' depots/test-soldier-unpacked/run-in-soldier)"
echo "# Expected: soldier_platform_$soldier_version"
echo "# In ./run-in-soldier: $run_dir"

if [ "$run_dir" != "soldier_platform_$soldier_version" ]; then
    exit 1
fi

if ! [ -d "depots/test-soldier-unpacked/$run_dir" ]; then
    echo "Bail out! $run_dir not found"
    exit 1
fi

for dir in depots/test-soldier-unpacked/soldier*; do
    if [ -d "$dir" ] && [ "$dir" != "depots/test-soldier-unpacked/$run_dir" ]; then
        echo "Bail out! Unexpected directory $dir"
        exit 1
    fi
done

echo "ok 2 - soldier, running from unpacked directory"

rm -fr depots/test-soldier-local
mkdir -p depots/test-soldier-local
python3 ./populate-depot.py \
    --depot=depots/test-soldier-local \
    --no-include-archives \
    --toolmanifest \
    --unpack-runtime \
    --versioned-directories \
    "$@" \
    --pressure-vessel=./.cache \
    'soldier={"path": "./depots/test-soldier-archives"}' \
    ${NULL+}
find depots/test-soldier-local -ls > depots/test-soldier-local.txt

soldier_version="$(
    IFS="$(printf '\t')"
    while read -r component version runtime comment; do
        : "$runtime"    # unused
        : "$comment"    # unused
        if [ "$component" = "soldier" ]; then
            printf '%s' "$version"
        fi
    done < depots/test-soldier-local/VERSIONS.txt
)"
if [ -z "$soldier_version" ]; then
    echo "Bail out! Unable to determine version"
    exit 1
fi

case "$soldier_version" in
    (latest-*)
        echo "Bail out! $soldier_version is not a valid version"
        exit 1
        ;;
esac

run_dir="$(sed -ne 's/^dir=//p' depots/test-soldier-local/run)"
echo "# Expected: soldier_platform_$soldier_version"
echo "# In ./run: $run_dir"

if [ "$run_dir" != "soldier_platform_$soldier_version" ]; then
    exit 1
fi

run_dir="$(sed -ne 's/^dir=//p' depots/test-soldier-local/run-in-soldier)"
echo "# Expected: soldier_platform_$soldier_version"
echo "# In ./run-in-soldier: $run_dir"

if [ "$run_dir" != "soldier_platform_$soldier_version" ]; then
    exit 1
fi

if ! [ -d "depots/test-soldier-local/$run_dir" ]; then
    echo "Bail out! $run_dir not found"
    exit 1
fi

for dir in depots/test-soldier-local/soldier*; do
    if [ -d "$dir" ] && [ "$dir" != "depots/test-soldier-local/$run_dir" ]; then
        echo "Bail out! Unexpected directory $dir"
        exit 1
    fi
done

echo "ok 3 - soldier, running from local builds"

rm -fr depots/test-soldier-unversioned
mkdir -p depots/test-soldier-unversioned
python3 ./populate-depot.py \
    --depot=depots/test-soldier-unversioned \
    --no-include-archives \
    --toolmanifest \
    --unpack-runtime \
    --no-versioned-directories \
    "$@" \
    soldier \
    ${NULL+}
find depots/test-soldier-unversioned -ls > depots/test-soldier-unversioned.txt

soldier_version="$(
    IFS="$(printf '\t')"
    while read -r component version runtime comment; do
        : "$runtime"    # unused
        : "$comment"    # unused
        if [ "$component" = "soldier" ]; then
            printf '%s' "$version"
        fi
    done < depots/test-soldier-unversioned/VERSIONS.txt
)"
if [ -z "$soldier_version" ]; then
    echo "Bail out! Unable to determine version"
    exit 1
fi

run_dir="$(sed -ne 's/^dir=//p' depots/test-soldier-unversioned/run)"
echo "# Expected: soldier"
echo "# In ./run: $run_dir"

if [ "$run_dir" != "soldier" ]; then
    exit 1
fi

run_dir="$(sed -ne 's/^dir=//p' depots/test-soldier-unversioned/run-in-soldier)"
echo "# Expected: soldier"
echo "# In ./run-in-soldier: $run_dir"

if [ "$run_dir" != "soldier" ]; then
    exit 1
fi

if ! [ -d "depots/test-soldier-unversioned/$run_dir" ]; then
    echo "Bail out! $run_dir not found"
    exit 1
fi

for dir in depots/test-soldier-unversioned/soldier*; do
    if [ -d "$dir" ] && [ "$dir" != "depots/test-soldier-unversioned/$run_dir" ]; then
        echo "Bail out! Unexpected directory $dir"
        exit 1
    fi
done

echo "ok 4 - soldier, running from unpacked directory without version"

# vim:set sw=4 sts=4 et:
