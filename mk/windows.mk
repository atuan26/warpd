#CC=cl.exe
CC=x86_64-w64-mingw32-gcc
CXX=x86_64-w64-mingw32-g++
CFLAGS+=-DWINDOWS -mwindows
CXXFLAGS+=-DWINDOWS -mwindows
LDFLAGS+=-luser32 -lgdi32 -lole32 -loleaut32 -luuid -lstdc++ -mwindows

# Add OpenCV support if available
ifndef DISABLE_OPENCV
	CXXFLAGS+=-I/mingw64/include/opencv4
	LDFLAGS+=-lopencv_imgproc -lopencv_core
	OPENCV_FILES=src/common/opencv_detector.cpp src/platform/windows/opencv_detector.cpp
else
	OPENCV_FILES=
endif

# Basic working file lists - explicit to avoid find command issues
CFILES=src/config.c src/daemon.c src/grid.c src/grid_drw.c src/hint.c src/histfile.c src/history.c src/input.c src/mode-loop.c src/mouse.c src/normal.c src/screen.c src/scroll.c src/smart_hint.c src/warpd.c src/windows/main.c src/windows/stubs.c src/platform/windows/filemon.c src/platform/windows/ui_detector.c src/platform/windows/windows.c src/platform/windows/winscreen.c
CXXFILES=src/platform/windows/uiautomation_detector.cpp $(OPENCV_FILES)
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