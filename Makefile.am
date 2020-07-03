# Copyright © 2017 Collabora Ltd

# This file is part of libcapsule.

# libcapsule is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation; either version 2.1 of the
# License, or (at your option) any later version.

# libcapsule is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.

# You should have received a copy of the GNU Lesser General Public
# License along with libcapsule.  If not, see <http://www.gnu.org/licenses/>.

AUTOMAKE_OPTIONS        = subdir-objects
ACLOCAL_AMFLAGS         = -I m4 --install
AM_CPPFLAGS             = -DHOST_PREFIX=\"$(HOST_PREFIX)\"
common_cflags           = -fvisibility=hidden \
                          --std=c99 \
                          $(WARN_CFLAGS) \
                          $(LIBELF_CFLAGS) \
                          $(GLIB_CFLAGS)
# Used for production code and tests, but not for test fixtures
AM_CFLAGS               = $(common_cflags) $(SANITIZE_CFLAGS)
# Used for test fixtures only
test_fixture_cflags     = $(common_cflags)
AM_LDFLAGS              = $(WARN_LDFLAGS)
static_docs             = Building Debugging Limitations Quick-Start Capsules

# installable items:
bin_PROGRAMS            = capsule-capture-libs capsule-symbols capsule-version
if ENABLE_LIBRARY
dist_bin_SCRIPTS        = data/capsule-init-project
lib_LTLIBRARIES         = libcapsule.la
pkgconfigdir            = $(libdir)/pkgconfig
pkgconfig_DATA          = data/libcapsule.pc data/libcapsule-tools.pc
nobase_include_HEADERS  = capsule/capsule.h
dist_doc_DATA           = $(foreach doc,$(static_docs),doc/$(doc).txt)
endif

if ENABLE_TOOLS_RPATH
# Use -Wl to hide it from libtool
tools_ldflags           = -Wl,-rpath,$(enable_tools_rpath)
endif

if ENABLE_HOST_PREFIX
if ENABLE_LIBRARY
bin_SCRIPTS             = data/$(HOST_PREFIX)capsule-mkstublib

# Write out a version of capsule-mkstublib with different defaults
# for each architecture
data/$(HOST_PREFIX)capsule-mkstublib: data/capsule-mkstublib Makefile
	$(AM_V_GEN)set -e; \
	( \
		sed -e 1q $<; \
		echo '# Inserted by Makefile to make this architecture-specific'; \
		echo ': "$${CAPSULE_SYMBOLS_TOOL:="$(bindir)/$(HOST_PREFIX)capsule-symbols"}"';\
		sed -e 1d $<; \
	) > $@.tmp; chmod +x $@.tmp; mv $@.tmp $@
endif

install-exec-hook: install-exec-hook-host-prefix
install-exec-hook-host-prefix:
	cd $(DESTDIR)$(bindir) && ln -f capsule-symbols $(HOST_PREFIX)capsule-symbols
	cd $(DESTDIR)$(bindir) && ln -f capsule-version $(HOST_PREFIX)capsule-version
	cd $(DESTDIR)$(bindir) && ln -f capsule-elf-dump $(HOST_PREFIX)capsule-elf-dump
	cd $(DESTDIR)$(bindir) && ln -f capsule-capture-libs $(HOST_PREFIX)capsule-capture-libs

uninstall-hook: uninstall-hook-host-prefix
uninstall-hook-host-prefix:
	cd $(DESTDIR)$(bindir) && rm -f $(HOST_PREFIX)capsule-capture-libs
	cd $(DESTDIR)$(bindir) && rm -f $(HOST_PREFIX)capsule-elf-dump
	cd $(DESTDIR)$(bindir) && rm -f $(HOST_PREFIX)capsule-symbols
	cd $(DESTDIR)$(bindir) && rm -f $(HOST_PREFIX)capsule-version
else
if ENABLE_LIBRARY
dist_bin_SCRIPTS            += data/capsule-mkstublib
endif
endif

