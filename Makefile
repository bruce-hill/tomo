SHELL=bash -o pipefail
# Run ./configure.sh to choose installation locations:
ifeq ($(wildcard config.mk),)
all: config.mk
	$(MAKE) all
install: config.mk
	$(MAKE) install
install-files: config.mk
	$(MAKE) install-files
install-lib: config.mk
	$(MAKE) install-lib
test: config.mk
	$(MAKE) test
dist: config.mk
	$(MAKE) dist
archive: config.mk
	$(MAKE) archive
config.mk: configure.sh
	bash ./configure.sh
else

include config.mk

# Pinned Zig version, per-platform checksums, and the platform->musl-triple map:
include vendor/zig-checksums.mk

# Keep the build hermetic: don't let the user's ambient include/library search
# paths leak non-musl (e.g. system glibc) headers into the zig cc builds.
unexport C_INCLUDE_PATH
unexport CPLUS_INCLUDE_PATH
unexport CPATH
unexport LIBRARY_PATH
unexport LD_LIBRARY_PATH

# Modified progress counter based on: https://stackoverflow.com/a/35320895
ifndef NO_PROGRESS
ifndef ECHO
T := $(shell $(MAKE) ECHO="COUNTTHIS" $(MAKECMDGOALS) --no-print-directory \
      -n | grep -c "COUNTTHIS")
N := x
C = $(words $N)$(eval N := x $N)
ECHO = echo -e "[`expr $C '*' 100 / $T`%]"
endif
endif
ifndef ECHO
ECHO = echo
endif
# End of progress counter

# The Tomo compiler is built as a fully static executable (musl libc) using
# `zig cc`. The build targets a single platform at a time:
#   ZIG_PLATFORM  a Zig platform key, e.g. "x86_64-linux" (the host by default)
#   ZIG_TARGET    the musl compile triple, e.g. "x86_64-linux-musl"
#   BUILD_BASE    where this platform's artifacts live: build/<platform>/
# `make dist` re-invokes the build once per platform in the distribution matrix.
# CC is the host `zig cc` used as the (cross-)compiler. DEFAULT_C_COMPILER is
# unused now that the installed tomo invokes its bundled zig.
# CC is a make built-in (default "cc"), so `?=` won't override it; replace only
# the built-in default (config.mk / command line / env still win).
ifneq ($(filter undefined default,$(origin CC)),)
CC=zig cc
endif
# Use zig's llvm-based ar/ranlib: unlike the host's GNU binutils they can create
# and index archives in any target object format (notably Mach-O for macOS).
# Set these unless the caller provided them (command line / env / config.mk). AR
# has a make built-in default (origin "default"); RANLIB usually has none (origin
# "undefined") -- cover both so we never pass an empty RANLIB downstream.
ifneq ($(filter undefined default,$(origin AR)),)
AR=zig ar
endif
ifneq ($(filter undefined default,$(origin RANLIB)),)
RANLIB=zig ranlib
endif
ZIG_PLATFORM?=$(shell uname -m)-linux
ZIG_TARGET?=$(call zig_target,$(ZIG_PLATFORM))
ZIG_OS=$(call zig_os,$(ZIG_PLATFORM))
BUILD_BASE=build/$(ZIG_PLATFORM)
ifneq ($(ZIG_TARGET),)
	TARGET_FLAG=-target $(ZIG_TARGET)
endif
# Linux/musl links fully statically; macOS and the BSDs require a dynamic libc,
# so there we bundle the vendored libraries statically but link libc dynamically.
ifneq ($(call zig_is_static,$(ZIG_PLATFORM)),)
	STATIC_FLAG=-static
endif
CCONFIG=$(TARGET_FLAG) -std=c2x -fPIC \
		-fno-signed-zeros -fno-trapping-math \
		-fvisibility=hidden -fdollars-in-identifiers \
		-DGC_THREADS
