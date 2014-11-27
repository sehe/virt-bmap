all:ranges
CPPFLAGS+=-std=c++0x -Wall -pedantic
CPPFLAGS+=-g -O3
CPPFLAGS+=-isystem ~/custom/boost/

# CPPFLAGS+=-fopenmp
CPPFLAGS+=-march=native
LDFLAGS+=-ltcmalloc

# CXX=/usr/lib/gcc-snapshot/bin/g++
# CC=/usr/lib/gcc-snapshot/bin/gcc
# CXX=clang++
# CC=clang

%:%.o
	$(CXX) $(CPPFLAGS) $^ -o $@ $(LDFLAGS)

ranges.cpp: ranges.h

ranges.o: ranges.cpp
	$(CXX) $(CPPFLAGS) $^ -o $@ -c

