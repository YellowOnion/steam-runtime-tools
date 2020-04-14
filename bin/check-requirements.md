---
title: steam-runtime-check-requirements
section: 1
...

# NAME

steam-runtime-check-requirements - perform checks to ensure that the Steam client requirements are met

# SYNOPSIS

**steam-runtime-check-requirements**

# DESCRIPTION

# OPTIONS

**--version**
:   Instead of performing the checks, write in output the version number as
    YAML.

# OUTPUT

If all the Steam client requirements are met the output will be empty.

Otherwise if some of the checks fails, the output will have a human-readable
message explaining what failed.


# EXIT STATUS

0
:   **steam-runtime-check-requirements** ran successfully and all the Steam
    client requirements are met.

71
:   At least one of the requirements is not met. In this case the exit status
    will be 71 (EX_OSERR).

Other Nonzero
:   An error occurred.

<!-- vim:set sw=4 sts=4 et: -->
