Steam Runtime debugging (SDK) environment
=========================================

SDK container
-------------

The default `scout/` directory is a version of
`com.valvesoftware.SteamRuntime.Platform-amd64,i386-scout-runtime.tar.gz`
from <http://repo.steampowered.com/steamrt-images-scout/snapshots/>
(see the build ID in its `metadata` file to find out which one).

To get a debug version of the Steam Runtime container, download the
corresponding
`com.valvesoftware.SteamRuntime.Sdk-amd64,i386-scout-runtime.tar.gz`
and unpack it into this directory, so that you have this layout:

    pressure-vessel/
        ...
    scout/
        ...
    scout_sdk/
        README.md (this file)
        metadata
        files/
            bin/
                ...
            lib/
                ...
            ...

Then use the developer mode to select the SDK runtime instead of the
default Platform runtime.

Older and newer containers
--------------------------

To test beta versions or debug regressions,
you can download any recent version of
`com.valvesoftware.SteamRuntime.Platform-amd64,i386-scout-runtime.tar.gz`
or `com.valvesoftware.SteamRuntime.Sdk-amd64,i386-scout-runtime.tar.gz`
from <http://repo.steampowered.com/steamrt-images-scout/snapshots/>
and unpack it into a similar layout, for example:

    pressure-vessel/
        ...
    scout/
        ...
    scout_sdk/
        ...
    scout_platform_0.20190926.0/
        metadata
        files/
            bin/
                ...
            lib/
                ...
            ...
    scout_sdk_0.20190927.0/
        metadata
        files/
            bin/
                ...
            lib/
                ...
            ...

Steam Runtime schroot
---------------------

For a fully modifiable version, which requires `sudo` privileges on a
host operating system with the `schroot` tool:

* Download one of the
  `com.valvesoftware.SteamRuntime.Sdk-amd64,i386-scout-sysroot.tar.gz`
  files from
  <http://repo.steampowered.com/steamrt-images-scout/snapshots/>

* Clone https://github.com/ValveSoftware/steam-runtime somewhere convenient

* Import the sysroot tarball as a chroot:

    ```
    cd steam-runtime
    ./setup_chroot.sh --amd64 --tarball ../com.valvesoftware...-sysroot.tar.gz
    ```

* Use `sudo schroot -c steamrt_scout_amd64 -- apt-get install ...` to
    install any additional software you need

* Create a `scout_schroot` directory next to `scout` and `pressure-vessel`:

    ```
    mkdir scout_schroot
    ```

* Create a symbolic link to the chroot and name it `scout_schroot/files`:

    ```
    ln -s /var/chroots/steamrt_scout_amd64 scout_schroot/files
    ```

* The `scout_schroot` directory should now be available in developer mode
