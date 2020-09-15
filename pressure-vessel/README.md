pressure-vessel — tools to put Steam in containers
==================================================

pressure-vessel is a bit like a simplified version of Flatpak for Steam
games. It is used by Steam's
[Steam Linux Runtime](https://steamdb.info/app/1070560/depots/)
compatibility tool to run games in individual game-specific containers.
For background on pressure-vessel and SteamLinuxRuntime, please see:

* <https://github.com/ValveSoftware/steam-runtime/tree/master/doc>
* <https://archive.fosdem.org/2020/schedule/event/containers_steam/>
* <https://steamcommunity.com/app/221410/discussions/0/1638675549018366706/>

Use cases
---------

### Using an alternative runtime

We can get a more predictable library stack than the
`LD_LIBRARY_PATH`-based Steam Runtime by making an alternative runtime
available over `/usr`, `/lib*`, `/bin`, `/sbin`, analogous to Flatpak
runtimes, using some or all of the host system's graphics stack.

In particular, this would avoid or mitigate the following problems with
the `LD_LIBRARY_PATH`-based Steam Runtime:

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

Building a local test version of pressure-vessel
------------------------------------------------

pressure-vessel is a reasonably ordinary Meson project. It depends on
GLib and libXau.

If you are using it with the under-development Steam Linux Runtime,
you will probably want to compile it with an unusual ${prefix}:

    prefix="${XDG_DATA_HOME:-"$HOME/.local/share"}/Steam/steamapps/common/SteamLinuxRuntime/pressure-vessel"
    libdir="lib/x86_64-linux-gnu"
    meson --prefix="$prefix" --libdir="$libdir" _build
    ninja -C _build
    meson test -v -C _build             # optional
    ninja -C _build install

Note that this will give you a non-production version of pressure-vessel
that is likely to depend on libraries from your host system, which is
good for quick turnaround and debugging, but not suitable for deployment
to the public. For that, you'll want a relocatable installation: see below.

If you are developing pressure-vessel, you might well want to alter
the bundled libcapsule tools. You can do this by building your own
copy of libcapsule. This requires 32- and 64-bit copies of libelf (the
version from elfutils) and its development files.

    git clone https://gitlab.collabora.com/vivek/libcapsule.git
    cd libcapsule
    prefix="${XDG_DATA_HOME:-"$HOME/.local/share"}/Steam/steamapps/common/SteamLinuxRuntime/pressure-vessel"
    libdir="lib/x86_64-linux-gnu"
    NOCONFIGURE=1 ./autogen.sh
    mkdir _build _build/i386 _build/x86_64
    ( cd _build/x86_64; ../../configure
    --host=x86_64-linux-gnu --enable-host-prefix=x86_64-linux-gnu- \
    --prefix="$prefix" --libdir="\${prefix}/$libdir" \
    --disable-gtk-doc --disable-shared --without-glib )
    ( cd _build/i386; ../../configure
    --host=i686-linux-gnu --enable-host-prefix=i386-linux-gnu- \
    --prefix="$prefix" --libdir="\${prefix}/$libdir" \
    --disable-gtk-doc --disable-shared --without-glib )
    make -C _build/x86_64
    make -C _build/i386
    make -C _build/x86_64 install
    make -C _build/i386 install

Use `--host=i586-linux-gnu` instead of `--host=i686-linux-gnu` if your
distribution uses that tuple for its 32-bit compiler. For the i386 build,
if you don't have an `i?86-linux-gnu-gcc` you might have to add
`CC="gcc -m32"` to the i386 configure command line.

Again, this will give you a non-production version of libcapsule that is
likely to depend on libraries from your host system.

Building a relocatable install for deployment
---------------------------------------------

To make the built version compatible with older systems, you will need
a SteamRT 1 'scout' environment.

For this, you need prebuilt versions of libcapsule-tools-relocatable:amd64
and libcapsule-tools-relocatable:i386 either in your build environment
or available via apt-get. For example, you could do the build in a
SteamRT 1 'scout' SDK Docker image that has those packages already.
Then you can do:

    meson --prefix="$(pwd)/_build/prefix" -Dsrcdir=src _build
    ninja -C _build
    meson test -v -C _build             # optional
    ninja -C _build install
    rm -fr _build/relocatable-install
    _build/prefix/bin/pressure-vessel-build-relocatable-install \
        --output _build/relocatable-install \
        --archive .
    ./tests/pressure-vessel/relocatable-install.py \
        _build/relocatable-install      # optional

When building a development version of `pressure-vessel` using a 'scout'
SDK Docker image, if the `pressure-vessel-build-relocatable-install`
script is unable to find some of the required dependencies, it might be
necessary to add apt repositories containing development versions of
those dependencies to `/etc/apt/sources.list`, and run `apt-get update`.

For more convenient use on a development system, if you have a
SteamRT 1 'scout' SDK tarball or an unpacked sysroot, you can place a
tarball at `_build/sysroot.tar.gz` or unpack a sysroot into
`_build/sysroot`, and prefix those commands with
`./sysroot/run-in-sysroot.py`:

    ./sysroot/run-in-sysroot.py apt-get update
    ./sysroot/run-in-sysroot.py meson --prefix="$(pwd)/_build/prefix" _build
    ./sysroot/run-in-sysroot.py ninja -C _build
    (etc.)

(Or put them in different locations and pass the `--sysroot` and
`--tarball` options to `./sysroot/run-in-sysroot.py`.)

The relocatable install goes into `relocatable-install` (or
whatever you used as the `--output`), and a compressed version ends
up in the directory passed as an argument to `--archive`.

Instructions for testing
------------------------

* Install Steam and some games. Convenient choices for a reasonably
  small free-to-play game on various engines include:

    - Floating Point (game ID 302380)
        - very small/quick to install, works OK on weak hardware
        - UnityPlayer 4.3.4f1
    - Life is Strange, episode 1 (319630)
        - Unreal Engine 3
    - Team Fortress 2 (440)
        - Source engine
    - Unturned (304930)
        - UnityPlayer 5.5.3f1

* Install bubblewrap or Flatpak (in Debian, Ubuntu or SteamOS:
  `sudo apt-get install bubblewrap`). pressure-vessel bundles its own
  fallback copy of bubblewrap, but that one will only work on kernels
  that are configured to allow unprivileged user namespaces, such as
  recent Ubuntu and Fedora (but not, for example, Debian, Arch Linux or
  Red Hat Enterprise Linux unless specially configured).

* Install Python 3, PyGI and GTK 3 introspection data
  (in Debian, Ubuntu or SteamOS:
  `sudo apt-get install python3-gi gir1.2-gtk-3.0`).
  This is not necessary for production use but is very convenient when
  testing interactively.

* Unpack `pressure-vessel-`*VERSION*`-bin.tar.gz` (or `...-bin+src.tar.gz`)
  in some convenient place, for example
  `~/.steam/steam/steamapps/common/SteamLinuxRuntime/pressure-vessel`:

        $ rm -fr ~/.steam/steam/steamapps/common/SteamLinuxRuntime/pressure-vessel
        $ mkdir -p ~/.steam/steam/steamapps/common/SteamLinuxRuntime/pressure-vessel
        $ tar \
            --strip-components=1 \
            -C ~/.steam/steam/steamapps/common/SteamLinuxRuntime/pressure-vessel \
            -xzvf ~/pressure-vessel-*-bin.tar.gz

* Put some Flatpak-style runtimes alongside pressure-vessel,
    for example `~/.steam/steam/steamapps/common/SteamLinuxRuntime/scout`,
    so that you have a
    `~/.steam/steam/steamapps/common/SteamLinuxRuntime/scout/files/` directory
    and a `~/.steam/steam/steamapps/common/SteamLinuxRuntime/scout/metadata`
    file. For example, download a
    `com.valvesoftware.SteamRuntime.Platform-amd64,i386-scout-runtime.tar.gz`
    from https://repo.steampowered.com/steamrt-images-scout/snapshots/ and
    unpack it like this:

        $ rm -fr ~/.steam/steam/steamapps/common/SteamLinuxRuntime/scout
        $ mkdir -p ~/.steam/steam/steamapps/common/SteamLinuxRuntime/scout
        $ tar \
            -C ~/.steam/steam/steamapps/common/SteamLinuxRuntime/scout \
            -xzvf ~/com.valvesoftware.SteamRuntime.Platform-amd64,i386-scout-runtime.tar.gz

    Or to have a SDK environment with more debugging tools, and optionally
    detached debugging symbols, download a
    `com.valvesoftware.SteamRuntime.Sdk-amd64,i386-scout-runtime.tar.gz`
    and optionally a matching
    `com.valvesoftware.SteamRuntime.Sdk-amd64,i386-scout-debug.tar.gz`,
    and unpack them like this:

        $ rm -fr ~/.steam/steam/steamapps/common/SteamLinuxRuntime/scout_sdk
        $ mkdir -p ~/.steam/steam/steamapps/common/SteamLinuxRuntime/scout_sdk
        $ tar \
            -C ~/.steam/steam/steamapps/common/SteamLinuxRuntime/scout_sdk \
            -xzvf ~/com.valvesoftware.SteamRuntime.Sdk-amd64,i386-scout-runtime.tar.gz

        $ tar --strip-components=1 \
            -C ~/.steam/steam/steamapps/common/SteamLinuxRuntime/scout_sdk/files/lib/debug/ \
            -xzvf ~/com.valvesoftware.SteamRuntime.Sdk-amd64,i386-scout-debug.tar.gz

    Or if you have a `chroot` environment with extra packages installed,
    such as the `/var/chroots/steamrt_scout_amd64` generated by
    [`setup_chroot.sh`][] or an unpacked copy of
    `com.valvesoftware.SteamRuntime.Sdk-amd64,i386-scout-sysroot.tar.gz`,
    you can use that by making `files` a symbolic link to it:

        $ rm -fr ~/.steam/root/common/SteamLinuxRuntime/scout-chroot
        $ mkdir -p ~/.steam/root/common/SteamLinuxRuntime/scout-chroot
        $ ln -s /var/chroots/steamrt_scout_amd64 \
            ~/.steam/root/common/SteamLinuxRuntime/scout-chroot/files

* Launch a game once without pressure-vessel

* Configure the launch options for the chosen game. Either use the Steam
    (non-Big-Picture) UI, for example by logging in as the `desktop`
    user on SteamOS, or:

    - be the Steam user, possibly via sudo -u steam -s

    - `$ nano ~/.steam/steam/userdata/[0-9]*/config/localconfig.vdf`

    - navigate to UserLocalConfigStore/Software/Valve/Steam/Apps/*game ID*

    - below LastPlayed, add:

            "LaunchOptions" "PRESSURE_VESSEL_WRAP_GUI=1 ~/.steam/steam/steamapps/common/SteamLinuxRuntime/pressure-vessel/bin/pressure-vessel-unruntime -- %command%"

    - restart Steam (on SteamOS use `sudo systemctl restart lightdm`)

        - TODO: Is there a scriptable way to make Steam edit `localconfig.vdf`
            itself, or to make it reload an edited `localconfig.vdf`?

* Run the game. If successful, you will get a GUI launcher that lists possible
    runtimes. Choose one and click Run. You should get a game.
    `pstree -pa` will look something like this:

        |-SteamChildMonit,13109
        |   `-sh,13110 -c...
        |       `-bwrap,13132 --new-session --setenv LD_LIBRARY_PATH...
        |           `-Floating Point.,13133

* The test UI automatically looks for runtimes in the following locations:

    - Next to pressure-vessel's `bin` and `lib` directories
    - The directory above pressure-vessel's `bin` and `lib` directories

    Runtimes intended for the test UI must have a `files` subdirectory,
    like Flatpak runtimes.

    You can also choose to use the host system `/usr` directly.

* The default runtime for `pressure-vessel-unruntime` is the host system,
    with the `LD_LIBRARY_PATH` Steam Runtime overlaid onto it.
    You can specify a runtime with the `--runtime` option.

    The runtime can be any of these:

    - The `files` subdirectory of a Flatpak-style runtime such as
        `~/.steam/steam/steamapps/common/SteamLinuxRuntime/scout` or
        `~/.local/share/flatpak/runtime/com.valvesoftware.SteamRuntime.Platform/x86_64/scout/active`,
        for example produced by [flatdeb][] (this is a special case of a
        merged `/usr`). For example, you could use:

            /opt/pressure-vessel/bin/pressure-vessel-unruntime --runtime=$HOME/.steam/steam/steamapps/common/SteamLinuxRuntime/scout -- %command%

        or:

            /opt/pressure-vessel/bin/pressure-vessel-unruntime --runtime=$HOME/.local/share/flatpak/runtime/com.valvesoftware.SteamRuntime.Platform/x86_64/scout/active -- %command%

    - A merged `/usr` containing `bin/bash`, `lib/ld-linux.so.2`,
      `bin/env`, `share/locale`, `lib/python2.7` and so on

    - A sysroot containing `bin/bash`, `lib/ld-linux.so.2`,
        `usr/bin/env`, `usr/share/locale`, `usr/lib/python2.7` and so on,
        optionally with `bin`, `lib` etc. being symlinks into `usr`.
        For example, if you have a chroot in
        `/var/chroots/steamrt_scout_amd64` generated by [`setup_chroot.sh`][],
        you could use:

            /opt/pressure-vessel/bin/pressure-vessel-unruntime --runtime=/var/chroots/steamrt_scout_amd64 -- %command%

When using a runtime, graphics drivers come from the host system, while
their dependencies (notably glibc and libstdc++) come from either
the host system or the runtime, whichever appears to be newer. This
is currently hard-coded in `pressure-vessel-wrap` and cannot be
configured.

[`setup_chroot.sh`]: https://github.com/ValveSoftware/steam-runtime

### Debugging

* To see the filesystem in which the game is executing, get the process ID
    of some game process from `ps` and use:

        $ sudo ls -l /proc/$game_pid/root/

    You'll see that the graphics drivers and possibly their dependencies
    are available in `/overrides` inside that filesystem, while selected
    files from the host are visible in `/run/host`.

* To test something manually, or to debug startup issues:

    - cd to the directory that you want to be the current working directory
      inside the container

    - Run:

            /opt/pressure-vessel/bin/pressure-vessel-test-ui -- ./whatever-game

      or for the low-level version:

            /opt/pressure-vessel/bin/pressure-vessel-wrap -- ./whatever-game

      Optionally add more options before the `--`, such as `--runtime`,
      `--xterm` and `--shell-instead`.

    In particular, if the `xterm` can't be launched and you are debugging
    why, use the `--tty` option to get a shell in the container.

    If `bwrap` is failing to start up and so you can't get a shell in the
    container at all, construct a simpler-but-simpler `bwrap` command-line
    (based on what `pressure-vessel-wrap` logs to stderr) and try that.

* For interactive testing from Steam, if your runtime (if used) or host
    system (if no runtime) contains an `xterm` binary, you can use
    something like:

        /opt/pressure-vessel/bin/pressure-vessel-unruntime --shell-instead -- %command%

    to run an xterm containing an interactive shell. The interactive
    shell's current working directory matches the game's, and you can
    explore the container from there.

    When ready, run `"$@"` in the interactive shell (with the double
    quotes included!) to run the game.

    Or, use `--shell-after` or `--shell-fail` to try running the game first,
    and then run a shell afterwards, either unconditionally or only if
    the command fails.

    Similarly, you can use `--xterm` to make the game's output appear in
    an xterm without having an interactive shell.

    When you use any of these options, Steam will treat the xterm as part
    of the game, so you cannot launch the game again from the Steam UI
    until you have exited from the xterm.

* For interactive testing, if you ran Steam from a shell (not normally
    valid on SteamOS!), you can use something like:

        /opt/pressure-vessel/bin/pressure-vessel-unruntime --tty --shell-instead -- %command%

    This is a simpler version of `--xterm`, which uses the terminal from
    which you ran Steam instead of an xterm inside the container.

    The interactive shell's current working directory matches the game's.
    As with the xterm, run `"$@"` in the interactive shell to run the game.

* To get a modifiable copy of the runtime, use something like:

        mkdir -p $HOME/.steam/steam/steamapps/common/SteamLinuxRuntime/var
        /opt/pressure-vessel/bin/pressure-vessel-unruntime \
            --runtime=$HOME/.steam/steam/steamapps/common/SteamLinuxRuntime/scout/files \
            --copy-runtime-into=$HOME/.steam/steam/steamapps/common/SteamLinuxRuntime/var \
            -- %command%

    You will find a temporary copy of the runtime in the directory you
    chose, in this example something like
    `.../SteamLinuxRuntime/var/tmp-1234567`. You can modify it in-place.
    For example, if you want to replace `libdbus-1.so.3` with an
    instrumented version, you can do something like this:

        cd .../SteamLinuxRuntime/var/tmp-1234567
        rm -f overrides/lib/x86_64-linux-gnu/libdbus-1.so.3*
        rm -f usr/lib/x86_64-linux-gnu/libdbus-1.so.3*
        cp --dereference ~/dbus/dbus/.libs/libdbus-1.so.3 \
            overrides/lib/x86_64-linux-gnu/libdbus-1.so.3

    To avoid temporary copies building up forever, they will be deleted
    *next* time you run a game in a container (assuming there is no longer
    anything running in the previous container), unless you create a file
    like `.../var/tmp-1234567/keep` to flag this root directory to be kept
    for future reference.

### More options

Use `pressure-vessel-unruntime`
if you are in a Steam Runtime environment (the Steam Runtime's `run.sh`
or a Steam game), and `pressure-vessel-wrap` or `pressure-vessel-test-ui`
if you are not ("Add non-Steam game" in Steam, or a non-Steam-related
interactive shell).

* To protect `$HOME`, add one of the following options before `--`:

    - `--fake-home=/some/path` (automatically unshares the home directory)
    - `--freedesktop-app-id=com.example.Anything --unshare-home`
    - `--steam-app-id=70 --unshare-home` (when running from Steam you can use
    `--steam-app-id=${SteamAppId}`, which is the default)

    This feature is not being actively developed right now.

Steam integration
-----------------

If a future version of Steam is modified to run certain games using
pressure-vessel, it should remove the Steam Runtime environment variables
from the environment with which it runs them (like it does for
"Add non-Steam game"), and use `pressure-vessel-wrap` directly.
`pressure-vessel-unruntime` is just a workaround for this not
being something that Steam supports yet.

Design constraints
------------------

* Steam games assume that they are executed with the current working
  directory equal to the game's base directory, typically something
  like `SteamApps/common/Half-Life`.

    - This is not actually completely true! Apparently some games, like
      "Estranged: Act I" (ID 261820), start up with the current working
      directory as *a subdirectory of* the directory they need.
      pressure-vessel does not currently receive enough information from
      Steam to fix this. See
      [steam-runtime#236](https://github.com/ValveSoftware/steam-runtime/issues/236).

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

* Games should not see each other's saved games and other private data
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
