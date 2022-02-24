pressure-vessel — tools to put Steam in containers
==================================================

<!-- This document:
Copyright © 2018-2022 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

pressure-vessel is a bit like a simplified version of Flatpak for Steam
games. It is used by Steam's [container runtime](container-runtime.md)
(Steam Linux Runtime)
compatibility tools to run games in individual game-specific containers.
For more information on pressure-vessel, please see:

* <https://archive.fosdem.org/2020/schedule/event/containers_steam/>
* <https://steamcommunity.com/app/221410/discussions/0/1638675549018366706/>
* [the source code](https://gitlab.steamos.cloud/steamrt/steam-runtime-tools/-/tree/HEAD/pressure-vessel)

Use cases
---------

### Using an alternative runtime

We can get a more predictable library stack than the
`LD_LIBRARY_PATH`-based Steam Runtime by making an alternative runtime
available over `/usr`, `/lib*`, `/bin`, `/sbin`, analogous to Flatpak
runtimes, using some or all of the host system's graphics stack.

In particular, this avoids or mitigates the following problems with
the [`LD_LIBRARY_PATH`-based Steam Runtime](ld-library-path-runtime.md):

* The Steam Runtime cannot contain glibc, because the path to
    [`ld.so(8)`][ld.so] is hard-coded into all executables (it is the ELF
    interpreter, part of the platform ABI), the version of `ld.so` is
    coupled to `libdl.so.2`, and the version of `libdl.so.2` is
    coupled to the rest of glibc; so everything in the Steam Runtime
    must be built for a glibc at least as old as the oldest system that
    Steam supports. This makes it difficult to advance beyond
    the Ubuntu 12.04-based 'scout' runtime: everything that is updated
    effectively has to be backported to Ubuntu 12.04.

* The Steam Runtime was originally designed to prefer its own version of
    each library even if the version in the host system was newer, but
    this broke any graphics driver that relied on a newer version of a
    host library, notably `libstdc++`.

* More recent versions of the Steam Runtime prefer the "pinned" host
    system version of most libraries that appear to be at least as new as
    the Runtime's own version, but this can also break things if
    libraries claim to be compatible (by having the same ELF `DT_SONAME`)
    but are in fact not compatible, for example:

    - libcurl.so.4 linked to libssl 1.0 is not completely compatible with
        libcurl.so.4 linked to libssl 1.1

    - The history of libcurl.so.4 in Debian/Ubuntu has involved two
       incompatible sets of versioned symbols, due to some decisions
       made in 2005 and 2007 that, with hindsight, were less wise than
       they appeared at the time

    - Various libraries can be compiled with or without particular
        features and dependencies; if the version in the Steam Runtime
        has a feature, and the host system version is newer but does not
        have that feature, then games cannot rely on the feature

    - In the worst-case, compiling a library without a particular
        feature or dependency can result in symbols disappearing from
        its ABI, resulting in games that reference those symbols crashing

    - There is a fairly subtle interaction between libdbus,
        libdbusmenu-gtk, libdbusmenu-glib and Ubuntu's patched GTK 2
        that has resulted in these libraries being forced to be taken
        from the Steam Runtime, to avoid breaking the Unity dock

* Preferring the "pinned" host system libraries makes it very easy for
    a game developer to make their game depend on a newer host-system
    library without realising that they have done so, resulting in their
    game not running correctly on older host systems, or on host systems
    that do not have a library with a matching `DT_SONAME`.

A future goal is to use [libcapsule][] to avoid the library dependencies
of the host system's graphics stack influencing the libraries loaded by
games at all, and in particular allow game logic to use the Runtime's
libstdc++ while the host system graphics driver uses a different,
newer libstdc++. This is not yet implemented.

[ld.so]: https://linux.die.net/man/8/ld.so
[libcapsule]: https://gitlab.collabora.com/vivek/libcapsule/

### Protecting `$HOME`

(This feature is not being actively developed right now.)

If we change the meaning of `/home` for a game to be a fake
app-specific directory, then the game can't write random files in unknown
subdirectories of $HOME, and we can be reasonably sure that uninstalling
a game removes all of its files. As a side benefit, this could make it
easier to be sure that all the necessary files have been selected for
Steam Cloud Sync, because we could prevent the game writing anywhere
that won't be synchronised, other than /tmp or similar.

Building and testing pressure-vessel
------------------------------------

Please see [../CONTRIBUTING.md](../CONTRIBUTING.md) for general
information.

The script `build-aux/many-builds.py` can be used to compile and test
steam-runtime-tools, including pressure-vessel. Please see
[../build-aux/many-builds.md](../build-aux/many-builds.md) for details.

Design constraints
------------------

* Most Steam games assume that they are executed with the current working
  directory equal to the game's base directory, typically something
  like `SteamApps/common/Half-Life`.

    - Compatibility tools like the one that wraps pressure-vessel will
      be run with that same working directory, and they need to arrange
      for the same location to be the working directory for the game itself.

* A few games, instead, assume that they are executed with the current
  working directory in *a subdirectory of* the game's base directory.

    - Again, compatibility tools like the one that wraps pressure-vessel will
      be run with that same working directory, and they need to arrange
      for the same location to be the working directory for the game itself.

* Steam games are delivered via existing code to use the Steam CDN, not
  as Flatpak packages.

* Each game is assumed to have one subdirectory of `SteamApps`
  (for example `SteamApps/common/Half-Life`) and one
  *AppID* (for example 70 for [Half-Life][]). These are not correlated
  in any obvious way.

  [Half-Life]: https://store.steampowered.com/app/70/HalfLife/

* Some games have distinct app IDs but share data: for example,
  [X3: Terran Conflict][] (ID 2820) and [X3: Albion Prelude][] (ID 201310)
  share `SteamApps/common/X3 Terran Conflict`.

  [X3: Terran Conflict]: https://store.steampowered.com/app/2820/X3_Terran_Conflict/
  [X3: Albion Prelude]: https://store.steampowered.com/app/201310/X3_Albion_Prelude/

* The Steam client and client library need to be able to communicate
  via shared memory and SysV semaphores.

    - ⇒ We must share `/dev/shm`
    - ⇒ We must not unshare the IPC namespace

Assumptions
-----------

* Games may write to their own subdirectories of `SteamApps/common`.

* Games ideally should not see each other's saved games and other private data
  in `XDG_CONFIG_HOME` etc.

* X11, Wayland, PulseAudio, D-Bus etc. are assumed to use sockets in
  `/run` (including `$XDG_RUNTIME_DIR`), `/var` or `/tmp`.

    - X11 uses `/tmp`: OK
    - Wayland uses `$XDG_RUNTIME_DIR`: OK
    - The D-Bus system bus always uses `/var/run`: OK
    - The D-Bus session bus uses `$XDG_RUNTIME_DIR/bus` on systems with
      systemd, systemd-logind and the user bus: OK
    - The D-Bus session bus uses abstract Unix sockets on other systems: OK
    - PulseAudio uses `$XDG_RUNTIME_DIR` on modern systems with systemd: OK
    - PulseAudio uses `/var/run` on SteamOS: OK
    - PulseAudio might not work on non-systemd non-SteamOS machines?

* Games can't see `~/.local/share/fonts`, `~/.fonts`, `~/Music`,
  `~/Desktop`, etc. and this is assumed to be OK.

TODO
----

* Games can currently read each other's static data via `~/.steam/steam`
  and `~/.steam/root`. This could be prevented, if we can define which
  parts of `~/.steam` are considered to be API and which are to be
  masked.

* What paths did PulseAudio historically use? It might not work on
  non-systemd non-SteamOS machines.

* Games like AudioSurf that want to read `~/Music` aren't going to work.
  They'll need some new special-case option analogous to
  Flatpak's `--filesystem=xdg-music:ro`.

Design
------

### Setting up the container

With a runtime, the container is basically the same as a Flatpak
sandbox. The root directory is a tmpfs. The runtime is bind-mounted on
`/usr`, with symbolic links at `/bin`, `/sbin` and `/lib*` so that
commonly-hard-coded paths like `/bin/sh`, `/sbin/ldconfig` and
`/lib/ld-linux.so.2` work as expected.

With a sysroot-style runtime, or when using the host system as the
runtime, the setup is the same, except that `/bin`, `/sbin` and `/lib*`
might be real directories bind-mounted from the runtime or host if it
has not undergone the [/usr merge][].

Either way, selected paths from the host system are exposed in `/run/host`,
the same as for `flatpak run --filesystem=host` in Flatpak.

[/usr merge]: https://fedoraproject.org/wiki/Features/UsrMove

### Interactive debugging

`--tty`, `--xterm` and the `--shell` options work by wrapping an
increasingly long "adverb" command around the command to be run.

### Unsharing the home directory

(This feature is not being actively developed right now.)

Each game gets a private home directory in `~/.var/app`, the same as
Flatpak apps do. The name of the private home directory follows the
[naming recommendations from the freedesktop.org Desktop Entry
Specification](https://specifications.freedesktop.org/desktop-entry-spec/desktop-entry-spec-latest.html#file-naming).
For Steam apps, unless a better name has been specified, the app's
freedesktop.org app ID is based on its Steam AppID, for example
`com.steampowered.App70` for Half-Life.

For the game, `XDG_CONFIG_HOME`, `XDG_DATA_HOME` and `XDG_CACHE_HOME`
are set to the `./.config`, `./.local/share` and `./.cache` directories
inside its private home directory, so that well-behaved freedesktop-style
apps will write there, and badly-behaved freedesktop-style apps that
ignore the environment variables and hard-code their default values will
*also* write there.

* Example: [X3: Terran Conflict][] writes to `$XDG_CONFIG_HOME/EgoSoft/X3TC`
* Example: Mesa writes to `$XDG_CACHE_HOME/mesa` (assuming
  `$MESA_GLSL_CACHE_DIR` is unset)

`./config`, `./data` and `./cache` in the private home directory are symbolic
links to `.config`, `.local/share` and `.cache` respectively, for better
discoverability and compatibility with Flatpak (which uses those directories in
`~/.var/app` as its usual values for `XDG_CONFIG_HOME`, `XDG_DATA_HOME` and
`XDG_CACHE_HOME`).

Anything that hard-codes a path relative to `$HOME` (including `.config`,
`.local/share` or `.cache`) will write to the corresponding directory in
`~/.var/app`. This is the same as the behaviour of a Flatpak app with
`--persist=.`.

* Example: [Avorion][] writes to `~/.avorion/` in the container, which is
  `~/.var/app/com.steampowered.App445220/.avorion/` in the real home
  directory

  [Avorion]: https://store.steampowered.com/app/445220/Avorion/

If more than one game is meant to share a private home directory, then
they need to be run with `--freedesktop-app-id=` or `--steam-app-id=`
to set that up.

* For example, if [X3: Terran Conflict][] and [X3: Albion Prelude][]
  are to share data, they would have to both be run with for example
  `--freedesktop-app-id=com.egosoft.X3TC` or `--steam-app-id=2820`.

The app is not currently made available read-only on `/app` (as it
would be in Flatpak). It could be, if desired.
