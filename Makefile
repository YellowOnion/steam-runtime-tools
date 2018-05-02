VERSION := $(shell ./build-aux/git-version-gen .tarball-version)

all: binary

mirror = http://deb.debian.org/debian
tarball = _build/sysroot.tar.gz
sysroot = _build/sysroot

_build/sysroot/etc/debian_version: $(tarball)
	rm -fr _build/sysroot
	mkdir -p _build/sysroot
	tar -zxf $(tarball) --exclude="./dev/*" -C _build/sysroot
	touch $@

_build/sysroot.tar.gz: sysroot/debos.yaml
	mkdir -p $(dir $@)
	debos -t mirror:$(mirror) -t ospack:$@ sysroot/debos.yaml

in_sysroot = \
	bwrap \
	--ro-bind $(CURDIR)/$(sysroot) / \
	--dev-bind /dev /dev \
	--ro-bind /etc/resolv.conf /etc/resolv.conf \
	--tmpfs /tmp \
	--tmpfs /home \
	--bind $(CURDIR) $(CURDIR) \
	--chdir $(CURDIR) \
	--setenv LC_ALL C.UTF-8 \
	$(NULL)

install:
	rm -fr relocatable-install
	$(MAKE) install-amd64 install-i386
	install pressure-vessel-wrap relocatable-install/bin/
	mkdir -p relocatable-install/sources
	install -m644 THIRD-PARTY.md relocatable-install/sources/README.txt
	install -m644 libcapsule/debian/copyright relocatable-install/sources/capsule-capture-libs.txt
	install -m644 /usr/share/doc/zlib1g/copyright relocatable-install/sources/libz.txt
	install -m644 /usr/share/doc/libelf1/copyright relocatable-install/sources/libelf.txt
	dcmd install -m644 libcapsule*.dsc relocatable-install/sources/
	$(in_sysroot) $(MAKE) in-sysroot/install

in-sysroot/install:
	cd relocatable-install/sources; \
	apt-get --download-only source elfutils zlib

libcapsule/configure: $(sysroot)/etc/debian_version
	rm -fr libcapsule
	first=; \
	for t in libcapsule*.dsc; do \
		if [ -z "$$first" ]; then \
			first="$$t"; \
		else \
			echo "Exactly one libcapsule*.dsc is required" >&2; \
			exit 1; \
		fi; \
	done
	$(in_sysroot) $(MAKE) in-sysroot/libcapsule/configure

in-sysroot/libcapsule/configure:
	dpkg-source -x libcapsule*.dsc
	mv libcapsule-*/ libcapsule
	set -e; cd libcapsule; NOCONFIGURE=1 ./autogen.sh

_build/%/config.stamp: libcapsule/configure $(sysroot)/etc/debian_version
	mkdir -p _build/$*/libcapsule
	$(in_sysroot) $(MAKE) in-sysroot/configure-$*
	touch $@

in-sysroot/configure-%:
	set -eu; \
	eval "$$(dpkg-architecture -a"$*" --print-set)"; \
	case "$${DEB_BUILD_ARCH}/$${DEB_HOST_ARCH}" in \
	    (amd64/i386) \
	        export CC="cc -m32"; \
	        ;; \
	esac; \
	cd _build/$*/libcapsule; \
	../../../libcapsule/configure \
	    --build="$${DEB_BUILD_GNU_TYPE}" \
	    --host="$${DEB_HOST_GNU_TYPE}" \
	    --enable-host-prefix="$${DEB_BUILD_MULTIARCH}-" \
	    --enable-tools-rpath="/_ORIGIN_/__/lib/$${DEB_BUILD_MULTIARCH}" \
	    --disable-shared \
	    --disable-gtk-doc \
	    $(NULL)

_build/%/build.stamp: _build/%/config.stamp $(sysroot)/etc/debian_version
	$(in_sysroot) $(MAKE) -C _build/$*/libcapsule
	touch $@

install-%:
	mkdir -p relocatable-install/bin
	$(in_sysroot) $(MAKE) in-sysroot/install-$*

in-sysroot/install-%:
	set -eu; \
	eval "$$(dpkg-architecture -a"$*" --print-set)"; \
	mkdir -p "relocatable-install/lib/$${DEB_HOST_MULTIARCH}"; \
	install "_build/$*/libcapsule/capsule-capture-libs" "_build/$${DEB_HOST_MULTIARCH}-capsule-capture-libs"; \
	chrpath -r "\$${ORIGIN}/../lib/$${DEB_HOST_MULTIARCH}" "_build/$${DEB_HOST_MULTIARCH}"-*; \
	install "_build/$${DEB_HOST_MULTIARCH}-capsule-capture-libs" relocatable-install/bin; \
	mkdir -p _build/$*/lib; \
	"relocatable-install/bin/$${DEB_HOST_MULTIARCH}-capsule-capture-libs" \
	    --dest=_build/$*/lib \
	    --no-glibc \
	    "soname-match:libelf.so.*" \
	    "soname-match:libz.so.*" \
	    $(NULL); \
	cp -a --dereference _build/$*/lib/*.so.* "relocatable-install/lib/$${DEB_HOST_MULTIARCH}"

check:
	prove -v t/

binary: pressure-vessel-$(VERSION)-bin.tar.gz
pressure-vessel-$(VERSION)-bin.tar.gz:
	$(MAKE) install
	tar -C relocatable-install -zcvf $@.tmp .
	mv $@.tmp $@

clean:
	rm -fr pressure-vessel-[0-9]*.tar.gz
	rm -fr _build
	rm -fr relocatable-install
	rm -fr libcapsule
