AUTOMAKE_OPTIONS = subdir-objects
ACLOCAL_AMFLAGS  = -I m4
AM_CFLAGS        = -std=c99 -D_GNU_SOURCE -Wall -Werror
AM_CFLAGS       += $(CAPSULE_CFLAGS)

CAPSULE_SONAMES := libGL.so.1
CAPSULE_VERSION_libGL.so.1 := 1
#AM_CAPSULE_MKSTUBLIB_FLAGS := --ext-dlopen --ext-dlsym

lib_LTLIBRARIES = libGL.la

nodist_libGL_la_SOURCES = shim/libGL.so.1.c
libGL_la_LDFLAGS = $(shim_ldflags_libGL.so.1)
libGL_la_LIBADD = $(CAPSULE_LIBS)

CLEANFILES      = $(nodist_libGL_la_SOURCES) \
                  $(nodist_libGL_la_SOURCES:.c=.map)
BUILT_SOURCES   = $(nodist_libGL_la_SOURCES)

include @CAPSULE_MKINC@/disabled.mk
include @CAPSULE_MKINC@/capsule-shim.mk