if ENABLE_LIBRARY
# Headers to be included by shim libraries
nobase_include_HEADERS += capsule/capsule-shim.h \
                          capsule/_int_dlopen.h
endif

noinst_LTLIBRARIES = utils/libutils.la \
                     utils/libld.la

utils_libutils_la_SOURCES  = utils/utils.c utils/utils.h
utils_libld_la_SOURCES     = utils/ld-cache.c utils/ld-cache.h
utils_libld_la_SOURCES    += utils/ld-libs.c utils/ld-libs.h
utils_libld_la_LIBADD      = utils/libutils.la -ldl $(LIBELF_LIBS)

# Mostly just a demo program, but keep this hanging around as it's a useful PoC
bin_PROGRAMS           += capsule-elf-dump

capsule_elf_dump_SOURCES        = utils/elf-dump.c utils/dump.c
capsule_elf_dump_SOURCES       += utils/tools.c utils/tools.h
capsule_elf_dump_LDADD          = utils/libutils.la -ldl
# This looks redundant, but it forces Automake to build separate
# instantiations of dump.o for elf-dump and libcapsule
capsule_elf_dump_CFLAGS         = $(AM_CFLAGS)
capsule_elf_dump_LDFLAGS        = $(AM_LDFLAGS) $(tools_ldflags)

capsule_capture_libs_SOURCES = utils/capture-libs.c \
                               utils/library-cmp.c  \
                               utils/library-cmp.h  \
                               utils/tools.c        \
                               utils/tools.h
capsule_capture_libs_LDADD   = utils/libld.la
capsule_capture_libs_LDFLAGS = $(AM_LDFLAGS) $(tools_ldflags)

# now the rules for building our installables:
capsule_symbols_SOURCES = utils/print-libstubs.c utils/tools.c utils/tools.h
capsule_symbols_LDADD   = utils/libld.la
capsule_symbols_LDFLAGS = $(AM_LDFLAGS) $(tools_ldflags)

capsule_version_SOURCES = utils/print-version.c utils/tools.c utils/tools.h
capsule_version_LDADD   = utils/libld.la
capsule_version_LDFLAGS = $(AM_LDFLAGS) $(tools_ldflags)

if ENABLE_LIBRARY
libcapsule_la_SOURCES   = capsule/capsule-dlmopen.c  \
                          capsule/capsule-relocate.c \
                          capsule/capsule-init.c     \
                          capsule/capsule-private.h  \
                          capsule/capsule-malloc.h   \
                          capsule/capsule-wrappers.c \
                          utils/dump.c               \
                          utils/process-pt-dynamic.c \
                          utils/mmap-info.c
libcapsule_la_LDFLAGS   = -version-number 0:1 -Wl,-z,nodelete
libcapsule_la_LIBADD    = utils/libld.la
libcapsule_la_SOURCES  += utils/debug.h              \
                          utils/dump.h               \
                          utils/process-pt-dynamic.h \
                          utils/mmap-info.h

dist_pkgdata_DATA       = data/capsule-shim.mk    \
                          data/disabled.mk        \
                          data/shim-.gitignore    \
                          data/shim-Makefile.am   \
                          data/shim-README.in     \
                          data/shim-configure.ac.in \
                          data/shim-target-Makefile.am
endif

BUILT_SOURCES           = $(top_srcdir)/.version

$(top_srcdir)/.version:
	$(AM_V_GEN)echo $(VERSION) > $@-t && mv $@-t $@

include $(top_srcdir)/data/disabled.mk

# ============================================================================
# GNOME-style installed-tests

insttestsdir                         = $(libexecdir)/installed-tests/$(PACKAGE_TARNAME)
nobase_insttests_LTLIBRARIES         =

