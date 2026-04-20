CC=g++
CXXFLAGS=-Wall -Wextra -Wuninitialized -std=c++20 -MMD -g -fdiagnostics-color=auto
LDFLAGS=
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
