CC = gcc
CFLAGS = -std=c17 -Wall -Wextra -O2

# Version information
VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo "dev")
GIT_COMMIT ?= $(shell git rev-parse --short HEAD 2>/dev/null || echo "unknown")
BUILD_DATE ?= $(shell date -u +"%Y-%m-%d_%H:%M:%S_UTC")

# Add version defines to CFLAGS (properly escaped)
VERSION_CFLAGS = -DVERSION='"$(VERSION)"' -DGIT_COMMIT='"$(GIT_COMMIT)"' -DBUILD_DATE='"$(BUILD_DATE)"'

# Directories
OBJ_DIR = obj
TEST_DIR = tests

# Source files
SOURCES = directory.c diskspace.c logging.c protocol.c recv.c send.c
OBJECTS = $(addprefix $(OBJ_DIR)/, $(SOURCES:.c=.o))

# Test files
TEST_SOURCES = $(wildcard $(TEST_DIR)/test_*.c)
TEST_TARGETS = $(TEST_SOURCES:$(TEST_DIR)/%.c=%)

# Targets
all: ncp $(TEST_TARGETS)

ncp: main.c $(OBJECTS)
	$(CC) $(CFLAGS) $(VERSION_CFLAGS) -I. -o $@ $^

$(TEST_TARGETS): %: $(TEST_DIR)/%.c $(OBJECTS)
	$(CC) $(CFLAGS) $(VERSION_CFLAGS) -I. -o $@ $^

$(OBJ_DIR)/%.o: %.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

test: $(TEST_TARGETS)
	@for test in $(TEST_TARGETS); do \
		echo "Running $$test..."; \
		./$$test || exit 1; \
	done

clean:
	rm -rf $(OBJ_DIR)/*.o $(TEST_TARGETS) ncp test_dir empty_dir test_files received_files

.PHONY: all test clean
