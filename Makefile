VERSION := $(shell ./build-aux/git-version-gen .tarball-version)
relocatabledir = /usr/lib/libcapsule/relocatable

all: binary

ifeq (,$(wildcard libcapsule_*.dsc))
_relocatabledir = $(relocatabledir)
else
_relocatabledir =
endif

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

install-%:
	mkdir -p relocatable-install/bin
	set -eu; \
	dhm="$$(dpkg-architecture -a"$*" -qDEB_HOST_MULTIARCH)"; \
	mkdir -p "relocatable-install/lib/$${dhm}"; \
	if [ -n "$(_relocatabledir)" ]; then \
		install $(_relocatabledir)/$${dhm}-capsule-capture-libs _build/; \
		install $(_relocatabledir)/$${dhm}-capsule-symbols _build/; \
	else \
		$(MAKE) -f Makefile.libcapsule $*; \
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
