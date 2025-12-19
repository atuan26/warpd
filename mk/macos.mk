.PHONY: rel all install clean

CFILES=$(shell find src/*.c src/common/*.c src/smart_hint/*.c)
OBJCFILES=$(shell find src/platform/macos -name '*.m')
CXXFILES=
OBJCPPFILES=$(shell find src/platform/macos -name '*.mm')
OBJECTS=$(CFILES:.c=.o) $(OBJCFILES:.m=.o) $(CXXFILES:.cpp=.o) $(OBJCPPFILES:.mm=.o)

# Determine Homebrew prefix based on architecture
BREW_PREFIX := $(shell if [ "$(shell uname -m)" = "arm64" ] && [ -d "/opt/homebrew" ]; then echo "/opt/homebrew"; else echo "/usr/local"; fi)

# Set up OpenCV paths
# OpenCV support
OPENCV_ENABLE ?= 0

# Set up OpenCV paths
ifeq ($(OPENCV_ENABLE), 1)
	CXXFILES+=src/common/opencv_detector.cpp

	OPENCV_INCLUDE := $(BREW_PREFIX)/opt/opencv/include/opencv4
	OPENCV_LIB := $(BREW_PREFIX)/opt/opencv/lib
	CFLAGS += -DHAVE_OPENCV
	CXXFLAGS += -I$(OPENCV_INCLUDE) -DHAVE_OPENCV

	# Configure OpenCV libraries
	OPENCV_PC := $(wildcard $(BREW_PREFIX)/Cellar/opencv/*/lib/pkgconfig/opencv4.pc)
	ifneq ($(OPENCV_PC),)
		OPENCV_LIBS := $(shell grep "^Libs:" $(OPENCV_PC) | sed 's/^Libs: //' | sed 's/-L[^ ]* //g')
		LDFLAGS += -L$(OPENCV_LIB) $(OPENCV_LIBS) -lstdc++
	else
		LDFLAGS += -L$(OPENCV_LIB) -lopencv_imgproc -lopencv_core -lstdc++
	endif
endif

RELFLAGS=-Wl,-adhoc_codesign -framework cocoa -framework carbon -framework ScreenCaptureKit -framework CoreVideo -framework CoreMedia -framework ApplicationServices

%.o: %.cpp
	$(CXX) -c $< -o $@ $(CXXFLAGS)

%.o: %.mm
	$(CXX) -x objective-c++ -c $< -o $@ $(CXXFLAGS)

all: $(OBJECTS)
	-mkdir -p bin
	$(CXX) -o bin/warpd-$(VERSION) $(OBJECTS) $(LDFLAGS) -framework cocoa -framework carbon -framework ScreenCaptureKit -framework CoreVideo -framework CoreMedia -framework ApplicationServices
	./codesign/sign.sh bin/warpd-$(VERSION) || true
	@echo "Built: bin/warpd-$(VERSION)"
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
	install -m755 bin/warpd-$(VERSION) $(DESTDIR)/usr/local/bin/warpd && \
	install -m644 files/com.warpd.warpd.plist $(DESTDIR)/Library/LaunchAgents
uninstall:
	rm -f $(DESTDIR)/usr/local/share/man/man1/warpd.1.gz \
		$(DESTDIR)/usr/local/bin/warpd \
		$(DESTDIR)/Library/LaunchAgents/com.warpd.warpd.plist
clean:
	-rm $(OBJECTS)
