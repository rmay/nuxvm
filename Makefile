CC = gcc
CFLAGS = -Wall -Wextra -Iinclude -O2 -g $(shell pkg-config --cflags sdl2)
LDFLAGS = $(shell pkg-config --libs sdl2)

SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin

VM_SRCS = $(SRC_DIR)/vm.c
SYS_SRCS = $(SRC_DIR)/system.c $(SRC_DIR)/machine.c $(SRC_DIR)/display.c
COMPILER_SRCS = $(SRC_DIR)/lexer.c $(SRC_DIR)/compiler.c

COMPILER_OBJS = $(OBJ_DIR)/lexer.o $(OBJ_DIR)/compiler.o
NUX_SRCS = $(VM_SRCS) $(SYS_SRCS) $(SRC_DIR)/vfs.c $(SRC_DIR)/nux.c
LUXC_SRCS = $(VM_SRCS) $(SYS_SRCS) $(SRC_DIR)/vfs.c $(COMPILER_SRCS) $(SRC_DIR)/luxc.c
REPL_SRCS = $(VM_SRCS) $(SYS_SRCS) $(SRC_DIR)/vfs.c $(COMPILER_SRCS) $(SRC_DIR)/repl.c

NUX_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(NUX_SRCS))
LUXC_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(LUXC_SRCS))
REPL_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(REPL_SRCS))

CLOISTER_SRCS = $(VM_SRCS) $(SYS_SRCS) $(SRC_DIR)/vfs.c $(COMPILER_SRCS) $(SRC_DIR)/cloister.c
CLOISTER_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(CLOISTER_SRCS))

TARGETS = $(BIN_DIR)/nux $(BIN_DIR)/luxc $(BIN_DIR)/luxrepl $(BIN_DIR)/cloister $(BIN_DIR)/test_vfs $(BIN_DIR)/test_vm $(BIN_DIR)/test_compiler

all: dir $(TARGETS)

dir:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)

$(BIN_DIR)/nux: $(NUX_OBJS) $(COMPILER_OBJS)
	@echo "Linking nux..."
	$(CC) $(NUX_OBJS) $(COMPILER_OBJS) -o $@ $(LDFLAGS)
	@echo "Built bin/nux successfully!"

$(BIN_DIR)/luxc: $(LUXC_OBJS)
	@echo "Linking luxc..."
	$(CC) $(LUXC_OBJS) -o $@ $(LDFLAGS)
	@echo "Built bin/luxc successfully!"

$(BIN_DIR)/luxrepl: $(REPL_OBJS)
	@echo "Linking luxrepl..."
	$(CC) $(REPL_OBJS) -o $@ $(LDFLAGS)
	@echo "Built bin/luxrepl successfully!"

$(BIN_DIR)/cloister: $(CLOISTER_OBJS)
	@echo "Linking cloister..."
	$(CC) $(CLOISTER_OBJS) -o $@ $(LDFLAGS)
	@echo "Built bin/cloister successfully!"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/test_vfs: $(OBJ_DIR)/test_vfs.o $(OBJ_DIR)/vfs.o $(OBJ_DIR)/system.o $(OBJ_DIR)/machine.o $(OBJ_DIR)/vm.o $(COMPILER_OBJS)
	@echo "Linking test_vfs..."
	$(CC) $(OBJ_DIR)/test_vfs.o $(OBJ_DIR)/vfs.o $(OBJ_DIR)/system.o $(OBJ_DIR)/machine.o $(OBJ_DIR)/vm.o $(COMPILER_OBJS) -o $@ $(LDFLAGS)
	@echo "Built bin/test_vfs successfully!"

$(BIN_DIR)/test_vm: $(OBJ_DIR)/test_vm.o $(OBJ_DIR)/vm.o $(OBJ_DIR)/vfs.o $(OBJ_DIR)/system.o $(OBJ_DIR)/machine.o $(OBJ_DIR)/display.o $(COMPILER_OBJS)
	@echo "Linking test_vm..."
	$(CC) $(OBJ_DIR)/test_vm.o $(OBJ_DIR)/vm.o $(OBJ_DIR)/vfs.o $(OBJ_DIR)/system.o $(OBJ_DIR)/machine.o $(OBJ_DIR)/display.o $(COMPILER_OBJS) -o $@ $(LDFLAGS)
	@echo "Built bin/test_vm successfully!"

$(BIN_DIR)/test_compiler: $(OBJ_DIR)/test_compiler.o $(OBJ_DIR)/compiler.o $(OBJ_DIR)/lexer.o $(OBJ_DIR)/vm.o $(OBJ_DIR)/vfs.o $(OBJ_DIR)/system.o $(OBJ_DIR)/machine.o $(OBJ_DIR)/display.o
	@echo "Linking test_compiler..."
	$(CC) $(OBJ_DIR)/test_compiler.o $(OBJ_DIR)/compiler.o $(OBJ_DIR)/lexer.o $(OBJ_DIR)/vm.o $(OBJ_DIR)/vfs.o $(OBJ_DIR)/system.o $(OBJ_DIR)/machine.o $(OBJ_DIR)/display.o -o $@ $(LDFLAGS)
	@echo "Built bin/test_compiler successfully!"

test: $(BIN_DIR)/test_vfs $(BIN_DIR)/test_vm $(BIN_DIR)/test_compiler
	@echo "Running VFS tests..."
	@./$(BIN_DIR)/test_vfs
	@echo "Running VM opcode tests..."
	@./$(BIN_DIR)/test_vm
	@echo "Running Compiler tests..."
	@./$(BIN_DIR)/test_compiler

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

.PHONY: all clean dir test