LDFLAGS=$(STATIC_FLAG) $(TARGET_FLAG)
# Use the vendored (musl-built) headers rather than any system-installed copies,
# so the compiler is built against the same libraries it links against. These are
# included with -isystem so that warnings from third-party headers (e.g. gc.h
# testing __GLIBC__ under -Wundef) don't clutter the build.
INCLUDE_DIRS=-isystem $(BUILD_BASE)/gc/include -isystem $(BUILD_BASE)/gmp/include -isystem $(BUILD_BASE)/unistring/include
CWARN=-Wall -Wextra -Wno-format -Wno-format-security -Wshadow \
	  -Wno-pedantic \
	  -Wno-pointer-arith \
	  -Wtype-limits -Wunused-result -Wnull-dereference \
	  -Walloca -Wcast-align \
	  -Wdangling-else -Wdate-time -Wdisabled-optimization -Wdouble-promotion \
	  -Wexpansion-to-defined -Wno-float-equal \
	  -Wframe-address -Winline -Winvalid-pch \
	  -Wmissing-format-attribute -Wmissing-include-dirs -Wmissing-noreturn \
	  -Wno-missing-field-initializers \
	  -Wnull-dereference -Woverlength-strings -Wpacked \
	  -Wredundant-decls -Wshadow \
	  -Wno-stack-protector -Wswitch-default \
	  -Wundef -Wunused -Wunused-but-set-variable \
	  -Wunused-const-variable -Wunused-local-typedefs -Wunused-macros -Wvariadic-macros \
	  -Wwrite-strings

ifeq ($(SUDO),)
ifeq ($(shell command -v doas 2>/dev/null),)
	SUDO=sudo
else
	SUDO=doas
endif
endif

OWNER=$(shell ls -ld '$(PREFIX)' | awk '{print $$3}')

ifeq ($(shell $(CC) -v 2>&1 | grep -c "gcc version"), 1)
	CWARN += -Werror -Wsign-conversion -Walloc-zero -Wduplicated-branches -Wduplicated-cond -Wjump-misses-init \
			 -Wlogical-op -Wpacked-not-aligned -Wshadow=compatible-local -Wshadow=global -Wshadow=local \
			 -Wsuggest-attribute=const -Wsuggest-attribute=noreturn -Wsuggest-attribute=pure \
			 -Wsync-nand -Wtrampolines -Wvector-operation-performance -Wcast-align=strict
	CCONFIG += -fsanitize=signed-integer-overflow -fno-sanitize-recover -fno-signaling-nans -fno-finite-math-only 
endif

OS := $(shell uname -s)

# Feature-test macros for the *target* OS (not the build host), since we may be
# cross-compiling: Linux/glibc/musl wants _GNU_SOURCE; macOS and the BSDs want
# _BSD_SOURCE.
OSFLAGS = $(if $(filter linux,$(ZIG_OS)),-D_GNU_SOURCE,-D_BSD_SOURCE)
EXTRA=
G=-ggdb
O=-O3
# Note: older versions of Make have buggy behavior with hash marks inside strings, so this ugly code is necessary:
TOMO_VERSION=$(shell awk 'BEGIN{hashes=sprintf("%c%c",35,35)} $$1==hashes {print $$2; exit}' CHANGES.md)
GIT_VERSION=$(shell git log -1 --pretty=format:"%as_%h" 2>/dev/null || echo "unknown")
CFLAGS+=$(CCONFIG) $(INCLUDE_DIRS) $(EXTRA) $(CWARN) $(G) $(O) $(OSFLAGS) \
	   -DSUDO='"$(SUDO)"' \
	   -DZIG_TARGET='"$(ZIG_TARGET)"' \
	   -DGIT_VERSION='"$(GIT_VERSION)"' -ffunction-sections -fdata-sections \
	   -UNDEBUG # `zig cc` defines NDEBUG at -O, but the code relies on active assert()s
CFLAGS_PLACEHOLDER="$$(printf '\033[2m<flags...>\033[m\n')" 
# Per-OS stack-trace support: musl (Linux) has no <execinfo.h>, so stacktrace.c
# falls back to the unwinder (-lunwind, bundled by zig); the BSDs provide
# backtrace() via libexecinfo (-lexecinfo); macOS provides it in libSystem.
LDLIBS=-lm
ifneq ($(call zig_is_static,$(ZIG_PLATFORM)),)
	LDLIBS += -lunwind
