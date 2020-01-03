Depot tests
===========

Tests in this directory are designed to be run on any system that is
meant to run Steam. In particular, they have been confirmed to work on
SteamOS 2 'brewmaster', and should work on Debian 8 'jessie' or later
(confirmed on Debian 9) and on Ubuntu 14.04 'trusty' or later.

The depot directory `depot/` must have been pre-populated with at least
`com.valvesoftware.SteamRuntime.Platform-amd64,i386-scout-runtime.tar.gz`
and `com.valvesoftware.SteamRuntime.Platform-amd64,i386-scout-buildid.txt`.
This is normally done by CI infrastructure while building Steam Runtime
releases.

To run the tests manually, use:

    ./tests/depot/pressure-vessel.py

Note that this will write to the `depot/` directory. If this is not
desired, copy the whole `SteamLinuxRuntime` directory elsewhere and run
the test in the copy.

The debian/ directory provides the basic layout of a Debian source
package (even though it isn't really) and wraps the test in autopkgtest
metadata, to be able to take advantage of the autopkgtest tool's pluggable
virtualization backends, and in particular the qemu backend. Typical use
would resemble this:

    rm -fr test-logs

    autopkgtest \
    --setup-commands "dpkg --add-architecture i386" \
    --apt-upgrade \
    --no-built-binaries \
    --output "$(pwd)/test-logs" \
    "$(pwd)" \
    -- \
    qemu \
    --efi \
    ~/Downloads/brewmaster_amd64.qcow2

Additional arguments like `--debug` can be placed after `autopkgtest`. See
autopkgtest(1) for more details.

Arguments like `--debug`, `--show-boot` and `--qemu-options="-display gtk"`
can be placed after `qemu` for debugging. See autopkgtest-virt-qemu(1)
for more details.

Testing a different runtime
---------------------------

If you have a different runtime unpacked into `depot/`, for example
in `depot/heavy/files`, set the environment variables
`TEST_CONTAINER_RUNTIME_SUITE` (the default is `scout`) and/or
`TEST_CONTAINER_RUNTIME_ARCHITECTURES` (the default is `amd64,i386`).

If using `autopkgtest`, you will need to set the environment variables
in the test system, like this:

    autopkgtest \
    --env=TEST_CONTAINER_RUNTIME_SUITE=heavy \
    --env=TEST_CONTAINER_RUNTIME_ARCHITECTURES=amd64 \
    ...

Testing inside a `LD_LIBRARY_PATH` Steam Runtime
------------------------------------------------

You can expand test coverage by setting the environment variable
`TEST_CONTAINER_RUNTIME_LD_LIBRARY_PATH_RUNTIME` to the path to an
unpacked `LD_LIBRARY_PATH` Steam Runtime, so that files like
`${TEST_CONTAINER_RUNTIME_LD_LIBRARY_PATH_RUNTIME}/run.sh` and
`${TEST_CONTAINER_RUNTIME_LD_LIBRARY_PATH_RUNTIME}/version.txt` exist.
This emulates what will happen when `pressure-vessel` is run as a
Steam compatibility tool from inside the Steam client, which is
itself running under the `LD_LIBRARY_PATH` Steam Runtime.

If using `autopkgtest`, you will need to ask autopkgtest to copy the
unpacked Steam Runtime into the test system and use the path on the
test system, something like this:

    autopkgtest \
    --copy "$HOME/.steam/steam/ubuntu12_32/steam-runtime:/tmp/srt" \
    --env TEST_CONTAINER_RUNTIME_LD_LIBRARY_PATH_RUNTIME="/tmp/srt" \
    ...
