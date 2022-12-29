CXX := clang++
AS := $(CXX)
ASFLAGS := -c
CXXFLAGS := -Wall -Wextra -Werror -Iinclude/ -I. -Iimgui -Iimgui/backends \
	-std=c++20 `sdl2-config --cflags` -O3
LDLIBS := -lncurses `sdl2-config --libs`
SHELL := /bin/bash

CPP_FILES := $(shell find src/ imgui/ -type f -name "*.cpp")
ASM_FILES := $(shell find src/ -type f -name "*.s")
OBJ_FILES := $(CPP_FILES:%.cpp=%.o) $(ASM_FILES:%.s=%.o)
HPP_FILES := $(shell find include/ -type f -name "*.hpp")

TEST_CPP_FILES := $(shell find tests/ -type f -name "*.cpp")
TEST_OBJ_FILES := $(TEST_CPP_FILES:%.cpp=%.o)
TEST_HPP_FILES := $(shell find tests/ -type f -name "*.hpp")

all: x86lab

# Compute each .cpp file's dependency list (headers) and generate a rule for
# their %.o with the correct deps.
# For each cpp file this output a line of the form:
# 	foo.o: foo.cpp bar.hpp baz.hpp ...
.deps: $(CPP_FILES) $(TEST_CPP_FILES)
	for f in $(CPP_FILES); do \
		$(CXX) $(CXXFLAGS) -MM $$f -MT $${f/cpp/o}; \
	done >> $@
	for f in $(TEST_CPP_FILES); do \
		$(CXX) $(CXXFLAGS) -Itests/include/ -MM $$f -MT $${f/cpp/o}; \
	done >> $@
# Include the deps computed above.
include .deps

# Executable.
x86lab: main.o $(OBJ_FILES)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

# Tests.
.PHONY: test
test: CXXFLAGS += -Itests/include/
test: x86labTests
	./x86labTests

x86labTests: $(TEST_OBJ_FILES) $(OBJ_FILES)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

.PHONY: clean
clean:
	rm -f x86lab .deps $(OBJ_FILES) $(TEST_OBJ_FILES) main.o x86labTests