endif
ifneq ($(filter freebsd netbsd openbsd,$(ZIG_OS)),)
	LDLIBS += -lexecinfo
endif

AR_FILE=libtomo@$(TOMO_VERSION).a
ifeq ($(OS),Darwin)
	INCLUDE_DIRS += -I/opt/homebrew/include
	LDFLAGS += -L/opt/homebrew/lib -Wl,-w
endif
EXE_FILE=tomo@$(TOMO_VERSION)

# Object files are compiled for a specific target platform, so they live in a
# per-platform directory. This keeps every platform's dependency graph fully
# independent and incremental: switching ZIG_PLATFORM never invalidates (or
# clobbers) another platform's objects.
OBJ_DIR=$(BUILD_BASE)/obj
COMPILER_OBJS=$(patsubst %.c,$(OBJ_DIR)/%.o,$(wildcard src/*.c src/compile/*.c src/parse/*.c src/formatter/*.c))
STDLIB_OBJS=$(patsubst %.c,$(OBJ_DIR)/%.o,$(wildcard src/stdlib/*.c))
TESTS=$(patsubst test/%.tm,test/results/%.tm.testresult,$(wildcard test/[!_]*.tm))
API_YAML=$(wildcard api/*.yaml)
API_MD=$(patsubst %.yaml,%.md,$(API_YAML))

all: config.mk check-c-compiler build test/api.tm
	@$(ECHO) "All done!"

# The set of platforms `make dist` builds distribution archives for. Defaults to
# the Linux/musl, macOS, and BSD targets (see vendor/zig-checksums.mk). Linux
# links a fully static musl libc; macOS and the BSDs link libc dynamically but
# bundle the vendored libraries statically. Windows is excluded (Tomo's runtime
# needs POSIX facilities Windows lacks).
DIST_TARGETS ?= $(ZIG_DIST_PLATFORMS)
DIST_DIR = build/dist

# Build distribution archives for every platform in DIST_TARGETS. Each platform
# builds into its own build/<platform>/ tree (including its own obj/ directory),
# so every sub-make is fully incremental: an up-to-date platform does no work and
# its archive isn't re-tarred. A failing platform doesn't abort the others;
# failures are collected and reported at the end (and the overall exit status
# reflects them). The archive rule itself lives below, next to `build`.
dist:
	@failed=""; \
	for p in $(DIST_TARGETS); do \
	    printf '\033[1;7m Distribution for %s \033[m\n' "$$p"; \
	    $(MAKE) --no-print-directory archive ZIG_PLATFORM="$$p" NO_PROGRESS=1 \
	        || { printf '\033[91;1mFAILED: %s\033[m\n' "$$p"; failed="$$failed $$p"; }; \
	done; \
	printf '\033[1;7m Distribution archives in %s: \033[m\n' "$(DIST_DIR)"; \
	ls -1 $(DIST_DIR)/*.tar.xz 2>/dev/null; \
	if [ -n "$$failed" ]; then \
	    printf '\033[91;1;7m Failed platforms:%s \033[m\n' "$$failed"; \
	    exit 1; \
	fi

BUILD_DIR=$(BUILD_BASE)/tomo@$(TOMO_VERSION)
headers := $(wildcard src/stdlib/*.h)
build_headers := $(patsubst src/stdlib/%.h, $(BUILD_DIR)/include/tomo@$(TOMO_VERSION)/%.h, $(headers))

# generate corresponding build paths with .gz
build_manpages := $(patsubst %,$(BUILD_DIR)/%.gz,$(wildcard man/man*/*))

# Ensure directories exist
dirs := $(BUILD_DIR)/include/tomo@$(TOMO_VERSION) \
        $(BUILD_DIR)/lib \
        $(BUILD_DIR)/lib/tomo@$(TOMO_VERSION) \
        $(BUILD_DIR)/bin \
        $(BUILD_DIR)/man/man1 \
        $(BUILD_DIR)/man/man3 \
        $(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)

