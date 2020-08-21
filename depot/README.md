Container based Steam Runtime
=============================

This experimental container-based release of the Steam Runtime
is enabled on a per-title basis by forcing its use in the title's
Properties dialog.

Its behaviour can be changed by running the Steam client with environment
variables set:

* `PRESSURE_VESSEL_WRAP_GUI=1` or `STEAM_RUNTIME_CONTAINER_TEST=1` will
    spawn a 'test mode' dialog allowing you to select a different runtime
    and set various options. This developer tool requires Python 3, PyGI,
    GTK 3 and the GTK 3 GObject-Introspection bindings
    (`apt install python3-gi gir1.2-gtk-3.0` on Debian-derived
    distributions like Ubuntu and SteamOS).

Some more advanced environment variables (subject to change):

* `PRESSURE_VESSEL_RUNTIME=scout_sdk/files` uses a SDK version of the
    runtime with extra debugging tools. This needs some setup to be done
    first: see scout_sdk/README.md for details.

* `PRESSURE_VESSEL_SHELL=instead` runs an interactive shell in the
    container instead of running the game.

* `STEAM_COMPAT_FORCE_SESSIONS` forces session mode when the compat tool
    is used.

* See the pressure-vessel source code for more.

