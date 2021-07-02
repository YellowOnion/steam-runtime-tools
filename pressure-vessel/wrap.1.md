---
title: pressure-vessel-wrap
section: 1
...

<!-- This document:
Copyright © 2020-2021 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

pressure-vessel-wrap - run programs in a bubblewrap container

# SYNOPSIS

**pressure-vessel-wrap**
[*OPTIONS*]
[**--**]
*COMMAND* [*ARGUMENTS...*]

**pressure-vessel-wrap --test**

**pressure-vessel-wrap --version**

# DESCRIPTION

**pressure-vessel-wrap** runs *COMMAND* in a container, using **bwrap**(1).

# OPTIONS

`--batch`
:   Disable all interactivity and redirection: ignore `--shell`,
    all `--shell-` options, `--terminal`, `--tty` and `--xterm`.

`--copy-runtime`, `--no-copy-runtime`
:   If a `--runtime` is active, copy it into a subdirectory of the
    `--variable-dir`, edit the copy in-place, and mount the copy read-only
    in the container, instead of setting up elaborate bind-mount structures.
    This option requires the `--variable-dir` option to be used.

    `--no-copy-runtime` disables this behaviour and is currently
    the default.

`--copy-runtime-into` *DIR*
:   If *DIR* is an empty string, equivalent to `--no-copy-runtime`.
    Otherwise, equivalent to `--copy-runtime --variable-dir=DIR`.

`--env-if-host` *VAR=VAL*
:   If *COMMAND* is run with `/usr` from the host system, set
    environment variable *VAR* to *VAL*. If not, leave *VAR* unchanged.

`--filesystem` *PATH*
:   Share *PATH* from the host system with the container.

`--freedesktop-app-id` *ID*
:   If using `--unshare-home`, use *~/.var/app/ID* as the home
    directory. This is the same home directory that a Flatpak app for
    freedesktop.org app *ID* would use.

`--gc-legacy-runtimes`, `--no-gc-legacy-runtimes`
:   Garbage-collect old temporary runtimes in the `--runtime-base` that
    appear to be left over from older versions of the **SteamLinuxRuntime**
    launcher scripts. `--no-gc-legacy-runtimes` disables this behaviour,
    and is the default.

`--gc-runtimes`, `--no-gc-runtimes`
:   If using `--variable-dir`, garbage-collect old temporary
    runtimes that are left over from a previous **pressure-vessel-wrap**.
    This is the default. `--no-gc-runtimes` disables this behaviour.

`--generate-locales`, `--no-generate-locales`
:   Passed to **pressure-vessel-adverb**(1).
    The default is `--generate-locales`, overriding the default
    behaviour of **pressure-vessel-adverb**(1).

`--graphics-provider` *DIR*
:   If using a `--runtime`, use *DIR* to provide graphics drivers.

    If *DIR* is the empty string, take the graphics drivers from the
    runtime. This will often lead to software rendering, poor performance,
    incompatibility with recent GPUs or a crash, and is only intended to
    be done for development or debugging.

    Otherwise, *DIR* must be the absolute path to a directory that is
    the root of an operating system installation (a "sysroot"). The
    default is `/`.

`--home` *DIR*
:   Use *DIR* as the home directory. This implies `--unshare-home`.

`--host-ld-preload` *MODULE*
:   Deprecated equivalent of `--ld-preload`. Despite its name, the
    *MODULE* has always been interpreted as relative to the current
    execution environment, even if **pressure-vessel-wrap** is running
    in a container.

`--import-vulkan-layers`, `--no-import-vulkan-layers`
:   If `--no-import-vulkan-layers` is specified, the Vulkan layers will
    not be imported from the host system.
    The default is `--import-vulkan-layers`.

`--keep-game-overlay`, `--remove-game-overlay`
:   If `--remove-game-overlay` is specified, remove the Steam Overlay
    from the `LD_PRELOAD`. The default is `--keep-game-overlay`.

`--launcher`
:   Instead of specifying a command with its arguments to execute, all the
    elements after `--` will be used as arguments for
    `pressure-vessel-launcher`. This option implies `--batch`.

`--ld-audit` *MODULE*
:   Add *MODULE* from the current execution environment to `LD_AUDIT`
    when executing *COMMAND*. If *COMMAND* is run in a container, or if
    **pressure-vessel-wrap** is run in a Flatpak sandbox and *COMMAND*
    will be run in a different container or on the host system, then the
    path of the *MODULE* will be adjusted as necessary.

`--ld-preload` *MODULE*
:   Add *MODULE* from the current execution environment to `LD_PRELOAD`
    when executing *COMMAND*. If *COMMAND* is run in a container, or if
    **pressure-vessel-wrap** is run in a Flatpak sandbox and *COMMAND*
    will be run in a different container or on the host system, then the
    path of the *MODULE* will be adjusted as necessary.

`--only-prepare`
:   Prepare the runtime, but do not actually run *COMMAND*.
    With `--copy-runtime`, the prepared runtime will appear in
    a subdirectory of the `--variable-dir`.

`--pass-fd` *FD*
:   Pass the file descriptor *FD* (specified as a small positive integer)
    from the parent process to the *COMMAND*. The default is to only pass
    through file descriptors 0, 1 and 2
    (**stdin**, **stdout** and **stderr**).

`--runtime=`
:   Use the current execution environment's /usr to provide /usr in
    the container.

`--runtime` *PATH*
:   Use *PATH* to provide /usr in the container.
    If *PATH*/files/ is a directory, *PATH*/files/ is used as the source
    of runtime files: this is appropriate for Flatpak-style runtimes that
    contain *PATH*/files/ and *PATH*/metadata. Otherwise, *PATH* is used
    directly.
    If *PATH*/files/usr or *PATH*/usr exists, the source of runtime files
    is assumed to be a complete sysroot
    (containing bin/sh, usr/bin/env and many other OS files).
    Otherwise, it is assumed to be a merged-/usr environment
    (containing bin/sh, bin/env and many other OS files).
    For example, a Flatpak runtime is a suitable value for *PATH*.

`--runtime-archive` *ARCHIVE*
:   Unpack *ARCHIVE* and use it to provide /usr in the container, similar
    to `--runtime`. The `--runtime-id` option is also required, unless
    the filename of the *ARCHIVE* ends with a supported suffix
    (`-runtime.tar.gz` or `-sysroot.tar.gz`) and it is accompanied by a
    `-buildid.txt` file.

    If this option is used, then `--variable-dir`
    (or its environment variable equivalent) is also required.
    This option and `--runtime` cannot both be used.

    The archive will be unpacked into a subdirectory of the `--variable-dir`.
    Any other subdirectories of the `--variable-dir` that appear to be
    different runtimes will be deleted, unless they contain a file
    at the top level named `keep` or are currently in use.

    The archive must currently be a gzipped tar file whose name ends
    with `.tar.gz`. Other formats might be allowed in future.

`--runtime-base` *PATH*
:   If `--runtime` or `--runtime-archive` is specified as a relative path,
    look for it relative to *PATH*.

`--runtime-id` *ID*
:   Use *ID* to construct a directory into which the `--runtime-archive`
    will be unpacked, overriding an accompanying `-buildid.txt` file
    if present. The *ID* must match the regular expression
    `^[A-Za-z0-9_][-A-Za-z0-9_.]*$` (a non-empty sequence of ASCII
    letters, digits, underscores, dots and dashes, starting with a
    letter, digit or underscore). It will typically be in a format
    similar to `0.20210301.0` or `soldier_0.20210301.0`.

    If the *ID* is the same as in a previous run of pressure-vessel-wrap,
    the content of the `--runtime-archive` will be assumed to be the
    same as in that previous run, resulting in the previous runtime
    being reused.

`--share-home`, `--unshare-home`
:   If `--unshare-home` is specified, use the home directory given
    by `--home`, `--freedesktop-app-id`, `--steam-app-id` or their
    corresponding environment variables, at least one of which must be
    provided.

    If `--share-home` is specified, use the home directory from the
    current execution environment.

    The default is `--share-home`, unless `--home` or its
    corresponding environment variable is used.

`--share-pid`, `--unshare-pid`
:   If `--unshare-pid` is specified, create a new process ID namespace.
    Note that this is known to interfere with IPC between Steam and its
    games. The default is `--share-pid`.

`--shell=none`
:   Don't run an interactive shell. This is the default.

`--shell=after`, `--shell-after`
:   Run an interactive shell in the container after *COMMAND* exits.
    In that shell, running `"$@"` will re-run *COMMAND*.

`--shell=fail`, `--shell-fail`
:   The same as `--shell=after`, but do not run the shell if *COMMAND*
    exits with status 0.

`--shell=instead`, `--shell-instead`
:   The same as `--shell=after`, but do not run *COMMAND* at all.

`--single-thread`
:   Avoid multi-threaded code paths, for debugging.

`--steam-app-id` *N*
:   Assume that we are running Steam app ID *N*, specified as an integer.
    If using `--unshare-home`, use *~/.var/app/com.steampowered.AppN*
    as the home directory.

`--terminal=none`
:   Disable features that would ordinarily use a terminal.

`--terminal=auto`
:   Equivalent to `--terminal=xterm` if a `--shell` option other
    than `none` is used, or `--terminal=none` otherwise.

`--terminal=tty`, `--tty`
:   Use the current execution environment's controlling tty for
    the `--shell` options.

`--terminal=xterm`, `--xterm`
:   Start an **xterm**(1) inside the container.

`--terminate-timeout` *SECONDS*, `--terminate-idle-timeout` *SECONDS*
:   Passed to **pressure-vessel-wrap**(1).

`--test`
:   Perform a smoke-test to determine whether **pressure-vessel-wrap**
    can work, and exit. Exit with status 0 if it can or 1 if it cannot.

`--variable-dir` *PATH*
:   Use *PATH* as a cache directory for files that are temporarily
    unpacked or copied. It will be created automatically if necessary.

`--verbose`
:   Be more verbose.

`--version`
:   Print the version number and exit.

`--with-host-graphics`, `--without-host-graphics`
:   Deprecated form of `--graphics-provider`.
    `--with-host-graphics` is equivalent to either
    `--graphics-provider=/run/host` if it looks suitable, or
    `--graphics-provider=/` if not.
    `--without-host-graphics` is equivalent to `--graphics-provider=""`.

# ENVIRONMENT

The following environment variables (among others) are read by
**pressure-vessel-wrap**(1).

`__EGL_VENDOR_LIBRARY_DIRS`, `__EGL_VENDOR_LIBRARY_FILENAMES`
:   Used to locate EGL ICDs to be made available in the container
    if `--runtime` and `--graphics-provider` are active.

`BWRAP` (path)
:   Absolute path to **bwrap**(1).
    The default is to try several likely locations.

`DBUS_SESSION_BUS_ADDRESS`, `DBUS_SYSTEM_BUS_ADDRESS`
:   Used to locate the well-known D-Bus session and system buses
    so that they can be made available in the container.

`DISPLAY`
:   Used to locate the X11 display to make available in the container.

`FLATPAK_ID`
:   Used to locate the app-specific data directory when running in a
    Flatpak environment.

`LIBGL_DRIVERS_PATH`
:   Used to locate Mesa DRI drivers to be made available in the container
    if `--runtime` and `--graphics-provider` are active.

`LIBVA_DRIVERS_PATH`
:   Used to locate VA-API drivers to be made available in the container
    if `--runtime` and `--graphics-provider` are active.

`PRESSURE_VESSEL_BATCH` (boolean)
:   If set to `1`, equivalent to `--batch`.
    If set to `0`, no effect.

`PRESSURE_VESSEL_COPY_RUNTIME` (boolean)
:   If set to `1`, equivalent to `--copy-runtime`.
    If set to `0`, equivalent to `--no-copy-runtime`.

`PRESSURE_VESSEL_COPY_RUNTIME_INTO` (path or empty string)
:   If the string is empty, it is a deprecated equivalent of
    `--no-copy-runtime`. Otherwise, it is a deprecated equivalent of
    `--copy-runtime --variable-dir="$PRESSURE_VESSEL_COPY_RUNTIME_INTO"`.

`PRESSURE_VESSEL_FILESYSTEMS_RO` (`:`-separated list of paths)
:   Make these paths available read-only inside the container if they
    exist, similar to `--filesystem` but read-only.
    For example, MangoHUD and vkBasalt users might use
    `PRESSURE_VESSEL_FILESYSTEMS_RO="$MANGOHUD_CONFIGFILE:$VKBASALT_CONFIG_FILE"`
    if the configuration files are outside the home directory.

`PRESSURE_VESSEL_FILESYSTEMS_RW` (`:`-separated list of paths)
:   Make these paths available read/write inside the container if they
    exist, similar to `--filesystem`.

`PRESSURE_VESSEL_FDO_APP_ID` (string)
:   Equivalent to
    `--freedesktop-app-id="$PRESSURE_VESSEL_FDO_APP_ID"`.

`PRESSURE_VESSEL_GC_RUNTIMES` (boolean)
:   If set to `1`, equivalent to `--gc-runtimes`.
    If set to `0`, equivalent to `--no-gc-runtimes`.

