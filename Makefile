CC = gcc
CFLAGS = -Wall -Wextra -Iinclude -O2
LDFLAGS = 

SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin

VM_SRCS = $(SRC_DIR)/vm.c
COMPILER_SRCS = $(SRC_DIR)/lexer.c $(SRC_DIR)/compiler.c

NUX_SRCS = $(VM_SRCS) $(SRC_DIR)/nux.c
LUXC_SRCS = $(VM_SRCS) $(COMPILER_SRCS) $(SRC_DIR)/luxc.c
REPL_SRCS = $(VM_SRCS) $(COMPILER_SRCS) $(SRC_DIR)/repl.c

NUX_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(NUX_SRCS))
LUXC_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(LUXC_SRCS))
REPL_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(REPL_SRCS))

TARGETS = $(BIN_DIR)/nux $(BIN_DIR)/luxc $(BIN_DIR)/luxrepl

all: dir $(TARGETS)

dir:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)

$(BIN_DIR)/nux: $(NUX_OBJS)
	@echo "Linking nux..."
	@$(CC) $(NUX_OBJS) -o $@ $(LDFLAGS)
	@echo "Built bin/nux successfully!"

$(BIN_DIR)/luxc: $(LUXC_OBJS)
	@echo "Linking luxc..."
	@$(CC) $(LUXC_OBJS) -o $@ $(LDFLAGS)
	@echo "Built bin/luxc successfully!"

$(BIN_DIR)/luxrepl: $(REPL_OBJS)
	@echo "Linking luxrepl..."
	@$(CC) $(REPL_OBJS) -o $@ $(LDFLAGS)
	@echo "Built bin/luxrepl successfully!"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<..."
	@$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean dir
