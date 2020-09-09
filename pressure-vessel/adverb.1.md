---
title: pressure-vessel-adverb
section: 1
...

# NAME

pressure-vessel-adverb - wrap processes in various ways

# SYNOPSIS

**pressure-vessel-adverb**
[**--[no-]exit-with-parent**]
[**--fd** *FD*...]
[**--[no-]generate-locales**]
[**--pass-fd** *FD*...]
[**--shell** **none**|**after**|**fail**|**instead**]
[**--subreaper**]
[**--terminal** **none**|**auto**|**tty**|**xterm**]
[**--terminate-timeout** *SECONDS* [**--terminate-idle-timeout** *SECONDS*]]
[[**--[no-]create**]
[**--[no-]wait**]
[**--[no-]write**]
**--lock-file** *FILENAME*...]
[**--verbose**]
[**--**]
*COMMAND* [*ARGUMENTS...*]

# DESCRIPTION

**pressure-vessel-adverb** runs *COMMAND* as a child process, with
modifications to its execution environment as determined by the options.

By default, it just runs *COMMAND* as a child process and reports its
exit status.

# OPTIONS

**--create**
:   Create each **--lock-file** that appears on the command-line after
    this option if it does not exist, until a **--no-create** option
    is seen. **--no-create** reverses this behaviour, and is the default.

**--exit-with-parent**
:   Arrange for **pressure-vessel-adverb** to receive **SIGTERM**
    (which it will pass on to *COMMAND*, if possible) when its parent
    process exits. This is particularly useful when it is wrapped in
    **bwrap** by **pressure-vessel-wrap**, to arrange for the
    **pressure-vessel-adverb** command to exit when **bwrap** is killed.
    **--no-exit-with-parent** disables this behaviour, and is the default.

**--fd** *FD*
:   Receive file descriptor *FD* (specified as a small positive integer)
    from the parent process, and keep it open until
    **pressure-vessel-adverb** exits. This is most useful if *FD*
    is locked with a Linux open file description lock (**F_OFD_SETLK**
    or **F_OFD_SETLKW** from **fcntl**(2)), in which case the lock will
    be held by **pressure-vessel-adverb**.

**--generate-locales**
:   If not all configured locales are available, generate them in a
    temporary directory which is passed to the *COMMAND* in the
    **LOCPATH** environment variable.
    **--no-generate-locales** disables this behaviour, and is the default.

**--lock-file** *FILENAME*
:   Lock the file *FILENAME* according to the most recently seen
    **--[no-]create**, **--[no-]wait** and **--[no-]write** options,
    using a Linux open file description lock (**F_OFD_SETLK** or
    **F_OFD_SETLKW** from **fcntl**(2)) if possible, or a POSIX
    process-associated record lock (**F_SETLK** or **F_SETLKW**) on older
    kernels.

**--pass-fd** *FD*
:   Pass the file descriptor *FD* (specified as a small positive integer)
    from the parent process to the *COMMAND*. The default is to only pass
    through file descriptors 0, 1 and 2
    (**stdin**, **stdout** and **stderr**).

**--shell=after**
:   Run an interactive shell after *COMMAND* exits.
    In that shell, running **"$@"** will re-run *COMMAND*.

**--shell=fail**
:   The same as **--shell=after**, but do not run the shell if *COMMAND*
    exits with status 0.

**--shell=instead**
:   The same as **--shell=after**, but do not run *COMMAND* at all.

**--shell=none**
:   Don't run an interactive shell. This is the default.

**--subreaper**
:   If the *COMMAND* starts background processes, arrange for them to
    be reparented to **pressure-vessel-adverb** instead of to **init**
    when their parent process exits, and do not exit until all such
    descendant processes have exited.
    A non-negative **--terminate-timeout** implies this option.

**--terminal=auto**
:   Equivalent to **--terminal=xterm** if a **--shell** option other
    than **none** is used, or **--terminal=none** otherwise.
    This is the default.

**--terminal=none**
:   Disable features that would ordinarily use a terminal.

**--terminal=tty**
:   Use the current execution environment's controlling tty for
    the **--shell** options.

**--terminal=xterm**
:   Start an **xterm**(1), and run *COMMAND* and/or an interactive
    shell in that environment.

