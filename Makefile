# Makefile para mp-mesi (C++20)
APP      := mp-mesi
CXX      := g++
CXXFLAGS := -std=gnu++20 -O2 -Wall -Wextra -Wpedantic -pthread
INCLUDES := -Iinclude
SRC_DIR  := src
OBJ_DIR  := build

SRCS := $(wildcard $(SRC_DIR)/*.cpp) main.cpp
OBJS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(SRCS))

.PHONY: all run clean debug runasm

all: $(APP)

$(APP): $(OBJS)
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Al ejecutar 'make run', si no se define ARGS, se usa examples/demo.asm por defecto
run: all
	@./$(APP) $(if $(ARGS),$(ARGS),examples/demo.asm)

# Alias explícito por si se prefiere un target dedicado
runasm: all
	@./$(APP) examples/demo.asm

debug: CXXFLAGS += -g -O0
debug: clean all

clean:
	@rm -rf $(OBJ_DIR) $(APP)
