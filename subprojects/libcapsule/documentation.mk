# Copyright © 2017 Collabora Ltd
# SPDX-License-Identifier: LGPL-2.1-or-later

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

#
# ============================================================================
# standalone man pages from docbook source:
XSLTPROC_FLAGS = \
        --nonet \
        --stringparam man.output.quietly 1 \
        --stringparam funcsynopsis.style ansi \
        --stringparam man.th.extra1.suppress 1 \
        --stringparam man.authors.section.enabled 1 \
        --stringparam man.copyright.section.enabled 0

XSLT_DOMAIN = docbook.sourceforge.net
XSLT_MAN = http://$(XSLT_DOMAIN)/release/xsl/current/manpages/docbook.xsl
XSLT_CAPARGS = --stringparam fsinfo "$$(printf "\#define _GNU_SOURCE\012\#include <capsule/capsule.h>")" --stringparam target
XSLTPROC_STD = $(XSLTPROC) $(XSLTPROC_FLAGS)

man_MANS =

if HAVE_XSLTPROC
%.1: doc/%.xml
	$(AM_V_GEN) $(XSLTPROC_STD) $(XSLT_MAN) $<

man_MANS   += capsule-init-project.1 capsule-mkstublib.1
endif # HAVE_XSLTPROC

if ENABLE_GTK_DOC

xml/capsule.xml: docs

%.3.xml: xml/capsule.xml doc/devhelp2man.xslt documentation.mk
	$(AM_V_GEN) $(XSLTPROC_STD) $(XSLT_CAPARGS) $* $(srcdir)/doc/devhelp2man.xslt $< > $@

%.3: %.3.xml documentation.mk
	$(AM_V_GEN) $(XSLTPROC_STD) $(XSLT_MAN) $<

man_MANS   += capsule_init.3
man_MANS   += capsule_shim_dlopen.3
man_MANS   += capsule_external_dlsym.3

endif # ENABLE_GTK_DOC

CLEANFILES += $(man_MANS)
