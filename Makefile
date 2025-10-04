# Makefile para mp-mesi (C++20)
APP      := mp-mesi
CXX      := g++
CXXFLAGS := -std=gnu++20 -O2 -Wall -Wextra -Wpedantic -pthread
INCLUDES := -Iinclude
SRC_DIR  := src
OBJ_DIR  := build

SRCS := $(wildcard $(SRC_DIR)/*.cpp) main.cpp
OBJS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SRCS))

.PHONY: all run clean debug

all: $(APP)

$(APP): $(OBJS)
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

run: all
	@./$(APP)

debug: CXXFLAGS += -g -O0
debug: clean all

clean:
	@rm -rf $(OBJ_DIR) $(APP)
