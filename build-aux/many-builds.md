# NAME

build-aux/many-builds.py - build steam-runtime-tools and pressure-vessel

# SYNOPSIS

**build-aux/many-builds.py** [*OPTIONS*] **setup** [*MESON SETUP OPTIONS*]

**build-aux/many-builds.py** [*OPTIONS*] **build** [*NINJA OPTIONS*]

**build-aux/many-builds.py** [*OPTIONS*] **install** [*NINJA OPTIONS*]

**build-aux/many-builds.py** [*OPTIONS*] **test** [*MESON TEST OPTIONS*]

**build-aux/many-builds.py** [*OPTIONS*] **clean** [*NINJA OPTIONS*]

# DESCRIPTION

`build-aux/many-builds.py` partially automates developer setup and
testing for steam-runtime-tools and pressure-vessel.

# REQUIREMENTS

By default (when not using the **--docker** or **--podman** options),
`many-builds.py` has
[the same user namespace requirements as Flatpak](https://github.com/flatpak/flatpak/wiki/User-namespace-requirements).

# OPTIONS

**--srcdir** *PATH*
:   Relative or absolute path to the `steam-runtime-tools` source
    directory. The default is the current working directory.

**--builddir-parent** *PATH*
:   Relative or absolute path to the parent directory for all builds.
    The default is `_build`. Individual builds and additional required
    files will appear in subdirectories such as `_build/host` and
    `_build/containers`.

    Instead of using this option, it is usually more convenient to make
    a symbolic link such as `_build -> ../builds/steam-runtime-tools`
    if a separate build location is desired, and then use the default.

**--docker**
:   Use **docker**(1) to do builds. The default is to use **bwrap**(1).
    Building with Docker requires that the invoking user is either in
    the `docker` Unix group or able to run **sudo**(8), either of which
    is equivalent to full root privileges, so this mode cannot be used on
    machines where gaining full root privileges would be unacceptable.

**--podman**
:   Use **podman**(1) to do builds. The default is to use **bwrap**(1).
    Building with Podman requires newuidmap, newgidmap and a uid range
    configured for the current user in /etc/subuid and /etc/subgid:
    see [Troubleshooting](https://github.com/containers/podman/blob/main/troubleshooting.md)
    for details.

# STEPS

## setup

The **setup** step is similar to **meson setup**. It also downloads
the necessary Steam Linux Runtime container runtime images to be able
to test **pressure-vessel-wrap**, and the necessary SDK image to be
able to compile for Steam Runtime 1 'scout'.

By default, the same SDK sysroot tarball that will be used for testing
is used for compilation. If the **--docker** or **--podman** options
are used, the official OCI container for the Steam Runtime 1 SDK will
be downloaded and used for compilation.

## build

The **build** step is similar to **meson compile**, but it only builds
a subset of the configurations from the **setup** step.

More specialized configurations such as *${builddir\_parent}*/**coverage**
can be compiled in the usual way with commands like
`meson compile -C _build/coverage` or `ninja -C _build/coverage`.

## install

The **install** step builds a complete relocatable version of the
Steam Runtime 1 'scout' builds of pressure-vessel. This can be
found in `_build/scout-relocatable`, and is also copied into
`_build/containers/pressure-vessel` for testing.

The other builds are not installed.

## test

The **test** step runs unit tests, with as comprehensive a test coverage
as possible. This will take a while.

## clean

The **clean** step is similar to **ninja -C ... clean**.

<!-- vim:set sw=4 sts=4 et: -->
