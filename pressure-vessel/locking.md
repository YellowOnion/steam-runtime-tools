pressure-vessel file locking model
==================================

pressure-vessel uses fcntl locks to guard concurrent access to runtimes.
They are `F_OFD_SETLK` locks if possible, falling back to POSIX
process-oriented `F_SETLK` locks on older kernels. See `bwrap-lock.c`
for full details.

Do not use `flock(2)`, `flock(1)` or `lockf(3)` to interact with
pressure-vessel. `bwrap --lock-file` or `pressure-vessel-adverb`
can be used.

Runtimes
--------

When pressure-vessel-wrap is told to use a runtime, for example
`scout/files`, it takes out a shared (read) lock on the file `.ref`
at top level, for example `scout/files/.ref`.

If told to make a mutable copy of the runtime, this lock is only held
for as long as it takes to carry out setup. There is a separate lock on
the mutable copy (see below).

If the runtime is used directly, the lock file may appear as either
`/usr/.ref` or `/.ref` inside the container. If the runtime is a merged
/usr rather than a complete root filesystem, the lock file will be
`/usr/.ref` with a symbolic link `/.ref -> usr/.ref`. If the runtime is
a complete root filesystem, the lock file will be `/.ref`. Processes
inside the container wishing to take out the lock must use `/.ref`.

Processes wishing to clean up obsolete runtimes should take out an
exclusive (write) lock on the runtime's `/.ref`, for example
`scout/files/.ref`, to prevent deletion of a runtime that is in use.

Mutable copies
--------------

pressure-vessel-wrap can be told to make a mutable copy of the runtime
with `--runtime-mutable-parent=${parent}`.

While setting this up, it takes out a shared (read) lock on
`${parent}/.ref`. During setup, it additionally takes out a shared (read)
lock on `${parent}/tmp-${random}/.ref`, which continues to be held after
setup has finished and the lock on the parent directory has been released.
(Implementation detail: that's actually a symbolic link to `usr/.ref`.)

Processes wishing to clean up the mutable copies must take an
exclusive (write) lock on `${parent}/.ref`, to prevent
pressure-vessel-wrap from creating new mutable copies during cleanup.

A process that is holding the lock on the parent may detect whether each
mutable runtime `${parent}/tmp-${random}/` is in use by attempting to
take out an exclusive (write) lock on `${parent}/tmp-${random}/.ref`
(to avoid deadlocks, this is best done in non-blocking mode). If it
successfully takes the lock, it may delete the runtime.