$(dirs):
	mkdir -p $@

# Rule for copying headers
$(BUILD_DIR)/include/tomo@$(TOMO_VERSION)%.h: src/stdlib/%.h | $(BUILD_DIR)/include/tomo@$(TOMO_VERSION)
	cp $< $@

# Install the vendored library headers (gc.h, gmp.h, libunistring's headers)
# alongside Tomo's own headers, so that programs compiled by tomo can find them.
# The system copies of these are no longer used, since the vendored versions are
# musl builds matching the static libraries linked into libtomo.
$(BUILD_DIR)/include/gc.h: $(BUILD_BASE)/gc/lib/libgc.a $(BUILD_BASE)/gmp/lib/libgmp.a $(BUILD_BASE)/unistring/lib/libunistring.a | $(BUILD_DIR)/include/tomo@$(TOMO_VERSION)
	cp -R $(BUILD_BASE)/gc/include/. $(BUILD_DIR)/include/
	cp -R $(BUILD_BASE)/gmp/include/. $(BUILD_DIR)/include/
	cp -R $(BUILD_BASE)/unistring/include/. $(BUILD_DIR)/include/

# Rule for gzipping man pages
$(BUILD_DIR)/man/%.gz: man/% | $(BUILD_DIR)/man/man1 $(BUILD_DIR)/man/man3
	gzip -c $< > $@

$(BUILD_DIR)/bin/tomo: $(BUILD_DIR)/bin/tomo@$(TOMO_VERSION) | $(BUILD_DIR)/bin
	ln -sf tomo@$(TOMO_VERSION) $@

$(BUILD_DIR)/bin/$(EXE_FILE): $(STDLIB_OBJS) $(COMPILER_OBJS) $(BUILD_BASE)/gc/lib/libgc.a $(BUILD_BASE)/gmp/lib/libgmp.a $(BUILD_BASE)/unistring/lib/libunistring.a | $(BUILD_DIR)/bin deps
	@$(ECHO) $(CC) $(CFLAGS_PLACEHOLDER) $(LDFLAGS) $(LDLIBS) $^ -o $@
	@$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) $^ -o $@

# Combine the stdlib objects and the vendored static libraries into a single
# relocatable object, then archive it. -no-pie is ELF-only (Linux); on Mach-O
# (macOS) it isn't accepted, so it's applied only for static/Linux targets.
NOPIE_FLAG=$(if $(call zig_is_static,$(ZIG_PLATFORM)),-no-pie,)
$(BUILD_DIR)/lib/$(AR_FILE): $(STDLIB_OBJS) $(BUILD_BASE)/gc/lib/libgc.a $(BUILD_BASE)/unistring/lib/libunistring.a $(BUILD_BASE)/gmp/lib/libgmp.a | $(BUILD_DIR)/lib
	$(CC) $(TARGET_FLAG) $(NOPIE_FLAG) -r -nostdlib $^ -o libtomo.o
	$(AR) rcs $@ libtomo.o
	rm -f libtomo.o

$(BUILD_DIR)/lib/tomo@$(TOMO_VERSION)/packages.ini: packages.ini | $(BUILD_DIR)/lib/tomo@$(TOMO_VERSION)
	@cp $^ $@

$(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)/LICENSE.md: LICENSE.md | $(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)
	cp $< $@

# --- Bundled Zig toolchain ------------------------------------------------
# The installed tomo always invokes a Zig toolchain bundled inside the install,
# so the distribution is self-contained (no system compiler needed to build and
# run Tomo programs). Zig is a native build for the target platform; on a cross
# build for a distribution, `make dist` fetches that platform's Zig to ship.
ZIG_STAGED = $(BUILD_BASE)/zig/zig
ZIG_BUNDLE_DIR = $(BUILD_DIR)/libexec/tomo@$(TOMO_VERSION)/zig

# Download + checksum-verify + extract the pinned Zig for this platform:
$(ZIG_STAGED):
	$(MAKE) -C vendor zig CC='$(CC)' ZIG_PLATFORM='$(ZIG_PLATFORM)' ZIG_TARGET='$(ZIG_TARGET)' BUILD_BASE='$(CURDIR)/$(BUILD_BASE)'

