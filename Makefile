CC := g++
CXXFLAGS := -Iinclude/ -O3 -std=c++20

CPP_FILES := $(shell find src/ -type f -name "*.cpp")
OBJ_FILES := $(CPP_FILES:%.cpp=%.o)
HPP_FILES := $(shell find include/ -type f -name "*.hpp")

all: x86lab

# Compute each .cpp file's dependency list (headers) and generate a rule for
# their %.o with the correct deps.
# For each cpp file this output a line of the form:
# 	foo.o: foo.cpp bar.hpp baz.hpp ...
.deps: $(CPP_FILES)
	for f in $(CPP_FILES); do \
		$(CC) -MM $$f; \
	done >> $@
# Include the deps computed above.
include .deps

# Executable.
x86lab: main.o $(OBJ_FILES)
	$(CC) -o $@ $^

.PHONY: clean
clean:
	rm -f x86lab .deps $(OBJ_FILES) main.o
