CXX = g++
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra $(shell pkg-config --cflags gtk+-3.0)
LDFLAGS = -Wl,--as-needed -static-libstdc++ -static-libgcc $(shell pkg-config --libs gtk+-3.0)

PREFIX ?= $(HOME)/.local

all: svt

svt: main.cpp icon_data.h
	$(CXX) $(CXXFLAGS) -o svt main.cpp $(LDFLAGS)

install: svt
	mkdir -p "$(PREFIX)/bin"
	mkdir -p "$(PREFIX)/share/icons/hicolor/64x64/apps"
	mkdir -p "$(PREFIX)/share/pixmaps"
	mkdir -p "$(PREFIX)/share/applications"
	install -m 755 svt "$(PREFIX)/bin/svt"
	install -m 644 svt.png "$(PREFIX)/share/icons/hicolor/64x64/apps/svt.png"
	install -m 644 svt.png "$(PREFIX)/share/pixmaps/svt.png"
	printf '[Desktop Entry]\nType=Application\nName=SVT\nComment=osu! Slider Velocity & Volume Tool (v1.0.3)\nExec=%s/bin/svt\nIcon=svt\nTerminal=false\nCategories=Game;Utility;\nStartupWMClass=svt\n' "$(PREFIX)" > "$(PREFIX)/share/applications/svt.desktop"
	-update-desktop-database "$(PREFIX)/share/applications" 2>/dev/null || true

uninstall:
	rm -f "$(PREFIX)/bin/svt"
	rm -f "$(PREFIX)/share/icons/hicolor/64x64/apps/svt.png"
	rm -f "$(PREFIX)/share/pixmaps/svt.png"
	rm -f "$(PREFIX)/share/applications/svt.desktop"
	-update-desktop-database "$(PREFIX)/share/applications" 2>/dev/null || true

clean:
	rm -f svt
