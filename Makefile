CC = gcc
# -MMD -MP: emit a obj/%.d dependency file per translation unit, listing every
# header it #includes. Without this, `make`'s dependency graph only knows
# about %.c -> %.o (the pattern rule below), so editing a shared header
# (e.g. include/fluxio_ast.h) with an unrelated .c file's mtime untouched
# leaves that file's .o stale after an incremental `make` -- silently
# linking mismatched struct layouts across translation units. Caught this
# for real during Phase B5 (docs/quill_fluxio.md): an incremental build
# after editing fluxio_ast.h produced an intermittently-crashing
# test_fluxio_compiler that a full rebuild (`make clean && make`) fixed --
# exactly this class of bug. The -include below wires the generated .d
# files back into the dependency graph so incremental builds are safe.
CFLAGS = -Wall -Wextra -Iinclude -O2 -g -MMD -MP
# SDL is only for Cloister (src/cloister.c, src/dialog.c). Headless tools
# must not link it -- that was most of nux's process RSS.
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)

SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin

VM_SRCS = $(SRC_DIR)/vm.c
SYS_SRCS = $(SRC_DIR)/system.c $(SRC_DIR)/machine.c $(SRC_DIR)/display.c
COMPILER_SRCS = $(SRC_DIR)/lexer.c $(SRC_DIR)/compiler.c

COMPILER_OBJS = $(OBJ_DIR)/lexer.o $(OBJ_DIR)/compiler.o
ROM_SRCS = $(SRC_DIR)/sha256.c $(SRC_DIR)/rom.c
ROM_OBJS = $(OBJ_DIR)/sha256.o $(OBJ_DIR)/rom.o

NUX_SRCS = $(VM_SRCS) $(SYS_SRCS) $(SRC_DIR)/vfs.c $(SRC_DIR)/nux.c $(SRC_DIR)/fonts_stub.c $(ROM_SRCS)
LUXC_SRCS = $(VM_SRCS) $(SYS_SRCS) $(SRC_DIR)/vfs.c $(COMPILER_SRCS) $(SRC_DIR)/luxc.c $(SRC_DIR)/fonts_stub.c $(ROM_SRCS)
REPL_SRCS = $(VM_SRCS) $(SYS_SRCS) $(SRC_DIR)/vfs.c $(COMPILER_SRCS) $(SRC_DIR)/repl.c $(SRC_DIR)/fonts_stub.c $(ROM_SRCS)

NUX_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(NUX_SRCS))
LUXC_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(LUXC_SRCS))
REPL_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(REPL_SRCS))

FLUXIO_COMPILER_SRCS = $(SRC_DIR)/fluxio_lexer.c $(SRC_DIR)/fluxio_ast.c \
                       $(SRC_DIR)/fluxio_parser.c $(SRC_DIR)/fluxio_codegen.c $(SRC_DIR)/fluxio_include.c
FLUXIO_COMPILER_OBJS = $(OBJ_DIR)/fluxio_lexer.o $(OBJ_DIR)/fluxio_ast.o \
                       $(OBJ_DIR)/fluxio_parser.o $(OBJ_DIR)/fluxio_codegen.o $(OBJ_DIR)/fluxio_include.o

# cloister links both front ends plus the fluxlink core, so it can compile
# (and, for an app with externs, link) a .lux or .fx app on the fly at launch.
CLOISTER_SRCS = $(VM_SRCS) $(SYS_SRCS) $(SRC_DIR)/vfs.c $(COMPILER_SRCS) $(FLUXIO_COMPILER_SRCS) \
                $(SRC_DIR)/fluxlink_core.c $(SRC_DIR)/fluxio_build.c $(SRC_DIR)/dialog.c $(SRC_DIR)/cloister.c $(SRC_DIR)/fonts.c $(ROM_SRCS)
CLOISTER_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(CLOISTER_SRCS))


