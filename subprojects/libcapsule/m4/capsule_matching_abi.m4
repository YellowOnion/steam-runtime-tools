# Copyright 2019 Collabora Ltd.
#
# Copying and distribution of this file, with or without modification, are
# permitted in any medium without royalty provided the copyright notice
# and this notice are preserved.  This file is offered as-is, without any
# warranty.

#serial 1

dnl CAPSULE_MATCHING_ABI()
dnl
dnl Assert that the --host architecture is consistent with the selected
dnl compiler.
AC_DEFUN([CAPSULE_MATCHING_ABI], [
    AC_CANONICAL_HOST
    AC_LANG_PUSH([C])

    AS_CASE([${host_cpu}-${host_os}],
        [i?86-*], [
            AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[
#if defined(__i386__)
/* OK */
#else
#error not i386
#endif
                    ]]
                )],
                [],
                [
                    AC_MSG_ERROR([Host system $host must use an i386 compiler such as CC='gcc -m32' or CC='${host_cpu}-${host_os}-gcc'])
                ]
            )
        ],

        [x86_64-*-gnu], [
            AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[
#if defined(__x86_64__) && defined(__LP64__)
/* OK */
#else
#error not x86_64
#endif
                    ]]
                )],
                [],
                [
                    AC_MSG_ERROR([Host system $host must use an x86_64 compiler such as CC='gcc -m64' or CC='${host_cpu}-${host_os}-gcc'])
                ]
            )
        ],

        # libcapsule doesn't actually support x32, but for completeness...
        [x86_64-*-gnux32], [
            AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[]], [[
#if defined(__x86_64__) && defined(__ILP32__)
/* OK */
#else
#error not x32
#endif
                    ]]
                )],
                [],
                [
                    AC_MSG_ERROR([Host system $host must use an x32 compiler such as CC='gcc -mx32' or CC='${host_cpu}-${host_os}-gcc'])
                ]
            )
        ],

        # For non-x86 we assume users are more aware of how to select the
        # right cross-compiler. (libcapsule doesn't support non-x86 yet
        # anyway.)
    )

    AC_LANG_POP([C])
])dnl CAPSULE_MATCHING_ABI

# vim:set sw=4 sts=4 et:
