CXX      ?= g++
CXXFLAGS ?= -O2 -Wall
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)

SRCS    := $(wildcard src/*.cpp)
OBJS    := $(SRCS:.cpp=.o)
HEADERS := $(wildcard src/*.h)

all: pacman

pacman: $(OBJS)
	$(CXX) $(CXXFLAGS) -o pacman $(OBJS) $(SDL_CFLAGS) $(SDL_LIBS)

src/%.o: src/%.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f pacman $(OBJS)

.PHONY: all clean