# Copy the Zig toolchain (binary + lib/ + LICENSE) into the install tree:
$(ZIG_BUNDLE_DIR)/zig: $(ZIG_STAGED)
	rm -rf $(ZIG_BUNDLE_DIR)
	mkdir -p $(ZIG_BUNDLE_DIR)
	cp -R $(BUILD_BASE)/zig/. $(ZIG_BUNDLE_DIR)/

# Ship Zig's license alongside Tomo's (Zig is MIT-licensed):
$(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)/ZIG-LICENSE: $(ZIG_STAGED) | $(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)
	cp $(BUILD_BASE)/zig/LICENSE $@
# --------------------------------------------------------------------------

# Everything that makes up an installed Tomo tree for the current platform:
BUILD_PRODUCTS = $(BUILD_DIR)/bin/tomo $(BUILD_DIR)/bin/tomo@$(TOMO_VERSION) \
	$(BUILD_DIR)/lib/$(AR_FILE) $(BUILD_DIR)/lib/tomo@$(TOMO_VERSION)/packages.ini \
	$(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)/LICENSE.md $(build_headers) $(build_manpages) \
	$(BUILD_DIR)/include/gc.h \
	$(ZIG_BUNDLE_DIR)/zig $(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)/ZIG-LICENSE

build: $(BUILD_PRODUCTS)

# The distribution archive for the current platform: a .tar.xz of the install
# tree whose contents (bin/, lib/, include/, libexec/, ...) extract directly
# into an install prefix, e.g.:
#   tar xf build/dist/tomo@VERSION-x86_64-linux.tar.xz -C /usr/local
# It depends on the build products, so it is only re-tarred when something in
# the install tree was actually rebuilt.
DIST_ARCHIVE = $(DIST_DIR)/tomo@$(TOMO_VERSION)-$(ZIG_PLATFORM).tar.xz
$(DIST_ARCHIVE): $(BUILD_PRODUCTS)
	@mkdir -p $(DIST_DIR)
	tar cJf $@ -C $(BUILD_DIR) .
	@printf 'Wrote \033[1m%s\033[m\n' "$@"

# Build the distribution archive for the current platform only:
archive: $(DIST_ARCHIVE)

version:
	@echo $(TOMO_VERSION)

check-c-compiler:
	@$(CC) -v 2>/dev/null >/dev/null \
		|| { printf '\033[91;1m%s\033[m\n' "You have set your build compiler (CC) to '$(CC)' in your config.mk, but I can't run it!"; exit 1; }

# check-libs: check-c-compiler | deps
# 	@echo 'int main() { return 0; }' | $(DEFAULT_C_COMPILER) $(LDFLAGS) -x c - $(LDLIBS) -o /dev/null 2>/dev/null >/dev/null \
# 		|| { printf '\033[91;1m%s\033[m\n' "I expected to find the following libraries on your system, but I can't find them: $(LDLIBS)"; exit 1; }

tags:
	ctags src/*.{c,h} src/stdlib/*.{c,h} src/compile/*.{c,h} src/parse/*.{c,h} src/formatter/*.{c,h}

config.mk: configure.sh
	bash ./configure.sh

$(OBJ_DIR)/%.o: %.c src/ast.h src/environment.h src/types.h config.mk | deps
	@mkdir -p $(dir $@)
	@$(ECHO) $(CC) $(CFLAGS_PLACEHOLDER) -c $< -o $@
	@$(CC) $(CFLAGS) -c $< -o $@

# Integer implementations depend on the shared header:
$(OBJ_DIR)/src/stdlib/int64.o $(OBJ_DIR)/src/stdlib/int32.o $(OBJ_DIR)/src/stdlib/int16.o $(OBJ_DIR)/src/stdlib/int8.o: src/stdlib/intX.c.h src/stdlib/intX.h

# Num implementations depend on the shared header:
$(OBJ_DIR)/src/stdlib/num32.o $(OBJ_DIR)/src/stdlib/num64.o: src/stdlib/numX.c.h

%: %.tm
	./local-tomo -e $<

test/results/%.tm.testresult: test/%.tm build
	@mkdir -p test/results
	@printf '\033[93;1;4m%s\033[m\n' $<
	@if ! COLOR=1 LC_ALL=C ./local-tomo -O 1 $< 2>&1 | tee $@; then \
		rm -f $@; \
		false; \
	fi

test: $(TESTS)
	@printf '\033[92;7m ALL TESTS PASSED! \033[m\n'

# Remove just the (target-specific) Tomo object files:
# Remove the compiled Tomo object files (all platforms):
clean-obj:
	rm -rf build/*/obj

