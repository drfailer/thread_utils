CC=g++
CXXFLAGS=-Wall -Wextra -Wuninitialized -std=c++20 -fPIC -mcx16 -MMD -O3 -fdiagnostics-color=auto
LDFLAGS=-latomic
SRC=$(wildcard src/**/*.cpp)
OBJ=$(addprefix build/,$(SRC:src/%.cpp=%.o))
DEP=$(addprefix build/,$(SRC:src/%.cpp=%.d))

prog: build/main.o $(OBJ)
	$(CC) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

.PHONY lib: build/lib/libthread_utils.so build/lib/libthread_utils.a

build/lib/libthread_utils.so: $(OBJ)
	@mkdir -p build/lib
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) -shared

build/lib/libthread_utils.a: $(OBJ)
	@mkdir -p build/lib
	ar rcs $@ $^

build/%.o: src/%.cpp
	@mkdir -p build/thread_utils
	$(CC) $(CXXFLAGS) -o $@ -c $<

clean:
	rm -rf build

.PHONY run:
	./prog

-include $(DEP)
