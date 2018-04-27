all: install-amd64 install-i386

libcapsule/configure:
	set -e; cd libcapsule; NOCONFIGURE=1 ./autogen.sh

_build/%/config.status: libcapsule/configure Makefile
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
	    $(NULL)

build-%: _build/%/config.status Makefile
	make -C _build/$*/libcapsule

install-%: build-% Makefile
	set -eu; \
	mkdir -p relocatable-install/bin; \
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
