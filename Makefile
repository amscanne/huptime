#!/usr/bin/make -f

DESCRIPTION := $(shell git describe --tags --match 'v*' | cut -d'v' -f2-)
VERSION ?= $(shell echo $(DESCRIPTION) | cut -d'-' -f1)
RELEASE ?= $(shell echo $(DESCRIPTION) | cut -d'-' -f2- -s | tr '-' '.')

ifeq ($(VERSION),)
$(error No VERSION available, please set manually.)
endif
ifeq ($(RELEASE),)
RELEASE := 1
endif

SOFILE := lib/huptime/huptime.so
INCLUDES := $(wildcard src/*.h)
C_SOURCES := $(wildcard src/*.c)
CXX_SOURCES := $(wildcard src/*.cc)
OBJECTS := $(patsubst %.c,%.o,$(C_SOURCES)) $(patsubst %.cc,%.o,$(CXX_SOURCES))
DESTDIR ?= /usr/local
ARCH_TARGET ?= $(shell uname -m)

RPMBUILD := rpmbuild
DEBBUILD := debbuild

INSTALL_DIR := install -m 0755 -d
INSTALL_BIN := install -m 0755

ifeq ($(ARCH_TARGET),x86_32)
RPM_ARCH_OPT ?= --target=i386
DEB_ARCH_OPT ?= i386
else
ifeq ($(ARCH_TARGET),x86_64)
RPM_ARCH_OPT ?= --target=x86_64
DEB_ARCH_OPT ?= amd64
else
$(error Unknown architecture $(ARCH_TARGET)?)
endif
endif

CC := gcc
CXX := g++
OFFSET_FLAGS ?= -D_LARGEFILE64_SOURCE=1 -D_FILE_OFFSET_BITS=64
ifeq ($(ARCH_TARGET),x86_64)
ARCH_FLAGS ?= -m64
else
ARCH_FLAGS ?= -m32
endif
CFLAGS ?= -Wall -fPIC -std=gnu99 -D_GNU_SOURCE $(OFFSET_FLAGS) $(ARCH_FLAGS)
CXXFLAGS ?= -Wall -fPIC -fno-exceptions -fno-rtti -D_GNU_SOURCE -Wno-unused-function $(OFFSET_FLAGS) $(ARCH_FLAGS)
LDFLAGS ?= -nostdlib -lc -ldl -lpthread

default: test
.PHONY: default

test: build
	@./py.test -vv
.PHONY: test

debug: build
	@./py.test --capture=no -vv
.PHONY: debug

build: $(SOFILE)
.PHONY: build

$(SOFILE): $(OBJECTS) src/stubs.map
	@mkdir -p $(shell dirname $(SOFILE)) 
	@$(CC) $(CFLAGS) -o $@ $(filter %.o,$^) $(LDFLAGS) \
	    -shared -Wl,--version-script,src/stubs.map \
	    -fvisibility=hidden

%.o: %.c $(INCLUDES)
	@$(CC) -o $@ $(CFLAGS) -c $<

%.o: %.cc $(INCLUDES)
	@$(CXX) -o $@ $(CXXFLAGS) -c $<

install: build
	@mkdir -p $(DESTDIR)/bin
	@mkdir -p $(DESTDIR)/lib/huptime
	@$(INSTALL_BIN) bin/huptime $(DESTDIR)/bin/huptime
	@$(INSTALL_BIN) $(SOFILE) $(DESTDIR)/lib/huptime/$(shell basename $(SOFILE))

$(DEBBUILD):
	@rm -rf $(DEBBUILD)
	@$(INSTALL_DIR) $(DEBBUILD)
.PHONY: $(DEBBUILD)

$(RPMBUILD):
	@rm -rf $(RPMBUILD)
	@$(INSTALL_DIR) $(RPMBUILD)
	@$(INSTALL_DIR) $(RPMBUILD)/SRPMS
	@$(INSTALL_DIR) $(RPMBUILD)/BUILD
	@$(INSTALL_DIR) $(RPMBUILD)/BUILDROOT
	@$(INSTALL_DIR) $(RPMBUILD)/SPECS
	@$(INSTALL_DIR) $(RPMBUILD)/RPMS/$(ARCH_TARGET)
	@$(INSTALL_DIR) $(RPMBUILD)/SOURCES
.PHONY: $(RPMBUILD)

deb: $(DEBBUILD)
	@$(MAKE) install DESTDIR=$(DEBBUILD)/usr
	@sed -i -e 's/@(VERSION)/$(VERSION)-$(RELEASE)/' \
	    $(DEBBUILD)/usr/bin/huptime
	@rsync -rav packagers/deb/DEBIAN $(DEBBUILD)
	@sed -i -e 's/@(VERSION)/$(VERSION)/' $(DEBBUILD)/DEBIAN/control
	@sed -i -e 's/@(RELEASE)/$(RELEASE)/' $(DEBBUILD)/DEBIAN/control
	@sed -i -e 's/@(ARCH)/$(DEB_ARCH_OPT)/' $(DEBBUILD)/DEBIAN/control
	@fakeroot dpkg -b $(DEBBUILD) .
.PHONY: deb

rpm: $(RPMBUILD)
	@$(MAKE) install DESTDIR=$(RPMBUILD)/BUILDROOT/usr
	@sed -i -e 's/@(VERSION)/$(VERSION)-$(RELEASE)/' \
	    $(RPMBUILD)/BUILDROOT/usr/bin/huptime
	@rpmbuild -bb $(RPM_ARCH_OPT) \
	    --buildroot $(CURDIR)/$(RPMBUILD)/BUILDROOT \
	    --define="%_topdir $(CURDIR)/$(RPMBUILD)" \
	    --define="%version $(VERSION)" \
	    --define="%release $(RELEASE)" \
	    packagers/rpm/huptime.spec
	@mv $(RPMBUILD)/RPMS/$(ARCH_TARGET)/*.rpm .
.PHONY: rpm

packages: deb rpm
.PHONY: packages

clean:
	@rm -rf $(DEBBUILD) $(RPMBUILD)
	@rm -rf *.deb *.rpm
	@rm -f $(SOFILE) $(OBJECTS)
	@find . -name \*.pyc -exec rm -rf {} \;
	@rm -rf test/__pycache__
.PHONY: clean
