# SPDX-License-Identifier: ISC
# SPDX-FileCopyrightText: Copyright © 2023–2026 Lucca M. A. Pellegrini <lucca@verticordia.com>

## Project configuration
NAME    ?= airnet-gnn

# Tools
CC      := musl-clang

# Directories
SRC_DIR := src
INC_DIR := include
OUT_DIR := build
OBJ_DIR := $(OUT_DIR)/obj
DEP_DIR := $(OUT_DIR)/dep
GEN_DIR := $(OUT_DIR)/gen

# Dataset paths
DATA_DIR := openflights-datasets/data

# GNN Python module
GNN_DIR  := gnn
GNN_VENV := $(OUT_DIR)/venv
GNN_BIN  := $(GNN_VENV)/bin


## Tools configuration

# Compiler flags
CFLAGS     += -std=c23 -Wall -Wextra -Wpedantic -O3 -D_GNU_SOURCE
DEPFLAGS   := -MMD -MP
LDFLAGS    += -flto=full -lm

# Use static linking if compiler is musl
ifneq ($(findstring musl-,$(CC)),)
    LDFLAGS += -static
endif

# Ignore unused arguments when using a clang-like compiler
CC_VERSION := $(shell $(CC) --version 2>/dev/null)
ifneq ($(findstring clang,$(CC_VERSION)),)
    CFLAGS += -Wno-unused-command-line-argument
endif

# Debug mode
ifndef NODEBUG
    CFLAGS += -g -DDEBUG --debug
endif
ifdef NOOPTIMIZE
    CFLAGS += -O0
endif

# Hand-written sources
SRCS     := $(wildcard $(SRC_DIR)/*.c)
OBJS     := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

# Generated header
GEN_H    := $(GEN_DIR)/graph_data.h

# Dependency lists
DEPS := $(patsubst $(OBJ_DIR)/%.o,$(DEP_DIR)/%.d,$(OBJS))

# Final binary
BIN      := $(OUT_DIR)/$(NAME)

# Platform-specific
ifneq ($(OS),Windows_NT)
    MKDIR := mkdir -p
    RMDIR := rm -rf
else
    MKDIR := mkdir
    RMDIR := rmdir /s /q
endif

# ccache support
ifeq ($(shell which ccache 2>/dev/null),)
    CC_WRAPPER :=
else
    CC_WRAPPER := ccache
endif

# compiledb (clangd) support
ifeq ($(shell which compiledb 2>/dev/null),)
    COMPILE_COMMANDS_JSON :=
else
    COMPILE_COMMANDS_JSON := $(OUT_DIR)/compile_commands.json
endif

# Default target
all: $(COMPILE_COMMANDS_JSON) $(BIN)
bin: $(BIN)

# List targets
help:
	@echo "airnet-gnn — Airline Network Resilience Analysis"
	@echo
	@echo "Usage: make [<OPTION>=<VALUE>...] [<TARGET>]"
	@echo
	@echo "Possible targets:"
	@echo "  all           Build the binary at ‘./<OUT_DIR>/airnet-gnn’."
	@echo "  preprocess    Generate ‘build/gen/graph_data.h’ + CSR from OpenFlights."
	@echo "  clean         Clean up all build artifacts."
	@echo "  run           Run classical analysis with compiled-in graph."
	@echo "  run-csr       Run classical analysis with CSR-loaded graph."
	@echo "  gnn-env       Create Python venv with PyTorch Geometric (uv)."
	@echo "  gnn-graph     Generate random graphs (ER, BA, WS) for training."
	@echo "  gnn-run       Run full GNN comparison pipeline."
	@echo "  compare       Full pipeline: preprocess + classical + GNN comparison."
	@echo "  help          Display this help page."
	@echo
	@echo "Options:"
	@echo "  CC            What C compiler to use. Defaults to ‘musl-clang’."
	@echo "  NODEBUG       If set, debug symbols are omitted."
	@echo "  NOOPTIMIZE    If set, builds with ‘-O0’ (default is ‘-O3’)."
	@echo "  OUT_DIR       Directory where all build artifacts are stored."
	@echo "                  Defaults to ‘build/’"
	@echo "  SRC_DIR       Directory containing .c source files."
	@echo "  INC_DIR       Directory containing header files."


## Generated graph data header and CSR binary for Python

CSR_DIR := $(GEN_DIR)/csr/openflights

$(GEN_DIR)/graph_data.h $(CSR_DIR)/graph.meta: scripts/preprocess.pl $(DATA_DIR)/airports.dat $(DATA_DIR)/routes.dat | $(GEN_DIR)
	perl $< --csr-out $(CSR_DIR) $(DATA_DIR) > $(GEN_DIR)/graph_data.h

preprocess: $(GEN_H) $(CSR_DIR)/graph.meta


## Normal compilation rules

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(GEN_H) | $(OBJ_DIR) $(DEP_DIR)
	$(CC_WRAPPER) $(CC) -iquote $(INC_DIR) -iquote $(GEN_DIR) $(CFLAGS) \
		$(DEPFLAGS) -MF $(DEP_DIR)/$*.d -MT $@ -c $< -o $@

# Directory creation
$(OUT_DIR) $(OBJ_DIR) $(DEP_DIR) $(GEN_DIR):
	$(MKDIR) $@

# Link the final executable
$(BIN): $(OBJS) | $(OUT_DIR)
	$(CC_WRAPPER) $(CC) $(LDFLAGS) $^ -o $@

# Create compile_commands.json for clangd
$(COMPILE_COMMANDS_JSON): Makefile | $(OUT_DIR)
	compiledb -nfo $@ $(MAKE) bin CC=clang

# Remove all artifacts
clean:
	$(RM) $(BIN)
	$(RMDIR) $(OUT_DIR)

run: $(BIN)
	$<

run-csr: $(BIN) $(CSR_DIR)/graph.meta
	$< --graph $(CSR_DIR)


## GNN targets

$(GNN_DIR)/requirements.txt:
	@echo "No requirements.txt found in $(GNN_DIR)" >&2; exit 1

$(GNN_VENV)/pyvenv.cfg: $(GNN_DIR)/requirements.txt | $(OUT_DIR)
	test -d $(GNN_VENV) || uv venv $(GNN_VENV)
	. $(GNN_BIN)/activate && uv pip install -r $(GNN_DIR)/requirements.txt

gnn-env: $(GNN_VENV)/pyvenv.cfg

gnn-graph: $(GNN_VENV)/pyvenv.cfg
	. $(GNN_BIN)/activate && uv run python $(GNN_DIR)/generate_graph.py $(GEN_DIR)/csr

CLASSICAL_CSV := $(OUT_DIR)/classical_scores.csv

$(CLASSICAL_CSV): $(BIN) $(CSR_DIR)/graph.meta
	$(BIN) --graph $(CSR_DIR) -o $(CLASSICAL_CSV)

gnn-run: $(GNN_VENV)/pyvenv.cfg $(CLASSICAL_CSV)
	. $(GNN_BIN)/activate && uv run python $(GNN_DIR)/run.py $(CSR_DIR) \
		--classical-csv $(CLASSICAL_CSV)

# Full compare pipeline: preprocess, build classical binary, run comparison
compare: preprocess $(BIN) gnn-run

.PHONY: all bin help clean preprocess run run-csr gnn-env gnn-graph gnn-run compare

-include $(DEPS)
