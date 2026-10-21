# Builds the engine library objects and the evaluation harnesses (E0 to E4).
# Usage: make -j2        (from this directory)
SRC_ROOT := ../cpp
OBJ := build/obj
BIN := build/bin
# Clang by default: GCC 13 rejects an out-of-order designated initializer in
# cpp/io/ViewProjection.cpp (ill-formed in C++20). Override with CXX=... if needed.
ifeq ($(origin CXX),default)
CXX := clang++
endif
# The engine's own -Werror flag set trips -Wmissing-field-initializers under Clang 18 on
# Linux; the harness build relaxes only that warning and Clang's designator-order warning.
# Optimisation flags match the paper.
CXXFLAGS := -std=c++20 -O2 -DNDEBUG -Wall -Wextra -Wpedantic -Wno-missing-field-initializers -Wno-reorder-init-list -I$(SRC_ROOT)
SRCS := $(shell find $(SRC_ROOT) -name '*.cpp' | sort)
OBJS := $(patsubst $(SRC_ROOT)/%.cpp,$(OBJ)/%.o,$(SRCS))
LIBOBJS := $(filter-out $(OBJ)/app/main.o,$(OBJS))
EXPS := $(patsubst %.cpp,$(BIN)/%,$(wildcard e*.cpp))

all: $(EXPS)

$(OBJ)/%.o: $(SRC_ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

$(BIN)/%: %.cpp common.hpp $(LIBOBJS)
	@mkdir -p $(BIN)
	$(CXX) $(CXXFLAGS) $< $(LIBOBJS) -o $@

clean:
	rm -rf build

-include $(OBJS:.o=.d)
