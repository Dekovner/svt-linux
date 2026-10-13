CXX = g++
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra $(shell pkg-config --cflags gtk+-3.0)
LDFLAGS = $(shell pkg-config --libs gtk+-3.0)

all: svt

svt: main.cpp icon_data.h
	$(CXX) $(CXXFLAGS) -o svt main.cpp $(LDFLAGS)

clean:
	rm -f svt
