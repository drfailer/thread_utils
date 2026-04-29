CC=personal-g++
COMMON_INCLUDE_PATH=../../common/
TU_INCLUDE_PATH=../../src/thread_utils/
HH_INCLUDE_PATH=../../lib/hedgehog/hedgehog/
CBLAS_INCLUDE_PATH=$(HOME)/Programming/usr/include/openblas
CBLAS_LIBRARY_PATH=$(HOME)/Programming/usr/lib
CXXFLAGS=-Wall -Wextra -Wuninitialized \
		 -std=c++20 -MMD -O3 -fdiagnostics-color=auto \
		 -I$(TU_INCLUDE_PATH) -I$(COMMON_INCLUDE_PATH) -I$(CBLAS_INCLUDE_PATH) -I$(HH_INCLUDE_PATH)
LDFLAGS=-L../../build/lib -l:libthread_utils.a -latomic -L$(CBLAS_LIBRARY_PATH) -lopenblas -ltbb
SRC=$(wildcard src/*.cpp)
OBJ=$(addprefix build/,$(SRC:src/%.cpp=%.o))
DEP=$(addprefix build/,$(SRC:src/%.cpp=%.d))

prog: $(OBJ)
	$(CC) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.cpp
	@mkdir -p build/
	$(CC) $(CXXFLAGS) -o $@ -c $<

clean:
	rm -rf build

.PHONY run:
	./prog

-include $(DEP)
