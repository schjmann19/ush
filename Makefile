CC ?= cc
MUSL_CC ?= musl-gcc

CSTD = -std=c99
POSIX = -D_POSIX_C_SOURCE=200809L

WARN = -Wall -Wextra -Wpedantic \
       -Wshadow \
       -Wstrict-prototypes \
       -Wmissing-prototypes \
       -Wformat=2

CFLAGS = $(CSTD) $(POSIX) $(WARN) -O0 -g
RELEASE_CFLAGS = $(CSTD) $(POSIX) $(WARN) -O2 -DNDEBUG

LDFLAGS ?= -flto

TARGET = ush
OBJECT_DIR = obj

SRCS = src/main.c src/shell.c src/builtins/builtins.c \
       src/builtins/echo.c src/builtins/cd.c src/builtins/exit.c \
       src/builtins/pwd.c src/builtins/export.c src/builtins/unset.c \
       src/builtins/command.c src/builtins/type.c src/builtins/dot.c \
       src/builtins/true.c src/builtins/false.c src/builtins/yes.c \
       src/builtins/alias.c \
       src/builtins/_test_builtin.c

OBJS = $(patsubst src/%.c,$(OBJECT_DIR)/%.o,$(SRCS))

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

$(OBJECT_DIR)/%.o: src/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

release: CFLAGS = $(RELEASE_CFLAGS)
release: clean all

debug: CFLAGS += -fsanitize=address,undefined -O1
debug: LDFLAGS += -fsanitize=address,undefined
debug: clean all

musl: CC = $(MUSL_CC)
musl: clean all

test: $(TARGET)
	./ush < tests/run.sh

clean:
	rm -rf $(OBJECT_DIR) $(TARGET)

cleanobj:
	rm -rf $(OBJECT_DIR)

.PHONY: all clean release debug musl cleanobj