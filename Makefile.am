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
ACLOCAL_AMFLAGS         = -I m4
AM_CFLAGS               = -fvisibility=hidden \
                          --std=c99 \
                          $(WARN_CFLAGS) \
                          $(LIBELF_CFLAGS)
AM_LDFLAGS              = $(WARN_LDFLAGS)
static_docs             = Building Debugging Limitations Quick-Start

# installable items:
libexec_PROGRAMS        = capsule-symbols capsule-version
dist_libexec_SCRIPTS    = data/capsule-mkstublib data/capsule-init-project
lib_LTLIBRARIES         = libcapsule.la
pkgconfigdir            = $(libdir)/pkgconfig
pkgconfig_DATA          = data/capsule.pc
nobase_include_HEADERS  = capsule/capsule.h
dist_doc_DATA           = $(foreach doc,$(static_docs),doc/$(doc).txt)

# Headers to be included by shim libraries
nobase_include_HEADERS += capsule/capsule-shim.h \
                          capsule/_ext_dlopen.h \
                          capsule/_ext_dlsym.h \
                          capsule/_int_dlopen.h

# demo program - keep this hanging around as it's a useful PoC
noinst_PROGRAMS         = elf-dump

elf_dump_SOURCES        = utils/elf-dump.c utils/utils.c
elf_dump_LDADD          = -ldl
elf_dump_SOURCES       += utils/utils.h
elf_dump_CFLAGS         = $(AM_CFLAGS)

# now the rules for building our installables:
capsule_symbols_SOURCES = utils/print-libstubs.c \
                          utils/utils.c          \
                          utils/ld-cache.c       \
                          utils/ld-libs.c
capsule_symbols_SOURCES += \
                          utils/utils.h          \
                          utils/ld-cache.h       \
                          utils/ld-libs.h
capsule_symbols_LDADD   = -ldl $(LIBELF_LIBS)
# this looks like a no-op but it gives capsule_symbols its own target
# so that libtool doesn't get bent out of shape about using an object
# (ld-cache.o, ld-libs.o) as both part of a library and a standalone
# executable:
capsule_symbols_CFLAGS  = $(AM_CFLAGS)

capsule_version_SOURCES = utils/print-version.c  \
                          utils/utils.c          \
                          utils/ld-cache.c       \
                          utils/ld-libs.c
capsule_version_SOURCES += \
                          utils/utils.h          \
                          utils/ld-cache.h       \
                          utils/ld-libs.h
capsule_version_LDADD   = -ldl $(LIBELF_LIBS)
capsule_version_CFLAGS  = $(AM_CFLAGS)


libcapsule_la_SOURCES   = capsule/capsule-dlmopen.c  \
                          capsule/capsule-relocate.c \
                          capsule/capsule-init.c     \
                          capsule/capsule-private.h  \
                          capsule/capsule-wrappers.c \
                          utils/utils.c              \
                          utils/dump.c               \
                          utils/process-pt-dynamic.c \
                          utils/mmap-info.c          \
                          utils/ld-cache.c           \
                          utils/ld-libs.c
libcapsule_la_LDFLAGS   = -version-number 0:1
libcapsule_la_LIBADD    = $(LIBELF_LIBS)
libcapsule_la_SOURCES  += utils/utils.h              \
                          utils/debug.h              \
                          utils/dump.h               \
                          utils/process-pt-dynamic.h \
                          utils/mmap-info.h          \
                          utils/ld-cache.h           \
                          utils/ld-libs.h

dist_pkgdata_DATA       = data/capsule-shim.mk.in \
                          data/disabled.mk        \
                          data/shim-.gitignore    \
                          data/shim-Makefile.am   \
                          data/shim-README.in     \
                          data/shim-configure.ac.in

BUILT_SOURCES           = $(top_srcdir)/.version

$(top_srcdir)/.version:
	$(AM_V_GEN)echo $(VERSION) > $@-t && mv $@-t $@

dist-hook: dist-hook-git-version-gen
.PHONY: dist-hook-git-version-gen
dist-hook-git-version-gen:
	$(AM_V_GEN)echo $(VERSION) > $(distdir)/.tarball-version

include $(top_srcdir)/data/disabled.mk

# ============================================================================
# GNOME-style installed-tests

insttestsdir                         = $(libexecdir)/installed-tests/$(PACKAGE_TARNAME)

# /!\ Order is important here: the helper library must come first
nobase_insttests_LTLIBRARIES         = tests/red/libhelper.la                  \
                                       tests/green/libhelper.la                \
                                       tests/helper/libhelper.la               \
                                       tests/red/libnotgl.la                   \
                                       tests/green/libnotgl.la                 \
                                       tests/lib/libnotgl.la                   \
                                       tests/shim/libnotgl.la
nobase_dist_insttests_DATA           = tests/CapsuleTest.pm
# Install the source code so we can rebuild it inside a container if we want to
nobase_dist_insttests_DATA          += tests/notgl.h                           \
                                       tests/notgl-green.c                     \
                                       tests/notgl-red.c                       \
                                       tests/notgl-ref.c                       \
                                       tests/notgl-helper.h                    \
                                       tests/notgl-helper-green.c              \
                                       tests/notgl-helper-red.c                \
                                       tests/notgl-helper-ref.c                \
                                       tests/shim/libnotgl.so.c.excluded       \
                                       tests/shim/libnotgl.so.c.shared         \
                                       tests/shim/libnotgl.so.c.symbols

