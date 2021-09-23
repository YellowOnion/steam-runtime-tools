# Contributing to steam-runtime-tools

<!-- This file:
Copyright 2019-2020 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

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

## Compiling steam-runtime-tools

steam-runtime-tools uses the Meson build system. However, because Steam
supports both 64-bit x86\_64 (amd64) and 32-bit IA32 (x86, i386), to
get a fully functional set of diagnostic tools or a fully functional
version of pressure-vessel that will work in all situations, it is
necessary to build steam-runtime-tools at least twice: once for 64-bit
x86\_64, and once for 32-bit IA32. The 32-bit build can disable
pressure-vessel, but must include at least the "helpers" that are used
to identify and inspect 32-bit libraries. Building for multiple
architectures is outside the scope of the Meson build system, so we
have to do at least two separate Meson builds and combine them.

Also, to make sure that pressure-vessel and the diagnostic tools work
with all the operating systems and runtime environments we support,
production builds of `steam-runtime-tools` need to be compiled in a
Steam Runtime 1 'scout' environment.

The script `build-aux/many-builds.py` can be used to compile and test
steam-runtime-tools in a convenient way. Please see
[build-aux/many-builds.md](build-aux/many-builds.md) for details.

Production builds are done by [Gitlab-CI][], which is also run to test
proposed branches. This does not use `many-builds.py`, because it needs
to run separate parts of the build in separate containers, but the general
principle is the same.

The diagnostic tools provided by steam-runtime-tools are shipped in
Steam Runtime releases via a `.deb` package. We usually build `.deb`
packages via the [deb-build-snapshot][] tool, with a command-line like
this:

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

## Automated testing for pressure-vessel

Testing a new build of pressure-vessel is relatively complicated, because
several things have to be pulled together:

* We need at least one Steam Linux Runtime container to use for testing,
    but for full test coverage, we need several containers. These can be
    downloaded by using `pressure-vessel/populate-depot.py`, which is
    a copy of the same script that is used to build official
    Steam Linux Runtime releases. `build-aux/many-builds.py setup`
    automates this, placing a suitable set of containers in
    `_build/containers` by default.

* `pressure-vessel-launcher` and `pressure-vessel-adverb` need to be
    compiled in a way that will work both on the host system and inside
    the test containers. The most reliable way to provide this is to build
    them for Steam Linux Runtime 1 'scout'.
    `build-aux/many-builds.py install` puts a complete relocatable
    installation of pressure-vessel in `_build/containers/pressure-vessel`
    by default.

* `pressure-vessel-wrap` needs to be compiled in a way that is compatible
    with the host system. In production builds, we use a binary that was
    built on scout for maximum compatibility, but when debugging a problem
    with pressure-vessel, it can be useful to build it with a newer
    compiler to get better warnings, or to build it with debug
    instrumentation such as AddressSanitizer or code-coverage
    instrumentation. `build-aux/many-builds.py` compiles `_build/host`
    with AddressSanitizer by default. It also sets up `_build/coverage`
    to be built with code coverage instrumentation, but does not build
    that copy by default.

* The pressure-vessel integration test needs to be told where to find
    the test containers and the relocatable build of pressure-vessel,
    using the `test_containers_dir` Meson option. `build-aux/many-builds.py`
    does this for the `_build/host` build by default.

After putting all that together, we can run
`tests/pressure-vessel/containers.py` as an ordinary automated test.
`build-aux/many-builds.py test` will do that for you.

## Manual testing for pressure-vessel

To reproduce issues involving pressure-vessel, it's often necessary to
run a real game in Steam.

`build-aux/many-builds.py install` puts a complete relocatable
installation of pressure-vessel in `_build/containers/pressure-vessel`
by default. This can be copied to a Steam installation for manual
testing, by deleting
`steamapps/common/SteamLinuxRuntime_soldier/pressure-vessel` and
replacing it with a copy of `_build/containers/pressure-vessel`.

If a branch under test has been built on our [Gitlab-CI][], the
artifacts from the `relocatable-install:production` job contain a
complete relocatable installation of pressure-vessel at
`_build/production/pressure-vessel-bin.tar.gz`, and a similar
installation with source code included at
`_build/production/pressure-vessel-bin+src.tar.gz`. These can be
downloaded from the web interface:

* pipeline or merge request
* `relocatable-install:production`
* Job artifacts
* Browse
* navigate into `_build/production`
* click on one of the filenames to view the associated artifact
* Download
* unpack the downloaded archive as a replacement for
    `steamapps/common/SteamLinuxRuntime_soldier/pressure-vessel`

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