if ENABLE_LIBRARY
# /!\ Order is important here: the helper library must come first
nobase_insttests_LTLIBRARIES        += tests/red/libhelper.la                  \
                                       tests/green/libhelper.la                \
                                       tests/helper/libhelper.la               \
                                       tests/red/libnotgl.la                   \
                                       tests/red/libnotgles.la                 \
                                       tests/green/libnotgl.la                 \
                                       tests/green/libnotgles.la               \
                                       tests/lib/libnotgl.la                   \
                                       tests/lib/libnotgles.la                 \
                                       tests/shim/libnotgl.la                  \
                                       tests/shim/libnotgles.la
nobase_dist_insttests_DATA           = tests/CapsuleTest.pm                    \
                                       tests/CapsuleTestDpkg.pm                \
                                       tests/notgl.h                           \
                                       tests/notgl-green.c                     \
                                       tests/notgl-red.c                       \
                                       tests/notgl-ref.c                       \
                                       tests/notgl-helper.h                    \
                                       tests/notgl-helper-green.c              \
                                       tests/notgl-helper-red.c                \
                                       tests/notgl-helper-ref.c                \
                                       tests/notgles.h                         \
                                       tests/notgles-green.c                   \
                                       tests/notgles-red.c                     \
                                       tests/notgles-ref.c                     \
                                       tests/shim/libnotgl.so.0.excluded       \
                                       tests/shim/libnotgl.so.0.shared         \
                                       tests/shim/libnotgl.so.0.symbols        \
                                       tests/shim/libnotgles.so.1.excluded     \
                                       tests/shim/libnotgles.so.1.shared       \
                                       tests/shim/libnotgles.so.1.symbols
# Install the source code so we can rebuild it inside a container if we want to
gl_shims =                             libEGL.so.1                             \
                                       libGL.so.1                              \
                                       libGLESv2.so.2                          \
                                       libGLX.so.0                             \
                                       libOpenGL.so.0                          \
                                       libX11.so.6                             \
                                       libXext.so.6                            \
                                       libXi.so.6                              \
                                       libgbm.so.1                             \
                                       libxcb-dri2.so.0                        \
                                       libxcb-glx.so.0                         \
                                       libxcb-present.so.0                     \
                                       libxcb-sync.so.1                        \
                                       libxcb.so.1
# We don't do this via nobase_dist_insttests_DATA because patsubst doesn't
# work for make dist, so we would have to write it out longhand
install-data-local-shim-sources:
	$(AM_V_GEN)set -e; \
	$(mkinstalldirs) $(DESTDIR)$(insttestsdir)/examples/shim; \
	for lib in ${gl_shims}; do \
		$(INSTALL_DATA) \
			$(srcdir)/examples/shim/$$lib.excluded \
			$(srcdir)/examples/shim/$$lib.shared \
			$(srcdir)/examples/shim/$$lib.symbols \
			$(srcdir)/examples/shim/$$lib.symbols.updated-for \
			$(DESTDIR)$(insttestsdir)/examples/shim; \
	done
uninstall-hook: uninstall-hook-shim-sources
uninstall-hook-shim-sources:
	$(AM_V_GEN)set -e; \
	for lib in ${gl_shims}; do \
		rm -fr \
			$(DESTDIR)$(insttestsdir)/examples/shim/$$lib.excluded \
			$(DESTDIR)$(insttestsdir)/examples/shim/$$lib.shared \
			$(DESTDIR)$(insttestsdir)/examples/shim/$$lib.symbols \
			$(DESTDIR)$(insttestsdir)/examples/shim/$$lib.symbols.updated-for \
		; \
	done
dist-hook-shim-sources:
	$(AM_V_GEN)set -e; \
	$(MKDIR_P) $(distdir)/examples/shim; \
	for lib in ${gl_shims}; do \
		cp \
			$(srcdir)/examples/shim/$$lib.excluded \
			$(srcdir)/examples/shim/$$lib.shared \
			$(srcdir)/examples/shim/$$lib.symbols \
			$(srcdir)/examples/shim/$$lib.symbols.updated-for \
			$(distdir)/examples/shim; \
	done