tests_lib_libnotgl_la_SOURCES        = tests/notgl-ref.c                       \
                                       tests/notgl.h                           \
                                       tests/notgl-helper.h
tests_lib_libnotgl_la_LDFLAGS        = -shared -version-number 0:42:23
tests_lib_libnotgl_la_LIBADD         = tests/helper/libhelper.la

tests_red_libnotgl_la_SOURCES        = tests/notgl-red.c                       \
                                       tests/notgl.h                           \
                                       tests/notgl-helper.h
# Give it a gratuitously higher version number
tests_red_libnotgl_la_LDFLAGS        = -shared -version-number 0:123:456
# We want the RUNPATH or RPATH to point to tests/helper here, so we can
# demonstrate the functionality of loading two different libraries from the
# same path on host and container
tests_red_libnotgl_la_LIBADD         = tests/helper/libhelper.la

tests_green_libnotgl_la_SOURCES      = tests/notgl-green.c                     \
                                       tests/notgl.h                           \
                                       tests/notgl-helper.h
# Give this one a gratuitously *lower* version number
tests_green_libnotgl_la_LDFLAGS      = -shared -version-number 0:23:42
# Again, we want the RUNPATH or RPATH to point to tests/helper here
tests_green_libnotgl_la_LIBADD       = tests/helper/libhelper.la

tests_helper_libhelper_la_SOURCES    = tests/notgl-helper-ref.c                \
                                       tests/notgl-helper.h
tests_helper_libhelper_la_LDFLAGS    = -shared -version-number 0:42:23

tests_red_libhelper_la_SOURCES       = tests/notgl-helper-red.c                \
                                       tests/notgl-helper.h
tests_red_libhelper_la_LDFLAGS       = -shared -version-number 0:123:456

tests_green_libhelper_la_SOURCES     = tests/notgl-helper-green.c              \
                                       tests/notgl-helper.h
tests_green_libhelper_la_LDFLAGS     = -shared -version-number 0:23:42

nodist_tests_shim_libnotgl_la_SOURCES = tests/shim/libnotgl.so.c
tests_shim_libnotgl_la_LIBADD        = libcapsule.la
tests_shim_libnotgl_la_LDFLAGS       = -shared -version-number 0:0:0

tests/shim/lib%.so.c: tests/shim/lib%.so.c.excluded tests/shim/lib%.so.c.shared tests/shim/lib%.so.c.symbols data/capsule-mkstublib Makefile
	$(AM_V_GEN)\
	PKG_CONFIG_PATH=$(abs_builddir)/data \
	V=$V \
	$(top_srcdir)/data/capsule-mkstublib \
		--ext-dlopen \
		--ext-dlsym \
		--symbols-from=$(srcdir)/$@.symbols \
		--no-update-symbols \
		--runtime-tree=/host \
		$$(basename $@ .c).0 \
		$(srcdir)/$@.excluded \
		$(srcdir)/$@.shared \
		$@

LOG_DRIVER                           = env AM_TAP_AWK='$(AWK)'                 \
                                       $(SHELL) $(top_srcdir)/build-aux/tap-driver.sh
LOG_COMPILER                         =
TEST_EXTENSIONS                      = .pl
PL_LOG_DRIVER                        = $(LOG_DRIVER)
PL_LOG_COMPILER                      = $(PERL)
AM_TESTS_ENVIRONMENT                 = export CAPSULE_CFLAGS="-I$(abs_top_srcdir) -I$(abs_top_builddir)"; \
                                       export CAPSULE_LIBS="$(abs_top_builddir)/libcapsule.la"; \
                                       export CAPSULE_SYMBOLS_TOOL="$(abs_builddir)/capsule-symbols";\
                                       export CAPSULE_TESTS_UNINSTALLED=1;     \
                                       export CAPSULE_VERSION_TOOL="$(abs_builddir)/capsule-version";\
                                       export G_TEST_SRCDIR="$(abs_srcdir)";   \
                                       export G_TEST_BUILDDIR="$(abs_builddir)";\
                                       export G_DEBUG=gc-friendly;             \
                                       export MALLOC_CHECK_=2;                 \
                                       export PKG_CONFIG_PATH="$(abs_builddir)/data";
test_extra_programs                  = tests/notgl-user                        \
                                       tests/notgl-helper-user
tests_notgl_user_LDADD               = tests/lib/libnotgl.la
tests_notgl_helper_user_LDADD        = tests/lib/libnotgl.la                   \
                                       tests/helper/libhelper.la
test_extra_scripts                   = tests/manual/gl.pl

test_programs                        =
test_scripts                         = tests/init-project.pl                   \
                                       tests/notgl.pl                          \
                                       tests/symbols.pl                        \
                                       tests/version.pl
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
	(\
		echo "[Test]"; \
		echo "Type=session"; \
		echo "Output=TAP"; \
		echo "Exec=$(insttestsdir)/tests/$*"; \
	) > $@.tmp && mv $@.tmp $@

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

# These need to come after we include gtk-doc.make, which unconditionally
# defines these variables
CLEANFILES             += $(testmeta_DATA)
CLEANFILES             += tests/shim/libnotgl.so.c
EXTRA_DIST             += $(top_srcdir)/.version
EXTRA_DIST             += autogen.sh
EXTRA_DIST             += build-aux/git-version-gen
EXTRA_DIST             += doc/Strategy.txt
EXTRA_DIST             += doc/capsule-init-project.xml
EXTRA_DIST             += doc/capsule-mkstublib.xml
EXTRA_DIST             += doc/devhelp2man.xslt
