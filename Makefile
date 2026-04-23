CC=g++
CXXFLAGS=-Wall -Wextra -Wuninitialized -std=c++20 -mcx16 -MMD -O3 -fdiagnostics-color=auto
LDFLAGS=-latomic
SRC=$(wildcard src/*.cpp) $(wildcard src/**/*.cpp)
OBJ=$(addprefix build/,$(SRC:src/%.cpp=%.o))
DEP=$(addprefix build/,$(SRC:src/%.cpp=%.d))

prog: $(OBJ)
	$(CC) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.cpp
	@mkdir -p build/thread_utils
	$(CC) $(CXXFLAGS) -o $@ -c $<

clean:
	rm -rf build

.PHONY run:
	./prog

-include $(DEP)
