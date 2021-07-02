---
title: steam-runtime-input-monitor
section: 1
...

<!-- This document:
Copyright 2020 Collabora Ltd.
SPDX-License-Identifier: MIT
-->

# NAME

steam-runtime-input-monitor - list input devices

# SYNOPSIS

**steam-runtime-input-monitor** [*OPTIONS*]

# DESCRIPTION

**steam-runtime-input-monitor** lists input devices.

# OPTIONS

**--direct**
:   List input devices by reading `/dev` and `/sys`.
    This currently only lists input devices for which at least read access
    to the underlying device node is available.

**--evdev**
:   List all input event devices.

**--hidraw**
:   List all raw HID devices.

**--once**
:   Discover the available input devices, print their details and exit.

**--one-line**
:   Print one event per line ("newline-delimited JSON", unless **--seq**
    is also used). By default they are pretty-printed with
    readable indentation, which is convenient for a human reader but
    less suitable for machine-readable use.

**--seq**
:   Print events in **application/json-seq** format (RFC 7464):
    each event is printed as U+001E RECORD SEPARATOR, followed by
    a JSON object, followed by U+000A LINE FEED.

**--udev**
:   List input devices by contacting udevd. This is usually not
    expected to work inside a container.

**--version**
:   Print the version number as YAML and exit.

# OUTPUT

**steam-runtime-input-monitor** prints one or more *events* on
standard output, in JSON format. The precise format is determined
by the **--one-line** and **--seq** options.

By default, all known input devices are listed. Either
**--evdev** or **--hidraw** can be used to request only a subset of
input devices. For convenience, if no options requesting a subset of
input devices are given, this is treated as equivalent to using options
that request *all* input devices.

Options to limit devices more selectively (for example, only evdev
devices that have the **ID_INPUT_JOYSTICK** udev property) are likely
to be added in future versions.

By default, an implementation is chosen automatically. The **--direct**
and **--udev** options override this, with the last one used
taking precedence.

Each event is a JSON object with a single key. Consumers should ignore
unknown keys to allow for future expansion.

**all-for-now**
:   The initial device enumeration has finished. If the **--once** option
    was used, **steam-runtime-input-monitor** will exit. If not, it will
    contine to monitor device hotplug events.

    The value is **true**.

**added**
:   A device was added. The value is an object describing the device,
    with the following keys and values:

    **dev_node**
    :   The device node in `/dev` for this device.

    **sys_path**
    :   The device directory in `/sys` for this device.

    **subsystem**
    :   The Linux kernel subsystem, either **input** or **hidraw**.

    Additional keys and values are likely to be added in future versions.

**removed**
:   A device was removed. The value is an object with keys **dev_node**
    and **sys_path**, as above.

# EXIT STATUS

0
:   Success.

Non-zero
:   A fatal error occurred.

<!-- vim:set sw=4 sts=4 et: -->
