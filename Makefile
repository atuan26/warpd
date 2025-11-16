VERSION=2.1.0
PREFIX?=/usr/local
COMMITSTR=$(shell commit=$$(git rev-parse --short HEAD 2> /dev/null) && echo " (built from: $$commit)")

ifndef PLATFORM
	UNAME_S := $(shell uname -s 2>/dev/null)
	ifeq ($(UNAME_S), Darwin)
		PLATFORM := macos
	else ifeq ($(UNAME_S), Linux)
		PLATFORM := linux
	else ifneq ($(OS),)
		ifeq ($(OS), Windows_NT)
			PLATFORM := windows
		endif
	endif
	PLATFORM ?= linux
endif

ifeq ($(PLATFORM), macos)
	VERSION:=$(VERSION)-osx
endif

%.o: %.c Makefile mk/*.mk
	$(CC) -c $< -o $@ $(CFLAGS)

CFLAGS:=-g\
       -Wall\
       -Wextra\
       -pedantic\
       -Wno-deprecated-declarations\
       -Wno-unused-parameter\
       -std=c99\
       -DVERSION='"v$(VERSION)$(COMMITSTR)"'\
       -D_DEFAULT_SOURCE \
       -D_FORTIFY_SOURCE=2  $(CFLAGS)

ifeq ($(PLATFORM), macos)
	include mk/macos.mk
else ifeq ($(PLATFORM), windows)
	include mk/windows.mk
else
	include mk/linux.mk
endif

debug:
	$(MAKE) DEBUG=1

man:
	scdoc < warpd.1.md | gzip > files/warpd.1.gz
