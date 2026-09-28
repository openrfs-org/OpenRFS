# Two flag sets on purpose.
#
# src/ is built the way it will be built on the metal: freestanding, no
# libc, no floating point. If it compiles here it compiles there.
#
# tools/ is an ordinary host program. It is the harness, not the shell.

SHELL_CFLAGS = -std=c11 -ffreestanding -nostdlib -msoft-float \
  -Wall -Wextra -Werror -Wpedantic -Wshadow -Wundef \
  -Wstrict-prototypes -Wmissing-prototypes -I../include

HOST_CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L -O2 -g \
  -Wall -Wextra -Werror -Wpedantic -Wshadow -Wundef \
  -Wstrict-prototypes -Wmissing-prototypes -I../include -I.

SHELL_SRC = term sys line parse builtins shell ui config install
SHELL_OBJ = $(addprefix ../build/,$(addsuffix .o,$(SHELL_SRC)))

# Every object depends on every header.
#
# It is blunt, and it is right: the widget struct is shared between the
# toolkit and the installer, and the one time these were not listed, a
# field was widened, half the objects were rebuilt, and the two halves
# of the program disagreed about where the struct ended. It linked. It
# ran. Nothing said a word.
HEADERS = $(wildcard ../include/rsd/*.h)

# An empty HEADERS is not an empty list, it is a missing safety net: the
# rules below would still fire, every object would lose its header
# dependencies, and the next struct change would half-rebuild the tree
# and link two incompatible halves together. That is exactly what
# happened when a rename walked through the wildcard above and pointed
# it at a directory that does not exist.
ifeq ($(HEADERS),)
$(error $$(HEADERS) is empty: no headers under ../include/rsd)
endif

all: ../build/rsd ../build/rsdinstall ../build/film

../build/%.o: ../src/%.c $(HEADERS)
	@mkdir -p ../build
	cc $(SHELL_CFLAGS) -c $< -o $@

../build/png.o: png.c png.h
	@mkdir -p ../build
	cc $(HOST_CFLAGS) -c png.c -o $@

../build/render.o: render.c render.h png.h ../src/rsd_font.h $(HEADERS)
	@mkdir -p ../build
	cc $(HOST_CFLAGS) -c render.c -o $@

../build/main.o: main.c render.h $(HEADERS)
	@mkdir -p ../build
	cc $(HOST_CFLAGS) -c main.c -o $@

../build/install-main.o: install-main.c render.h $(HEADERS)
	@mkdir -p ../build
	cc $(HOST_CFLAGS) -c install-main.c -o $@

../build/film.o: film.c render.h $(HEADERS)
	@mkdir -p ../build
	cc $(HOST_CFLAGS) -c film.c -o $@

HOST_OBJ = ../build/png.o ../build/render.o

../build/rsd: $(SHELL_OBJ) $(HOST_OBJ) ../build/main.o
	cc $(HOST_CFLAGS) $^ -lz -o $@

../build/rsdinstall: $(SHELL_OBJ) $(HOST_OBJ) ../build/install-main.o
	cc $(HOST_CFLAGS) $^ -lz -o $@

../build/film: $(SHELL_OBJ) $(HOST_OBJ) ../build/film.o
	cc $(HOST_CFLAGS) $^ -lz -o $@

# Run the harnesses, and only ever the ones this build just produced.
#
# Running ../build/rsd by hand after a failed compile runs the
# binary from the last build that worked, which reports everything green
# about code that is no longer there. It has happened. Depending on the
# binaries means make will not get here unless they were rebuilt.
check: ../build/rsd ../build/rsdinstall
	@cd ../build && ./rsd
	@cd ../build && ./rsdinstall

clean:
	rm -rf ../build

.PHONY: all check clean
