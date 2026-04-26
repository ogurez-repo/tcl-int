CC = gcc
CFLAGS = -Wall -Wextra -std=c11
GEN_CFLAGS = -Wno-sign-compare -Wno-unused-function -Wno-unneeded-internal-declaration
.DEFAULT_GOAL := all

REQUIRED_BUILD_TOOLS = $(CC) bison flex
REQUIRED_TEST_TOOLS = pytest

SRC_DIR = src
CORE_DIR = $(SRC_DIR)/core
RUNTIME_DIR = $(SRC_DIR)/runtime
GRAMMAR_DIR = $(SRC_DIR)/grammar
BIN_DIR = bin
GEN_DIR = $(BIN_DIR)/generated
OBJ_DIR = $(BIN_DIR)/obj

PARSER_SRC = $(GRAMMAR_DIR)/parser.y
LEXER_SRC = $(GRAMMAR_DIR)/lexer.l

PARSER_C = $(GEN_DIR)/parser.tab.c
PARSER_H = $(GEN_DIR)/parser.tab.h
LEXER_C = $(GEN_DIR)/lexer.yy.c

ifeq ($(OS),Windows_NT)
TARGET = $(BIN_DIR)/tclsh.exe
else
TARGET = $(BIN_DIR)/tclsh
endif

INCLUDES = -I$(CORE_DIR) -I$(RUNTIME_DIR) -I$(SRC_DIR) -I$(GEN_DIR)

rwildcard = $(foreach d,$(wildcard $(1)/*),$(call rwildcard,$(d),$(2))) \
    $(filter $(subst *,%,$(2)),$(wildcard $(1)/$(2)))

SRC_SOURCES = $(call rwildcard,$(SRC_DIR),*.c)
GEN_SOURCES = $(PARSER_C) $(LEXER_C)

SRC_OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/src/%.o,$(SRC_SOURCES))
GEN_OBJECTS = $(patsubst $(GEN_DIR)/%.c,$(OBJ_DIR)/gen/%.o,$(GEN_SOURCES))
OBJECTS = $(SRC_OBJECTS) $(GEN_OBJECTS)

$(SRC_OBJECTS): $(PARSER_H)

.PHONY: all build clean clean-all run test test-parser-lexer test-runtime help check-tools check-test-tools

all: check-tools $(TARGET)
build: check-tools $(TARGET)

check-tools:
	@for tool in $(REQUIRED_BUILD_TOOLS); do \
		command -v $$tool >/dev/null 2>&1 || { echo "missing tool: $$tool"; exit 1; }; \
	done

check-test-tools:
	@for tool in $(REQUIRED_TEST_TOOLS); do \
		command -v $$tool >/dev/null 2>&1 || { echo "missing test tool: $$tool"; exit 1; }; \
	done

$(GEN_DIR):
	mkdir -p $(GEN_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(PARSER_C) $(PARSER_H): $(PARSER_SRC) | $(BIN_DIR) $(GEN_DIR)
	bison -d -y -o $(PARSER_C) $(PARSER_SRC)

$(LEXER_C): $(LEXER_SRC) $(PARSER_H) | $(BIN_DIR) $(GEN_DIR)
	flex -t $(LEXER_SRC) > $(LEXER_C)

$(OBJ_DIR)/src/%.o: $(SRC_DIR)/%.c | $(BIN_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(OBJ_DIR)/gen/%.o: $(GEN_DIR)/%.c | $(BIN_DIR) $(GEN_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(GEN_CFLAGS) $(INCLUDES) -c -o $@ $<

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS)

run: $(TARGET)
	./$(TARGET)

test: check-test-tools $(TARGET)
	pytest -q

test-parser-lexer: check-test-tools $(TARGET)
	pytest -q tests/parser_lexer

test-runtime: check-test-tools $(TARGET)
	pytest -q tests/runtime

help:
	@echo "Targets:"
	@echo "  make / make all    Build the interpreter"
	@echo "  make run           Run the interpreter"
	@echo "  make test          Run Python tests"
	@echo "  make test-parser-lexer  Run parser/lexer-focused tests"
	@echo "  make test-runtime       Run runtime-focused tests"
	@echo "  make clean         Remove build output"
	@echo "  make clean-all     Alias for clean"

clean:
	rm -rf $(BIN_DIR)

clean-all: clean
