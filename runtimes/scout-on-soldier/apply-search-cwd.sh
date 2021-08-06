#!/bin/sh
# Copyright 2021 Collabora Ltd.
# SPDX-License-Identifier: MIT

# Implement the search-cwd compatibility flag, by appending
# STEAM_COMPAT_INSTALL_PATH to LD_LIBRARY_PATH if it is not already there;
# then execute arguments.

set -eu

if [ -n "${STEAM_COMPAT_INSTALL_PATH-}" ]; then
    case ":${LD_LIBRARY_PATH-}:" in
        (*:$STEAM_COMPAT_INSTALL_PATH:*)
            ;;
        (*)
            LD_LIBRARY_PATH="${LD_LIBRARY_PATH:+"$LD_LIBRARY_PATH:"}${STEAM_COMPAT_INSTALL_PATH}"
            export LD_LIBRARY_PATH
            ;;
    esac
fi

exec "$@"

# vi: ts=4 sw=4 expandtab
