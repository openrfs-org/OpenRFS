SHELL := /bin/sh

BUILD_DIR := build
ISO_ROOT := $(BUILD_DIR)/iso-root
KERNEL := $(BUILD_DIR)/openrfs.elf
ISO := $(BUILD_DIR)/openrfs.iso
SERIAL_LOG := $(BUILD_DIR)/serial.log
TEST_BUILD_DIR := $(BUILD_DIR)/tests
TEST_SCENARIOS := normal breakpoint invalid-opcode page-fault ist pit unexpected \
	double-fault apic ioapic ioapic-level retired apic-timer tsc pm-timer \
	pit-retired timers paging heap pci pci-ecam threads thread-guard framebuffer \
	screen keyboard shell surface write-combining device-windows \
	boot-ledger openrfs-proof device-substrate xhci nvme filesystem process \
	linux-abi linux-abi-uname openrfs-proof-userland \
	openrfs-proof-userland-absent openrfs-proof-userland-interactive \
	openrfs-proof-userland-interactive-absent \
	fat32-system fat32-data fat32-nested fat32-growth fat32-random \
	fat32-truncate fat32-rename fat32-delete fat32-full fat32-corrupt \
	fat32-missing fat32-persistence fat32-cache fat32-immutable fat32-handles \
	network-nic-discovery network-nic-initialization network-nic-absent \
	network-link-down network-dhcp network-dhcp-timeout network-static \
	network-arp network-icmp network-icmp-timeout network-udp network-dns-a \
	network-dns-cname network-dns-malformed network-tcp \
	network-tcp-retransmit network-tcp-reset network-http-length \
	network-http-chunked network-http-redirect network-http-malformed \
	network-http-nested network-http-replace network-http-disk-full \
	network-nic-reset network-system-immutable network-missing-linux-echo \
	network-missing-linux-uname network-missing-linux-cat network-files \
	network-notes network-persistence network-socket-isolation \
	network-tcp-listen network-tcp-refused network-native \
	multiprocess multiprocess-slots driver-matrix driver-matrix-builtin audio \
	nvidia nvidia-builtin native native-lua native-sqlite \
	native-rust native-crash native-elf-refusal native-digest-refusal \
	native-abi-refusal native-relaunch native-audio native-sdl native-dynamic \
	native-https native-openrfs
TEST_TARGETS := $(addprefix qemu-test-,$(TEST_SCENARIOS))
EXPECTED_TEST_SCENARIO_COUNT := 115
EXPECTED_SHELL_ASSERTION_COUNT := 459

CC := gcc
LD := ld
NM := nm
OBJCOPY := objcopy
OBJDUMP := objdump
RUSTC := rustc
CARGO := cargo
PYTHON := python3
SDK_CC ?= clang
SDK_AR ?= ar
READELF ?= readelf
FFMPEG ?= ffmpeg
ifeq ($(OS),Windows_NT)
# The MSVC Rust host toolchain collides with MSYS2's `/usr/bin/link.exe`.
# Prefer an installed GNU host toolchain so cargo build scripts and host tests
# use the same MinGW linker as the rest of the Windows development environment.
WINDOWS_RUST_TOOLCHAIN := $(shell rustup toolchain list 2>/dev/null | \
	awk '$$1 == "stable-x86_64-pc-windows-gnu" { print $$1; exit }')
ifneq ($(WINDOWS_RUST_TOOLCHAIN),)
RUSTUP_TOOLCHAIN ?= $(WINDOWS_RUST_TOOLCHAIN)
export RUSTUP_TOOLCHAIN
endif
endif
# The kernel objects and linker must produce ELF. MinGW's default gcc/as pair
# targets PE/COFF on Windows, so use clang's ELF target and lld there while
# leaving the host-test compiler (`CC`) unchanged.
KERNEL_CC = $(CC)
KERNEL_LD = $(LD)
ifeq ($(OS),Windows_NT)
RUST_SYSROOT := $(shell $(RUSTC) --print sysroot | cygpath -u -f -)
RUST_HOST := $(shell $(RUSTC) -vV | sed -n 's/^host: //p')
KERNEL_CC = clang --target=x86_64-unknown-none
KERNEL_LD = $(RUST_SYSROOT)/lib/rustlib/$(RUST_HOST)/bin/rust-lld -flavor gnu
# Use Rust's ELF frontend as a single executable for SDK shell wrappers too.
# The MSYS2 ld.lld installation can select a non-ELF driver on Windows.
SDK_LD ?= $(RUST_SYSROOT)/lib/rustlib/$(RUST_HOST)/bin/gcc-ld/ld.lld
endif
SDK_LD ?= ld.lld
HOST_EXEEXT := $(if $(filter Windows_NT,$(OS)),.exe,)
HOST_SOCKET_LIBS := $(if $(filter Windows_NT,$(OS)),-lws2_32,)
HOST_THREAD_FLAGS := $(if $(filter Windows_NT,$(OS)),,-pthread)
# MSYS2 cannot pass GNU make's descriptor jobserver to native Cargo. Clear
# MAKEFLAGS for Cargo tests on Windows while retaining it on Linux CI.
CARGO_TEST_ENV := $(if $(filter Windows_NT,$(OS)),MAKEFLAGS=,)
QEMU_ACCEL ?= tcg
GRUB_MKRESCUE ?= grub-mkrescue
GRUB_MODULE_DIR ?=
GRUB_MKRESCUE_FLAGS := $(if $(GRUB_MODULE_DIR),-d $(GRUB_MODULE_DIR),)
ifeq ($(OS),Windows_NT)
# The bundled MinGW Python has no AF_UNIX socket family. Use separate loopback
# HMP ports for the interactive QEMU scenarios on Windows.
QEMU_LUA_MONITOR_SOCKET := tcp:127.0.0.1:46201
QEMU_LUA_MONITOR_ARGUMENT := -monitor tcp:127.0.0.1:46201,server=on,wait=off
QEMU_CANVAS_MONITOR_SOCKET := tcp:127.0.0.1:46202
QEMU_CANVAS_MONITOR_ARGUMENT := -monitor tcp:127.0.0.1:46202,server=on,wait=off
QEMU_SDL_MONITOR_SOCKET := tcp:127.0.0.1:46203
QEMU_SDL_MONITOR_ARGUMENT := -monitor tcp:127.0.0.1:46203,server=on,wait=off
else
QEMU_LUA_MONITOR_SOCKET := $(TEST_BUILD_DIR)/native-lua/monitor.sock
QEMU_LUA_MONITOR_ARGUMENT := -monitor unix:$(QEMU_LUA_MONITOR_SOCKET),server=on,wait=off
QEMU_CANVAS_MONITOR_SOCKET := $(TEST_BUILD_DIR)/native-canvas/monitor.sock
QEMU_CANVAS_MONITOR_ARGUMENT := -monitor unix:$(QEMU_CANVAS_MONITOR_SOCKET),server=on,wait=off
QEMU_SDL_MONITOR_SOCKET := $(TEST_BUILD_DIR)/native-sdl/monitor.sock
QEMU_SDL_MONITOR_ARGUMENT := -monitor unix:$(QEMU_SDL_MONITOR_SOCKET),server=on,wait=off
endif
# CI keeps the clean-build contract.  Local loops can skip the clean step after
# one complete run, while independent host-test groups fan out by default.
VERIFY_CLEAN ?= 1
VERIFY_JOBS ?= 2
# Real image tests retain large independent device snapshots.
RUST_TEST_THREADS ?= 2
export RUST_TEST_THREADS

