CC = gcc
CFLAGS = -std=c17 -Wall -Wextra -O2

# Directories
OBJ_DIR = obj
TEST_DIR = tests

# Source files
SOURCES = directory.c diskspace.c protocol.c recv.c send.c
OBJECTS = $(addprefix $(OBJ_DIR)/, $(SOURCES:.c=.o))

# Test files
TEST_SOURCES = $(wildcard $(TEST_DIR)/test_*.c)
TEST_TARGETS = $(TEST_SOURCES:$(TEST_DIR)/%.c=%)

# Targets
all: ncp $(TEST_TARGETS)

ncp: main.c $(OBJECTS)
	$(CC) $(CFLAGS) -I. -o $@ $^

$(TEST_TARGETS): %: $(TEST_DIR)/%.c $(OBJECTS)
	$(CC) $(CFLAGS) -I. -o $@ $^

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
	rm -rf $(OBJ_DIR)/*.o $(TEST_TARGETS) ncp test_dir empty_dir

.PHONY: all test clean
