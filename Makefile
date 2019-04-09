VERSION := $(shell ./build-aux/git-version-gen .tarball-version)
relocatabledir = /usr/lib/libcapsule/relocatable

all: binary

ifeq (,$(wildcard libcapsule_*.dsc))
_relocatabledir = $(relocatabledir)
else
_relocatabledir =
endif

install:
	./build-relocatable-install.py $(_relocatabledir)

check:
	prove -v t/*.sh

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
