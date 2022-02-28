# Steam compatibility tool interface

<!-- This document:
Copyright 2021 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

## Compatibility tool declaration

Compatibility tools are declared by a file `compatibilitytool.vdf`,
which is a [VDF](https://developer.valvesoftware.com/wiki/KeyValues)
text file in this format:

```
"compatibilitytools"
{
  "compat tools"
  {
    "my_compat_tool"
    {
      "install_path" "."
      "display_name" "My Compatibility Tool"
      "from_oslist" "windows"
      "to_oslist" "linux"
    }
  }
}
```

`compat tools` can presumably contain one or more compatibility tools,
each identified by a unique name, in this case `my_compat_tool`.

Each compatibility tool can have these fields:

`install_path`
: Installation directory containing `toolmanifest.vdf` and other
    necessary files, for example
    `/path/to/steamapps/common/Proton - Experimental` for Proton

`display_name`
: Name to display in the Steam user interface

`from_oslist`
: Operating system(s?) whose executables can be run by this
    compatibility tool, for example `windows` for Proton

`to_oslist`
: Operating system(s?) that can run this compatibility tool,
    for example `linux` for Proton

For the compatibility tools provided by Steam itself, such as
Steam Linux Runtime and Proton, the equivalent of this information is
downloaded out-of-band and stored in `~/.steam/root/appcache/appinfo.vdf`,
so the depot downloaded into the Steam library will not contain this file.

Third-party compatibility tools can be installed in various locations
such as `~/.steam/root/compatibilitytools.d`.

Each subdirectory of `compatibilitytools.d` that contains
`compatibilitytool.vdf` is assumed to be a compatibility tool. The
installation path should be `"."` in this case.

Alternatively, the manifest can be placed directly in
`compatibilitytools.d` with the `install_path` set to the relative or
absolute path to the tool's installation directory.

## Tool manifest

Each compatibility tool has a *tool manifest*, `toolmanifest.vdf`, which
is a [VDF](https://developer.valvesoftware.com/wiki/KeyValues) text
file with one top-level entry, `manifest`.

`manifest` has the following known fields:

`version`
: The version number of the file format. The current version is `2`.
    The default is `1`.

`commandline`
: The command-line for the entry point.

`require_tool_appid`
: If set, this compatibility tool needs to be wrapped in another
    compatibility tool, specified by its numeric Steam app ID. For
    example, Proton 5.13 needs to be wrapped by "Steam Linux Runtime -
    soldier", so it sets this field to `1391110`.

`unlisted`
: If `1`, this compatibility tool will not be shown to users as an option for
    running games. For example, until Steam is able to distinguish between
    Steam Runtime 1 'scout' and Steam Runtime 2 'soldier' as distinct
    platforms, it is not useful to run ordinary Steam games under 'soldier',
    due to it being incompatible with 'scout'.

`use_sessions`
: If set to `1`, older versions of Steam would use the "session mode"
    described below.

`use_tool_subprocess_reaper`
: If set to `1`, Steam will send `SIGINT` to the compatibility tool
    instead of proceeding directly to `SIGKILL`, to give it a chance
    to do graceful cleanup.

## Command-line

In a version 1 manifest, the `commandline` is a shell expression.
The first word should start with `/` and is interpreted as being
relative to the directory containing the compat tool, so `/run`
really means something like
`~/.local/share/Steam/steamapps/common/MyCompatTool/run`.

The game's command-line will be appended to the tool's command-line,
so if you are using `getopt` or a compatible command-line parser, you
might want to use a `commandline` like `/run --option --other-option --`
to get the game's command-line to appear after a `--` separator.

In a version 2 manifest, the special token `%verb%` can appear in the
`commandline`. It is replaced by either `run` or `waitforexitandrun`.

If a compatibility tool has `require_tool_appid` set, then the command-line
is built up by taking the required tool, then appending the tool that
requires it, and finally appending the actual game: so if a compat tool
named `Inner` requires a compat tool named `Outer`, and both tools have
`/run %verb% --` as their `commandline`, then the final command line will
look something like this:

    /path/to/Outer/run waitforexitandrun -- \
    /path/to/Inner/run waitforexitandrun -- \
    /path/to/game.exe

(In reality it would all be one line, rather than having escaped newlines
as shown here.)

## Launch options

If a Steam game has user-specified launch options configured in its
Properties, and they contain the special token `%command%`, then
`%command%` is replaced by the entire command-line built up from the
compatibility tools. For example, if a compat tool named `Inner` requires
a compat tool named `Outer`, both tools have `/run %verb% --` as their
`commandline`, and the user has set the Launch Options in the game's
properties to

    DEBUG=yes taskset --cpu-list 0,2 %command% -console

then the final command line will look something like this:

    DEBUG=yes taskset --cpu-list 0,2 \
    /path/to/Outer/run waitforexitandrun -- \
    /path/to/Inner/run waitforexitandrun -- \
    /path/to/game.exe -console

(In reality it would all be one line, rather than having escaped newlines
as shown here.)

If the user-specified launch options for a Steam game do not contain
`%command%`, then they are simply appended to the command-line. For
example, setting the launch options to `--debug` is equivalent to
`%command% --debug`.

Shortcuts for non-Steam games do not currently implement the special
handling for `%command%`: the configured launch options are simply
appended to the command-line, regardless of whether they contain
`%command%` or not.
Feature request [steam-for-linux#6046][] is a request for this
limitation to be removed.

[steam-for-linux#6046]: https://github.com/ValveSoftware/steam-for-linux/issues/6046

Please note that not all invocations of compatibility tools take the
user-specified launch options into account: they are used when running
the actual game, but they are not used for install scripts.

## Environment

Some environment variables are set by Steam, including:

`LD_LIBRARY_PATH`
: Used to load Steam Runtime libraries.

`LD_PRELOAD`
: Used to load the Steam Overlay.

`STEAM_COMPAT_APP_ID`
: The decimal app-ID of the game, for example 440 for Team Fortress 2.

`STEAM_COMPAT_CLIENT_INSTALL_PATH`
: The absolute path to the directory where Steam is installed. This is the
    same as the target of the `~/.steam/root` symbolic link.

`STEAM_COMPAT_DATA_PATH`
: The absolute path to a directory that compatibility tools can use to
    store game-specific data. For example, Proton puts its `WINEPREFIX`
    in `$STEAM_COMPAT_DATA_PATH/pfx`.

`STEAM_COMPAT_INSTALL_PATH`
: The absolute path to the game's installation directory, for example
    `/home/you/.local/share/Steam/steamapps/common/Estranged Act I`,
    even if the current working directory is a subdirectory of this
    (as is the case for Estranged: Act I (261820) for example).

`STEAM_COMPAT_LIBRARY_PATHS`
: Colon-delimited list of paths to Steam Library directories containing
    the game, the compatibility tools  if  any,  and any  other resources
    that the game will need, such as DirectX installers.

`STEAM_COMPAT_MOUNTS`
: Colon-delimited list of paths to additional directories that are to be
    made available read/write in a pressure-vessel container.

`STEAM_COMPAT_SESSION_ID`
: In the historical session mode (see below), this is used to link together
    multiple container invocations into a logical "session".

`STEAM_COMPAT_SHADER_PATH`
: The absolute path to the variable data directory used for cached
    shaders, if any.

`STEAM_COMPAT_TOOL_PATHS`
: Colon-delimited list of paths to Steam compatibility tools in use.
    Typically this will be a single path, but when using tools with
    `require_tool_appid` set, such as Proton, it is a colon-separated
    list with the "innermost" tool first and the "outermost" tool last.

`SteamAppId`
: The same as `STEAM_COMPAT_APP_ID`, but only when running the actual
    game; not set when running
    [install scripts](https://partner.steamgames.com/doc/sdk/installscripts)
    or other setup commands. The Steamworks API assumes that every process
    with this environment variable set is part of the actual game.

When Steam invokes a tool with `require_tool_appid` set, such as Proton,
the environment variables are set as described for the "outermost" tool,
which can change them if required before it invokes the "innermost" tool
(although most should not be changed).

For example, when using Proton with "Steam Linux Runtime - soldier", the
"outer" compatibility tool "Steam Linux Runtime - soldier" is run with
`LD_LIBRARY_PATH` set by the Steam Runtime to point to mixed host and
scout libraries. After setting up the container, it runs the "inner"
compatibility tool (Proton) with an entirely new `LD_LIBRARY_PATH`
pointing to mixed host and soldier libraries.

## Native Linux Steam games

This is the simplest situation and can be considered to be the baseline.

In recent versions of Steam, the game process is wrapped in a `reaper`
process which sets itself as a subreaper using `PR_SET_CHILD_SUBREAPER`
(see
[**prctl**(2)](https://manpages.debian.org/unstable/manpages-dev/prctl.2.en.html)
for details).

Version 1 compat tools are not invoked specially.

Version 2 compat tools are invoked with a `%verb%` in the `commandline`
(if any) replaced by `waitforexitandrun`.

The game is expected to run in the usual way. Conventionally, it does not
double-fork to put itself in the background, but if it does, any background
processes will be reparented to have the subreaper as their parent.

The current working directory is the working directory configured in the
game's Steam metadata (for example `estrangedact1` for Estranged: Act 1,
261820), or the game's directory (or maybe the executable's directory?)
if unconfigured. The compatibility tool is expected to preserve this
working directory when running the actual game.

The environment variable `STEAM_COMPAT_SESSION_ID` is not set.

The environment variable `STEAM_COMPAT_APP_ID` is set to the app-ID
of the game. The environment variable `SteamAppId` has the same value.

The [launch options](#launch-options) are used.

## Windows games with no install script

Some Windows games, such as Soldat (638490), do not have an
"install script" in their metadata. Running these games behaves much the
same as a native Linux game: it is wrapped in a subreaper, version 1
compat tools are not invoked specially, and version 2 compat tools
are invoked with a `%verb%` in the `commandline` (if any) replaced by
`waitforexitandrun`.

The environment variable `STEAM_COMPAT_SESSION_ID` is not set.
Historically, it was set to a unique token that was not used in any
previous invocation.

The environment variable `STEAM_COMPAT_APP_ID` is set to the app-ID
of the game. The environment variable `SteamAppId` has the same value.

The current working directory is the working directory configured in the
game's Steam metadata, as usual.

The [launch options](#launch-options) are used.

## Windows games with an install script (since Steam beta 1623823138)

Other Windows games, such as Everquest (205710), have an
[install script](https://partner.steamgames.com/doc/sdk/installscripts),
conventionally named `installscript.vdf`, which must be run by an
interpreter.

Since Steam beta 1623823138, these games are run like this:

* Run the install script. For version 1 compat tools, the compat
    tool is run as usual, except that the command appended to the
    command-line is the install script interpreter, not the actual game.
    For version 2 compat tools, the `%verb%` is set to `run` instead
    of the usual `waitforexitandrun`.

    The environment variable `STEAM_COMPAT_SESSION_ID` is not set.

    The environment variable `STEAM_COMPAT_APP_ID` is set to the app-ID
    of the game.

    The environment variable `SteamAppId` is *not* set in this case.

    The current working directory is *not* the game's directory in this case.
    As currently implemented, it appears to be the Steam installation
    (usually `~/.local/share/Steam` or `~/.steam/debian-installation`)
    but this is probably not guaranteed.

    The [launch options](#launch-options) are **not** used here.

* Run the actual game in the usual way.

    The environment variable `STEAM_COMPAT_SESSION_ID` is not set.

    The environment variables `STEAM_COMPAT_APP_ID` and `SteamAppId`
    are both set to the app-ID of the game as usual.

    The current working directory is the working directory configured in
    the game's Steam metadata, as usual.

    The [launch options](#launch-options) are used, as usual.

## Historical behaviour of Windows games (session mode v2)

Before Steam beta 1623823138, each Windows game had several setup commands
to run before it could be launched. They were invoked like this:

* Run the first setup command. For version 1 compat tools, the compat
    tool is run as usual, except that the command appended to the
    command-line is the setup command, not the actual game.
    For version 2 compat tools, additionally the `%verb%` is set to `run`
    instead of `waitforexitandrun`.

    The environment variable `STEAM_COMPAT_SESSION_ID` is set to a
    unique token (in practice a 64-bit number encoded in hex).

    The environment variable `STEAM_COMPAT_APP_ID` is set to the app-ID
    of the game.

    The environment variable `SteamAppId` is *not* set in this case.

    The current working directory is *not* the game's directory in this case.
    As currently implemented, it appears to be the Steam installation
    (usually `~/.local/share/Steam` or `~/.steam/debian-installation`)
    but this is probably not guaranteed.

    The [launch options](#launch-options) are **not** used here.

    The compat tool is expected to do any setup that it needs to do
    (for example pressure-vessel must start a container), then run
    the given command and exit. The compat tool may leave processes
    running in the background.

* Run the second setup command, in the same way as the first, and so on.
    More than one command can be run in parallel, so tools that need to
    do setup (such as pressure-vessel) must use a lock to ensure the setup
    is only done by whichever setup command wins the race to be the first.

    All environment variables are the same as for the first setup command,
    and in particular the `STEAM_COMPAT_SESSION_ID` is the same. This is
    how compatibility tools can know which containers or subprocesses
    to reuse.

* When all setup commands have finished, run the actual game. For
    version 2 compat tools, the `%verb%` is set to `waitforexitandrun`.
    The compat tool is expected to terminate any background processes
    (for example pressure-vessel terminates the container and Proton
    terminates the `wineserver`) and wait for them to exit, then launch
    the actual game.

    The environment variable `STEAM_COMPAT_SESSION_ID` is set to the
    same unique token as for all the setup commands, so that the compat
    tool can identify which subprocesses or containers to tear down.

    The environment variables `STEAM_COMPAT_APP_ID` and `SteamAppId`
    are both set to the app-ID of the game as usual.

    The [launch options](#launch-options) are used, as usual.

## Non-Steam games

Version 1 compat tools are not invoked specially.

Version 2 compat tools are invoked with a `%verb%` in the `commandline`
(if any) replaced by `waitforexitandrun`.

The environment variable `STEAM_COMPAT_SESSION_ID` is not set.
Historically, it was set to a unique token that was not used in any
previous invocation.

`STEAM_COMPAT_APP_ID` is set to `0`. `SteamAppId` is not set.

`STEAM_COMPAT_DATA_PATH` is set to a unique directory per non-Steam game.
It is currently in the same format as Steam games' equivalent directories,
but with a large arbitrary integer replacing the Steam app ID.

`STEAM_COMPAT_INSTALL_PATH` is not set.

`LD_LIBRARY_PATH` is usually set to the empty string to "escape" from
the Steam Runtime library stack for non-Steam games, although games
configured in Steam via development tools can be flagged to use the
Steam Runtime library stack (this is done for games that are intended to
be released onto Steam as native Linux games).

The current working directory defaults to the directory containing the
main executable, but can be reconfigured through the shortcut's properties.
Steam Linux Runtime (pressure-vessel) containers will not work as expected
unless this is set to the game's top-level directory.

Launch options are appended to the command-line. The special token
`%command%` in Steam games' launch options is not currently used here,
but feature request [steam-for-linux#6046][] is a request for this
limitation to be removed.