endif

maintainer-update-gl-shims:
	@if test -z "${GL_SHIMS_SOURCE_MACHINE}"; then \
		echo "usage: make -C $(CURDIR) maintainer-update-gl-shims GL_SHIMS_SOURCE_MACHINE=some-machine">&2; \
		exit 2; \
	fi
	set -e; for lib in ${gl_shims}; do \
		ssh "${GL_SHIMS_SOURCE_MACHINE}" \
		x86_64-linux-gnu-capsule-symbols "$$lib" \
		> $(abs_top_srcdir)/examples/shim/"$$lib.symbols.tmp"; \
		LC_ALL=C sort -u \
		< $(abs_top_srcdir)/examples/shim/"$$lib.symbols.tmp" \
		> $(abs_top_srcdir)/examples/shim/"$$lib.symbols"; \
		rm -f $(abs_top_srcdir)/examples/shim/"$$lib.symbols.tmp"; \
		: > $(abs_top_srcdir)/examples/shim/"$$lib.excluded"; \
		: > $(abs_top_srcdir)/examples/shim/"$$lib.shared"; \
		: > $(abs_top_srcdir)/examples/shim/"$$lib.symbols.updated-for"; \
	done

if ENABLE_LIBRARY
tests_lib_libnotgl_la_SOURCES        = tests/notgl-ref.c                       \
                                       tests/notgl.h                           \
                                       tests/notgl-helper.h
tests_lib_libnotgl_la_CFLAGS         = $(test_fixture_cflags)
tests_lib_libnotgl_la_LDFLAGS        = -shared -version-number 0:42:23
tests_lib_libnotgl_la_LIBADD         = tests/helper/libhelper.la

tests_lib_libnotgles_la_SOURCES      = tests/notgles-ref.c                     \
                                       tests/notgles.h                         \
                                       tests/notgl-helper.h
tests_lib_libnotgles_la_CFLAGS       = $(test_fixture_cflags)
tests_lib_libnotgles_la_LDFLAGS      = -shared -version-number 1:42:23
tests_lib_libnotgles_la_LIBADD       = tests/helper/libhelper.la

tests_red_libnotgl_la_SOURCES        = tests/notgl-red.c                       \
                                       tests/notgl.h                           \
                                       tests/notgl-helper.h
tests_red_libnotgl_la_CFLAGS         = $(test_fixture_cflags)
# Give it a gratuitously higher version number
tests_red_libnotgl_la_LDFLAGS        = -shared -version-number 0:123:456
# We want the RUNPATH or RPATH to point to tests/helper here, so we can
# demonstrate the functionality of loading two different libraries from the
# same path on host and container
tests_red_libnotgl_la_LIBADD         = tests/helper/libhelper.la

tests_red_libnotgles_la_SOURCES      = tests/notgles-red.c                     \
                                       tests/notgles.h                         \
                                       tests/notgl-helper.h
tests_red_libnotgles_la_CFLAGS       = $(test_fixture_cflags)
tests_red_libnotgles_la_LDFLAGS      = -shared -version-number 1:123:456
tests_red_libnotgles_la_LIBADD       = tests/helper/libhelper.la

tests_green_libnotgl_la_SOURCES      = tests/notgl-green.c                     \
                                       tests/notgl.h                           \
                                       tests/notgl-helper.h
tests_green_libnotgl_la_CFLAGS       = $(test_fixture_cflags)
# Give this one a gratuitously *lower* version number
tests_green_libnotgl_la_LDFLAGS      = -shared -version-number 0:23:42
# Again, we want the RUNPATH or RPATH to point to tests/helper here
tests_green_libnotgl_la_LIBADD       = tests/helper/libhelper.la

tests_green_libnotgles_la_SOURCES    = tests/notgles-green.c                   \
                                       tests/notgles.h                         \
                                       tests/notgl-helper.h
