# Steam Linux Runtime - guide for game developers

<!-- This document:
Copyright 2021-2022 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

[[_TOC_]]

## Audience

This document is primarily intended for game developers intending to
release their games on Steam.
It might also be interesting to new Steam Linux Runtime developers,
and to Steam-on-Linux enthusiasts with an interest in tweaking settings.

Please note that most of the configurations described in this document
should be considered to be unsupported: they are intended to be used by
game developers while debugging a new game, and are not intended to be
used by Steam customers to play released games.
Please consult official [Steam support documentation][] for help with
playing released games on Linux.

[Steam support documentation]: https://help.steampowered.com/

## Introduction

The *Steam Linux Runtime* is a collection of container environments
which can be used to run Steam games on Linux in a relatively predictable
container environment, instead of running directly on an unknown Linux
distribution which might be old, new or unusually set up.

It is implemented as a collection of Steam
[compatibility tools][], but can also be used outside Steam for
development and debugging.

The Steam Linux Runtime consists of a series of scripts that wrap a
container-launching tool written in C, *pressure-vessel*.
pressure-vessel normally creates containers using an included copy of the
third-party [bubblewrap][] container-runner.

If Steam or the Steam Linux Runtime is run inside a Flatpak sandbox,
then pressure-vessel cannot create new containers directly.
Instead, it communicates with the Flatpak service on the host system,
and asks the Flatpak service to launch new containers on its behalf.

[compatibility tools]: steam-compat-tool-interface.md
[bubblewrap]: https://github.com/containers/bubblewrap

Unlike more typical container launchers such as Flatpak and Docker,
pressure-vessel is a special-purpose container launcher designed
specifically for Steam games.
It combines the host system's graphics drivers with the
container runtime's library stack, to get an environment that is as
similar to the container runtime as possible, but has graphics drivers
matching the host system. Lower-level libraries such as `libc`, `libdrm`
and `libX11` are taken from either the host system or the container runtime,
whichever one appears to be newer.

The Steam Linux Runtime can be used to run three categories of games:

  * Native Linux games on scout
  * Native Linux games on future runtimes: soldier, sniper, etc.
  * Windows games, using Proton

### Native Linux games on scout

As of 2021, in theory all native Linux games on Steam are built to target
Steam Runtime version 1, codenamed scout, which is based on
Ubuntu 12.04 (2012).
However, many games require newer libraries than Ubuntu 12.04, and many
game developers are not building their games in a strictly 'scout'-based
environment.

As a result, the Steam Linux Runtime compatibility tool runs games
in a hybrid environment where the majority of libraries are taken from
Steam Runtime version 2, codenamed soldier, which is based on
Debian 10 (2019).
Older libraries that are necessary for ABI compatibility with scout, such
as `libssl.so.1.0.0`, are also available.
A small number of libraries from soldier, such as `libcurl.so.3`, are
overridden by their scout equivalents to provide ABI compatibility.

Games targeting this environment should be built in a Steam Runtime 1 'scout'
Docker container.
By default, Steam will run them directly on the host system, providing
compatibility with scout by using the same `LD_LIBRARY_PATH`-based scout
runtime that is used to run Steam itself.
If the user selects the *Steam Linux Runtime* compatibility tool in the
game's properties, then Steam will launch a *Steam Linux Runtime - soldier*
container, then use the `LD_LIBRARY_PATH`-based scout runtime inside that
container to provide ABI compatibility for the game.

### Native Linux games on future runtimes: soldier, sniper, etc.

pressure-vessel is able to run games in a runtime that is newer than
scout.
Steam Runtime version 2, codenamed soldier, is based on Debian 10 (2019)
and is already available to the public.

Steam Runtime version 3, codenamed sniper, is very similar to soldier.
It is based on Debian 11 (2021) instead of Debian 10 (2019), with
correspondingly newer versions of various shared libraries and other
packages.

For commands, filenames, etc. in this document that refer to `soldier`,
substituting `sniper` will usually provide a similar result for the sniper
runtime.

As of mid 2022, releasing games on Steam that require the soldier, sniper
or newer container runtimes is not possible without manual action by the
Steam developers.
A development branch of Battle for Wesnoth is the first example of
[a game using the sniper container runtime][wesnoth-sniper].
We hope this will become available for general use in future.

[wesnoth-sniper]: https://github.com/ValveSoftware/steam-runtime/issues/508#issuecomment-1147665747

If it is useful to run in a newer container during development,
the *Steam Linux Runtime - soldier* or
*Steam Linux Runtime - sniper* compatibility tools can be used to
achieve this.

### Windows games, using Proton

Versions 5.13+ of Proton require recent Linux shared library stacks.
To ensure that these are available, even when running on an older
operating system, Steam automatically runs these versions of Proton
inside a *Steam Linux Runtime - soldier* container.

Future versions of Proton might switch to *Steam Linux Runtime - sniper*
or a newer stack.

## Suggested Steam configuration

You can move compatibility tools between Steam libraries through
the Steam user interface, in the same way as if they were games.
When developing with compatibility tools, it is usually most convenient
to [add a Steam Library folder][] in an easy-to-access location such as
`~/steamlibrary`, set it as the default, and move all compatibility
tools and games into that folder.

It is sometimes useful to try beta versions of the various compatibility
tools.
This is the same as [switching a game to a beta branch][], except that
instead of accessing the properties of the game, you would access the
properties of a compatibility tool such as *Steam Linux Runtime - soldier*
or *Proton 6.3*.

[add a Steam Library folder]: https://help.steampowered.com/en/faqs/view/4BD4-4528-6B2E-8327
[switching a game to a beta branch]: https://help.steampowered.com/en/faqs/view/5A86-0DF4-C59E-8C4A

## Launching Steam games in a Steam Linux Runtime container

To run Windows games using Proton in a Steam Linux Runtime container:

  * Edit the Properties of the game in the Steam client
  * Select `Force the use of a specific Steam Play compatibility tool`
  * Select Proton 5.13 or later

To run Linux games in a Steam Linux Runtime container:

  * Edit the Properties of the game in the Steam client
  * Select `Force the use of a specific Steam Play compatibility tool`
  * Select `Steam Linux Runtime`

This will automatically download *Steam Linux Runtime - soldier*,
together with Proton and/or *Steam Linux Runtime*, into your default
Steam library.

## Launching non-Steam games in a Steam Linux Runtime container

First, install a Steam game and configure it to use the required
compatibility tool, as above.
This ensures that the compatibility tool will be downloaded, and provides
an easy way to test that the compatibility tool is working correctly.

For a more scriptable version of this, launch one of these URLs:

  * Steam Linux Runtime (scout): `steam steam://install/1070560`
  * Steam Linux Runtime - soldier: `steam steam://install/1391110`
  * Steam Linux Runtime - sniper: `steam steam://install/1628350`
  * Proton Experimental: `steam steam://install/1493710`
  * Proton 7.0: `steam steam://install/1887720`
  * Proton 6.3: `steam steam://install/1580130`
  * Proton 5.13: `steam steam://install/1420170`

### Running commands in soldier, sniper, etc.

The simplest scenario for using the Steam Linux Runtime framework is to
run commands in a newer runtime such as soldier or sniper.
This is not directly supported by Steam itself, but is useful as a
baseline for testing.
To do this, run a command like:

```
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_soldier/run \
    -- \
    xterm
```

or more realistically for a game,

```
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_soldier/run \
    $pressure_vessel_options \
    -- \
    ./my-game.sh \
    $game_options
```

Like many Unix commands, pressure-vessel uses the special option `--`
as a divider between its own options and the game's options.
Anything before `--` will be parsed as a pressure-vessel option.
Anything after `--` will be ignored by pressure-vessel, but will be
passed to the game unaltered.

By default, the command to be run in the container gets `/dev/null` as
its standard input, so it cannot be an interactive shell like `bash`.
To pass through standard input from the shell where you are running the
command, use the `--terminal=tty` option:

```
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_soldier/run \
    --terminal=tty \
    -- \
    bash
```

or export the environment variable `PRESSURE_VESSEL_TERMINAL=tty`.

### Running commands in the scout Steam Linux Runtime environment

To run a game that was compiled for Steam Runtime 1 'scout', an
extra step is needed: the `SteamLinuxRuntime` compatibility tool
needs to make older libraries like `libssl.so.1.0.0` available
to the game.
You will also need to ensure that the `SteamLinuxRuntime` compatibility
tool is visible in the container environment: Steam normally does this
automatically, but outside Steam it can be necessary to do this yourself.
This means the commands required are not the same as for soldier or
sniper.

To enter this environment, use commands like this:

```
$ export STEAM_COMPAT_MOUNTS=/path/to/steamlibrary
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_soldier/run \
    $pressure_vessel_options \
    -- \
    /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime/scout-on-soldier-entry-point-v2 \
    -- \
    ./my-game.sh \
    $game_options
```

See [Making more files available in the container][],
below, for more information on `STEAM_COMPAT_MOUNTS`.

Similar to the `run` script, the `scout-on-soldier-entry-point-v2` script
uses `--` as a divider between its own options and the game to be run.

### Running a game under Proton in the Steam Linux Runtime environment

To run a Windows game under Proton 5.13 or later, again, an
extra step is needed to add Proton to the command-line.

Several extra environment variables starting with `STEAM_COMPAT_`
need to be set to make Proton work. They are usually set by Steam itself.

Something like this should generally work:

```
$ gameid=123            # replace with your numeric Steam app ID
$ export STEAM_COMPAT_CLIENT_INSTALL_PATH=$(readlink -f "$HOME/.steam/root")
$ export STEAM_COMPAT_DATA_PATH="/path/to/steamlibrary/compatdata/$gameid"
$ export STEAM_COMPAT_INSTALL_PATH=$(pwd)
$ export STEAM_COMPAT_LIBRARY_PATHS=/path/to/steamlibrary:/path/to/otherlibrary
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_soldier/run \
    $pressure_vessel_options \
    -- \
    /path/to/steamlibrary/steamapps/common/"Proton - Experimental"/proton \
    run \
    my-game.exe \
    $game_options
```

## Logging

By default, anything that the game writes to standard output or
standard error will appear on Steam's standard output or standard error.
Depending on the operating system, this might mean that it appears in
the systemd Journal, in a log file, or on an interactive terminal, or
it might be discarded.

Setting the environment variable `STEAM_LINUX_RUNTIME_LOG=1` makes
the Steam Linux Runtime infrastructure write more verbose output to a
log file, matching the pattern
`steamapps/common/SteamLinuxRuntime_soldier/var/slr-*.log`.
The log file's name will include the Steam app ID, if available.
The game's standard output and standard error are also redirected to
this log file.
A symbolic link `steamapps/common/SteamLinuxRuntime_soldier/var/slr-latest.log`
is also created, pointing to the most recently-created log.

The environment variable `STEAM_LINUX_RUNTIME_VERBOSE=1` can be exported
to make the Steam Linux Runtime even more verbose, which is useful when
debugging an issue.
This variable does not change the logging destination: if
`STEAM_LINUX_RUNTIME_LOG` is set to `1`, the Steam Linux Runtime will
write messages to its log file, or if not, it will write messages to
whatever standard error stream it inherits from Steam.

For Proton games, the environment variable `PROTON_LOG=1` makes Proton
write more verbose output to a log file, usually `~/steam-<appid>.log`.
The game's standard output and standard error will also appear in this
log file.
If both this and `STEAM_LINUX_RUNTIME_LOG` are used, this takes precedence:
the container runtime's own output will still appear in the container
runtime's log file, but Proton's output will not, and neither will the
game's output.
See [Proton documentation][] for more details.

[Proton documentation]: https://github.com/ValveSoftware/Proton/

## Running in an interactive shell

By default, the Steam Linux Runtime will just launch the game, but this
is not always convenient.

You can get an interactive shell inside the container instead of running
your game, by exporting the environment variable
`PRESSURE_VESSEL_SHELL=instead` or using the equivalent command-line option
`--shell=instead`.

