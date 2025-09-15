CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

# Directories
OBJ_DIR = obj
TEST_DIR = tests

# Source files
SOURCES = directory.cpp diskspace.cpp protocol.cpp recv.cpp send.cpp
OBJECTS = $(addprefix $(OBJ_DIR)/, $(SOURCES:.cpp=.o))

# Test files
TEST_SOURCES = $(wildcard $(TEST_DIR)/test_*.cpp)
TEST_TARGETS = $(TEST_SOURCES:$(TEST_DIR)/%.cpp=%)

# Targets
all: ncp $(TEST_TARGETS)

ncp: main.cpp $(OBJECTS)
	$(CXX) $(CXXFLAGS) -I. -o $@ $^

$(TEST_TARGETS): %: $(TEST_DIR)/%.cpp $(OBJECTS)
	$(CXX) $(CXXFLAGS) -I. -o $@ $^

$(OBJ_DIR)/%.o: %.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

test: $(TEST_TARGETS)
	@for test in $(TEST_TARGETS); do \
		echo "Running $$test..."; \
		./$$test || exit 1; \
	done

clean:
	rm -rf $(OBJ_DIR) $(TEST_TARGETS) ncp test_dir empty_dir

.PHONY: all test clean
