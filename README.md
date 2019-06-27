steam-runtime-tools — Steam Runtime integration for the Steam client
====================================================================

The steam-runtime-tools library provides low-level Unix-specific tools
and functionality for the Steam client.

To support multiple architectures (currently only `i386` and `x86_64`
are supported), you will need to build it once for each architecture and
install at least the helper tools in `/usr/libexec/steam-runtime-tools-0`
(the `libsteam-runtime-tools-0-helpers` package) for every architecture
in parallel.

The helper tools are located relative to the shared library, so it's OK
to bundle steam-runtime-tools alongside some other stack in this layout:

    anything/
        lib/
            x86_64-linux-gnu/
                libsteam-runtime-tools-0.so.0
        libexec/
            steam-runtime-tools-0/
                i386-linux-gnu-*
                x86_64-linux-gnu-*

as long as the program that is linked to `libsteam-runtime-tools-0.so.0`
can find it (via a `RPATH` or `RUNPATH` or by setting the `LD_LIBRARY_PATH`
environment variable).