# The one target Rust is built for. It matches the C flags exactly - no MMX, no
# SSE, soft float, no red zone - which is why the two halves can share a stack.
RUST_TARGET := x86_64-unknown-none
RUST_LIB := $(BUILD_DIR)/libopenrfs.a
RUST_FAT16_TEST := $(BUILD_DIR)/fat16-tests$(HOST_EXEEXT)
RUST_FAT32_TEST := $(BUILD_DIR)/fat32-tests$(HOST_EXEEXT)
RUST_LINUX_FAT16_TEST := $(BUILD_DIR)/linux-fat16-tests$(HOST_EXEEXT)
RUST_LINUX_ELF64_TEST := $(BUILD_DIR)/linux-elf64-tests$(HOST_EXEEXT)
RUST_LINUX_UNAME_FAT16_TEST := $(BUILD_DIR)/linux-uname-fat16-tests$(HOST_EXEEXT)
RUST_LINUX_UNAME_ELF64_TEST := $(BUILD_DIR)/linux-uname-elf64-tests$(HOST_EXEEXT)
RUST_LINUX_CAT_FAT16_TEST := $(BUILD_DIR)/linux-cat-fat16-tests$(HOST_EXEEXT)
RUST_LINUX_CAT_ELF64_TEST := $(BUILD_DIR)/linux-cat-elf64-tests$(HOST_EXEEXT)
RUST_ELF64_TEST := $(BUILD_DIR)/elf64-tests$(HOST_EXEEXT)
RUST_DYNAMIC_ELF64_TEST := $(BUILD_DIR)/elf64-dynamic-tests$(HOST_EXEEXT)
RUST_NVBIOS_TEST := $(BUILD_DIR)/nvbios-tests$(HOST_EXEEXT)
RUST_NATIVE_IMAGE_TEST := $(BUILD_DIR)/native-image-tests$(HOST_EXEEXT)
WALL_CLOCK_HOST_TEST := $(TEST_BUILD_DIR)/wall-clock-host-test$(HOST_EXEEXT)
SDK_TIME_HOST_TEST := $(TEST_BUILD_DIR)/sdk-time-host-test$(HOST_EXEEXT)
PACKAGE_STATE_HOST_TEST := $(TEST_BUILD_DIR)/package-state-host-test$(HOST_EXEEXT)
PACKAGE_SERVICE_HOST_TEST := $(TEST_BUILD_DIR)/package-service-host-test$(HOST_EXEEXT)
PACKAGE_MANAGER_HOST_TEST := $(TEST_BUILD_DIR)/package-manager-host-test$(HOST_EXEEXT)
PACKAGE_CONTROL_HOST_TEST := $(TEST_BUILD_DIR)/package-control-host-test$(HOST_EXEEXT)
PACKAGE_TRUST_HOST_TEST := $(TEST_BUILD_DIR)/package-trust-host-test$(HOST_EXEEXT)
PACKAGE_FETCH_HOST_TEST := $(TEST_BUILD_DIR)/package-fetch-host-test$(HOST_EXEEXT)
PACKAGE_UPLOAD_HOST_TEST := $(TEST_BUILD_DIR)/package-upload-host-test$(HOST_EXEEXT)
NATIVE_TEARDOWN_REPORT_HOST_TEST := $(TEST_BUILD_DIR)/native-teardown-report-host-test$(HOST_EXEEXT)
NATIVE_TEARDOWN_HISTORY_HOST_TEST := $(TEST_BUILD_DIR)/native-teardown-history-host-test$(HOST_EXEEXT)
NATIVE_TEARDOWN_DIAGNOSTICS_HOST_TEST := $(TEST_BUILD_DIR)/native-teardown-diagnostics-host-test$(HOST_EXEEXT)
TLS_HOST_TEST := $(TEST_BUILD_DIR)/tls-client-host-test$(HOST_EXEEXT)
TLS_HOST_OBJECT := $(TEST_BUILD_DIR)/tls-client.o
TLS_HOST_WRAPPER_OBJECT := $(TEST_BUILD_DIR)/tls-wrapper.o
HTTPS_HOST_TEST := $(TEST_BUILD_DIR)/https-client-host-test$(HOST_EXEEXT)
HTTPS_HOST_OBJECT := $(TEST_BUILD_DIR)/https-client-host.o
ZLIB_HOST_TEST := $(TEST_BUILD_DIR)/zlib-host-test$(HOST_EXEEXT)
EXT4_FIXTURE := $(TEST_BUILD_DIR)/ext4/openrfs-ext4.raw
EXT4_RECOVERY_FIXTURE := $(TEST_BUILD_DIR)/ext4-recovery/data.raw
RUST_SOURCES := $(wildcard src/rust/*.rs)
RUST_MANIFEST := src/rust/Cargo.toml
RUST_LOCKFILE := src/rust/Cargo.lock
RUST_VENDOR_SOURCES := $(shell find vendor/ext4plus vendor/rust-crates \
	-type f -print 2>/dev/null)
LOGO_CANONICAL_SOURCE := assets/openrfs/logo.png
LOGO_SOURCE := assets/openrfs/logo.png
LOGO_BLOB := $(BUILD_DIR)/logo.srl
LOGO_MAX_DIMENSION := 280
WALLPAPER_SOURCES := assets/openrfs/wallpaper.png
WALLPAPER_BLOB := $(BUILD_DIR)/wallpaper.spw
FONT_SOURCE := tools/font8x16.txt
FONT_BLOB := $(BUILD_DIR)/font.snf
UI_FONT_SOURCE := assets/fonts/inter-ui-atlas.png
UI_FONT_METRICS := assets/fonts/inter-ui-metrics.txt
UI_FONT_LICENSE := assets/fonts/Inter-LICENSE.txt
UI_FONT_TTF := assets/fonts/InterVariable.ttf
UI_FONT_BLOB := $(BUILD_DIR)/ui-font.suf
PACKAGE_TRUST_SPEC ?= platform/package-trust.json
PACKAGE_TRUST_BLOB := $(BUILD_DIR)/package-trust.skt
PACKAGE_TRUST_ASSET_C := $(BUILD_DIR)/package-trust-asset.c
PACKAGE_TRUST_ASSET_OBJECT := $(BUILD_DIR)/package-trust-asset.o
OPENRFS_PROOF_IMAGE := assets/openrfs/proof.png
OPENRFS_PROOF_FOCUS_IMAGE := assets/openrfs/proof-focus.png
OPENRFS_PROOF_TERMINAL_IMAGE := assets/openrfs/proof-terminal.png
OPENRFS_PROOF_CAPTURE_DIR := $(BUILD_DIR)/openrfs-proof-captures
OPENRFS_PROOF_BOOT_VIDEO := assets/openrfs-proof-boot-20s.mp4
OPENRFS_CAPTURE_DIR := $(BUILD_DIR)/openrfs-captures
NETWORK_CAPTURE_DIR := $(BUILD_DIR)/networking-capture
NVME_FIXTURE := $(TEST_BUILD_DIR)/nvme/nvme-fixture.raw
FILESYSTEM_FIXTURE := $(TEST_BUILD_DIR)/filesystem/fat16-fixture.raw
PROCESS_ELF := $(TEST_BUILD_DIR)/process/OPENRFS.BIN
PROCESS_FIXTURE := $(TEST_BUILD_DIR)/process/process-fixture.raw
BUSYBOX_OUTPUT_DIR := $(BUILD_DIR)/busybox-contract
BUSYBOX_WORK_DIR := $(BUILD_DIR)/busybox-work
BUSYBOX_BINARY := $(BUSYBOX_OUTPUT_DIR)/busybox
LINUX_ABI_FIXTURE := $(BUILD_DIR)/fixtures/linux-abi-fat16.raw
BUSYBOX_UNAME_OUTPUT_DIR := $(BUILD_DIR)/busybox-uname-contract
BUSYBOX_UNAME_WORK_DIR := $(BUILD_DIR)/busybox-uname-work
BUSYBOX_UNAME_BINARY := $(BUSYBOX_UNAME_OUTPUT_DIR)/busybox
LINUX_UNAME_FIXTURE := $(BUILD_DIR)/fixtures/linux-uname-fat16.raw
BUSYBOX_CAT_OUTPUT_DIR := $(BUILD_DIR)/busybox-cat-contract
BUSYBOX_CAT_WORK_DIR := $(BUILD_DIR)/busybox-cat-work
BUSYBOX_CAT_BINARY := $(BUSYBOX_CAT_OUTPUT_DIR)/busybox
OPENRFS_PROOF_USERLAND_IMAGE := $(BUILD_DIR)/userspace/openrfs-userland-fat16.raw
OPENRFS_PROOF_USERLAND_NO_CAT_IMAGE := \
	$(BUILD_DIR)/userspace/openrfs-userland-no-cat-fat16.raw
FAT32_SYSTEM_IMAGE := $(BUILD_DIR)/userspace/openrfs-system-fat32.raw
DESKTOP_SYSTEM_IMAGE := $(BUILD_DIR)/userspace/openrfs-desktop-system-fat32.raw
FAT32_DATA_IMAGE := $(BUILD_DIR)/userspace/openrfs-data-fat32.raw
FAT32_RUN_DATA_IMAGE := $(BUILD_DIR)/run-data-fat32.raw
FAT32_FULL_IMAGE := $(BUILD_DIR)/userspace/openrfs-data-full-fat32.raw
FAT32_CORRUPT_IMAGE := $(BUILD_DIR)/userspace/openrfs-data-corrupt-fat32.raw
SDK_BUILD_DIR ?= $(BUILD_DIR)/sdk
SDK_OBJECT_DIR := $(SDK_BUILD_DIR)/obj
SDK_LIB := $(SDK_BUILD_DIR)/lib/libopenrfs.a
SDK_CRT := $(SDK_BUILD_DIR)/lib/crt0.o
SDK_C_SOURCES := $(wildcard sdk/src/*.c)
SDK_ASM_SOURCES := $(wildcard sdk/src/*.S)
SDK_C_OBJECTS := $(patsubst sdk/src/%.c,$(SDK_OBJECT_DIR)/%.o,$(SDK_C_SOURCES))
SDK_ASM_OBJECTS := $(patsubst sdk/src/%.S,$(SDK_OBJECT_DIR)/%.o,$(SDK_ASM_SOURCES))
SDK_OBJECTS := $(SDK_C_OBJECTS) $(SDK_ASM_OBJECTS)
BEARSSL_SOURCE := $(shell find vendor/bearssl/src -name '*.c' -print | \
	LC_ALL=C sort)
BEARSSL_OBJECT_DIR := $(SDK_BUILD_DIR)/bearssl
BEARSSL_OBJECTS := $(patsubst vendor/bearssl/src/%.c,\
	$(BEARSSL_OBJECT_DIR)/%.o,$(BEARSSL_SOURCE))
BEARSSL_LIB := $(SDK_BUILD_DIR)/lib/libbearssl.a
TLS_HOST_BEARSSL_OBJECTS := $(patsubst vendor/bearssl/src/%.c,\
	$(TEST_BUILD_DIR)/bearssl/%.o,$(BEARSSL_SOURCE))
TLS_HOST_BEARSSL_LIB := $(TEST_BUILD_DIR)/libbearssl-host.a
ZLIB_SOURCE := vendor/zlib/src/adler32.c vendor/zlib/src/crc32.c \
	vendor/zlib/src/deflate.c vendor/zlib/src/infback.c \
	vendor/zlib/src/inffast.c vendor/zlib/src/inflate.c \
	vendor/zlib/src/inftrees.c vendor/zlib/src/trees.c \
	vendor/zlib/src/zutil.c
ZLIB_HEADERS := $(wildcard vendor/zlib/include/*.h) \
	$(wildcard vendor/zlib/src/*.h)
ZLIB_OBJECT_DIR := $(SDK_BUILD_DIR)/zlib
ZLIB_OBJECTS := $(patsubst vendor/zlib/src/%.c,\
	$(ZLIB_OBJECT_DIR)/%.o,$(ZLIB_SOURCE))
ZLIB_LIB := $(SDK_BUILD_DIR)/lib/libz.a
ZLIB_DEFINES := -DZ_SOLO -DZ_U4=unsigned -DZ_U8='unsigned long long' \
	-DHAVE_UNISTD_H=0 -DHAVE_STDARG_H=0
SDK_CFLAGS := --target=x86_64-unknown-none-elf -Isdk/include -Iinclude \
	-Ivendor/bearssl/inc -Ivendor/zlib/include -Isdk/src \
	$(ZLIB_DEFINES) -std=c11 -O2 -g -ffreestanding -fno-pie \
	-fno-stack-protector -mcmodel=large -mno-red-zone -fno-builtin \
	-ffunction-sections -fdata-sections -ftls-model=local-exec \
	-Wall -Wextra -Werror -Wpedantic -Wshadow -Wundef \
	-Wstrict-prototypes -Wmissing-prototypes
BEARSSL_CFLAGS := --target=x86_64-unknown-none-elf -Isdk/include -Iinclude \
	-Ivendor/bearssl/inc -Ivendor/bearssl/src -std=c11 -O2 -g -ffreestanding -fno-pie \
	-fno-stack-protector -mcmodel=large -mno-red-zone -fno-builtin \
	-ffunction-sections -fdata-sections \
	-DBR_USE_URANDOM=0 -DBR_USE_WIN32_RAND=0 \
	-DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0 \
	-DBR_SSE2=0 -DBR_AES_X86NI=0 -DBR_POWER8=0 \
	-Wall -Wextra -Werror
ZLIB_CFLAGS := --target=x86_64-unknown-none-elf -Isdk/include -Iinclude \
	-Ivendor/zlib/include -Ivendor/zlib/src $(ZLIB_DEFINES) \
	-std=c11 -O2 -g -ffreestanding -fno-pie -fno-stack-protector \
	-mcmodel=large -mno-red-zone -fno-builtin -ffunction-sections \
	-fdata-sections -Wall -Wextra -Werror -Wpedantic -Wshadow -Wundef \
	-Wstrict-prototypes -Wmissing-prototypes
include ports/sdl2/sources.mk
SDL2_OBJECT_DIR := $(SDK_BUILD_DIR)/sdl2
SDL2_OBJECTS := $(patsubst vendor/sdl2/src/%.c,\
	$(SDL2_OBJECT_DIR)/%.o,$(SDL2_SOURCES))
SDL2_LIB := $(SDK_BUILD_DIR)/lib/libSDL2.a
SDL2_PUBLIC_HEADERS := $(wildcard vendor/sdl2/include/*.h)
SDL2_CFLAGS := --target=x86_64-unknown-none-elf -D__OPENRFS__=1 \
	-Ivendor/sdl2/include -Isdk/include -Iinclude -std=c11 -O2 -g \
	-ffreestanding -fno-pie -fno-stack-protector -mcmodel=large \
	-mno-red-zone -fno-builtin -ffunction-sections -fdata-sections \
	-ftls-model=local-exec -Wall -Wextra -Werror
SDL2_VENDOR_CFLAGS := $(SDL2_CFLAGS) -Wno-sign-compare \
	-Wno-unused-parameter
SDK_LDFLAGS := -nostdlib -static --gc-sections --build-id=none \
	-z max-page-size=0x1000 -z noexecstack --fatal-warnings \
	--orphan-handling=error -T sdk/linker.ld
NATIVE_APP_DIR := $(BUILD_DIR)/native-apps
NATIVE_TEST_APP := $(NATIVE_APP_DIR)/NATIVET.APP
NATIVE_TEST_PACKAGE := $(NATIVE_APP_DIR)/NATIVET.SPK
NATIVE_SYSTEM_IMAGE := $(NATIVE_APP_DIR)/system.raw
NATIVE_DATA_IMAGE := $(NATIVE_APP_DIR)/data.raw
LUA_PORT_DIR := $(BUILD_DIR)/ports/lua
LUA_PORT_WORK_DIR := $(BUILD_DIR)/ports/lua-work
LUA_APP := $(LUA_PORT_DIR)/LUA.APP
LUA_PACKAGE := $(LUA_PORT_DIR)/LUA.SPK
LUA_SYSTEM_IMAGE := $(LUA_PORT_DIR)/system.raw
LUA_EMPTY_DATA_IMAGE := $(LUA_PORT_DIR)/empty-data.raw
LUA_DATA_IMAGE := $(LUA_PORT_DIR)/data.raw
SQLITE_PORT_DIR := $(BUILD_DIR)/ports/sqlite
SQLITE_PORT_WORK_DIR := $(BUILD_DIR)/ports/sqlite-work
SQLITE_APP := $(SQLITE_PORT_DIR)/SQLITE.APP
SQLITE_PACKAGE := $(SQLITE_PORT_DIR)/SQLITE.SPK
SQLITE_SYSTEM_IMAGE := $(SQLITE_PORT_DIR)/system.raw
SQLITE_DATA_IMAGE := $(SQLITE_PORT_DIR)/data.raw
NETAPP_DIR := $(BUILD_DIR)/native-network
NETAPP_APP := $(NETAPP_DIR)/NETAPP.APP
NETAPP_PACKAGE := $(NETAPP_DIR)/NETAPP.SPK
NETAPP_SYSTEM_IMAGE := $(NETAPP_DIR)/system.raw
NETAPP_DATA_IMAGE := $(NETAPP_DIR)/data.raw
HTTPSAPP_DIR := $(BUILD_DIR)/native-https
HTTPSAPP_APP := $(HTTPSAPP_DIR)/HTTPS.APP
HTTPSAPP_PACKAGE := $(HTTPSAPP_DIR)/HTTPSAPP.SPK
HTTPSAPP_SYSTEM_IMAGE := $(HTTPSAPP_DIR)/system.raw
HTTPSAPP_DATA_IMAGE := $(HTTPSAPP_DIR)/data.raw
OPENRFSAPP_DIR := $(BUILD_DIR)/native-openrfs
OPENRFSAPP_APP := $(OPENRFSAPP_DIR)/OPENRFS.APP
OPENRFSAPP_PACKAGE := $(OPENRFSAPP_DIR)/OPENRFS.SPK
OPENRFSAPP_REPAIR_PACKAGE := $(OPENRFSAPP_DIR)/OPENRFSREP.SPK
OPENRFSAPP_SYSTEM_IMAGE := $(OPENRFSAPP_DIR)/system.raw
OPENRFSAPP_DATA_IMAGE := $(OPENRFSAPP_DIR)/data.raw
OPENRFSAPP_REPOSITORY := $(OPENRFSAPP_DIR)/repository/repository.sri
AUDIO_APP_DIR := $(BUILD_DIR)/native-audio
AUDIO_APP := $(AUDIO_APP_DIR)/AUDIO.APP
AUDIO_PACKAGE := $(AUDIO_APP_DIR)/AUDIO.SPK
AUDIO_REFUSAL_PACKAGE := $(AUDIO_APP_DIR)/AUDIONO.SPK
AUDIO_SYSTEM_IMAGE := $(AUDIO_APP_DIR)/system.raw
AUDIO_DATA_IMAGE := $(AUDIO_APP_DIR)/data.raw
SDL_PROOF_DIR := $(BUILD_DIR)/native-sdl
SDL_PROOF_APP := $(SDL_PROOF_DIR)/SDL.APP
SDL_PROOF_PACKAGE := $(SDL_PROOF_DIR)/SDLPROOF.SPK
SDL_PROOF_SYSTEM_IMAGE := $(SDL_PROOF_DIR)/system.raw
SDL_PROOF_DATA_IMAGE := $(SDL_PROOF_DIR)/data.raw
SDL_CHESS_DIR := $(BUILD_DIR)/upstream-sdl-chess
SDL_CHESS_APP := $(SDL_CHESS_DIR)/CHESS.APP
SDL_CHESS_RELEASE_APP := $(SDL_CHESS_DIR)/CHESS-RELEASE.APP
SDL_CHESS_PACKAGE := $(SDL_CHESS_DIR)/SDLCHESS.SPK
DYNAMIC_APP_DIR := $(BUILD_DIR)/native-dynamic
DYNAMIC_ROOT_APP := $(DYNAMIC_APP_DIR)/DYNROOT.APP
DYNAMIC_LIBRARY := $(DYNAMIC_APP_DIR)/DYNLIB.SO
DYNAMIC_CATALOG := $(DYNAMIC_APP_DIR)/DYNROOT.CAT
DYNAMIC_PACKAGE_SPEC := $(DYNAMIC_APP_DIR)/package.json
DYNAMIC_PACKAGE := $(DYNAMIC_APP_DIR)/DYNROOT.SPK
DYNAMIC_SYSTEM_IMAGE := $(DYNAMIC_APP_DIR)/system.raw
DYNAMIC_DATA_IMAGE := $(DYNAMIC_APP_DIR)/data.raw
RUST_APP_DIR := $(BUILD_DIR)/native-rust
RUST_APP_CARGO_TARGET := $(RUST_APP_DIR)/cargo
RUST_APP_SOURCE := $(RUST_APP_CARGO_TARGET)/x86_64-unknown-none/release/openrfs-native-rust-proof
RUST_APP := $(RUST_APP_DIR)/RUST.APP
RUST_APP_PACKAGE := $(RUST_APP_DIR)/RUSTAPP.SPK
RUST_APP_SYSTEM_IMAGE := $(RUST_APP_DIR)/system.raw
RUST_APP_DATA_IMAGE := $(RUST_APP_DIR)/data.raw
CRASH_APP_DIR := $(BUILD_DIR)/native-crash
CRASH_APP := $(CRASH_APP_DIR)/CRASH.APP
CRASH_PACKAGE := $(CRASH_APP_DIR)/CRASH.SPK
CRASH_SYSTEM_IMAGE := $(CRASH_APP_DIR)/system.raw
CRASH_DATA_IMAGE := $(CRASH_APP_DIR)/data.raw
ADMISSION_DIR := $(BUILD_DIR)/native-admission
ADMISSION_SYSTEM_IMAGE := $(ADMISSION_DIR)/system.raw
ADMISSION_DATA_IMAGE := $(ADMISSION_DIR)/data.raw
RUST_APP_FLAGS := -Dwarnings -C panic=abort -C relocation-model=static \
	-C code-model=large -C link-arg=-nostdlib -C link-arg=-static \
	-C link-arg=--gc-sections -C link-arg=--build-id=none \
	-C link-arg=-z -C link-arg=max-page-size=0x1000 \
	-C link-arg=-z -C link-arg=noexecstack -C link-arg=--fatal-warnings \
	-C link-arg=--orphan-handling=error \
	-C link-arg=-T../../sdk/linker.ld

CPPFLAGS := -Iinclude
COMMON_FLAGS := -m64 -g -ffreestanding -fno-pie -fno-stack-protector
CFLAGS := $(COMMON_FLAGS) -std=c11 -O2 -mno-red-zone -mno-mmx -mno-sse \
	-mno-sse2 -msoft-float -fno-tree-vectorize -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -Wall -Wextra -Werror -Wpedantic -Wshadow -Wundef \
	-Wstrict-prototypes -Wmissing-prototypes
ASFLAGS := $(COMMON_FLAGS) -Wa,--fatal-warnings
# --orphan-handling=error is what keeps the two languages honest. A section
# neither linker.ld names nor discards is otherwise placed wherever ld prefers,
# which is how a Rust static library silently opened a gap between data and bss
# the first time one was linked in. Now an unnamed section is a link error.
LDFLAGS := -nostdlib -z max-page-size=0x1000 -z noexecstack --fatal-warnings \
	--orphan-handling=error --build-id=none -T linker.ld \
	-Map=$(BUILD_DIR)/openrfs.map

C_SOURCES := $(wildcard src/kernel/*.c)
C_OBJECTS := $(patsubst src/kernel/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES))
MONOCYPHER_OBJECTS := $(BUILD_DIR)/monocypher/monocypher.o \
	$(BUILD_DIR)/monocypher/monocypher-ed25519.o
MONOCYPHER_HOST_OBJECTS := $(TEST_BUILD_DIR)/monocypher/monocypher.o \
	$(TEST_BUILD_DIR)/monocypher/monocypher-ed25519.o
ASM_SOURCES := $(wildcard src/arch/x86_64/*.S)
ASM_OBJECTS := $(patsubst src/arch/x86_64/%.S,$(BUILD_DIR)/arch_%.o,$(ASM_SOURCES))
# Upstream driver layers. Vendored sources are byte-for-byte upstream and are
# compiled freestanding against their compatibility headers only: -nostdinc
# keeps the host C library out, and the layer's compiler.h is force-included
# exactly as the upstream build force-includes its own.
include ports/ipxe/sources.mk
GCC_FREESTANDING_INCLUDE := $(shell $(CC) -print-file-name=include)
IPXE_OBJECT_DIR := $(BUILD_DIR)/ipxe
IPXE_OBJECTS := $(patsubst %.c,$(IPXE_OBJECT_DIR)/%.o,\
	$(IPXE_VENDOR_SOURCES) $(IPXE_GLUE_SOURCES))
# iPXE's USB stack registers its drivers and processes through linker
# tables, so its objects are partially linked with ports/ipxe/usb-layer.ld,
# which keeps the table sections in order, and every symbol except the
# entry points in ports/ipxe/usb-exports.txt is made local.
IPXE_USB_OBJECTS := $(patsubst %.c,$(IPXE_OBJECT_DIR)/%.o,\
	$(IPXE_USB_VENDOR_SOURCES) $(IPXE_USB_GLUE_SOURCES))
IPXE_USB_LAYER_OBJECT := $(IPXE_OBJECT_DIR)/ipxe-usb-layer.o
IPXE_USB_EXPORTS := ports/ipxe/usb-exports.txt
IPXE_USB_LINKER_SCRIPT := ports/ipxe/usb-layer.ld
IPXE_BASE_CFLAGS := $(COMMON_FLAGS) -std=gnu11 -O2 -mno-red-zone -mno-mmx \
	-mno-sse -mno-sse2 -msoft-float -fno-tree-vectorize -fno-builtin \
	-fno-strict-aliasing -fno-asynchronous-unwind-tables -fno-unwind-tables \
	-nostdinc -isystem $(GCC_FREESTANDING_INCLUDE) -Iports/ipxe/include \
	-Ivendor/ipxe/src/include -Iinclude \
	-include ports/ipxe/include/compiler.h \
	$(if $(IPXE_DEBUG),-DOPENRFS_IPXE_DEBUG_OUTPUT)
# Upstream code keeps upstream's warning profile: every warning iPXE's own
# build treats as an error is an error here too.
IPXE_VENDOR_CFLAGS := $(IPXE_BASE_CFLAGS) -Wall -Werror -Wno-address \
	-Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
	-Wno-array-bounds -Wno-stringop-overflow -Wno-maybe-uninitialized
IPXE_GLUE_CFLAGS := $(IPXE_BASE_CFLAGS) -Wall -Wextra -Werror -Wshadow \
	-Wundef -Wstrict-prototypes -Wmissing-prototypes -Wno-unused-parameter
# The SeaBIOS layer. Its vendored drivers are compiled as SeaBIOS compiles
# them for 32-bit flat mode (-fno-delete-null-pointer-checks, no strict
# aliasing, packed-member addresses allowed), plus the pointer/integer casts
# of that 32-bit code, which OpenRFS makes exact by keeping everything they
# hand a device below 4 GiB. The objects are partially linked into one and
# every symbol except the exported glue entry points is made local, so the
# layer's own malloc, printf and PCI helpers never meet the kernel's.
include ports/seabios/sources.mk
SEABIOS_OBJECT_DIR := $(BUILD_DIR)/seabios
SEABIOS_OBJECTS := $(patsubst %.c,$(SEABIOS_OBJECT_DIR)/%.o,\
	$(SEABIOS_VENDOR_SOURCES) $(SEABIOS_LP64_SOURCES) \
	$(SEABIOS_GLUE_SOURCES))
SEABIOS_LAYER_OBJECT := $(SEABIOS_OBJECT_DIR)/seabios-layer.o
# SeaBIOS's dprintf level; messages at or below it reach the serial console.
SEABIOS_DEBUG_LEVEL ?= 1
SEABIOS_BASE_CFLAGS := $(COMMON_FLAGS) -std=gnu11 -O2 -mno-red-zone -mno-mmx \
	-mno-sse -mno-sse2 -msoft-float -fno-tree-vectorize -fno-strict-aliasing \
	-fno-delete-null-pointer-checks -fno-common \
	-fno-asynchronous-unwind-tables -fno-unwind-tables \
	-nostdinc -isystem $(GCC_FREESTANDING_INCLUDE) -Iports/seabios/include \
	-Ivendor/seabios/src -Iinclude -DCONFIG_DEBUG_LEVEL=$(SEABIOS_DEBUG_LEVEL)
SEABIOS_VENDOR_CFLAGS := $(SEABIOS_BASE_CFLAGS) -Wall -Werror \
	-Wno-address-of-packed-member -Wno-pointer-to-int-cast \
	-Wno-int-to-pointer-cast -Wno-unused-function -Wno-array-bounds \
	-Wno-stringop-overflow -Wno-maybe-uninitialized
SEABIOS_GLUE_CFLAGS := $(SEABIOS_BASE_CFLAGS) -Wall -Wextra -Werror \
	-Wno-unused-parameter -Wno-address-of-packed-member -Wno-sign-compare

# The SeaBIOS VGA drivers: the same environment, with the VGA configuration
# in front of the storage layer's and real-mode segments resolved at run
# time (see ports/seabios/include/farptr.h). One object per card type.
include ports/seavga/sources.mk
SEAVGA_OBJECT_DIR := $(BUILD_DIR)/seavga
SEAVGA_BASE_CFLAGS := $(subst -Iports/seabios/include,-Iports/seavga/include \
	-Iports/seabios/include -Ivendor/seabios/vgasrc,$(SEABIOS_BASE_CFLAGS)) \
	-DOPENRFS_SEABIOS_FAR_SEGMENTS
SEAVGA_VENDOR_CFLAGS := $(SEAVGA_BASE_CFLAGS) -Wall -Werror \
	-Wno-address-of-packed-member -Wno-pointer-to-int-cast \
	-Wno-int-to-pointer-cast -Wno-unused-function -Wno-array-bounds \
	-Wno-stringop-overflow -Wno-maybe-uninitialized
SEAVGA_GLUE_CFLAGS := $(SEAVGA_BASE_CFLAGS) -Wall -Wextra -Werror \
	-Wno-unused-parameter -Wno-address-of-packed-member -Wno-sign-compare
SEAVGA_LIBC_OBJECT := $(SEAVGA_OBJECT_DIR)/libc.o

# The MINIX 3 audio drivers, compiled as MINIX's i386 port compiles them
# (32-bit phys_bytes, negative errno under _SYSTEM) against the small part
# of MINIX's system library ports/minix/include declares. One object per
# driver, each with only its dispatch entry global.
include ports/minix/sources.mk
MINIX_OBJECT_DIR := $(BUILD_DIR)/minix
MINIX_BASE_CFLAGS := $(COMMON_FLAGS) -std=gnu11 -O2 -mno-red-zone -mno-mmx \
	-mno-sse -mno-sse2 -msoft-float -fno-tree-vectorize -fno-strict-aliasing \
	-fno-common -fno-asynchronous-unwind-tables -fno-unwind-tables \
	-nostdinc -isystem $(GCC_FREESTANDING_INCLUDE) -Iports/minix/include \
	-Ivendor/minix/minix/include
MINIX_VENDOR_CFLAGS := $(MINIX_BASE_CFLAGS) -Wall -Werror \
	-Wno-unused-function -Wno-unused-variable
MINIX_GLUE_CFLAGS := $(MINIX_BASE_CFLAGS) -Iinclude -Wall -Wextra -Werror \
	-Wno-unused-parameter -Wno-sign-compare
MINIX_LAYER_OBJECTS := $(patsubst %,$(MINIX_OBJECT_DIR)/minix-%.o,\
	$(MINIX_DRIVERS))
MINIX_OBJECTS := $(foreach driver,$(MINIX_DRIVERS),\
	$(patsubst vendor/%.c,$(MINIX_OBJECT_DIR)/$(driver)/%.o,\
		$(MINIX_$(driver)_SOURCES)) \
	$(MINIX_OBJECT_DIR)/$(driver)/audio_glue.o)
SEAVGA_LAYER_OBJECTS := $(patsubst %,$(SEAVGA_OBJECT_DIR)/seavga-%.o,\
	$(SEAVGA_VARIANTS))
SEAVGA_OBJECTS := $(SEAVGA_LIBC_OBJECT) $(foreach variant,$(SEAVGA_VARIANTS),\
	$(patsubst %,$(SEAVGA_OBJECT_DIR)/$(variant)/%.o,\
		$(SEAVGA_$(variant)_FILES) seavga_glue) \
	$(patsubst %,$(SEAVGA_OBJECT_DIR)/$(variant)/lp64/%.o,\
		$(SEAVGA_$(variant)_LP64_FILES)))

OBJECTS := $(ASM_OBJECTS) $(C_OBJECTS) $(MONOCYPHER_OBJECTS) \
	$(IPXE_OBJECTS) $(IPXE_USB_LAYER_OBJECT) $(SEABIOS_LAYER_OBJECT) \
	$(SEAVGA_LAYER_OBJECTS) $(MINIX_LAYER_OBJECTS) \
	$(PACKAGE_TRUST_ASSET_OBJECT)

MONOCYPHER_CFLAGS := $(COMMON_FLAGS) -std=c11 -O2 -mno-red-zone \
	-mno-mmx -mno-sse -mno-sse2 -msoft-float -fno-tree-vectorize \
	-fno-builtin -fno-asynchronous-unwind-tables -fno-unwind-tables \
	-Wall -Wextra -Werror -Ivendor/monocypher/src \
	-Ivendor/monocypher/src/optional

# Warnings are errors on both sides of the language boundary, and Rust is held
# to the stricter rule that an unsafe operation inside an unsafe function still
# needs its own unsafe block naming why it is sound. The measured 5,136-byte
# FAT chain is the largest Rust aggregate; allowing 1,024 direct stores keeps
# its bounded copies and zeroing inline instead of introducing a GOT-backed
# compiler memory call into the fixed-address kernel. One codegen unit also
# keeps calls between Rust boundary functions direct; the two variable-sized
# font copies use explicit bounded byte operations instead of runtime calls.
RUSTFLAGS := -C panic=abort -C relocation-model=static \
	-C llvm-args=-max-store-memcpy=1024 \
	-C llvm-args=-max-store-memset=1024
DEPENDENCIES := $(C_OBJECTS:.o=.d) $(MONOCYPHER_OBJECTS:.o=.d) \
	$(IPXE_OBJECTS:.o=.d) $(IPXE_USB_OBJECTS:.o=.d) \
	$(SEABIOS_OBJECTS:.o=.d) $(SEAVGA_OBJECTS:.o=.d) \
	$(MINIX_OBJECTS:.o=.d) \
	$(PACKAGE_TRUST_ASSET_OBJECT:.o=.d) $(SDL2_OBJECTS:.o=.d)

# The qemu-test-% scenarios are deliberately absent from .PHONY. GNU Make skips
# implicit and pattern rule search for a phony target, so declaring them phony
# makes every scenario resolve to "nothing to be done" and pass without booting.
# They never create a file of their own name, so they rerun regardless.
.PHONY: all installer-port-test audio-wav-tests capture-boot-video capture-openrfs capture-openrfs-proof capture-networking clean contract-counts contract-scenarios dynamic-elf-tests ext4-images ext4-tests ext4-fsync-test ext4-sparse-truncate-test fat32-images force-package-trust hooks https-tests \
	iso kernel lint native-apps native-audio-proof native-dynamic-proof native-https-proof native-openrfs-proof native-sdl-proof sdl-preference-tests port-tests qemu-port-tests reproducible-sdk run \
	package-control-tests package-fetch-tests package-manager-tests package-repository-tests package-service-tests package-state-tests package-transaction-tests package-trust-asset-tests package-trust-tests package-upload-tests qemu-test-ext4-powercuts screenshot-proof sdk sdk-once smoke tls-tests toolchain verify wall-clock-tests zlib-tests

all: kernel

$(SDK_OBJECT_DIR):
	mkdir -p $@

$(SDK_BUILD_DIR)/lib $(SDK_BUILD_DIR)/include $(SDK_BUILD_DIR)/bin:
	mkdir -p $@

$(SDK_OBJECT_DIR)/%.o: sdk/src/%.c | $(SDK_OBJECT_DIR)
	$(SDK_CC) $(SDK_CFLAGS) -MMD -MP -MT obj/$*.o -c $< -o $@

$(SDK_OBJECT_DIR)/%.o: sdk/src/%.S | $(SDK_OBJECT_DIR)
	$(SDK_CC) --target=x86_64-unknown-none-elf -ffreestanding -fno-pie \
		-mcmodel=large -mno-red-zone -c $< -o $@

$(BEARSSL_OBJECT_DIR)/%.o: vendor/bearssl/src/%.c
	mkdir -p $(dir $@)
	$(SDK_CC) $(BEARSSL_CFLAGS) -c $< -o $@

$(ZLIB_OBJECT_DIR)/%.o: vendor/zlib/src/%.c $(ZLIB_HEADERS)
	mkdir -p $(dir $@)
	$(SDK_CC) $(ZLIB_CFLAGS) -c $< -o $@

$(SDL2_OBJECT_DIR)/%.o: vendor/sdl2/src/%.c $(SDL2_PUBLIC_HEADERS) \
		vendor/sdl2/include/SDL_config_openrfs.h
	mkdir -p $(dir $@)
	$(SDK_CC) $(SDL2_VENDOR_CFLAGS) -MMD -MP -MT sdl2/$*.o -c $< -o $@

$(TEST_BUILD_DIR)/bearssl/%.o: vendor/bearssl/src/%.c
	mkdir -p $(dir $@)
	$(CC) -Ivendor/bearssl/inc -Ivendor/bearssl/src -std=c11 -O2 \
		-DBR_USE_URANDOM=0 -DBR_USE_WIN32_RAND=0 \
		-DBR_USE_UNIX_TIME=0 -DBR_USE_WIN32_TIME=0 \
		-DBR_SSE2=0 -DBR_AES_X86NI=0 -DBR_POWER8=0 \
		-Wall -Wextra -Werror -c $< -o $@

$(TLS_HOST_BEARSSL_LIB): $(TLS_HOST_BEARSSL_OBJECTS)
	$(SDK_AR) rcsD $@ $^

$(SDK_CRT): sdk/crt/start.S | $(SDK_BUILD_DIR)/lib
	$(SDK_CC) --target=x86_64-unknown-none-elf -ffreestanding -fno-pie \
		-mcmodel=large -mno-red-zone -c $< -o $@

$(SDK_LIB): $(SDK_OBJECTS) | $(SDK_BUILD_DIR)/lib
	$(SDK_AR) rcsD $@ $(SDK_OBJECTS)

$(BEARSSL_LIB): $(BEARSSL_OBJECTS) | $(SDK_BUILD_DIR)/lib
	$(SDK_AR) rcsD $@ $(BEARSSL_OBJECTS)

$(ZLIB_LIB): $(ZLIB_OBJECTS) | $(SDK_BUILD_DIR)/lib
	$(SDK_AR) rcsD $@ $(ZLIB_OBJECTS)

$(SDL2_LIB): $(SDL2_OBJECTS) | $(SDK_BUILD_DIR)/lib
	$(SDK_AR) rcsD $@ $(SDL2_OBJECTS)

$(SDK_BUILD_DIR)/.installed: Makefile $(SDK_LIB) $(BEARSSL_LIB) $(ZLIB_LIB) \
		$(SDL2_LIB) $(SDK_CRT) \
		sdk/linker.ld \
		sdk/bin/openrfs-cc $(wildcard sdk/include/*.h) \
		$(wildcard sdk/include/openrfs/*.h) $(wildcard sdk/include/sys/*.h) \
		$(wildcard vendor/bearssl/inc/*.h) \
		$(wildcard vendor/zlib/include/*.h) \
		$(SDL2_PUBLIC_HEADERS) \
		$(wildcard include/openrfs/abi/*.h) \
		include/openrfs/abi.h | $(SDK_BUILD_DIR)/include $(SDK_BUILD_DIR)/bin
	mkdir -p $(SDK_BUILD_DIR)/include/openrfs/abi $(SDK_BUILD_DIR)/include/sys \
		$(SDK_BUILD_DIR)/include/SDL2
	cp sdk/include/*.h $(SDK_BUILD_DIR)/include/
	cp sdk/include/openrfs/*.h $(SDK_BUILD_DIR)/include/openrfs/
	cp sdk/include/sys/*.h $(SDK_BUILD_DIR)/include/sys/
	cp include/openrfs/abi.h $(SDK_BUILD_DIR)/include/openrfs/
	cp include/openrfs/abi/*.h $(SDK_BUILD_DIR)/include/openrfs/abi/
	cp vendor/bearssl/inc/*.h $(SDK_BUILD_DIR)/include/
	cp vendor/zlib/include/*.h $(SDK_BUILD_DIR)/include/
	cp vendor/sdl2/include/*.h $(SDK_BUILD_DIR)/include/SDL2/
	cp sdk/linker.ld $(SDK_BUILD_DIR)/linker.ld
	cp sdk/bin/openrfs-cc $(SDK_BUILD_DIR)/bin/openrfs-cc
	touch $@

sdk-once: $(SDK_BUILD_DIR)/.installed

sdk: sdk-once

reproducible-sdk:
	rm -rf $(BUILD_DIR)/sdk-repro-a $(BUILD_DIR)/sdk-repro-b
	$(MAKE) SDK_BUILD_DIR=$(BUILD_DIR)/sdk-repro-a sdk-once
	$(MAKE) SDK_BUILD_DIR=$(BUILD_DIR)/sdk-repro-b sdk-once
	$(PYTHON) tools/compare-trees.py $(BUILD_DIR)/sdk-repro-a \
		$(BUILD_DIR)/sdk-repro-b

$(NATIVE_APP_DIR):
	mkdir -p $@

$(LUA_PORT_DIR):
	mkdir -p $@

$(SQLITE_PORT_DIR):
	mkdir -p $@

$(NETAPP_DIR):
	mkdir -p $@

$(HTTPSAPP_DIR):
	mkdir -p $@

$(OPENRFSAPP_DIR):
	mkdir -p $@

$(AUDIO_APP_DIR):
	mkdir -p $@

$(SDL_PROOF_DIR):
	mkdir -p $@

$(SDL_CHESS_DIR):
	mkdir -p $@

$(DYNAMIC_APP_DIR):
	mkdir -p $@

$(RUST_APP_DIR):
	mkdir -p $@

$(CRASH_APP_DIR):
	mkdir -p $@

$(ADMISSION_DIR):
	mkdir -p $@

$(NATIVE_APP_DIR)/native-test.o: apps/native-test/main.c \
		$(SDK_BUILD_DIR)/.installed | $(NATIVE_APP_DIR)
	$(SDK_CC) $(SDK_CFLAGS) -c $< -o $@

$(NATIVE_APP_DIR)/native-state.o: apps/native-test/state.S | $(NATIVE_APP_DIR)
	$(SDK_CC) --target=x86_64-unknown-none-elf -ffreestanding -fno-pie \
		-mcmodel=large -mno-red-zone -c $< -o $@

$(NATIVE_TEST_APP): $(NATIVE_APP_DIR)/native-test.o \
		$(NATIVE_APP_DIR)/native-state.o $(SDK_BUILD_DIR)/.installed
	$(SDK_LD) $(SDK_LDFLAGS) -Map=$(NATIVE_APP_DIR)/NATIVET.map \
		-o $@ $(SDK_CRT) $(NATIVE_APP_DIR)/native-test.o \
			$(NATIVE_APP_DIR)/native-state.o $(SDK_LIB)

$(NATIVE_TEST_PACKAGE): $(NATIVE_TEST_APP) apps/native-test/manifest.json \
		apps/native-test/RESOURCE.TXT
	$(PYTHON) tools/openrfs-package.py build \
		--spec apps/native-test/manifest.json --executable $< --output $@

$(CRASH_APP_DIR)/main.o: apps/native-crash/main.c \
		$(SDK_BUILD_DIR)/.installed | $(CRASH_APP_DIR)
	$(SDK_CC) $(SDK_CFLAGS) -c $< -o $@

$(CRASH_APP): $(CRASH_APP_DIR)/main.o $(SDK_BUILD_DIR)/.installed
	$(SDK_LD) $(SDK_LDFLAGS) -Map=$(CRASH_APP_DIR)/CRASH.map \
		-o $@ $(SDK_CRT) $< $(SDK_LIB)

$(CRASH_PACKAGE): $(CRASH_APP) apps/native-crash/manifest.json
	$(PYTHON) tools/openrfs-package.py build \
		--spec apps/native-crash/manifest.json --executable $< --output $@

$(LUA_APP): tools/build-lua-port.sh ports/lua/source/SHA256SUMS \
		ports/lua/source/lua-5.4.7.tar.gz $(SDK_BUILD_DIR)/.installed
	OPENRFS_SDK_CC='$(SDK_CC)' OPENRFS_SDK_LD='$(SDK_LD)' \
		bash tools/build-lua-port.sh $(LUA_PORT_DIR) $(LUA_PORT_WORK_DIR)

$(LUA_PACKAGE): $(LUA_APP) ports/lua/manifest.json
	$(PYTHON) tools/openrfs-package.py build \
		--spec ports/lua/manifest.json --executable $< --output $@

$(LUA_SYSTEM_IMAGE): $(LUA_PACKAGE) tools/openrfs-package.py \
		tools/fat32_image.py
	$(PYTHON) tools/openrfs-package.py install-system \
		--output $@ $(LUA_PACKAGE)

$(LUA_EMPTY_DATA_IMAGE): tools/fat32_image.py | $(LUA_PORT_DIR)
	$(PYTHON) tools/fat32_image.py format data $@

$(LUA_DATA_IMAGE): $(LUA_EMPTY_DATA_IMAGE) ports/lua/SCRIPT.LUA \
		tools/fat32_image.py
	$(PYTHON) tools/fat32_image.py populate-tree $< $@ \
		--file LUA/SCRIPT.LUA=ports/lua/SCRIPT.LUA

$(SQLITE_APP): tools/build-sqlite-port.sh ports/sqlite/main.c \
		ports/sqlite/openrfs_vfs.c ports/sqlite/source/SHA256SUMS \
		ports/sqlite/source/sqlite-amalgamation-3460000.zip \
		$(SDK_BUILD_DIR)/.installed
	OPENRFS_SDK_CC='$(SDK_CC)' OPENRFS_SDK_LD='$(SDK_LD)' \
		PYTHON='$(PYTHON)' bash tools/build-sqlite-port.sh \
		$(SQLITE_PORT_DIR) $(SQLITE_PORT_WORK_DIR)

$(SQLITE_PACKAGE): $(SQLITE_APP) ports/sqlite/manifest.json
	$(PYTHON) tools/openrfs-package.py build \
		--spec ports/sqlite/manifest.json --executable $< --output $@

$(SQLITE_SYSTEM_IMAGE): $(SQLITE_PACKAGE) tools/openrfs-package.py \
		tools/fat32_image.py
	$(PYTHON) tools/openrfs-package.py install-system \
		--output $@ $(SQLITE_PACKAGE)

$(SQLITE_DATA_IMAGE): tools/fat32_image.py | $(SQLITE_PORT_DIR)
	$(PYTHON) tools/fat32_image.py format data $@

$(NETAPP_DIR)/main.o: apps/native-network/main.c \
		$(SDK_BUILD_DIR)/.installed | $(NETAPP_DIR)
	$(SDK_CC) $(SDK_CFLAGS) -c $< -o $@

$(NETAPP_APP): $(NETAPP_DIR)/main.o $(SDK_BUILD_DIR)/.installed
	$(SDK_LD) $(SDK_LDFLAGS) -Map=$(NETAPP_DIR)/NETAPP.map \
		-o $@ $(SDK_CRT) $< $(SDK_LIB)

$(NETAPP_PACKAGE): $(NETAPP_APP) apps/native-network/manifest.json
	$(PYTHON) tools/openrfs-package.py build \
		--spec apps/native-network/manifest.json --executable $< --output $@

$(NETAPP_SYSTEM_IMAGE): $(NETAPP_PACKAGE) tools/openrfs-package.py \
		tools/fat32_image.py
	$(PYTHON) tools/openrfs-package.py install-system \
		--output $@ $(NETAPP_PACKAGE)

$(NETAPP_DATA_IMAGE): tools/fat32_image.py | $(NETAPP_DIR)
	$(PYTHON) tools/fat32_image.py format data $@

$(HTTPSAPP_DIR)/main.o: apps/native-https/main.c \
		apps/native-https/trust_anchor.h $(SDK_BUILD_DIR)/.installed | \
		$(HTTPSAPP_DIR)
	$(SDK_CC) $(SDK_CFLAGS) -c $< -o $@

$(HTTPSAPP_APP): $(HTTPSAPP_DIR)/main.o $(SDK_BUILD_DIR)/.installed
	$(SDK_LD) $(SDK_LDFLAGS) -Map=$(HTTPSAPP_DIR)/HTTPS.map \
		-o $@ $(SDK_CRT) $< $(SDK_LIB) $(BEARSSL_LIB)

$(HTTPSAPP_PACKAGE): $(HTTPSAPP_APP) apps/native-https/manifest.json
	$(PYTHON) tools/openrfs-package.py build \
		--spec apps/native-https/manifest.json --executable $< --output $@

$(HTTPSAPP_SYSTEM_IMAGE): $(HTTPSAPP_PACKAGE) tools/openrfs-package.py \
		tools/fat32_image.py
	$(PYTHON) tools/openrfs-package.py install-system \
		--output $@ $(HTTPSAPP_PACKAGE)

$(HTTPSAPP_DATA_IMAGE): tools/fat32_image.py | $(HTTPSAPP_DIR)
	$(PYTHON) tools/fat32_image.py format data $@

$(OPENRFSAPP_DIR)/main.o: apps/openrfs/main.c \
		apps/native-https/trust_anchor.h $(SDK_BUILD_DIR)/.installed | \
		$(OPENRFSAPP_DIR)
	$(SDK_CC) $(SDK_CFLAGS) -c $< -o $@

$(OPENRFSAPP_APP): $(OPENRFSAPP_DIR)/main.o $(SDK_BUILD_DIR)/.installed
	$(SDK_LD) $(SDK_LDFLAGS) -Map=$(OPENRFSAPP_DIR)/OPENRFS.map \
		-o $@ $(SDK_CRT) $< $(SDK_LIB) $(BEARSSL_LIB)

$(OPENRFSAPP_PACKAGE): $(OPENRFSAPP_APP) apps/openrfs/manifest.json
	$(PYTHON) tools/openrfs-package.py build \
		--spec apps/openrfs/manifest.json --executable $< --output $@

$(OPENRFSAPP_REPAIR_PACKAGE): $(OPENRFSAPP_APP) apps/openrfs/repair-manifest.json
	$(PYTHON) tools/openrfs-package.py build \
		--spec apps/openrfs/repair-manifest.json --executable $< --output $@

$(OPENRFSAPP_SYSTEM_IMAGE): $(OPENRFSAPP_PACKAGE) $(OPENRFSAPP_REPAIR_PACKAGE) \
		tools/openrfs-package.py \
		tools/fat32_image.py
	$(PYTHON) tools/openrfs-package.py install-system \
		--output $@ $(OPENRFSAPP_PACKAGE) $(OPENRFSAPP_REPAIR_PACKAGE)

$(OPENRFSAPP_DATA_IMAGE): $(EXT4_FIXTURE) | $(OPENRFSAPP_DIR)
	cp $< $@

$(OPENRFSAPP_REPOSITORY): $(SDL_CHESS_RELEASE_APP) \
		apps/upstream-sdl-chess/manifest.json \
		tools/package_lifecycle_fixture.py \
		tools/openrfs-package.py tools/openrfs-repository.py \
		platform/package-trust.json
	$(PYTHON) tools/package_lifecycle_fixture.py \
		--output $(OPENRFSAPP_DIR)/repository \
		--executable $(SDL_CHESS_RELEASE_APP) \
		--manifest-spec apps/upstream-sdl-chess/manifest.json

$(AUDIO_APP_DIR)/main.o: apps/native-audio/main.c \
		$(SDK_BUILD_DIR)/.installed | $(AUDIO_APP_DIR)
	$(SDK_CC) $(SDK_CFLAGS) -c $< -o $@

$(AUDIO_APP): $(AUDIO_APP_DIR)/main.o $(SDK_BUILD_DIR)/.installed
	$(SDK_LD) $(SDK_LDFLAGS) -Map=$(AUDIO_APP_DIR)/AUDIO.map \
		-o $@ $(SDK_CRT) $< $(SDK_LIB)

$(AUDIO_PACKAGE): $(AUDIO_APP) apps/native-audio/manifest.json
	$(PYTHON) tools/openrfs-package.py build \
		--spec apps/native-audio/manifest.json --executable $< --output $@

$(AUDIO_REFUSAL_PACKAGE): $(AUDIO_APP) \
		apps/native-audio/manifest-refusal.json
	$(PYTHON) tools/openrfs-package.py build \
		--spec apps/native-audio/manifest-refusal.json \
		--executable $< --output $@

$(AUDIO_SYSTEM_IMAGE): $(AUDIO_PACKAGE) $(AUDIO_REFUSAL_PACKAGE) \
		tools/openrfs-package.py tools/fat32_image.py
	$(PYTHON) tools/openrfs-package.py install-system \
		--output $@ $(AUDIO_PACKAGE) $(AUDIO_REFUSAL_PACKAGE)

$(AUDIO_DATA_IMAGE): tools/fat32_image.py | $(AUDIO_APP_DIR)
	$(PYTHON) tools/fat32_image.py format data $@

$(SDL_PROOF_DIR)/main.o: apps/native-sdl/main.c \
		$(SDK_BUILD_DIR)/.installed | $(SDL_PROOF_DIR)
	$(SDK_CC) $(SDL2_CFLAGS) -Wpedantic -Wshadow -Wundef \
		-Wstrict-prototypes -Wmissing-prototypes -c $< -o $@

$(SDL_PROOF_APP): $(SDL_PROOF_DIR)/main.o $(SDK_BUILD_DIR)/.installed
	$(SDK_LD) $(SDK_LDFLAGS) -Map=$(SDL_PROOF_DIR)/SDL.map \
		-o $@ $(SDK_CRT) $< $(SDL2_LIB) $(SDK_LIB)

$(SDL_PROOF_PACKAGE): $(SDL_PROOF_APP) apps/native-sdl/manifest.json
	$(PYTHON) tools/openrfs-package.py build \
		--spec apps/native-sdl/manifest.json --executable $< --output $@

$(SDL_PROOF_SYSTEM_IMAGE): $(SDL_PROOF_PACKAGE) tools/openrfs-package.py \
		tools/fat32_image.py
	$(PYTHON) tools/openrfs-package.py install-system \
		--output $@ $(SDL_PROOF_PACKAGE)

$(SDL_PROOF_DATA_IMAGE): tools/fat32_image.py | $(SDL_PROOF_DIR)
	$(PYTHON) tools/fat32_image.py format data $@

$(SDL_CHESS_DIR)/main.o: apps/upstream-sdl-chess/main.c \
		apps/upstream-sdl-chess/upstream.c $(SDK_BUILD_DIR)/.installed | \
		$(SDL_CHESS_DIR)
	$(SDK_CC) $(SDL2_VENDOR_CFLAGS) -c $< -o $@

$(SDL_CHESS_APP): $(SDL_CHESS_DIR)/main.o $(SDK_BUILD_DIR)/.installed
	$(SDK_LD) $(SDK_LDFLAGS) -Map=$(SDL_CHESS_DIR)/CHESS.map \
		-o $@ $(SDK_CRT) $< $(SDL2_LIB) $(SDK_LIB)

# Repository payloads are release artifacts. Keep CHESS.APP plus its link map
# for diagnosis, but do not carry DWARF and linker symbols over guest HTTPS.
$(SDL_CHESS_RELEASE_APP): $(SDL_CHESS_DIR)/main.o \
		$(SDK_BUILD_DIR)/.installed
	$(SDK_LD) $(SDK_LDFLAGS) --strip-all \
		-o $@ $(SDK_CRT) $< $(SDL2_LIB) $(SDK_LIB)

$(SDL_CHESS_PACKAGE): $(SDL_CHESS_APP) \
		apps/upstream-sdl-chess/manifest.json
	$(PYTHON) tools/openrfs-package.py build \
		--spec apps/upstream-sdl-chess/manifest.json \
		--executable $< --output $@

$(DYNAMIC_ROOT_APP) $(DYNAMIC_LIBRARY) $(DYNAMIC_CATALOG) \
		$(DYNAMIC_PACKAGE_SPEC) &: apps/native-dynamic/root.c \
		apps/native-dynamic/library.c apps/native-dynamic/start.S \
		apps/native-dynamic/proof.h apps/native-dynamic/manifest.json \
		tools/build-native-dynamic-proof.sh \
		tools/make-native-dynamic-proof.py | $(DYNAMIC_APP_DIR)
	OPENRFS_SDK_CC='$(SDK_CC)' OPENRFS_SDK_LD='$(SDK_LD)' \
		PYTHON='$(PYTHON)' READELF='$(READELF)' \
		bash tools/build-native-dynamic-proof.sh $(DYNAMIC_APP_DIR)

$(DYNAMIC_PACKAGE): $(DYNAMIC_ROOT_APP) $(DYNAMIC_LIBRARY) \
		$(DYNAMIC_CATALOG) $(DYNAMIC_PACKAGE_SPEC) tools/openrfs-package.py
	$(PYTHON) tools/openrfs-package.py build \
		--spec $(DYNAMIC_PACKAGE_SPEC) --executable $(DYNAMIC_ROOT_APP) \
		--output $@

$(DYNAMIC_SYSTEM_IMAGE): $(DYNAMIC_PACKAGE) tools/openrfs-package.py \
		tools/fat32_image.py
	$(PYTHON) tools/openrfs-package.py install-system \
		--output $@ $(DYNAMIC_PACKAGE)

$(DYNAMIC_DATA_IMAGE): tools/fat32_image.py | $(DYNAMIC_APP_DIR)
	$(PYTHON) tools/fat32_image.py format data $@

$(RUST_APP): apps/native-rust/Cargo.toml apps/native-rust/Cargo.lock \
		apps/native-rust/manifest.json apps/native-rust/src/main.rs \
		rust/openrfs/Cargo.toml rust/openrfs/src/lib.rs sdk/linker.ld | $(RUST_APP_DIR)
	CARGO_TARGET_DIR='$(abspath $(RUST_APP_CARGO_TARGET))' \
		RUSTFLAGS='$(RUST_APP_FLAGS)' $(CARGO) build \
		--manifest-path apps/native-rust/Cargo.toml --release \
		--target x86_64-unknown-none --locked --offline
	cp '$(RUST_APP_SOURCE)' $@

$(RUST_APP_PACKAGE): $(RUST_APP) apps/native-rust/manifest.json
	$(PYTHON) tools/openrfs-package.py build \
		--spec apps/native-rust/manifest.json --executable $< --output $@

$(RUST_APP_SYSTEM_IMAGE): $(RUST_APP_PACKAGE) tools/openrfs-package.py \
		tools/fat32_image.py
	$(PYTHON) tools/openrfs-package.py install-system \
		--output $@ $(RUST_APP_PACKAGE)

$(RUST_APP_DATA_IMAGE): tools/fat32_image.py | $(RUST_APP_DIR)
	$(PYTHON) tools/fat32_image.py format data $@

$(NATIVE_SYSTEM_IMAGE): $(NATIVE_TEST_PACKAGE) tools/openrfs-package.py \
		tools/fat32_image.py
	$(PYTHON) tools/openrfs-package.py install-system \
		--output $@ $(NATIVE_TEST_PACKAGE)

$(NATIVE_DATA_IMAGE): tools/fat32_image.py | $(NATIVE_APP_DIR)
	$(PYTHON) tools/fat32_image.py format data $@

$(CRASH_SYSTEM_IMAGE): $(CRASH_PACKAGE) $(NATIVE_TEST_PACKAGE) \
		tools/openrfs-package.py tools/fat32_image.py
	$(PYTHON) tools/openrfs-package.py install-system \
		--output $@ $(CRASH_PACKAGE) $(NATIVE_TEST_PACKAGE)

$(CRASH_DATA_IMAGE): tools/fat32_image.py | $(CRASH_APP_DIR)
	$(PYTHON) tools/fat32_image.py format data $@

$(ADMISSION_SYSTEM_IMAGE): $(NATIVE_TEST_PACKAGE) \
		tools/make-native-admission-fixture.py tools/openrfs-package.py \
		tools/fat32_image.py | $(ADMISSION_DIR)
	$(PYTHON) tools/make-native-admission-fixture.py \
		$(NATIVE_TEST_PACKAGE) $@

$(ADMISSION_DATA_IMAGE): tools/fat32_image.py | $(ADMISSION_DIR)
	$(PYTHON) tools/fat32_image.py format data $@

native-apps: $(NATIVE_TEST_PACKAGE) $(LUA_PACKAGE) $(SQLITE_PACKAGE) \
	$(NETAPP_PACKAGE) \
	$(HTTPSAPP_PACKAGE) $(OPENRFSAPP_PACKAGE) $(OPENRFSAPP_REPAIR_PACKAGE) \
	$(AUDIO_PACKAGE) $(AUDIO_REFUSAL_PACKAGE) $(RUST_APP_PACKAGE) \
	$(CRASH_PACKAGE) $(SDL_PROOF_PACKAGE) $(SDL_CHESS_PACKAGE) \
	$(DYNAMIC_PACKAGE)

audio-wav-tests:
	$(PYTHON) -S tools/audio-wav-host-test.py --self-test

native-audio-proof: $(AUDIO_SYSTEM_IMAGE) $(AUDIO_DATA_IMAGE) audio-wav-tests
	@echo 'native audio proof packages, images and WAV controls built'

native-sdl-proof: $(SDL_PROOF_SYSTEM_IMAGE) $(SDL_PROOF_DATA_IMAGE)
	@echo 'native SDL proof package and images built'

sdl-preference-tests: tools/check-sdl-preference-paths.py \
		apps/native-sdl/main.c apps/upstream-sdl-chess/main.c \
		src/kernel/test.c vendor/sdl2/src/filesystem/openrfs/SDL_sysfilesystem.c
	$(PYTHON) tools/check-sdl-preference-paths.py

native-dynamic-proof: $(DYNAMIC_SYSTEM_IMAGE) $(DYNAMIC_DATA_IMAGE) \
		dynamic-elf-tests
	@echo 'native dynamic ELF package and images built'

native-https-proof: $(HTTPSAPP_SYSTEM_IMAGE) $(HTTPSAPP_DATA_IMAGE) \
		https-tests
	@echo 'native authenticated HTTPS package and images built'

native-openrfs-proof: $(OPENRFSAPP_SYSTEM_IMAGE) $(OPENRFSAPP_DATA_IMAGE) \
		$(OPENRFSAPP_REPOSITORY) https-tests
	@echo 'native signed HTTPS package lifecycle proof built'

port-tests: native-apps audio-wav-tests sdl-preference-tests
	OPENRFS_NATIVE_TEST_ELF='$(abspath $(NATIVE_TEST_APP))' \
		$(RUSTC) --edition 2024 --test -D warnings \
		tools/native-image-host-test.rs -o $(RUST_NATIVE_IMAGE_TEST)
	$(RUST_NATIVE_IMAGE_TEST)
	OPENRFS_NATIVE_TEST_ELF='$(abspath $(LUA_APP))' \
		$(RUST_NATIVE_IMAGE_TEST)
	OPENRFS_NATIVE_TEST_ELF='$(abspath $(SQLITE_APP))' \
		$(RUST_NATIVE_IMAGE_TEST)
	OPENRFS_NATIVE_TEST_ELF='$(abspath $(NETAPP_APP))' \
		$(RUST_NATIVE_IMAGE_TEST)
	OPENRFS_NATIVE_TEST_ELF='$(abspath $(HTTPSAPP_APP))' \
		$(RUST_NATIVE_IMAGE_TEST)
	OPENRFS_NATIVE_TEST_ELF='$(abspath $(OPENRFSAPP_APP))' \
		$(RUST_NATIVE_IMAGE_TEST)
	OPENRFS_NATIVE_TEST_ELF='$(abspath $(AUDIO_APP))' \
		$(RUST_NATIVE_IMAGE_TEST)
	OPENRFS_NATIVE_TEST_ELF='$(abspath $(SDL_PROOF_APP))' \
		$(RUST_NATIVE_IMAGE_TEST)
	OPENRFS_NATIVE_TEST_ELF='$(abspath $(SDL_CHESS_APP))' \
		$(RUST_NATIVE_IMAGE_TEST)
	OPENRFS_NATIVE_TEST_ELF='$(abspath $(RUST_APP))' \
		$(RUST_NATIVE_IMAGE_TEST)
	OPENRFS_NATIVE_TEST_ELF='$(abspath $(CRASH_APP))' \
		$(RUST_NATIVE_IMAGE_TEST)
	OPENRFS_REQUIRE_ED25519=1 $(PYTHON) -u tools/openrfs_package_host_test.py
	$(PYTHON) tools/openrfs-package.py inspect $(NATIVE_TEST_PACKAGE)
	$(PYTHON) tools/openrfs-package.py inspect $(LUA_PACKAGE)
	$(PYTHON) tools/openrfs-package.py inspect $(SQLITE_PACKAGE)
	$(PYTHON) tools/openrfs-package.py inspect $(NETAPP_PACKAGE)
	$(PYTHON) tools/openrfs-package.py inspect $(HTTPSAPP_PACKAGE)
	$(PYTHON) tools/openrfs-package.py inspect $(OPENRFSAPP_PACKAGE)
	$(PYTHON) tools/openrfs-package.py inspect $(AUDIO_PACKAGE)
	$(PYTHON) tools/openrfs-package.py inspect $(AUDIO_REFUSAL_PACKAGE)
	$(PYTHON) tools/openrfs-package.py inspect $(SDL_PROOF_PACKAGE)
	$(PYTHON) tools/openrfs-package.py inspect $(SDL_CHESS_PACKAGE)
	$(PYTHON) tools/openrfs-package.py inspect $(DYNAMIC_PACKAGE)
	$(PYTHON) tools/openrfs-package.py inspect $(RUST_APP_PACKAGE)
	$(PYTHON) tools/openrfs-package.py inspect $(CRASH_PACKAGE)

qemu-port-tests: qemu-test-native qemu-test-native-lua qemu-test-native-sqlite \
	qemu-test-network-native qemu-test-native-rust \
	qemu-test-native-crash qemu-test-native-elf-refusal \
	qemu-test-native-digest-refusal qemu-test-native-abi-refusal \
	qemu-test-native-relaunch qemu-test-native-audio qemu-test-native-sdl \
	qemu-test-native-dynamic qemu-test-native-https qemu-test-native-openrfs
	@echo 'native userspace, Lua, SQLite, network, HTTPS, signed package lifecycle, audio, SDL, dynamic ELF and Rust QEMU scenarios passed'

contract-counts:
	@printf '%s %s\n' '$(EXPECTED_TEST_SCENARIO_COUNT)' \
		'$(EXPECTED_SHELL_ASSERTION_COUNT)'

contract-scenarios:
	@printf '%s\n' $(TEST_SCENARIOS)

kernel: $(KERNEL)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/arch_%.o: src/arch/x86_64/%.S | $(BUILD_DIR)
	$(KERNEL_CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: src/kernel/%.c | $(BUILD_DIR)
	$(KERNEL_CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(IPXE_OBJECT_DIR)/vendor/%.o: vendor/%.c ports/ipxe/include/compiler.h
	mkdir -p $(dir $@)
	$(KERNEL_CC) $(IPXE_VENDOR_CFLAGS) -MMD -MP -c $< -o $@

$(IPXE_OBJECT_DIR)/ports/%.o: ports/%.c ports/ipxe/include/compiler.h
	mkdir -p $(dir $@)
	$(KERNEL_CC) $(IPXE_GLUE_CFLAGS) -MMD -MP -c $< -o $@

$(IPXE_USB_LAYER_OBJECT): $(IPXE_USB_OBJECTS) $(IPXE_USB_EXPORTS) \
		$(IPXE_USB_LINKER_SCRIPT)
	$(KERNEL_LD) -r -T $(IPXE_USB_LINKER_SCRIPT) -o $@.partial \
		$(IPXE_USB_OBJECTS)
	$(OBJCOPY) --keep-global-symbols=$(IPXE_USB_EXPORTS) $@.partial $@
	rm -f $@.partial

$(SEABIOS_OBJECT_DIR)/vendor/%.o: vendor/%.c
	mkdir -p $(dir $@)
	$(KERNEL_CC) $(SEABIOS_VENDOR_CFLAGS) -MMD -MP -c $< -o $@

# The LP64 wrappers compile vendored code: vendor warnings, plus the
# pointer/u32 conversions the corrected header makes explicit to GCC.
$(SEABIOS_OBJECT_DIR)/ports/seabios/lp64/%.o: ports/seabios/lp64/%.c
	mkdir -p $(dir $@)
	$(KERNEL_CC) $(SEABIOS_VENDOR_CFLAGS) -Iports/seabios/lp64 \
		-Wno-int-conversion -MMD -MP -c $< -o $@

$(SEABIOS_OBJECT_DIR)/ports/%.o: ports/%.c
	mkdir -p $(dir $@)
	$(KERNEL_CC) $(SEABIOS_GLUE_CFLAGS) -MMD -MP -c $< -o $@

$(SEABIOS_LAYER_OBJECT): $(SEABIOS_OBJECTS) $(SEABIOS_EXPORTS)
	$(KERNEL_LD) -r -o $@.partial $(SEABIOS_OBJECTS)
	$(OBJCOPY) --keep-global-symbols=$(SEABIOS_EXPORTS) $@.partial $@
	rm -f $@.partial

$(SEAVGA_LIBC_OBJECT): ports/seabios/libc.c
	mkdir -p $(dir $@)
	$(KERNEL_CC) $(SEAVGA_GLUE_CFLAGS) -MMD -MP -c $< -o $@

# One card type's build: its vgasrc files and the glue, compiled with its
# SEAVGA_VARIANT_* definition, then linked with every symbol but its
# dispatch entry made local.
define SEAVGA_VARIANT_RULES
$$(SEAVGA_OBJECT_DIR)/$(1)/%.o: vendor/seabios/vgasrc/%.c
	mkdir -p $$(dir $$@)
	$$(KERNEL_CC) $$(SEAVGA_VENDOR_CFLAGS) -D$$(SEAVGA_$(1)_DEFINE) \
		-MMD -MP -c $$< -o $$@

$$(SEAVGA_OBJECT_DIR)/$(1)/lp64/%.o: ports/seavga/lp64/%.c
	mkdir -p $$(dir $$@)
	$$(KERNEL_CC) $$(SEAVGA_VENDOR_CFLAGS) -D$$(SEAVGA_$(1)_DEFINE) \
		-MMD -MP -c $$< -o $$@

$$(SEAVGA_OBJECT_DIR)/$(1)/seavga_glue.o: ports/seavga/seavga_glue.c
	mkdir -p $$(dir $$@)
	$$(KERNEL_CC) $$(SEAVGA_GLUE_CFLAGS) -D$$(SEAVGA_$(1)_DEFINE) \
		-DSEAVGA_DISPATCH=seavga_$(1)_dispatch -MMD -MP -c $$< -o $$@

$$(SEAVGA_OBJECT_DIR)/seavga-$(1).o: $$(patsubst %,$$(SEAVGA_OBJECT_DIR)/$(1)/%.o,\
		$$(SEAVGA_$(1)_FILES) seavga_glue) \
		$$(patsubst %,$$(SEAVGA_OBJECT_DIR)/$(1)/lp64/%.o,\
		$$(SEAVGA_$(1)_LP64_FILES)) $$(SEAVGA_LIBC_OBJECT)
	$$(KERNEL_LD) -r -o $$@.partial $$^
	$$(OBJCOPY) --keep-global-symbol=seavga_$(1)_dispatch $$@.partial $$@
	rm -f $$@.partial
endef
$(foreach variant,$(SEAVGA_VARIANTS),\
	$(eval $(call SEAVGA_VARIANT_RULES,$(variant))))

# One MINIX driver: its sources and the glue, linked with every symbol but
# its dispatch entry made local.
define MINIX_DRIVER_RULES
$$(MINIX_OBJECT_DIR)/$(1)/%.o: vendor/%.c
	mkdir -p $$(dir $$@)
	$$(KERNEL_CC) $$(MINIX_VENDOR_CFLAGS) -MMD -MP -c $$< -o $$@

$$(MINIX_OBJECT_DIR)/$(1)/audio_glue.o: ports/minix/audio_glue.c
	mkdir -p $$(dir $$@)
	$$(KERNEL_CC) $$(MINIX_GLUE_CFLAGS) \
		-DMINIX_AUDIO_DISPATCH=minix_$(1)_dispatch -MMD -MP -c $$< -o $$@

$$(MINIX_OBJECT_DIR)/minix-$(1).o: $$(patsubst vendor/%.c,\
		$$(MINIX_OBJECT_DIR)/$(1)/%.o,$$(MINIX_$(1)_SOURCES)) \
		$$(MINIX_OBJECT_DIR)/$(1)/audio_glue.o
	$$(KERNEL_LD) -r -o $$@.partial $$^
	$$(OBJCOPY) --keep-global-symbol=minix_$(1)_dispatch $$@.partial $$@
	rm -f $$@.partial
endef
$(foreach driver,$(MINIX_DRIVERS),\
	$(eval $(call MINIX_DRIVER_RULES,$(driver))))

$(BUILD_DIR)/package_trust.o: CPPFLAGS += -Ivendor/monocypher/src \
	-Ivendor/monocypher/src/optional
$(BUILD_DIR)/package_trust.o: vendor/monocypher/src/monocypher.h \
	vendor/monocypher/src/optional/monocypher-ed25519.h

force-package-trust:

$(PACKAGE_TRUST_BLOB): force-package-trust $(PACKAGE_TRUST_SPEC) \
		tools/make-package-trust.py | $(BUILD_DIR)
	$(PYTHON) tools/make-package-trust.py build $(PACKAGE_TRUST_SPEC) $@

$(PACKAGE_TRUST_ASSET_C): $(PACKAGE_TRUST_BLOB) tools/make-package-trust.py
	$(PYTHON) tools/make-package-trust.py emit-c $< $@

$(PACKAGE_TRUST_ASSET_OBJECT): $(PACKAGE_TRUST_ASSET_C) | $(BUILD_DIR)
	$(KERNEL_CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

package-trust-asset-tests: $(PACKAGE_TRUST_BLOB) $(PACKAGE_TRUST_ASSET_C)
	$(PYTHON) tools/make-package-trust.py self-test
	$(PYTHON) tools/make-package-trust.py audit $(PACKAGE_TRUST_BLOB)

$(BUILD_DIR)/monocypher/monocypher.o: vendor/monocypher/src/monocypher.c
	mkdir -p $(dir $@)
	$(KERNEL_CC) $(MONOCYPHER_CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/monocypher/monocypher-ed25519.o: \
		vendor/monocypher/src/optional/monocypher-ed25519.c
	mkdir -p $(dir $@)
	$(KERNEL_CC) $(MONOCYPHER_CFLAGS) -MMD -MP -c $< -o $@

# Regenerated only when the logo itself changes. The result is a build
# artifact and is deliberately not committed; src/rust/abi.rs includes it.
$(LOGO_BLOB): $(LOGO_SOURCE) tools/make-logo-asset.py | $(BUILD_DIR)
	$(PYTHON) tools/make-logo-asset.py $(LOGO_SOURCE) \
		$(LOGO_MAX_DIMENSION) $@ --keep-canvas

$(WALLPAPER_BLOB): $(WALLPAPER_SOURCES) tools/make-wallpaper-asset.py | $(BUILD_DIR)
	$(PYTHON) tools/make-wallpaper-asset.py $(WALLPAPER_SOURCES) $@

# The terminal font still comes from committed ASCII art, so a clone needs
# nothing but Python to pack it for the kernel.
$(FONT_BLOB): $(FONT_SOURCE) tools/make-font-asset.py | $(BUILD_DIR)
	$(PYTHON) tools/make-font-asset.py $(FONT_SOURCE) $@

$(UI_FONT_BLOB): $(UI_FONT_SOURCE) $(UI_FONT_METRICS) \
		tools/make-ui-font-asset.py | $(BUILD_DIR)
	# Inter was rasterized ahead of time; the normal build only packs committed
	# alpha and metrics and therefore needs no host font or imaging library.
	$(PYTHON) tools/make-ui-font-asset.py $(UI_FONT_SOURCE) \
		$(UI_FONT_METRICS) $@

$(RUST_LIB): $(RUST_SOURCES) $(RUST_MANIFEST) $(RUST_LOCKFILE) \
		.cargo/config.toml $(RUST_VENDOR_SOURCES) \
		$(LOGO_BLOB) \
		$(WALLPAPER_BLOB) $(FONT_BLOB) $(UI_FONT_BLOB) | $(BUILD_DIR)
	OPENRFS_LOGO_BLOB='$(abspath $(LOGO_BLOB))' \
	OPENRFS_WALLPAPER_BLOB='$(abspath $(WALLPAPER_BLOB))' \
	OPENRFS_FONT_BLOB='$(abspath $(FONT_BLOB))' \
	OPENRFS_UI_FONT_BLOB='$(abspath $(UI_FONT_BLOB))' \
	CARGO_TARGET_DIR='$(abspath $(BUILD_DIR))/rust-target' \
	RUSTFLAGS='$(RUSTFLAGS)' \
		$(CARGO) build --manifest-path $(RUST_MANIFEST) \
			--target $(RUST_TARGET) --release --locked --offline
	cp $(BUILD_DIR)/rust-target/$(RUST_TARGET)/release/libopenrfs.a $@

$(BUSYBOX_BINARY): tools/build-busybox-proof.sh \
		tools/check-exercised-instructions.py \
		userspace/busybox/busybox.config \
		userspace/busybox/source/busybox-1.38.0.tar.bz2 \
		userspace/busybox/source/musl-1.2.6.tar.gz
	OPENRFS_BUSYBOX_BUILD_ONLY=1 bash tools/build-busybox-proof.sh \
		$(BUSYBOX_OUTPUT_DIR) $(BUSYBOX_WORK_DIR)

$(LINUX_ABI_FIXTURE): $(BUSYBOX_BINARY) tools/make-linux-abi-fixture.py
	mkdir -p $(dir $@)
	$(PYTHON) tools/make-linux-abi-fixture.py $(BUSYBOX_BINARY) $@

$(BUSYBOX_UNAME_BINARY): tools/build-busybox-uname-proof.sh \
		tools/check-exercised-instructions.py \
		userspace/busybox/busybox-uname.config \
		userspace/busybox/musl-vfprintf-scalar.h \
		userspace/busybox/source/busybox-1.38.0.tar.bz2 \
		userspace/busybox/source/musl-1.2.6.tar.gz
	OPENRFS_BUSYBOX_BUILD_ONLY=1 bash tools/build-busybox-uname-proof.sh \
		$(BUSYBOX_UNAME_OUTPUT_DIR) $(BUSYBOX_UNAME_WORK_DIR)

$(LINUX_UNAME_FIXTURE): $(BUSYBOX_UNAME_BINARY) \
		tools/make-linux-uname-fixture.py
	mkdir -p $(dir $@)
	$(PYTHON) tools/make-linux-uname-fixture.py $(BUSYBOX_UNAME_BINARY) $@

$(BUSYBOX_CAT_BINARY): tools/build-busybox-cat-proof.sh \
		tools/check-exercised-instructions.py \
		userspace/busybox/busybox-cat.config \
		userspace/busybox/source/busybox-1.38.0.tar.bz2 \
		userspace/busybox/source/musl-1.2.6.tar.gz
	OPENRFS_BUSYBOX_BUILD_ONLY=1 bash tools/build-busybox-cat-proof.sh \
		$(BUSYBOX_CAT_OUTPUT_DIR) $(BUSYBOX_CAT_WORK_DIR)

$(OPENRFS_PROOF_USERLAND_IMAGE): $(BUSYBOX_BINARY) $(BUSYBOX_UNAME_BINARY) \
		$(BUSYBOX_CAT_BINARY) \
		tools/make-openrfs-proof-userland.py
	mkdir -p $(dir $@)
	$(PYTHON) tools/make-openrfs-proof-userland.py \
		$(BUSYBOX_BINARY) $(BUSYBOX_UNAME_BINARY) $(BUSYBOX_CAT_BINARY) $@

$(OPENRFS_PROOF_USERLAND_NO_CAT_IMAGE): $(BUSYBOX_BINARY) \
		$(BUSYBOX_UNAME_BINARY) tools/make-openrfs-proof-userland.py
	mkdir -p $(dir $@)
	$(PYTHON) tools/make-openrfs-proof-userland.py \
		$(BUSYBOX_BINARY) $(BUSYBOX_UNAME_BINARY) --without-cat $@

$(FAT32_SYSTEM_IMAGE): $(BUSYBOX_BINARY) $(BUSYBOX_UNAME_BINARY) \
		$(BUSYBOX_CAT_BINARY) tools/fat32_image.py
	mkdir -p $(dir $@)
	$(PYTHON) tools/fat32_image.py format system $@ \
		--echo $(BUSYBOX_BINARY) --uname $(BUSYBOX_UNAME_BINARY) \
		--cat $(BUSYBOX_CAT_BINARY)

$(DESKTOP_SYSTEM_IMAGE): $(BUSYBOX_BINARY) $(BUSYBOX_UNAME_BINARY) \
		$(BUSYBOX_CAT_BINARY) tools/openrfs-package.py \
		tools/fat32_image.py
	mkdir -p $(dir $@)
	$(PYTHON) tools/openrfs-package.py install-system \
		--echo $(BUSYBOX_BINARY) --uname $(BUSYBOX_UNAME_BINARY) \
		--cat $(BUSYBOX_CAT_BINARY) --output $@

$(FAT32_DATA_IMAGE): tools/fat32_image.py
	mkdir -p $(dir $@)
	$(PYTHON) tools/fat32_image.py format data $@

$(FAT32_FULL_IMAGE): tools/fat32_image.py
	mkdir -p $(dir $@)
	$(PYTHON) tools/fat32_image.py format data $@ --full

$(FAT32_CORRUPT_IMAGE): $(FAT32_DATA_IMAGE) tools/fat32_image.py
	mkdir -p $(dir $@)
	$(PYTHON) tools/fat32_image.py malform fat-mismatch \
		$(FAT32_DATA_IMAGE) $@

fat32-images: $(FAT32_SYSTEM_IMAGE) $(FAT32_DATA_IMAGE)

$(KERNEL): $(OBJECTS) $(RUST_LIB) linker.ld
	$(KERNEL_LD) $(LDFLAGS) -o $@ $(OBJECTS) $(RUST_LIB) || { \
		rm -f $@; \
		sed -n '/__got_start/,/__got_end/p' $(BUILD_DIR)/openrfs.map; \
		sed 's/ASSERT(__got_end - __got_start <= 0x400,/ASSERT(1,/' \
			linker.ld >$(BUILD_DIR)/linker-got-diagnostic.ld; \
		$(KERNEL_LD) -nostdlib -z max-page-size=0x1000 -z noexecstack \
			--orphan-handling=error --build-id=none --emit-relocs \
			-T $(BUILD_DIR)/linker-got-diagnostic.ld \
			-o $(BUILD_DIR)/openrfs-got-diagnostic.elf \
			$(OBJECTS) $(RUST_LIB) || true; \
		readelf -W -r $(BUILD_DIR)/openrfs-got-diagnostic.elf \
			| grep 'GOT' || true; \
		$(OBJDUMP) -dr $(RUST_LIB) \
			| grep -B 8 -A 2 'R_X86_64_GOTPCREL' || true; \
		exit 1; \
	}

toolchain:
	@missing_tools=; \
	for tool in bash bzip2 gcc gzip ld grub-file readelf nm objdump rustc python3 sha256sum strings tar; do \
		if ! command -v $$tool >/dev/null 2>&1; then \
			missing_tools="$$missing_tools $$tool"; \
		fi; \
	done; \
	test -z "$$missing_tools" || { echo "missing tools:$$missing_tools"; exit 1; }
	@version=$$($(RUSTC) --version | awk '{ print $$2 }'); \
		echo "$$version" | awk -F'[.-]' \
			'{ exit !($$1 > 1 || ($$1 == 1 && $$2 >= 86)) }' || \
		{ echo "rustc 1.86.0 or newer is required (found $$version)"; exit 1; }
	@$(RUSTC) --print target-list | grep -Fxq '$(RUST_TARGET)' || \
		{ echo 'rustc does not know $(RUST_TARGET)'; exit 1; }
	@libdir=$$($(RUSTC) --target $(RUST_TARGET) --print target-libdir 2>/dev/null) || \
		{ echo 'run: rustup target add $(RUST_TARGET)'; exit 1; }; \
		set -- "$$libdir"/libcore-*.rlib; \
		test -f "$$1" || \
		{ echo 'run: rustup target add $(RUST_TARGET)'; exit 1; }

lint:
	@if git grep -nI -E '[[:blank:]]+$$' -- . ':!assets/*' ':!vendor/*'; then \
		echo "trailing whitespace is forbidden"; exit 1; \
	fi

$(WALL_CLOCK_HOST_TEST): tools/wall-clock-host-test.c \
		src/kernel/wall_clock.c include/openrfs/wall_clock.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Wshadow \
		-Wundef -Wstrict-prototypes -Wmissing-prototypes -Iinclude \
		tools/wall-clock-host-test.c src/kernel/wall_clock.c -o $@

$(SDK_TIME_HOST_TEST): tools/sdk-time-host-test.c sdk/src/time.c \
		sdk/include/time.h sdk/include/openrfs/runtime.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Wshadow \
		-Wundef -Wstrict-prototypes -Wmissing-prototypes \
		-Isdk/include -Iinclude tools/sdk-time-host-test.c \
		sdk/src/time.c -o $@

wall-clock-tests: $(WALL_CLOCK_HOST_TEST) $(SDK_TIME_HOST_TEST)
	$(WALL_CLOCK_HOST_TEST)
	$(SDK_TIME_HOST_TEST)

$(BUILD_DIR)/sdk-filesystem-host-test: tools/sdk-filesystem-host-test.c sdk/src/posix.c sdk/src/runtime.c \
		sdk/src/internal.h sdk/include/openrfs/runtime.h include/openrfs/abi/storage.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections -fvisibility=hidden \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion \
		-DOPENRFS_HOSTED \
		-Isdk/include -Iinclude tools/sdk-filesystem-host-test.c sdk/src/posix.c sdk/src/runtime.c \
		-Wl,--gc-sections -o $@

$(BUILD_DIR)/ext4-vfs-host-test: tools/ext4-vfs-host-test.c \
		src/kernel/ext4_fs.c include/openrfs/ext4_fs.h include/openrfs/nvme.h \
		include/openrfs/fat32_fs.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Iinclude \
		tools/ext4-vfs-host-test.c -Wl,--gc-sections -o $@

$(BUILD_DIR)/ext4-fsync-host-test: tools/ext4-fsync-host-test.c \
		src/kernel/ext4_fs.c include/openrfs/ext4_fs.h include/openrfs/nvme.h \
		include/openrfs/fat32_fs.h include/openrfs/slot_claim.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Iinclude \
		tools/ext4-fsync-host-test.c -Wl,--gc-sections -o $@

ext4-fsync-test: $(BUILD_DIR)/ext4-fsync-host-test
	$(BUILD_DIR)/ext4-fsync-host-test

$(BUILD_DIR)/ext4-sparse-truncate-host-test: tools/ext4-sparse-truncate-host-test.c \
		src/kernel/ext4_fs.c include/openrfs/ext4_fs.h include/openrfs/nvme.h \
		include/openrfs/fat32_fs.h include/openrfs/slot_claim.h include/openrfs/cpu.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Iinclude \
		tools/ext4-sparse-truncate-host-test.c -Wl,--gc-sections -o $@

ext4-sparse-truncate-test: $(BUILD_DIR)/ext4-sparse-truncate-host-test tools/ext4_image.py tools/ext4_host_test.py
	$(BUILD_DIR)/ext4-sparse-truncate-host-test
	OPENRFS_EXT4_RUST_FIXTURE='$(abspath $(BUILD_DIR))/ext4-rust-fixture.img' \
		$(PYTHON) -u tools/ext4_host_test.py
	if test -f '$(BUILD_DIR)/ext4-rust-fixture.img'; then \
		OPENRFS_EXT4_RUST_FIXTURE='$(abspath $(BUILD_DIR))/ext4-rust-fixture.img' $(CARGO_TEST_ENV) \
		CARGO_TARGET_DIR='$(abspath $(BUILD_DIR))/ext4-transaction-target' \
		$(CARGO) test --manifest-path tools/ext4-transaction-tests/Cargo.toml \
		--locked --offline --test coordinator \
		bounded_sparse_growth_partial_write_and_truncate_retry_contract -- --nocapture; \
	else \
		echo "ERROR: required coordinator fixture unavailable"; exit 1; \
	fi

$(BUILD_DIR)/ext4-handle-claims-host-test: tools/ext4-handle-claims-host-test.c \
		src/kernel/ext4_fs.c include/openrfs/ext4_fs.h include/openrfs/fat32_fs.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Iinclude \
		tools/ext4-handle-claims-host-test.c $(HOST_THREAD_FLAGS) -Wl,--gc-sections -o $@

$(BUILD_DIR)/vfs-file-claims-host-test $(BUILD_DIR)/vfs-directory-claims-host-test: \
		tools/ext4-handle-claims-host-test.c src/kernel/vfs.c \
		include/openrfs/vfs_backend.h include/openrfs/fat32_fs.h include/openrfs/slot_claim.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Iinclude \
		-DOPENRFS_TEST_VFS_$(if $(findstring directory,$@),DIRECTORIES,FILES) \
		tools/ext4-handle-claims-host-test.c $(HOST_THREAD_FLAGS) -Wl,--gc-sections -o $@

$(BUILD_DIR)/ext4-vfs-host-test $(BUILD_DIR)/ext4-handle-claims-host-test \
		$(BUILD_DIR)/vfs-mutation-host-test $(BUILD_DIR)/ext4-nvme-close-host-test \
		$(BUILD_DIR)/ext4-msix-close-host-test: include/openrfs/slot_claim.h include/openrfs/cpu.h

$(BUILD_DIR)/vfs-mutation-host-test: tools/vfs-mutation-host-test.c \
		src/kernel/vfs.c include/openrfs/vfs_backend.h include/openrfs/fat32_fs.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Iinclude \
		tools/vfs-mutation-host-test.c -Wl,--gc-sections -o $@

$(BUILD_DIR)/vfs-vnode-host-test: tools/vfs-vnode-host-test.c src/kernel/vfs.c \
		include/openrfs/vfs_backend.h include/openrfs/fat32_fs.h include/openrfs/slot_claim.h include/openrfs/cpu.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Iinclude \
		tools/vfs-vnode-host-test.c $(HOST_THREAD_FLAGS) -Wl,--gc-sections -o $@

$(BUILD_DIR)/vfs-mount-host-test: tools/vfs-mount-host-test.c src/kernel/vfs.c \
		include/openrfs/vfs_backend.h include/openrfs/fat32_fs.h include/openrfs/slot_claim.h include/openrfs/cpu.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Iinclude \
		tools/vfs-mount-host-test.c $(HOST_THREAD_FLAGS) -Wl,--gc-sections -o $@

$(BUILD_DIR)/vfs-directory-host-test: tools/vfs-directory-host-test.c src/kernel/vfs.c \
		include/openrfs/vfs_backend.h include/openrfs/fat32_fs.h include/openrfs/slot_claim.h include/openrfs/cpu.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Iinclude \
		tools/vfs-directory-host-test.c $(HOST_THREAD_FLAGS) -Wl,--gc-sections -o $@

$(BUILD_DIR)/ext4-append-contention-host-test: tools/ext4-append-contention-host-test.c src/kernel/ext4_fs.c \
		include/openrfs/ext4_fs.h include/openrfs/nvme.h include/openrfs/slot_claim.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Iinclude \
		tools/ext4-append-contention-host-test.c $(HOST_THREAD_FLAGS) -Wl,--gc-sections -o $@

$(BUILD_DIR)/ext4-registry-host-test: tools/ext4-registry-host-test.c tools/ext4-vfs-host-test.c src/kernel/ext4_fs.c \
		include/openrfs/ext4_fs.h include/openrfs/nvme.h include/openrfs/slot_claim.h include/openrfs/cpu.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Iinclude \
		tools/ext4-registry-host-test.c $(HOST_THREAD_FLAGS) -Wl,--gc-sections -o $@

$(BUILD_DIR)/ext4-storage-cut-host-test: tools/ext4-storage-cut-host-test.c src/kernel/ext4_fs.c include/openrfs/ext4_fs.h include/openrfs/nvme.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Iinclude \
		tools/ext4-storage-cut-host-test.c -Wl,--gc-sections -o $@

ext4-tests: $(BUILD_DIR)/vfs-vnode-host-test $(BUILD_DIR)/vfs-mount-host-test $(BUILD_DIR)/vfs-directory-host-test $(BUILD_DIR)/ext4-append-contention-host-test $(BUILD_DIR)/ext4-registry-host-test $(BUILD_DIR)/ext4-storage-cut-host-test

# These exercise the same close dispositions used by failed filesystem lease
# teardown and native process cleanup, including wrapper reuse after refusal.
$(BUILD_DIR)/native-%-close-host-test: tools/native-%-close-host-test.c \
        src/kernel/native_handle.c src/kernel/native_process.c \
        src/kernel/audio.c src/kernel/vfs.c include/openrfs/native_handle.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Iinclude \
		$< src/kernel/native_handle.c -Wl,--gc-sections -o $@

.PHONY: native-close-tests
native-close-tests: $(BUILD_DIR)/native-file-close-host-test $(BUILD_DIR)/native-audio-close-host-test $(BUILD_DIR)/native-window-close-host-test
	$(BUILD_DIR)/native-file-close-host-test
	$(BUILD_DIR)/native-audio-close-host-test
	$(BUILD_DIR)/native-window-close-host-test

ext4-tests: native-close-tests native-teardown-report-test native-teardown-history-test native-teardown-diagnostics-test

$(BUILD_DIR)/ext4-nvme-close-host-test: tools/ext4-nvme-close-host-test.c \
		src/kernel/nvme.c include/openrfs/nvme.h include/openrfs/dma.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Iinclude \
		tools/ext4-nvme-close-host-test.c -Wl,--gc-sections -o $@

$(BUILD_DIR)/ext4-msix-close-host-test: tools/ext4-msix-close-host-test.c \
		src/kernel/msix.c include/openrfs/msix.h include/openrfs/interrupt_vector.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wconversion -Iinclude \
		tools/ext4-msix-close-host-test.c -Wl,--gc-sections -o $@


$(BUILD_DIR)/shell-ext4-host-test: tools/shell-ext4-host-test.c src/kernel/shell.c \
		include/openrfs/fat32_fs.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Iinclude \
		tools/shell-ext4-host-test.c -Wl,--gc-sections -o $@


$(BUILD_DIR)/keyboard-channel-host-test: tools/keyboard-channel-host-test.c src/kernel/keyboard.c include/openrfs/keyboard.h
	mkdir -p $(BUILD_DIR)
	$(CC) -std=c11 -O2 -flto -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Iinclude \
		tools/keyboard-channel-host-test.c -Wl,--gc-sections -o $@

ext4-tests: tools/ext4_image.py tools/ext4_host_test.py $(BUILD_DIR)/sdk-filesystem-host-test $(BUILD_DIR)/ext4-vfs-host-test $(BUILD_DIR)/ext4-handle-claims-host-test $(BUILD_DIR)/vfs-file-claims-host-test $(BUILD_DIR)/vfs-directory-claims-host-test $(BUILD_DIR)/vfs-mutation-host-test $(BUILD_DIR)/ext4-nvme-close-host-test $(BUILD_DIR)/ext4-msix-close-host-test $(BUILD_DIR)/shell-ext4-host-test $(BUILD_DIR)/keyboard-channel-host-test
	$(PYTHON) tools/ext4_overwrite_device_test.py
	$(PYTHON) tools/filesystem_evidence_test.py
	$(BUILD_DIR)/vfs-mount-host-test
	$(BUILD_DIR)/vfs-vnode-host-test
	$(BUILD_DIR)/vfs-directory-host-test
	$(BUILD_DIR)/ext4-append-contention-host-test
	$(BUILD_DIR)/ext4-registry-host-test
	$(BUILD_DIR)/ext4-storage-cut-host-test
	$(BUILD_DIR)/vfs-file-claims-host-test
	$(BUILD_DIR)/vfs-directory-claims-host-test
	$(BUILD_DIR)/ext4-handle-claims-host-test
	$(BUILD_DIR)/keyboard-channel-host-test
	$(BUILD_DIR)/sdk-filesystem-host-test
	$(BUILD_DIR)/ext4-vfs-host-test
	$(BUILD_DIR)/vfs-mutation-host-test
	$(BUILD_DIR)/ext4-nvme-close-host-test
	$(BUILD_DIR)/ext4-msix-close-host-test
	$(BUILD_DIR)/shell-ext4-host-test
	OPENRFS_EXT4_RUST_FIXTURE='$(abspath $(BUILD_DIR))/ext4-rust-fixture.img' \
		$(PYTHON) -u tools/ext4_host_test.py
	if test -f '$(BUILD_DIR)/ext4-rust-fixture.img'; then \
		OPENRFS_EXT4_RUST_FIXTURE='$(abspath $(BUILD_DIR))/ext4-rust-fixture.img' $(CARGO_TEST_ENV) \
		CARGO_TARGET_DIR='$(abspath $(BUILD_DIR))/ext4-transaction-target' \
		$(CARGO) test \
		--manifest-path tools/ext4-transaction-tests/Cargo.toml \
		--locked --offline -- --include-ignored --nocapture; \
	else \
		echo "ERROR: required ext4 fixture unavailable"; exit 1; \
	fi

package-repository-tests: tools/openrfs-repository.py \
		tools/openrfs_repository_host_test.py tools/openrfs-package.py
	OPENRFS_REQUIRE_ED25519=1 $(PYTHON) -u tools/openrfs_repository_host_test.py

package-transaction-tests: tools/openrfs-transaction.py \
		tools/openrfs_transaction_host_test.py tools/openrfs-package.py
	$(PYTHON) -u tools/openrfs_transaction_host_test.py

$(PACKAGE_STATE_HOST_TEST): tools/package-state-host-test.c \
		src/kernel/package_generation.c src/kernel/package_state.c \
		include/openrfs/package_generation.h include/openrfs/package_state.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Wshadow \
		-Wundef -Wstrict-prototypes -Wmissing-prototypes -Iinclude \
		tools/package-state-host-test.c src/kernel/package_generation.c \
		src/kernel/package_state.c -o $@

package-state-tests: $(PACKAGE_STATE_HOST_TEST)
	$(PACKAGE_STATE_HOST_TEST)

$(PACKAGE_SERVICE_HOST_TEST): tools/package-service-host-test.c \
		tools/package-state-host-test.c src/kernel/package_service.c \
		src/kernel/package_generation.c src/kernel/package_state.c \
		include/openrfs/package_builder.h include/openrfs/package_generation.h \
		include/openrfs/package_manager.h include/openrfs/package_service.h \
		include/openrfs/package_state.h include/openrfs/fat32_fs.h include/openrfs/heap.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Wshadow \
		-Wundef -Wstrict-prototypes -Wmissing-prototypes -Iinclude \
		tools/package-service-host-test.c src/kernel/package_service.c \
		src/kernel/package_generation.c src/kernel/package_state.c -o $@

package-service-tests: $(PACKAGE_SERVICE_HOST_TEST)
	$(PACKAGE_SERVICE_HOST_TEST)

$(PACKAGE_UPLOAD_HOST_TEST): tools/package-upload-host-test.c \
		src/kernel/package_upload.c src/kernel/package_state.c \
		src/kernel/native_handle.c \
		include/openrfs/package_upload.h include/openrfs/package_state.h \
		include/openrfs/fat32_fs.h include/openrfs/native_handle.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Wshadow \
		-Wundef -Wstrict-prototypes -Wmissing-prototypes -Iinclude \
		tools/package-upload-host-test.c src/kernel/package_upload.c \
		src/kernel/package_state.c src/kernel/native_handle.c -o $@

$(TEST_BUILD_DIR)/package-upload-claims-host-test$(HOST_EXEEXT): tools/package-upload-claims-host-test.c \
		tools/package-upload-host-test.c src/kernel/package_upload.c src/kernel/package_state.c \
		src/kernel/native_handle.c include/openrfs/package_upload.h \
		include/openrfs/package_state.h include/openrfs/fat32_fs.h \
		include/openrfs/native_handle.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Wshadow \
		-Wundef -Wstrict-prototypes -Wmissing-prototypes $(HOST_THREAD_FLAGS) -Iinclude \
		tools/package-upload-claims-host-test.c src/kernel/package_upload.c \
		src/kernel/package_state.c src/kernel/native_handle.c -o $@

package-upload-tests: $(PACKAGE_UPLOAD_HOST_TEST) $(TEST_BUILD_DIR)/package-upload-claims-host-test$(HOST_EXEEXT)
	$(PACKAGE_UPLOAD_HOST_TEST)
	$(TEST_BUILD_DIR)/package-upload-claims-host-test$(HOST_EXEEXT)

$(NATIVE_TEARDOWN_REPORT_HOST_TEST): tools/native-teardown-report-host-test.c \
		src/kernel/native_process.c src/kernel/native_handle.c \
		include/openrfs/native_process.h include/openrfs/native_handle.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Wshadow \
		-Wundef -Wstrict-prototypes -Wmissing-prototypes -Iinclude \
		-fsyntax-only src/kernel/native_process.c
	$(CC) -std=c11 -O2 -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wundef \
		-Wstrict-prototypes -Wmissing-prototypes -Iinclude \
		tools/native-teardown-report-host-test.c src/kernel/native_handle.c \
		-Wl,--gc-sections -o $@

.PHONY: native-teardown-report-test
native-teardown-report-test: $(NATIVE_TEARDOWN_REPORT_HOST_TEST)
	$(NATIVE_TEARDOWN_REPORT_HOST_TEST)

$(NATIVE_TEARDOWN_HISTORY_HOST_TEST): tools/native-teardown-history-host-test.c \
		src/kernel/native_process_teardown.c src/kernel/native_handle.c \
		include/openrfs/native_process.h include/openrfs/native_handle.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wundef \
		-Wstrict-prototypes -Wmissing-prototypes -Iinclude \
		tools/native-teardown-history-host-test.c \
		src/kernel/native_process_teardown.c src/kernel/native_handle.c \
		-Wl,--gc-sections -o $@

.PHONY: native-teardown-history-test
native-teardown-history-test: $(NATIVE_TEARDOWN_HISTORY_HOST_TEST)
	$(NATIVE_TEARDOWN_HISTORY_HOST_TEST)

$(NATIVE_TEARDOWN_DIAGNOSTICS_HOST_TEST): \
		tools/native-teardown-diagnostics-host-test.c \
		src/kernel/native_teardown_diagnostics.c \
		src/kernel/native_process_teardown.c src/kernel/native_handle.c \
		include/openrfs/native_teardown_diagnostics.h \
		include/openrfs/native_process.h include/openrfs/native_handle.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -ffunction-sections -fdata-sections \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wundef \
		-Wstrict-prototypes -Wmissing-prototypes -Iinclude \
		tools/native-teardown-diagnostics-host-test.c \
		src/kernel/native_teardown_diagnostics.c \
		src/kernel/native_process_teardown.c src/kernel/native_handle.c \
		-Wl,--gc-sections -o $@

.PHONY: native-teardown-diagnostics-test
native-teardown-diagnostics-test: $(NATIVE_TEARDOWN_DIAGNOSTICS_HOST_TEST)
	$(NATIVE_TEARDOWN_DIAGNOSTICS_HOST_TEST)

$(TEST_BUILD_DIR)/monocypher/monocypher.o: \
		vendor/monocypher/src/monocypher.c vendor/monocypher/src/monocypher.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -fno-tree-vectorize \
		-Ivendor/monocypher/src -c $< -o $@

$(TEST_BUILD_DIR)/monocypher/monocypher-ed25519.o: \
		vendor/monocypher/src/optional/monocypher-ed25519.c \
		vendor/monocypher/src/optional/monocypher-ed25519.h \
		vendor/monocypher/src/monocypher.h
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -fno-tree-vectorize \
		-Ivendor/monocypher/src -Ivendor/monocypher/src/optional \
		-c $< -o $@

$(PACKAGE_TRUST_HOST_TEST): tools/package-trust-host-test.c \
		src/kernel/package_platform_trust.c src/kernel/package_trust.c \
		src/kernel/package_state.c $(PACKAGE_TRUST_ASSET_C) \
		include/openrfs/package_platform_trust.h \
		include/openrfs/package_trust.h include/openrfs/package_manager.h \
		include/openrfs/package_state.h $(MONOCYPHER_HOST_OBJECTS)
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Wshadow \
		-Wundef -Wstrict-prototypes -Wmissing-prototypes -Iinclude \
		-Ivendor/monocypher/src -Ivendor/monocypher/src/optional \
		tools/package-trust-host-test.c src/kernel/package_platform_trust.c \
		src/kernel/package_trust.c $(PACKAGE_TRUST_ASSET_C) \
		src/kernel/package_state.c $(MONOCYPHER_HOST_OBJECTS) -o $@

package-trust-tests: $(PACKAGE_TRUST_HOST_TEST)
	cd vendor/monocypher && sha256sum --check SOURCE-MANIFEST.sha256
	$(PACKAGE_TRUST_HOST_TEST)

$(PACKAGE_MANAGER_HOST_TEST): tools/package-manager-host-test.c \
		src/kernel/package_builder.c src/kernel/package_generation.c \
		src/kernel/package_manager.c src/kernel/package_trust.c \
		src/kernel/package_state.c include/openrfs/package_builder.h \
		include/openrfs/package_generation.h include/openrfs/package_manager.h \
		include/openrfs/package_trust.h include/openrfs/package_state.h \
		$(MONOCYPHER_HOST_OBJECTS)
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Wshadow \
		-Wundef -Wstrict-prototypes -Wmissing-prototypes -Iinclude \
		-Ivendor/monocypher/src -Ivendor/monocypher/src/optional \
		tools/package-manager-host-test.c src/kernel/package_builder.c \
		src/kernel/package_generation.c src/kernel/package_manager.c \
		src/kernel/package_trust.c src/kernel/package_state.c \
		$(MONOCYPHER_HOST_OBJECTS) -o $@

$(PACKAGE_CONTROL_HOST_TEST): tools/package-control-host-test.c \
		src/kernel/package_control.c src/kernel/package_builder.c \
		src/kernel/package_generation.c src/kernel/package_manager.c \
		src/kernel/package_trust.c src/kernel/package_state.c \
		include/openrfs/package_control.h include/openrfs/package_builder.h \
		include/openrfs/package_generation.h include/openrfs/package_manager.h \
		include/openrfs/package_trust.h include/openrfs/package_state.h \
		$(MONOCYPHER_HOST_OBJECTS)
	mkdir -p $(dir $@)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Wshadow \
		-Wundef -Wstrict-prototypes -Wmissing-prototypes -Iinclude \
		-Ivendor/monocypher/src -Ivendor/monocypher/src/optional \
		tools/package-control-host-test.c src/kernel/package_control.c \
		src/kernel/package_builder.c src/kernel/package_generation.c \
		src/kernel/package_manager.c src/kernel/package_trust.c \
		src/kernel/package_state.c $(MONOCYPHER_HOST_OBJECTS) -o $@

package-control-tests: $(PACKAGE_MANAGER_HOST_TEST) $(PACKAGE_CONTROL_HOST_TEST) \
		tools/package_manager_host_test.py tools/openrfs-repository.py \
		tools/openrfs-package.py
	OPENRFS_REQUIRE_ED25519=1 $(PYTHON) -u \
		tools/package_manager_host_test.py $(PACKAGE_MANAGER_HOST_TEST) \
			$(PACKAGE_CONTROL_HOST_TEST)

package-manager-tests: package-control-tests

$(ZLIB_HOST_TEST): tools/zlib-host-test.c sdk/src/zlib.c \
		sdk/include/openrfs/zlib.h $(ZLIB_SOURCE) $(ZLIB_HEADERS)
	mkdir -p $(dir $@)
	$(CC) -Ivendor/zlib/include -Ivendor/zlib/src -idirafter sdk/include \
		$(ZLIB_DEFINES) -std=c11 -O2 -Wall -Wextra -Werror \
		-Wpedantic -Wshadow -Wundef -Wstrict-prototypes \
		-Wmissing-prototypes tools/zlib-host-test.c sdk/src/zlib.c \
		$(ZLIB_SOURCE) -o $@

zlib-tests: $(ZLIB_HOST_TEST)
	$(ZLIB_HOST_TEST)

dynamic-elf-tests: src/rust/elf64_dynamic.rs \
		tools/elf64-dynamic-host-test.rs
	$(RUSTC) --edition 2024 --test -D warnings \
		tools/elf64-dynamic-host-test.rs -o $(RUST_DYNAMIC_ELF64_TEST)
	$(RUST_DYNAMIC_ELF64_TEST)

$(TLS_HOST_WRAPPER_OBJECT): sdk/src/tls.c sdk/include/openrfs/tls.h
	mkdir -p $(dir $@)
	$(CC) -Isdk/include -Iinclude -Ivendor/bearssl/inc -std=c11 -O2 \
		-Wall -Wextra -Werror -Wpedantic -Wshadow -Wundef \
		-Wstrict-prototypes -Wmissing-prototypes -c $< -o $@

$(TLS_HOST_OBJECT): tools/tls-client-host-test.c \
		sdk/include/openrfs/tls.h
	mkdir -p $(dir $@)
	$(CC) -Iinclude -Ivendor/bearssl/inc -idirafter sdk/include \
		-std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Wshadow \
		-Wundef -Wstrict-prototypes -Wmissing-prototypes \
		$(HOST_THREAD_FLAGS) -c $< -o $@

$(TLS_HOST_TEST): $(TLS_HOST_OBJECT) $(TLS_HOST_WRAPPER_OBJECT) \
		$(TLS_HOST_BEARSSL_LIB)
	$(CC) $^ $(HOST_THREAD_FLAGS) $(HOST_SOCKET_LIBS) -o $@

tls-tests: $(TLS_HOST_TEST) tools/tls_host_test.py \
		tests/fixtures/tls/anchor.txt tests/fixtures/tls/ca.pem \
		tests/fixtures/tls/valid.pem tests/fixtures/tls/valid-key.pem \
		tests/fixtures/tls/expired.pem tests/fixtures/tls/expired-key.pem \
		tests/fixtures/tls/future.pem tests/fixtures/tls/future-key.pem \
		tests/fixtures/tls/untrusted.pem tests/fixtures/tls/untrusted-key.pem
	$(PYTHON) -u tools/tls_host_test.py $(TLS_HOST_TEST)

$(HTTPS_HOST_OBJECT): tools/https-client-host-test.c \
		apps/native-https/trust_anchor.h sdk/include/openrfs/tls.h
	mkdir -p $(dir $@)
	$(CC) -Iinclude -Ivendor/bearssl/inc -idirafter sdk/include \
		-std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Wshadow \
		-Wundef -Wstrict-prototypes -Wmissing-prototypes \
		$(HOST_THREAD_FLAGS) -c $< -o $@

$(HTTPS_HOST_TEST): $(HTTPS_HOST_OBJECT) $(TLS_HOST_WRAPPER_OBJECT) \
		$(TLS_HOST_BEARSSL_LIB)
	$(CC) $^ $(HOST_THREAD_FLAGS) $(HOST_SOCKET_LIBS) -o $@

https-tests: $(HTTPS_HOST_TEST) tools/https_host_test.py \
		tools/network_packet_audit_test.py tools/network_packet_audit.py \
		tools/https_anchor.py tools/https_network_fixture.py \
		tests/fixtures/tls/ca.pem tests/fixtures/tls/valid.pem \
		tests/fixtures/tls/valid-key.pem tests/fixtures/tls/expired.pem \
		tests/fixtures/tls/expired-key.pem tests/fixtures/tls/future.pem \
		tests/fixtures/tls/future-key.pem tests/fixtures/tls/untrusted.pem \
		tests/fixtures/tls/untrusted-key.pem
	$(PYTHON) tools/https_anchor.py audit
	$(PYTHON) tools/network_packet_audit_test.py
	$(PYTHON) tools/https_network_fixture.py --self-test
	$(PYTHON) -u tools/https_host_test.py $(HTTPS_HOST_TEST)

$(PACKAGE_FETCH_HOST_TEST): tools/package-fetch-host-test.c \
		sdk/src/package_fetch.c sdk/include/openrfs/package_fetch.h \
		sdk/include/openrfs/package_upload.h \
		sdk/include/openrfs/tls.h include/openrfs/abi.h \
		$(TLS_HOST_BEARSSL_LIB)
	mkdir -p $(dir $@)
	$(CC) -Iinclude -Ivendor/bearssl/inc -idirafter sdk/include \
		-std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Wshadow \
		-Wundef -Wstrict-prototypes -Wmissing-prototypes \
		tools/package-fetch-host-test.c sdk/src/package_fetch.c \
		$(TLS_HOST_BEARSSL_LIB) -o $@

package-fetch-tests: $(PACKAGE_FETCH_HOST_TEST)
	$(PACKAGE_FETCH_HOST_TEST)

$(EXT4_FIXTURE): tools/ext4_image.py
	mkdir -p $(dir $@)
	$(PYTHON) tools/ext4_image.py build $@ --report $@.json

ext4-images: $(EXT4_FIXTURE)

INSTALLER_PORT_TEST := $(BUILD_DIR)/tools/installer-port-test

$(INSTALLER_PORT_TEST): tools/installer-port-test.c \
		src/kernel/orfs_term.c src/kernel/orfs_ui.c src/kernel/orfs_install.c \
		include/orfs/term.h include/orfs/ui.h include/orfs/install.h \
		include/orfs/line.h include/orfs/version.h
	mkdir -p $(dir $@)
	$(CC) -Iinclude -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
		-Wshadow -Wundef -Wstrict-prototypes -Wmissing-prototypes \
		tools/installer-port-test.c src/kernel/orfs_term.c \
		src/kernel/orfs_ui.c src/kernel/orfs_install.c -o $@

installer-port-test: $(INSTALLER_PORT_TEST)
	$(INSTALLER_PORT_TEST)

MINIMAL_DE_HOST_TEST := $(BUILD_DIR)/tools/minimal-de-host-test

$(MINIMAL_DE_HOST_TEST): tools/minimal-de-host-test.c \
		src/kernel/minimal_de.c include/openrfs/minimal_de.h \
		$(wildcard src/kernel/trait_*.c) \
		$(wildcard src/kernel/trait_*.h) \
		$(wildcard include/trait/*.h)
	mkdir -p $(dir $@)
	$(CC) -Iinclude -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
		-Wshadow -Wundef -Wstrict-prototypes -Wmissing-prototypes \
		tools/minimal-de-host-test.c src/kernel/minimal_de.c \
		$(wildcard src/kernel/trait_*.c) -o $@

.PHONY: minimal-de-host-test
minimal-de-host-test: $(MINIMAL_DE_HOST_TEST)
	$(MINIMAL_DE_HOST_TEST)

verify: toolchain lint installer-port-test minimal-de-host-test
ifneq ($(VERIFY_CLEAN),0)
	$(MAKE) clean
endif
	$(MAKE) kernel
	$(MAKE) --jobs=$(VERIFY_JOBS) \
		wall-clock-tests ext4-tests package-repository-tests \
		package-transaction-tests package-state-tests package-service-tests \
		package-trust-asset-tests package-trust-tests package-control-tests \
		package-fetch-tests package-upload-tests \
		dynamic-elf-tests \
		https-tests tls-tests zlib-tests sdl-preference-tests
	$(PYTHON) tools/verify-ui-assets.py
	@test '$(LOGO_MAX_DIMENSION)' -eq 280
	$(PYTHON) tools/make-fat16-fixture.py $(FILESYSTEM_FIXTURE)
	@test "$$(sha256sum $(FILESYSTEM_FIXTURE) | awk '{ print toupper($$1) }')" = \
		'34A217787FD60E7C528DEF6E2B4F280A5011465A736730A50FE8FA85845D86A5'
	$(PYTHON) tools/make-elf64-fixture.py $(PROCESS_ELF)
	@test "$$(sha256sum $(PROCESS_ELF) | awk '{ print toupper($$1) }')" = \
		'C923A94F08DF64523D3DB701E4F9FC5FF5B51DFC21447E1DC57586D40D42B8A9'
	$(PYTHON) tools/make-process-fixture.py $(PROCESS_FIXTURE)
	@test "$$(sha256sum $(PROCESS_FIXTURE) | awk '{ print toupper($$1) }')" = \
		'9BDC1FE33DF03F28CA07605F85A28D8C7CDFF240C9304F39007E7D7C1B979ED3'
	$(RUSTC) --edition 2024 --test -D warnings src/rust/fat16.rs \
		-o $(RUST_FAT16_TEST)
	$(RUST_FAT16_TEST)
	$(RUSTC) --edition 2024 --test -D warnings src/rust/fat32.rs \
		-o $(RUST_FAT32_TEST)
	$(RUST_FAT32_TEST)
	$(PYTHON) -u tools/fat32_host_test.py
	$(RUSTC) --edition 2024 --test -D warnings \
		tools/native-image-host-test.rs -o $(RUST_NATIVE_IMAGE_TEST)
	$(RUST_NATIVE_IMAGE_TEST)
	OPENRFS_REQUIRE_ED25519=1 $(PYTHON) -u tools/openrfs_package_host_test.py
	$(MAKE) $(LINUX_ABI_FIXTURE)
	@test "$$(sha256sum $(LINUX_ABI_FIXTURE) | awk '{ print toupper($$1) }')" = \
		'4E7D0FEB6F6356503E968EA8BBF1A76924CCC2B35BDD4CD245106685A6CFC9FB'
	OPENRFS_BUSYBOX_BINARY='$(abspath $(BUSYBOX_BINARY))' \
		$(RUSTC) --edition 2024 --test -D warnings \
		tools/linux-fat16-host-test.rs -o $(RUST_LINUX_FAT16_TEST)
	$(RUST_LINUX_FAT16_TEST)
	OPENRFS_BUSYBOX_BINARY='$(abspath $(BUSYBOX_BINARY))' \
		$(RUSTC) --edition 2024 --test -D warnings \
		tools/linux-elf64-host-test.rs -o $(RUST_LINUX_ELF64_TEST)
	$(RUST_LINUX_ELF64_TEST)
	$(MAKE) $(LINUX_UNAME_FIXTURE)
	@test "$$(sha256sum $(LINUX_UNAME_FIXTURE) | awk '{ print toupper($$1) }')" = \
		'FC92FE49F976F42BC2DBDEA2692A220E3F7C46981F269D886A6967AB09445715'
	OPENRFS_UNAME_BUSYBOX_BINARY='$(abspath $(BUSYBOX_UNAME_BINARY))' \
		$(RUSTC) --edition 2024 --test -D warnings \
		tools/linux-uname-fat16-host-test.rs \
		-o $(RUST_LINUX_UNAME_FAT16_TEST)
	$(RUST_LINUX_UNAME_FAT16_TEST)
	OPENRFS_UNAME_BUSYBOX_BINARY='$(abspath $(BUSYBOX_UNAME_BINARY))' \
		$(RUSTC) --edition 2024 --test -D warnings \
		tools/linux-uname-elf64-host-test.rs \
		-o $(RUST_LINUX_UNAME_ELF64_TEST)
	$(RUST_LINUX_UNAME_ELF64_TEST)
	$(MAKE) $(BUSYBOX_CAT_BINARY)
	@test "$$(sha256sum $(BUSYBOX_CAT_BINARY) | awk '{ print toupper($$1) }')" = \
		'8191596A22778B575942895071A2E50CCEEE0F82F4D88B6D986584CE0914FC3E'
	OPENRFS_CAT_BUSYBOX_BINARY='$(abspath $(BUSYBOX_CAT_BINARY))' \
		$(RUSTC) --edition 2024 --test -D warnings \
		tools/linux-cat-fat16-host-test.rs \
		-o $(RUST_LINUX_CAT_FAT16_TEST)
	$(RUST_LINUX_CAT_FAT16_TEST)
	OPENRFS_CAT_BUSYBOX_BINARY='$(abspath $(BUSYBOX_CAT_BINARY))' \
		$(RUSTC) --edition 2024 --test -D warnings \
		tools/linux-cat-elf64-host-test.rs \
		-o $(RUST_LINUX_CAT_ELF64_TEST)
	$(RUST_LINUX_CAT_ELF64_TEST)
	$(MAKE) $(OPENRFS_PROOF_USERLAND_IMAGE)
	@test "$$(sha256sum $(OPENRFS_PROOF_USERLAND_IMAGE) | awk '{ print toupper($$1) }')" = \
		'29E49384CBF65B9F54516B875ABE183C69072DF461C76F7F6DD8E497055D4D21'
	$(MAKE) $(OPENRFS_PROOF_USERLAND_NO_CAT_IMAGE)
	@test "$$(sha256sum $(OPENRFS_PROOF_USERLAND_NO_CAT_IMAGE) | awk '{ print toupper($$1) }')" = \
		'B2AA321D73954E110BA914254E798832684C4F1F7051D63FB4C18AE0CF848E51'
	$(MAKE) $(FAT32_SYSTEM_IMAGE) $(FAT32_DATA_IMAGE) \
		$(FAT32_FULL_IMAGE) $(FAT32_CORRUPT_IMAGE)
	@test "$$(sha256sum $(FAT32_SYSTEM_IMAGE) | awk '{ print toupper($$1) }')" = \
		'CD116CB5755270BF1E6F20FBAA6F1505BACF184C6A8B19F15EA54B6DC16CE310'
	@test "$$(sha256sum $(FAT32_DATA_IMAGE) | awk '{ print toupper($$1) }')" = \
		'87017AF6336746D314B36C57DC9B30B751B9EF017C2FA607E71279F7E38F6DC4'
	@test "$$(sha256sum $(FAT32_FULL_IMAGE) | awk '{ print toupper($$1) }')" = \
		'657B57CC3072853A52EE3A7A54639E49DE3E4FAFAB2E4E2A436D3A46D00CF6DB'
	@test "$$(sha256sum $(FAT32_CORRUPT_IMAGE) | awk '{ print toupper($$1) }')" = \
		'79077EF749491B0C641E9D45273629508B051E47DAF7A1D49F9BD03992EFB852'
	rm -rf $(BUILD_DIR)/fat32-reconstruction
	mkdir -p $(BUILD_DIR)/fat32-reconstruction
	$(PYTHON) tools/fat32_image.py format system \
		$(BUILD_DIR)/fat32-reconstruction/system.raw \
		--echo $(BUSYBOX_BINARY) --uname $(BUSYBOX_UNAME_BINARY) \
		--cat $(BUSYBOX_CAT_BINARY)
	$(PYTHON) tools/fat32_image.py format data \
		$(BUILD_DIR)/fat32-reconstruction/data.raw
	cmp $(FAT32_SYSTEM_IMAGE) $(BUILD_DIR)/fat32-reconstruction/system.raw
	cmp $(FAT32_DATA_IMAGE) $(BUILD_DIR)/fat32-reconstruction/data.raw
	$(PYTHON) tools/fat32_image.py verify system $(FAT32_SYSTEM_IMAGE) \
		--echo $(BUSYBOX_BINARY) --uname $(BUSYBOX_UNAME_BINARY) \
		--cat $(BUSYBOX_CAT_BINARY)
	$(PYTHON) tools/fat32_image.py verify data $(FAT32_DATA_IMAGE)
	$(PYTHON) tools/fat32_image.py verify data $(FAT32_FULL_IMAGE) --full
	$(RUSTC) --edition 2024 --test -D warnings src/rust/elf64.rs \
		-o $(RUST_ELF64_TEST)
	$(RUST_ELF64_TEST)
	# Bytes from an NVIDIA board's ROM are bytes from outside, so the parser
	# is Rust's and its controls run on the host as well as in the kernel.
	$(RUSTC) --edition 2024 --test -D warnings src/rust/nvbios.rs \
		-o $(RUST_NVBIOS_TEST)
	$(RUST_NVBIOS_TEST)
	$(PYTHON) tools/nvidia_vbios_image.py --self-test
	# The kernel's reference VBIOS table, the Rust validator that parses it,
	# and the Python record that rebuilds it are three independent statements
	# of the same 1,024 bytes.
	$(PYTHON) tools/check-nvidia-vbios.py
	# The kernel's multiprocess executable table, the Rust profile that
	# validates it, and the Python record that rebuilds it are three
	# independent statements of the same 256 bytes. Any two disagreeing is a
	# build failure rather than a program that quietly does something else.
	$(PYTHON) tools/check-multiprocess-image.py
	@test "$(words $(TEST_SCENARIOS))" -eq \
		'$(EXPECTED_TEST_SCENARIO_COUNT)'
	@grep -Fq '#define SHELL_PROMPT "openrfs$$ "' src/kernel/shell.c
	grub-file --is-x86-multiboot2 $(KERNEL)
	readelf -h $(KERNEL) | grep -Eq 'Class:[[:space:]]+ELF64'
	readelf -h $(KERNEL) | grep -Eq 'Machine:[[:space:]]+Advanced Micro Devices X86-64'
	@test -z "$$($(NM) -u $(KERNEL))" || { $(NM) -u $(KERNEL); exit 1; }
	@if readelf -W -r $(KERNEL) | grep -Eq 'R_X86_64_'; then \
		echo 'kernel contains unresolved relocation records'; \
		readelf -W -r $(KERNEL); exit 1; \
	fi
	@test "$$($(NM) $(KERNEL) | grep -Ec ' [tT] interrupt_vector_[0-9]+$$')" -eq 256
	@$(OBJDUMP) -d $(KERNEL) | grep -Fq 'iretq'
	@$(OBJDUMP) -d $(KERNEL) | grep -Fq 'ltr'
	@$(OBJDUMP) -d $(KERNEL) | grep -Fq 'lidt'
	# This inspects the ELF file, and for a long time it was the only thing
	# behind OpenRFS's W^X claim - while the kernel ran on boot.S's huge pages
	# with no NX bit enabled at all. It is kept because it catches a bad link
	# before anything boots, but the guarantee now rests on paging.c walking
	# the installed tables at runtime.
	@if readelf -W -l $(KERNEL) | grep -Eq 'LOAD[[:space:]].*RWE'; then \
		echo "kernel contains an RWX load segment"; exit 1; \
	fi
	@$(OBJDUMP) -d $(KERNEL) | grep -Fq 'invlpg'
	# The Multiboot entry is genuinely 32-bit code in an ELF64 image. Check that
	# input section in 32-bit mode; otherwise objdump consumes its four-byte
	# absolute addresses as eight-byte operands and can invent instructions from
	# adjacent bytes whenever the kernel's BSS layout changes.
	@boot_forbidden="$$( $(OBJDUMP) -d -j .boot.text32 -mi386 \
		--no-show-raw-insn $(BUILD_DIR)/arch_boot.o | \
		grep -Ei '%(xmm|ymm|zmm|mm|k)[0-9]+|^[[:space:]]*[0-9a-f]+:[[:space:]]+(f[a-z0-9]+|emms|fxsave|fxrstor|ldmxcsr|stmxcsr|v[a-z0-9]+)([[:space:]]|$$)' | \
		grep -Ev '[[:space:]](verr|verw)[[:space:]]' || true )"; \
		test -z "$$boot_forbidden" || { echo '32-bit boot entry contains floating-point, MMX, SSE, or AVX instructions'; echo "$$boot_forbidden"; exit 1; }
	@forbidden="$$( $(OBJDUMP) -d -j .text --no-show-raw-insn $(KERNEL) | \
		awk '/^[[:space:]]*[0-9a-f]+ <[^>]+>:/ { allowed = \
			($$0 ~ / <(_start|native_fx(save|rstor|init))>:/) } !allowed { print }' | \
		grep -Ei '%(xmm|ymm|zmm|mm|k)[0-9]+|^[[:space:]]*[0-9a-f]+:[[:space:]]+(f[a-z0-9]+|emms|fxsave|fxrstor|ldmxcsr|stmxcsr|v[a-z0-9]+)([[:space:]]|$$)' | \
		grep -Ev '[[:space:]](verr|verw)[[:space:]]' || true )"; \
		test -z "$$forbidden" || { echo 'kernel contains floating-point, MMX, SSE, or AVX instructions'; echo "$$forbidden"; exit 1; }
	@$(NM) $(KERNEL) | grep -Eq ' [ABDRTt] __text_start$$'
	@$(NM) $(KERNEL) | grep -Eq ' [ABDRTt] __rodata_start$$'
	@$(NM) $(KERNEL) | grep -Eq ' [ABDRTt] __data_start$$'
	# The Rust half has to actually be in the image, and has to have been
	# linked as ordinary code rather than as something with its own runtime.
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_logo_decode$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_logo_self_test$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_font_glyph$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_font_self_test$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_ui_font_glyph$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_ui_font_self_test$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_fat16_parse_bpb$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_fat16_find_root$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_fat16_parse_fat$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_fat16_validate_extent$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_fat16_validate_payload$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_fat32_parse_bpb$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_fat32_parse_fsinfo$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_fat32_validate_fat_pair$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_fat32_parse_directory_entry$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_linux_fat16_find_root$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_linux_fat16_build_chain$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_linux_fat16_validate_payload$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_linux_elf64_parse$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_linux_elf64_self_test$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_linux_uname_fat16_find_root$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_linux_uname_fat16_build_chain$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_linux_uname_fat16_validate_payload$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_linux_uname_elf64_parse$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_linux_uname_elf64_self_test$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_linux_cat_fat16_find_root$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_linux_cat_fat16_build_chain$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_linux_cat_fat16_validate_payload$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_linux_cat_elf64_parse$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_linux_cat_elf64_self_test$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_elf64_parse$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_elf64_self_test$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_native_image_validate$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_native_image_self_test$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_multiprocess_elf64_parse$$'
	@$(NM) $(KERNEL) | grep -Eq ' T openrfs_multiprocess_elf64_self_test$$'
	# Hostile ext4 metadata is parsed by safe Rust and may retain compiler-
	# inserted bounds traps. Those traps are a corruption backstop, not an
	# unwinding runtime: require every one to terminate through OpenRFS's panic
	# handler and reject any linked exception personality or unwinder.
	@if $(NM) $(KERNEL) | grep -Eq 'panic_bounds_check'; then \
		$(NM) $(KERNEL) | grep -Eq ' [tT] .*rust_begin_unwind' && \
		$(OBJDUMP) -d $(KERNEL) | \
			awk '/^[[:space:]]*[0-9a-f]+ <[^>]*rust_begin_unwind>:/ { inside = 1; next } \
				inside && /^[[:space:]]*[0-9a-f]+ <[^>]+>:/ { inside = 0 } \
				inside && /[[:space:]]call.*<[^>]*openrfs3abi5panic[^>]*>/ { found = 1 } \
				END { exit !found }' && \
		$(OBJDUMP) -d $(KERNEL) | \
			awk '/^[[:space:]]*[0-9a-f]+ <[^>]*openrfs3abi5panic[^>]*>:/ { inside = 1; next } \
				inside && /^[[:space:]]*[0-9a-f]+ <[^>]+>:/ { inside = 0 } \
				inside && /[[:space:]]call.*<console_panic>/ { found = 1 } \
				END { exit !found }'; \
	fi
	@if $(NM) $(KERNEL) | grep -Eq \
		'(_Unwind_|rust_eh_personality|__gcc_personality_v0|panic_unwind)'; then \
		echo 'kernel Rust linked an unwinding runtime or exception personality'; \
		exit 1; \
	fi
	# Paging and the scenario runner must stay coupled to one typed aggregate,
	# never grow hardware-specific parameters or hidden firmware reads again.
	@grep -Fq 'paging_initialize(const struct paging_device_windows *windows);' \
		include/openrfs/paging.h
	@! grep -Eq 'struct (acpi_topology|acpi_mcfg|boot_framebuffer)' \
		src/kernel/paging.c
	@grep -Fq 'const struct kernel_test_context *context' \
		include/openrfs/test.h
	# Migrated boot operations are reachable only from typed ledger descriptors.
	@if grep -ERn \
		'\b(prove_frame_lifecycle|install_page_tables|prove_paging_lifecycle|prove_write_combining|bring_up_heap|prove_heap_lifecycle|prove_timer_route|retire_legacy_interrupt_path|prove_level_route|prove_pm_timer|prove_apic_timer|prove_tsc|retire_pit|prove_clocks_without_pit|prove_monotonic_time|bring_up_pci|prove_threads|prove_preemption|prove_framebuffer|prove_surface|draw_logo|prove_screen_console|prove_keyboard|prove_shell)[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=boot_plan.c --exclude=boot_proofs.c; then \
		echo 'migrated boot stage bypasses the Boot Ledger'; exit 1; \
	fi
	@if grep -ERn \
		'\b(ui_font_initialize|pointer_initialize|ui_construct|ui_activate)[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=boot_plan.c \
		--exclude=ui.c --exclude=ui_font.c --exclude=pointer.c; then \
		echo 'OpenRFS boot stage bypasses the Boot Ledger'; exit 1; \
	fi
	@if grep -ERn \
		'\b(pci_resource_initialize|interrupt_vector_initialize|dma_initialize|device_substrate_prove)[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=boot_plan.c \
		--exclude=pci_resource.c --exclude=interrupt_vector.c \
		--exclude=dma.c --exclude=virtio_rng_proof.c; then \
		echo 'device foundation boot operation bypasses the Boot Ledger'; exit 1; \
	fi
	@if grep -ERn '\bxhci_descriptor_prove[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=boot_plan.c --exclude=xhci.c; then \
		echo 'xHCI descriptor proof bypasses the Boot Ledger'; exit 1; \
	fi
	@if grep -ERn '\bnvme_read_prove[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=boot_plan.c --exclude=nvme.c; then \
		echo 'NVMe read proof bypasses the Boot Ledger'; exit 1; \
	fi
	@if grep -ERn '\bfilesystem_file_prove[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=boot_plan.c \
		--exclude=filesystem.c; then \
		echo 'filesystem file proof bypasses the Boot Ledger'; exit 1; \
	fi
	@if grep -ERn '\bprocess_installed_prove[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=boot_plan.c \
		--exclude=process.c; then \
		echo 'process proof bypasses the Boot Ledger'; exit 1; \
	fi
	@if grep -ERn '\bmultiprocess_prove[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=boot_plan.c \
		--exclude=multiprocess.c; then \
		echo 'multiprocess proof bypasses the Boot Ledger'; exit 1; \
	fi
	@if grep -ERn '\bdriver_matrix_bind[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=boot_plan.c \
		--exclude=driver.c; then \
		echo 'driver matrix bind bypasses the Boot Ledger'; exit 1; \
	fi
	@if grep -ERn '\baudio_prove[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=boot_plan.c \
		--exclude=audio.c; then \
		echo 'HD Audio proof bypasses the Boot Ledger'; exit 1; \
	fi
	# The one driver that lets a device write kernel memory has to withdraw
	# that permission before it reclaims the memory. OpenRFS has no IOMMU, so
	# the order is the whole guarantee: engines stopped, controller reset, bus
	# mastering disabled, and only then the rings released.
	@grep -Fq 'PCI_RESOURCE_STATUS_DMA_NOT_PREPARED' src/kernel/audio.c || \
		{ echo 'the audio driver stopped proving the DMA guard'; exit 1; }
	@stop=$$(grep -n 'mmio_write8(controller->registers, HDA_RIRBCTL, 0U);' \
		src/kernel/audio.c | head -n 1 | cut -d: -f1); \
		reset=$$(grep -n 'mmio_write32(controller->registers, HDA_GCTL, 0U);' \
		src/kernel/audio.c | head -n 1 | cut -d: -f1); \
		withdraw=$$(grep -n 'pci_claim_disable_bus_master' \
		src/kernel/audio.c | head -n 1 | cut -d: -f1); \
		release=$$(grep -n 'dma_release(&controller->response_ring)' \
		src/kernel/audio.c | head -n 1 | cut -d: -f1); \
		test -n "$$stop" && test -n "$$reset" && test -n "$$withdraw" && \
		test -n "$$release" && test "$$stop" -lt "$$reset" && \
		test "$$reset" -lt "$$withdraw" && \
		test "$$withdraw" -lt "$$release" || \
		{ echo 'audio teardown releases DMA before withdrawing bus mastering'; \
		exit 1; }
	@test "$$(grep -Ec 'pci_claim_enable_bus_master[[:space:]]*[(]' \
		src/kernel/audio.c)" -eq 2 || \
		{ echo 'the audio driver lost its bus-master negative control'; \
		exit 1; }
	@if grep -ERn '\bnvidia_bind[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=boot_plan.c \
		--exclude=nvidia.c; then \
		echo 'NVIDIA probe bypasses the Boot Ledger'; exit 1; \
	fi
	# Fifteen drivers, and exactly one of them may write a register: the video
	# BIOS window needs the ROM shadow bit cleared, and nothing else here has
	# any business changing a live graphics part.
	@grep -Fq '#define NVIDIA_DRIVER_COUNT 15U' include/openrfs/nvidia.h
	@test "$$(grep -Ec '^        \.name = "NVIDIA ' src/kernel/nvidia.c)" \
		-eq 15 || \
		{ echo 'the NVIDIA table does not declare fifteen drivers'; exit 1; }
	# Eight map a window, one reads aperture descriptions, six read
	# configuration space and take nothing at all.
	@test "$$(grep -Ec '\.access = NVIDIA_ACCESS_MEMORY' \
		src/kernel/nvidia.c)" -eq 8 && \
		test "$$(grep -Ec '\.access = NVIDIA_ACCESS_APERTURE' \
		src/kernel/nvidia.c)" -eq 1 && \
		test "$$(grep -Ec '\.access = NVIDIA_ACCESS_CONFIGURATION' \
		src/kernel/nvidia.c)" -eq 6 || \
		{ echo 'the NVIDIA access census changed'; exit 1; }
	@test "$$(grep -Ec '\.writes_registers = true' src/kernel/nvidia.c)" \
		-eq 1 || \
		{ echo 'the NVIDIA drivers gained a second register writer'; \
		exit 1; }
	@test "$$(grep -Ec 'mmio_write32[[:space:]]*[(]' \
		src/kernel/nvidia.c)" -eq 3 || \
		{ echo 'an NVIDIA driver gained an unreviewed register write'; \
		exit 1; }
	# The one write is reversible and is proved reversed, not assumed.
	@save=$$(grep -n 'saved = mmio_read32(registers, shadow);' \
		src/kernel/nvidia.c | head -n 1 | cut -d: -f1); \
		restore=$$(grep -n 'mmio_write32(registers, shadow, saved);' \
		src/kernel/nvidia.c | head -n 1 | cut -d: -f1); \
		verify=$$(grep -n 'restored = mmio_read32(registers, shadow);' \
		src/kernel/nvidia.c | head -n 1 | cut -d: -f1); \
		test -n "$$save" && test -n "$$restore" && test -n "$$verify" && \
		test "$$save" -lt "$$restore" && test "$$restore" -lt "$$verify" || \
		{ echo 'the NVIDIA ROM shadow bit is not proved restored'; exit 1; }
	# No driver here may reach memory: OpenRFS has no IOMMU.
	@if grep -En \
		'pci_claim_enable_bus_master|dma_(allocate|mark_initialized|transfer_to_device|transfer_to_cpu|release)' \
		src/kernel/nvidia.c; then \
		echo 'an NVIDIA driver reached for bus-mastering DMA'; exit 1; \
	fi
	@if grep -En 'pci_config_write_(port|ecam)' src/kernel/nvidia.c; then \
		echo 'an NVIDIA driver wrote configuration space'; exit 1; \
	fi
	# C never parses a VBIOS byte; the freestanding Rust validator does.
	@grep -Fq 'openrfs_nvbios_parse(' src/kernel/nvidia.c || \
		{ echo 'the NVIDIA driver stopped using the Rust VBIOS boundary'; \
		exit 1; }
	@grep -Fq 'NOTHING HERE HAS BEEN RUN AGAINST NVIDIA SILICON' \
		include/openrfs/nvidia.h || \
		{ echo 'the NVIDIA hardware-testing limit was dropped'; exit 1; }
	# Five drivers read what an earlier driver established, and the table's
	# order is those dependencies. The control that states them pair by pair is
	# what keeps a reordered table from silently producing a weaker result.
	@grep -Fq '#define NVIDIA_CONTROLLED_CONTROLS 21U' include/openrfs/nvidia.h
	@test "$$(grep -Ec '^            \{ probe_[a-z_]+, probe_[a-z_]+ \}' \
		src/kernel/nvidia.c)" -eq 5 || \
		{ echo 'the NVIDIA driver ordering control changed shape'; exit 1; }
	# A capability the enumeration could not reach must be a named refusal, not
	# a field decoded out of the capability before it.
	@grep -Fq '(header & UINT32_C(0xFF)) != PCI_CAPABILITY_EXPRESS' \
		src/kernel/nvidia.c || \
		{ echo 'the NVIDIA Express driver stopped checking its capability'; \
		exit 1; }
	# RF is the processor's bookkeeping about a trap, not state the program
	# chose, so every CPL3 boundary discards it rather than authenticating it.
	# A boundary that forgot would refuse a legal return on any processor that
	# sets the bit -- which is the difference between QEMU 8.2 and 9.1.
	@for file in src/kernel/multiprocess.c src/kernel/process.c \
		src/kernel/linux_syscall.c; do \
		grep -Fq 'CPU_RFLAGS_PROCESSOR_BOOKKEEPING' "$$file" || \
			{ echo "$$file authenticates processor bookkeeping as user state"; \
			exit 1; }; \
	done
	@grep -Fq '#define CPU_RFLAGS_PROCESSOR_BOOKKEEPING UINT64_C(0x00010000)' \
		include/openrfs/cpu.h || \
		{ echo 'the processor-bookkeeping flag set moved'; exit 1; }
	# The saved context is normalised, not merely checked: nothing hands the
	# bit back to a process through an IRETQ.
	@grep -Fq 'context->rflags = authenticated_user_rflags(frame->rflags);' \
		src/kernel/multiprocess.c || \
		{ echo 'a saved user context keeps the processor bookkeeping bit'; \
		exit 1; }
	# One receive buffer and one transmit buffer serve the whole stack, so a
	# handler that answers the frame it is reading must never re-enter the
	# pump. The guard is the only thing standing between an inbound segment
	# that needs an unknown hardware address and a recursive service call that
	# overwrites the frame its own caller is parsing.
	@test "$$(grep -Ec '^enum network_status network_service\(void\)$$' \
		src/kernel/network.c)" -eq 1 && \
		grep -Fq 'if (runtime.servicing) {' src/kernel/network.c && \
		grep -Fq 'runtime.servicing = true;' src/kernel/network.c && \
		grep -Fq 'runtime.servicing = false;' src/kernel/network.c || \
		{ echo 'the network pump lost its re-entrancy guard'; exit 1; }
	@guard=$$(grep -n 'if (runtime.servicing) {' src/kernel/network.c \
		| head -n 1 | cut -d: -f1); \
		pump=$$(grep -n 'status = network_service_pump();' \
		src/kernel/network.c | head -n 1 | cut -d: -f1); \
		test -n "$$guard" && test -n "$$pump" && \
		test "$$guard" -lt "$$pump" || \
		{ echo 'the network pump runs before it checks the guard'; exit 1; }
	@test "$$(grep -Ec '\(void\)network_service\(\)' \
		src/kernel/network.c)" -ge 1 && \
		grep -Fq '++runtime.public.statistics.arp_deferred;' \
		src/kernel/network.c || \
		{ echo 'the receive path no longer defers unresolved sends'; exit 1; }
	# A segment nobody is listening for is answered, never swallowed, and a
	# reset is never answered with a reset: that is what stops two closed
	# ports from talking forever.
	@grep -Fq 'if ((flags & TCP_FLAG_RST) != 0U ||' src/kernel/network.c || \
		{ echo 'the TCP refusal lost its reset-for-reset guard'; exit 1; }
	@test "$$(grep -Ec 'tcp_refuse[[:space:]]*[(]' \
		src/kernel/network.c)" -eq 3 || \
		{ echo 'the TCP refusal gained an unreviewed call site'; exit 1; }
	# A passive open is bounded twice: by the listener's declared backlog and
	# by the same connection table an active open draws from.
	@grep -Fq '#define NETWORK_TCP_MAX_BACKLOG 4U' include/openrfs/network.h
	@grep -Fq 'if (tcp_pending_count(listener) >= listener->backlog) {' \
		src/kernel/network.c || \
		{ echo 'a passive open stopped honouring its backlog'; exit 1; }
	@grep -Fq 'backlog > NETWORK_TCP_MAX_BACKLOG' src/kernel/network.c || \
		{ echo 'a listener may now declare an unbounded backlog'; exit 1; }
	# A listener owns the children nobody has accepted yet; closing it must
	# refuse those peers rather than orphan their slots.
	@grep -Fq 'tcp_release(child);' src/kernel/network.c && \
		grep -Fq 'tcp_release_children(connection);' src/kernel/network.c && \
		grep -Fq 'if (connection->backlog != 0U) {' src/kernel/network.c || \
		{ echo 'closing a listener no longer reclaims its children'; exit 1; }
	# The saved-context CPL3 entry belongs to the scheduler that saves the
	# context. Anything else entering Ring 3 from a register set it did not
	# authenticate would be a second, unreviewed user boundary.
	@test "$$(grep -ERh '\bprocess_enter_user_context[[:space:]]*[(]' \
		src/kernel --include='*.c' | wc -l)" -eq 2 && \
		grep -Fq 'process_enter_user_context(&process->context);' \
			src/kernel/multiprocess.c && \
		grep -Fq 'process_enter_user_context(&thread->context);' \
			src/kernel/native_process.c || \
		{ echo 'saved-context user entry escaped the scheduler'; exit 1; }
	@test "$$(grep -Ec 'multiprocess_trap_interrupt[[:space:]]*[(]' \
		src/kernel/multiprocess.c)" -eq 1 && \
		grep -Fq 'interrupt_process_gate_arm(multiprocess_trap_interrupt,' \
		src/kernel/multiprocess.c || \
		{ echo 'multiprocess trap handler has an unexpected call site'; \
		exit 1; }
	# Thirteen drivers, and no driver may enable bus mastering: OpenRFS has no
	# IOMMU, so a register-only driver is one that cannot reach memory at all.
	@grep -Fq '#define DRIVER_MATRIX_CAPACITY 13U' include/openrfs/driver.h
	@test "$$(grep -Ec '^        \.name = ' src/kernel/driver.c)" -eq 13 || \
		{ echo 'the driver matrix does not declare thirteen drivers'; \
		exit 1; }
	# The station addresses the scenario asserts are the ones the host hands
	# QEMU on its command line. A driver that invented them, cached them, or
	# read the wrong device would have to invent these exact four.
	@for mac in 01 02 03 04; do \
		grep -Fq "mac=52:54:00:AA:BB:$$mac" Makefile || \
			{ echo "driver scenario lost a pinned station address"; \
			exit 1; }; \
		grep -Fq "UINT64_C(0x$${mac}BBAA005452)" src/kernel/test.c || \
			{ echo "the kernel lost a pinned station address"; exit 1; }; \
	done
	@if grep -En \
		'pci_claim_enable_bus_master|dma_(allocate|mark_initialized|transfer_to_device|transfer_to_cpu|release)' \
		src/kernel/driver.c; then \
		echo 'a bounded driver reached for bus-mastering DMA'; exit 1; \
	fi
	@if grep -En 'pci_config_write_(port|ecam)' src/kernel/driver.c; then \
		echo 'a bounded driver wrote configuration space'; exit 1; \
	fi
	@grep -Fq '#define PAGING_PROCESS_SPACE_SLOTS 4U' include/openrfs/paging.h
	@grep -Fq 'newest_owned_alias_order()' src/kernel/paging.c || \
		{ echo 'private alias restores lost their ordering guard'; exit 1; }
	@if grep -ERn '\blinux_abi_installed_prove[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=boot_plan.c \
		--exclude=linux_abi.c; then \
		echo 'Linux ABI proof bypasses the Boot Ledger'; exit 1; \
	fi
	@if grep -ERn '\blinux_uname_abi_installed_prove[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=boot_plan.c \
		--exclude=linux_uname.c; then \
		echo 'Linux uname ABI proof bypasses the Boot Ledger'; exit 1; \
	fi
	@test "$$(grep -ERh '\blinux_abi_launch[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=linux_abi.c | wc -l)" -eq 1 && \
		test "$$(grep -ERh '\blinux_uname_abi_launch[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=linux_uname.c | wc -l)" -eq 1 && \
		test "$$(grep -ERh '\blinux_cat_abi_launch[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=linux_cat.c | wc -l)" -eq 1 || \
		{ echo 'measured launch entry escaped its userspace owner'; exit 1; }
	@! grep -Eq 'console_(write|putc)[[:space:]]*\([[:space:]]*"(OPENRFS|Linux)' \
		src/kernel/shell.c || \
		{ echo 'OpenRFS shell contains prerecorded userspace output'; exit 1; }
	@if grep -ERn '\bfilesystem_private_read_(open|close)[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=filesystem.c \
		--exclude=process.c; then \
		echo 'private one-file read seam escaped the process owner'; exit 1; \
	fi
	@if grep -ERn '\bfilesystem_linux_read_(open|close)[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=filesystem.c \
		--exclude=linux_abi.c; then \
		echo 'private BusyBox read seam escaped the Linux process owner'; exit 1; \
	fi
	@if grep -ERn '\bfilesystem_linux_uname_read_(open|close)[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=filesystem.c \
		--exclude=linux_uname.c; then \
		echo 'private uname BusyBox read seam escaped its process owner'; exit 1; \
	fi
	@if grep -ERn '\bfilesystem_linux_cat_read_(open|close)[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=filesystem.c \
		--exclude=linux_cat.c; then \
		echo 'private cat BusyBox read seam escaped its process owner'; exit 1; \
	fi
	@if grep -ERn '\bpaging_process_table_failure_(arm|result|disarm|armed)[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=paging.c \
		--exclude=linux_abi.c --exclude=linux_uname.c \
		--exclude=linux_cat.c; then \
		echo 'private paging failure control escaped the Linux process owner'; \
		exit 1; \
	fi
	@if grep -ERn '\bnvme_filesystem_session_(open|read|view|close)[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=filesystem.c --exclude=nvme.c; then \
		echo 'private filesystem read session escaped its owner'; exit 1; \
	fi
	@grep -Fq '#define NVME_NVM_WRITE UINT8_C(0x01)' src/kernel/nvme.c
	@test "$$(grep -ERh '\bnvme_volume_write[[:space:]]*[(]' \
		src/kernel --include='*.c' --exclude=nvme.c | wc -l)" -eq 3 && \
		grep -Fq 'nvme_volume_write(session, sector, data,' \
			src/kernel/fat32_fs.c && \
		test "$$(grep -Ec '\bnvme_volume_write[[:space:]]*[(]' \
			src/kernel/ext4_fs.c)" -eq 2 && \
		grep -Fq 'if (nvme_volume_write(session, lba, source, chunk)' \
			src/kernel/ext4_fs.c && \
		grep -Fq 'if (nvme_volume_write(session, lba, mount->block_buffer,' \
			src/kernel/ext4_fs.c || \
		{ echo 'NVMe write access escaped the FAT32 or ext4 recovery owner'; \
			exit 1; }
	@test "$$(grep -Ec 'process_return_interrupt[[:space:]]*[(]' \
		src/kernel/process.c)" -eq 1 && \
		grep -Fq 'interrupt_process_gate_arm(process_return_interrupt,' \
		src/kernel/process.c || \
		{ echo 'process proof return handler has an unexpected call site'; exit 1; }
	@test "$$($(NM) $(KERNEL) | grep -Ec ' T linux_syscall_entry$$')" -eq 1 || \
		{ echo 'Linux proof has no unique architectural syscall entry'; exit 1; }
	@$(OBJDUMP) -d --no-show-raw-insn $(BUSYBOX_BINARY) \
		| grep -Eq '[[:space:]]syscall[[:space:]]*$$' || \
		{ echo 'pinned BusyBox has no x86-64 syscall instruction'; exit 1; }
	@$(OBJDUMP) -d --no-show-raw-insn $(BUSYBOX_UNAME_BINARY) \
		| grep -Eq '[[:space:]]syscall[[:space:]]*$$' || \
		{ echo 'pinned uname BusyBox has no x86-64 syscall instruction'; exit 1; }
	@$(OBJDUMP) -d --no-show-raw-insn $(BUSYBOX_CAT_BINARY) \
		| grep -Eq '[[:space:]]syscall[[:space:]]*$$' || \
		{ echo 'pinned cat BusyBox has no x86-64 syscall instruction'; exit 1; }
	@if grep -ERn '(^|[^[:alnum:]_])unsafe[[:space:]]*(\{|fn|extern|openrfs|impl)|#\[unsafe' \
		src/rust --include='*.rs' --exclude=abi.rs; then \
		echo 'unsafe Rust escaped the reviewed FFI boundary'; exit 1; \
	fi
	@! grep -Eq '\*const|\*mut' src/rust/fat16.rs || \
		{ echo 'safe FAT16 parser retained or exposed a raw pointer'; exit 1; }
	@! grep -Eq '\*const|\*mut' src/rust/linux_fat16.rs || \
		{ echo 'safe Linux FAT16 parser retained or exposed a raw pointer'; exit 1; }
	@! grep -Eq '\*const|\*mut' src/rust/linux_elf64.rs || \
		{ echo 'safe Linux ELF64 parser retained or exposed a raw pointer'; exit 1; }
	@! grep -Eq '\*const|\*mut' src/rust/elf64.rs || \
		{ echo 'safe ELF64 parser retained or exposed a raw pointer'; exit 1; }
	@grep -Fq 'case KERNEL_TEST_DEVICE_SUBSTRATE:' src/kernel/test.c
	@grep -Fq '        return UINT8_C(0x30);' src/kernel/test.c
	@guest_exit=$$(sed -n \
		'/case KERNEL_TEST_DEVICE_SUBSTRATE:/{n;s/.*UINT8_C(\(0x[0-9A-Fa-f]*\)).*/\1/p;}' \
		src/kernel/test.c); \
		host_exit=$$(sed -n \
		's/^[[:space:]]*device-substrate) expected=\([0-9][0-9]*\) ;;.*/\1/p' \
		Makefile | head -n 1); \
		test -n "$$guest_exit" && test -n "$$host_exit" && \
		test "$$((guest_exit * 2 + 1))" -eq "$$host_exit" || \
		{ echo 'device-substrate guest and host exits disagree'; exit 1; }
	@test "$$(grep -Ec 'proof_interrupt[[:space:]]*[(]' \
		src/kernel/virtio_rng_proof.c)" -eq 1 || \
		{ echo 'VirtIO proof directly injects its MSI-X handler'; exit 1; }
	@grep -Fq 'case KERNEL_TEST_XHCI:' src/kernel/test.c
	@grep -Fq '        return UINT8_C(0x31);' src/kernel/test.c
	@guest_exit=$$(sed -n \
		'/case KERNEL_TEST_XHCI:/{n;s/.*UINT8_C(\(0x[0-9A-Fa-f]*\)).*/\1/p;}' \
		src/kernel/test.c); \
		host_exit=$$(sed -n \
		's/^[[:space:]]*xhci) expected=\([0-9][0-9]*\) ;;.*/\1/p' \
		Makefile | head -n 1); \
		test -n "$$guest_exit" && test -n "$$host_exit" && \
		test "$$((guest_exit * 2 + 1))" -eq "$$host_exit" || \
		{ echo 'xHCI guest and host exits disagree'; exit 1; }
	@test "$$(grep -Ec 'xhci_interrupt_handler[[:space:]]*[(]' \
		src/kernel/xhci.c)" -eq 1 || \
		{ echo 'xHCI proof directly injects its MSI-X handler'; exit 1; }
	@grep -Fq 'case KERNEL_TEST_NVME:' src/kernel/test.c
	@grep -Fq '        return UINT8_C(0x32);' src/kernel/test.c
	@guest_exit=$$(sed -n \
		'/case KERNEL_TEST_NVME:/{n;s/.*UINT8_C(\(0x[0-9A-Fa-f]*\)).*/\1/p;}' \
		src/kernel/test.c); \
		host_exit=$$(sed -n \
		's/^[[:space:]]*nvme) expected=\([0-9][0-9]*\) ;;.*/\1/p' \
		Makefile | head -n 1); \
		test -n "$$guest_exit" && test -n "$$host_exit" && \
		test "$$((guest_exit * 2 + 1))" -eq "$$host_exit" && \
		test "$$((0x33 * 2 + 1))" -ne "$$host_exit" || \
		{ echo 'NVMe guest and host exit contracts disagree'; exit 1; }
	@test "$$(grep -Ec 'nvme_interrupt_handler[[:space:]]*[(]' \
		src/kernel/nvme.c)" -eq 1 || \
		{ echo 'NVMe proof directly injects its MSI-X handler'; exit 1; }
	@grep -Fq 'case KERNEL_TEST_FILESYSTEM:' src/kernel/test.c
	@grep -Fq '        return UINT8_C(0x33);' src/kernel/test.c
	@guest_exit=$$(sed -n \
		'/case KERNEL_TEST_FILESYSTEM:/{n;s/.*UINT8_C(\(0x[0-9A-Fa-f]*\)).*/\1/p;}' \
		src/kernel/test.c); \
		host_exit=$$(sed -n \
		's/^[[:space:]]*filesystem) expected=\([0-9][0-9]*\) ;;.*/\1/p' \
		Makefile | head -n 1); \
		test -n "$$guest_exit" && test -n "$$host_exit" && \
		test "$$((guest_exit * 2 + 1))" -eq "$$host_exit" && \
		test "$$((0x34 * 2 + 1))" -ne "$$host_exit" || \
		{ echo 'filesystem guest and host exit contracts disagree'; exit 1; }
	@grep -Fq 'case KERNEL_TEST_PROCESS:' src/kernel/test.c
	@grep -Fq '        return UINT8_C(0x34);' src/kernel/test.c
	@guest_exit=$$(sed -n \
		'/case KERNEL_TEST_PROCESS:/{n;s/.*UINT8_C(\(0x[0-9A-Fa-f]*\)).*/\1/p;}' \
		src/kernel/test.c); \
		host_exit=$$(sed -n \
		's/^[[:space:]]*process) expected=\([0-9][0-9]*\) ;;.*/\1/p' \
		Makefile | head -n 1); \
		test -n "$$guest_exit" && test -n "$$host_exit" && \
		test "$$((guest_exit * 2 + 1))" -eq "$$host_exit" && \
		test "$$((0x33 * 2 + 1))" -ne "$$host_exit" || \
		{ echo 'process guest and host exit contracts disagree'; exit 1; }
	@grep -Fq 'case KERNEL_TEST_LINUX_ABI:' src/kernel/test.c
	@grep -Fq '        return UINT8_C(0x36);' src/kernel/test.c
	@guest_exit=$$(sed -n \
		'/case KERNEL_TEST_LINUX_ABI:/{n;s/.*UINT8_C(\(0x[0-9A-Fa-f]*\)).*/\1/p;}' \
		src/kernel/test.c); \
		host_exit=$$(sed -n \
		's/^[[:space:]]*linux-abi) expected=\([0-9][0-9]*\) ;;.*/\1/p' \
		Makefile | head -n 1); \
		test -n "$$guest_exit" && test -n "$$host_exit" && \
		test "$$((guest_exit * 2 + 1))" -eq "$$host_exit" && \
		test "$$((0x35 * 2 + 1))" -ne "$$host_exit" || \
		{ echo 'Linux ABI guest and host exit contracts disagree'; exit 1; }
	@grep -Fq 'case KERNEL_TEST_LINUX_ABI_UNAME:' src/kernel/test.c
	@grep -Fq '        return UINT8_C(0x37);' src/kernel/test.c
	@guest_exit=$$(sed -n \
		'/case KERNEL_TEST_LINUX_ABI_UNAME:/{n;s/.*UINT8_C(\(0x[0-9A-Fa-f]*\)).*/\1/p;}' \
		src/kernel/test.c); \
		host_exit=$$(sed -n \
		's/^[[:space:]]*linux-abi-uname) expected=\([0-9][0-9]*\) ;;.*/\1/p' \
		Makefile | head -n 1); \
		test -n "$$guest_exit" && test -n "$$host_exit" && \
		test "$$((guest_exit * 2 + 1))" -eq "$$host_exit" && \
		test "$$((0x36 * 2 + 1))" -ne "$$host_exit" || \
		{ echo 'Linux uname ABI guest and host exit contracts disagree'; exit 1; }
	@grep -Fq 'case KERNEL_TEST_OPENRFS_PROOF_USERLAND:' src/kernel/test.c
	@grep -Fq '        return UINT8_C(0x38);' src/kernel/test.c
	@guest_exit=$$(sed -n \
		'/case KERNEL_TEST_OPENRFS_PROOF_USERLAND:/{n;s/.*UINT8_C(\(0x[0-9A-Fa-f]*\)).*/\1/p;}' \
		src/kernel/test.c); \
		host_exit=$$(sed -n \
		's/^[[:space:]]*openrfs-proof-userland) expected=\([0-9][0-9]*\) ;;.*/\1/p' \
		Makefile | head -n 1); \
		test -n "$$guest_exit" && test -n "$$host_exit" && \
		test "$$((guest_exit * 2 + 1))" -eq "$$host_exit" || \
		{ echo 'OpenRFS userland guest and host exits disagree'; exit 1; }
	@grep -Fq 'case KERNEL_TEST_OPENRFS_PROOF_USERLAND_ABSENT:' src/kernel/test.c
	@grep -Fq '        return UINT8_C(0x39);' src/kernel/test.c
	@guest_exit=$$(sed -n \
		'/case KERNEL_TEST_OPENRFS_PROOF_USERLAND_ABSENT:/{n;s/.*UINT8_C(\(0x[0-9A-Fa-f]*\)).*/\1/p;}' \
		src/kernel/test.c); \
		host_exit=$$(sed -n \
		's/^[[:space:]]*openrfs-proof-userland-absent) expected=\([0-9][0-9]*\) ;;.*/\1/p' \
		Makefile | head -n 1); \
		test -n "$$guest_exit" && test -n "$$host_exit" && \
		test "$$((guest_exit * 2 + 1))" -eq "$$host_exit" || \
		{ echo 'OpenRFS absent-volume guest and host exits disagree'; exit 1; }
	@grep -Fq 'case KERNEL_TEST_OPENRFS_PROOF_USERLAND_INTERACTIVE:' src/kernel/test.c
	@grep -Fq '        return UINT8_C(0x3A);' src/kernel/test.c
	@guest_exit=$$(sed -n \
		'/case KERNEL_TEST_OPENRFS_PROOF_USERLAND_INTERACTIVE:/{n;s/.*UINT8_C(\(0x[0-9A-Fa-f]*\)).*/\1/p;}' \
		src/kernel/test.c); \
		host_exit=$$(sed -n \
		's/^[[:space:]]*openrfs-proof-userland-interactive) expected=\([0-9][0-9]*\) ;;.*/\1/p' \
		Makefile | head -n 1); \
		test -n "$$guest_exit" && test -n "$$host_exit" && \
		test "$$((guest_exit * 2 + 1))" -eq "$$host_exit" || \
		{ echo 'Interactive OpenRFS guest and host exits disagree'; exit 1; }
	@grep -Fq 'case KERNEL_TEST_OPENRFS_PROOF_USERLAND_INTERACTIVE_ABSENT:' src/kernel/test.c
	@grep -Fq '        return UINT8_C(0x3B);' src/kernel/test.c
	@guest_exit=$$(sed -n \
		'/case KERNEL_TEST_OPENRFS_PROOF_USERLAND_INTERACTIVE_ABSENT:/{n;s/.*UINT8_C(\(0x[0-9A-Fa-f]*\)).*/\1/p;}' \
		src/kernel/test.c); \
		host_exit=$$(sed -n \
		's/^[[:space:]]*openrfs-proof-userland-interactive-absent) expected=\([0-9][0-9]*\) ;;.*/\1/p' \
		Makefile | head -n 1); \
		test -n "$$guest_exit" && test -n "$$host_exit" && \
		test "$$((guest_exit * 2 + 1))" -eq "$$host_exit" || \
		{ echo 'Interactive absent-profile guest and host exits disagree'; exit 1; }
	@if grep -En '\bframebuffer_(write_pixel|fill|scroll_up)[[:space:]]*[(]' \
		src/kernel/ui.c src/kernel/ui_font.c src/kernel/pointer.c; then \
		echo 'OpenRFS bypasses the cached surface'; exit 1; \
	fi
	@if grep -En \
		'\b(ui_process_events|ui_flush|surface_present)[[:space:]]*[(]' \
		src/kernel/pointer.c; then \
		echo 'PS/2 pointer interrupt path attempts UI drawing'; exit 1; \
	fi
	@grep -Fq '    cpu_store_fence();' src/kernel/surface.c || \
		{ echo 'cached-surface WC present lost its sfence'; exit 1; }
	@grep -Fq 'OpenRFS: installed proof passed' \
		src/kernel/boot_plan.c
	$(MAKE) screenshot-proof