**--terminate-idle-timeout** *SECONDS*
:   If a non-negative **--terminate-timeout** is specified, wait this
    many seconds before sending **SIGTERM** to child processes.
    Non-integer decimal values are allowed.
    0 or negative means send **SIGTERM** immediately, which is the
    default.
    This option is ignored if **--terminate-timeout** is not used.

**--terminate-timeout** *SECONDS*
:   If non-negative, terminate background processes after the *COMMAND*
    exits. This implies **--subreaper**.
    Non-integer decimal values are allowed.
    When this option is enabled, after *COMMAND* exits,
    **pressure-vessel-adverb** will wait for up to the time specified
    by **--terminate-idle-timeout**, then send **SIGTERM** to any
    remaining child processes and wait for them to exit gracefully.
    If child processes continue to run after a further time specified
    by this option, send **SIGKILL** to force them to be terminated,
    and continue to send **SIGKILL** until there are no more descendant
    processes. If *SECONDS* is 0, **SIGKILL** is sent immediately.
    A negative number means signals are not sent, which is the default.

**--verbose**
:   Be more verbose.

**--wait**
:   For each **--lock-file** that appears on the command-line after
    this option until a **--no-wait** option is seen, if the file is
    already locked in an incompatible way, **pressure-vessel-adverb**
    will wait for the current holder of the lock to release it.
    With **--no-wait**, which is the default, **pressure-vessel-adverb**
    will exit with status 75 (**EX_TEMPFAIL**) if a lock cannot be acquired.

**--write**
:   Each **--lock-file** that appears on the command-line after
    this option will be locked in **F_WRLCK** mode (an exclusive/write
    lock), until a **--no-write** option is seen. **--no-write** results
    in use of **F_RDLCK** (a shared/read lock), and is the default.

# OUTPUT

The standard output from *COMMAND* is printed on standard output.

The standard error from *COMMAND* is printed on standard error.
Diagnostic messages from **pressure-vessel-adverb** may also be printed
on standard error.

# SIGNALS

If **pressure-vessel-adverb** receives signals **SIGHUP**, **SIGINT**,
**SIGQUIT**, **SIGTERM**, **SIGUSR1** or **SIGUSR2**, it immediately
sends the same signal to *COMMAND*, hopefully causing *COMMAND* to
exit gracefully.

# EXIT STATUS

Nonzero (failure) exit statuses are subject to change, and might be
changed to be more like **env**(1) in future.

0
:   The *COMMAND* exited successfully with status 0

1
:   An error occurred while setting up the execution environment or
    starting the *COMMAND*

64 (**EX_USAGE**)
:   Invalid arguments were given

69 (**EX_UNAVAILABLE**)
:   An error occurred while setting up the execution environment

70 (**EX_SOFTWARE**)
:   The *COMMAND* terminated in an unknown way

75 (**EX_TEMPFAIL**)
:   A **--lock-file** could not be acquired, and **--wait** was not given

127
:   The *COMMAND* could not be started

Any value 1-255
:   The *COMMAND* exited unsuccessfully with the status indicated

128 + *n*
:   The *COMMAND* was killed by signal *n*
    (this is the same encoding used by **bash**(1), **bwrap**(1) and
    **env**(1))

# EXAMPLE

When running a game in a container as a single command,
**pressure-vessel-wrap** uses a pattern similar to:

    bwrap \
        ... \
    /path/to/pressure-vessel-adverb \
        --exit-with-parent \
        --generate-locales \
        --lock-file=/path/to/runtime/.ref \
        --subreaper \
        -- \
    /path/to/game ...

to preserve Steam's traditional behaviour for native Linux games (where
all background processes must exit before the game is considered to have
exited).

When running **pressure-vessel-launcher** in a container, the adverb looks
more like this:

    bwrap \
        ... \
    /path/to/pressure-vessel-adverb \
        --exit-with-parent \
        --generate-locales \
        --lock-file=/path/to/runtime/.ref \
        --subreaper \
        --terminate-timeout=10 \
        -- \
    /path/to/pressure-vessel-launcher ...

so that when the **pressure-vessel-launcher** is terminated by
**pressure-vessel-launch --terminate**, or when the **bwrap** process
receives a fatal signal, the **pressure-vessel-adverb** process will
gracefully terminate any remaining child/descendant processes before
exiting itself.

<!-- vim:set sw=4 sts=4 et: -->
