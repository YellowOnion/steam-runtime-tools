# Introduction to the `LD_LIBRARY_PATH` Steam Runtime

<!-- This document:
Copyright 2019-2022 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

## The `scout` runtime

The Linux version of Steam runs on many Linux distributions, ranging
from the latest rolling-release distributions like Arch Linux to older
LTS distributions like Ubuntu 14.04.
To achieve this, it uses a special library stack, the *Steam Runtime*,
which is installed in `~/.steam/root/ubuntu12_32/steam-runtime`.
This is Steam Runtime version 1, codenamed `scout` after the Team
Fortress 2 character class.

The Steam Runtime provides many shared libraries that are commonly
required by games, as well as libraries that are required by Steam itself.
In general, they are based on the versions from Ubuntu 12.04: however,
newer versions of key libraries such as SDL and Vulkan-Loader are
backported into `scout` so that games can rely on having these newer
versions available.
Each library is provided as both a 64-bit build and a 32-bit build.

The Steam client itself is run in an environment that adds the shared
libraries to the library loading path, using the `LD_LIBRARY_PATH`
environment variable.
Games that do not use a [container runtime](container-runtime.md)
are also run in this environment.
Here's a diagram of the situation:

    |----------------------------
    |                    Host system
    |  /usr/bin/steam
    |    |
    |    \- steam.sh
    |         |
    |      .- \-run.sh- - - - - - - - - -
    |      .    |            steam-runtime (scout)
    |      .    |
    |      .    \- steam binary
    |      .         |
    |      .         \- The game

Outside the Steam Runtime (outside the dotted line), all libraries come
from the operating system or from user configuration.

In the Steam Runtime environment (inside the dotted line), each library
required by a game or application is chosen like this:

  * If the library is not provided by the Steam Runtime, then the version
    from the operating system or user configuration is used.

      * The C runtime library (`glibc`) cannot be provided by the
        Steam Runtime for technical reasons involving cross-distribution
        compatibility.
        Steam has to rely on the operating system to provide 32- and 64-bit
        `glibc`.
      * Graphics drivers are also not provided by the Steam Runtime,
        because this would prevent Steam from working correctly on the
        latest GPUs.

  * In general, if there is a version of the library provided by the
    operating system or user configuration, with:

      * the same word size that is required (32-bit or 64-bit)
      * the same `SONAME`, indicating drop-in compatibility with the
        version in `scout`
      * a version equal to or greater than the version of the equivalent
        library in `scout`

    then that library will be used.

  * If the library in `scout` is *newer* than the library provided by the
    operating system, then the library from `scout` must be used.
    This is because games that were compiled against `scout` might be
    relying on newer features that are not available in the operating
    system's version of the library.

  * Otherwise, the library from `scout` is used.
    This ensures that games compiled against older libraries like
    `libpng12.so.12` can run successfully, even if the operating system
    no longer provides a compatible library.

  * A small number of libraries that are known to have suffered from
    compatibility breaks in the past are always taken from `scout`,
    even if the operating system provides a version that claims to be
    compatible.
    This ensures that games that use these libraries will see a
    compatible version.
    For example, `libcurl` has special treatment.
    These libraries are said to have been "pinned", by adding symbolic
    links in the `pinned_libs_32` and/or `pinned_libs_64` directories.

Documentation in the `steamrt` "metapackage" provides
[more information about scout](https://gitlab.steamos.cloud/steamrt/steamrt/-/blob/steamrt/scout/README.md).

More technical background on the Steam Runtime is available in a talk
recorded at FOSDEM 2020:
<https://archive.fosdem.org/2020/schedule/event/containers_steam/>.

## The `heavy` runtime

The `steamwebhelper` component that is used for the Steam user interface
requires newer libraries than the ones available in `scout`.
To avoid destabilizing `scout`, it has its own set of libraries,
referred to as Steam Runtime 1½ `heavy`.

This behaves in the same way as the `scout` runtime, but it contains
fewer libraries (only the ones that are needed for the `steamwebhelper`),
and it is newer than `scout` (it's based on Debian 8 instead of
Ubuntu 12.04).

Documentation in the `steamrt` "metapackage" provides
[more information about heavy](https://gitlab.steamos.cloud/steamrt/steamrt/-/blob/steamrt/heavy/README.md).

## Reporting issues

Bugs and issues in the Steam Runtime should be reported to the
[steam-runtime project on Github](https://github.com/ValveSoftware/steam-runtime).

## Steam Linux Runtime (container runtimes)

The [Steam Linux Runtime](container-runtime.md) container runtimes
are a newer approach to running Steam games, and are described
[elsewhere](container-runtime.md).

## Acknowledgements

The libraries included in the Steam Runtime are derived
from [Debian](https://www.debian.org/) and [Ubuntu](https://ubuntu.com/)
packages, and indirectly from various upstream projects.
See the copyright information included in the Steam Runtime for details.
