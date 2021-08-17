Developer tests
===============

<!-- This file:
Copyright 2020 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

`tests/*.sh` carry out some basic QA checks. Run with:

    make check

`python3` and the `prove` tool from Perl are required.

`mypy`, `pycodestyle`, `pyflakes3`, `python3.4`, `python3.5` and
`shellcheck` are required for full coverage, but the relevant checks
are automatically skipped if those utilities are missing.

Non-fatal issues such as failed coding-style checks and other warnings
are reported as a TAP "TODO" diagnostic.

Depot tests
===========

Pleas see [depot/README.md](depot/README.md).
