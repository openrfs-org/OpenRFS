# SPDX-License-Identifier: GPL-3.0-only
#
# The C desktop, and the harness that renders it.
#
#     make -C tools/c        build
#     make -C tools/c run    build, render frames into build/c
#
# TWO SETS OF FLAGS, AND THE DIFFERENCE MATTERS.  The shell's own sources
# are built FREESTANDING - no libc, no floating point - because that is
# what they will be built as on the metal, and a warning that only appears
# there is a warning nobody sees.  The harness is an ordinary host program
# and is built as one.
ROOT := ..
BUILD := $(ROOT)/build

SHELL_FLAGS := -std=c11 -ffreestanding -nostdlib -msoft-float \
	-Wall -Wextra -Werror -Wpedantic -Wshadow -Wundef \
	-Wstrict-prototypes -Wmissing-prototypes \
	-I$(ROOT)/include -I$(ROOT)/src
HOST_FLAGS := -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -g \
	-Wall -Wextra -Werror -Wpedantic -Wshadow -Wundef \
	-Wstrict-prototypes -Wmissing-prototypes \
	-I$(ROOT)/include -I$(ROOT)/src -I.

SHELL_SOURCES := $(ROOT)/src/surface.c $(ROOT)/src/oxygen.c $(ROOT)/src/space.c $(ROOT)/src/panel.c $(ROOT)/src/theme.c $(ROOT)/src/font.c \
	$(ROOT)/src/window.c $(ROOT)/src/taskmgr.c $(ROOT)/src/settings.c $(ROOT)/src/files.c $(ROOT)/src/terminal.c \
	$(ROOT)/src/menu.c $(ROOT)/src/packages.c $(ROOT)/src/shell.c
HOST_SOURCES := png.c main.c

# Every object depends on every header, generated ones included.
#
# It is blunt and it is right. The generated art and font tables are
# #included by name, so a regenerated icon set changes no .c file and
# make has nothing to notice - the desktop rendered the old icons out of
# a stale files.o for a full round of this work before anyone looked.
HEADERS := $(wildcard $(ROOT)/include/rsd_desktop/*.h) $(wildcard $(ROOT)/src/*.h)
ifeq ($(HEADERS),)
$(error $$(HEADERS) is empty: no headers found under $(ROOT))
endif

SHELL_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SHELL_SOURCES)))
HOST_OBJECTS := $(patsubst %.c,$(BUILD)/%.o,$(HOST_SOURCES))
BINARY := $(BUILD)/rsd

all: $(BINARY)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: $(ROOT)/src/%.c $(HEADERS) | $(BUILD)
	$(CC) $(SHELL_FLAGS) -c $< -o $@

$(BUILD)/%.o: %.c $(HEADERS) | $(BUILD)
	$(CC) $(HOST_FLAGS) -c $< -o $@

$(BINARY): $(SHELL_OBJECTS) $(HOST_OBJECTS)
	$(CC) $(HOST_FLAGS) $^ -o $@

run: $(BINARY)
	cd $(ROOT) && ./build/rsd build

clean:
	rm -rf $(BUILD)

.PHONY: all run clean
