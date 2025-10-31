#CC=cl.exe
CC=x86_64-w64-mingw32-gcc
CXX=x86_64-w64-mingw32-g++
CFLAGS+=-DWINDOWS -mwindows
CXXFLAGS+=-DWINDOWS -mwindows
LDFLAGS+=-luser32 -lgdi32 -lole32 -loleaut32 -luuid -lstdc++ -mwindows

CXXFLAGS+=-I/mingw64/include/opencv4
LDFLAGS+=-lopencv_imgproc -lopencv_core
OPENCV_FILES=src/common/opencv_detector.cpp

CFILES=$(shell find src/*.c src/windows/*.c src/platform/windows/*.c ! -name 'warpd.c' ! -name 'atspi-detector.c')
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

all: $(OBJFILES)
	-mkdir -p bin
	$(CXX) -o bin/warpd.exe $(OBJFILES) $(LDFLAGS)

clean:
	-rm -f $(OBJFILES)
	-rm -rf bin

.PHONY: all clean