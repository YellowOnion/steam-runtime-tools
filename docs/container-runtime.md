# Introduction to the Steam container runtime framework

<!-- This document:
Copyright 2022 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

The Steam container runtime framework, often referred to as
*Steam Linux Runtime*, is a collection of container environments
which can be used to run Steam games on Linux in a relatively predictable
container environment, even when running on an arbitrary Linux
distribution which might be old, new or unusually set up.

Instead of forming a `LD_LIBRARY_PATH` that merges the host OS's shared
libraries with the shared libraries provided by Valve, these new runtimes
use Linux namespace (container) technology to build a more predictable
environment.

It is implemented as a collection of Steam Play compatibility tools.

More technical background on this work is available in a talk recorded at
FOSDEM 2020:
<https://archive.fosdem.org/2020/schedule/event/containers_steam/>.
Many of the features described as future work in that talk have now been
implemented and are in active use.

## `pressure-vessel`

The core of all of these compatibility tools is
[pressure-vessel](pressure-vessel.md),
which combines application-level libraries from the Steam Runtime
with graphics drivers from the host operating system, resulting in a
system that is as compatible as possible with the Steam Runtime
while still having all the necessary graphics drivers to work with recent
GPU hardware.

The Steam Play compatibility tools automatically run pressure-vessel
when necessary.

## Steam Runtime 2, `soldier`

Steam Runtime 2, `soldier`, is a newer runtime than
[scout](ld-library-path-runtime.md), based on Debian 10 (released in 2019).
Most of its libraries are taken directly from Debian, and can benefit
from Debian's long-term security support.
Selected libraries that are particularly important for games, such as
SDL and Vulkan-Loader, have been upgraded to newer versions backported
from newer branches of Debian.

soldier is designed to be used as a container runtime for `pressure-vessel`,
and [cannot be used](#why) as a
[`LD_LIBRARY_PATH` runtime](ld-library-path-runtime.md).

At the time of writing, soldier is used as a runtime environment for
Proton 5.13 or later, which are compiled against the newer library stack
and would not be compatible with scout.

Native Linux games that require soldier cannot yet be released on Steam.
This will hopefully become possible in future.

The *Steam Linux Runtime - soldier* compatibility tool, app ID 1391110,
is automatically downloaded to your Steam library as
`steamapps/common/SteamLinuxRuntime_soldier` when you select a version
of Proton that requires it.
It can also be installed by running this command:

    steam steam://install/1391110

Documentation in the `steamrt` "metapackage" provides
[more information about soldier](https://gitlab.steamos.cloud/steamrt/steamrt/-/blob/steamrt/soldier/README.md).

## Steam Linux Runtime (scout-on-soldier)

Steam offers a large number of older native Linux games.
Some of these games, such as Team Fortress 2, were carefully compiled in
a strict `scout` environment, so that they can run in the
[scout `LD_LIBRARY_PATH` runtime](ld-library-path-runtime.md),
or in any environment that provides at least the same libraries as scout.

Unfortunately, many native Linux games have been compiled in a newer
environment, and will only work in the `LD_LIBRARY_PATH` runtime
if the host operating system happens to provide libraries that are newer
than the ones in `scout`, while still being compatible with the game's
assumptions.
This is not a stable situation: a game that happened to work in Ubuntu
20.04 could easily be broken by a routine upgrade to Ubuntu 22.04.

The *Steam Linux Runtime* compatibility tool, app ID 1070560, uses the
same container technology as `soldier` to mitigate this problem.
It will automatically be downloaded to your Steam library as
`steamapps/common/SteamLinuxRuntime` if it is selected to run a particular
game, or if a game requires it.
It can also be installed by running this command:

    steam steam://install/1070560

It is implemented by entering a `soldier` container, and then setting up
a `scout` `LD_LIBRARY_PATH` runtime inside that container.

Dota 2 recently started to
[use the Steam Linux Runtime compatibility tool by default](https://store.steampowered.com/news/app/570/view/4978168332488878344).
This mechanism is not yet available for third-party games, but will
hopefully become available in future.

## Steam Runtime 3, `sniper`

Steam Runtime 3, `sniper`, is another newer runtime based on Debian 11
(released in 2021).
It is very similar to `soldier`, except for its base distribution being
2 years newer: this means its core libraries and compiler are also
approximately 2 years newer.

Games that require sniper cannot yet be released on Steam.
This will hopefully become possible in future.

The *Steam Linux Runtime - sniper* compatibility tool, app ID 1628350,
will automatically be downloaded to your Steam library as
`steamapps/common/SteamLinuxRuntime_sniper` if a game requires it.
It can also be installed by running this command:

    steam steam://install/1628350

Documentation in the `steamrt` "metapackage" provides
[more information about sniper](https://gitlab.steamos.cloud/steamrt/steamrt/-/blob/steamrt/sniper/README.md).

## <span id="why">Why the container runtimes are necessary</span>

The [traditional `LD_LIBRARY_PATH` runtime](ld-library-path-runtime.md)
only works because modern host OSs are strictly newer than it.
Making a `LD_LIBRARY_PATH`-based runtime reliable is difficult, especially
since we want it to be runnable on host OSs that have some packages that
are older than the runtime, allowing users of older LTS distributions to
run the latest games.

Some libraries cannot be bundled in a `LD_LIBRARY_PATH` for technical
reasons (mainly glibc and graphics drivers). A `LD_LIBRARY_PATH` runtime
needs to get these from the host system, and they need to be at least the
version it was compiled against. This is fine for scout, which is very
old, but would not be OK for a Debian 10-based runtime, which wouldn't work
on (for example) Ubuntu 18.04.

Some libraries *can* be bundled, but need to be patched to search for
plugins in different places (either inside the runtime itself, or in
multiple distro-dependent places), which is not really sustainable.
Avoiding the need to patch these libraries greatly reduces the time
required to update them, ensuring that we can apply security and
bug-fix updates as needed.

Using namespace (container) technology to replace `/usr` with the
runtime's libraries sidesteps both these problems.

## Reporting issues

Bugs and issues in the Steam Runtime should be reported to the
[steam-runtime project on Github](https://github.com/ValveSoftware/steam-runtime).

## Acknowledgements

The libraries included in the container runtimes are derived
from [Debian](https://www.debian.org/) and [Ubuntu](https://ubuntu.com/)
packages, and indirectly from various upstream projects.
See the copyright information included in the Steam Runtime for details.

The container technology used in `pressure-vessel` is heavily based on
code from [Flatpak](https://flatpak.org/), and makes use of the
lower-level components [bubblewrap](https://github.com/containers/bubblewrap)
and [libcapsule](https://gitlab.collabora.com/vivek/libcapsule/).
libcapsule is heavily based on internal code from glibc's dynamic linker,
and of course, all of this container/namespace juggling relies on features
contributed to the Linux kernel.
