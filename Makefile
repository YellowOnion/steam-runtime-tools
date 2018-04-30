all: install

chroot = /srv/jessie

in_chroot = \
	bwrap \
	--ro-bind $(chroot) / \
	--dev-bind /dev /dev \
	--tmpfs /tmp \
	--tmpfs /home \
	--bind $(CURDIR) $(CURDIR) \
	--chdir $(CURDIR) \
	--setenv LC_ALL C.UTF-8 \
	$(NULL)

install: install-amd64 install-i386 libcapsule/configure
	install pressure-vessel-wrap relocatable-install/bin/
	mkdir -p relocatable-install/sources
	install -m644 THIRD-PARTY.md relocatable-install/sources/README.txt
	install -m644 libcapsule/debian/copyright relocatable-install/sources/capsule-capture-libs.txt
	install -m644 /usr/share/doc/zlib1g/copyright relocatable-install/sources/libz.txt
	install -m644 /usr/share/doc/libelf1/copyright relocatable-install/sources/libelf.txt
	dcmd install -m644 libcapsule*.dsc relocatable-install/sources/
	$(in_chroot) $(MAKE) in-chroot/install

in-chroot/install:
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
	$(in_chroot) $(MAKE) in-chroot/libcapsule/configure

in-chroot/libcapsule/configure:
	dpkg-source -x libcapsule*.dsc
	mv libcapsule-*/ libcapsule
	set -e; cd libcapsule; NOCONFIGURE=1 ./autogen.sh

_build/%/config.status: libcapsule/configure Makefile
	mkdir -p _build/$*/libcapsule
	$(in_chroot) $(MAKE) in-chroot/configure-$*

in-chroot/configure-%:
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

build-%: _build/%/config.status Makefile
	$(in_chroot) $(MAKE) -C _build/$*/libcapsule

install-%: build-% Makefile
	mkdir -p relocatable-install/bin
	$(in_chroot) $(MAKE) in-chroot/install-$*

in-chroot/install-%:
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