tests_green_libnotgles_la_CFLAGS     = $(test_fixture_cflags)
tests_green_libnotgles_la_LDFLAGS    = -shared -version-number 1:23:42
tests_green_libnotgles_la_LIBADD     = tests/helper/libhelper.la

tests_helper_libhelper_la_SOURCES    = tests/notgl-helper-ref.c                \
                                       tests/notgl-helper.h
tests_helper_libhelper_la_CFLAGS     = $(test_fixture_cflags)
tests_helper_libhelper_la_LDFLAGS    = -shared -version-number 0:42:23

tests_red_libhelper_la_SOURCES       = tests/notgl-helper-red.c                \
                                       tests/notgl-helper.h
tests_red_libhelper_la_CFLAGS        = $(test_fixture_cflags)
tests_red_libhelper_la_LDFLAGS       = -shared -version-number 0:123:456

tests_green_libhelper_la_SOURCES     = tests/notgl-helper-green.c              \
                                       tests/notgl-helper.h
tests_green_libhelper_la_CFLAGS      = $(test_fixture_cflags)
tests_green_libhelper_la_LDFLAGS     = -shared -version-number 0:23:42

nodist_tests_shim_libnotgl_la_SOURCES = tests/shim/libnotgl.so.0.c
tests_shim_libnotgl_la_LIBADD        = libcapsule.la
tests_shim_libnotgl_la_CFLAGS        = $(test_fixture_cflags)
tests_shim_libnotgl_la_LDFLAGS       = -shared -version-number 0:0:0

nodist_tests_shim_libnotgles_la_SOURCES = tests/shim/libnotgles.so.1.c
tests_shim_libnotgles_la_LIBADD        = libcapsule.la
tests_shim_libnotgles_la_CFLAGS        = $(test_fixture_cflags)
tests_shim_libnotgles_la_LDFLAGS       = -shared -version-number 1:0:0

tests/shim/%.c: tests/shim/%.excluded tests/shim/%.shared tests/shim/%.symbols data/capsule-mkstublib Makefile
	$(AM_V_GEN)\
	PKG_CONFIG_PATH=$(abs_builddir)/data \
	V=$V \
	$(top_srcdir)/data/capsule-mkstublib \
		--symbols-from=$(srcdir)/tests/shim/$*.symbols \
		--no-update-symbols \
		--runtime-tree=/host \
		$* \
		$(srcdir)/tests/shim/$*.excluded \
		$(srcdir)/tests/shim/$*.shared \
		$@
endif

if ENABLE_SHARED
nobase_insttests_LTLIBRARIES        += tests/version1/libunversionedabibreak.la \
                                       tests/version2/libunversionedabibreak.la \
                                       tests/version1/libunversionednumber.la \
                                       tests/version2/libunversionednumber.la \
                                       tests/version1/libunversionedsymbols.la \
                                       tests/version2/libunversionedsymbols.la \
                                       tests/version1/libversionedabibreak.la \
                                       tests/version2/libversionedabibreak.la \
                                       tests/version1/libversionedlikedbus.la \
                                       tests/version2/libversionedlikedbus.la \
                                       tests/version1/libversionedlikeglibc.la \
                                       tests/version2/libversionedlikeglibc.la \
                                       tests/version1/libversionednumber.la \
                                       tests/version2/libversionednumber.la \
                                       tests/version1/libversionedsymbols.la \
                                       tests/version2/libversionedsymbols.la \
                                       tests/version1/libversionedupgrade.la \
                                       tests/version2/libversionedupgrade.la
