top_srcdir = $(dir $(CURDIR))
top_builddir = $(dir $(CURDIR))

all:

mirror = http://deb.debian.org/debian
security_mirror = http://security.debian.org/debian-security
tarball = $(top_builddir)/_build/sysroot.tar.gz
sysroot = $(top_builddir)/_build/sysroot

$(top_builddir)/_build/sysroot/etc/debian_version: $(tarball)
	rm -fr $(top_builddir)/_build/sysroot
	mkdir -p $(top_builddir)/_build/sysroot
	tar -zxf $(tarball) --exclude="./dev/*" -C $(top_builddir)/_build/sysroot
	touch $@

$(top_builddir)/_build/sysroot.tar.gz: $(wildcard *)
	mkdir -p $(dir $@)
	set -e; \
	debos -t mirror:$(mirror) -t security_mirror:$(security_mirror) -t ospack:sysroot.tar.gz --artifactdir=$(top_builddir)/_build debos.yaml

ifeq ($(sysroot),/)
in_sysroot =
else
in_sysroot = \
	bwrap \
	--ro-bind $(sysroot) / \
	--dev-bind /dev /dev \
	--ro-bind /etc/resolv.conf /etc/resolv.conf \
	--tmpfs /tmp \
	--tmpfs /home \
	--bind $(top_builddir) $(top_builddir) \
	--bind $(top_srcdir) $(top_srcdir) \
	--chdir $(top_builddir) \
	--setenv LC_ALL C.UTF-8 \
	$(NULL)
endif

all binary check install: $(sysroot)/etc/debian_version
	$(in_sysroot) $(MAKE) -C $(top_builddir) $@

clean:
	if [ -e $(sysroot)/etc/debian_version ]; then \
		$(in_sysroot) $(MAKE) -C $(top_builddir) $@; \
	else \
		$(MAKE) -C $(top_builddir) $@; \
	fi
