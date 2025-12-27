#CC=cl.exe
CC=x86_64-w64-mingw32-gcc
CXX=x86_64-w64-mingw32-g++

# VERSION=2.0.0
# COMMITSTR=$(shell commit=$$(git rev-parse --short HEAD 2> /dev/null) && echo " (built from: $$commit)")

CFLAGS+=-DWINDOWS -DVERSION='"v$(VERSION)$(COMMITSTR)"'
CXXFLAGS+=-DWINDOWS -DVERSION='"v$(VERSION)$(COMMITSTR)"'
LDFLAGS+=-luser32 -lgdi32 -lole32 -loleaut32 -luuid -lstdc++ -lshell32

# Debug mode: show console for logs
ifdef DEBUG
CFLAGS+=-DDEBUG -g
CXXFLAGS+=-DDEBUG -g
LDFLAGS+=-mconsole
else
CFLAGS+=-mwindows
CXXFLAGS+=-mwindows
LDFLAGS+=-mwindows
endif

# Static linking: eliminate MinGW DLL dependencies (libgcc, libstdc++, libwinpthread)
# Usage: make STATIC=1
ifdef STATIC
LDFLAGS+=-static -static-libgcc -static-libstdc++
endif

# OpenCV Config
OPENCV_ENABLE ?= 0
ifeq ($(OPENCV_ENABLE), 1)
	CFLAGS+=-DHAVE_OPENCV
	CXXFLAGS+=-I/mingw64/include/opencv4 -DHAVE_OPENCV
	LDFLAGS+=-lopencv_imgproc -lopencv_core
	OPENCV_FILES=src/common/opencv_detector.cpp
else
	OPENCV_FILES=
endif

CFILES=$(shell find src/*.c src/windows/*.c src/platform/windows/*.c src/common/*.c src/smart_hint/*.c ! -name 'warpd.c' ! -name 'atspi-detector.c')
CXXFILES=$(shell find src/platform/windows/*.cpp) $(OPENCV_FILES)
OBJFILES=$(CFILES:.c=.o) $(CXXFILES:.cpp=.o)

ifeq ($(CC), cl.exe)
OBJFILES:=$(OBJFILES:%.c=%.obj)
OBJFILES:=$(OBJFILES:%.cpp=%.obj)
else
OBJFILES:=$(OBJFILES:%.c=%.o)
OBJFILES:=$(OBJFILES:%.cpp=%.o)
endif

%.obj: %.c
	$(CC) /c /Fo:$@ $<

%.obj: %.cpp
	$(CXX) /c /Fo:$@ $<

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS)

# Admin mode: build with UAC manifest that requests admin privileges
# Usage: make ADMIN=1
# This is needed for warpd to work with elevated (admin) apps
ifdef ADMIN
MANIFEST_RES=src/windows/manifest.res
ADMIN_NOTE=@echo "NOTE: This build requests administrator privileges on startup"
else
MANIFEST_RES=
ADMIN_NOTE=@echo "NOTE: This build does NOT request admin. Use 'make ADMIN=1' for elevated app support."
endif

all: $(OBJFILES) src/windows/icon.res $(MANIFEST_RES)
	-mkdir -p bin
	$(CXX) -o bin/warpd-$(VERSION).exe $(OBJFILES) src/windows/icon.res $(MANIFEST_RES) $(LDFLAGS)
	@echo "Built: bin/warpd-$(VERSION).exe"
	$(ADMIN_NOTE)

src/windows/icon.res: src/windows/icon.rc
	windres $< -O coff -o $@

src/windows/manifest.res: src/windows/manifest.rc src/windows/warpd.manifest
	windres $< -O coff -o $@

clean:
	-rm -f $(OBJFILES)
	-rm -f src/windows/icon.res
	-rm -f src/windows/manifest.res
	-rm -rf bin

.PHONY: all clean