`PRESSURE_VESSEL_GENERATE_LOCALES` (boolean)
:   If set to `1`, equivalent to `--generate-locales`.
    If set to `0`, equivalent to `--no-generate-locales`.

`PRESSURE_VESSEL_GRAPHICS_PROVIDER` (absolute path or empty string)
:   Equivalent to `--graphics-provider="$PRESSURE_VESSEL_GRAPHICS_PROVIDER"`.

`PRESSURE_VESSEL_HOME` (path)
:   Equivalent to `--home="$PRESSURE_VESSEL_HOME"`.

`PRESSURE_VESSEL_HOST_GRAPHICS` (boolean)
:   Deprecated form of `$PRESSURE_VESSEL_GRAPHICS_PROVIDER`.
    If set to `1`, equivalent to either
    `--graphics-provider=/run/host` if it looks suitable, or
    `--graphics-provider=/` if not.
    If set to `0`, equivalent to `--graphics-provider=""`.

`PRESSURE_VESSEL_IMPORT_VULKAN_LAYERS` (boolean)
:   If set to `1`, equivalent to `--import-vulkan-layers`.
    If set to `0`, equivalent to `--no-import-vulkan-layers`.

`PRESSURE_VESSEL_LOG_INFO` (boolean)
:   If set to `1`, increase the log verbosity up to the info level.
    If set to `0`, no effect.

`PRESSURE_VESSEL_LOG_WITH_TIMESTAMP` (boolean)
:   If set to `1`, prepend the log entries with a timestamp.
    If set to `0`, no effect.

`PRESSURE_VESSEL_REMOVE_GAME_OVERLAY` (boolean)
:   If set to `1`, equivalent to `--remove-game-overlay`.
    If set to `0`, equivalent to `--keep-game-overlay`.

`PRESSURE_VESSEL_RUNTIME` (path, filename or empty string)
:   Equivalent to `--runtime="$PRESSURE_VESSEL_RUNTIME"`.

`PRESSURE_VESSEL_RUNTIME_ARCHIVE` (path, filename or empty string)
:   Equivalent to `--runtime-archive="$PRESSURE_VESSEL_RUNTIME_ARCHIVE"`.

`PRESSURE_VESSEL_RUNTIME_BASE` (path, filename or empty string)
:   Equivalent to `--runtime-base="$PRESSURE_VESSEL_RUNTIME_BASE"`.

`PRESSURE_VESSEL_RUNTIME_ID` (string matching `^[A-Za-z0-9_][-A-Za-z0-9_.]*$`)
:   Equivalent to `--runtime-id="$PRESSURE_VESSEL_RUNTIME_ID"`.

`PRESSURE_VESSEL_SHARE_HOME` (boolean)
:   If set to `1`, equivalent to `--share-home`.
    If set to `0`, equivalent to `--unshare-home`.

`PRESSURE_VESSEL_SHARE_PID` (boolean)
:   If set to `1`, equivalent to `--share-pid`.
    If set to `0`, equivalent to `--unshare-pid`.

`PRESSURE_VESSEL_SHELL` (`none`, `after`, `fail` or `instead`)
:   Equivalent to `--shell="$PRESSURE_VESSEL_SHELL"`.

`PRESSURE_VESSEL_TERMINAL` (`none`, `auto`, `tty` or `xterm`)
:   Equivalent to `--terminal="$PRESSURE_VESSEL_TERMINAL"`.

`PRESSURE_VESSEL_VARIABLE_DIR` (path)
:   Equivalent to `--variable-dir="$PRESSURE_VESSEL_VARIABLE_DIR"`.

`PRESSURE_VESSEL_VERBOSE` (boolean)
:   If set to `1`, equivalent to `--verbose`.

`PULSE_CLIENTCONFIG`
:   Used to locate PulseAudio client configuration.

`PULSE_SERVER`
:   Used to locate a PulseAudio server.

`PWD`
:   Used to choose between logically equivalent names for the current
    working directory (see **get_current_dir_name**(3)).

`STEAM_COMPAT_APP_ID` (integer)
:   Equivalent to `--steam-app-id="$STEAM_COMPAT_APP_ID"`.

`STEAM_COMPAT_APP_LIBRARY_PATH` (path)
:   Deprecated equivalent of `STEAM_COMPAT_MOUNTS`, except that it is
    a single path instead of being colon-delimited.

`STEAM_COMPAT_APP_LIBRARY_PATHS` (`:`-separated list of paths)
:   Deprecated equivalent of `STEAM_COMPAT_MOUNTS`.

`STEAM_COMPAT_CLIENT_INSTALL_PATH` (path)
:   When used as a Steam compatibility tool, the absolute path to the
    Steam client installation directory.
    This is made available read/write in the container.

`STEAM_COMPAT_DATA_PATH` (path)
:   When used as a Steam compatibility tool, the absolute path to the
    variable data directory used by Proton, if any.
    This is made available read/write in the container.

`STEAM_COMPAT_INSTALL_PATH` (path)
:   Top-level directory containing the game itself, even if the current
    working directory is actually a subdirectory of this.
    This is made available read/write in the container.

`STEAM_COMPAT_LIBRARY_PATHS` (`:`-separated list of paths)
:   Colon-delimited list of paths to Steam Library directories containing
    the game, the compatibility tools if any, and any other resources
    that the game will need, such as DirectX installers.
    Each is currently made available read/write in the container.

`STEAM_COMPAT_MOUNT_PATHS` (`:`-separated list of paths)
:   Deprecated equivalent of `STEAM_COMPAT_MOUNTS`.

`STEAM_COMPAT_MOUNTS` (`:`-separated list of paths)
:   Colon-delimited list of paths to additional directories that are to
    be made available read/write in the container.

`STEAM_COMPAT_SESSION_ID` (integer)
:   (Not used yet, but should be.)

`STEAM_COMPAT_SHADER_PATH` (path)
:   When used as a Steam compatibility tool, the absolute path to the
    variable data directory used for cached shaders, if any.
    This is made available read/write in the container.

`STEAM_COMPAT_TOOL_PATH` (path)
:   Deprecated equivalent of `STEAM_COMPAT_TOOL_PATHS`, except that it is
    a single path instead of being colon-delimited.

`STEAM_COMPAT_TOOL_PATHS` (`:`-separated list of paths)
:   Colon-delimited list of paths to Steam compatibility tools in use,
    such as Proton and the Steam Linux Runtime.
    They are currently made available read/write in the container.

`STEAM_RUNTIME` (path)
:   **pressure-vessel-wrap** refuses to run if this environment variable
    is set. Use **pressure-vessel-unruntime**(1) instead.

`SteamAppId` (integer)
:   Equivalent to `--steam-app-id="$SteamAppId"`.
    Must only be set for the main processes that are running a game, not
    for any setup/installation steps that happen first.
    `STEAM_COMPAT_APP_ID` is used with a higher priority.

`VDPAU_DRIVER_PATH`
:   Used to locate VDPAU drivers to be made available in the container
    if `--runtime` and `--graphics-provider` are active.

`VK_ICD_FILENAMES`
:   Used to locate Vulkan ICDs to be made available in the container
    if `--runtime` and `--graphics-provider` are active.

`VK_LAYER_PATH`
:   Used to locate Vulkan explicit layers
    if `--runtime` and `--graphics-provider` are active.

`WAYLAND_DISPLAY`
:   Used to locate the Wayland display to make available in the container.

`XDG_DATA_DIRS`
:   Used to locate Vulkan ICDs and layers
    if `--runtime` and `--graphics-provider` are active.

The following environment variables are set by **pressure-vessel-wrap**(1).

`__EGL_VENDOR_LIBRARY_DIRS`
:   Unset if `--runtime` and `--graphics-provider` are active,
    to make sure `__EGL_VENDOR_LIBRARY_FILENAMES` will be used instead.

`__EGL_VENDOR_LIBRARY_FILENAMES`
:   Set to a search path for EGL ICDs
    if `--runtime` and `--graphics-provider` are active.

`DBUS_SESSION_BUS_ADDRESS`, `DBUS_SYSTEM_BUS_ADDRESS`
:   Set to paths in the container's private `/run` where the well-known
    D-Bus session and system buses are made available.

`DISPLAY`
:   Set to a value corresponding to the socket in the container's
    `/tmp/.X11-unix`.

`LD_AUDIT`
:   Set according to `--ld-audit`.

`LD_LIBRARY_PATH`
:   Set to a search path for shared libraries if `--runtime` is active.

`LD_PRELOAD`
:   Set according to `--ld-preload`, `--keep-game-overlay`,
    `--remove-game-overlay`.

