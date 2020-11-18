---
title: steam-runtime-system-info
section: 1
...

# NAME

steam-runtime-system-info - examine the Steam runtime environment and diagnose potential issues

# SYNOPSIS

**steam-runtime-system-info**
[**--expectations** *PATH*]
[**--verbose**]

# DESCRIPTION

# OPTIONS

**--expectations** *PATH*
:   Path to a directory containing details of the libraries that are
    expected to be available. By default, *$STEAM_RUNTIME***/usr/lib/steamrt**
    or **/usr/lib/steamrt** is used.

**--verbose**
:   Show additional information. This currently adds details of all the
    expected libraries that loaded successfully.

# OUTPUT

The output is a JSON *object* (mapping, dictionary) with the following
keys:

**can-write-uinput**
:   A boolean value indicating whether **/dev/uinput** can be opened for
    writing. If this is **false**, the Steam Controller will not be able
    to emulate a keyboard, a mouse or a different game controller.

**steam-installation**
:   An object describing the Steam installation. The keys are strings:

    **path**
    :   A string: the absolute path to the Steam installation, typically
        containing **steam.sh** and **ubuntu12_32** among others.
        This is usually the same as **data_path** but can differ under some
        circumstances.

    **data_path**
    :   A string: the absolute path to the Steam data directory, typically
        containing **appcache**, **steamapps** and **userdata** among others.
        This is usually the same as **path** but can differ under some
        circumstances.

    **bin32_path**
    :   A string: the absolute path to the Steam "ubuntu12_32" directory,
        typically containing **steam-runtime** among others.
        This is usually a direct subdirectory of **path** but can differ under
        some circumstances.

    **steamscript_path**
    :   A string: if **steam-runtime-system-info** was run by Steam,
        the absolute path to the "bootstrapper" script used to launch
        Steam, typically **/usr/bin/steam** or **/usr/games/steam**.
        Otherwise, or if unavailable, it is **null**.

    **steamscript_version**
    :   A string: if **steam-runtime-system-info** was run by Steam,
        the version of the "bootstrapper" script used to launch
        Steam, for example **1.0.0.66** or **1.0.0.66-2/Debian**.
        Otherwise, or if unavailable, it is **null**.

    **issues**
    :   An array of strings representing problems with the Steam
        installation. If empty, no problems were found.

        **internal-error**
        :   An error occurred while checking the Steam installation.

        **cannot-find**
        :   The Steam installation could not be found.

        **cannot-find-data**
        :   The Steam data directory could not be found.

        **dot-steam-root-not-directory**
        :   **~/.steam/root** is not a (symbolic link to a) directory.
            Steam will probably not work.

        **dot-steam-root-not-symlink**
        :   **~/.steam/root** is not a symbolic link to the installation
            directory. Steam will probably not work.

        **dot-steam-steam-not-directory**
        :   **~/.steam/steam** is not a (symbolic link to a) directory.
            Steam will probably not work.

        **dot-steam-steam-not-symlink**
        :   **~/.steam/steam** is not a symbolic link to the data directory,
            for example due to
            [Debian bug #916303](https://bugs.debian.org/916303).

**runtime**
:   An object describing the `LD_LIBRARY_PATH`-based Steam Runtime.
    The keys are strings:

    **path**
    :   A string: the absolute path to the Steam Runtime.

    **version**
    :   The version number of the Steam Runtime, for example
        `0.20190711.3`.

    **issues**
    :   An array of strings representing problems with the Steam
        Runtime. If empty, no problems were found.

        **internal-error**
        :   An error occurred while checking the Steam Runtime.

        **disabled**
        :   The Steam Runtime has been explicitly disabled. This is
            intended for use by game and Steam Runtime developers, and
            is not supported.

        **not-runtime**
        :   The **STEAM_RUNTIME** environment variable does not point
            to a directory that looks like a Steam Runtime.

        **unofficial**
        :   The **STEAM_RUNTIME** environment variable points to an
            unofficial Steam Runtime build.

        **unexpected-location**
        :   The **STEAM_RUNTIME** environment variable points to a Steam
            Runtime that is not in the expected location in the Steam
            installation.

        **unexpected-version**
        :   The **STEAM_RUNTIME** environment variable points to a Steam
            Runtime that was not the expected version.

        **not-in-ld-path**
        :   The Steam Runtime does not appear in the **LD_LIBRARY_PATH**.
            Libraries from the Steam Runtime will not be found correctly.

        **not-in-path**
        :   The Steam Runtime does not appear in the **PATH**.
            Executables from the Steam Runtime will not be found correctly.

        **not-in-environment**
        :   The **STEAM_RUNTIME** environment variable is not set.

        **not-using-newer-host-libraries**
        :   "Pinned" shared libraries from the host system are not in
            their intended places in the **LD_LIBRARY_PATH**. This is
            likely to break the use of libraries from the host system,
            in particular the Mesa graphics drivers.

    **overrides**
    :   An object representing the libraries that have been
        overridden when the Steam Runtime is running in a
        `pressure-vessel` container.
        These libraries are taken from the host system, instead of being
        taken from the container like all other libraries.

        **list**
        :   A human-readable array of overridden libraries.
            It is currently in `find -ls` format.

        **messages**
        :   A human-readable array of errors and warning messages about
            the overridden libraries.
            It is currently the output on standard error of `find -ls`.

    **pinned_libs_32**
    :   An object representing the %SRT_ABI_I386 libraries that have been
        "pinned" when the Steam Runtime is running in an
        `LD_LIBRARY_PATH`-based environment.
        These libraries are taken from the Steam Runtime, even if a library
        of the same SONAME exists on the host system.

        **list**
        :   A human-readable array of %SRT_ABI_I386 "pinned" libraries.
            It is currently in `find -ls` format.

        **messages**
        :   A human-readable array of errors and warning messages about the
            %SRT_ABI_I386 "pinned" libraries.
            It is currently the output on standard error of `find -ls`.

    **pinned_libs_64**
    :   An object representing the %SRT_ABI_X86_64 libraries that have been
        "pinned" when the Steam Runtime is running in an
        `LD_LIBRARY_PATH`-based environment.
        These libraries are taken from the Steam Runtime, even if a library
        of the same SONAME exists on the host system.

        **list**
        :   A human-readable array of "pinned" %SRT_ABI_X86_64 libraries.
            It is currently in `find -ls` format.

        **messages**
        :   A human-readable array of errors and warning messages about the
            %SRT_ABI_X86_64 "pinned" libraries.
            It is currently the output on standard error of `find -ls`.

**os-release**
:   An object describing the operating system or container in which
    **steam-runtime-system-info** was run. The keys and values are
    strings, and any field that is not known is omitted.

    **id**
    :   A short lower-case string identifying the operating system,
        for example **debian** or **arch**. In a Steam Runtime
        chroot or container this will be **steamrt**.

        This is the **ID** from **os-release**(5).
        If **os-release**(5) is not available, future versions of this
        library might derive a similar ID from **lsb_release**(1).

    **id_like**
    :   An array of short lower-case strings identifying operating systems
        that this one was derived from. For example, Steam Runtime 1 'scout'
        is derived from Ubuntu, which is itself derived from Debian, so
        in a scout chroot or container this will be **["ubuntu", "debian"]**.

        This is the **ID_LIKE** field from **os-release**(5), split
        at whitespace. It does not include the **id**.

        If **os-release**(5) is not available, future versions of this
        library might derive a similar array from **lsb_release**(1)
        or from hard-coded knowledge of particular operating systems.

    **name**
    :   A human-readable name for the operating system without its
        version, for example **Debian GNU/Linux** or **Arch Linux**.

        This is the **NAME** from **os-release**(5).
        If **os-release**(5) is not available, future versions of this
        library might derive a similar name from **lsb_release**(1).

    **pretty_name**
    :   A human-readable name for the operating system, including its
        version if applicable, for example
        **Debian GNU/Linux 10 (buster)** or **Arch Linux**. For rolling
        releases like Debian testing/unstable and Arch Linux, this
        might be the same as **name**.

        This is the **PRETTY_NAME** from **os-release**(5).
        If **os-release**(5) is not available, future versions of this
        library might derive a similar name from **lsb_release**(1).

    **version_id**
    :   A short machine-readable string identifying the operating system,
        for example **10** for Debian 10 'buster'. In a
        Steam Runtime 1 'scout' chroot or container this will be **1**.
        In rolling releases like Debian testing/unstable and Arch Linux,
        this is likely to be omitted. It is not guaranteed to be numeric.

        This is the **VERSION_ID** from **os-release**(5).
        If **os-release**(5) is not available, future versions of this
        library might derive a similar ID from **lsb_release**(1).

    **version_codename**
    :   A short string identifying the operating system release codename,
        for example **buster** for Debian 10 'buster'. In a
        Steam Runtime 1 'scout' chroot or container this will be **scout**
        if present.

        In rolling releases like Debian testing/unstable and Arch Linux,
        and in operating systems like Fedora that have codenames but do
        not use them in a machine-readable context, this is likely to be
        omitted.

        This is the **VERSION_CODENAME** from **os-release**(5).
        If **os-release**(5) is not available, future versions of this
        library might derive a similar ID from **lsb_release**(1).

    **build_id**
    :   A short machine-readable string identifying the image from which
        the operating system was installed (depending on the operating
        system, it might have been upgraded since then). In operating
        systems with only non-image-based installation methods this will
        usually be omitted.

        In a Steam Runtime 1 'scout' chroot or container, if present this
        identifies the Steam Runtime build on which the chroot or container
        is based, such as **0.20190926.0** for an official build or
        **tools-test-0.20190926.0** for an unofficial build (but, again, it
        might have been upgraded since then).

        This is the **BUILD_ID** from **os-release**(5).

    **variant_id**
    :   A short machine-readable string identifying the variant or edition
        of the operating system, such as **workstation**.

        In a Steam Runtime 1 'scout' chroot or container built from a
        Flatpak-style runtime, this will currently be something like
        **com.valvesoftware.steamruntime.platform-amd64_i386-scout**.

        This is the **VARIANT_ID** from **os-release**(5).

    **variant**
    :   A human-readable string identifying the variant or edition
        of the operating system, such as **Workstation Edition**.

        In a Steam Runtime 1 'scout' chroot or container built from a
        Flatpak-style runtime, this will usually be **Platform** or **SDK**.

        This is the **VARIANT** from **os-release**(5).

**container**
:   Details of the container we are running in, if any.
    The keys are strings:

    **type**
    :   A short machine-readable string identifying the container type,
        such as **flatpak**, **pressure-vessel**, **docker**,
        **unknown** (if we appear to be in a container but the type is
        unknown), or **none** (if we do not appear to be in a container).

    **host**
    :   An object describing the host system. The keys are strings:

        **path**
        :   Absolute path to a directory where important files from the
            host system can be found, or **null** if unavailable.
            In Flatpak and pressure-vessel, this will be either **/run/host**
            or **null*.

        **os-release**
        :   The same as the **os-release** described above, but describing
            the host system rather than the container.

**driver_environment**
:   An array of strings, in the form "NAME=VALUE", with the well-known driver environment
    variables that are currently set.
    For example Mesa has `MESA_LOADER_DRIVER_OVERRIDE`, VA-API has
    `LIBVA_DRIVER_NAME` and so on.
    If empty, no environment variables related to driver selection have been found.

**architectures**
:   An object with architecture-specific information. The keys are
    Debian-style *multiarch tuples* describing ABIs, as returned by
    `gcc -print-multiarch`: the multiarch tuples normally used on x86 PCs
    are `x86_64-linux-gnu` for 64-bit binaries and `i386-linux-gnu` for
    32-bit binaries (the multiarch tuple contains i386 even if they were
    built for an i486, i586 or i686 baseline instruction set).

    The values are objects with the following keys:

    **can-run**
    :   A boolean value indicating whether a simple executable for this
        architecture was run successfully.

    **runtime-linker**
    :   If the multiarch tuple is a known one, an object with the
        following keys and values describing the runtime linker **ld.so**(8):

        **path**
        :   The path required to run Steam binaries on this architecture,
            for example `/lib64/ld-linux-x86-64.so.2` on x86_64

        **resolved**
        :   The result of resolving symbolic links in **path** if possible,
            for example `/lib/x86_64-linux-gnu/ld-2.28.so` on
            Debian 10 x86_64

        If the runtime linker is detected to be missing or unusable,
        details of the error appear here:

        **error-domain**
        :   A machine-readable string indicating a category of errors.

        **error-code**
        :   A small integer indicating a specific error. Its meaning depends
            on the **error-domain**.

        **error**
        :   A human-readable error message.

    **library-issues-summary**
    :   A summary of issues found when loading the expected libraries,
        as an array of strings. If empty, no problems were found.

        **internal-error**
        :   There was an internal error while checking libraries.

        **cannot-load**
        :   At least one of the expected libraries could not be loaded.

        **missing-symbols**
        :   At least one of the expected libraries did not contain all
            the symbols that were expected, indicating use of an
            incompatible version.

        **misversioned-symbols**
        :   At least one of the expected libraries did not contain all
            the symbol versioning that was expected, indicating use of an
            incompatible version.

        **unknown-expectations**
        :   Details of the expected libraries were not found.

    **library-details**
    :   An object representing shared libraries. If the **--verbose**
        option was used, all known libraries are listed here; if not,
        only the libraries with problems are listed.

        The keys are library SONAMEs such as **libc.so.6**. The values
        are objects with these string keys:

        **path**
        :   The absolute path to the library.

        **soname**
        :   The real SONAME of the library, which might be different from
            the name that was requested if it is a compatibility alias,
            or **null** if it could not be determined.

        **issues**
        :   Problems with the library, as arrays of the same strings
            described in **library-issues-summary** above.

        **missing-symbols**
        :   If the library has missing symbols they are listed here.

        **misversioned-symbols**
        :   If the library has missing versioned symbols, but the same
            symbol exists with an unexpected version or no version, they
            are listed here.

    **graphics-details**
    :   An object representing graphics stacks. The keys are strings
        such as **glx/gl**, **egl_x11/glesv2** or **x11/vulkan**
        representing the combination of a window system interface and a
        rendering interface.

        Known window system interfaces:

        **x11**
        :   The X11 windowing system for Vulkan

        **glx**
        :   GLX, the traditional OpenGL API for X11

        **egl_x11**
        :   EGL, a windowing system interface for either OpenGL or OpenGL ES,
            rendering to an X11 window

        Known rendering interfaces:

        **gl**
        :   "Desktop" OpenGL

        **glesv2**
        :   OpenGL ES v2

        **vulkan**
        :   Vulkan

        **vdpau**
        :   VDPAU

        **va-api**
        :   VA-API

        The values are objects with the following string keys:

        **renderer**
        :   The renderer string describing the graphics device and/or driver,
            or **null** if it could not be determined

        **version**
        :   The version string describing the graphics API and/or driver,
            or **null** if it could not be determined

        **issues**
        :   Problems with this graphics stack, represented as an array
            of strings. The array is empty if no problems were detected.

            **internal-error**
            :   There was an internal error while checking graphics support.

            **cannot-load**
            :   The necessary libraries could not be loaded, or a rendering
                context could not be created

            **software-rendering**
            :   This graphics stack appears to be using unaccelerated
                software rendering.

            **timeout**
            :   The test to check the given graphics mode timed out.

            **cannot-draw**
            :   The test to see if drawing works for the given graphics mode
                had an error and was unable to draw.

    **dri_drivers**
    :   An array of objects describing the Mesa DRI driver modules that have been
        found. These drivers are used by Mesa and by older versions of Xorg.
        Every object can have the following keys:

        **library_path**
        :   Path to the driver module that has been found.

        **library_path_resolved**
        :   Absolute path to the driver module that has been found.
            This key is present only if **library_path** is relative.

        **is_extra**
        :   A boolean value indicating whether the driver module is in an
            "unusual" location, probably not in the canonical search path.
            This key is optional and if is not provided, the default %FALSE
            value should be assumed.

    **va-api_drivers**
    :   An array of objects describing the VA-API driver modules that have been
        found. These drivers are used by the VA-API, either `libva.so.2` (the
        current version) or `libva.so.1` (a legacy version).
        Every object can have the following keys:

        **library_path**
        :   Path to the driver module that has been found.

        **library_path_resolved**
        :   Absolute path to the driver module that has been found.
            This key is present only if **library_path** is relative.

        **is_extra**
        :   A boolean value indicating whether the driver module is in an
            "unusual" location, probably not in the canonical search path.
            This key is optional and if is not provided, the default %FALSE
            value should be assumed.

    **vdpau_drivers**
    :   An array of objects describing the VDPAU driver modules that have been
        found. These drivers are used by the VDPAU `libvdpau.so.1`.
        Every object can have the following keys:

        **library_path**
        :   Path to the driver module that has been found.

        **library_path_resolved**
        :   Absolute path to the driver module that has been found.
            This key is present only if **library_path** is relative.

        **library_link**
        :   Content of the **library_path** symbolic link. This key is optional
            and if is not provided, it should be assumed that **library_path**
            was a regular file and not a symbolic link.

        **is_extra**
        :   A boolean value indicating whether the driver module is in an
            "unusual" location, probably not in the canonical search path.
            This key is optional and if is not provided, the default %FALSE
            value should be assumed.

    **glx_drivers**
    :   An array of objects describing the GLX ICD modules that have been
        found. These drivers provide the OpenGL API for the X Window System,
        and are used by the `libGL.so.1` or `libGLX.so.0` loaders provided by
        [GLVND](https://github.com/NVIDIA/libglvnd).
        Every object can have the following keys:

        **library_soname**
        :   SONAME of the ICD module that has been found.

        **library_path**
        :   Absolute path to the library that imlements the **library_soname**.

**locale-issues**
:   An array of strings indicating locale-related issues.
    The array is empty if no problems were detected.

    **internal-error**
    :   There was an internal error while checking locale support.

    **default-missing**
    :   The system's configured locale is not available.

    **default-not-utf8**
    :   The system's configured locale is not a UTF-8 locale such as
        `en_US.UTF-8` or `de_DE.UTF-8`. This may cause problems in games
        and frameworks that assume that all strings are UTF-8, or that
        assume that all Unicode characters can be output.

    **c-utf8-missing**
    :   The special `C.UTF-8` locale provided by Debian and Fedora
        derivatives is not available. This may cause problems in games
        and frameworks that assume it is always present.

    **en-us-utf8-missing**
    :   The `en_US.UTF-8` locale is not available. This may cause
        problems in games and frameworks that assume it is always present.

    **i18n-supported-missing**
    :   The **SUPPORTED** file listing supported locales was not found
        in the expected location.
        This indicates that either locale data is not installed, or this
        operating system does not put it in the expected location.
        The Steam Runtime might be unable to generate extra locales if needed.

    **i18n-locales-en-us-missing**
    :   The **locales/en_US** file describing the USA English locale was
        not found in the expected location. This indicates that either
        locale data is not installed, or this operating system does not
        put it in the expected location, or only a partial set of locale
        source data is available. The Steam Runtime will be unable to
        generate extra locales if needed.

**locales**
:   An object describing locales. The keys are either **<default>** or
    a locale name. The values are objects containing either details of
    a locale:

    **resulting-name**
    :   The canonical name of the locale, which might differ from the
        name that was requested.

    **charset**
    :   The character set used by the locale, for example **UTF-8**.

    **is_utf8**
    :   Whether the locale appears to be UTF-8.

    or details of the error encountered when attempting to set a locale:

    **error-domain**
    :   A machine-readable string indicating a category of errors.

    **error-code**
    :   A small integer indicating a specific error. Its meaning depends
        on the **error-domain**.

    **error**
    :   A human-readable error message.

**egl**
:   An object describing EGL support. Currently the only key is
    **icds**. Its value is an array of objects describing ICDs,
    with the following keys and values if the metadata was loaded
    successfully:

    **json_path**
    :   Absolute path to the JSON file describing the ICD

    **library_path**
    :   The library as described in the JSON file: either an absolute
        path, a path relative to the JSON file containing at least one **/**,
        or a bare library name to be loaded from the system default library
        search path.

    **dlopen**
    :   The name to pass to **dlopen**(3), if it differs from
        **library_path**. This key/value pair is omitted if it would
        be the same as **library_path**. Currently, it is only shown
        if the **library_path** is relative, in which case **dlopen**
        is an absolute path formed by prefixing the directory part
        of **json_path** to **library_path**.

    or the following keys and values if the metadata failed to load:

    **json_path**
    :   Absolute path to the JSON file describing the ICD

    **error-domain**
    :   A machine-readable string indicating a category of errors.

    **error-code**
    :   A small integer indicating a specific error. Its meaning depends
        on the **error-domain**.

    **error**
    :   A human-readable error message.

**vulkan**
:   An object describing Vulkan support. Currently the keys are **icds**,
    **external_layers** and **internal_layers**. For **icds**, its value is an
    array of objects describing ICDs, with the same keys and values as for EGL
    ICDs, plus one extra key:

    **api_version**
    :   Vulkan API version implemented by this ICD as a dotted-decimal
        string, for example **1.1.90**

    Instead, the value of **external_layers**, and **internal_layers**, is an
    array of objects, where the former describes external Vulkan layers and
    the latter describes internal Vulkan layers. They have the same keys and
    values as for Vulkan ICDs, with the differences that **library_path**
    might be omitted, if it is not present, and they have the following extra
    keys:

    **name**
    :   The name that uniquely identify this layer to applications

    **description**
    :   Brief description of the layer

    **type**
    :   The type of the layer, expected to be either "GLOBAL" or "INSTANCE"

    **implementation_version**
    :   Version of the implemented layer

    **component_layer**
    :   An array of strings that identifies the component layer names that
        are part of a meta-layer. This will be omitted if **library_path**
        is used instead.

**desktop-entries**
:   An array of objects describing the Steam related desktop entries
    that have been found.
    Every object can have the following keys:

    **id**
    :   A string with the ID of the application entry, such as `steam.desktop`.

    **commandline**
    :   A string with which the application will be started, such as
        `/usr/bin/steam %U`.

    **filename**
    :   Absolute path to the desktop entry file.

    **default_steam_uri_handler**
    :   A boolean value indicating whether this entry is the default used
        to open `steam:` URIs.

    **steam_uri_handler**
    :   A boolean value indicating whether this entry can open `steam:` URIs.

**xdg-portals**
:   An object describing the XDG portal support. Whether `xdg-desktop-portal`
    is installed, and working, and which portal backend implementations are
    available. The keys are the strings:

    **details**
    :   An object describing in details which XDG portals have been checked.
        The keys are the strings:

        **interfaces**
        :   An object describing XDG portal interfaces. The keys are the
            interface name. The values are objects containing details of
            of an interface:

            **available**
            :   A boolean value indicating whether this interface is available.

            **version**
            :   An integer representing the version property of this interface.

        **backends**
        :   An object describing XDG portal backends. The keys are the
            backend name. The values are objects containing details of
            of an interface:

            **available**
            :   A boolean value indicating whether this backend is available.

    **issues**
    :   Problems with the XDG portal support, represented as an array
        of strings. The array is empty if no problems were detected.

        **unknown**
        :   There was an unknown error while checking XDG portal support.

        **timeout**
        :   The test to check the XDG portal support timed out.

        **missing-interface**
        :   At least one of the required XDG portal interfaces is missing.

        **no-implementation**
        :   There isn't a working XDG portal implementation.

    **messages**
    :   Human-readable string with error messages about the XDG portal
        support. This element will not be printed if there aren't any error
        messages.

**cpu-features**
:   An object decribing some of the features that the CPU in use supports.
    Currently it has the following string keys, each with a boolean
    value indicating whether the CPU feature is present or absent:

    **x86-64**
    :   Whether the CPU supports the "Long mode", i.e. x86-64 architecture
        (listed as `lm` in `/proc/cpuinfo`).

    **sse3**
    :   Whether the CPU supports the SSE3 extension (Streaming SIMD Extensions
        3, listed as `pni` (Prescott New Instructions) in `/proc/cpuinfo`).

    **cmpxchg16b**
    :   Whether the CPU supports the CMPXCHG16B instruction
        (listed as `cx16` in `/proc/cpuinfo`).

# EXIT STATUS

0
:   **steam-runtime-system-info** ran successfully. Note that this does
    not mean that Steam games will run successfully: inspect the JSON
    output to see whether there are problems.

Nonzero
:   An error occurred.

# EXAMPLES

To examine the Steam Runtime:

    $ ~/.local/share/Steam/ubuntu12_32/steam-runtime/setup.sh
    Pins up-to-date!
    $ env \
    PATH="$(~/.local/share/Steam/ubuntu12_32/steam-runtime/setup.sh --print-bin-path):$PATH" \
    ~/.local/share/Steam/ubuntu12_32/steam-runtime/run.sh \
    steam-runtime-system-info

Or change the launch options for a Steam game to:

    xterm & %command%

then launch the game, and in the resulting **xterm**, run

    $ steam-runtime-system-info

(Note that Steam will not consider the game to have exited until the
**xterm** is closed.)

<!-- vim:set sw=4 sts=4 et: -->
