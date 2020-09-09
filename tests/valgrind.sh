#!/bin/sh

# Copyright © 2019 Collabora Ltd.
#
# SPDX-License-Identifier: MIT
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be included
# in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
# CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
# TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
# SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

# Usage:
# meson test -C _build -v --wrap=$(pwd)/tests/valgrind.sh
# or build with DEB_BUILD_OPTIONS=nostrip and use:
# $(pwd)/tests/valgrind.sh /usr/libexec/installed-tests/steam-runtime-tools-0/test-architecture
#
# Optionally run with these environment variables:
# VALGRIND_FATAL: if non-empty, valgrind errors cause tests to fail
# VALGRIND_OPTIONS: add more options to the valgrind command-line
# VALGRIND_VERBOSE: if non-empty, make valgrind more verbose

here="$(dirname "$0")"

export G_DEBUG="gc-friendly${G_DEBUG+",${G_DEBUG}"}"
export G_SLICE=always-malloc

exec valgrind \
${VALGRIND_FATAL:+--error-exitcode=1} \
--gen-suppressions=all \
--leak-check=full \
--num-callers=20 \
--show-reachable=yes \
--suppressions="${here}/valgrind.supp" \
--trace-children=yes \
${VALGRIND_VERBOSE:+--verbose} \
${VALGRIND_OPTIONS:+${VALGRIND_OPTIONS}} \
-- \
"$@"
