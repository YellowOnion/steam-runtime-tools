#!/bin/sh
# Copyright © 2022 Collabora Ltd.
# SPDX-License-Identifier: MIT

set -eu

if command -v python3.5 >/dev/null; then
    exec python3.5 "$@"
else
    exec python3 "$@"
fi
