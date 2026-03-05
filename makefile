CC = gcc
CFLAGS = -Wall -Wextra -std=c11

SRC_DIR = src
CORE_DIR = $(SRC_DIR)/core
GRAMMAR_DIR = $(SRC_DIR)/grammar
BIN_DIR = bin
GEN_DIR = $(BIN_DIR)/generated

PARSER_SRC = $(GRAMMAR_DIR)/parser.y
LEXER_SRC = $(GRAMMAR_DIR)/lexer.l

PARSER_C = $(GEN_DIR)/parser.tab.c
PARSER_H = $(GEN_DIR)/parser.tab.h
LEXER_C = $(GEN_DIR)/lexer.yy.c

TARGET = $(BIN_DIR)/calc
SOURCES = $(CORE_DIR)/main.c $(CORE_DIR)/calc_runtime.c $(PARSER_C) $(LEXER_C)

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
	$(CC) $(CFLAGS) -I$(CORE_DIR) -I$(SRC_DIR) -I$(GEN_DIR) -o $(TARGET) $(SOURCES)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BIN_DIR)/*