tests_version1_libunversionedabibreak_la_CFLAGS  = $(test_fixture_cflags)
tests_version1_libunversionedabibreak_la_LDFLAGS = -shared -version-number 1:0:0
tests_version2_libunversionedabibreak_la_CFLAGS  = $(test_fixture_cflags)
tests_version2_libunversionedabibreak_la_LDFLAGS = -shared -version-number 1:0:0
tests_version1_libunversionednumber_la_CFLAGS  = $(test_fixture_cflags)
tests_version1_libunversionednumber_la_LDFLAGS = -shared -version-number 1:0:0
tests_version2_libunversionednumber_la_CFLAGS  = $(test_fixture_cflags)
tests_version2_libunversionednumber_la_LDFLAGS = -shared -version-number 1:2:3
tests_version1_libunversionedsymbols_la_CFLAGS  = $(test_fixture_cflags)
tests_version1_libunversionedsymbols_la_LDFLAGS = -shared -version-number 1:0:0
tests_version2_libunversionedsymbols_la_CFLAGS  = $(test_fixture_cflags)
tests_version2_libunversionedsymbols_la_LDFLAGS = -shared -version-number 1:0:0
tests_version1_libversionedabibreak_la_CFLAGS  = $(test_fixture_cflags)
tests_version1_libversionedabibreak_la_LDFLAGS = -shared -version-number 1:0:0 \
                                                 -Wl,--version-script=$(srcdir)/tests/version1/libversionedabibreak.map
tests_version2_libversionedabibreak_la_CFLAGS  = $(test_fixture_cflags)
tests_version2_libversionedabibreak_la_LDFLAGS = -shared -version-number 1:0:0 \
                                                 -Wl,--version-script=$(srcdir)/tests/version2/libversionedabibreak.map
tests_version1_libversionedlikedbus_la_CFLAGS  = $(test_fixture_cflags)
tests_version1_libversionedlikedbus_la_LDFLAGS = -shared -version-number 1:0:0 \
                                                 -Wl,--version-script=$(srcdir)/tests/version1/libversionedlikedbus.map
tests_version2_libversionedlikedbus_la_CFLAGS  = $(test_fixture_cflags)
tests_version2_libversionedlikedbus_la_LDFLAGS = -shared -version-number 1:2:0 \
                                                 -Wl,--version-script=$(srcdir)/tests/version2/libversionedlikedbus.map
tests_version1_libversionedlikeglibc_la_CFLAGS  = $(test_fixture_cflags)
tests_version1_libversionedlikeglibc_la_LDFLAGS = -shared -version-number 1:0:0 \
                                                  -Wl,--version-script=$(srcdir)/tests/version1/libversionedlikeglibc.map
tests_version2_libversionedlikeglibc_la_CFLAGS  = $(test_fixture_cflags)
tests_version2_libversionedlikeglibc_la_LDFLAGS = -shared -version-number 1:0:0 \
                                                  -Wl,--version-script=$(srcdir)/tests/version2/libversionedlikeglibc.map
tests_version1_libversionednumber_la_CFLAGS  = $(test_fixture_cflags)
tests_version1_libversionednumber_la_LDFLAGS = -shared -version-number 1:0:0 \
                                               -Wl,--version-script=$(srcdir)/tests/libversioned.map
tests_version2_libversionednumber_la_CFLAGS  = $(test_fixture_cflags)
tests_version2_libversionednumber_la_LDFLAGS = -shared -version-number 1:2:3 \
                                               -Wl,--version-script=$(srcdir)/tests/libversioned.map
tests_version1_libversionedsymbols_la_CFLAGS  = $(test_fixture_cflags)
tests_version1_libversionedsymbols_la_LDFLAGS = -shared -version-number 1:0:0 \
                                                -Wl,--version-script=$(srcdir)/tests/libversioned.map
tests_version2_libversionedsymbols_la_CFLAGS  = $(test_fixture_cflags)
tests_version2_libversionedsymbols_la_LDFLAGS = -shared -version-number 1:0:0 \
                                                -Wl,--version-script=$(srcdir)/tests/libversioned.map
tests_version1_libversionedupgrade_la_CFLAGS  = $(test_fixture_cflags)
tests_version1_libversionedupgrade_la_LDFLAGS = -shared -version-number 1:0:0 \
                                                -Wl,--version-script=$(srcdir)/tests/version1/libversionedupgrade.map