screenshot-proof:
	$(PYTHON) tools/compare-openrfs-proof-screenshot.py --mode clean \
		--self-test $(OPENRFS_PROOF_IMAGE)
	$(PYTHON) tools/compare-openrfs-proof-screenshot.py --mode focus \
		--self-test $(OPENRFS_PROOF_FOCUS_IMAGE)
	$(PYTHON) tools/compare-openrfs-proof-screenshot.py --mode terminal \
		--self-test $(OPENRFS_PROOF_TERMINAL_IMAGE)

capture-openrfs-proof: iso $(FAT32_SYSTEM_IMAGE) $(FAT32_DATA_IMAGE)
	rm -rf $(OPENRFS_PROOF_CAPTURE_DIR)
	$(PYTHON) tools/capture-openrfs-proof.py --iso $(ISO) \
		--system $(FAT32_SYSTEM_IMAGE) --data $(FAT32_DATA_IMAGE) \
		--output $(OPENRFS_PROOF_CAPTURE_DIR)
	$(PYTHON) tools/compare-openrfs-proof-screenshot.py --mode clean \
		$(OPENRFS_PROOF_IMAGE) $(OPENRFS_PROOF_CAPTURE_DIR)/openrfs-proof.png
	$(PYTHON) tools/compare-openrfs-proof-screenshot.py --mode focus \
		$(OPENRFS_PROOF_FOCUS_IMAGE) \
		$(OPENRFS_PROOF_CAPTURE_DIR)/openrfs-proof-focus.png
	$(PYTHON) tools/compare-openrfs-proof-screenshot.py --mode terminal \
		$(OPENRFS_PROOF_TERMINAL_IMAGE) \
		$(OPENRFS_PROOF_CAPTURE_DIR)/openrfs-proof-terminal.png

capture-openrfs: iso $(FAT32_SYSTEM_IMAGE) $(FAT32_DATA_IMAGE)
	rm -rf $(OPENRFS_CAPTURE_DIR)
	$(PYTHON) tools/capture-openrfs-proof.py --iso $(ISO) \
		--system $(FAT32_SYSTEM_IMAGE) --data $(FAT32_DATA_IMAGE) \
		--output $(OPENRFS_CAPTURE_DIR)

capture-networking: iso $(FAT32_SYSTEM_IMAGE) $(FAT32_DATA_IMAGE)
	rm -rf $(NETWORK_CAPTURE_DIR)
	$(PYTHON) tools/capture-networking.py --iso $(ISO) \
		--system $(FAT32_SYSTEM_IMAGE) --data $(FAT32_DATA_IMAGE) \
		--output $(NETWORK_CAPTURE_DIR) --ffmpeg $(FFMPEG)

capture-boot-video: iso $(FAT32_SYSTEM_IMAGE) $(FAT32_DATA_IMAGE)
	cp $(FAT32_DATA_IMAGE) $(BUILD_DIR)/capture-video-data-fat32.raw
	$(PYTHON) tools/capture-fat32-persistence.py --iso $(ISO) \
		--system $(FAT32_SYSTEM_IMAGE) \
		--data $(BUILD_DIR)/capture-video-data-fat32.raw \
		--screenshot $(BUILD_DIR)/fat32-persistence.png \
		--video $(OPENRFS_PROOF_BOOT_VIDEO) \
		--transcript $(BUILD_DIR)/fat32-persistence.log \
		--ffmpeg $(FFMPEG)

