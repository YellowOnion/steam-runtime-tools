#!/bin/sh
# Copyright © 2015 Collabora Ltd
# SPDX-License-Identifier: LGPL-2.1-or-later

# Wrapper to make GTest tests output TAP syntax, because Automake's test
# drivers do not currently support passing the same command-line argument
# to each test executable. All GTest tests produce TAP output if invoked
# with the --tap option.
#
# Usage: "glib-tap.sh test-foo --verbose ..." is equivalent to
# "test-foo --tap --verbose ..."

set -e
t="$1"
shift

exec "$t" --tap "$@"
