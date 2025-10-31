.PHONY: rel all install clean

CFILES=$(shell find src/*.c)
OBJCFILES=$(shell find src/platform/macos -name '*.m')
CXXFILES=src/common/opencv_detector.cpp src/platform/macos/opencv_detector.cpp
OBJECTS=$(CFILES:.c=.o) $(OBJCFILES:.m=.o) $(CXXFILES:.cpp=.o)

# OpenCV support - always enabled
LDFLAGS+=-lopencv_imgproc -lopencv_core -lstdc++

RELFLAGS=-Wl,-adhoc_codesign -framework cocoa -framework carbon

%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS)

all: $(OBJECTS)
	-mkdir -p bin
	$(CXX) -o bin/warpd $(OBJECTS) -framework cocoa -framework carbon $(LDFLAGS)
	./codesign/sign.sh
rel: clean
	$(CC) -o bin/warpd-arm $(CFILES) $(OBJCFILES) -target arm64-apple-macos $(CFLAGS) $(RELFLAGS)
	$(CC) -o bin/warpd-x86  $(CFILES) $(OBJCFILES) -target x86_64-apple-macos $(CFLAGS) $(RELFLAGS)
	lipo -create bin/warpd-arm bin/warpd-x86 -output bin/warpd && rm -r bin/warpd-*
	./codesign/sign.sh
	-rm -rf tmp dist
	mkdir tmp dist
	DESTDIR=tmp make install
	cd tmp && tar czvf ../dist/macos-$(VERSION).tar.gz $$(find . -type f)
	-rm -rf tmp
install:
	mkdir -p $(DESTDIR)/usr/local/bin/ \
		$(DESTDIR)/usr/local/share/man/man1/ \
		$(DESTDIR)/Library/LaunchAgents && \
	install -m644 files/warpd.1.gz $(DESTDIR)/usr/local/share/man/man1 && \
	install -m755 bin/warpd $(DESTDIR)/usr/local/bin/ && \
	install -m644 files/com.warpd.warpd.plist $(DESTDIR)/Library/LaunchAgents
uninstall:
	rm -f $(DESTDIR)/usr/local/share/man/man1/warpd.1.gz \
		$(DESTDIR)/usr/local/bin/warpd \
		$(DESTDIR)/Library/LaunchAgents/com.warpd.warpd.plist
clean:
	-rm $(OBJECTS)