$(ISO): $(KERNEL) grub/grub.cfg
	mkdir -p $(ISO_ROOT)/boot/grub
	cp $(KERNEL) $(ISO_ROOT)/boot/openrfs.elf
	cp grub/grub.cfg $(ISO_ROOT)/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) $(GRUB_MKRESCUE_FLAGS) -o $@ $(ISO_ROOT)

iso: $(ISO)

$(TEST_BUILD_DIR)/%/openrfs.iso: $(KERNEL) Makefile
	rm -rf $(TEST_BUILD_DIR)/$*
	mkdir -p $(TEST_BUILD_DIR)/$*/iso-root/boot/grub
	cp $(KERNEL) $(TEST_BUILD_DIR)/$*/iso-root/boot/openrfs.elf
	printf '%s\n' 'set default=0' 'set timeout=0' '' \
		'menuentry "OpenRFS test" {' \
		'    multiboot2 /boot/openrfs.elf openrfs.test=$*' \
		'    boot' '}' >$(TEST_BUILD_DIR)/$*/iso-root/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) $(GRUB_MKRESCUE_FLAGS) -o $@ $(TEST_BUILD_DIR)/$*/iso-root

# Networking scenarios have an isolated Ethernet peer and packet capture rather
# than a host-network dependency.  This more-specific pattern is selected ahead
# of qemu-test-% and keeps the existing 58 scenario recipe unchanged.
qemu-test-network-%: $(TEST_BUILD_DIR)/network-%/openrfs.iso
	@for tool in qemu-system-x86_64 $(PYTHON); do \
		command -v $$tool >/dev/null 2>&1 || { echo "missing tool: $$tool"; exit 1; }; \
	done
	@case '$*' in \
		nic-discovery) expected=151 ;; \
		nic-initialization) expected=153 ;; \
		nic-absent) expected=155 ;; \
		link-down) expected=157 ;; \
		dhcp) expected=159 ;; \
		dhcp-timeout) expected=161 ;; \
		static) expected=163 ;; \
		arp) expected=165 ;; \
		icmp) expected=167 ;; \
		icmp-timeout) expected=169 ;; \
		udp) expected=171 ;; \
		dns-a) expected=173 ;; \
		dns-cname) expected=175 ;; \
		dns-malformed) expected=177 ;; \
		tcp) expected=179 ;; \
		tcp-retransmit) expected=181 ;; \
		tcp-reset) expected=183 ;; \
		http-length) expected=185 ;; \
		http-chunked) expected=187 ;; \
		http-redirect) expected=189 ;; \
		http-malformed) expected=191 ;; \
		http-nested) expected=193 ;; \
		http-replace) expected=195 ;; \
		http-disk-full) expected=197 ;; \
		nic-reset) expected=199 ;; \
		system-immutable) expected=201 ;; \
		missing-linux-echo) expected=203 ;; \
		missing-linux-uname) expected=205 ;; \
		missing-linux-cat) expected=207 ;; \
		files) expected=209 ;; \
		notes) expected=211 ;; \
		persistence) expected=215 ;; \
		socket-isolation) expected=217 ;; \
		tcp-listen) expected=219 ;; \
		tcp-refused) expected=221 ;; \
		native) expected=245 ;; \
		*) echo 'unknown network scenario: network-$*'; exit 1 ;; \
	esac; \
	case '$*' in \
		http-disk-full) \
			$(MAKE) '$(FAT32_SYSTEM_IMAGE)' '$(FAT32_FULL_IMAGE)' || exit 1 ;; \
		native) \
			$(MAKE) '$(NETAPP_SYSTEM_IMAGE)' '$(NETAPP_DATA_IMAGE)' || exit 1 ;; \
		http-length|http-chunked|http-redirect|http-malformed|http-nested|\
		http-replace|system-immutable|missing-linux-echo|missing-linux-uname|\
		missing-linux-cat|files|notes|persistence) \
			$(MAKE) '$(FAT32_SYSTEM_IMAGE)' '$(FAT32_DATA_IMAGE)' || exit 1 ;; \
	esac; \
	system='$(FAT32_SYSTEM_IMAGE)'; data='$(FAT32_DATA_IMAGE)'; \
	if test '$*' = native; then \
		system='$(NETAPP_SYSTEM_IMAGE)'; data='$(NETAPP_DATA_IMAGE)'; \
	fi; \
	timeout=45; \
	if test '$*' = persistence; then timeout=70; \
	elif test '$*' = native; then timeout=120; fi; \
	$(PYTHON) tools/run_network_scenario.py \
		--scenario 'network-$*' --expected "$$expected" --iso '$<' \
		--output '$(TEST_BUILD_DIR)/network-$*' \
		--fixture tools/network_fixture.py \
		--audit tools/network_packet_audit.py \
		--system "$$system" --data "$$data" \
		--full '$(FAT32_FULL_IMAGE)' --qemu qemu-system-x86_64 \
		--python '$(PYTHON)' --accel '$(QEMU_ACCEL)' --timeout "$$timeout"