tests_version2_libversionedupgrade_la_CFLAGS  = $(test_fixture_cflags)
tests_version2_libversionedupgrade_la_LDFLAGS = -shared -version-number 1:0:0 \
                                                -Wl,--version-script=$(srcdir)/tests/version2/libversionedupgrade.map
endif

LOG_DRIVER                           = env AM_TAP_AWK='$(AWK)'                 \
                                       $(SHELL) $(top_srcdir)/build-aux/tap-driver.sh
LOG_COMPILER                         =
TEST_EXTENSIONS                      = .pl .t
PL_LOG_DRIVER                        = $(LOG_DRIVER)
PL_LOG_COMPILER                      = $(PERL)
T_LOG_DRIVER                         = $(LOG_DRIVER)
T_LOG_COMPILER                       = $(SHELL) $(top_srcdir)/tests/glib-tap.sh
AM_TESTS_ENVIRONMENT                 = export CAPSULE_CAPTURE_LIBS_TOOL="$(abs_builddir)/capsule-capture-libs"; \
                                       export CAPSULE_CFLAGS="-I$(abs_top_srcdir) -I$(abs_top_builddir)"; \
                                       export CAPSULE_LIBS="$(abs_top_builddir)/libcapsule.la"; \
                                       export CAPSULE_SYMBOLS_TOOL="$(abs_builddir)/capsule-symbols";\
                                       export CAPSULE_TESTS_GNU_BUILD="${build}"; \
                                       export CAPSULE_TESTS_GNU_HOST="${host}"; \
                                       export CAPSULE_TESTS_UNINSTALLED=1;     \
                                       export CAPSULE_VERSION_TOOL="$(abs_builddir)/capsule-version";\
                                       export CC="${CC}";                      \
                                       export G_TEST_SRCDIR="$(abs_srcdir)";   \
                                       export G_TEST_BUILDDIR="$(abs_builddir)";\
                                       export G_DEBUG=gc-friendly;             \
                                       export MALLOC_CHECK_=2;                 \
                                       export NM="$(NM)";                      \
                                       export PKG_CONFIG_PATH="$(abs_builddir)/data";
if ENABLE_LIBRARY
test_extra_programs                  = tests/notgl-user                        \
                                       tests/notgl-helper-user                 \
                                       tests/notgl-dlopener
tests_notgl_user_LDADD               = tests/lib/libnotgl.la
tests_notgl_helper_user_LDADD        = tests/lib/libnotgl.la                   \
                                       tests/lib/libnotgles.la                 \
                                       tests/helper/libhelper.la
tests_notgl_dlopener_LDADD           = -ldl
endif
test_extra_scripts                   = tests/manual/gl.pl

test_programs                        =
if HAVE_GLIB
test_programs                       += tests/utils.t
endif

tests_utils_t_SOURCES                = tests/utils.c
tests_utils_t_LDADD                  = utils/libutils.la $(GLIB_LIBS)

test_scripts                         = tests/capture-libs.pl                   \
                                       tests/symbols.pl                        \
                                       tests/version.pl

if ENABLE_LIBRARY
test_scripts                        += tests/gl-shim.pl                        \
                                       tests/init-project.pl                   \
                                       tests/notgl.pl
endif

nobase_insttests_PROGRAMS            = $(test_programs)                        \
                                       $(test_extra_programs)
nobase_dist_insttests_SCRIPTS        = $(test_scripts)                         \
                                       $(test_extra_scripts)
TESTS                                = $(test_programs)                        \
                                       $(test_scripts)
testmetadir                          = $(datadir)/installed-tests/$(PACKAGE_TARNAME)
testmeta_DATA                        = $(patsubst %,%.test,$(test_programs) $(test_scripts))
$(testmeta_DATA): tests/%.test: tests/% Makefile
	@$(MKDIR_P) $(dir $@)
	$(AM_V_GEN)set -e; \
	maybe_tap=; \
	case "$*" in \
		(*.t) \
			maybe_tap=" --tap"; \
			;; \
	esac; \
	(\
		echo "[Test]"; \
		echo "Type=session"; \
		echo "Output=TAP"; \
		echo "Exec=env CAPSULE_TESTS_GNU_HOST=$(host) $(insttestsdir)/tests/$*$$maybe_tap"; \
	) > $@.tmp && mv $@.tmp $@

