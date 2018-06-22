# Makefile - (c) 2018 James Renwick
CXX ?= g++-7
PROFILE ?= application

CFLAGS += -Wall -Wextra -O0 -g -std=c++14 -fsanitize=address

.PHONY: library example clean test all

library:
	$(CXX) -c -fno-sized-deallocation $(CFLAGS) *.cpp -o osdb.o

tests/ostest/ostest.o:
	make -C tests/ostest CXX=$(CXX)

test: library tests/ostest/ostest.o
	$(CXX) -Wall -Wextra -O0 -g -std=c++14 -fsanitize=address -I. osdb.o tests/ostest/ostest.o tests/*.cpp -o test.exe

all: test

clean:
	rm -f test.exe osdb.o
