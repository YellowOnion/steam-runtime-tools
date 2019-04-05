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
	    --without-glib \
	    $(NULL)
	touch $@

_build/%/build.stamp: _build/%/config.stamp
	$(MAKE) -C _build/$*/libcapsule
	touch $@

install-%: _build/%/build.stamp
	mkdir -p relocatable-install/bin
	set -eu; \
	eval "$$(dpkg-architecture -a"$*" --print-set)"; \
	mkdir -p "relocatable-install/lib/$${DEB_HOST_MULTIARCH}"; \
	install "_build/$*/libcapsule/capsule-capture-libs" "_build/$${DEB_HOST_MULTIARCH}-capsule-capture-libs"; \
	install "_build/$*/libcapsule/capsule-symbols" "_build/$${DEB_HOST_MULTIARCH}-capsule-symbols"; \
	chrpath -r "\$${ORIGIN}/../lib/$${DEB_HOST_MULTIARCH}" "_build/$${DEB_HOST_MULTIARCH}"-*; \
	install "_build/$${DEB_HOST_MULTIARCH}-capsule-capture-libs" relocatable-install/bin; \
	install "_build/$${DEB_HOST_MULTIARCH}-capsule-symbols" relocatable-install/bin; \
	mkdir -p _build/$*/lib; \
	"relocatable-install/bin/$${DEB_HOST_MULTIARCH}-capsule-capture-libs" \
	    --dest=_build/$*/lib \
	    --no-glibc \
	    "soname:libelf.so.1" \
	    "soname:libz.so.1" \
	    $(NULL); \
	cp -a --dereference _build/$*/lib/*.so.* "relocatable-install/lib/$${DEB_HOST_MULTIARCH}"

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
