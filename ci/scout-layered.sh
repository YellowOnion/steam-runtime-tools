#!/bin/sh
# Copyright 2021-2023 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eux

builddir="${1:-_build/scout-layered}"

rm -fr "$builddir/steam-container-runtime/depot"
install -d "$builddir/steam-container-runtime/depot"
install -d "$builddir/steam-container-runtime/steampipe"
install -m644 \
    subprojects/container-runtime/steampipe/app_build_1070560.vdf \
    subprojects/container-runtime/steampipe/depot_build_1070561.vdf \
    "$builddir/steam-container-runtime/steampipe/"

case "${CI_COMMIT_TAG-}" in
    (v*)
        depot_version="${CI_COMMIT_TAG}"
        ;;

    (*)
        depot_version="$(git describe --always --match="v*" HEAD)"
        ;;
esac

if [ -n "${SOURCE_DATE_EPOCH-}" ]; then
    timestamp="@${SOURCE_DATE_EPOCH}"
elif [ -n "${CI_COMMIT_TIMESTAMP-}" ]; then
    timestamp="${CI_COMMIT_TIMESTAMP}"
else
    timestamp="$(git log --pretty=format:'@%at' HEAD~..)"
fi

echo "${depot_version#v}" > subprojects/container-runtime/.tarball-version
./subprojects/container-runtime/populate-depot.py \
    --depot="$builddir/steam-container-runtime/depot" \
    --depot-version="${depot_version#v}" \
    --layered \
    --steam-app-id=1070560 \
    scout
head -n-0 "$builddir/steam-container-runtime/depot/VERSIONS.txt"
tar \
    -C "$builddir" \
    --clamp-mtime \
    --mtime="${timestamp}" \
    --owner=nobody:65534 \
    --group=nogroup:65534 \
    --mode=u=rwX,go=rX \
    --use-compress-program='pigz --fast -c -n --rsyncable' \
    -cvf "$builddir/steam-container-runtime.tar.gz" \
    steam-container-runtime

# vim:set sw=4 sts=4 et:
