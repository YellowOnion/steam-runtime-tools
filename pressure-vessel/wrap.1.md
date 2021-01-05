---
title: pressure-vessel-wrap
section: 1
...

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

`--copy-runtime-into` *DIR*
:   If a `--runtime` is active, copy it into a subdirectory of *DIR*,
    edit the copy in-place, and mount the copy read-only in the container,
    instead of setting up elaborate bind-mount structures.

`--env-if-host` *VAR=VAL*
:   If *COMMAND* is run with `/usr` from the host system, set
    environment variable *VAR* to *VAL*. If not, leave *VAR* unchanged.

`--filesystem` *PATH*
:   Share *PATH* from the host system with the container.

`--freedesktop-app-id` *ID*
:   If using `--unshare-home`, use *~/.var/app/ID* as the home
    directory. This is the same home directory that a Flatpak app for
    freedesktop.org app *ID* would use.

`--gc-runtimes`, `--no-gc-runtimes`
:   If using `--copy-runtime-into`, garbage-collect old temporary
    runtimes that are left over from a previous **pressure-vessel-wrap**.
    This is the default. `--no-gc-runtimes` disables this behaviour.

`--generate-locales`, `--no-generate-locales`
:   Passed to **pressure-vessel-wrap**(1).
    The default is `--generate-locales`, overriding the default
    behaviour of **pressure-vessel-wrap**(1).

`--home` *DIR*
:   Use *DIR* as the home directory. This implies `--unshare-home`.

`--host-ld-preload` *MODULE*
:   Add *MODULE* from the host system to `LD_PRELOAD` when executing
    *COMMAND*. If *COMMAND* is run in a container, the path of the
    *MODULE* will be adjusted appropriately.

`--import-vulkan-layers`, `--no-import-vulkan-layers`
:   If `--no-import-vulkan-layers` is specified, the Vulkan layers will
    not be imported from the host system. Please note that some layers might
    still be reachable from inside the container. E.g. layers located in
    `~/.local/share/vulkan` if used in combination with `--share-home`.
    The default is `--import-vulkan-layers`.

`--keep-game-overlay`, `--remove-game-overlay`
:   If `--remove-game-overlay` is specified, remove the Steam Overlay
    from the `LD_PRELOAD`. The default is `--keep-game-overlay`.

`--launcher`
:   Instead of specifying a command with its arguments to execute, all the
    elements after `--` will be used as arguments for
    `pressure-vessel-launcher`. This option implies `--batch`.

`--only-prepare`
:   Prepare the runtime, but do not actually run *COMMAND*.
    With `--copy-runtime-into`, the prepared runtime will appear in
    a subdirectory of *DIR*.

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
    If *PATH*/usr exists, *PATH* is assumed to be a complete sysroot
    (containing bin/sh, usr/bin/env and many other OS files).
    Otherwise, *PATH* is assumed to be a merged-/usr environment
    (containing bin/sh, bin/env and many other OS files).
    For example, the `files` subdirectory of a Flatpak runtime is a
    suitable value for *PATH*.

`--runtime-base` *PATH*
:   If `--runtime` specifies a relative path, look for it relative
    to *PATH*.

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
:   Start an `xterm`(1) inside the container.

`--terminate-timeout` *SECONDS*, `--terminate-idle-timeout` *SECONDS*
:   Passed to **pressure-vessel-wrap**(1).

`--test`
:   Perform a smoke-test to determine whether **pressure-vessel-wrap**
    can work, and exit. Exit with status 0 if it can or 1 if it cannot.

`--verbose`
:   Be more verbose.

`--version`
:   Print the version number and exit.

`--with-host-graphics`, `--without-host-graphics`
:   If using a `--runtime`, either import the host system's
    graphics stack into it (default), or use the runtime's graphics
    stack if any (likely to result in slow rendering or a crash).

# ENVIRONMENT

The following environment variables (among others) are read by
**pressure-vessel-wrap**(1).

`__EGL_VENDOR_LIBRARY_DIRS`, `__EGL_VENDOR_LIBRARY_FILENAMES`
:   Used to locate EGL ICDs to be made available in the container
    if `--runtime` and `--with-host-graphics` are active.

`BWRAP` (path)
:   Absolute path to **bwrap**(1).
    The default is to try several likely locations.

`DBUS_SESSION_BUS_ADDRESS`, `DBUS_SYSTEM_BUS_ADDRESS`
:   Used to locate the well-known D-Bus session and system buses
    so that they can be made available in the container.

`DISPLAY`
:   Used to locate the X11 display to make available in the container.

`LIBGL_DRIVERS_PATH`
:   Used to locate Mesa DRI drivers to be made available in the container
    if `--runtime` and `--with-host-graphics` are active.

`LIBVA_DRIVERS_PATH`
:   Used to locate VA-API drivers to be made available in the container
    if `--runtime` and `--with-host-graphics` are active.

`PRESSURE_VESSEL_BATCH` (boolean)
:   If set to `1`, equivalent to `--batch`.
    If set to `0`, no effect.

