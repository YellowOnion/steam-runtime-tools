steam-runtime-tools — Steam Runtime integration for the Steam client
====================================================================

The steam-runtime-tools library provides low-level Unix-specific tools
and functionality for the Steam client, including the pressure-vessel
tool that runs Steam games in containers.

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

pressure-vessel — putting Steam in containers
---------------------------------------------

The `pressure-vessel/` subdirectory of this project contains the
pressure-vessel utilities, which are used by Steam's
[Steam Linux Runtime](https://steamdb.info/app/1070560/depots/)
compatibility tool to run games in individual game-specific containers.
For background on pressure-vessel and SteamLinuxRuntime, please see:

* <https://github.com/ValveSoftware/steam-runtime/tree/master/doc>
* <https://archive.fosdem.org/2020/schedule/event/containers_steam/>
* <https://steamcommunity.com/app/221410/discussions/0/1638675549018366706/>