qemu-test-native-https: $(TEST_BUILD_DIR)/native-https/openrfs.iso
	$(MAKE) '$(HTTPSAPP_SYSTEM_IMAGE)' '$(HTTPSAPP_DATA_IMAGE)'
	$(PYTHON) tools/run_network_scenario.py \
		--scenario native-https --expected 11 --iso '$<' \
		--output '$(TEST_BUILD_DIR)/native-https' \
		--fixture tools/https_network_fixture.py \
		--audit tools/network_packet_audit.py \
		--system '$(HTTPSAPP_SYSTEM_IMAGE)' \
		--data '$(HTTPSAPP_DATA_IMAGE)' --full '$(FAT32_FULL_IMAGE)' \
		--qemu qemu-system-x86_64 --python '$(PYTHON)' \
		--accel '$(QEMU_ACCEL)' --timeout 180

qemu-test-native-openrfs: $(TEST_BUILD_DIR)/native-openrfs/openrfs.iso
	$(MAKE) '$(OPENRFSAPP_SYSTEM_IMAGE)' '$(OPENRFSAPP_DATA_IMAGE)' \
		'$(OPENRFSAPP_REPOSITORY)'
	$(PYTHON) tools/run_network_scenario.py \
		--scenario native-openrfs --expected 15 --iso '$<' \
		--output '$(TEST_BUILD_DIR)/native-openrfs' \
		--fixture tools/https_network_fixture.py \
		--audit tools/network_packet_audit.py \
		--content-root '$(OPENRFSAPP_DIR)/repository' \
		--system '$(OPENRFSAPP_SYSTEM_IMAGE)' \
		--data '$(OPENRFSAPP_DATA_IMAGE)' --data-filesystem ext4 \
		--full '$(FAT32_FULL_IMAGE)' \
		--qemu qemu-system-x86_64 --python '$(PYTHON)' \
		--accel '$(QEMU_ACCEL)' --timeout 900

qemu-test-ext4-space: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_space_test.py tools/ext4_powercut_test.py
	$(PYTHON) tools/ext4_space_test.py \
		--kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-space/$(shell git rev-parse --short HEAD)' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-inodes: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_space_test.py tools/ext4_powercut_test.py
	$(PYTHON) tools/ext4_space_test.py --inodes \
		--kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-inodes/$(shell git rev-parse --short HEAD)' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-unlink-powercuts: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py
	$(PYTHON) tools/ext4_unlink_powercut_test.py \
		--kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-replace-powercuts: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py
	$(PYTHON) tools/ext4_unlink_powercut_test.py --operation replace \
		--kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/replace' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-truncate-powercuts: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py
	$(PYTHON) tools/ext4_unlink_powercut_test.py --operation truncate \
		--kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/truncate' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-grow-powercuts: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py
	$(PYTHON) tools/ext4_unlink_powercut_test.py --operation grow \
		--kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/grow' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-create-powercuts: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py
	$(PYTHON) tools/ext4_unlink_powercut_test.py --operation create \
		--kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/create' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-mkdir-powercuts: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py
	$(PYTHON) tools/ext4_unlink_powercut_test.py --operation mkdir \
		--kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/mkdir' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-rmdir-powercuts: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py
	$(PYTHON) tools/ext4_unlink_powercut_test.py --operation rmdir \
		--kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/rmdir' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-xattr-powercuts: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py tools/ext4_kernel_read.py
	$(PYTHON) tools/ext4_unlink_powercut_test.py --operation xattr \
		--kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/xattr' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-xattr-remove-powercuts: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py tools/ext4_kernel_read.py
	$(PYTHON) tools/ext4_unlink_powercut_test.py --operation xattr-remove \
		--kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/xattr-remove' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-link-powercuts: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py tools/ext4_kernel_read.py
	$(PYTHON) tools/ext4_unlink_powercut_test.py --operation link \
		--kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/link' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-symlink-powercuts qemu-test-ext4-symlink-long-powercuts qemu-test-ext4-chmod-powercuts qemu-test-ext4-times-powercuts qemu-test-ext4-rename-powercuts qemu-test-ext4-rename-cross-powercuts qemu-test-ext4-overwrite-powercuts: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py tools/ext4_kernel_read.py
	$(PYTHON) tools/ext4_unlink_powercut_test.py --operation '$(patsubst qemu-test-ext4-%-powercuts,%,$@)' \
		--kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/$(patsubst qemu-test-ext4-%-powercuts,%,$@)' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-append-powercuts: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py tools/ext4_kernel_read.py
	$(PYTHON) tools/ext4_unlink_powercut_test.py --operation append \
		--kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/append' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-overwrite-errors-powercuts qemu-test-ext4-append-errors-powercuts qemu-test-ext4-truncate-errors-powercuts qemu-test-ext4-grow-errors-powercuts qemu-test-ext4-rename-errors-powercuts qemu-test-ext4-rename-cross-errors-powercuts qemu-test-ext4-create-errors-powercuts qemu-test-ext4-mkdir-errors-powercuts qemu-test-ext4-rmdir-errors-powercuts qemu-test-ext4-chmod-errors-powercuts qemu-test-ext4-times-errors-powercuts qemu-test-ext4-xattr-errors-powercuts qemu-test-ext4-xattr-remove-errors-powercuts qemu-test-ext4-link-errors-powercuts qemu-test-ext4-symlink-errors-powercuts qemu-test-ext4-symlink-long-errors-powercuts: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py tools/ext4_kernel_read.py
	$(PYTHON) tools/ext4_unlink_powercut_test.py --operation '$(patsubst qemu-test-ext4-%-errors-powercuts,%,$@)' --storage-failures \
		--kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/$(patsubst qemu-test-ext4-%-powercuts,%,$@)' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-journal-wrap: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_journal_wrap_test.py tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py tools/ext4_kernel_read.py
	$(PYTHON) tools/ext4_journal_wrap_test.py --kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/journal-wrap' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-storage-refusals: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_unlink_powercut_test.py
	@operations="$$($(PYTHON) -c 'import sys; sys.path.insert(0, "tools"); from ext4_unlink_powercut_test import STORAGE_CONTROLS; print(" ".join(STORAGE_CONTROLS))')" || exit 1; \
	result=0; \
	for operation in $$operations; do \
		$(MAKE) "qemu-test-ext4-$$operation-errors-powercuts" || result=1; \
	done; \
	exit $$result

qemu-test-ext4-admission-refusals: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_admission_test.py tools/ext4_journal_corruption_test.py tools/ext4_image.py tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py tools/ext4_kernel_read.py
	$(PYTHON) tools/ext4_admission_test.py --kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/admission-refusals' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'
	$(PYTHON) tools/ext4_journal_corruption_test.py --kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/journal-corruption' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-geometry: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_geometry_test.py tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py tools/ext4_kernel_read.py
	$(PYTHON) tools/ext4_geometry_test.py --kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/geometry' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-dense-file: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_dense_file_test.py tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py tools/ext4_kernel_read.py
	$(PYTHON) tools/ext4_dense_file_test.py --kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/dense-file' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-rename-device-powercuts qemu-test-ext4-rename-cross-device-powercuts qemu-test-ext4-rename-wrap-device-powercuts qemu-test-ext4-append-device-powercuts qemu-test-ext4-truncate-device-powercuts qemu-test-ext4-grow-device-powercuts qemu-test-ext4-create-device-powercuts qemu-test-ext4-chmod-device-powercuts qemu-test-ext4-times-device-powercuts qemu-test-ext4-xattr-device-powercuts qemu-test-ext4-xattr-remove-device-powercuts qemu-test-ext4-unlink-device-powercuts qemu-test-ext4-replace-device-powercuts qemu-test-ext4-mkdir-device-powercuts qemu-test-ext4-rmdir-device-powercuts qemu-test-ext4-link-device-powercuts qemu-test-ext4-symlink-device-powercuts qemu-test-ext4-symlink-long-device-powercuts qemu-test-ext4-overwrite-device-powercuts: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_unlink_powercut_test.py tools/ext4_powercut_test.py tools/ext4_kernel_read.py
	$(PYTHON) tools/ext4_unlink_powercut_test.py --operation '$(patsubst qemu-test-ext4-%-device-powercuts,%,$@)' --physical-cuts \
		$(if $(filter qemu-test-ext4-rename-wrap-device-powercuts,$@),--timeout 600) \
		--kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-unlink-powercuts/$(shell git rev-parse --short HEAD)/$(patsubst qemu-test-ext4-%-powercuts,%,$@)' \
		--grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)'

qemu-test-ext4-namespace-device-cuts: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_unlink_powercut_test.py
	@result=0; \
	for operation in unlink replace mkdir rmdir link symlink symlink-long; do \
		$(MAKE) "qemu-test-ext4-$$operation-device-powercuts" || result=1; \
	done; \
	exit $$result

qemu-test-ext4-metadata-device-cuts: $(KERNEL) $(EXT4_FIXTURE) tools/ext4_unlink_powercut_test.py
	@result=0; \
	for operation in chmod times xattr xattr-remove; do \
		$(MAKE) "qemu-test-ext4-$$operation-device-powercuts" || result=1; \
	done; \
	exit $$result

qemu-test-ext4-powercuts: $(KERNEL) $(EXT4_FIXTURE) \
		tools/ext4_image.py tools/ext4_powercut_test.py
	@for tool in qemu-system-x86_64 $(GRUB_MKRESCUE) $(PYTHON); do \
		command -v $$tool >/dev/null 2>&1 || { echo "missing tool: $$tool"; exit 1; }; \
	done
	rm -rf '$(TEST_BUILD_DIR)/ext4-powercuts'
	$(PYTHON) tools/ext4_powercut_test.py \
		--kernel '$(KERNEL)' --fixture '$(EXT4_FIXTURE)' \
		--output '$(TEST_BUILD_DIR)/ext4-powercuts' \
		--qemu qemu-system-x86_64 --grub-mkrescue '$(GRUB_MKRESCUE)' \
		$(if $(GRUB_MODULE_DIR),--grub-module-dir '$(GRUB_MODULE_DIR)') \
		--accel '$(QEMU_ACCEL)' --timeout 600 --keep-images

