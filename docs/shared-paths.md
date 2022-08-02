# Paths shared between host system and container runtime

<!-- This document:
Copyright 2022 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

[[_TOC_]]

## Introduction

When using the [Steam container runtime framework](container-runtime.md),
not all filesystem paths in a game's container are the same as for the
host system.

In general, we follow the same conventions as Flatpak: other than
the paths that are specially reserved by the container runtime, each
path that exists is either the same inside and outside the container
(we say that it is *shared*), or does not exist inside the container
(we say that it is *unshared*).
The precise sets of paths that are shared or unshared depends on
configuration and the state of the system.

The sharing and unsharing is implemented in the `pressure-vessel` tool.

## If under Flatpak

[under Flatpak]: #if-under-flatpak
[Steam Flatpak app]: https://github.com/flathub/com.valvesoftware.Steam

If Steam was run as a [Flatpak app][Steam Flatpak app],
and the container runtime was run from inside that Flatpak app, then the
directories that are shared are controlled by Flatpak.
The container runtime does not have the opportunity to alter this.

The container runtime *does* change what is in `/app` and `/usr`.
When the container runtime is run under a Flatpak app:

* `/app` is an empty directory
* `/usr` is the container runtime
* `/run/parent/app` is the Steam Flatpak app's `/app`
* `/run/parent/usr` is the Flatpak runtime that provides the
    Steam Flatpak app's `/usr`

All other directories, including the [home directory][], are controlled by
Flatpak and not by the container runtime.
Flatpak's mechanisms such as `flatpak override` must be used to control
whether directories such as removable media are shared with the Steam
Flatpak app or not.

The rest of this document assumes that Steam was run as a native
application on the host machine, with no container confinement.

## Always shared

[Always shared]: #always-shared

These paths are always, or nearly always, shared with the game's container
if they exist:

* `/dev` (RW): Device nodes

* `/dev/shm` (RW): Shared memory

* `/proc` (RW): Process management and system parameters

* `/sys` (normally RO, the `--devel` option makes it RW):
    Process management and system parameters

* `/tmp` (RW, except for `/tmp/.X11-unix`):
   Used for IPC by Steam and Proton

* `/gnu/store` (RO): Host OS on GNU Guix

* `/nix` (RO): Host OS on NixOS

* The current working directory (RW)
    (but only if it is *not* [the home directory][])

* `~/.steam` (RO)

Paths listed in these environment variables are generally shared read-only
with the game's container, but are sometimes filtered out or diverted to
different locations:

* `LD_AUDIT` (RO):
    Set by users. Arbitrary code to be loaded by all processes.
* `LD_PRELOAD` (RO):
    Set by users or by Steam. Arbitrary code to be loaded by all processes.

Paths listed in these environment variables, if set, are always shared
with the game's container:

* `PRESSURE_VESSEL_FILESYSTEMS_RO` (RO):
    Set by users. An override for debugging and development.
* `PRESSURE_VESSEL_FILESYSTEMS_RW` (RW):
    Set by users. An override for debugging and development.
* `PROTON_LOG_DIR` (RW):
    Set by users. Used by Proton to write out log files.
* `STEAM_COMPAT_CLIENT_INSTALL_PATH` (RW):
    Set internally by Steam. The Steam client itself.
* `STEAM_COMPAT_DATA_PATH` (RW):
    Set internally by Steam. A variable data directory used by Proton.
* `STEAM_COMPAT_INSTALL_PATH` (RW):
    Set internally by Steam. The game's installation directory.
* `STEAM_COMPAT_LIBRARY_PATHS` (RW):
    Set internally by Steam. All the Steam libraries containing
    compat tools that are active in this stack.
* `STEAM_COMPAT_MOUNTS` (RW):
    TODO: is this meant to be set by users, or used
    internally by Steam, or a mixture of both?
* `STEAM_COMPAT_SHADER_PATH` (RW):
    Set internally by Steam. The Steam shader cache.
* `STEAM_COMPAT_TOOL_PATHS` (RW):
    Set internally by Steam. All the compatibility tools in the stack
    (the container runtime, the layered scout-on-soldier runtime if used,
    and Proton if used).
* Some deprecated environment variables which are not listed here

## Never shared

[Never shared]: #never-shared

* `/app` is reserved by Flatpak, and as a consequence, it is reserved by
    the code we reuse from Flatpak.
    It is never shared.

* `/boot` is irrelevant to a user-level application.
    It is never shared.

* `/root` is irrelevant to a user-level application.
    It is never shared.

* `/run` is unique to the container: in general we cannot share this,
    because we need it for filesystem APIs like `/run/host` and
    `/run/pressure-vessel`.
    Some individual paths within `/run` are still shared, such as the
    D-Bus and Wayland sockets.

* `/run/gfx` is used internally by pressure-vessel and can never be shared.

* `/run/host` is used internally by pressure-vessel and can never be shared.

* `/run/pressure-vessel` is used internally by pressure-vessel and
    can never be shared.

* `/usr` is used by the container runtime libraries and executables.
    It cannot be shared (unless [not using a runtime][], but the Steam
    container runtime framework never operates in this mode).

* `/bin`, `/lib*`, `/sbin` are used by the container runtime to provide
    compatibility symlinks into `/usr`.
    It cannot be shared (unless [not using a runtime][], but the Steam
    container runtime framework never operates in this mode).

* `/usr/local` is treated the same as the rest of `/usr`, and therefore
   never shared.
    We cannot safely share the host `/usr/local` with the container, because
    directories like `/usr/local/bin` and `/usr/local/lib` are frequently
    part of libraries' search paths, and could therefore break games.

## Diverted into `/run/host`

