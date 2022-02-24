Contributing to pressure-vessel
===============================

<!-- This document:
Copyright © 2019-2021 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

See [documentation](../docs/pressure-vessel.md) for general information about
`pressure-vessel`,
and [README](README.md) for information about building and testing it.

More extensive notes should go here eventually, but aren't
available yet. Sorry!

Adding dependencies
-------------------

Some of the `pressure-vessel` tools are designed to run on the host
system, which can in principle be anything, so think carefully before
adding new dependencies.

It's OK to depend on these from the host system:

  * Linux kernel
      - Linux 3.8 (first version to have mount and user namespaces)
        is a hard dependency.
      - Linux 3.9 (first version to let unprivileged users create a
        `tmpfs` in a user namespace) is required if your host system
        does not provide a setuid `bwrap`.
        This version is available in (at least) Debian 8+,
        Ubuntu 14.04+, RHEL 7+, openSUSE 42.1+, SteamOS 1+.
      - Linux 3.15 (first version to have `F_OFD_SETLCKW`) is
        strongly recommended at runtime. We should try to have fallback
        paths for older kernels, but if that becomes impossible, it's
        OK to bump this up to a hard dependency.
        This version is available in (at least) Debian 8+, Ubuntu 16.04+,
        RHEL 8+, openSUSE 42.1+, SteamOS 2+.
  * GNU libc
      - glibc 2.15 (SteamRT 1 'scout', Ubuntu 12.04 'precise') is
        a hard dependency, and we currently compile against this version
        This version is available in (at least) Debian 8+, Ubuntu 12.04+,
        RHEL 7+, openSUSE 42.1+, SteamOS 1+.
      - glibc 2.19 (SteamOS 2 'brewmaster', Ubuntu 14.04 'trusty',
        Debian 8 'jessie') is strongly recommended at runtime. We should
        try to have fallback paths for older glibc, but if that becomes
        impossible, it's OK to bump this up to a hard dependency.
        This version is available in (at least) Debian 8+, Ubuntu 14.04+,
        RHEL 8+, openSUSE 42.1+, SteamOS 2+.
  * setuid `bwrap`, if the kernel requires it
    (always required on RHEL, runtime-configurable and required by default
    on Debian, runtime-configurable but not required by default on Arch)
      - It can be the version bundled in Flatpak, or standalone

We bundle libraries from the Steam Runtime 1 'scout' for other
dependencies. See `build-relocatable-install.py` for those. This means
we can only rely on those libraries being at least as new as the ones
in scout, which are pretty old.

The scout build environment has backports of Python 3.5 and Meson, so we
can rely on those at build-time.

If we gain a dependency on glibc 2.19 or other newer libraries, we'll have
to increase the build environment to SteamRT 1½ 'heavy', and backport
Python 3.5 and Meson to 'heavy'.