qemu-test-%: $(TEST_BUILD_DIR)/%/openrfs.iso
	@for tool in qemu-system-x86_64 timeout grep; do \
		command -v $$tool >/dev/null 2>&1 || { echo "missing tool: $$tool"; exit 1; }; \
	done
	# 0x22, which is status 69, remains assigned to the ioapic-level scenario;
	# the later scenarios start at 0x23 so every exit value stays stable.
	@case '$*' in \
		normal) expected=33 ;; \
		breakpoint) expected=35 ;; \
		invalid-opcode) expected=37 ;; \
		page-fault) expected=39 ;; \
		ist) expected=41 ;; \
		pit) expected=43 ;; \
		unexpected) expected=45 ;; \
		double-fault) expected=47 ;; \
		apic) expected=49 ;; \
		ioapic) expected=51 ;; \
		retired) expected=53 ;; \
		apic-timer) expected=55 ;; \
		tsc) expected=57 ;; \
		pm-timer) expected=59 ;; \
		pit-retired) expected=61 ;; \
		timers) expected=63 ;; \
		paging) expected=65 ;; \
		heap) expected=67 ;; \
		ioapic-level) expected=69 ;; \
		pci) expected=71 ;; \
		pci-ecam) expected=73 ;; \
		threads) expected=75 ;; \
		thread-guard) expected=77 ;; \
		framebuffer) expected=79 ;; \
		screen) expected=81 ;; \
		keyboard) expected=83 ;; \
		shell) expected=85 ;; \
		surface) expected=87 ;; \
		write-combining) expected=89 ;; \
		device-windows) expected=91 ;; \
		boot-ledger) expected=93 ;; \
		openrfs-proof) expected=95 ;; \
		device-substrate) expected=97 ;; \
		xhci) expected=99 ;; \
		nvme) expected=101 ;; \
		filesystem) expected=103 ;; \
		process) expected=105 ;; \
		linux-abi) expected=109 ;; \
		linux-abi-uname) expected=111 ;; \
		openrfs-proof-userland) expected=113 ;; \
		openrfs-proof-userland-absent) expected=115 ;; \
		openrfs-proof-userland-interactive) expected=117 ;; \
		openrfs-proof-userland-interactive-absent) expected=119 ;; \
		fat32-system) expected=121 ;; \
		fat32-data) expected=123 ;; \
		fat32-nested) expected=125 ;; \
		fat32-growth) expected=127 ;; \
		fat32-random) expected=129 ;; \
		fat32-truncate) expected=131 ;; \
		fat32-rename) expected=133 ;; \
		fat32-delete) expected=135 ;; \
		fat32-full) expected=137 ;; \
		fat32-corrupt) expected=139 ;; \
		fat32-missing) expected=141 ;; \
		fat32-persistence) expected=143 ;; \
		fat32-cache) expected=145 ;; \
		fat32-immutable) expected=147 ;; \
		fat32-handles) expected=149 ;; \
		multiprocess) expected=223 ;; \
		multiprocess-slots) expected=225 ;; \
		driver-matrix) expected=227 ;; \
		driver-matrix-builtin) expected=229 ;; \
		audio) expected=231 ;; \
		nvidia) expected=233 ;; \
		nvidia-builtin) expected=235 ;; \
		native) expected=237 ;; \
		native-lua) expected=239 ;; \
		native-sqlite) expected=241 ;; \
		native-rust) expected=247 ;; \
		native-crash) expected=249 ;; \
		native-elf-refusal) expected=251 ;; \
		native-digest-refusal) expected=253 ;; \
		native-abi-refusal) expected=1 ;; \
		native-relaunch) expected=3 ;; \
		native-audio) expected=5 ;; \
		native-sdl) expected=7 ;; \
		native-dynamic) expected=9 ;; \
		ext4-recovery) expected=13 ;; \
		*) echo 'unknown QEMU scenario: $*'; exit 1 ;; \
	esac; \
		# The ECAM and device-window scenarios depart from the default machine. \
		# i440fx publishes no \
		# MCFG, so every other scenario - including pci - proves the path that \
		# has nothing but the I/O ports. q35 is the only machine here with a \
		# PCI Express host bridge, and the root port is what gives the \
		# enumeration a second bus to find. Both PCI scenarios name their \
		# network device explicitly instead of relying on QEMU defaults. \
		case '$*' in \
			pci) hardware='-device e1000e' ;; \
			pci-ecam) \
				hardware='-machine q35 -device pcie-root-port,id=rp0,chassis=1 -device e1000e,bus=rp0 -device e1000e' ;; \
			device-windows) hardware='-machine q35' ;; \
			driver-matrix) \
				hardware='-nic none -device e1000,mac=52:54:00:AA:BB:01 -device e1000e,mac=52:54:00:AA:BB:02 -device rtl8139,mac=52:54:00:AA:BB:03 -device pcnet,mac=52:54:00:AA:BB:04 -device ich9-ahci -device ich9-intel-hda -device usb-ehci -vga std -device cirrus-vga,romfile= -device bochs-display,romfile=' ;; \
			driver-matrix-builtin) hardware='' ;; \
			audio) \
				hardware='-device ich9-intel-hda,id=hda -device hda-duplex,bus=hda.0,audiodev=none0 -audiodev none,id=none0' ;; \
			native-audio) \
				$(MAKE) '$(AUDIO_SYSTEM_IMAGE)' '$(AUDIO_DATA_IMAGE)' || exit 1; \
				cp '$(AUDIO_DATA_IMAGE)' '$(TEST_BUILD_DIR)/$*/data.raw' || exit 1; \
				audio_wav='$(abspath $(TEST_BUILD_DIR)/$*/native-audio.wav)'; rm -f "$$audio_wav"; audio_capture=false; \
				if test '$(OS)' != Windows_NT && qemu-system-x86_64 -audiodev help 2>&1 | grep -Eq '(^|[[:space:]])wav([[:space:]]|$$)'; then \
					audio_capture=true; \
					audio_backend="-audiodev wav,id=wav0,path=$$audio_wav,out.frequency=48000,out.channels=2,out.format=s16"; \
				else audio_backend='-audiodev none,id=wav0'; fi; \
				hardware="-boot order=d -blockdev driver=file,filename=$(AUDIO_SYSTEM_IMAGE),node-name=audio-system-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=audio-system-file,node-name=audio-system-raw,read-only=on -device nvme,serial=openrfs-system-fat32,drive=audio-system-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 -blockdev driver=file,filename=$(TEST_BUILD_DIR)/$*/data.raw,node-name=audio-data-file,read-only=off,auto-read-only=off -blockdev driver=raw,file=audio-data-file,node-name=audio-data-raw,read-only=off -device nvme,serial=openrfs-data-fat32,drive=audio-data-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 -device ich9-intel-hda,id=hda -device hda-duplex,bus=hda.0,audiodev=wav0 $$audio_backend" ;; \
			native-sdl) \
				$(MAKE) '$(SDL_PROOF_SYSTEM_IMAGE)' '$(SDL_PROOF_DATA_IMAGE)' || exit 1; \
				cp '$(SDL_PROOF_DATA_IMAGE)' '$(TEST_BUILD_DIR)/$*/data.raw' || exit 1; \
				audio_wav='$(abspath $(TEST_BUILD_DIR)/$*/native-sdl.wav)'; rm -f "$$audio_wav"; audio_capture=false; \
				if test '$(OS)' != Windows_NT && qemu-system-x86_64 -audiodev help 2>&1 | grep -Eq '(^|[[:space:]])wav([[:space:]]|$$)'; then \
					audio_capture=true; \
					audio_backend="-audiodev wav,id=wav0,path=$$audio_wav,out.frequency=48000,out.channels=2,out.format=s16"; \
				else audio_backend='-audiodev none,id=wav0'; fi; \
			hardware="-boot order=d -blockdev driver=file,filename=$(SDL_PROOF_SYSTEM_IMAGE),node-name=sdl-system-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=sdl-system-file,node-name=sdl-system-raw,read-only=on -device nvme,serial=openrfs-system-fat32,drive=sdl-system-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 -blockdev driver=file,filename=$(TEST_BUILD_DIR)/$*/data.raw,node-name=sdl-data-file,read-only=off,auto-read-only=off -blockdev driver=raw,file=sdl-data-file,node-name=sdl-data-raw,read-only=off -device nvme,serial=openrfs-data-fat32,drive=sdl-data-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 -device ich9-intel-hda,id=hda -device hda-duplex,bus=hda.0,audiodev=wav0 $$audio_backend" ;; \
		native-dynamic) \
			$(MAKE) '$(DYNAMIC_SYSTEM_IMAGE)' '$(DYNAMIC_DATA_IMAGE)' || exit 1; \
			cp '$(DYNAMIC_DATA_IMAGE)' '$(TEST_BUILD_DIR)/$*/data.raw' || exit 1; \
			hardware='-boot order=d -blockdev driver=file,filename=$(DYNAMIC_SYSTEM_IMAGE),node-name=dynamic-system-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=dynamic-system-file,node-name=dynamic-system-raw,read-only=on -device nvme,serial=openrfs-system-fat32,drive=dynamic-system-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 -blockdev driver=file,filename=$(TEST_BUILD_DIR)/$*/data.raw,node-name=dynamic-data-file,read-only=off,auto-read-only=off -blockdev driver=raw,file=dynamic-data-file,node-name=dynamic-data-raw,read-only=off -device nvme,serial=openrfs-data-fat32,drive=dynamic-data-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1' ;; \
			# No emulator models an NVIDIA part, so the nvidia scenario \
			# attaches display and HD Audio functions of exactly the classes \
			# these drivers match on, from vendors that are not NVIDIA. \
			# That turns "no function present" from an empty machine into a \
			# refusal with something to refuse. \
			nvidia) \
				hardware='-vga std -device cirrus-vga,romfile= -device bochs-display,romfile= -device ich9-intel-hda' ;; \
			nvidia-builtin) hardware='' ;; \
			native|native-relaunch) \
				$(MAKE) '$(NATIVE_SYSTEM_IMAGE)' '$(NATIVE_DATA_IMAGE)' || exit 1; \
				cp '$(NATIVE_DATA_IMAGE)' '$(TEST_BUILD_DIR)/$*/data.raw' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(NATIVE_SYSTEM_IMAGE),node-name=native-system-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=native-system-file,node-name=native-system-raw,read-only=on -device nvme,serial=openrfs-system-fat32,drive=native-system-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 -blockdev driver=file,filename=$(TEST_BUILD_DIR)/$*/data.raw,node-name=native-data-file,read-only=off,auto-read-only=off -blockdev driver=raw,file=native-data-file,node-name=native-data-raw,read-only=off -device nvme,serial=openrfs-data-fat32,drive=native-data-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1' ;; \
			native-lua) \
				$(MAKE) '$(LUA_SYSTEM_IMAGE)' '$(LUA_DATA_IMAGE)' || exit 1; \
				cp '$(LUA_DATA_IMAGE)' '$(TEST_BUILD_DIR)/$*/data.raw' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(LUA_SYSTEM_IMAGE),node-name=lua-system-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=lua-system-file,node-name=lua-system-raw,read-only=on -device nvme,serial=openrfs-system-fat32,drive=lua-system-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 -blockdev driver=file,filename=$(TEST_BUILD_DIR)/$*/data.raw,node-name=lua-data-file,read-only=off,auto-read-only=off -blockdev driver=raw,file=lua-data-file,node-name=lua-data-raw,read-only=off -device nvme,serial=openrfs-data-fat32,drive=lua-data-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1' ;; \
			native-sqlite) \
				$(MAKE) '$(SQLITE_SYSTEM_IMAGE)' '$(SQLITE_DATA_IMAGE)' || exit 1; \
				cp '$(SQLITE_DATA_IMAGE)' '$(TEST_BUILD_DIR)/$*/data.raw' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(SQLITE_SYSTEM_IMAGE),node-name=sqlite-system-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=sqlite-system-file,node-name=sqlite-system-raw,read-only=on -device nvme,serial=openrfs-system-fat32,drive=sqlite-system-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 -blockdev driver=file,filename=$(TEST_BUILD_DIR)/$*/data.raw,node-name=sqlite-data-file,read-only=off,auto-read-only=off -blockdev driver=raw,file=sqlite-data-file,node-name=sqlite-data-raw,read-only=off -device nvme,serial=openrfs-data-fat32,drive=sqlite-data-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1' ;; \
			native-rust) \
				$(MAKE) '$(RUST_APP_SYSTEM_IMAGE)' '$(RUST_APP_DATA_IMAGE)' || exit 1; \
				cp '$(RUST_APP_DATA_IMAGE)' '$(TEST_BUILD_DIR)/$*/data.raw' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(RUST_APP_SYSTEM_IMAGE),node-name=rust-system-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=rust-system-file,node-name=rust-system-raw,read-only=on -device nvme,serial=openrfs-system-fat32,drive=rust-system-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 -blockdev driver=file,filename=$(TEST_BUILD_DIR)/$*/data.raw,node-name=rust-data-file,read-only=off,auto-read-only=off -blockdev driver=raw,file=rust-data-file,node-name=rust-data-raw,read-only=off -device nvme,serial=openrfs-data-fat32,drive=rust-data-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1' ;; \
			native-crash) \
				$(MAKE) '$(CRASH_SYSTEM_IMAGE)' '$(CRASH_DATA_IMAGE)' || exit 1; \
				cp '$(CRASH_DATA_IMAGE)' '$(TEST_BUILD_DIR)/$*/data.raw' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(CRASH_SYSTEM_IMAGE),node-name=crash-system-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=crash-system-file,node-name=crash-system-raw,read-only=on -device nvme,serial=openrfs-system-fat32,drive=crash-system-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 -blockdev driver=file,filename=$(TEST_BUILD_DIR)/$*/data.raw,node-name=crash-data-file,read-only=off,auto-read-only=off -blockdev driver=raw,file=crash-data-file,node-name=crash-data-raw,read-only=off -device nvme,serial=openrfs-data-fat32,drive=crash-data-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1' ;; \
			native-elf-refusal|native-digest-refusal|native-abi-refusal) \
				$(MAKE) '$(ADMISSION_SYSTEM_IMAGE)' '$(ADMISSION_DATA_IMAGE)' || exit 1; \
				cp '$(ADMISSION_DATA_IMAGE)' '$(TEST_BUILD_DIR)/$*/data.raw' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(ADMISSION_SYSTEM_IMAGE),node-name=admission-system-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=admission-system-file,node-name=admission-system-raw,read-only=on -device nvme,serial=openrfs-system-fat32,drive=admission-system-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 -blockdev driver=file,filename=$(TEST_BUILD_DIR)/$*/data.raw,node-name=admission-data-file,read-only=off,auto-read-only=off -blockdev driver=raw,file=admission-data-file,node-name=admission-data-raw,read-only=off -device nvme,serial=openrfs-data-fat32,drive=admission-data-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1' ;; \
			device-substrate) \
				hardware='-object rng-builtin,id=rng0 -device virtio-rng-pci,disable-legacy=on,rng=rng0' ;; \
			xhci) \
				hardware='-device qemu-xhci,id=xhci,streams=off -device usb-kbd,bus=xhci.0,port=1,usb_version=2' ;; \
			nvme) \
				rm -f '$(NVME_FIXTURE)' || exit 1; \
				$(PYTHON) tools/make-nvme-fixture.py '$(NVME_FIXTURE)' || exit 1; \
				test -f '$(NVME_FIXTURE)' || exit 1; \
				hardware='-blockdev driver=file,filename=$(NVME_FIXTURE),node-name=nvme-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=nvme-file,node-name=nvme-raw,read-only=on -device nvme,serial=openrfs-fixture,drive=nvme-raw,logical_block_size=4096,physical_block_size=4096,max_ioqpairs=1,msix_qsize=1' ;; \
			filesystem) \
				rm -f '$(FILESYSTEM_FIXTURE)' || exit 1; \
				$(PYTHON) tools/make-fat16-fixture.py '$(FILESYSTEM_FIXTURE)' || exit 1; \
				test -f '$(FILESYSTEM_FIXTURE)' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(FILESYSTEM_FIXTURE),node-name=filesystem-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=filesystem-file,node-name=filesystem-raw,read-only=on -device nvme,serial=openrfs-fat16-fixture,drive=filesystem-raw,logical_block_size=4096,physical_block_size=4096,max_ioqpairs=1,msix_qsize=1' ;; \
			process) \
				rm -f '$(PROCESS_FIXTURE)' '$(PROCESS_ELF)' || exit 1; \
				$(PYTHON) tools/make-process-fixture.py '$(PROCESS_FIXTURE)' || exit 1; \
				test -f '$(PROCESS_FIXTURE)' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(PROCESS_FIXTURE),node-name=process-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=process-file,node-name=process-raw,read-only=on -device nvme,serial=openrfs-process,drive=process-raw,logical_block_size=4096,physical_block_size=4096,max_ioqpairs=1,msix_qsize=1' ;; \
			linux-abi) \
				$(MAKE) '$(LINUX_ABI_FIXTURE)' || exit 1; \
				test -f '$(LINUX_ABI_FIXTURE)' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(LINUX_ABI_FIXTURE),node-name=linux-abi-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=linux-abi-file,node-name=linux-abi-raw,read-only=on -device nvme,serial=openrfs-linux-abi,drive=linux-abi-raw,logical_block_size=4096,physical_block_size=4096,max_ioqpairs=1,msix_qsize=1' ;; \
			linux-abi-uname) \
				$(MAKE) '$(LINUX_UNAME_FIXTURE)' || exit 1; \
				test -f '$(LINUX_UNAME_FIXTURE)' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(LINUX_UNAME_FIXTURE),node-name=linux-uname-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=linux-uname-file,node-name=linux-uname-raw,read-only=on -device nvme,serial=openrfs-linux-uname,drive=linux-uname-raw,logical_block_size=4096,physical_block_size=4096,max_ioqpairs=1,msix_qsize=1' ;; \
			ext4-recovery) \
				$(MAKE) '$(EXT4_FIXTURE)' || exit 1; \
				$(PYTHON) tools/ext4_image.py prepare-recovery-marker \
					'$(EXT4_FIXTURE)' '$(EXT4_RECOVERY_FIXTURE)' \
					--report '$(EXT4_RECOVERY_FIXTURE).before.json' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(EXT4_RECOVERY_FIXTURE),node-name=ext4-recovery-file,read-only=off,auto-read-only=off -blockdev driver=raw,file=ext4-recovery-file,node-name=ext4-recovery-raw,read-only=off -device nvme,serial=openrfs-ext4-recovery,drive=ext4-recovery-raw,logical_block_size=4096,physical_block_size=4096,max_ioqpairs=1,msix_qsize=1' ;; \
			openrfs-proof-userland) \
				$(MAKE) '$(OPENRFS_PROOF_USERLAND_IMAGE)' || exit 1; \
				test -f '$(OPENRFS_PROOF_USERLAND_IMAGE)' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(OPENRFS_PROOF_USERLAND_IMAGE),node-name=openrfs-proof-userland-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=openrfs-proof-userland-file,node-name=openrfs-proof-userland-raw,read-only=on -device nvme,serial=openrfs-userland,drive=openrfs-proof-userland-raw,logical_block_size=4096,physical_block_size=4096,max_ioqpairs=1,msix_qsize=1' ;; \
			openrfs-proof-userland-interactive) \
				$(MAKE) '$(OPENRFS_PROOF_USERLAND_IMAGE)' || exit 1; \
				test -f '$(OPENRFS_PROOF_USERLAND_IMAGE)' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(OPENRFS_PROOF_USERLAND_IMAGE),node-name=interactive-userland-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=interactive-userland-file,node-name=interactive-userland-raw,read-only=on -device nvme,serial=openrfs-interactive,drive=interactive-userland-raw,logical_block_size=4096,physical_block_size=4096,max_ioqpairs=1,msix_qsize=1' ;; \
			openrfs-proof-userland-interactive-absent) \
				$(MAKE) '$(OPENRFS_PROOF_USERLAND_NO_CAT_IMAGE)' || exit 1; \
				test -f '$(OPENRFS_PROOF_USERLAND_NO_CAT_IMAGE)' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(OPENRFS_PROOF_USERLAND_NO_CAT_IMAGE),node-name=interactive-absent-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=interactive-absent-file,node-name=interactive-absent-raw,read-only=on -device nvme,serial=openrfs-interactive-absent,drive=interactive-absent-raw,logical_block_size=4096,physical_block_size=4096,max_ioqpairs=1,msix_qsize=1' ;; \
			fat32-missing) \
				$(MAKE) '$(FAT32_SYSTEM_IMAGE)' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(FAT32_SYSTEM_IMAGE),node-name=fat32-system-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=fat32-system-file,node-name=fat32-system-raw,read-only=on -device nvme,serial=openrfs-system-fat32,drive=fat32-system-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1' ;; \
			fat32-full) \
				$(MAKE) '$(FAT32_SYSTEM_IMAGE)' '$(FAT32_FULL_IMAGE)' || exit 1; \
				cp '$(FAT32_FULL_IMAGE)' '$(TEST_BUILD_DIR)/$*/data.raw' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(FAT32_SYSTEM_IMAGE),node-name=fat32-system-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=fat32-system-file,node-name=fat32-system-raw,read-only=on -device nvme,serial=openrfs-system-fat32,drive=fat32-system-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 -blockdev driver=file,filename=$(TEST_BUILD_DIR)/$*/data.raw,node-name=fat32-data-file,read-only=off,auto-read-only=off -blockdev driver=raw,file=fat32-data-file,node-name=fat32-data-raw,read-only=off -device nvme,serial=openrfs-data-fat32,drive=fat32-data-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1' ;; \
			fat32-corrupt) \
				$(MAKE) '$(FAT32_SYSTEM_IMAGE)' '$(FAT32_CORRUPT_IMAGE)' || exit 1; \
				cp '$(FAT32_CORRUPT_IMAGE)' '$(TEST_BUILD_DIR)/$*/data.raw' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(FAT32_SYSTEM_IMAGE),node-name=fat32-system-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=fat32-system-file,node-name=fat32-system-raw,read-only=on -device nvme,serial=openrfs-system-fat32,drive=fat32-system-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 -blockdev driver=file,filename=$(TEST_BUILD_DIR)/$*/data.raw,node-name=fat32-data-file,read-only=off,auto-read-only=off -blockdev driver=raw,file=fat32-data-file,node-name=fat32-data-raw,read-only=off -device nvme,serial=openrfs-data-fat32,drive=fat32-data-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1' ;; \
			fat32-*) \
				$(MAKE) '$(FAT32_SYSTEM_IMAGE)' '$(FAT32_DATA_IMAGE)' || exit 1; \
				cp '$(FAT32_DATA_IMAGE)' '$(TEST_BUILD_DIR)/$*/data.raw' || exit 1; \
				hardware='-boot order=d -blockdev driver=file,filename=$(FAT32_SYSTEM_IMAGE),node-name=fat32-system-file,read-only=on,auto-read-only=off -blockdev driver=raw,file=fat32-system-file,node-name=fat32-system-raw,read-only=on -device nvme,serial=openrfs-system-fat32,drive=fat32-system-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 -blockdev driver=file,filename=$(TEST_BUILD_DIR)/$*/data.raw,node-name=fat32-data-file,read-only=off,auto-read-only=off -blockdev driver=raw,file=fat32-data-file,node-name=fat32-data-raw,read-only=off -device nvme,serial=openrfs-data-fat32,drive=fat32-data-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1' ;; \
			*) hardware='' ;; \
	esac; \
	log='$(TEST_BUILD_DIR)/$*/serial.log'; \
	rm -f "$$log"; \
	timeout_seconds=15; reboot_control='-no-reboot'; \
	case '$*' in \
		openrfs-proof) timeout_seconds=60 ;; \
		fat32-*) timeout_seconds=45 ;; \
		ext4-recovery) timeout_seconds=600 ;; \
		native) timeout_seconds=180 ;; \
		native-lua) timeout_seconds=150 ;; \
		native-sqlite) timeout_seconds=240 ;; \
		native-crash|native-relaunch|native-audio) timeout_seconds=180 ;; \
		native-sdl) timeout_seconds=240 ;; \
		native-dynamic) timeout_seconds=120 ;; \
		native-rust|native-*-refusal) timeout_seconds=120 ;; \
	esac; \
	if test '$*' = fat32-persistence -o '$*' = native-sqlite; then reboot_control=''; fi; \
	monitor_argument='-monitor none'; injector=''; injection_result=0; \
	if test '$*' = native-lua; then \
		monitor_socket='$(QEMU_LUA_MONITOR_SOCKET)'; \
		rm -f "$$monitor_socket"; \
		monitor_argument='$(QEMU_LUA_MONITOR_ARGUMENT)'; \
		$(PYTHON) tools/qemu-send-keys.py --monitor "$$monitor_socket" \
			--serial "$$log" --marker 'OPENRFS LUA INPUT READY' \
			--text openrfs --enter --timeout 120 & injector=$$!; \
	elif test '$*' = native-sdl; then \
		monitor_socket='$(QEMU_SDL_MONITOR_SOCKET)'; \
		rm -f "$$monitor_socket"; \
		monitor_argument='$(QEMU_SDL_MONITOR_ARGUMENT)'; \
		$(PYTHON) tools/qemu-send-keys.py --monitor "$$monitor_socket" \
			--serial "$$log" --marker 'OPENRFS SDL READY run=1' \
			--text s --hmp 'mouse_move -100 0' \
			--hmp 'mouse_button 1' \
			--hmp 'mouse_button 0' \
			--capture-dir '$(abspath $(TEST_BUILD_DIR)/$*/sdl-frames)' \
			--screenshot '$(abspath $(TEST_BUILD_DIR)/$*/sdl.png)' \
			--video '$(abspath $(TEST_BUILD_DIR)/$*/sdl.mp4)' \
			--ffmpeg '$(FFMPEG)' --timeout 180 \
			& injector=$$!; \
	fi; \
	set +e; \
	timeout "$${timeout_seconds}s" qemu-system-x86_64 \
		-machine accel=$(QEMU_ACCEL) -m 128M -smp 1 $$hardware \
		-cdrom '$<' -display none $$monitor_argument -serial stdio \
		-device isa-debug-exit,iobase=0xf4,iosize=0x04 \
		$$reboot_control >"$$log" 2>&1; result=$$?; \
	if test -n "$$injector"; then wait "$$injector" || injection_result=$$?; fi; \
	set -e; \
	begin_count=$$(grep -Fxc 'ST BEGIN $*' "$$log" || true); \
	pass_count=$$(grep -Fxc 'ST PASS $*' "$$log" || true); \
	expected_begin=1; \
	if test '$*' = fat32-persistence -o '$*' = native-sqlite; then expected_begin=2; fi; \
	if test $$result -ne $$expected -o $$injection_result -ne 0 -o "$$begin_count" -ne "$$expected_begin" -o "$$pass_count" -ne 1 || \
		grep -Fq 'ST FAIL' "$$log" || grep -Fq 'OpenRFS PANIC' "$$log"; then \
		echo 'QEMU scenario $* failed: status='$$result' expected='$$expected; \
		cat "$$log"; \
		exit 1; \
	fi; \
	if test '$*' = normal && \
		{ ! grep -Fq 'OpenRFS: ACPI root verified' "$$log" || \
		  ! grep -Fq 'OpenRFS: ACPI MADT verified' "$$log" || \
		  ! grep -Fq 'OpenRFS: ACPI topology verified' "$$log" || \
		  ! grep -Eq '^OpenRFS: ACPI I/O APIC id [0-9]+ at 0x' "$$log" || \
		  ! grep -Fq 'OpenRFS: local APIC online' "$$log" || \
		  ! grep -Fq 'OpenRFS: local APIC legacy routing LINT0 ExtINT' "$$log" || \
		  ! grep -Eq '^OpenRFS: local APIC EOI-broadcast suppression (supported|unsupported) active (yes|no)$$' "$$log" || \
		  ! grep -Fq 'OpenRFS: I/O APIC online' "$$log" || \
		  ! grep -Eq '^OpenRFS: I/O APIC id [0-9]+ version 0x[0-9A-F]+ entries [0-9]+ base GSI [0-9]+ directed EOI (yes|no)$$' "$$log" || \
		  ! grep -Fq 'OpenRFS: I/O APIC delivered eight interrupts' "$$log" || \
		  ! grep -Fq 'OpenRFS: legacy 8259 retired' "$$log" || \
		  ! grep -Fq 'OpenRFS: timer survives legacy retirement' "$$log" || \
		  ! grep -Eq '^OpenRFS: I/O APIC level route id [0-9]+ GSI [0-9]+ vector [0-9]+ active (high|low) acknowledgement (directed|broadcast)$$' "$$log" || \
		  ! grep -Eq '^OpenRFS: I/O APIC level deliveries [0-9]+ remote IRR [0-9]+ directed EOI [0-9]+ in [0-9]+ ns$$' "$$log" || \
		  ! grep -Fq 'OpenRFS: I/O APIC delivered eight level-triggered interrupts' "$$log" || \
		  ! grep -Fq 'OpenRFS: level-triggered routing established' "$$log" || \
		  ! grep -Eq '^OpenRFS: local APIC timer calibrated at [0-9]+ counts' "$$log" || \
		  ! grep -Fq 'OpenRFS: local APIC timer delivered eight interrupts' "$$log" || \
		  ! grep -Eq '^OpenRFS: TSC calibrated at [0-9]+ Hz' "$$log" || \
		  ! grep -Fq 'OpenRFS: TSC reference established' "$$log" || \
		  ! grep -Fq 'OpenRFS: ACPI FADT verified' "$$log" || \
		  ! grep -Fq 'OpenRFS: ACPI MCFG absent' "$$log" || \
		  ! grep -Fq 'OpenRFS: ACPI configuration windows verified' "$$log" || \
		  ! grep -Eq '^OpenRFS: ACPI PM timer port 0x[0-9A-F]+ width (24|32) bits address (fixed|extended)$$' "$$log" || \
		  ! grep -Eq '^OpenRFS: PM timer counted [0-9]+ ticks in [0-9]+ ns$$' "$$log" || \
		  ! grep -Fq 'OpenRFS: PM timer independent reference established' "$$log" || \
		  ! grep -Eq '^OpenRFS: clocks agree: PM [0-9]+ ns, APIC timer [0-9]+ ns, TSC [0-9]+ ns$$' "$$log" || \
		  ! grep -Fq 'OpenRFS: PIT retired' "$$log" || \
		  ! grep -Fq 'OpenRFS: clocks survive PIT retirement' "$$log" || \
		  ! grep -Fq 'OpenRFS: monotonic clock on time-stamp counter' "$$log" || \
		  ! grep -Eq '^OpenRFS: slept [0-9]+ ns for a [0-9]+ ns deadline$$' "$$log" || \
		  ! grep -Fq 'OpenRFS: deadline timers online' "$$log" || \
		  ! grep -Fq 'OpenRFS: monotonic time established' "$$log" || \
		  ! grep -Eq '^OpenRFS: paging root 0x[0-9A-F]+ table frames [0-9]+ regions [0-9]+ NX yes write protect yes$$' "$$log" || \
		  ! grep -Eq '^OpenRFS: paging leaves [0-9]+ writable [0-9]+ executable [0-9]+ both 0$$' "$$log" || \
		  ! grep -Fq 'OpenRFS: kernel page tables installed' "$$log" || \
		  ! grep -Fq 'OpenRFS: no writable executable mapping' "$$log" || \
		  ! grep -Eq '^OpenRFS: IA32_PAT before 0x[0-9A-F]{16} after 0x[0-9A-F]{16} entry 1 write-combining$$' "$$log" || \
		  ! grep -Eq '^OpenRFS: framebuffer memory type write-combining pages [1-9][0-9]*$$' "$$log" || \
		  ! grep -Fq 'OpenRFS: write-combining established' "$$log" || \
		  ! grep -Fq 'OpenRFS: virtual memory established' "$$log" || \
		  ! grep -Eq '^OpenRFS: heap window 0x[0-9A-F]+ size [0-9]+ guards 0x[0-9A-F]+ 0x[0-9A-F]+$$' "$$log" || \
		  ! grep -Eq '^OpenRFS: heap committed [0-9]+ bytes in [0-9]+ pages, live 3$$' "$$log" || \
		  ! grep -Fq 'OpenRFS: kernel heap online' "$$log" || \
		  ! grep -Fq 'OpenRFS: heap coalesced to one free block' "$$log" || \
		  ! grep -Fq 'OpenRFS: kernel heap established' "$$log" || \
		  ! grep -Eq '^OpenRFS: deadline table of [0-9]+ entries on the heap$$' "$$log" || \
		  ! grep -Eq '^OpenRFS: PCI mechanism 1 online, no window mapped$$' "$$log" || \
		  ! grep -Eq '^OpenRFS: PCI buses [1-9][0-9]* functions [1-9][0-9]* bridges [0-9]+$$' "$$log" || \
		  ! grep -Eq '^OpenRFS: PCI 0:0\.0 vendor 0x[0-9A-F]+ device 0x[0-9A-F]+ class 0x0*6\.0x0* ' "$$log" || \
		  ! grep -Fq 'OpenRFS: PCI configuration space enumerated' "$$log" || \
		  ! grep -Fq 'OpenRFS: PCI enumeration established' "$$log" || \
		  ! grep -Fxq 'OpenRFS: PCI resource ownership negative controls 4/4 passed' "$$log" || \
		  ! grep -Fxq 'OpenRFS: supervisor NX UC device-MMIO arena established' "$$log" || \
		  ! grep -Fxq 'OpenRFS: dynamic vector negative controls 4/4 passed' "$$log" || \
		  ! grep -Fxq 'OpenRFS: dynamic interrupt vector foundation established' "$$log" || \
		  ! grep -Fxq 'OpenRFS: bounded DMA negative controls 2/2 passed' "$$log" || \
		  ! grep -Fxq 'OpenRFS: contiguous DMA ownership foundation established' "$$log" || \
		  ! grep -Fxq 'OpenRFS: xHCI foundation robustness controls 17/17 passed' "$$log" || \
		  ! grep -Fxq 'OpenRFS: bounded xHCI host-controller foundation established' "$$log" || \
		  ! grep -Fxq 'OpenRFS: xHCI fixture absent' "$$log" || \
		  ! grep -Fxq 'OpenRFS: NVMe foundation robustness controls 20/20 passed' "$$log" || \
		  ! grep -Fxq 'OpenRFS: bounded NVMe block-controller foundation established' "$$log" || \
		  ! grep -Fxq 'OpenRFS: NVMe fixture absent' "$$log" || \
		  ! grep -Fxq 'OpenRFS: FAT16 foundation robustness controls 26/26 passed' "$$log" || \
		  ! grep -Fxq 'OpenRFS: bounded read-only FAT16 foundation established' "$$log" || \
		  ! grep -Fxq 'OpenRFS: FAT16 fixture absent' "$$log" || \
		  ! grep -Fxq 'OpenRFS: process address-space foundation controls 8/8 passed' "$$log" || \
		  ! grep -Fxq 'OpenRFS: ELF64 parser robustness controls 34/34 passed' "$$log" || \
		  ! grep -Fxq 'OpenRFS: process fixture absent' "$$log" || \
		  ! grep -Fxq 'OpenRFS: Linux SYSCALL CPU foundation controls 10/10 passed' "$$log" || \
		  ! grep -Fxq 'OpenRFS: BusyBox image and Linux stack controls 32/32 passed' "$$log" || \
		  ! grep -Fxq 'OpenRFS: Linux ABI fixture absent' "$$log" || \
		  ! grep -Eq '^OpenRFS: threads online, 3 ready of [0-9]+ on 12 stack frames$$' "$$log" || \
		  ! grep -Fxq 'OpenRFS: thread rotation 123123123123' "$$log" || \
		  ! grep -Eq '^OpenRFS: threads switched [1-9][0-9]* times, 3 exited$$' "$$log" || \
		  ! grep -Fq 'OpenRFS: kernel threads established' "$$log" || \
		  ! grep -Eq '^OpenRFS: framebuffer [0-9]+x[0-9]+ at 0x[0-9A-F]+ pitch [0-9]+ RGB [0-9]+/[0-9]+/[0-9]+$$' "$$log" || \
		  ! grep -Fxq 'OpenRFS: framebuffer verified 786432 pixels' "$$log" || \
		  ! grep -Fq 'OpenRFS: framebuffer established' "$$log" || \
		  ! grep -Eq '^OpenRFS: surface [0-9]+x[0-9]+ pitch [0-9]+ buffer [0-9]+ bytes$$' "$$log" || \
		  ! grep -Eq '^OpenRFS: surface cycles full present [0-9]+ one-line update [0-9]+ scroll [0-9]+$$' "$$log" || \
		  ! grep -Eq '^OpenRFS: surface split cycles full draw [0-9]+ push [0-9]+ one-line draw [0-9]+ push [0-9]+ scroll draw [0-9]+ push [0-9]+$$' "$$log" || \
		  ! grep -Eq '^OpenRFS: surface sparse two-corner cycles total [0-9]+ draw [0-9]+ push [0-9]+ union [0-9]+$$' "$$log" || \
		  ! grep -Eq '^OpenRFS: surface copied [0-9]+ full, [0-9]+ line, [0-9]+ scroll pixels$$' "$$log" || \
		  ! grep -Fq 'OpenRFS: cached surface established' "$$log" || \
		  ! grep -Eq '^OpenRFS: screen console [0-9]+x[0-9]+ cells of 8x16, font [0-9]+ bytes$$' "$$log" || \
		  ! grep -Eq '^OpenRFS: screen console drew [0-9]+ characters and scrolled [0-9]+ times$$' "$$log" || \
		  ! grep -Fq 'OpenRFS: screen console established' "$$log" || \
		  ! grep -Fq 'OpenRFS: screen console passed' "$$log" || \
		  ! grep -Eq '^OpenRFS: keyboard 8042 online, IRQ 1 routed, [0-9]+ interrupts for [0-9]+ events$$' "$$log" || \
		  ! grep -Fxq 'OpenRFS: keyboard decoded "hiI" from injected scancodes' "$$log" || \
		  ! grep -Fq 'OpenRFS: keyboard established' "$$log" || \
		  ! grep -Fq 'OpenRFS: keyboard passed' "$$log" || \
		  ! grep -Fq 'OpenRFS: BT11 Boot Ledger installed proof passed' "$$log" || \
		  ! grep -Fq 'OpenRFS: font verified' "$$log" || \
		  ! grep -Eq '^OpenRFS: PS/2 pointer (available|unavailable: .+)$$' "$$log" || \
		  ! grep -Fq 'OpenRFS: layout validated' "$$log" || \
		  ! grep -Fq 'OpenRFS: command line ready; desktop waits for starty' "$$log" || \
		  ! grep -Fxq 'OpenRFS: shell ran "echo hi" from 8 injected scancodes' "$$log" || \
		  ! grep -Fq 'OpenRFS: shell output verified on screen' "$$log" || \
		  ! grep -Fq 'OpenRFS: shell established' "$$log" || \
		  ! grep -Fq 'OpenRFS: shell passed' "$$log" || \
		  ! grep -Fq 'OpenRFS: never triple fault milestone passed' "$$log"; }; then \
		echo 'normal scenario did not complete the integrated production path'; \
		cat "$$log"; \
		exit 1; \
	fi; \
	if test '$*' = normal && \
		{ grep -Fq 'OpenRFS: desktop constructed' "$$log" || \
		  grep -Fq 'OpenRFS: desktop activated' "$$log" || \
		  grep -Fq 'OpenRFS: installed proof passed' "$$log"; }; then \
		echo 'normal scenario entered the desktop before starty authentication'; \
		cat "$$log"; \
		exit 1; \
	fi; \
	diagnostics_ok=true; \
	case '$*' in \
		invalid-opcode) \
			grep -Fq '  vector=6 name=invalid opcode' "$$log" || diagnostics_ok=false ;; \
		page-fault) \
			grep -Fq '  vector=14 name=page fault' "$$log" && \
			grep -Fq '  cr2=0x0000000100000000' "$$log" && \
			grep -Fq '  page-fault bits: P=0 W=0 U=0 RSVD=0 I=0' "$$log" || \
				diagnostics_ok=false ;; \
		unexpected) \
			grep -Fq '  vector=128 name=unexpected vector' "$$log" || diagnostics_ok=false ;; \
		double-fault) \
			grep -Fq 'OpenRFS DOUBLE FAULT - HALTED' "$$log" || diagnostics_ok=false ;; \
		paging) \
			grep -Fq '  vector=14 name=page fault' "$$log" && \
			grep -Fq '  cr2=0x0000000200000000' "$$log" && \
			grep -Fq '  page-fault bits: P=1 W=1 U=0 RSVD=0 I=0' "$$log" || \
				diagnostics_ok=false ;; \
		heap) \
			grep -Fq '  vector=14 name=page fault' "$$log" && \
			grep -Fq '  cr2=0x0000000401000000' "$$log" && \
			grep -Fq '  page-fault bits: P=0 W=1 U=0 RSVD=0 I=0' "$$log" || \
				diagnostics_ok=false ;; \
		ioapic-level) \
			grep -Eq '^ST INFO ioapic-level: [0-9]+ deliveries, remote IRR [0-9]+, directed EOI [0-9]+, mode (directed|broadcast), in [0-9]+ ns$$' "$$log" || \
				diagnostics_ok=false ;; \
		pci) \
			grep -Eq '^ST PCI ports functions [0-9]+ buses [0-9]+$$' "$$log" && \
			! grep -Fq 'OpenRFS: ACPI MCFG at' "$$log" || \
				diagnostics_ok=false ;; \
		pci-ecam) \
			grep -Fq 'OpenRFS: ACPI MCFG at' "$$log" && \
			grep -Eq '^ST PCI window agreed on [0-9]+ registers of [0-9]+ functions across [0-9]+ buses, [0-9]+ with MSI-X$$' "$$log" && \
			! grep -Eq '^ST PCI window agreed on [0-9]+ registers of 0 functions' "$$log" || \
				diagnostics_ok=false ;; \
		threads) \
			grep -Eq '^ST THREADS created [0-9]+ switches [0-9]+ exited [0-9]+$$' "$$log" || \
				diagnostics_ok=false ;; \
		framebuffer) \
			grep -Eq '^ST FRAMEBUFFER [0-9]+x[0-9]+ probes 16 pitch [0-9]+$$' "$$log" || \
				diagnostics_ok=false ;; \
		surface) \
			grep -Eq '^ST SURFACE full [0-9]+ line [0-9]+ clipped 4 overlap both damage 20$$' "$$log" || \
				diagnostics_ok=false ;; \
		write-combining) \
			grep -Eq '^ST WRITE-COMBINING PAT 0x[0-9A-F]{16} ENTRY 1 FRAMEBUFFER [1-9][0-9]* PAGES$$' "$$log" || \
				diagnostics_ok=false ;; \
		device-windows) \
			grep -Eq '^ST DEVICE-WINDOWS WINDOWS [1-9][0-9]* PAGES [1-9][0-9]* VGA 1 LOCAL-APIC 1 IO-APICS [1-9][0-9]* ECAM 1 FRAMEBUFFER 1$$' "$$log" || \
				diagnostics_ok=false ;; \
		boot-ledger) \
			grep -Eq '^ST LEDGER stages [1-9][0-9]* receipts [1-9][0-9]* capabilities [1-9][0-9]* skips [0-9]+ fingerprint 0x[0-9A-F]{16}$$' "$$log" && \
			grep -Fxq 'OpenRFS: BT11 Boot Ledger installed proof passed' "$$log" || \
				diagnostics_ok=false ;; \
		openrfs-proof) \
		grep -Eq '^ST OPENRFS_PROOF geometry 1024x768 apps 5 events [1-9][0-9]* windows [1-9][0-9]* cursor [1-9][0-9]* damage [1-9][0-9]* fingerprint 0x[0-9A-F]{16}$$' "$$log" && \
			grep -Fxq 'OpenRFS: installed proof passed' "$$log" || \
				diagnostics_ok=false ;; \
		device-substrate) \
			grep -Fxq 'ST DEVICE_SUBSTRATE dma 64 msix 1 used 0->1 ownership CPU-DEVICE-CPU teardown clean negatives 14' "$$log" && \
			grep -Fxq 'OpenRFS: device substrate teardown complete' "$$log" && \
			grep -Eq '^OpenRFS: VirtIO RNG device DMA wrote 64 bytes; nonzero [1-9][0-9]*$$' "$$log" && \
			grep -Fxq 'OpenRFS: MSI-X delivered 1 interrupt; used ring 0 -> 1' "$$log" || \
				diagnostics_ok=false ;; \
		xhci) \
			grep -Fxq 'ST XHCI descriptor 18 msix 1 ownership CPU-CONTROLLER-CPU teardown clean robustness 19' "$$log" && \
			grep -Fxq 'OpenRFS: xHCI controller ready' "$$log" && \
			grep -Fxq 'OpenRFS: USB device descriptor DMA completed: 18 bytes' "$$log" && \
			grep -Fxq 'OpenRFS: xHCI MSI-X descriptor completion count 1' "$$log" && \
			grep -Fxq 'OpenRFS: xHCI DMA ownership CPU-CONTROLLER-CPU complete' "$$log" && \
			grep -Fxq 'OpenRFS: xHCI teardown complete' "$$log" || \
				diagnostics_ok=false ;; \
		nvme) \
			grep -Fxq 'ST NVME read 4096 msix 1 ownership CPU-CONTROLLER-CPU teardown clean robustness 22' "$$log" && \
			grep -Fxq 'OpenRFS: NVMe controller ready' "$$log" && \
			grep -Fxq 'OpenRFS: NVMe namespace ready' "$$log" && \
			grep -Fxq 'OpenRFS: NVMe block read completed: 4096 bytes' "$$log" && \
			grep -Fxq 'OpenRFS: NVMe MSI-X read completion count 1' "$$log" && \
			grep -Fxq 'OpenRFS: NVMe DMA ownership CPU-CONTROLLER-CPU complete' "$$log" && \
				grep -Fxq 'OpenRFS: NVMe teardown complete' "$$log" || \
				diagnostics_ok=false ;; \
		filesystem) \
			grep -Fxq 'ST FAT16 file OPENRFS.BIN bytes 128 reads 4 msix 4 ownership CPU-CONTROLLER-CPU teardown clean robustness 28' "$$log" && \
			grep -Fxq 'OpenRFS: NVMe fixture absent' "$$log" && \
			grep -Fxq 'OpenRFS: FAT16 volume ready' "$$log" && \
			grep -Fxq 'OpenRFS: FAT16 file OPENRFS.BIN read: 128 bytes' "$$log" && \
			grep -Fxq 'OpenRFS: FAT16 MSI-X completion count 4' "$$log" && \
			grep -Fxq 'OpenRFS: FAT16 DMA ownership CPU-CONTROLLER-CPU complete' "$$log" && \
			grep -Fxq 'OpenRFS: FAT16 teardown complete' "$$log" || \
				diagnostics_ok=false ;; \
		process) \
			grep -Fxq 'ST PROCESS ELF64 OPENRFS.BIN bytes 128 segments 1 ring 3 address-space private result valid teardown clean robustness 50' "$$log" && \
			grep -Fxq 'OpenRFS: NVMe fixture absent' "$$log" && \
			grep -Fxq 'OpenRFS: FAT16 fixture absent' "$$log" && \
			grep -Fxq 'OpenRFS: process address-space foundation controls 8/8 passed' "$$log" && \
			grep -Fxq 'OpenRFS: ELF64 parser robustness controls 34/34 passed' "$$log" || \
				diagnostics_ok=false ;; \
		linux-abi) \
			grep -Fxq 'ST LINUX ABI busybox echo bytes 8 syscalls 9 stdout valid exit 0 ring 3 address-space private teardown clean robustness 72' "$$log" && \
			grep -Fxq 'OpenRFS: Linux SYSCALL CPU foundation controls 10/10 passed' "$$log" && \
			grep -Fxq 'OpenRFS: BusyBox image and Linux stack controls 32/32 passed' "$$log" && \
			grep -Fqx 'OPENRFS' "$$log" || \
				diagnostics_ok=false ;; \
		linux-abi-uname) \
			grep -Fxq 'ST LINUX ABI busybox uname bytes 6 syscalls 6 output valid exit 0 ring 3 address-space private copy-out valid teardown clean robustness 97' "$$log" && \
			grep -Fxq 'OpenRFS: Linux SYSCALL CPU foundation controls 10/10 passed' "$$log" && \
			grep -Fxq 'OpenRFS: BusyBox uname image and UTS controls 50/50 passed' "$$log" && \
			grep -Fqx 'Linux' "$$log" || \
				diagnostics_ok=false ;; \
		openrfs-proof-userland) \
			grep -Fxq 'ST OPENRFS_PROOF_USERLAND shell production echo 2 uname 2 invalid-profile recovered CPL3 SYSCALL stdout exact exit 0 teardown clean prompt restored' "$$log" && \
			test "$$(grep -Fxc 'OPENRFS' "$$log")" -eq 2 && \
			test "$$(grep -Fxc 'Linux' "$$log")" -eq 2 && \
			grep -Fxq 'RW USERLAND launch completed successfully echo ordinal 2' "$$log" && \
			grep -Fxq 'RW USERLAND launch completed successfully uname ordinal 2' "$$log" && \
			grep -Fxq 'RW USERLAND OpenRFS prompt restored' "$$log" || \
				diagnostics_ok=false ;; \
		openrfs-proof-userland-absent) \
			grep -Fxq 'linux: userspace volume unavailable' "$$log" && \
			grep -Fxq 'still usable' "$$log" && \
			grep -Fxq 'ST OPENRFS_PROOF_USERLAND_ABSENT concise refusal prompt usable teardown clean' "$$log" && \
			grep -Fxq 'RW USERLAND launch refused and teardown complete' "$$log" && \
			grep -Fxq 'RW USERLAND OpenRFS prompt restored' "$$log" || \
				diagnostics_ok=false ;; \
		openrfs-proof-userland-interactive) \
			grep -Fxq 'ST OPENRFS_PROOF_USERLAND_INTERACTIVE cat 2 keyboard IRQ read SYSCALL copy-out resume write SYSCALL stdout exact EOF exit 0 teardown clean fresh generation prompt restored' "$$log" && \
			test "$$(grep -Fxc 'RW USERLAND command accepted through OpenRFS shell linux cat' "$$log")" -eq 2 && \
			test "$$(grep -Fxc 'RW USERLAND deterministic read-only NVMe/FAT16 profile selected cat CATBOX' "$$log")" -eq 2 && \
			test "$$(grep -Fxc 'RW USERLAND Rust FAT16 SHA-256 ELF64 validation passed cat bytes 38632' "$$log")" -eq 2 && \
			test "$$(grep -Fxc 'RW USERLAND private CPL3 address space entered cat' "$$log")" -eq 2 && \
			test "$$(grep -Fxc 'RW CAT authentic read SYSCALL observed' "$$log")" -eq 4 && \
			test "$$(grep -Fxc 'RW CAT suspended with kernel CR3 and safe stack restored' "$$log")" -eq 4 && \
			grep -Fxq 'RW CAT terminal input accepted through keyboard events bytes 7' "$$log" && \
			grep -Fxq 'RW CAT terminal input accepted through keyboard events bytes 6' "$$log" && \
			test "$$(grep -Fxc 'RW CAT destination validated and all-or-nothing copy-out complete' "$$log")" -eq 2 && \
			test "$$(grep -Fxc 'RW CAT runtime negative controls 28/28 passed' "$$log")" -eq 2 && \
			test "$$(grep -Fxc 'RW CAT authenticated process generation ready to resume' "$$log")" -eq 4 && \
			test "$$(grep -Fxc 'RW CAT authentic write SYSCALL observed' "$$log")" -eq 2 && \
			test "$$(grep -Fxc 'RW CAT userspace stdout accepted' "$$log")" -eq 2 && \
			test "$$(grep -Fxc 'RW CAT EOF converted to zero-length read result' "$$log")" -eq 2 && \
			test "$$(grep -Fxc 'RW CAT exit status zero observed' "$$log")" -eq 2 && \
			test "$$(grep -Fxc 'RW CAT address-space teardown complete' "$$log")" -eq 2 && \
			test "$$(grep -Fxc 'RW USERLAND OpenRFS prompt restored' "$$log")" -eq 2 && \
			test "$$(grep -Fxc 'pebble' "$$log")" -eq 2 && \
			test "$$(grep -Fxc 'again' "$$log")" -eq 2 && \
			grep -Fxq 'RW USERLAND launch completed successfully cat ordinal 2' "$$log" || \
				diagnostics_ok=false ;; \
		openrfs-proof-userland-interactive-absent) \
			grep -Fxq 'ST OPENRFS_PROOF_USERLAND_INTERACTIVE_ABSENT cat missing echo valid keyboard IRQ refusal recoverable teardown clean prompt usable' "$$log" && \
			grep -Fxq 'linux: measured profile refused' "$$log" && \
			grep -Fxq 'RW USERLAND deterministic read-only NVMe/FAT16 profile selected cat CATBOX' "$$log" && \
			grep -Fxq 'RW USERLAND launch refused and teardown complete' "$$log" && \
			grep -Fxq 'RW USERLAND command accepted through OpenRFS shell linux echo' "$$log" && \
			grep -Fqx 'OPENRFS' "$$log" && \
			grep -Fxq 'RW USERLAND launch completed successfully echo ordinal 1' "$$log" && \
			test "$$(grep -Fxc 'RW USERLAND OpenRFS prompt restored' "$$log")" -eq 2 || \
				diagnostics_ok=false ;; \
		fat32-system) \
			grep -Fxq 'ST FAT32 SYSTEM authenticated echo uname FAT32 immutable' "$$log" && \
			grep -Fxq 'RW USERLAND deterministic read-only NVMe/FAT32 profile selected echo BUSYBOX' "$$log" && \
			grep -Fxq 'RW USERLAND deterministic read-only NVMe/FAT32 profile selected uname UNAMEBOX' "$$log" && \
			grep -Fxq 'RW USERLAND Rust FAT32 SHA-256 ELF64 validation passed echo bytes 33584' "$$log" && \
			grep -Fxq 'RW USERLAND Rust FAT32 SHA-256 ELF64 validation passed uname bytes 38368' "$$log" || \
				diagnostics_ok=false ;; \
		fat32-data) \
			grep -Fxq 'ST FAT32 DATA create read write append sync exact' "$$log" && \
			grep -Fxq 'data synchronized' "$$log" || diagnostics_ok=false ;; \
		fat32-nested) \
			grep -Fxq 'ST FAT32 NESTED dot dotdot traversal enumeration exact' "$$log" || diagnostics_ok=false ;; \
		fat32-growth) \
			grep -Fxq 'ST FAT32 GROWTH bytes 568 clusters 2 contents readable' "$$log" || diagnostics_ok=false ;; \
		fat32-random) \
			grep -Fxq 'ST FAT32 RANDOM seek overwrite preserved surrounding bytes' "$$log" || diagnostics_ok=false ;; \
		fat32-truncate) \
			grep -Fxq 'ST FAT32 TRUNCATE release regrow zero tail exact' "$$log" || diagnostics_ok=false ;; \
		fat32-rename) \
			grep -Fxq 'ST FAT32 RENAME file move directory parent updated' "$$log" || diagnostics_ok=false ;; \
		fat32-delete) \
			grep -Fxq 'ST FAT32 DELETE cluster reused nonempty directory refused' "$$log" || diagnostics_ok=false ;; \
		fat32-full) \
			grep -Fxq 'ST FAT32 FULL refusal no leak deletion recovered' "$$log" && \
			grep -Fxq 'write: volume has no free cluster' "$$log" || diagnostics_ok=false ;; \
		fat32-corrupt) \
			grep -Fxq 'ST FAT32 CORRUPT refused session usable system executable valid' "$$log" && \
			grep -Fxq 'data    fat32  unavailable' "$$log" && \
			grep -Fqx 'OPENRFS' "$$log" || diagnostics_ok=false ;; \
		fat32-missing) \
			grep -Fxq 'ST FAT32 MISSING session usable system executable valid' "$$log" && \
			grep -Fxq 'data    fat32  absent' "$$log" && \
			grep -Fqx 'OPENRFS' "$$log" || diagnostics_ok=false ;; \
		fat32-persistence) \
			grep -Fxq 'ST FAT32 PERSISTENCE synchronized reboot phase' "$$log" && \
			grep -Fxq 'ST FAT32 PERSISTENCE clean reboot retained exact contents' "$$log" && \
			grep -Fqx 'first cut' "$$log" && grep -Fqx 'second line' "$$log" || \
				diagnostics_ok=false ;; \
		fat32-cache) \
			grep -Fxq 'ST FAT32 CACHE six clusters eviction sync readback exact' "$$log" || diagnostics_ok=false ;; \
		fat32-immutable) \
			grep -Fxq 'ST FAT32 IMMUTABLE write refused below shell executable valid' "$$log" && \
			grep -Fqx 'OPENRFS' "$$log" || diagnostics_ok=false ;; \
		fat32-handles) \
			grep -Fxq 'ST FAT32 HANDLES generation stale double-close access bound clean' "$$log" || diagnostics_ok=false ;; \
		ext4-recovery) \
			grep -Fxq 'ST EXT4 RECOVERY marker cleared transaction committed appended exact truncate revoke rearm create mode hardlink unlink journal clean transactions 0 replay 0 slots 0 VFS writable remount clean resources exact' "$$log" && \
			$(PYTHON) tools/ext4_image.py inspect '$(EXT4_RECOVERY_FIXTURE)' \
				--report '$(EXT4_RECOVERY_FIXTURE).after.json' && \
			$(PYTHON) tools/ext4_kernel_read.py '$(EXT4_RECOVERY_FIXTURE)' \
				'$(TEST_BUILD_DIR)/ext4-recovery/linux-kernel' || diagnostics_ok=false ;; \
		thread-guard) \
			grep -Fq 'ST THREAD guard 0x0000000800005000' "$$log" && \
			grep -Fq '  vector=14 name=page fault' "$$log" && \
			grep -Fq '  cr2=0x0000000800005000' "$$log" && \
			grep -Fq '  page-fault bits: P=0 W=1 U=0 RSVD=0 I=0' "$$log" || \
				diagnostics_ok=false ;; \
		native) \
			grep -Eq '^OPENRFS PERF syscall iterations=1024 total_ns=[1-9][0-9]* average_ns=[1-9][0-9]*$$' "$$log" && \
			grep -Eq '^OPENRFS PERF file sequential_bytes=65536 write_ns=[1-9][0-9]* read_ns=[1-9][0-9]*$$' "$$log" && \
			grep -Eq '^OPENRFS PERF context-switch transitions=[1-9][0-9]* without_fpu_cycles=[1-9][0-9]* with_fpu_cycles=[1-9][0-9]*$$' "$$log" && \
			grep -Eq '^OPENRFS NATIVE PASS argc=[1-9][0-9]* app=NATIVET.APP$$' "$$log" && \
			grep -Fxq 'OpenRFS: native general loader, SDK, TLS, threads and FPU passed' "$$log" || \
				diagnostics_ok=false ;; \
		native-lua) \
			grep -Eq '^OPENRFS PERF lua startup_ns=[1-9][0-9]*$$' "$$log" && \
			grep -Fxq 'OPENRFS LUA INPUT READY' "$$log" && \
			grep -Fxq 'OPENRFS LUA PASS input=openrfs sum=5050' "$$log" && \
			grep -Fxq 'OpenRFS: upstream Lua used stdin, Data, math and stdout' "$$log" || \
				diagnostics_ok=false ;; \
		native-sqlite) \
			grep -Eq '^OPENRFS PERF sqlite transaction_ns=[1-9][0-9]*$$' "$$log" && \
			grep -Eq '^OPENRFS PERF sqlite reopen_query_ns=[1-9][0-9]*$$' "$$log" && \
			grep -Fxq 'OPENRFS SQLITE PHASE1 PASS rows=3 locking=busy' "$$log" && \
			grep -Fxq 'OPENRFS SQLITE PHASE2 PASS rows=3 sum=66 integrity=ok' "$$log" && \
			grep -Fxq 'OpenRFS: upstream SQLite retained and verified three rows after reboot' "$$log" || \
				diagnostics_ok=false ;; \
		native-rust) \
			grep -Fxq 'OPENRFS RUST PASS alloc file time entropy thread' "$$log" && \
			grep -Fxq 'OpenRFS: no_std Rust application used native ABI v1 services' "$$log" || \
				diagnostics_ok=false ;; \
		native-crash) \
			grep -Fxq 'OpenRFS: native crash contained; mappings handles threads windows FS x87 SSE reclaimed' "$$log" || \
				diagnostics_ok=false ;; \
		native-elf-refusal) \
			grep -Fxq 'OpenRFS: native malformed ELF refused; resource census unchanged' "$$log" || diagnostics_ok=false ;; \
		native-digest-refusal) \
			grep -Fxq 'OpenRFS: native manifest digest mismatch refused; resource census unchanged' "$$log" || diagnostics_ok=false ;; \
		native-abi-refusal) \
			grep -Fxq 'OpenRFS: native unsupported ABI version refused; resource census unchanged' "$$log" || diagnostics_ok=false ;; \
		native-relaunch) \
			grep -Fxq 'OpenRFS: native relaunch advanced generation; both resource censuses clean' "$$log" || diagnostics_ok=false ;; \
		native-audio) \
			grep -Fxq 'OPENRFS AUDIO REFUSAL PASS capability=EACCES' "$$log" && \
			grep -Fxq 'OPENRFS AUDIO PHASE open-limit-readiness PASS' "$$log" && \
			grep -Fxq 'OPENRFS AUDIO PHASE two-stream-mix-drain PASS' "$$log" && \
			grep -Fxq 'OPENRFS AUDIO PHASE cancel-terminal-readiness PASS' "$$log" && \
			grep -Fxq 'OPENRFS AUDIO PASS frames=1024 format=48000/S16LE/2 close=stale teardown=process' "$$log" && \
			grep -Fxq 'OpenRFS: native audio ABI capability, mixing, cancellation and teardown passed' "$$log" || diagnostics_ok=false; \
			if test "$$audio_capture" = true; then \
				$(PYTHON) -S tools/audio-wav-host-test.py "$$audio_wav" || diagnostics_ok=false; \
			else echo 'OPENRFS AUDIO WAV SKIP qemu wav backend unavailable'; fi ;; \
		native-sdl) \
			test -s '$(TEST_BUILD_DIR)/$*/sdl.png' && \
			test -s '$(TEST_BUILD_DIR)/$*/sdl.mp4' && \
			grep -Fxq 'OPENRFS SDL READY run=1 video=openrfs audio=openrfs pref=Data:SDL/DCDB3FF2/' "$$log" && \
			grep -Fxq 'OPENRFS SDL PASS run=1 present=partial input=key-pointer audio=non-silent persistent=yes' "$$log" && \
			grep -Fxq 'OPENRFS SDL READY run=2 video=openrfs audio=openrfs pref=Data:SDL/DCDB3FF2/' "$$log" && \
			grep -Fxq 'OPENRFS SDL PASS run=2 present=partial input=prior-run audio=non-silent persistent=yes' "$$log" && \
			grep -Fxq 'OpenRFS: SDL 2 window, input, partial damage, PCM and persistence passed' "$$log" || diagnostics_ok=false; \
			if test "$$audio_capture" = true; then \
				$(PYTHON) -S tools/audio-wav-host-test.py --profile sdl \
					"$$audio_wav" || diagnostics_ok=false; \
			else echo 'OPENRFS SDL WAV SKIP qemu wav backend unavailable'; fi ;; \
		native-dynamic) \
			grep -Eq '^OpenRFS: dynamic immutable RX shared pages [1-9][0-9]*$$' "$$log" && \
			test "$$(grep -Fxc 'OPENRFS DYNAMIC RING3 PASS' "$$log")" -eq 2 && \
			$(PYTHON) -S tools/serial-marker-order.py --count 2 "$$log" \
				'OPENRFS DYNAMIC LIB INIT' \
				'OPENRFS DYNAMIC ROOT INIT' \
				'OPENRFS DYNAMIC RING3 PASS' \
				'OPENRFS DYNAMIC ROOT FINI' \
				'OPENRFS DYNAMIC LIB FINI' && \
			test "$$(grep -Fxc \
				'OpenRFS: dynamic ELF shared RX, private TLS and lifecycle passed' \
				"$$log")" -eq 1 || \
				diagnostics_ok=false ;; \
	esac; \
	if test "$$diagnostics_ok" != true; then \
		echo 'QEMU scenario $* omitted its required diagnostic'; \
		cat "$$log"; \
		exit 1; \
	fi; \
	$(PYTHON) tools/write_qemu_result.py --scenario '$*' \
		--expected "$$expected" --observed "$$result" \
		--expected-begins "$$expected_begin" --serial "$$log" \
		--output '$(TEST_BUILD_DIR)/$*/scenario-result.json'; \
	echo 'QEMU scenario $* passed'