When running games through Steam, you can either export
`PRESSURE_VESSEL_SHELL=instead` for the whole Steam process, or
[change an individual game's launch options][set launch options] to
`PRESSURE_VESSEL_SHELL=instead %command%`.

The special token `%command%` should be typed literally: it changes Steam's
interpretation of the launch options so that instead of appending the
given launch options to the game's command-line, Steam will replace
`%command%` with the complete command-line for the game, including any
compatibility tool wrappers.
See the [compatibility tool interface][] for more information on how
this works.

When launching the Steam Linux Runtime separately, you can either set
the same environment variable, or use the command-line option like this:

```
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_soldier/run \
    --shell=instead \
    -- \
    ./my-game.sh \
    $game_options
```

By default, the interactive shell runs in an `xterm` terminal emulator
which is included in the container runtime.
If you ran Steam or the game from a terminal or `ssh` session, you can
use `PRESSURE_VESSEL_TERMINAL=tty` or `--terminal=tty` to put the
interactive shell in the same place as your previous shell session.

When the interactive shell starts, the game's command-line is placed
in the special variable `"$@"`, as though you had run a command similar
to `set -- ./my-game.sh $game_options`.
You can run the game by entering `"$@"` at the prompt, including the
double quotes.
The game's standard output and standard error file descriptors will be
connected to the `xterm`, if used.

If you are using a Debian-derived system for development, the contents
of the container's `/etc/debian_chroot` file appear in the default shell
prompt to help you to recognise the container shell, for example:

```
(steamrt soldier 0.20211013.0)user@host:~$
```

Code similar to [Debian's /etc/bash.bashrc][] can be used to provide
this behaviour on other distributions, if desired.

[set launch options]: https://help.steampowered.com/en/faqs/view/7D01-D2DD-D75E-2955
[Debian's /etc/bash.bashrc]: https://sources.debian.org/src/bash/5.1-2/debian/etc.bash.bashrc/

## Inserting debugging commands into the container

Recent versions of the various container runtimes include a feature that
can be used to run arbitrary debugging commands inside the container.
This feature requires a working D-Bus session bus.

To activate this, set the `STEAM_COMPAT_LAUNCHER_SERVICE` environment
variable to the `compatmanager_layer_name` listed in the `toolmanifest.vdf`
of the compatibility tool used to run a game:

* `container-runtime` for "Steam Linux Runtime - soldier" or
    "Steam Linux Runtime - sniper"
    (as of July 2022, this feature is available in the `client_beta`
    branch of these compatibility tools)

* `proton` for any version of Proton
    (as of July 2022, this feature is available in the `bleeding-edge`
    branch of "Proton - Experimental")

* `scout-in-container` for "Steam Linux Runtime"
    (as of July 2022, this feature is available in the `client_beta`
    branch of this compatibility tool)

When running games through Steam, you can either export something like
`STEAM_COMPAT_LAUNCHER_SERVICE=container-runtime` for the whole Steam
process, or [change an individual game's launch options][set launch options]
to `STEAM_COMPAT_LAUNCHER_SERVICE=container-runtime %command%`.
The special token `%command%` should be typed literally.

The `SteamLinuxRuntime_soldier/run` script also accepts this environment
variable, so it can be used in commands like these:

```
$ export STEAM_COMPAT_MOUNTS=/path/to/steamlibrary
$ export STEAM_COMPAT_LAUNCHER_SERVICE=scout-in-container
$ cd /builds/native-linux-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_soldier/run \
    $pressure_vessel_options \
    -- \
    /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime/scout-on-soldier-entry-point-v2 \
    -- \
    ./my-game.sh \
    $game_options
```

or

```
$ gameid=123            # replace with your numeric Steam app ID
$ cd /builds/proton-game
$ export STEAM_COMPAT_LAUNCHER_SERVICE=proton
$ export STEAM_COMPAT_CLIENT_INSTALL_PATH=$(readlink -f "$HOME/.steam/root")
$ export STEAM_COMPAT_DATA_PATH="/path/to/steamlibrary/compatdata/$gameid"
$ export STEAM_COMPAT_INSTALL_PATH=$(pwd)
$ export STEAM_COMPAT_LIBRARY_PATHS=/path/to/steamlibrary:/path/to/otherlibrary
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_soldier/run \
    $pressure_vessel_options \
    -- \
    /path/to/steamlibrary/steamapps/common/"Proton - Experimental"/proton \
    run \
    my-game.exe \
    $game_options
```

After configuring this, while a game is running, you can list game sessions
where this has taken effect like this:

```
$ .../SteamLinuxRuntime_soldier/pressure-vessel/bin/steam-runtime-launch-client --list
--bus-name=com.steampowered.App123
--bus-name=com.steampowered.App123.Instance31679
```

and then connect to one of them with a command like:

```
$ .../SteamLinuxRuntime_soldier/pressure-vessel/bin/steam-runtime-launch-client \
    --bus-name=com.steampowered.App123 \
    --directory='' \
    -- \
    bash
```

Commands that are run like this will run inside the container, but their
standard input, standard output and standard error are connected to
the `steam-runtime-launch-client` command, similar to `ssh` or `docker exec`.
For example, `bash` can be used to get an interactive shell inside the
container, or an interactive tool like `gdb` or `python3` or a
non-interactive tool like `ls` can be placed directly after the `--`
separator.

### Debugging a game that is crashing on startup

Normally, the debug interface used by `steam-runtime-launch-client`
exits when the game does.
However, this is not useful if the game exits or crashes on startup
and the opportunity to debug it is lost.

To debug a game that is in this situation, in addition to
`STEAM_COMPAT_LAUNCHER_SERVICE`, you can export
`SRT_LAUNCHER_SERVICE_STOP_ON_EXIT=0`.
With this variable set, the command-launching service will *not* exit when
the game does, allowing debugging commands to be sent to it by using
`steam-runtime-launch-client`.
For example, it is possible to re-run the crashed game under [gdbserver][]
with a command like:

```
$ .../SteamLinuxRuntime_soldier/pressure-vessel/bin/steam-runtime-launch-client \
    --bus-name=com.steampowered.App123 \
    --directory='' \
    -- \
    gdbserver 127.0.0.1:12345 ./my-game-executable
```

Steam will behave as though the game is still running, because from
Steam's point of view, the debugging service has replaced the game.
To exit the "game" when you have finished debugging, instruct the
command server to terminate:

```
$ .../SteamLinuxRuntime_soldier/pressure-vessel/bin/steam-runtime-launch-client \
    --bus-name=com.steampowered.App123 \
    --terminate
```

## Layout of the container runtime

In general, the container runtime is similar to Debian and Ubuntu.
In particular, the standard directories for C/C++ libraries are
`/usr/lib/x86_64-linux-gnu` and `/usr/lib/i386-linux-gnu`.
The `lib64` or `lib32` directories are not used.

The host system's `/usr`, `/bin`, `/sbin` and `/lib*` appear below
`/run/host` in the container.
For example, a Fedora host system might provide
`/run/host/usr/lib64/libz.so.1`.

Files imported from the host system appear as symbolic links in the
`/usr/lib/pressure-vessel/overrides` hierarchy.
For example, if we are using the 64-bit `libz.so.1` from the host system,
it is found via the symbolic link
`/usr/lib/pressure-vessel/overrides/lib/x86_64-linux-gnu/libz.so.1`.

Non-OS directories such as `/home` and `/media` either do not appear
in the container, or appear in the container with the same paths that
they have on the host system. For example, `/home/me/.steam/root` on the
host system becomes `/home/me/.steam/root` in the container.

### Exploring the container from the host

The container's root directory can be seen from the host system by using
`ps` to find the process ID of any game or shell process inside the
container, and then using

```
ls -l /proc/$game_pid/root/
```

You'll see that the graphics drivers and possibly their dependencies
are available in `/overrides` inside that filesystem, while selected
files from the host are visible in `/run/host`.

You can also access a temporary copy of the container runtime in a
subdirectory of `steamapps/common/SteamLinuxRuntime_soldier/var/`
with a name similar to
`steamapps/common/SteamLinuxRuntime_soldier/var/tmp-1234567`.
These temporary copies use hard-links to avoid consuming additional
disk space and I/O bandwidth.
To avoid these temporary copies building up forever, they will be
deleted the next time you run a game in a container, unless you create a
file `steamapps/common/SteamLinuxRuntime_soldier/var/tmp-1234567/keep`
to flag that particular root directory to be kept for future reference.

## Access to filesystems

By default, `pressure-vessel` makes a limited set of files available in
the container, including:

  * the user's home directory
  * the Steam installation directory, if found
  * the current working directory

When running the Steam Linux Runtime via Steam, it also uses the environment
variables set by the [compatibility tool interface][] to find additional
files and directories that should be shared with the container.

### Private home directory

The Steam Linux Runtime has experimental support for giving each game
a private (virtualized) home directory.
In this mode, the user's real home directory is *not* shared with the game.
Instead, a directory on the host system is used as a "fake" home directory
for the game to write into.

This mode is not yet documented here.
Please see pressure-vessel source code for more details.

### Making more files available in the container

[Making more files available in the container]: #making-more-files-available-in-the-container

When running outside Steam, or when loading files from elsewhere in the
filesystem during debugging, it might be necessary to share additional
paths.
This can be done by setting the `STEAM_COMPAT_MOUNTS`,
`PRESSURE_VESSEL_FILESYSTEMS_RO` and/or `PRESSURE_VESSEL_FILESYSTEMS_RW`
environment variables.

For example, to share `/builds` and `/resources` with the container, you
might use a command like this:

```
$ export STEAM_COMPAT_MOUNTS=/builds:/resources
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_soldier/run \
    -- \
    ./my-game.sh \
    +set extra_texture_path /resources/my-game/textures
```

[compatibility tool interface]: steam-compat-tool-interface.md

## Developer mode

The `--devel` option puts `pressure-vessel` into a "developer mode"
which enables experimental or developer-oriented features.
It should be passed to the `run` script before the `--` marker,
like this:

```
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_soldier/run \
    --devel \
    -- \
    ./my-game.sh
```

Exporting `PRESSURE_VESSEL_DEVEL=1` is equivalent to using the `--devel`
option.

Currently, the features enabled by this option are:

  * The standard input file descriptor is inherited from the parent
    process, the same as `--terminal=tty`.
    This is useful when running an interactive shell like `bash`, or a
    game that accepts developer console commands on standard input.

  * `/sys` is mounted read-write instead of read-only, so that game
    developers can use advanced profiling and debugging mechanisms that
    might require writing to `/sys/kernel` or similar pseudo-filesystems.

This option is likely to have more effects in future pressure-vessel releases.

## Running in a SDK environment

By default, the various *Steam Linux Runtime* tools use a variant of the
container runtime that is identified as the *Platform*.
This is the same naming convention used in Flatpak.
The Platform runtime contains shared libraries needed by the games
themselves, as well as some very basic debugging tools, but to keep its
size manageable it does not contain a complete suite of debugging and
development tools.

A larger variant of each container runtime, the *SDK*, contains all the
same debugging and development tools that are provided in our official
Docker images.

To use the SDK, first identify the version of the Platform that you are
using.
This information can be found in `SteamLinuxRuntime_soldier/VERSIONS.txt`,
in the row starting with `soldier`.
Next, visit the corresponding numbered directory in
<https://repo.steampowered.com/steamrt-images-soldier/snapshots/>
and download the large archive named
`com.valvesoftware.SteamRuntime.Sdk-amd64,i386-soldier-runtime.tar.gz`.

In the `SteamLinuxRuntime_soldier` directory in your
Steam library, create a directory `SteamLinuxRuntime_soldier/sdk` and unpack the
archive into it, so that you have files like
`steamapps/common/SteamLinuxRuntime_soldier/sdk/files/lib/os-release` and
`steamapps/common/SteamLinuxRuntime_soldier/sdk/metadata`.

You can now use this runtime by passing the option `--runtime=sdk` to
the `SteamLinuxRuntime_soldier/run` script, for example:

```
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_soldier/run \
    $pressure_vessel_options \
    --runtime=sdk \
    -- \
    ./my-game.sh \
    $game_options
```

You will find that tools like `gdb` and `strace` are available in the SDK
environment.

## Upgrading pressure-vessel

[Upgrading pressure-vessel]: #upgrading-pressure-vessel

The recommended version of `pressure-vessel` is the one that is included
in the *Steam Linux Runtime - soldier* depot, and other versions are
not necessarily compatible with the container runtime and scripts in
the depot.
However, it can sometimes be useful for developers and testers to upgrade
their version of the `pressure-vessel` container tool, so that they can
make use of new features or try out new bug-fixes.

To do this, you can download an archive named `pressure-vessel-bin.tar.gz`
or `pressure-vessel-bin+src.tar.gz`, unpack it, and use it to replace the
`steamapps/common/SteamLinuxRuntime_soldier/pressure-vessel/` directory.

Official releases of pressure-vessel are available from
<https://repo.steampowered.com/pressure-vessel/snapshots/>.
If you are comfortable with using untested pre-release software, it is
also possible to download unofficial builds of pressure-vessel from our
continuous-integration system; the steps to do this are deliberately not
documented here.

To return to the recommended version of `pressure-vessel`, simply delete
the `steamapps/common/SteamLinuxRuntime_soldier/pressure-vessel/`
directory and use Steam's [Verify integrity][] feature to re-download it.

[Verify integrity]: https://help.steampowered.com/en/faqs/view/0C48-FCBD-DA71-93EB

## Attaching a debugger by using gdbserver

[gdbserver]: #attaching-a-debugger-by-using-gdbserver

The Platform runtime does not contain a full version of the `gdb` debugger,
but it does contain `gdbserver`, a `gdb` "stub" to which a full debugger
can be connected.

To use `gdbserver`, either run it from an interactive shell in the
container environment, or add it to your game's command-line:

```
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_soldier/run \
    $pressure_vessel_options \
    -- \
    gdbserver 127.0.0.1:12345 ./my-game-executable \
    $game_options
```

Alternatively, some games' launch scripts have a way to attach an external
debugger given in an environment variable, such as `GAME_DEBUGGER` in
several Valve games, including the `dota.sh` script that launches DOTA 2.
For games like this, export an environment variable similar to
`GAME_DEBUGGER="gdbserver 127.0.0.1:12345"`.

When `gdbserver` is used like this, it will pause until a debugger is
attached.
You can connect a debugger running outside the container to `gdb`
by writing gdb configuration similar to:

```
# This will search /builds/my-game/lib:/builds/my-game/lib64 for
# libraries
set sysroot /nonexistent
set solib-search-path /builds/my-game/lib:/builds/my-game/lib64
target remote 127.0.0.1:12345
```

or

```
# This will transfer executables and libraries through the remote
# debugging TCP channel
set sysroot /proc/54321/root
target remote 127.0.0.1:12345
```

where 54321 is the process ID of any process in the container, and then
running `gdb -x file-containing-configuration`.
In gdb, use the `cont` command to continue execution.

### Remote debugging via TCP

`gdbserver` and `gdb` communicate via TCP, so you can run a game on
one computer (such as a Steam Deck) and debug it on another (such as
your workstation).

Note that **there is no authentication**, so anyone on your local LAN
can use this to remote-control the `gdbserver`. Only do this on fully
trusted networks.

To use remote debugging, tell the `gdbserver` on the gaming device to
listen on `0.0.0.0` instead of `127.0.0.1`:

```
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_soldier/run \
    $pressure_vessel_options \
    -- \
    gdbserver 0.0.0.0:12345 ./my-game-executable \
    $game_options
```

and on the developer workstation, configure `gdb` to communicate with it,
replacing `192.0.2.42` with the gaming device's local IP address:

```
$ cat > gdb-config <<EOF
set sysroot /nonexistent
set solib-search-path /builds/my-game/lib:/builds/my-game/lib64
target remote 192.0.2.42:12345
EOF
$ gdb -x gdb-config
```

If your network assigns locally-resolvable hostnames to IP addresses,
then you can use those instead of the IP address.

### Remote debugging via ssh

Alternatively, if you have `ssh` access to the remote device, you can use
`ssh` port-forwarding to make the remote device's debugger port available
on your workstation. On the gaming device, listen on 127.0.0.1, the same
as for local debugging:

```
$ cd /builds/my-game
$ /path/to/steamlibrary/steamapps/common/SteamLinuxRuntime_soldier/run \
    $pressure_vessel_options \
    -- \
    gdbserver 127.0.0.1:12345 ./my-game-executable \
    $game_options
```

and on the developer workstation, configure `gdb` to communicate with it
via a port forwarded through a ssh tunnel, for example:

```
$ ssh -f -N -L 23456:127.0.0.1:12345 user@192.0.2.42
$ cat > gdb-config <<EOF
set sysroot /nonexistent
set solib-search-path /builds/my-game/lib:/builds/my-game/lib64
target remote 127.0.0.1:23456
EOF
$ gdb -x gdb-config
```

## Getting debug symbols

`gdb` can provide better backtraces for crashes and breakpoints if it
is given access to some sources of detached debug symbols.
Because the Steam Linux Runtime container combines libraries from the
container runtime with graphics drivers from the host system, a backtrace
might involve libraries from both of those locations, therefore detached
debug symbols for both of those might be required.

### For the host system

For Linux distributions that provide a [debuginfod][] server, it is usually
the easiest way to obtain detached debug symbols on-demand.
For example, on Debian systems:

```
$ export DEBUGINFOD_URLS="https://debuginfod.debian.net"
$ gdb -x file-containing-configuration
```

or on Arch Linux systems:

```
$ export DEBUGINFOD_URLS="https://debuginfod.archlinux.org"
$ gdb -x file-containing-configuration
```

Ubuntu does not yet provide a `debuginfod` server.
For Ubuntu, you will need to install special `-dbgsym` packages that
contain the detached debug symbols.

[debuginfod]: https://sourceware.org/elfutils/Debuginfod.html

### For the container runtime

There is currently no public `debuginfod` instance for the Steam Runtime.
Many of the libraries in soldier are taken directly from Debian, so
their debug symbols can be obtained from Debian's `debuginfod`:

```
$ export DEBUGINFOD_URLS="https://debuginfod.debian.net"
$ gdb -x file-containing-configuration
```

This can be combined with a `debuginfod` for a non-Debian distribution
such as Fedora by setting `DEBUGINFOD_URLS` to a space-separated list
of URLs.

For more thorough symbol coverage, first identify the version of the
Platform that you are using.
This information can be found in `SteamLinuxRuntime_soldier/VERSIONS.txt`,
in the row starting with `soldier`.
Next, visit the corresponding numbered directory in
<https://repo.steampowered.com/steamrt-images-soldier/snapshots/>
and download the large archive named
`com.valvesoftware.SteamRuntime.Sdk-amd64,i386-soldier-debug.tar.gz`.
Create a directory, for example `/tmp/soldier-dbgsym-0.20211013.0`,
and unpack the archive into that directory.

Then configure gdb with:

```
set debug-file-directory /tmp/soldier-dbgsym-0.20211013.0/files:/usr/lib/debug
```

and it should load the new debug symbols.

## Making a game container-friendly

The container runtime is intended to be relatively "transparent" so
that it can run existing games without modification, but there are
some things that game developers can do to make games work better in
the container environment, particularly developers of Linux-native games.

### Working directory

*Windows or Linux-native*

Each game has a subdirectory in `steamapps/common`, such as
`steamapps/common/My Great Game`, referred to in Steamworks as the
[install folder][].

It's simplest and most reliable if the game is designed to be launched
with its working directory equal to the top-level install folder.
In the [launch options][], this means leaving the `Working Dir` box
empty.
The main executable can be in a subdirectory, if you want it to be
(for example, DOTA 2 does this).

* Good: `Working Dir:` *(empty)*
* Might cause issues: `Working Dir: bin/linux64`

If you are choosing the name of the install folder for a new game, it's
simplest for various developer workflows if that subdirectory uses only
letters, digits, dashes and underscores, and doesn't contain punctuation
or Unicode.
Spaces are usually OK, but can be awkward when you are writing shell
scripts.

The container runtime is designed to cope with any directory name, but
it's more likely to have bugs when the directory name contains special
characters.

* Good: `steamapps/common/my-great-game` or `steamapps/common/MyGreatGame`
* Might cause issues: `steamapps/common/My Great Game™... 😹 Edition!`

[install folder]: https://partner.steamgames.com/doc/store/application/depots
[launch options]: https://partner.steamgames.com/doc/sdk/uploading

### Configuration and state

*Windows or Linux-native*

For best results, either use the [Steam Cloud API][], or save configuration
and state in the conventional directories for the platform.

For Windows games running under Proton, paths below `%USERPROFILE%`
should work well.
In Proton, these are redirected into the `steamapps/compatdata` directory.

For Linux-native games, the configuration and data directories from the
[freedesktop.org Base Directory specification][basedirs]
are recommended.

Major game engines and middleware libraries often have built-in support
for these conventional directories.
For example, the Unity engine has [Application.persistentDataPath][]
and the SDL library has [SDL\_GetPrefPath][SDL_GetPrefPath], both of
which are suitable.

[Steam Cloud API]: https://partner.steamgames.com/doc/features/cloud
[basedirs]: https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html
[Application.persistentDataPath]: https://docs.unity3d.com/ScriptReference/Application-persistentDataPath.html
[SDL_GetPrefPath]: https://wiki.libsdl.org/SDL_GetPrefPath

### Build environment

*Linux-native only*

For best results, compile Linux-native games in the official
[Steam Runtime SDK Docker container][] using [Docker][] or [Podman][].
The [SDK documentation][Steam Runtime SDK Docker container] has more
information about this.

[Steam Runtime SDK Docker container]: https://gitlab.steamos.cloud/steamrt/scout/sdk/-/blob/master/README.md
[Docker]: https://www.docker.com/
[Podman]: https://podman.io/

### Detecting the container environment

*Linux-native only*

When running in the Steam Linux Runtime environment and using Steam Runtime
libraries, the file `/etc/os-release` will contain a line `ID=steamrt`,
`ID="steamrt"` or `ID='steamrt'`.
Please see [os-release(5)][] for more details of the format and contents
of this file.

When running under the `pressure-vessel` container manager used by the
Steam Linux Runtime, the file `/run/host/container-manager` will contain
`pressure-vessel` followed by a newline.
The same file can be used to detect Flatpak ≥ 1.10.x, which
are identified as `flatpak` followed by a newline.
To support Flatpak 1.8.x or older, check whether the file `/.flatpak-info`
exists.

[os-release(5)]: https://www.freedesktop.org/software/systemd/man/os-release.html

### Input devices

*Linux-native only*

For best results, either use the [Steam Input][] APIs, or use a
middleware library with container support (such as SDL 2) to access
input devices more directly.
This ensures that your game will automatically detect new hotplugged
controllers, even across a container boundary.

If lower-level access is required, please note that `libudev` does not
provide hotplug support in the Steam Linux Runtime container, and cannot
guarantee to provide device enumeration either.  This is because the
protocol between `libudev` and `udevd` was not designed for use with
containers and is considered private to a particular version of udev.

In engines that implement their own input device handling, the suggested
approach is currently what SDL and Proton do: if one of the files
`/run/host/container-manager` or `/.flatpak-info` exists, then
enumerate input devices by reading `/dev` and `/sys`, with
change-notification by monitoring `/dev` using [inotify][].
Please see the [Linux joystick implementation in SDL][], specifically
the `ENUMERATION_FALLBACK` code paths, for sample code.

[Steam Input]: https://partner.steamgames.com/doc/features/steam_controller
[inotify]: https://man7.org/linux/man-pages/man7/inotify.7.html
[Linux joystick implementation in SDL]: https://github.com/libsdl-org/SDL/blob/main/src/joystick/linux/SDL_sysjoystick.c

### Shared libraries

[shared libraries]: #shared-libraries

*Linux-native only*

Try to avoid bundling libraries with your game if they are also available
in the Steam Runtime.
This can cause compatibility problems.
In particular, the Steam Runtime contains an up-to-date release of SDL 2,
so it should not be necessary to build your own version of SDL.

If you load a library dynamically, make sure to use its versioned SONAME,
such as `libvulkan.so.1` or `libgtk-3.so.0`, as the name to search for.
Don't use the development symlink such as `libvulkan.so` or `libgtk-3.so`,
and also don't use the fully-versioned name such as `libvulkan.so.1.2.189`
or `libgtk-3.so.0.2404.26`.

Use the versions of libraries that are included in the Steam Runtime,
if possible.

If you need to include a library in your game, consider using static
linking if the library's licensing permits this.
If you link statically, linking with the `-Wl,-Bsymbolic` compiler option
might avoid compatibility issues.

### Environment variables

*Linux-native only*

Avoid overwriting the `LD_LIBRARY_PATH` environment variable: that will
break some of the Steam Runtime's compatibility mechanisms.
If your game needs to use local (bundled, vendored) [shared libraries][],
it's better to append or prepend your library directory, depending on
whether your library directory should be treated as higher or lower
priority than system and container libraries.

Similarly, avoid overwriting the `LD_PRELOAD` environment variable:
that will break the Steam Overlay.
If your game needs to load a module via `LD_PRELOAD`, it's better to
append or prepend your module.

### Scripts

*Linux-native only*

Shell scripts can start with `#!/bin/sh` to use a small POSIX shell,
or `#!/bin/bash` to use GNU `bash`.
Similar to Debian and Ubuntu, the `/bin/sh` in the container is not
`bash`, so `bash` features cannot be used in `#!/bin/sh` scripts.
[Debian Policy][] has some useful advice on writing robust shell scripts.
Basic shell utilities are available in the container runtime, but more
advanced utilities might not be present.

[Debian Policy]: https://www.debian.org/doc/debian-policy/ch-files.html#scripts