`LIBGL_DRIVERS_PATH`
:   Set to a search path for Mesa DRI drivers
    if `--runtime` and `--graphics-provider` are active.

`LIBVA_DRIVERS_PATH`
:   Set to a search path for VA-API drivers
    if `--runtime` and `--graphics-provider` are active.

`PATH`
:   Reset to a reasonable value if `--runtime` is active.

`PULSE_CLIENTCONFIG`
:   Set to the address of a PulseAudio client configuration file.

`PULSE_SERVER`
:   Set to the address of a PulseAudio server socket.

`PWD`
:   Set to the current working directory inside the container.

`STEAM_RUNTIME`
:   Set to `/` if using the Steam Runtime 1 'scout' runtime.

`TERMINFO_DIRS`
:   Set to the required search path for **terminfo**(5) files if
    the `--runtime` appears to be Debian-based.

`VDPAU_DRIVER_PATH`
:   Set to a search path for VDPAU drivers
    if `--runtime` and `--graphics-provider` are active.

`VK_ICD_FILENAMES`
:   Set to a search path for Vulkan ICDs
    if `--runtime` and `--graphics-provider` are active.

`VK_LAYER_PATH`
:   Unset if `--runtime` and `--graphics-provider` are active.

`XAUTHORITY`
:   Set to a value corresponding to a file in the container's
    private `/run`.

`XDG_CACHE_HOME`
:   Set to `$HOME/.cache` (in the private home directory)
    if `--unshare-home` is active.

`XDG_CONFIG_HOME`
:   Set to `$HOME/.config` (in the private home directory)
    if `--unshare-home` is active.

`XDG_DATA_HOME`
:   Set to `$HOME/.local/share` (in the private home directory)
    if `--unshare-home` is active.

`XDG_DATA_DIRS`
:   Set to include a search path for Vulkan layers
    if `--runtime` and `--graphics-provider` are active.

`XDG_RUNTIME_DIR`
:   Set to a new directory in the container's private `/run`
    if `--runtime` is active.

# OUTPUT

The standard output from *COMMAND* is printed on standard output.

The standard error from *COMMAND* is printed on standard error.
Diagnostic messages from **pressure-vessel-wrap** and
**pressure-vessel-wrap** may also be printed on standard error.

# SIGNALS

The **pressure-vessel-wrap** process replaces itself with a **bwrap**(1)
process. Fatal signals to the resulting **bwrap**(1) process will result
in `SIGTERM` being received by the **pressure-vessel-wrap** process
that runs *COMMAND* inside the container.

# EXIT STATUS

Nonzero (failure) exit statuses are subject to change, and might be
changed to be more like **env**(1) in future.

0
:   The *COMMAND* exited successfully with status 0

Assorted nonzero statuses
:   An error occurred while setting up the execution environment or
    starting the *COMMAND*

Any value 1-255
:   The *COMMAND* exited unsuccessfully with the status indicated

128 + *n*
:   The *COMMAND* was killed by signal *n*
    (this is the same encoding used by **bash**(1), **bwrap**(1) and
    **env**(1))

255
:   The *COMMAND* terminated in an unknown way (neither a normal exit
    nor terminated by a signal).

# EXAMPLE

In this example we install and run a small free game that does not
require communication or integration with Steam, without going via
Steam to launch it. This will only work for simple games without DRM
or significant Steam integration.

    $ steam steam://install/1070560     # Steam Linux Runtime 'scout'
    $ steam steam://install/302380      # Floating Point, a small free game
    $ rm -fr ~/tmp/pressure-vessel-var
    $ mkdir -p ~/tmp/pressure-vessel-var
    $ archive=com.valvesoftware.SteamRuntime.Platform-amd64,i386-scout-runtime.tar.gz
    $ cd ~/.steam/steam/steamapps/common/"Floating Point"
    $ /path/to/pressure-vessel/bin/pressure-vessel-wrap \
        --runtime-archive ~/.steam/steamapps/common/SteamLinuxRuntime/"$archive" \
        --variable-dir ~/tmp/pressure-vessel-var \
        --shell=instead \
        -- \
        "./Floating Point.x86"

In the resulting **xterm**(1), you can explore the container interactively,
then type `"$@"` (including the double quotes) to run the game itself.

For more joined-up integration with Steam, install the Steam Linux Runtime
(`steam://install/1070560`), and configure a native Linux game in Steam
to be run with the `Steam Linux Runtime` "compatibility tool".

<!-- vim:set sw=4 sts=4 et: -->