FLUXIOC_SRCS = $(VM_SRCS) $(SYS_SRCS) $(SRC_DIR)/vfs.c $(COMPILER_SRCS) $(FLUXIO_COMPILER_SRCS) $(SRC_DIR)/fluxioc.c $(SRC_DIR)/fonts_stub.c $(ROM_SRCS)
FLUXIOC_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(FLUXIOC_SRCS))

TARGETS = $(BIN_DIR)/nux $(BIN_DIR)/luxc $(BIN_DIR)/luxrepl $(BIN_DIR)/cloister $(BIN_DIR)/fluxioc $(BIN_DIR)/fluxlink \
          $(BIN_DIR)/test_vfs $(BIN_DIR)/test_vm $(BIN_DIR)/test_compiler $(BIN_DIR)/test_fluxio_compiler \
          $(BIN_DIR)/test_abi_conformance $(BIN_DIR)/test_rom

APP_LUX = $(wildcard apps/*.lux)
APP_LUX_BINS = $(APP_LUX:.lux=.bin)
APP_FX = $(wildcard apps/fluxio/*.fx)
APP_FX_BINS = $(APP_FX:.fx=.bin)
APP_BINS = $(APP_LUX_BINS) $(APP_FX_BINS)

all: dir $(TARGETS) apps

# Rebuild guest ROMs for the graphical base address (0x600000).
# Lux: apps/*.lux -> apps/*.bin (picker still compile-on-runs the .lux).
# Fluxio: apps/fluxio/*.fx -> apps/fluxio/*.bin.
# `make all` depends on this target so both stay in sync.
apps: $(BIN_DIR)/luxc $(BIN_DIR)/fluxioc $(APP_BINS)

apps/%.bin: apps/%.lux $(BIN_DIR)/luxc $(wildcard lib/*.lux)
	@echo "Compiling $< -> $@"
	@$(BIN_DIR)/luxc -target graphical -o $@ $<

# The lib/*.fx dependency mirrors the Lux rule's lib/*.lux one: Fluxio
# apps `include` library sources (lib/escape_menu.fx), and without it an
# edit to the library left every app's .bin stale.
apps/fluxio/%.bin: apps/fluxio/%.fx $(BIN_DIR)/fluxioc $(wildcard lib/*.fx)
	@echo "Compiling $< -> $@"
	@$(BIN_DIR)/fluxioc -target graphical -o $@ $<

# MM_ABI_LIBRARY_CODE_BASE / MM_ABI_LIBRARY_LINK_BASE (include/memory_map.h)
# -- keep in sync. LINK is the trampoline table's base (fluxlink --lib-base);
# BASE is where the library's own code starts (luxc -base), fixed at
# LINK + MM_ABI_TRAMPOLINE_RESERVE regardless of export count (Phase 0.5).
UI_LIB_LINK_BASE = 0x700000
UI_LIB_BASE = 0x701000
GRAPHICAL_BASE = 0x600000

# Compiled Lux UI/SF library for linking into Fluxio apps via fluxlink
# (docs/quill_fluxio.md Phase B7). lib/sf.lux transitively includes
# lib/ui.lux, so one compile covers both modules' exported words.
# lib/uisf.bin and lib/uisf.symtab.json are build artifacts (like every
# other *.bin, gitignored) -- the committed append-only contract is
# abi/uisf.exports.json.
uilib: $(BIN_DIR)/luxc
	@echo "Compiling lib/sf.lux (+ lib/ui.lux) -> lib/uisf.bin"
	@$(BIN_DIR)/luxc -base $(UI_LIB_BASE) -symbols lib/uisf.symtab.json -o lib/uisf.bin lib/sf.lux

# Quill.fx is the first Fluxio app that actually calls into the linked
# UI/SF library (docs/quill_fluxio.md Phase C menus/scrollbar/file-picker)
# -- an explicit rule overrides the generic apps/fluxio/%.bin pattern above
# (GNU make: an explicit target rule always wins over a pattern rule for
# the same target) so this one compile-then-link pipeline runs instead of
# a plain fluxioc compile. $@.app is a scratch intermediate, not shipped.
apps/fluxio/Quill.bin: apps/fluxio/Quill.fx $(BIN_DIR)/fluxioc $(BIN_DIR)/fluxlink $(BIN_DIR)/luxc \
    lib/sf.lux lib/ui.lux abi/uisf.exports.json $(wildcard lib/*.fx)
	@echo "Compiling lib/sf.lux (+ lib/ui.lux) -> lib/uisf.bin"
	@$(BIN_DIR)/luxc -base $(UI_LIB_BASE) -symbols lib/uisf.symtab.json -o lib/uisf.bin lib/sf.lux
	@echo "Compiling $< -> $@.app"
	@$(BIN_DIR)/fluxioc -target graphical -o $@.app apps/fluxio/Quill.fx
	@echo "Linking $@.app + lib/uisf.bin -> $@"
	@$(BIN_DIR)/fluxlink --lib lib/uisf.bin --symtab lib/uisf.symtab.json --exports abi/uisf.exports.json \
	    --app $@.app --app-base $(GRAPHICAL_BASE) --lib-base $(UI_LIB_LINK_BASE) -o $@
	@rm -f $@.app

dir:
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(BIN_DIR)
	@mkdir -p apps/fluxio

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
	$(CC) $(CLOISTER_OBJS) -o $@ $(LDFLAGS) $(SDL_LIBS)
	@echo "Built bin/cloister successfully!"

$(BIN_DIR)/fluxlink: $(OBJ_DIR)/fluxlink.o $(OBJ_DIR)/fluxlink_core.o $(ROM_OBJS)
	@echo "Linking fluxlink..."
	$(CC) $(OBJ_DIR)/fluxlink.o $(OBJ_DIR)/fluxlink_core.o $(ROM_OBJS) -o $@
	@echo "Built bin/fluxlink successfully!"

$(BIN_DIR)/fluxioc: $(FLUXIOC_OBJS)
	@echo "Linking fluxioc..."
	$(CC) $(FLUXIOC_OBJS) -o $@ $(LDFLAGS)
	@echo "Built bin/fluxioc successfully!"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cloister.o: $(SRC_DIR)/cloister.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(OBJ_DIR)/dialog.o: $(SRC_DIR)/dialog.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

FONT_OBJ = $(OBJ_DIR)/fonts.o

$(BIN_DIR)/test_vfs: $(OBJ_DIR)/test_vfs.o $(OBJ_DIR)/vfs.o $(OBJ_DIR)/system.o $(OBJ_DIR)/machine.o $(OBJ_DIR)/vm.o $(COMPILER_OBJS) $(FONT_OBJ) $(ROM_OBJS)
	@echo "Linking test_vfs..."
	$(CC) $(OBJ_DIR)/test_vfs.o $(OBJ_DIR)/vfs.o $(OBJ_DIR)/system.o $(OBJ_DIR)/machine.o $(OBJ_DIR)/vm.o $(COMPILER_OBJS) $(FONT_OBJ) $(ROM_OBJS) -o $@ $(LDFLAGS)
	@echo "Built bin/test_vfs successfully!"

$(BIN_DIR)/test_vm: $(OBJ_DIR)/test_vm.o $(OBJ_DIR)/vm.o $(OBJ_DIR)/vfs.o $(OBJ_DIR)/system.o $(OBJ_DIR)/machine.o $(OBJ_DIR)/display.o $(COMPILER_OBJS) $(FONT_OBJ) $(ROM_OBJS)
	@echo "Linking test_vm..."
	$(CC) $(OBJ_DIR)/test_vm.o $(OBJ_DIR)/vm.o $(OBJ_DIR)/vfs.o $(OBJ_DIR)/system.o $(OBJ_DIR)/machine.o $(OBJ_DIR)/display.o $(COMPILER_OBJS) $(FONT_OBJ) $(ROM_OBJS) -o $@ $(LDFLAGS)
	@echo "Built bin/test_vm successfully!"

$(BIN_DIR)/test_compiler: $(OBJ_DIR)/test_compiler.o $(OBJ_DIR)/compiler.o $(OBJ_DIR)/lexer.o $(OBJ_DIR)/vm.o $(OBJ_DIR)/vfs.o $(OBJ_DIR)/system.o $(OBJ_DIR)/machine.o $(OBJ_DIR)/display.o $(FONT_OBJ) $(ROM_OBJS)
	@echo "Linking test_compiler..."
	$(CC) $(OBJ_DIR)/test_compiler.o $(OBJ_DIR)/compiler.o $(OBJ_DIR)/lexer.o $(OBJ_DIR)/vm.o $(OBJ_DIR)/vfs.o $(OBJ_DIR)/system.o $(OBJ_DIR)/machine.o $(OBJ_DIR)/display.o $(FONT_OBJ) $(ROM_OBJS) -o $@ $(LDFLAGS)
	@echo "Built bin/test_compiler successfully!"

$(BIN_DIR)/test_fluxio_compiler: $(OBJ_DIR)/test_fluxio_compiler.o $(FLUXIO_COMPILER_OBJS) $(COMPILER_OBJS) \
    $(OBJ_DIR)/vm.o $(OBJ_DIR)/vfs.o $(OBJ_DIR)/system.o $(OBJ_DIR)/machine.o $(OBJ_DIR)/display.o $(FONT_OBJ) $(ROM_OBJS)
	@echo "Linking test_fluxio_compiler..."
	$(CC) $(OBJ_DIR)/test_fluxio_compiler.o $(FLUXIO_COMPILER_OBJS) $(COMPILER_OBJS) $(OBJ_DIR)/vm.o $(OBJ_DIR)/vfs.o $(OBJ_DIR)/system.o $(OBJ_DIR)/machine.o $(OBJ_DIR)/display.o $(FONT_OBJ) $(ROM_OBJS) -o $@ $(LDFLAGS)
	@echo "Built bin/test_fluxio_compiler successfully!"

# Also links the Fluxio front end, the fluxlink core and fluxio_build, so the
# suite can check cloister's in-process .fx build against the shipped .bin.
ABI_TEST_OBJS = $(OBJ_DIR)/test_abi_conformance.o $(OBJ_DIR)/compiler.o $(OBJ_DIR)/lexer.o \
                $(OBJ_DIR)/vm.o $(OBJ_DIR)/vfs.o $(OBJ_DIR)/system.o $(OBJ_DIR)/machine.o \
                $(OBJ_DIR)/display.o $(FONT_OBJ) $(FLUXIO_COMPILER_OBJS) \
                $(OBJ_DIR)/fluxlink_core.o $(OBJ_DIR)/fluxio_build.o $(ROM_OBJS)

$(BIN_DIR)/test_abi_conformance: $(ABI_TEST_OBJS)
	@echo "Linking test_abi_conformance..."
	$(CC) $(ABI_TEST_OBJS) -o $@ $(LDFLAGS)
	@echo "Built bin/test_abi_conformance successfully!"

$(BIN_DIR)/test_rom: $(OBJ_DIR)/test_rom.o $(ROM_OBJS)
	@echo "Linking test_rom..."
	$(CC) $(OBJ_DIR)/test_rom.o $(ROM_OBJS) -o $@ $(LDFLAGS)
	@echo "Built bin/test_rom successfully!"

# Host digest of on-disk app images (header + payload). sha256sum on Linux,
# shasum -a 256 on macOS; both -c the same two-column format.
HASHCMD := $(shell if command -v sha256sum >/dev/null 2>&1; then echo sha256sum; else echo "shasum -a 256"; fi)

pin-bins: apps
	$(HASHCMD) $(sort $(APP_BINS)) > abi/app-images.sha256

# Two claims, checked in order:
#   1. every shipped .bin is bit-for-bit reproducible from the source in this
#      tree -- that is what makes the pinned digest mean "compiled from source"
#      rather than "whatever was lying around when someone ran pin-bins";
#   2. the shipped .bin files still match the committed manifest.
# Fluxio apps recompile through fluxioc; Quill.fx additionally links against
# lib/uisf.bin, so it is checked via its own make rule rather than duplicating
# the link line here.
verify-bins: apps $(BIN_DIR)/luxc $(BIN_DIR)/fluxioc
	@set -e; \
	tmp=$$(mktemp -d); \
	trap 'rm -rf $$tmp' EXIT; \
	for src in $(APP_LUX); do \
	    out=$$tmp/$$(basename $$src .lux).bin; \
	    $(BIN_DIR)/luxc -target graphical -o $$out $$src >/dev/null; \
	    cmp $$out $${src%.lux}.bin || { echo "verify-bins: $$src does not reproduce $${src%.lux}.bin"; exit 1; }; \
	done; \
	for src in $(APP_FX); do \
	    case $$src in *Quill.fx) continue;; esac; \
	    out=$$tmp/$$(basename $$src .fx).bin; \
	    $(BIN_DIR)/fluxioc -target graphical -o $$out $$src >/dev/null; \
	    cmp $$out $${src%.fx}.bin || { echo "verify-bins: $$src does not reproduce $${src%.fx}.bin"; exit 1; }; \
	done; \
	echo "All app images reproduce from source."; \
	$(HASHCMD) -c abi/app-images.sha256

test: $(BIN_DIR)/test_vfs $(BIN_DIR)/test_vm $(BIN_DIR)/test_compiler $(BIN_DIR)/test_fluxio_compiler $(BIN_DIR)/test_abi_conformance $(BIN_DIR)/test_rom
	@vfs=FAIL; vm=FAIL; compiler=FAIL; fluxio=FAIL; abi=FAIL; rom=FAIL; bins=FAIL; fail=0; \
	echo "Running VFS tests..."; \
	./$(BIN_DIR)/test_vfs && vfs=PASS || fail=1; \
	echo "Running VM opcode tests..."; \
	./$(BIN_DIR)/test_vm && vm=PASS || fail=1; \
	echo "Running Compiler tests..."; \
	./$(BIN_DIR)/test_compiler && compiler=PASS || fail=1; \
	echo "Running Fluxio compiler tests..."; \
	./$(BIN_DIR)/test_fluxio_compiler && fluxio=PASS || fail=1; \
	echo "Running ABI conformance tests..."; \
	./$(BIN_DIR)/test_abi_conformance && abi=PASS || fail=1; \
	echo "Running ROM header tests..."; \
	./$(BIN_DIR)/test_rom && rom=PASS || fail=1; \
	echo "Verifying app image digests..."; \
	$(MAKE) verify-bins && bins=PASS || fail=1; \
	echo ""; \
	echo "========== Test summary =========="; \
	printf "  %-22s %s\n" "VFS" "$$vfs"; \
	printf "  %-22s %s\n" "VM opcodes" "$$vm"; \
	printf "  %-22s %s\n" "Compiler" "$$compiler"; \
	printf "  %-22s %s\n" "Fluxio compiler" "$$fluxio"; \
	printf "  %-22s %s\n" "ABI conformance" "$$abi"; \
	printf "  %-22s %s\n" "ROM header" "$$rom"; \
	printf "  %-22s %s\n" "App image SHA-256" "$$bins"; \
	echo "----------------------------------"; \
	if [ $$fail -ne 0 ]; then \
		echo " One or more test suites failed."; \
		echo "=================================="; \
		exit 1; \
	fi; \
	echo " All test suites passed."; \
	echo "=================================="

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# Pulls in the per-translation-unit header dependencies emitted by -MMD -MP
# above, so `make` rebuilds a .o when a header it includes changes, not
# just when its own .c changes. Wildcard + -include: silently does nothing
# on a fresh checkout (no .d files yet) rather than erroring.
-include $(wildcard $(OBJ_DIR)/*.d)

asan:
	$(MAKE) clean
	$(MAKE) all CFLAGS="$(CFLAGS) -O1 -fsanitize=address,undefined" LDFLAGS="-fsanitize=address,undefined"

.PHONY: all clean dir test apps uilib asan pin-bins verify-bins
