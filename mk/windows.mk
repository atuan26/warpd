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

CXXFLAGS+=-I/mingw64/include/opencv4
LDFLAGS+=-lopencv_imgproc -lopencv_core
OPENCV_FILES=src/common/opencv_detector.cpp

CFILES=$(shell find src/*.c src/windows/*.c src/platform/windows/*.c src/common/*.c ! -name 'warpd.c' ! -name 'atspi-detector.c')
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

all: $(OBJFILES) src/windows/icon.res
	-mkdir -p bin
	$(CXX) -o bin/warpd-$(VERSION).exe $(OBJFILES) src/windows/icon.res $(LDFLAGS)
	@echo "Built: bin/warpd-$(VERSION).exe"

src/windows/icon.res: src/windows/icon.rc
	windres $< -O coff -o $@

clean:
	-rm -f $(OBJFILES)
	-rm -f src/windows/icon.res
	-rm -rf bin

.PHONY: all clean