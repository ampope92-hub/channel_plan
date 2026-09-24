# Builds the channel planner. Standard library only; these are the flags used when it is graded.
CXX ?= g++
CXXFLAGS ?= -O2 -std=c++17

SRCS := $(shell find src -maxdepth 1 -type f -name '*.cpp' | sort)

planner: $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -f planner