qemu-tests: $(TEST_TARGETS)
	@echo "all deterministic QEMU scenarios passed"

smoke: qemu-test-normal
	@echo "strict boot smoke test passed"

# The upstream driver suite: one QEMU boot per device profile, outside the
# 115-scenario matrix. See tools/run_driver_tests.py for what each requires.
DRIVER_TEST_DIR := $(TEST_BUILD_DIR)/drivers
.PHONY: qemu-test-drivers qemu-test-drivers-list run-drivers driver-provenance
# Every vendored upstream driver file still matches its pinned upstream bytes.
driver-provenance:
	cd vendor/ipxe && sha256sum --check --quiet SOURCE-MANIFEST.sha256
	cd vendor/seabios && sha256sum --check --quiet SOURCE-MANIFEST.sha256
	cd vendor/minix && sha256sum --check --quiet SOURCE-MANIFEST.sha256

qemu-test-drivers: $(KERNEL) driver-provenance
	$(PYTHON) tools/run_driver_tests.py --kernel '$(KERNEL)' \
		--output '$(DRIVER_TEST_DIR)' --qemu qemu-system-x86_64 \
		--grub-mkrescue '$(GRUB_MKRESCUE)' --accel '$(QEMU_ACCEL)' \
		$(foreach scenario,$(DRIVER_SCENARIOS),--scenario $(scenario))

qemu-test-drivers-list:
	@$(PYTHON) tools/run_driver_tests.py --kernel '$(KERNEL)' \
		--output '$(DRIVER_TEST_DIR)' --list

# An interactive boot with every compiled upstream driver enabled.
DRIVER_ISO := $(BUILD_DIR)/openrfs-drivers.iso
$(DRIVER_ISO): $(KERNEL)
	rm -rf $(BUILD_DIR)/iso-drivers
	mkdir -p $(BUILD_DIR)/iso-drivers/boot/grub
	cp $(KERNEL) $(BUILD_DIR)/iso-drivers/boot/openrfs.elf
	printf '%s\n' 'set default=0' 'set timeout=0' '' \
		'menuentry "OpenRFS (upstream drivers)" {' \
		'    multiboot2 /boot/openrfs.elf openrfs.drivers=auto' \
		'    boot' '}' >$(BUILD_DIR)/iso-drivers/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) $(GRUB_MKRESCUE_FLAGS) -o $@ $(BUILD_DIR)/iso-drivers

run-drivers: $(DRIVER_ISO) $(DESKTOP_SYSTEM_IMAGE) $(FAT32_DATA_IMAGE)
	cp $(FAT32_DATA_IMAGE) $(FAT32_RUN_DATA_IMAGE)
	qemu-system-x86_64 -m 256M -smp 1 -boot order=d -cdrom $(DRIVER_ISO) \
		-blockdev driver=file,filename=$(DESKTOP_SYSTEM_IMAGE),node-name=system-file,read-only=on,auto-read-only=off \
		-blockdev driver=raw,file=system-file,node-name=system-raw,read-only=on \
		-device nvme,serial=openrfs-system-fat32,drive=system-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 \
		-blockdev driver=file,filename=$(FAT32_RUN_DATA_IMAGE),node-name=data-file,read-only=off,auto-read-only=off \
		-blockdev driver=raw,file=data-file,node-name=data-raw,read-only=off \
		-device nvme,serial=openrfs-data-fat32,drive=data-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 \
		-nic user,model=e1000 -serial stdio -no-reboot -no-shutdown

run: iso $(DESKTOP_SYSTEM_IMAGE) $(FAT32_DATA_IMAGE)
	cp $(FAT32_DATA_IMAGE) $(FAT32_RUN_DATA_IMAGE)
	qemu-system-x86_64 -m 128M -smp 1 -boot order=d -cdrom $(ISO) \
		-blockdev driver=file,filename=$(DESKTOP_SYSTEM_IMAGE),node-name=system-file,read-only=on,auto-read-only=off \
		-blockdev driver=raw,file=system-file,node-name=system-raw,read-only=on \
		-device nvme,serial=openrfs-system-fat32,drive=system-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 \
		-blockdev driver=file,filename=$(FAT32_RUN_DATA_IMAGE),node-name=data-file,read-only=off,auto-read-only=off \
		-blockdev driver=raw,file=data-file,node-name=data-raw,read-only=off \
		-device nvme,serial=openrfs-data-fat32,drive=data-raw,logical_block_size=512,physical_block_size=512,max_ioqpairs=1,msix_qsize=1 \
		-serial stdio -no-reboot -no-shutdown

hooks:
	git config core.hooksPath .githooks
	@echo "repository hooks enabled"

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPENDENCIES)
