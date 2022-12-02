Container based Steam Runtime v1 'scout'
========================================

This experimental container-based release of the Steam Runtime
is enabled on a per-title basis by forcing its use in the title's
Properties dialog.

This version of the Steam Runtime uses the traditional
`LD_LIBRARY_PATH`-based Steam Runtime, but launches it in a container
using the "Steam Linux Runtime - soldier" compatibility tool, for
compatibility with games that were not compiled in a correct
Steam Runtime 1 'scout' environment. This produces a result similar to
running the traditional `LD_LIBRARY_PATH`-based Steam Runtime on a
Debian 10 system, so games that work in that environment are likely to
work in this container too.

Known issues
------------

Please see
https://github.com/ValveSoftware/steam-runtime/blob/master/doc/steamlinuxruntime-known-issues.md

Reporting bugs
--------------

Please see
https://github.com/ValveSoftware/steam-runtime/blob/master/doc/reporting-steamlinuxruntime-bugs.md

Development and debugging
-------------------------

See `SteamLinuxRuntime_soldier/README.md` for details of the container
runtime.

This additional layer uses a `LD_LIBRARY_PATH`-based Steam Runtime to
provide the required libraries for the Steam Runtime version 1 ABI.

By default, it will use the version in the Steam installation directory,
`~/.steam/root/ubuntu12_32/steam-runtime` (normally this is the same as
`~/.local/share/Steam/ubuntu12_32/steam-runtime`). You can use a different
version of Steam Runtime 1 'scout' by unpacking a `steam-runtime.tar.xz`
into the `SteamLinuxRuntime/steam-runtime/` directory, so that you have
files like `SteamLinuxRuntime/steam-runtime/run.sh`.

If you have `SteamLinuxRuntime` and `SteamLinuxRuntime_soldier` installed
in the same Steam library, you can use `run-in-scout-on-soldier` to test
commands in the scout-on-soldier environment, for example:

    .../steamapps/common/SteamLinuxRuntime/run-in-scout-on-soldier -- xterm

Licensing and copyright
-----------------------

The Steam Runtime contains many third-party software packages under
various open-source licenses.

For full source code, please see the version-numbered subdirectories of
<https://repo.steampowered.com/steamrt-images-scout/snapshots/>
corresponding to the version numbers listed in VERSIONS.txt.
