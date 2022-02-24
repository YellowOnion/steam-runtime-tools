# Steam Runtime distribution assumptions

<!-- This document:
Copyright 2022 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

[[_TOC_]]

## Introduction

This document describes some assumptions that are known to be made by
the Steam Runtime, the Steam container runtime framework
([Steam Linux Runtime](container-runtime.md),
[pressure-vessel](pressure-vessel.md)),
and/or Steam itself.

As a general, high-level rule of thumb, if something is true in all of
Arch Linux, Ubuntu and Fedora, then Steam or Steam games are likely to
assume that it is true.
If you are maintaining a Steam package or installer on another operating
system, it is likely to be useful to have an Arch or Ubuntu installation
available to use as a reference for how it normally fits together.

There are two main components involved in providing libraries to Steam and
Steam games:

- The [traditional Steam Runtime](ld-library-path-runtime.md),
    implemented in terms of `LD_LIBRARY_PATH`.
    Instances of this can be found in
    `~/.steam/root/ubuntu12_32/steam-runtime` and
    `~/.steam/root/ubuntu12_64/steam-runtime-heavy` in a working Steam
    installation.
    `~/.steam/root/ubuntu12_32/steam-runtime` provides
    [Steam Runtime 1 'scout'](https://gitlab.steamos.cloud/steamrt/steamrt/-/blob/steamrt/scout/README.md),
    based on Ubuntu 12.04.
    `~/.steam/root/ubuntu12_64/steam-runtime-heavy` provides
    [Steam Runtime 1½ 'heavy'](https://gitlab.steamos.cloud/steamrt/steamrt/-/blob/steamrt/heavy/README.md),
    based on Debian 8.

- The [container runtime framework](container-runtime.md),
    implemented in terms of containers launched by the pressure-vessel tool.
    Instances of this can be found in
    `.../steamapps/common/SteamLinuxRuntime_soldier` and perhaps
    `.../steamapps/common/SteamLinuxRuntime_sniper`.
    [Steam Runtime 2 'soldier'](https://gitlab.steamos.cloud/steamrt/steamrt/-/blob/steamrt/soldier/README.md)
    is based on Debian 10.
    [Steam Runtime 3 'sniper'](https://gitlab.steamos.cloud/steamrt/steamrt/-/blob/steamrt/sniper/README.md)
    is based on Debian 11.

To improve long-term compatibility with older native Linux games, there
is also a special container runtime `.../steamapps/common/SteamLinuxRuntime`,
which starts a Steam Runtime 2 'soldier' container and then uses a
Steam Runtime 1 'scout' environment inside that container.
This behaves like a combination of 'soldier' and 'scout'.

## Audience

This document is intended for operating system vendors who want to be
able to run Steam, and for operating system vendors or system administrators
who want to set up a container or chroot environment in which Steam can
be run.

## Exceptions to these assumptions

If a particular assumption is troublesome for a particular operating system,
we can discuss whether it can be avoided.
For example, the container runtime framework has several special cases
intended to benefit Flatpak, Exherbo, NixOS and Guix.
Please contact the Steam Runtime maintainers via
[its Github issue tracker](https://github.com/ValveSoftware/steam-runtime/issues)
if a similar special-case would be beneficial.

## Execution environment

Steam can be run directly on the host system, or in a container or chroot.
If it is in a container or chroot, then some functionality will be reduced,
especially if the container acts as a security boundary or if not all
filesystem-based sockets (in `/run` and similar directories) are shared.

If Steam is run in a container, the container runtime framework usually
requires the ability to launch a "nested" container.
As a special case, if Steam is run inside a Flatpak container by Flatpak
version 1.12 or later, then this is not required: the container framework
automatically detects Flatpak, and uses the same interfaces as the
`flatpak-spawn` tool, to ask Flatpak to launch additional containers
on its behalf.

The container runtime framework has the same
[user namespace requirements](https://github.com/flatpak/flatpak/wiki/User-namespace-requirements)
as Flatpak, as a result of using much of the same code to start its
containers.
The container runtime framework will use its own included copy of the
[bubblewrap](https://github.com/containers/bubblewrap) tool, `pv-bwrap`,
if possible.
In operating systems where a setuid version of the bubblewrap executable
is required, it is assumed to be available in the `PATH` as `bwrap`, or
installed at `/usr/local/libexec/flatpak-bwrap`, `/usr/libexec/flatpak-bwrap`
or `/usr/lib/flatpak/flatpak-bwrap` as part of Flatpak.

## Filesystem layout

In general, Steam assumes that it is run in a filesystem layout that is
approximately
[FHS](https://en.wikipedia.org/wiki/Filesystem_Hierarchy_Standard)-compliant,
with some minor variation such as the names of architecture-specific library
directories.
Non-FHS operating systems such as NixOS and Guix are likely to need to
run it in a chroot or container to achieve this.

The main prefix for operating system libraries is assumed to be `/usr`.

The container runtime framework assumes that libraries are found in one
of the following sets of paths:

* `/lib64` (`x86_64`) and `/lib` (`i386`)
* `/lib` (`x86_64`) and `/lib32` (`i386`)
* `/lib/x86_64-linux-gnu` and `/lib/i386-linux-gnu`
* any of the above with `/usr` prepended
* `/usr/x86_64-pc-linux-gnu/lib` and `/usr/i686-pc-linux-gnu/lib`
* more paths can be added if necessary,
    [contact the maintainers](https://github.com/ValveSoftware/steam-runtime/issues)

The container runtime assumes that the canonicalized paths of all
libraries in those locations are below either those locations (preferred),
or below `/gnu/store` or `/nix`.

The glibc locale data conventionally stored in `/usr/lib/locale` is
assumed to be found in one of the following paths:

* `/usr/lib/locale` (preferred)
* `/usr/x86_64-pc-linux-gnu/lib/locale` and/or `/usr/i686-pc-linux-gnu/lib/locale`
* more paths can be added if necessary,
    [contact the maintainers](https://github.com/ValveSoftware/steam-runtime/issues)

The glibc locale data conventionally stored in `/usr/share/i18n` is
assumed to be found in that directory.

Many files are assumed to have their conventional paths in `/etc`,
including:

* `/etc/hosts`
* `/etc/host.conf`
* `/etc/resolv.conf`
* `/etc/passwd`
* `/etc/group`
* `/etc/amd` (if present)
* `/etc/drirc` (if present)
* `/etc/nvidia` (if present)

The container runtime assumes that `/app` and `/usr` do not need to be
shared with containers in order to share personal files.

If the D-Bus/systemd machine ID is present, it is assumed to be located at
`/etc/machine-id` or `/var/lib/dbus/machine-id`.

Sockets used for IPC mechanisms are assumed to have their conventional
paths, for example:

* X11: `/tmp/.X11-unix/X0`, replacing `0` with the display number
* Wayland (optional): `$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY` or
    `$XDG_RUNTIME_DIR/wayland-0`
* D-Bus system bus (recommended): `/var/run/dbus/system_bus_socket` or
    `/run/dbus/system_bus_socket`
* D-Bus session bus (recommended): as described by `$DBUS_SESSION_BUS_ADDRESS`,
    or `$XDG_RUNTIME_DIR/bus`
* PulseAudio (recommended): as described by `$PULSE_SERVER`, or see
    `pressure-vessel/flatpak-run.c` in `steam-runtime-tools` source code
    for the full search path
* Pipewire (optional): `$XDG_RUNTIME_DIR/pipewire-0`, or see
    `pressure-vessel/wrap-pipewire.c` in `steam-runtime-tools` source code
    for the full search path

## ABIs

Steam requires GNU libc (glibc).
Distributions that are based on a different libc, such as musl, are
likely to need to run Steam in a chroot or container.

Both 32-bit and 64-bit x86 libraries are needed.

The 64-bit libraries need to be compatible with the `x86_64-linux-gnu`
GNU system type, also known as `x86_64` or AMD64.
More formally, they need to be compatible with the ABI of the
`x86_64-linux-gnu` [multiarch tuple](https://wiki.debian.org/Multiarch/Tuples)
as used in Debian.
The container runtime framework uses multiarch tuples as its preferred
internal representation for ABIs, so you will see it appearing in various
paths, even when running on a non-Debian-derived operating system.

The 32-bit libraries need to be compatible with the `i386-linux-gnu`
GNU system type, also known as `i386` or IA32.
More formally, they need to be compatible with the ABI of the
`i386-linux-gnu` multiarch tuple as used in Debian.
Higher 32-bit functionality levels like `i686-linux-gnu` are
backwards-compatible with `i386-linux-gnu`, and in practice modern
distributions are usually compiled for `i586` or higher, which is preferred.

The dynamic linker needs to be available at its interoperable paths:

* `i386-linux-gnu` compatible: `/lib/ld-linux.so.2`
* `x86_64-linux-gnu`: `/lib64/ld-linux-x86-64.so.2`

These may be symbolic links to a different location if desired.
However, they must exist, and this is not negotiable: they are a baseline
requirement for being able to run pre-existing binaries built on other
Linux operating systems.

The container runtime framework assumes that executables provided by
the operating system will have one of the following paths set as their
ELF interpreter:

* The interoperable paths above (strongly preferred)
* `/usr/lib/ld-linux-x86-64.so.2`
* `/usr/x86_64-pc-linux-gnu/lib/ld-linux-x86-64.so.2`
* `/usr/i686-pc-linux-gnu/lib/ld-linux.so.2`
* A path below `/gnu/store` or `/nix`
* more paths can be added if necessary,
    [contact the maintainers](https://github.com/ValveSoftware/steam-runtime/issues)

## Shared libraries

All shared libraries that the OS vendor expects Steam to use, other than
those provided by Steam itself, need to appear in the dynamic linker's
search path.
There are two ways this can happen:

1. They can be in a directory that is listed in the `LD_LIBRARY_PATH`
    environment variable
2. (preferred) They can be listed in the `ld.so` cache, with one of the
    following filenames:
    - `/etc/ld.so.cache` (glibc default, preferred)
    - `/var/cache/ldconfig/ld.so.cache`
    - `/etc/ld-x86_64-pc-linux-gnu.cache` or `/etc/ld-i686-pc-linux-gnu.cache`
    - more paths can be added if necessary,
        [contact the maintainers](https://github.com/ValveSoftware/steam-runtime/issues)

As a result of implementation limitations in
[libcapsule](https://gitlab.collabora.com/vivek/libcapsule), the
container runtime does not currently support following the `DT_RUNPATH`
and `DT_RPATH` ELF headers.
On operating systems that make extensive use of these headers, such as
NixOS, it will be necessary to copy or symlink the required libraries into
a location that appears in the search path.

The shared libraries found in those locations need to include at least:

* glibc itself
* Graphics drivers (see below)
* All of the dependencies of the above, recursively, all the way down
    the stack to `libc.so.6`

The Steam Runtime assumes that running `/sbin/ldconfig -XNv` will list
all of the directories that were searched while building the `ld.so`
cache, and the libraries that they contain, in the same format that is
used on typical distributions such as Debian.
This command does not need to list libraries in the `LD_LIBRARY_PATH`.

On entry to Steam code, shared libraries that are shipped with Steam
itself, including the Steam Runtime in
`~/.steam/root/ubuntu12_32/steam-runtime`, should **not** be in the
`LD_LIBRARY_PATH`.
The Steam startup scripts will add the Steam Runtime
to the `LD_LIBRARY_PATH` automatically, and doing this redundantly
might cause additional problems.

When the container framework replaces `/etc/ld.so.cache` and the other
paths listed above, it assumes that this is enough to make `ld.so` load
the libraries listed in the new cache (in other words, glibc needs to be
configured so that `ld.so` will read from one of those paths).
It is assumed to be possible to mix `i386` and `x86_64` libraries in a
single `ld.so.cache`.

The operating system should not edit the Steam Runtime in-place or
attempt to disable it.

For the `LD_LIBRARY_PATH`-based 'scout' and 'heavy' runtimes, the
operating system may provide shared libraries that are newer than
the corresponding libraries in the Steam Runtime, backwards-compatible
with the Steam Runtime libraries, and have the same `DT_SONAME` ELF header.
If it does, those libraries will generally be used in preference to
the corresponding libraries in the Steam Runtime.
For example, an operating system wishing to replace the Steam Runtime's
`usr/lib/i386-linux-gnu/libfontconfig.so.1` with a newer or patched
version can provide its own 32-bit (`i386`) version of
`libfontconfig.so.1`, with an equal or newer version.
These will be used automatically.

In general, it is not possible to "downgrade" a shared library in a
supportable way, because this would break games' expectations.
The operating system should not attempt to force use of a shared library
that is an older version than the one in the Steam Runtime.

The container runtime will usually *not* use newer shared libraries,
unless they happen to be part of the dependency stack of a graphics driver.
This is intentional, and is done to provide a predictable environment
across distributions.

## Graphics drivers

The operating system needs to provide GLX drivers, for which a GLVND-based
driver stack is recommended.
For a GLVND-based driver stack, the driver libraries `libGLX_*.so.0`
need to be be in the dynamic linker search path (`ld.so` cache or
`LD_LIBRARY_PATH`).

EGL drivers are recommended.
They are located in the same way that is documented for the upstream
libglvnd project:
[EGL ICD enumeration](https://github.com/NVIDIA/libglvnd/blob/v1.4.0/src/EGL/icd_enumeration.md).
If the operating system patches GLVND to have a non-standard search path,
the container runtime will not usually be able to find drivers whose
JSON manifests appear in non-standard locations.
If the operating system needs a non-standard search path to be used,
then Steam should be run with the `__EGL_VENDOR_LIBRARY_DIRS` or
`__EGL_VENDOR_LIBRARY_FILENAMES` environment variable set appropriately.

Vulkan drivers are recommended, and will usually be needed for newer
games and for Proton.
They are located in the same way that is documented for the upstream
Vulkan-Loader:
[Driver Interface](https://github.com/KhronosGroup/Vulkan-Loader/blob/sdk-1.2.198/docs/LoaderDriverInterface.md).
If the operating system patches Vulkan-Loader to have a non-standard
search path, the container runtime will not usually find drivers whose
JSON manifests appear in non-standard locations.
If the operating system needs a non-standard search path to be used,
then Steam should be run with the `XDG_DATA_DIRS` or `VK_ICD_FILENAMES`
environment variable set appropriately.

If the graphics drivers are based on Mesa, then the DRI drivers such
as `swrast_dri.so` are assumed to be available in the search path
defined by `LIBGL_DRIVERS_PATH`, or in a `dri` subdirectory of the
directory containing the loader library `libGLX_mesa.so.0`,
`libEGL_mesa.so.0` or `libGL.so.1`.
Some special cases for the Flatpak freedesktop.org SDK are also checked
(see steam-runtime-tools source code for details).
If the operating system needs a non-standard search path to be used,
then Steam should be run with the `LIBGL_DRIVERS_PATH` environment variable
set appropriately.

VA-API drivers are optional.
If present, they are assumed to be compatible with `libva.so.2`, and
available in either the search path defined by `LIBVA_DRIVERS_PATH`, or
a `dri` subdirectory of the directory containing the loader library
`libva.so.2`.
Some special cases for the Flatpak freedesktop.org SDK are also checked
(see steam-runtime-tools source code for details).
If the operating system needs a non-standard search path to be used,
then Steam should be run with the `LIBGL_DRIVER_PATH` environment variable
set appropriately.

VDPAU drivers are optional.
If present, they are assumed to be available in either the search path
defined by `VDPAU_DRIVER_PATH`, or a `vdpau` subdirectory of the directory
containing the loader library `libvdpau.so.1`, or the `ld.so` cache,
or the `LD_LIBRARY_PATH`.
Some special cases for the Flatpak freedesktop.org SDK are also checked
(see steam-runtime-tools source code for details).
If the operating system needs a non-standard search path to be used,
then Steam should be run with the `VDPAU_DRIVER_PATH` environment variable
set appropriately.

If any of the graphics drivers depend on `libdrm.so.2`, it is assumed to be
available in one of the conventional library directories listed in
"Filesystem layout" above.
Its associated data files are assumed to be available in a `share`
directory in the same prefix, for example `/usr/lib64/libdrm.so.2` implies
`/usr/share`.
Using `/usr/share` is recommended.

If the graphics drivers are based on Mesa, their associated data files such
as the `drirc.d` directory are assumed to be available in a `share` directory
in the same prefix, for example `/usr/lib/x86_64-linux-gnu/libGLX_mesa.so.0`
implies `/usr/share`.
Using `/usr/share` is recommended.

If the NVIDIA proprietary graphics drivers are used, their associated data
files are assumed to be available in `/usr/share/nvidia` (preferred),
or in a `share` directory in the same prefix.

## Environment variables

On entry to Steam code, the following environment variables should
**not** be present in the environment.

* `STEAM_RUNTIME` *(debug) (internal)*
* `STEAM_RUNTIME_SCOUT` *(debug)*
* `STEAM_RUNTIME_HEAVY` *(debug)*
* `STEAM_RUNTIME_DEBUG` *(debug)*
* `STEAM_RUNTIME_DEBUG_DIR` *(debug)*
* `STEAM_RUNTIME_LIBRARY_PATH` *(internal)*
* `STEAM_RUNTIME_PREFER_HOST_LIBRARIES` *(obsolete)*

Variables marked *(debug)* can be set for development and debugging,
but this is unsupported, and should not be done routinely by end users
or by "official" packages.
Variables marked *(internal)* are intended to be set by the Steam Runtime
infrastructure, and setting them externally is likely to cause problems.
For historical reasons, `STEAM_RUNTIME` has a mixture of both of these roles.

## Command-line tools

Steam and the Steam Runtime infrastructure assume that some common
command-line tools are available at their conventional locations:

* `/bin/sh`
* `/bin/bash`
* `/sbin/ldconfig`
* `/usr/bin/env`

(This list is unlikely to be complete.)

It is OK for these to be symbolic links to somewhere else, as long as
they resolve to the appropriate executable.
For example, on Arch Linux and other "merged-`/usr`" operating systems,
`/bin` is a symbolic link to `usr/bin`, so the shell is really `/usr/bin/sh`.

Other command-line tools are assumed to be available in the
`PATH`, including:

* GNU coreutils
* `file`
* `realpath` (if coreutils is an older version that does not provide this)
* `xz`
* `zenity` (not strictly required, but recommended)

## Audio

Steam assumes that either PulseAudio or Pipewire is used for audio mixing
and stream management.
If Pipewire is used, then compatibility with PulseAudio (the
`pipewire-pulse` service) should be enabled.

Other audio frameworks like JACK, ALSA dmix, OSS and RoarAudio are not
supported, although they might work with limited functionality.

## Locales

The glibc locale data conventionally stored in `/usr/lib/locale` and
`/usr/share/i18n` is assumed to be available in one of the locations
described in the "Filesystem layout" section above.

For historical reasons, some games rely on having an `en_US.UTF-8` locale
available.
Generating this locale is recommended, even if it is not used as the
preferred locale for any users.

Some games might rely on having a `C.UTF-8` locale compatible with the
one provided by glibc 2.35, and previously available in distributions
such as Fedora and Debian.

## Checking these assumptions

The `steam-runtime-system-info` command-line tool can automatically
check for various common issues, as well as providing information
about the drivers, libraries, etc. that will be used.
It is used as the implementation of several of the compatibility checks
in Steam's *Help -> System Information*.

All of the diagnostic commands described in this section should be run
from the same environment in which Steam will be run.
In operating systems that use a more-FHS-compatible container to run Steam,
such as NixOS, these commands should be run inside that container.
Similarly, in Flatpak, these commands should be run inside the shell
provided by `flatpak run --command=bash com.valvesoftware.Steam`.

`steam-runtime-system-info` is primarily designed to be run inside a
Steam Runtime environment, either `LD_LIBRARY_PATH` or container.

For the scout `LD_LIBRARY_PATH` runtime, *Help -> System Information*
does the equivalent of these shell commands, to enter the scout
environment and inspect it:

```
~/.steam/root/ubuntu12_32/steam-runtime/setup.sh
~/.steam/root/ubuntu12_32/steam-runtime/run.sh -- steam-runtime-system-info
```

For the soldier container runtime, *Help -> System Information* does
the equivalent of this, to enter the soldier environment and inspect it:

```
.../steamapps/common/SteamLinuxRuntime_soldier/run -- steam-runtime-system-info
```

The output is in JSON format.
Potential issues are diagnosed as non-empty lists labelled `issues`.

To check for operating system compatibility with Steam assumptions,
a standalone version of `steam-runtime-system-info` is available
as part of the pressure-vessel tool.
This can be downloaded as part of the `SteamLinuxRuntime_soldier`
container tool, or directly from
<https://repo.steampowered.com/pressure-vessel/snapshots/latest/>,
and run as:

```
.../pressure-vessel/bin/steam-runtime-system-info
```

When run like this, some of the reported issues can safely be ignored,
because the tool normally expects to be run from within a Steam Runtime
environment, but in this case it is not:

* `steam-installation` -> `issues` -> `steamscript-not-in-environment`
* `runtime` -> `issues` -> `not-in-ld-path`
* `runtime` -> `issues` -> `not-in-path`
* `runtime` -> `issues` -> `not-in-environment`
* `architectures` -> (any) -> `library-issues-summary` -> `unknown-expectations`
* `libglib-2.0.so.0: cannot open shared object file: No such file or directory`

A future version of the diagnostic tool might provide a way to suppress
these checks when run outside Steam.