* The host's `/etc` becomes `/run/host/etc`
* The host's `/usr` becomes `/run/host/usr`
* The host's `/bin`, `/lib*`, `/sbin` (if the OS is not merged-/usr)
    become `/run/host/bin`, etc.
* The host's `/usr/share/fonts` becomes `/run/host/fonts`
* The host's `/usr/local/share/fonts` becomes `/run/host/local-fonts`
* The user's `~/.local/share/fonts` becomes `/run/host/user-fonts`
* The host's `/usr/share/icons` becomes `/run/host/share/icons`

## The runtime

[The runtime]: #the-runtime

The main purpose of the container runtime is to provide Steam games with
a predictable library stack such as `soldier` or `sniper`, avoiding
system libraries that might be:

* missing
* too old (and therefore incompatible)
* too new (and therefore incompatible)

To achieve this, the container runtime manages `/usr`, `/bin`, `/lib*`
and `/sbin`: in the container environment, these directories are completely
replaced with shared libraries managed by Steam.
As a result, these locations cannot be shared.

Parts of `/etc` and `/var` need to be kept compatible with `/usr`,
while other parts need to be kept compatible with the host system.
As a result, the runtime manages these:

* In general, they come from the container runtime, to make games'
    execution environments as predictable as possible.
* Some files such as `/etc/hosts` come from the host system.
* Some files like `/etc/ld.so.cache` must be managed specially.

### If not using a runtime

[Not using a runtime]: #if-not-using-a-runtime

Steam's container runtime framework always uses a container runtime
such as `soldier` or `sniper`.
There is code in pressure-vessel to operate in a mode where the host
operating system's `/usr` is used as-is, instead of replacing it with a
different runtime environment.
This code path is documented here for completeness, but Steam's container
runtime framework never takes this code path, since that would defeat
the majority of its purpose by providing games with an unpredictable
runtime environment where the libraries are not necessarily compatible
with the game.

If this is done, then the following additional paths are shared with
the container:

* `/etc`
* Every member of `/run` except for:
    * `/run/gfx`
    * `/run/host`
    * `/run/pressure-vessel`
* `/tmp` (but in practice this is [always shared][] anyway)
* `/var`
* `/usr`
* `/bin`, `/lib*`, `/sbin` (if the OS is not merged-/usr)
* Every top-level directory not mentioned here, except for:
    * `/app`
    * `/boot`
    * `/root`
* `$STEAM_RUNTIME`

## The home directory

[The home directory]: #the-home-directory
[Home directory]: #the-home-directory

Normally, the container runtime framework shares the entire home directory
with the game's container.
This means that games are free to save files in `~/.local/share/Game` or
`~/.game`, for example.

The container runtime framework has a mode where it gives each game a
separate, private home directory, similar to how Flatpak apps can have
their own private home directory.
This is not done by default, and is not currently compatible with Steam
Cloud auto-sync.
If this mode is enabled, then the home directory is unshared, and a
game-specific private directory named similar to
`~/.var/app/com.steampowered.App1234` outside the container is mapped
to the home directory inside the container.
Developers of Flatpak apps will recognise this as similar to how
`flatpak build-finish --persist=.` would behave.

The private directory itself, such as `~/.var/app/com.steampowered.App1234`,
is also shared at its own path.

Even if the home directory is generally unshared, some individual
directories that are required for Steam to function correctly are
[always shared][].

`/var/tmp` is treated specially.
If [the home directory][] is shared, then so is `/var/tmp`.
If not, then `/var/tmp` is a bind-mount pointing to a medium-term temporary
directory in the game-specific private home directory, such as
`~/.var/app/com.steampowered.App1234/.cache/tmp` (this is consistent
with Flatpak's behaviour).

## Usually not shared

[Usually not shared]: #usually-not-shared

All top-level directories that are not otherwise mentioned are generally
not shared with the container. This includes:

* Users' home directories in `/home`, apart from
    [the current user's home directory][the home directory]
* Removable media, typically mounted in `/media`, `/mnt` or `/run/media`
* The FHS third-party software directory `/opt`
* The FHS server-data directory `/srv`
* Any custom top-level directory such as `/large-disk-drive`

## Summary

This assumes Steam is not running [under Flatpak][].

### FHS paths

* `/bin`: part of [the runtime][]
* `/boot`: [never shared][]
* `/dev`: [always shared][]
* `/etc`: managed by [the runtime][], some files come from the host
* `/home`
    * The current user's [home directory][] is usually shared
    * The rest of `/home` is [usually not shared][]
* `/lib`, `/libQUAL`: part of [the runtime][]
* `/media`: [usually not shared][]
* `/mnt`: [usually not shared][]
* `/opt`: [usually not shared][]
* `/proc`: [always shared][]
* `/sbin`: part of [the runtime][]
* `/usr`: part of [the runtime][]
* `/usr/local`: [never shared][] (technically part of the runtime)
* `/var`: managed by [the runtime][], some files come from the host
* `/var/run`: managed by [the runtime][] which makes it a symbolic link to `/run`
* `/var/tmp`: [usually not shared][]
* `/root`: [never shared][]
* `/run`: [never shared][] in general, but many locations inside it are shared
* `/sbin`: part of [the runtime][]
* `/srv`: [usually not shared][]
* `/sys`: [always shared][]
* `/tmp`:
    * `/tmp/.X11-unix` is [never shared][], but individual sockets appear there
    * The rest is [always shared][]

### Non-FHS paths

* `/app`: [never shared][]
* `/gnu`: [always shared][]
* `/nix`: [always shared][]
* `/overrides`: used internally by [the runtime][]
* Custom non-FHS top-level directories, e.g. `/large-disk-drive`:
    [usually not shared][]
