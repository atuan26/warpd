CFILES=$(shell find src/platform/linux/*.c src/*.c)
CXXFILES=

ifndef DISABLE_WAYLAND
	CFLAGS+=-lwayland-client\
		-lxkbcommon\
		-lcairo\
		-lrt\
		-DWARPD_WAYLAND=1

	CFILES+=$(shell find src/platform/linux/wayland/ -name '*.c')
endif

ifndef DISABLE_X
	CFLAGS+=-I/usr/include/freetype2/\
	    -I/usr/include/at-spi-2.0 -I/usr/include/dbus-1.0 -I/usr/lib/dbus-1.0/include -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -I/usr/include/libmount -I/usr/include/blkid -I/usr/include/sysprof-6 -pthread -latspi -ldbus-1 -lgobject-2.0 -lglib-2.0 \
		-lXfixes\
		-lXext\
		-lXinerama\
		-lXi\
		-lXtst\
		-lX11\
		-lXft\
		-DWARPD_X=1

	# OpenCV support for smart hint fallback
	CFLAGS+=-DENABLE_OPENCV=1 -I/usr/include/opencv4
	CXXFLAGS+=-DENABLE_OPENCV=1 -I/usr/include/opencv4
	LDFLAGS+=-lopencv_imgproc -lopencv_core -lstdc++
	CXXFILES+=$(shell find src/platform/linux -name 'opencv_detector.cpp')

	CFILES+=$(shell find src/platform/linux/X/*.c)
endif

OBJECTS=$(CFILES:.c=.o) $(CXXFILES:.cpp=.o)

%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS) $(CFLAGS)

all: $(OBJECTS)
	-mkdir -p bin
	$(CXX) -o bin/warpd $(OBJECTS) $(CFLAGS) $(LDFLAGS)
clean:
	-rm $(OBJECTS)
	-rm -r bin
install:
	mkdir -p $(DESTDIR)$(PREFIX)/share/man/man1/ $(DESTDIR)$(PREFIX)/bin/
	install -m644 files/warpd.1.gz $(DESTDIR)$(PREFIX)/share/man/man1/
	install -m755 bin/warpd $(DESTDIR)$(PREFIX)/bin/
uninstall:
	rm $(DESTDIR)$(PREFIX)/share/man/man1/warpd.1.gz\
		$(DESTDIR)$(PREFIX)/bin/warpd

.PHONY: all platform assets install uninstall bin
