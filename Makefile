VERSION := $(shell ./build-aux/git-version-gen .tarball-version)

all: binary

install:
	rm -fr relocatable-install
	$(MAKE) install-amd64 install-i386
	install pressure-vessel-unruntime relocatable-install/bin/
	install pressure-vessel-wrap relocatable-install/bin/
	mkdir -p relocatable-install/sources
	install -m644 THIRD-PARTY.md relocatable-install/sources/README.txt
	install -m644 libcapsule/debian/copyright relocatable-install/sources/capsule-capture-libs.txt
	install -m644 /usr/share/doc/zlib1g/copyright relocatable-install/sources/libz.txt
	install -m644 /usr/share/doc/libelf1/copyright relocatable-install/sources/libelf.txt
	dcmd install -m644 libcapsule*.dsc relocatable-install/sources/
	cd relocatable-install/sources; \
	apt-get --download-only source elfutils zlib

libcapsule/configure:
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
	dpkg-source -x libcapsule*.dsc
	mv libcapsule-*/ libcapsule
	set -e; cd libcapsule; NOCONFIGURE=1 ./autogen.sh

_build/%/config.stamp: libcapsule/configure
	mkdir -p _build/$*/libcapsule
	set -eu; \
	DEB_BUILD_ARCH="$$(dpkg-architecture -a"$*" -qDEB_BUILD_ARCH)"; \
	DEB_HOST_ARCH="$$(dpkg-architecture -a"$*" -qDEB_HOST_ARCH)"; \
	DEB_BUILD_GNU_TYPE="$$(dpkg-architecture -a"$*" -qDEB_BUILD_GNU_TYPE)"; \
	DEB_HOST_GNU_TYPE="$$(dpkg-architecture -a"$*" -qDEB_HOST_GNU_TYPE)"; \
	DEB_HOST_MULTIARCH="$$(dpkg-architecture -a"$*" -qDEB_HOST_MULTIARCH)"; \
	case "$${DEB_BUILD_ARCH}/$${DEB_HOST_ARCH}" in \
	    (amd64/i386) \
	        export CC="cc -m32"; \
	        ;; \
	esac; \
	cd _build/$*/libcapsule; \
	../../../libcapsule/configure \
	    --build="$${DEB_BUILD_GNU_TYPE}" \
	    --host="$${DEB_HOST_GNU_TYPE}" \
	    --enable-host-prefix="$${DEB_HOST_MULTIARCH}-" \
	    --enable-tools-rpath="/_ORIGIN_/__/lib/$${DEB_HOST_MULTIARCH}" \
	    --disable-shared \
	    --disable-gtk-doc \
	    --without-glib \
	    $(NULL)
	touch $@

_build/%/build.stamp: _build/%/config.stamp
	$(MAKE) -C _build/$*/libcapsule
	touch $@

relocatabledir = /usr/lib/libcapsule/relocatable

install-%:
	mkdir -p relocatable-install/bin
	set -eu; \
	dhm="$$(dpkg-architecture -a"$*" -qDEB_HOST_MULTIARCH)"; \
	mkdir -p "relocatable-install/lib/$${dhm}"; \
	if [ -e $(relocatabledir)/$${dhm}-capsule-capture-libs ]; then \
		install $(relocatabledir)/$${dhm}-capsule-capture-libs _build/; \
		install $(relocatabledir)/$${dhm}-capsule-symbols _build/; \
	else \
		$(MAKE) _build/$*/build.stamp; \
		install "_build/$*/libcapsule/capsule-capture-libs" "_build/$${dhm}-capsule-capture-libs"; \
		install "_build/$*/libcapsule/capsule-symbols" "_build/$${dhm}-capsule-symbols"; \
		chrpath -r "\$${ORIGIN}/../lib/$${dhm}" "_build/$${dhm}"-*; \
		install "_build/$${dhm}-capsule-capture-libs" relocatable-install/bin; \
		install "_build/$${dhm}-capsule-symbols" relocatable-install/bin; \
	fi; \
	mkdir -p _build/$*/lib; \
	"relocatable-install/bin/$${dhm}-capsule-capture-libs" \
	    --dest=_build/$*/lib \
	    --no-glibc \
	    "soname:libelf.so.1" \
	    "soname:libz.so.1" \
	    $(NULL); \
	cp -a --dereference _build/$*/lib/*.so.* "relocatable-install/lib/$${dhm}"

check:
	prove -v t/

binary:
	$(MAKE) install
	tar --transform='s,^relocatable-install,pressure-vessel-$(VERSION),' -zcvf pressure-vessel-$(VERSION)-bin+src.tar.gz.tmp relocatable-install
	tar --transform='s,^relocatable-install,pressure-vessel-$(VERSION),' --exclude=sources -zcvf pressure-vessel-$(VERSION)-bin.tar.gz.tmp relocatable-install
	mv pressure-vessel-$(VERSION)-bin+src.tar.gz.tmp pressure-vessel-$(VERSION)-bin+src.tar.gz
	mv pressure-vessel-$(VERSION)-bin.tar.gz.tmp pressure-vessel-$(VERSION)-bin.tar.gz

clean:
	rm -fr pressure-vessel-[0-9]*.tar.gz
	rm -fr _build
	rm -fr relocatable-install
	rm -fr libcapsule
