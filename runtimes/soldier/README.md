Container based Steam Runtime
=============================

This container-based release of the Steam Runtime is used for Proton 5.13+.

Known issues
------------

Please see
https://github.com/ValveSoftware/steam-runtime/blob/master/doc/steamlinuxruntime-known-issues.md

Reporting bugs
--------------

Please see
https://github.com/ValveSoftware/steam-runtime/blob/master/doc/reporting-steamlinuxruntime-bugs.md

Development and debugging
-------------------------

The runtime's behaviour can be changed by running the Steam client with
environment variables set.

`PRESSURE_VESSEL_WRAP_GUI=1` will
spawn a 'test mode' dialog allowing you to select a different runtime
and set various options. This developer tool requires Python 3, PyGI,
GTK 3 and the GTK 3 GObject-Introspection bindings
(`apt install python3-gi gir1.2-gtk-3.0` on Debian-derived
distributions like Ubuntu and SteamOS).

Using that test mode, it is possible to switch from the normal Platform
runtime to a SDK runtime with more debugging tools available:

* In <https://repo.steampowered.com/steamrt-images-soldier/snapshots/>,
    browse to the directory corresponding to the version number listed
    in VERSIONS.txt.

* Download the files
    `com.valvesoftware.SteamRuntime.Sdk-amd64,i386-soldier-buildid.txt`
    and
    `com.valvesoftware.SteamRuntime.Sdk-amd64,i386-soldier-runtime.tar.gz`.

* Use the test mode (see above) to select the SDK runtime.

Some more advanced environment variables (subject to change):

* `PRESSURE_VESSEL_SHELL=instead` runs an interactive shell in the
    container instead of running the game.

* `STEAM_COMPAT_FORCE_SESSIONS` forces session mode when the compat tool
    is used.

* See the pressure-vessel source code for more.

Licensing and copyright
-----------------------

The Steam Runtime contains many third-party software packages under
various open-source licenses.

For full source code, please see the version-numbered subdirectories of
<https://repo.steampowered.com/steamrt-images-scout/snapshots/> and
<https://repo.steampowered.com/steamrt-images-soldier/snapshots/>
corresponding to the version numbers listed in VERSIONS.txt.