`PRESSURE_VESSEL_COPY_RUNTIME_INTO` (path or empty string)
:   Equivalent to
    `--copy-runtime-into="$PRESSURE_VESSEL_COPY_RUNTIME_INTO"`.

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

`PRESSURE_VESSEL_HOME` (path)
:   Equivalent to `--home="$PRESSURE_VESSEL_HOME"`.

`PRESSURE_VESSEL_HOST_GRAPHICS` (boolean)
:   If set to `1`, equivalent to `--with-host-graphics`.
    If set to `0`, equivalent to `--without-host-graphics`.

`PRESSURE_VESSEL_IMPORT_VULKAN_LAYERS` (boolean)
:   If set to `1`, equivalent to `--import-vulkan-layers`.
    If set to `0`, equivalent to `--no-import-vulkan-layers`.

`PRESSURE_VESSEL_LOG_INFO` (boolean)
:   If set to `1`, increase the log verbosity up to the info level.
    If set to `0`, no effect.

`PRESSURE_VESSEL_REMOVE_GAME_OVERLAY` (boolean)
:   If set to `1`, equivalent to `--remove-game-overlay`.
    If set to `0`, equivalent to `--keep-game-overlay`.

`PRESSURE_VESSEL_RUNTIME` (path, filename or empty string)
:   Equivalent to `--runtime="$PRESSURE_VESSEL_RUNTIME"`.

`PRESSURE_VESSEL_RUNTIME_BASE` (path, filename or empty string)
:   Equivalent to `--runtime-base="$PRESSURE_VESSEL_RUNTIME_BASE"`.

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
    is set. Use `pressure-vessel-unruntime`(1) instead.

`SteamAppId` (integer)
:   Equivalent to `--steam-app-id="$SteamAppId"`.
    Must only be set for the main processes that are running a game, not
    for any setup/installation steps that happen first.

`VDPAU_DRIVER_PATH`
:   Used to locate VDPAU drivers to be made available in the container
    if `--runtime` and `--with-host-graphics` are active.

`VK_ICD_FILENAMES`
:   Used to locate Vulkan ICDs to be made available in the container
    if `--runtime` and `--with-host-graphics` are active.

`WAYLAND_DISPLAY`
:   Used to locate the Wayland display to make available in the container.

The following environment variables are set by **pressure-vessel-wrap**(1).

`__EGL_VENDOR_LIBRARY_DIRS`
:   Unset if `--runtime` and `--with-host-graphics` are active,
    to make sure `__EGL_VENDOR_LIBRARY_FILENAMES` will be used instead.

`__EGL_VENDOR_LIBRARY_FILENAMES`
:   Set to a search path for EGL ICDs
    if `--runtime` and `--with-host-graphics` are active.

`DBUS_SESSION_BUS_ADDRESS`, `DBUS_SYSTEM_BUS_ADDRESS`
:   Set to paths in the container's private `/run` where the well-known
    D-Bus session and system buses are made available.

`DISPLAY`
:   Set to a value corresponding to the socket in the container's
    `/tmp/.X11-unix`.

`LD_LIBRARY_PATH`
:   Set to a search path for shared libraries if `--runtime` is active.

`LD_PRELOAD`
:   Set according to `--host-ld-preload`, `--keep-game-overlay`,
    `--remove-game-overlay`.

`LIBGL_DRIVERS_PATH`
:   Set to a search path for Mesa DRI drivers
    if `--runtime` and `--with-host-graphics` are active.

`LIBVA_DRIVERS_PATH`
:   Set to a search path for VA-API drivers
    if `--runtime` and `--with-host-graphics` are active.

`PATH`
:   Reset to a reasonable value if `--runtime` is active.

`PULSE_CLIENTCONFIG`
:   Set to the address of a PulseAudio client configuration file.

`PULSE_SERVER`
:   Set to the address of a PulseAudio server socket.

`PWD`
:   Set to the current working directory inside the container.

`VDPAU_DRIVER_PATH`
:   Set to a search path for VDPAU drivers
    if `--runtime` and `--with-host-graphics` are active.

`VK_ICD_FILENAMES`
:   Set to a search path for Vulkan ICDs
    if `--runtime` and `--with-host-graphics` are active.

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
    $ rm -fr ~/tmp/scout
    $ mkdir -p ~/tmp/scout
    $ tar \
        -C ~/tmp/scout \
        -xzvf ~/.steam/steamapps/common/SteamLinuxRuntime/com.valvesoftware.SteamRuntime.Platform-amd64,i386-scout-runtime.tar.gz
    $ cd ~/.steam/steam/steamapps/common/"Floating Point"
    $ /path/to/pressure-vessel/bin/pressure-vessel-wrap \
        --runtime ~/tmp/scout/files \
        --shell=instead \
        -- \
        "./Floating Point.x86"

In the resulting `xterm`(1), you can explore the container interactively,
then type `"$@"` to run the game itself.

For more joined-up integration with Steam, install the Steam Linux Runtime
(`steam://install/1070560`), and configure a native Linux game in Steam
to be run with the `Steam Linux Runtime` "compatibility tool".

<!-- vim:set sw=4 sts=4 et: -->
