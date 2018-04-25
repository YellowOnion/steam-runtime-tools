pressure-vessel — tools to put Steam in containers
==================================================

pressure-vessel is a bit like a simplified version of Flatpak for Steam games.

Use cases
---------

### Protecting `$HOME`

If we change the meaning of `/home` for a game to be a fake
app-specific directory, then the game can't write random files in unknown
subdirectories of $HOME, and we can be reasonably sure that uninstalling
a game removes all of its files. (As a side benefit, this could make it
easier to be sure that all the necessary files have been selected for
Steam Cloud Sync, because we could prevent the game writing anywhere
that won't be synchronised, other than /tmp or similar.)

### Using an alternative runtime

We can get a more predictable library stack than the
`LD_LIBRARY_PATH`-based Steam Runtime by making an alternative runtime
available over `/usr`, `/lib*`, `/bin`, `/sbin`, analogous to Flatpak
runtimes, possibly using [libcapsule][] to share the host's graphics
driver stack. (This mode is not yet implemented.)

[libcapsule]: https://gitlab.collabora.com/vivek/libcapsule/

Instructions for testing
------------------------

* Install Steam and some games

* Run Steam in desktop mode (not Big Picture)

* Configure the launch options for a game in Steam to be:

        /path/to/pressure-vessel-wrap -- %command%

* For interactive testing, if you ran Steam from a shell, you can use:

        /path/to/pressure-vessel-wrap --interactive -- %command%

    The interactive shell's current working directory matches the game's.
    Run `"$@"` in the interactive shell to run the game.

* To test something manually:

    - cd to the directory that you want to be the current working directory
      inside the container

    - Run one of:

            /path/to/pressure-vessel-wrap --fake-home=/some/path --interactive -- ./whatever-game
            /path/to/pressure-vessel-wrap --freedesktop-app-id=com.example.Anything --interactive -- ./whatever-game
            /path/to/pressure-vessel-wrap --steam-app-id=70 --interactive -- ./whatever-game

Design constraints
------------------

* Steam games assume that they are executed with the current working
  directory equal to the game's base directory, typically something
  like `~/.steam/steam/SteamApps/common/Half-Life`.

* Steam games are delivered via existing code to use the Steam CDN, not
  as Flatpak packages.

* Each game is assumed to have one subdirectory of `SteamApps`
  (for example `~/.steam/steam/SteamApps/common/Half-Life`) and one
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

* Flatpak-like *runtimes* that replace `/usr` (and also `/lib*`, `/bin`,
  `/sbin` on systems without merged `/usr`) are not yet implemented.

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

Each game gets a private home directory in `~/.var/app`, the same as
Flatpak apps do. The name of the private home directory follows the
[naming recommendations from the freedesktop.org Desktop Entry
Specification](https://specifications.freedesktop.org/desktop-entry-spec/desktop-entry-spec-latest.html#file-naming).
For Steam apps, unless a better name has been specified, the app's
freedesktop.org app ID is based on its Steam AppID, for example
`com.steampowered.App70` for Half-Life.

For the game, `XDG_CONFIG_HOME`, `XDG_DATA_HOME` and `XDG_CACHE_HOME`
are set to the `./config`, `./data` and `./cache` directories inside
its private home directory, so well-behaved freedesktop-style apps
will write there.

* Example: [X3: Terran Conflict][] writes to `$XDG_CONFIG_HOME/EgoSoft/X3TC`
* Example: Mesa writes to `$XDG_CACHE_HOME/mesa` (assuming
  `$MESA_GLSL_CACHE_DIR` is unset)

Anything that hard-codes a path relative to `$HOME` (including `.config`,
`.local/share` or `.cache`) will write to the corresponding directory in
`~/.var/app`.

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
would be in Flatpak) because that would require us to reassemble the
root directory.
