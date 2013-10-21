#!/usr/bin/make -f

SOFILE := lib/huptime/huptime.so
INCLUDES := $(wildcard src/*.h)
C_SOURCES := $(wildcard src/*.c)
CXX_SOURCES := $(wildcard src/*.cc)
OBJECTS := $(patsubst %.c,%.o,$(C_SOURCES)) $(patsubst %.cc,%.o,$(CXX_SOURCES))
DESTDIR ?= /usr/local

CC := gcc
CXX := g++
OFFSET_FLAGS ?= -D_LARGEFILE64_SOURCE=1 -D_FILE_OFFSET_BITS=64
CFLAGS ?= -Wall -fPIC -std=c99 -D_GNU_SOURCE $(OFFSET_FLAGS)
CXXFLAGS ?= -Wall -fPIC -D_GNU_SOURCE -Wno-unused-function $(OFFSET_FLAGS)
LDFLAGS ?= -shared

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
	    -ldl -lpthread -shared -Wl,--version-script,src/stubs.map \
	    -fvisibility=hidden

%.o: %.c $(INCLUDES)
	@$(CC) -o $@ $(CFLAGS) -c $<

%.o: %.cc $(INCLUDES)
	@$(CXX) -o $@ $(CXXFLAGS) -c $<

install: build
	@mkdir -p $(DESTDIR)/bin
	@mkdir -p $(DESTDIR)/lib/huptime
	@install -m 0755 -o 0 -g 0 bin/huptime $(DESTDIR)/bin/huptime
	@install -m 0755 -o 0 -g 0 $(SOFILE) $(DESTDIR)/lib/huptime/$(shell basename $(SOFILE))

clean:
	@rm -f $(SOFILE) $(OBJECTS)
	@find . -name \*.pyc -exec rm -rf {} \;
	@rm -rf test/__pycache__
.PHONY: clean
