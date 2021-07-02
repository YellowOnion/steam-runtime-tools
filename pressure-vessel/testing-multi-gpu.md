Testing pressure-vessel on multiple-GPU systems
===============================================

<!-- This document:
Copyright © 2021 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

Multiple GPU support on Vulkan is complicated, and it often isn't obvious
which part of the stack has a bug. This document describes how you can
test various scenarios, to help to isolate which part of the stack we
should be looking at.

Prerequisites
-------------

### Host system

Install appropriate graphics drivers on the host system.

Install Steam on the host system.

### Flatpak (optional)

If you will be testing Flatpak, also
[install Flatpak](https://flatpak.org/setup/), then use it to install
the `vulkaninfo` tool and the
[Steam Flatpak app](https://flathub.org/apps/details/com.valvesoftware.Steam).
If you want to install these for all users, remove `--user` from the
commands and run them as a root-equivalent user.

```
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub com.valvesoftware.Steam
flatpak install --user flathub org.freedesktop.Platform.VulkanInfo
```

If you want to share a Steam library between the copy of Steam on the
host system and the copy of Steam in Flatpak, you can achieve that
like this:

* Run Steam on the host system and log in

* [Create a new Steam library folder](https://support.steampowered.com/kb_article.php?ref=7418-YUBN-8129)
    for example in `/home/you/SteamLibrary` or
    `/media/somedisk/SteamLibrary`. For best results put it below
    `/home`, `/srv`, `/media` or `/run/media`.
    Do not put it below `/usr`, `~/.local/share/Steam` or `~/.steam`.
    Set it as the default, if you want.

* At this point you can use Steam's UI to move installed games, Proton
    and/or Steam Linux Runtime installations to the new Steam library.

* Completely exit from the host version of Steam.

* Configure Flatpak to share that folder with the host system, for example:

    ```
    flatpak override --filesystem=/home/you/SteamLibrary com.valvesoftware.Steam
    ```

    Do not try to share your whole home directory with the Flatpak app:
    that won't work and can cause damage.

* Run the Flatpak version of Steam and log in:

    ```
    flatpak run com.valvesoftware.Steam
    ```

* Add the same library folder in the Flatpak version of Steam.
    Games and runtimes that you moved into it should appear.

* Completely exit from the Flatpak version of Steam.

### Steam games and runtimes

Have at least one suitable game available in your library.
[Life is Strange, episode 1](https://store.steampowered.com/app/319630/Life_is_Strange__Episode_1/)
is a game we often test with, for several reasons: it's not too big,
it will work on relatively old hardware, the first episode is free (so
everyone can try the same game), it's single-player (so there's no
anti-cheat mechanism interfering with attempts to debug it), and it has
a native Linux port that can be used to compare native Linux with Proton.

Install the game.

If it's a native Linux game, right-click on the game in your Steam
library, open its Properties and select the "Steam Linux Runtime"
compatibility tool. Wait for it to install.

Change the compatibility tool again, and select "Proton 5.13-6" (or if
a newer version is available, use that). Wait for it to install.
"Steam Linux Runtime - soldier" will also be installed as a side-effect.

Testing the host system
-----------------------

If your operating system has
[vulkaninfo](https://github.com/KhronosGroup/Vulkan-Tools) available,
install and run it. It will often be in a package named `vulkan-tools`
or `vulkaninfo`. Save its output to a file:

```
vulkaninfo > ~/tmp/vulkaninfo.txt 2>&1
```

Testing Steam on the host system (no workaround)
------------------------------------------------

Run Steam (on the host system) with the environment variables
`STEAM_LINUX_RUNTIME_LOG=1` and `STEAM_LINUX_RUNTIME_VERBOSE=1`:

```
STEAM_LINUX_RUNTIME_LOG=1 STEAM_LINUX_RUNTIME_VERBOSE=1 steam
```

Open the Help -> System Information window and wait a while for the
full report to appear. Copy the report and paste it into a text file
`sysinfo-host.txt`.

In your Steam library, you should find log files with symbolic links
`steamapps/common/SteamLinuxRuntime/var/slr-latest.log` and
`steamapps/common/SteamLinuxRuntime_soldier/var/slr-latest.log` pointing
to the latest log file for each runtime. The real names of the files they
point to should start with `slr-non-steam-game-`. Copy those log files as
`slr-scout-host-srsi.txt` and `slr-soldier-host-srsi.txt`.

For each game you want to test:

* For native Linux games only:
    in its Properties, turn off the compatibility tool to get a baseline.
    Try running the game, and note whether it runs on the GPU that
    you expected.

* For native Linux games only:
    in its Properties, set the compatibility tool to "Steam Linux Runtime".
    Try running the game, and note whether it runs on the GPU that you
    expected. Copy the log file from
    `steamapps/common/SteamLinuxRuntime/var/slr-latest.log`
    (the real filename will start with `slr-app319630-` or similar).

* In its Properties, set the compatibility tool to Proton 5.0 or older
    to get a baseline for Proton. Try running the game, and note whether
    it runs on the GPU that you expected.

* In its Properties, set the compatibility tool to "Proton 5.13-6"
    (or whatever is the current version). Try running the game, and note
    whether it runs on the GPU that you expected. Copy the log file from
    `steamapps/common/SteamLinuxRuntime_soldier/var/slr-latest.log`
    (the real filename will start with `slr-app319630-` or similar).

Testing Steam on the host system (forcing GPU)
----------------------------------------------

If your main GPU uses the NVIDIA proprietary drivers, repeat the previous
section, but run Steam with additional environment variables
`__NV_PRIME_RENDER_OFFLOAD=1`, `__VK_LAYER_NV_optimus=NVIDIA_only`
and `__GLX_VENDOR_LIBRARY_NAME=nvidia`:

```
__NV_PRIME_RENDER_OFFLOAD=1 \
__VK_LAYER_NV_optimus=NVIDIA_only \
__GLX_VENDOR_LIBRARY_NAME=nvidia \
STEAM_LINUX_RUNTIME_LOG=1 \
STEAM_LINUX_RUNTIME_VERBOSE=1 \
steam
```

Or if your main GPU uses Mesa, repeat the previous section, but run
Steam with `DRI_PRIME=1` instead:

```
DRI_PRIME=1 \
STEAM_LINUX_RUNTIME_LOG=1 \
STEAM_LINUX_RUNTIME_VERBOSE=1 \
steam
```

Save the logs as `sysinfo-host-forcing-gpu.txt` and so on.

Testing in Flatpak (no workaround)
----------------------------------

Run `vulkaninfo` in the Flatpak environment and save its output:

```
flatpak run org.freedesktop.Platform.VulkanInfo > ~/tmp/vulkaninfo-flatpak.txt
```

Run the Steam Flatpak app with the environment variables
`STEAM_LINUX_RUNTIME_LOG=1` and `STEAM_LINUX_RUNTIME_VERBOSE=1`:

```
STEAM_LINUX_RUNTIME_LOG=1 STEAM_LINUX_RUNTIME_VERBOSE=1 \
flatpak run com.valvesoftware.Steam
```

If you are using the branch of Flatpak that has new features needed by
pressure-vessel, also set the environment variable that makes
pressure-vessel use that mechanism (deliberately not documented here,
this branch is currently only suitable for people who are looking at
the source code of the relevant components).

Open the Help -> System Information window and wait a while for the
full report to appear. Copy the report and paste it into a text file
`sysinfo-flatpak.txt`.

If you are using the branch of Flatpak that has new features needed by
pressure-vessel, copy
`steamapps/common/SteamLinuxRuntime/var/slr-latest.log` and
`steamapps/common/SteamLinuxRuntime_soldier/var/slr-latest.log` as
`slr-scout-flatpak-srsi.txt` and `slr-soldier-flatpak-srsi.txt`.

For each game you want to test:

* For native Linux games only:
    in its Properties, turn off the compatibility tool. Try running the game,
    and note whether it runs on the GPU that you expected.

* In its Properties, set the compatibility tool to Proton 5.0 or older,
    or a Flatpak community build of Proton 5.13.
    Try running the game, and note whether it runs on
    the GPU that you expected.

* For native Linux games only:
    if you are using the branch of Flatpak that has the new features needed
    by pressure-vessel, also try "Steam Linux Runtime". Copy
    `steamapps/common/SteamLinuxRuntime/var/slr-latest.log`.

* If you are using the branch of Flatpak that has the new features needed
    by pressure-vessel, also try the official Proton 5.13 build. Copy
    `steamapps/common/SteamLinuxRuntime_soldier/var/slr-latest.log`.

Testing in Flatpak (forcing GPU and working around missing implicit layers)
---------------------------------------------------------------------------

Copy `/usr/share/vulkan/implicit_layer.d/VkLayer_MESA_device_select.json`
into
`~/.var/app/com.valvesoftware.Steam/.local/share/vulkan/implicit_layer.d/`.
Remember that you have done this, so that you can undo the workaround
when it becomes unnecessary (see
<https://gitlab.com/freedesktop-sdk/freedesktop-sdk/-/merge_requests/4723>
and <https://github.com/flathub/org.freedesktop.Platform.GL.nvidia/pull/58>).

If you use the NVIDIA proprietary drivers, do the same with
`/usr/share/vulkan/implicit_layer.d/nvidia_layers.json`.

If your main GPU uses the NVIDIA proprietary drivers, repeat the previous
section, but run both vulkaninfo and Steam with additional environment
variables
`__NV_PRIME_RENDER_OFFLOAD=1`, `__VK_LAYER_NV_optimus=NVIDIA_only`
and `__GLX_VENDOR_LIBRARY_NAME=nvidia`:

```
__NV_PRIME_RENDER_OFFLOAD=1 \
__VK_LAYER_NV_optimus=NVIDIA_only \
__GLX_VENDOR_LIBRARY_NAME=nvidia \
STEAM_LINUX_RUNTIME_LOG=1 \
STEAM_LINUX_RUNTIME_VERBOSE=1 \
flatpak run org.freedesktop.Platform.VulkanInfo \
> ~/tmp/vulkaninfo-flatpak-workarounds.txt

__NV_PRIME_RENDER_OFFLOAD=1 \
__VK_LAYER_NV_optimus=NVIDIA_only \
__GLX_VENDOR_LIBRARY_NAME=nvidia \
STEAM_LINUX_RUNTIME_LOG=1 \
STEAM_LINUX_RUNTIME_VERBOSE=1 \
flatpak run com.valvesoftware.Steam
```

Or if your main GPU uses Mesa, repeat the previous section, but run
Steam with `DRI_PRIME=1` instead:

```
DRI_PRIME=1 \
STEAM_LINUX_RUNTIME_LOG=1 \
STEAM_LINUX_RUNTIME_VERBOSE=1 \
flatpak run org.freedesktop.Platform.VulkanInfo \
> ~/tmp/vulkaninfo-flatpak-workarounds.txt

DRI_PRIME=1 \
STEAM_LINUX_RUNTIME_LOG=1 \
STEAM_LINUX_RUNTIME_VERBOSE=1 \
flatpak run com.valvesoftware.Steam
```

Save the log files as `sysinfo-flatpak-workarounds.txt` and so on.
