# Variables
SRC := writer.c
TARGET := writer
OBJS := $(SRC:.c=.o)

# Cross-compiler support
# If CROSS_COMPILE is specified, it prefixes gcc (e.g., aarch64-none-linux-gnu-gcc)
# If not specified, it just uses gcc
CC := $(CROSS_COMPILE)gcc
CFLAGS := -Wall -Werror

# Default target
all: $(TARGET)

# Link the object files to create the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@

# Compile source files to object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean target
clean:
	rm -f $(TARGET) *.o

.PHONY: all clean