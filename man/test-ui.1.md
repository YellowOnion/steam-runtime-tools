---
title: pressure-vessel-test-ui
section: 1
...

# NAME

pressure-vessel-test-ui - testing/development UI for pressure-vessel

# SYNOPSIS

**pressure-vessel-test-ui**
[*OPTIONS*]
[**--**]
*COMMAND* [*ARGUMENTS...*]

# DESCRIPTION

**pressure-vessel-test-ui** runs **pressure-vessel-wrap** with an
interactive GUI to modify settings.

# OPTIONS

All options are passed directly to **pressure-vessel-wrap**(1), and
override anything that is chosen in the GUI. For best results, don't
use any options.

# ENVIRONMENT

Various environment variables understood by **pressure-vessel-wrap**
are used to set default values for the GUI.

# DEPENDENCIES

**pressure-vessel-test-ui** requires Python 3.4 or later, a modern
version of PyGObject (PyGI, for example the `python3-gi` package in
Debian and Ubuntu), and GObject-Introspection data for GLib and GTK 3.

# EXIT STATUS

The same as **pressure-vessel-wrap**, or nonzero for an internal error
in **pressure-vessel-test-ui**.

# EXAMPLE

Run Steam with `PRESSURE_VESSEL_WRAP_GUI=1` in the environment.
Install the Steam Linux Runtime (`steam://install/1070560`), configure
a native Linux game in Steam to be run with the `Steam Linux Runtime`
"compatibility tool", and launch it.

<!-- vim:set sw=4 sts=4 et: -->