clean: clean-obj
	rm -rf build/*/tomo@*/{bin,lib,libexec} test/*.tm.testresult test/.build lib/*/.build examples/.build examples/*/.build

%: %.md
	pandoc --lua-filter=docs/.pandoc/bold-code.lua -s $< -t man -o $@

%.md: %.yaml scripts/api_gen.py
	./scripts/api_gen.py $< >$@

api/api.md: $(API_YAML)
	./scripts/api_gen.py $^ >$@

test/api.tm: $(API_YAML) | ./scripts/api_tests.py
	./scripts/api_tests.py $^ >$@

.PHONY: api-docs
api-docs: $(API_MD) api/api.md

.PHONY: manpages
manpages: $(API_YAML) man/man1/tomo.1
	./scripts/mandoc_gen.py $(API_YAML)

man/man1/tomo.1: docs/tomo.1.md
	pandoc --lua-filter=docs/.pandoc/bold-code.lua -s $< -t man -o $@

examples:
	./local-tomo examples/learnxiny.tm

core-libs:
	./local-tomo -L packages/core.ini

deps: $(BUILD_BASE)/gc/lib/libgc.a $(BUILD_BASE)/unistring/lib/libunistring.a $(BUILD_BASE)/gmp/lib/libgmp.a

$(BUILD_BASE)/gc/lib/libgc.a $(BUILD_BASE)/unistring/lib/libunistring.a $(BUILD_BASE)/gmp/lib/libgmp.a:
	$(MAKE) -C vendor CC='$(CC)' AR='$(AR)' RANLIB='$(RANLIB)' ZIG_PLATFORM='$(ZIG_PLATFORM)' ZIG_TARGET='$(ZIG_TARGET)' BUILD_BASE='$(CURDIR)/$(BUILD_BASE)'

install-files: build check-c-compiler
	@if ! echo "$$PATH" | tr ':' '\n' | grep -qx "$(PREFIX)/bin"; then \
		echo $$PATH; \
		printf "\033[91;1mError: '$(PREFIX)/bin' is not in your \$$PATH variable!\033[m\n" >&2; \
		printf "\033[91;1mSpecify a different prefix with 'make PREFIX=... install'\033[m\n" >&2; \
		printf "\033[91;1mor add the following line to your .profile:\033[m\n" >&2; \
		printf "\n\033[1mexport PATH=\"$(PREFIX):\$$PATH\"\033[m\n\n" >&2; \
		exit 1; \
	fi
	if ! [ -w "$(PREFIX)" ]; then \
		$(SUDO) -u $(OWNER) $(MAKE) install-files; \
		exit 0; \
	fi; \
	cp -R $(BUILD_DIR)/* $(PREFIX)/

install: install-files

uninstall:
	if ! [ -w "$(PREFIX)" ]; then \
		$(SUDO) -u $(OWNER) $(MAKE) uninstall; \
		exit 0; \
	fi; \
	rm -rvf "$(PREFIX)/bin/tomo" "$(PREFIX)/bin/tomo"* "$(PREFIX)/include/tomo"* \
		"$(PREFIX)/lib/libtomo@"* "$(PREFIX)/lib/tomo@"* "$(PREFIX)/share/licenses/tomo@"* \
		~/.local/tomo/state/tomo@$(TOMO_VERSION); \

endif

.SUFFIXES:
.PHONY: all build clean clean-obj dist archive install install-files uninstall test tags core-libs examples deps check-c-compiler version
