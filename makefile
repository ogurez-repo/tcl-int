CC = gcc
CFLAGS = -Wall -Wextra -std=c11

SRC_DIR = src
CORE_DIR = $(SRC_DIR)/core
RUNTIME_DIR = $(SRC_DIR)/runtime
GRAMMAR_DIR = $(SRC_DIR)/grammar
BIN_DIR = bin
GEN_DIR = $(BIN_DIR)/generated

PARSER_SRC = $(GRAMMAR_DIR)/parser.y
LEXER_SRC = $(GRAMMAR_DIR)/lexer.l

PARSER_C = $(GEN_DIR)/parser.tab.c
PARSER_H = $(GEN_DIR)/parser.tab.h
LEXER_C = $(GEN_DIR)/lexer.yy.c

TARGET = $(BIN_DIR)/tclsh

rwildcard = $(foreach d,$(wildcard $(1)/*),$(call rwildcard,$(d),$(2))) \
    $(filter $(subst *,%,$(2)),$(wildcard $(1)/$(2)))

SRC_SOURCES = $(call rwildcard,$(SRC_DIR),*.c)
GEN_SOURCES = $(PARSER_C) $(LEXER_C)
SOURCES = $(SRC_SOURCES) $(GEN_SOURCES)

.PHONY: all clean run

all: $(TARGET)

$(GEN_DIR):
	mkdir -p $(GEN_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(PARSER_C) $(PARSER_H): $(PARSER_SRC) | $(BIN_DIR) $(GEN_DIR)
	bison -d -y -o $(PARSER_C) $(PARSER_SRC)

$(LEXER_C): $(LEXER_SRC) $(PARSER_H) | $(BIN_DIR) $(GEN_DIR)
	flex -t $(LEXER_SRC) > $(LEXER_C)

$(TARGET): $(SOURCES) | $(BIN_DIR)
	$(CC) $(CFLAGS) -I$(CORE_DIR) -I$(RUNTIME_DIR) -I$(SRC_DIR) -I$(GEN_DIR) -o $(TARGET) $(SOURCES)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BIN_DIR)/*
