#!/bin/sh
# Copyright © 2022 Collabora Ltd.
# SPDX-License-Identifier: MIT

# This doesn't really test anything, it's only here to check our
# infrastructure for running shell scripts as build-time and as-installed
# tests.

set -eu

echo "1..1"
echo "ok 1 - found a shell"