if ENABLE_LIBRARY
# ============================================================================
# gtk-doc configuration: see /usr/share/doc/gtk-doc-tools/examples/Makefile.am
DOC_MODULE           = libcapsule
DOC_MAIN_SGML_FILE   = $(DOC_MODULE)-docs.xml
DOC_SOURCE_DIR       = $(top_srcdir)/capsule
SCAN_OPTIONS         = --rebuild-sections --rebuild-types
MKDB_OPTIONS         = --xml-mode --output-format=xml
MKTMPL_OPTIONS       =
MKHTML_OPTIONS       =
FIXXREF_OPTIONS      =
HFILE_GLOB           = $(top_srcdir)/capsule/capsule.h
CFILE_GLOB           = $(top_srcdir)/capsule/*.c
EXTRA_HFILES         =
IGNORE_HFILES        =
HTML_IMAGES          =
content_files        =
expand_content_files =
GTKDOC_CFLAGS        =
GTKDOC_LIBS          =

include $(top_srcdir)/gtk-doc.make
# standalone man pages
include  $(top_srcdir)/documentation.mk

# Workaround for https://bugzilla.gnome.org/show_bug.cgi?id=786174
$(DOC_MAIN_SGML_FILE): sgml-build.stamp
	@true
else
CLEANFILES              =
EXTRA_DIST              =
endif

# These need to come after we include gtk-doc.make, which unconditionally
# defines these variables and targets
dist-hook: dist-hook-git-version-gen
.PHONY: dist-hook-git-version-gen

dist-hook-git-version-gen:
	$(AM_V_GEN)echo $(VERSION) > $(distdir)/.tarball-version

dist-hook: dist-hook-shim-sources
.PHONY: dist-hook-shim-sources

install-data-local: install-data-local-shim-sources
.PHONY: install-data-local-shim-sources

CLEANFILES             += $(testmeta_DATA)
if ENABLE_HOST_PREFIX
CLEANFILES             += data/$(HOST_PREFIX)capsule-mkstublib
endif
CLEANFILES             += $(nodist_tests_shim_libnotgl_la_SOURCES)
CLEANFILES             += $(nodist_tests_shim_libnotgles_la_SOURCES)

EXTRA_DIST             += $(top_srcdir)/.version
EXTRA_DIST             += autogen.sh
EXTRA_DIST             += build-aux/git-version-gen
EXTRA_DIST             += ci/Jenkinsfile
EXTRA_DIST             += ci/gitlab-ci.yml
EXTRA_DIST             += data/capsule-mkstublib
EXTRA_DIST             += doc/Building-libcapsule.txt
EXTRA_DIST             += doc/Strategy.txt
EXTRA_DIST             += doc/capsule-init-project.xml
EXTRA_DIST             += doc/capsule-mkstublib.xml
EXTRA_DIST             += doc/devhelp2man.xslt
EXTRA_DIST             += tests/glib-tap.sh
EXTRA_DIST             += tests/libversioned.map
EXTRA_DIST             += tests/version1/libversionedabibreak.map
EXTRA_DIST             += tests/version1/libversionedlikedbus.map
EXTRA_DIST             += tests/version1/libversionedlikeglibc.map
EXTRA_DIST             += tests/version1/libversionedupgrade.map
EXTRA_DIST             += tests/version2/libversionedabibreak.map
EXTRA_DIST             += tests/version2/libversionedlikedbus.map
EXTRA_DIST             += tests/version2/libversionedlikeglibc.map
EXTRA_DIST             += tests/version2/libversionedupgrade.map
