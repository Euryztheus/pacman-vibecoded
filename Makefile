CXX      ?= g++
CXXFLAGS ?= -O2 -Wall
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)

all: pacman

pacman: pacman.cpp
	$(CXX) $(CXXFLAGS) -o pacman pacman.cpp $(SDL_CFLAGS) $(SDL_LIBS)

clean:
	rm -f pacman

.PHONY: all clean
