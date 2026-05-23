# ==============================================================================
# Makefile for hybrid-lock test and fuzzing suite
# ==============================================================================

# Compiler configuration (allow overriding from command line)
CC ?= gcc

# Compilation flags
# -O3: Maximum optimization for performance benchmarking
# -Wall -Wextra: Enable comprehensive compiler warnings
# -std=c11: Enforce standard C11 compliance
# -I./include: Expose the header-only directory to the compiler
CFLAGS += -O3 -Wall -Wextra -std=c11 -I./include

# Linker flags
# -lpthread: Required for multi-threaded execution on Linux systems
LDFLAGS += -lpthread

# Target binary and source definitions
TARGET = lock_fuzz
SRC = tests/lock_fuzz.c
DEPS = include/hybrid_lock.h

# Default target
all: $(TARGET)

# Build rule for the fuzzer/stress binary
$(TARGET): $(SRC) $(DEPS)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LDFLAGS)

# Convenience target to build and immediately execute the test suite
run: $(TARGET)
	./$(TARGET)

# Clean up build artifacts
clean:
	rm -f $(TARGET)

.PHONY: all clean run
