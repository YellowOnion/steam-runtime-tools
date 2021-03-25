# Contributing to steam-runtime-tools

## Issue tracking

Our main bug tracking system for the whole Steam Runtime is
<https://github.com/ValveSoftware/steam-runtime/issues>. Before reporting
an issue, please take a look at the
[bug reporting information](https://github.com/ValveSoftware/steam-runtime/blob/master/doc/reporting-steamlinuxruntime-bugs.md)
to make sure your issue report contains all the information we need.

The issue tracker on our Gitlab installation, `gitlab.steamos.cloud`, is
primarily used by steam-runtime-tools developers to track issues for
which we already know the technical details of what is happening.

## Contributing code

At the moment our Gitlab installation, `gitlab.steamos.cloud`, is not
set up to receive merge requests from third-party contributors. However,
git is a distributed version control system, so it is possible to push a
clone of the steam-runtime-tools git repository to some other git hosting
platform (such as Github or `gitlab.com`) and send a link to a proposed
branch via an issue report.

If you want to contribute code to steam-runtime-tools, please include a
`Signed-off-by` message in your commits to indicate acceptance of the
[Developer's Certificate of Origin](https://developercertificate.org/)
terms.

## Doing test-builds

For quick tests during development, you can build on any reasonably
modern Linux distribution. Ubuntu 18.04 and Debian 10 are examples of
suitable versions.

To check that everything works in the older Steam Runtime 1 'scout'
environment, either use [Gitlab-CI][], or do a `meson dist` on a modern
distribution and use that to build a package in the SteamRT environment.
The Gitlab-CI does the `meson dist` in the `build` step, and builds the
package on SteamRT in the `autopkgtest` step.

You might find [deb-build-snapshot][] useful for this. smcv uses this
configuration for test-builds:

```
LC_ALL=C.UTF-8 \
deb-build-snapshot \
--upstream \
--source \
--download ~/tmp/build-area \
--deb-schroot steamrt_scout_amd64 \
--install-all \
build-vm
```

where `build-vm` is a virtual machine with steam-runtime-tools'
dependencies and a `steamrt_scout_amd64` chroot, that can be accessed
via `ssh build-vm`. If the dependencies and chroot are available on your
development system, use `localhost`, which will make `deb-build-snapshot`
do the build locally instead of using ssh.

[Gitlab-CI]: https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/pipelines

## Design notes

* ABIs/architectures are identified by their Debian-style multiarch
    tuple, which is a GNU tuple (`x86_64-linux-gnu`, `arm-linux-gnueabi`)
    with its CPU part normalized to the oldest/most compatible in a
    CPU family (so `i386`, not `i686`, and `arm`, not `armv5tel`).
    Helper subprocesses are prefixed with the multiarch tuple,
    in the same style used for cross-compilers and cross-tools
    (`x86_64-linux-gnu-inspect-library`, `i386-linux-gnu-wflinfo`).

* Be careful when adding dependencies to the main library. GLib 2.32
    (Ubuntu 12.04 'precise', SteamRT 1 'scout') can be relied on.
    JSON-GLib is OK (we have a backport in SteamRT 1 'scout'). Anything
    else is probably bad news.

    - In particular, try not to pull in C++ ABIs, because incompatible C++
        ABIs are one of the things that steam-runtime-tools should be
        able to detect and diagnose. We use C ABIs because they are the
        lowest common denominator for compatibility between distros.

* Any checks that can reasonably crash should be in a subprocess. This
    means that if they crash, they don't bring down the entire Steam
    client with them.

* Any checks that need to look at more than one ABI (32- and 64-bit) must
    be in a subprocess, so that the same process can run both.

* The `inspect-library` helper should not have non-libc dependencies -
    not even GLib. Other helpers may have library dependencies if it is
    useful to do so.

* For system information and checks, new functionality should be accessed
    through `SrtSystemInfo` so that its cache lifetime can be managed.
    Separate access points like `srt_check_library_presence()` will be
    kept until the next ABI break, but don't add new ones.

## Automated tests

New code should have test coverage where feasible. It's OK to have some
"design for test" in the APIs to facilitate this, such as
`srt_system_info_set_environ()`.

If a helper can't be unit-tested (for example because it needs working
OpenGL), we'll understand. The code that calls into the helper should
still be unit-tested, by using a mock implementation of the helper.

Most automated tests run in two phases:

* Build-time tests (`meson test`, `ninja test`, `dh_auto_test`):
    `steam-runtime-tools` has just been built, and probably isn't
    installed. The environment variables in `tests/meson.build` are set.
    The code under test should look for data, helper subprocesses, etc.
    relative to those environment variables. We can add more environment
    variables like `GI_TYPELIB_PATH` if they become necessary.

* Installed-tests (`autopkgtest`, `debian/tests`): `steam-runtime-tools`
    has been built and installed system-wide. The code under test should
    look for data, helper subprocesses, etc. in their real installed
    locations. For things that get installed next to the tests, such as
    mock implementations of helpers and mock ABI data, it should look
    in the directory containing the executable.

## Release procedure

* The version number is *EPOCH*.*YYYYMMDD*.*MICRO* where:

    - *EPOCH* increases on major (incompatible) changes, or if you change
      the version-numbering scheme
    - *YYYYMMDD* is today's date if you are doing a feature release, or
      the date of the version you are branching from if you are applying
      "stable branch" hotfixes to an older version
    - *MICRO* starts at 0, and increases if you need to apply
      "stable branch" hotfixes to an old version (or if you need to do
      two feature releases on the same day)

* Look at the Debian package's build log from `deb-build-snapshot`
    or [Gitlab-CI][]. If new library ABI has been added, you will see
    warnings like these:

    ```
    dpkg-gensymbols: warning: debian/libsteam-runtime-tools-0-0/DEBIAN/symbols doesn't match completely debian/libsteam-runtime-tools-0-0.symbols
    --- debian/libsteam-runtime-tools-0-0.symbols (libsteam-runtime-tools-0-0_0.20190806.0+14+g7bda756-0~snapshot_amd64)
    +++ dpkg-gensymbolsWIRbYG	2019-08-16 10:34:42.915782703 +0000
    @@ -13,9 +13,20 @@
      srt_library_get_type@Base 0.20190801.0
      srt_library_issues_get_type@Base 0.20190801.0
      srt_library_symbols_format_get_type@Base 0.20190801.0
    + srt_runtime_issues_get_type@Base 0.20190806.0+14+g7bda756-0~snapshot
    + srt_steam_issues_get_type@Base 0.20190806.0+14+g7bda756-0~snapshot
      srt_system_info_can_run@Base 0.20190801.0
    ```

    Update `debian/libsteam-runtime-tools-0-0.symbols`, adding the symbols
    that you have added with the version number that you plan to release,
    for example `srt_runtime_issues_get_type@Base 0.20190816.0`.

* Update `debian/changelog`: `gbp dch`, edit to clarify the changelog if
  desired, `dch -r`, set the version number.

* Update the version number and ABI minor/micro version in `meson.build`.

* Commit everything.

* Add an annotated git tag v*VERSION*.

* Do a final release build, for example with:

    ```
    deb-build-snapshot -d ~/tmp/build-area --source-only --release localhost
    ```

* Upload the resulting `.changes` file to your OBS branch to be built.
    If it succeeds, submit it to the main OBS project to be included in
    the next scout release. If it fails, fix it and prepare a new release
    (with the next micro version number).

[deb-build-snapshot]: https://gitlab.collabora.com/smcv/deb-build-snapshot
