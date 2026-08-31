SHELL=bash -o pipefail

# config.mk records the choices made by ./configure.sh. If it's missing or older
# than configure.sh, make runs the rule below to (re)generate it and then
# automatically restarts itself with the fresh file included. This rule is the
# first in the file, so the default goal must be set explicitly:
.DEFAULT_GOAL := all
-include config.mk
config.mk: configure.sh
	bash ./configure.sh

# Pinned Zig version, per-platform checksums, and the platform->musl-triple map:
include vendor/zig-checksums.mk
# Pinned versions of the vendored libraries (embedded into compiled binaries):
include vendor/versions.mk

# Keep the build hermetic: don't let the user's ambient include/library search
# paths leak non-musl (e.g. system glibc) headers into the zig cc builds.
unexport C_INCLUDE_PATH
unexport CPLUS_INCLUDE_PATH
unexport CPATH
unexport LIBRARY_PATH
unexport LD_LIBRARY_PATH

# Modified progress counter based on: https://stackoverflow.com/a/35320895
# The counter dry-runs the goal to count its steps. `make -n` still *executes*
# recipe lines that invoke $(MAKE), so for goals built out of recursive makes
# (dist/archive/deps) the "dry" run would recurse into vendor builds and try to
# enter directories that don't exist yet, spraying errors, so skip those goals
# and silence the counting run's stderr. Skipped before configure.sh has run:
# the counting sub-make would otherwise try to remake config.mk (an interactive
# prompt) inside $(shell); once config.mk is generated, make restarts and the
# counter works normally.
ifneq ($(wildcard config.mk),)
ifndef NO_PROGRESS
ifndef ECHO
ifeq ($(filter dist archive deps install-targets,$(MAKECMDGOALS)),)
T := $(shell $(MAKE) ECHO="COUNTTHIS" $(MAKECMDGOALS) --no-print-directory \
      -n 2>/dev/null | grep -c "COUNTTHIS")
N := x
C = $(words $N)$(eval N := x $N)
ECHO = echo -e "[`expr $C '*' 100 / $T`%]"
endif
endif
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
#
# The toolchain is not configurable: Tomo requires Zig. `zig cc` is the only
# supported (cross-)compiler, and zig's llvm-based ar/ranlib are used because,
# unlike the host's GNU binutils, they can create and index archives in any
# target object format (notably Mach-O for macOS).
CC=zig cc
AR=zig ar
RANLIB=zig ranlib
ZIG_PLATFORM?=$(ZIG_HOST_PLATFORM)
ZIG_TARGET?=$(call zig_target,$(ZIG_PLATFORM))
ZIG_OS=$(call zig_os,$(ZIG_PLATFORM))
BUILD_BASE=build/$(ZIG_PLATFORM)
# Static libraries + license texts produced by the vendor build (see
# vendor/Makefile, which pins a version and SHA-256 checksum for each). Each
# vendored dependency "foo" installs into $(BUILD_BASE)/foo/{lib,include}:
VENDOR_DEPS=gc gmp unistring backtrace miniz
VENDORED_LIBS=$(foreach d,$(VENDOR_DEPS),$(BUILD_BASE)/$(d)/lib/lib$(d).a)
VENDOR_LICENSES=$(BUILD_BASE)/gc/LICENSE $(BUILD_BASE)/gmp/COPYING.LESSERv3 $(BUILD_BASE)/gmp/COPYINGv2 $(BUILD_BASE)/unistring/COPYING.LIB $(BUILD_BASE)/backtrace/LICENSE $(BUILD_BASE)/miniz/LICENSE
ifneq ($(ZIG_TARGET),)
	TARGET_FLAG=-target $(ZIG_TARGET)
endif
# Linux/musl links fully statically; macOS and the BSDs require a dynamic libc,
# so there we bundle the vendored libraries statically but link libc dynamically.
ifneq ($(call zig_is_static,$(ZIG_PLATFORM)),)
	STATIC_FLAG=-static
endif
CCONFIG=$(TARGET_FLAG) -std=gnu23 -fPIC \
		-fno-signed-zeros -fno-trapping-math \
		-fvisibility=hidden -fdollars-in-identifiers \
		-DGC_THREADS
LDFLAGS=$(STATIC_FLAG) $(TARGET_FLAG)
# Use the vendored (musl-built) headers rather than any system-installed copies,
# so the compiler is built against the same libraries it links against. These are
# included with -isystem so that warnings from third-party headers (e.g. gc.h
# testing __GLIBC__ under -Wundef) don't clutter the build.
INCLUDE_DIRS=$(foreach d,$(VENDOR_DEPS),-isystem $(BUILD_BASE)/$(d)/include)
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

OS := $(shell uname -s)

EXTRA=
# No debug info in libtomo. A Tomo stacktrace names the .tm file, function, and
# line from the *generated* program's own DWARF, which each program compiles
# for itself; libtomo's would only ever name libtomo's own frames, which a
# stacktrace doesn't show. Emitting it cost every compiled program ~340KB. Set
# G=-ggdb to get it back for stepping through the runtime or the compiler.
G=-g0
O=-O3
# Note: older versions of Make have buggy behavior with hash marks inside strings, so this ugly code is necessary:
TOMO_VERSION=$(shell awk 'BEGIN{hashes=sprintf("%c%c",35,35)} $$1==hashes {print $$2; exit}' CHANGES.md)
GIT_VERSION=$(shell git log -1 --pretty=format:"%as_%h" 2>/dev/null || echo "unknown")
CFLAGS+=$(CCONFIG) $(INCLUDE_DIRS) $(EXTRA) $(CWARN) $(G) $(O) \
	   -DSUDO='"$(SUDO)"' \
	   -DZIG_TARGET='"$(ZIG_TARGET)"' \
	   -DTOMO_PLATFORM='"$(ZIG_PLATFORM)"' \
	   -DTOMO_DIST_PLATFORMS='"$(ZIG_DIST_PLATFORMS)"' \
	   -DGIT_VERSION='"$(GIT_VERSION)"' \
	   -DGC_VERSION='"$(GC_VERSION)"' -DGMP_VERSION='"$(GMP_VERSION)"' \
	   -DUNISTRING_VERSION='"$(UNISTRING_VERSION)"' \
	   -DLIBBACKTRACE_VERSION='"$(LIBBACKTRACE_VERSION)"' \
	   -DMINIZ_VERSION='"$(MINIZ_VERSION)"' \
	   -DZIG_VERSION='"$(ZIG_VERSION)"' \
	   -DMINIZ_NO_TIME \
	   -ffunction-sections -fdata-sections \
	   -UNDEBUG # `zig cc` defines NDEBUG at -O, but the code relies on active assert()s
# Emit a .d makefile fragment per object recording the headers it actually
# includes (-MMD), with phony targets so deleted headers don't break the build
# (-MP). The fragments are -include'd next to the object pattern rule below.
CFLAGS += -MMD -MP
CFLAGS_PLACEHOLDER="$$(printf '\033[2m<flags...>\033[m\n')" 
# Stack traces collect addresses with the compiler's unwinder (-lunwind, which
# zig provides for every target) on all platforms:
LDLIBS=-lm -lunwind

# Everything installed lives inside a versioned directory (lib/tomo@VER/,
# include/tomo@VER/, ...) so multiple Tomo versions can coexist in one prefix:
AR_FILE=tomo@$(TOMO_VERSION)/libtomo.a
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
COMPILER_OBJS=$(patsubst %.c,$(OBJ_DIR)/%.o,$(wildcard src/*.c src/cmd/*.c src/compile/*.c src/parse/*.c src/formatter/*.c))
STDLIB_OBJS=$(patsubst %.c,$(OBJ_DIR)/%.o,$(wildcard src/stdlib/*.c)) $(OBJ_DIR)/versions.o
# One `tomo test` invocation over every test file: it builds and runs them in
# parallel itself (see --jobs), so make no longer forks per file.
TEST_FILES=$(sort $(wildcard test/[!_]*.tm) test/api.tm)
TEST_LOG=test/results/all.testresult
API_YAML=$(wildcard api/*.yaml)
API_MD=$(patsubst %.yaml,%.md,$(API_YAML))

all: config.mk check-zig build test/api.tm
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

# Install the DIST_TARGETS platforms' libraries straight from the local build
# trees into the XDG data directory, where `tomo --target <platform>` looks for
# them, so cross-compilation can be tested without any network access. The
# host platform is skipped (native builds don't use a target pack). Limit the
# set with e.g. `make install-targets DIST_TARGETS=aarch64-linux`.
XDG_DATA_HOME ?= $(HOME)/.local/share
TARGET_PACKS_DIR = $(XDG_DATA_HOME)/tomo/tomo@$(TOMO_VERSION)/targets
install-targets:
	@for p in $(filter-out $(ZIG_HOST_PLATFORM),$(DIST_TARGETS)); do \
	    printf '\033[1;7m Target platform %s \033[m\n' "$$p"; \
	    $(MAKE) --no-print-directory build ZIG_PLATFORM="$$p" NO_PROGRESS=1 || exit 1; \
	    mkdir -p '$(TARGET_PACKS_DIR)'/$$p; \
	    cp -R build/$$p/tomo/lib build/$$p/tomo/include '$(TARGET_PACKS_DIR)'/$$p/; \
	    printf 'Installed \033[1m%s\033[m\n' '$(TARGET_PACKS_DIR)'/$$p; \
	done

BUILD_DIR=$(BUILD_BASE)/tomo
# Tomo's stdlib headers install under include/tomo@VER/tomo/, with the
# umbrella tomo.h at include/tomo@VER/tomo.h, and compiled programs get
# -I PREFIX/include/tomo@VER, so they say `#include <tomo.h>`. (The headers
# can't live flat at the -I root: some share names with libc headers, like
# stdlib.h, and would shadow them.) The datatypes/ subdirectory comes along with
# them, keeping the same shape it has in the source tree, since datatypes.h
# includes its members by that relative path.
headers := $(filter-out src/stdlib/tomo.h,$(wildcard src/stdlib/*.h)) $(wildcard src/stdlib/datatypes/*.h)
build_headers := $(patsubst src/stdlib/%.h, $(BUILD_DIR)/include/tomo@$(TOMO_VERSION)/tomo/%.h, $(headers)) \
	$(BUILD_DIR)/include/tomo@$(TOMO_VERSION)/tomo.h

# Tomo's own man pages live in a versioned store (man/tomo@VER/man1/...), with
# symlinks at the man-visible paths (man/man1/tomo.1.gz -> ../tomo@VER/man1/...),
# the same last-install-wins scheme as the bin/tomo -> bin/tomo@VER symlink:
manpages := $(wildcard man/man*/*)
build_manpages := $(patsubst man/%,$(BUILD_DIR)/man/tomo@$(TOMO_VERSION)/%.gz,$(manpages)) \
	$(patsubst man/%,$(BUILD_DIR)/man/%.gz,$(manpages))

# Ensure directories exist
dirs := $(BUILD_DIR)/include/tomo@$(TOMO_VERSION)/tomo \
        $(BUILD_DIR)/include/tomo@$(TOMO_VERSION)/tomo/datatypes \
        $(BUILD_DIR)/lib \
        $(BUILD_DIR)/lib/tomo@$(TOMO_VERSION) \
        $(BUILD_DIR)/bin \
        $(BUILD_DIR)/man/man1 \
        $(BUILD_DIR)/man/man3 \
        $(BUILD_DIR)/man/tomo@$(TOMO_VERSION)/man1 \
        $(BUILD_DIR)/man/tomo@$(TOMO_VERSION)/man3 \
        $(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)

$(dirs):
	mkdir -p $@

# Rule for copying headers
$(BUILD_DIR)/include/tomo@$(TOMO_VERSION)/tomo/%.h: src/stdlib/%.h | $(BUILD_DIR)/include/tomo@$(TOMO_VERSION)/tomo $(BUILD_DIR)/include/tomo@$(TOMO_VERSION)/tomo/datatypes
	cp $< $@

# The umbrella header: in the (flat) source tree it includes its siblings as
# `#include "bool.h"`, but installed they live in the tomo/ subdirectory
# next to it, so rewrite the quoted includes on the way in:
$(BUILD_DIR)/include/tomo@$(TOMO_VERSION)/tomo.h: src/stdlib/tomo.h | $(BUILD_DIR)/include/tomo@$(TOMO_VERSION)/tomo
	sed 's|#include "|#include "tomo/|' $< > $@

# Install the vendored library headers (gc.h, gmp.h, libunistring's headers)
# alongside Tomo's own headers, so that programs compiled by tomo can find them.
# The system copies of these are no longer used, since the vendored versions are
# musl builds matching the static libraries linked into libtomo.
$(BUILD_DIR)/include/tomo@$(TOMO_VERSION)/gc.h: $(VENDORED_LIBS) | $(BUILD_DIR)/include/tomo@$(TOMO_VERSION)/tomo
	cp -R $(BUILD_BASE)/gc/include/. $(BUILD_DIR)/include/tomo@$(TOMO_VERSION)/
	cp -R $(BUILD_BASE)/gmp/include/. $(BUILD_DIR)/include/tomo@$(TOMO_VERSION)/
	cp -R $(BUILD_BASE)/unistring/include/. $(BUILD_DIR)/include/tomo@$(TOMO_VERSION)/

# Gzip man pages into the versioned store:
$(BUILD_DIR)/man/tomo@$(TOMO_VERSION)/%.gz: man/% | $(BUILD_DIR)/man/tomo@$(TOMO_VERSION)/man1 $(BUILD_DIR)/man/tomo@$(TOMO_VERSION)/man3
	gzip -c $< > $@

# ...and symlink them from the paths man actually searches:
$(BUILD_DIR)/man/%.gz: $(BUILD_DIR)/man/tomo@$(TOMO_VERSION)/%.gz | $(BUILD_DIR)/man/man1 $(BUILD_DIR)/man/man3
	ln -sf ../tomo@$(TOMO_VERSION)/$*.gz $@

$(BUILD_DIR)/bin/tomo: $(BUILD_DIR)/bin/tomo@$(TOMO_VERSION) | $(BUILD_DIR)/bin
	ln -sf tomo@$(TOMO_VERSION) $@

$(BUILD_DIR)/bin/$(EXE_FILE): $(STDLIB_OBJS) $(COMPILER_OBJS) $(VENDORED_LIBS) | $(BUILD_DIR)/bin deps
	@$(ECHO) $(CC) $(CFLAGS_PLACEHOLDER) $(LDFLAGS) $(LDLIBS) $^ -o $@
	@$(CC) $(CFLAGS) $(LDFLAGS) $(LDLIBS) $^ -o $@

# Combine the stdlib objects into a single relocatable object, then archive
# it. The vendored libraries are NOT merged in: tomo links them from the
# installed vendor/ directory (see below), so that packages using their full
# APIs (e.g. `use -lgmp`) never collide with a partial copy. -no-pie is
# ELF-only (Linux); on Mach-O (macOS) it isn't accepted, so it's applied only
# for static/Linux targets.
NOPIE_FLAG=$(if $(call zig_is_static,$(ZIG_PLATFORM)),-no-pie,)
$(BUILD_DIR)/lib/$(AR_FILE): $(STDLIB_OBJS) | $(BUILD_DIR)/lib/tomo@$(TOMO_VERSION)
	$(CC) $(TARGET_FLAG) $(NOPIE_FLAG) -r -nostdlib $^ -o libtomo.o
	$(AR) rcs $@ libtomo.o
	rm -f libtomo.o

$(BUILD_DIR)/lib/tomo@$(TOMO_VERSION)/packages.ini: packages.ini.default | $(BUILD_DIR)/lib/tomo@$(TOMO_VERSION)
	@cp $^ $@

# gdb's view of a Tomo program (`tomo run --debug` loads it; see src/cmd/run.c):
$(BUILD_DIR)/lib/tomo@$(TOMO_VERSION)/tomo-gdb.py: src/stdlib/tomo-gdb.py | $(BUILD_DIR)/lib/tomo@$(TOMO_VERSION)
	@cp $< $@

$(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)/TOMO-LICENSE: LICENSE.md | $(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)
	cp $< $@

# --- Bundled Zig toolchain ------------------------------------------------
# The installed tomo always invokes a Zig toolchain bundled inside the install,
# so the distribution is self-contained (no system compiler needed to build and
# run Tomo programs). Zig is a native build for the target platform; on a cross
# build for a distribution, `make dist` fetches that platform's Zig to ship.
ZIG_STAGED = $(BUILD_BASE)/zig/zig
# The toolchain is shared between coresident Tomo versions: the real copy
# lives in a store keyed by ZIG's version, and each Tomo version's
# libexec/tomo@VER/zig is a symlink into it, so a Tomo upgrade that keeps
# the same zig pin adds ~10MB instead of ~400MB. `tomo uninstall` (no args)
# removes stores that no remaining installation's symlink points into.
ZIG_STORE_DIR = $(BUILD_DIR)/libexec/zig@$(ZIG_VERSION)
ZIG_LINK = $(BUILD_DIR)/libexec/tomo@$(TOMO_VERSION)/zig

# Download + checksum-verify + extract the pinned Zig for this platform:
$(ZIG_STAGED):
	$(MAKE) -C vendor zig ZIG_PLATFORM='$(ZIG_PLATFORM)' ZIG_TARGET='$(ZIG_TARGET)' BUILD_BASE='$(CURDIR)/$(BUILD_BASE)'

# Copy the Zig toolchain (binary + lib/ + LICENSE) into the install tree:
$(ZIG_STORE_DIR)/zig: $(ZIG_STAGED)
	rm -rf $(ZIG_STORE_DIR)
	mkdir -p $(ZIG_STORE_DIR)
	cp -R $(BUILD_BASE)/zig/. $(ZIG_STORE_DIR)/

$(ZIG_LINK): $(ZIG_STORE_DIR)/zig
	mkdir -p $(dir $(ZIG_LINK))
	rm -rf $(ZIG_LINK)
	ln -s ../zig@$(ZIG_VERSION) $@

# Ship Zig's license alongside Tomo's (Zig is MIT-licensed):
$(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)/ZIG-LICENSE: $(ZIG_STAGED) | $(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)
	cp $(BUILD_BASE)/zig/LICENSE $@
# --------------------------------------------------------------------------

# Version info for everything statically linked into Tomo binaries, embedded
# as a named section (.tomo.versions on ELF, __TEXT,__tomo_versions on Mach-O)
# in every binary via libtomo, retrievable with standard tools:
#   readelf -p .tomo.versions BINARY
# (The license texts themselves travel in each binary's embedded source zip;
# see `tomo --extract-source`.)
ifeq ($(ZIG_OS),macos)
VERSIONS_SRC=$(BUILD_BASE)/versions.c
else
VERSIONS_SRC=$(BUILD_BASE)/versions.s
endif
$(VERSIONS_SRC): vendor/versions.mk vendor/zig-checksums.mk scripts/embed_versions.sh
	@mkdir -p $(BUILD_BASE)
	@printf '%s\n' \
	    'tomo: $(TOMO_VERSION) ($(GIT_VERSION))' \
	    'zig: $(ZIG_VERSION)' \
	    'gc: $(GC_VERSION)' \
	    'gmp: $(GMP_VERSION)' \
	    'unistring: $(UNISTRING_VERSION)' \
	    'libbacktrace: $(LIBBACKTRACE_VERSION)' \
	    'miniz: $(MINIZ_VERSION)' \
	    > $(BUILD_BASE)/versions.txt
	./scripts/embed_versions.sh $@ $(BUILD_BASE)/versions.txt

$(OBJ_DIR)/versions.o: $(VERSIONS_SRC)
	@mkdir -p $(dir $@)
	@$(ECHO) $(CC) $(TARGET_FLAG) -c $< -o $@
	@$(CC) $(TARGET_FLAG) -c $< -o $@

# Ship the license text of every vendored library too (GMP is dual-licensed,
# LGPLv3+ or GPLv2+, so both of its texts ship):
LICENSES_DIR = $(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)
VENDOR_LICENSE_PRODUCTS = $(LICENSES_DIR)/GC-LICENSE \
	$(LICENSES_DIR)/GMP-COPYING.LESSERv3 $(LICENSES_DIR)/GMP-COPYINGv2 \
	$(LICENSES_DIR)/UNISTRING-COPYING.LIB $(LICENSES_DIR)/LIBBACKTRACE-LICENSE \
	$(LICENSES_DIR)/MINIZ-LICENSE

$(VENDOR_LICENSE_PRODUCTS) &: $(VENDOR_LICENSES) | $(LICENSES_DIR)
	cp $(BUILD_BASE)/gc/LICENSE $(LICENSES_DIR)/GC-LICENSE
	cp $(BUILD_BASE)/gmp/COPYING.LESSERv3 $(LICENSES_DIR)/GMP-COPYING.LESSERv3
	cp $(BUILD_BASE)/gmp/COPYINGv2 $(LICENSES_DIR)/GMP-COPYINGv2
	cp $(BUILD_BASE)/unistring/COPYING.LIB $(LICENSES_DIR)/UNISTRING-COPYING.LIB
	cp $(BUILD_BASE)/backtrace/LICENSE $(LICENSES_DIR)/LIBBACKTRACE-LICENSE
	cp $(BUILD_BASE)/miniz/LICENSE $(LICENSES_DIR)/MINIZ-LICENSE

# Ship the vendored static libraries inside the versioned lib dir (NOT
# directly in lib/, where they could shadow real system libraries). Every
# tomo-compiled program links these full archives alongside libtomo.a, so the
# linker extracts exactly the members used, whether by the stdlib or by packages
# with e.g. `use <gmp.h>`:
VENDOR_INSTALL_DIR=$(BUILD_DIR)/lib/tomo@$(TOMO_VERSION)/vendor
VENDOR_INSTALLED_LIBS=$(foreach d,$(filter-out miniz,$(VENDOR_DEPS)),$(VENDOR_INSTALL_DIR)/lib$(d).a)
$(VENDOR_INSTALLED_LIBS) &: $(filter-out %/libminiz.a,$(VENDORED_LIBS))
	@mkdir -p $(VENDOR_INSTALL_DIR)
	cp $(filter-out %/libminiz.a,$(VENDORED_LIBS)) $(VENDOR_INSTALL_DIR)/

# A debug-info-free copy of the C runtime zig links into static executables
# (musl libc, compiler-rt, crt1.o, ...). Static (linux/musl) targets only:
# tomo links compiled programs -nostdlib against these instead of zig's own
# unstripped versions, keeping megabytes of libc DWARF out of every binary
# (see scripts/stage_zig_libc.sh and src/tomo.c):
ZIG_LIBC_DIR=$(VENDOR_INSTALL_DIR)/zig-libc
$(ZIG_LIBC_DIR)/libc.a: $(ZIG_STAGED) scripts/stage_zig_libc.sh
	@$(ECHO) "Staging debug-stripped zig libc:\t$(ZIG_LIBC_DIR)"
	@sh scripts/stage_zig_libc.sh '$(ZIG_STAGED)' '$(ZIG_TARGET)' '$(ZIG_LIBC_DIR)'

# Everything that makes up an installed Tomo tree for the current platform:
BUILD_PRODUCTS = $(BUILD_DIR)/bin/tomo $(BUILD_DIR)/bin/tomo@$(TOMO_VERSION) \
	$(BUILD_DIR)/lib/$(AR_FILE) $(BUILD_DIR)/lib/tomo@$(TOMO_VERSION)/packages.ini \
	$(BUILD_DIR)/lib/tomo@$(TOMO_VERSION)/tomo-gdb.py \
	$(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)/TOMO-LICENSE $(build_headers) $(build_manpages) \
	$(BUILD_DIR)/include/tomo@$(TOMO_VERSION)/gc.h \
	$(ZIG_STORE_DIR)/zig $(ZIG_LINK) $(BUILD_DIR)/share/licenses/tomo@$(TOMO_VERSION)/ZIG-LICENSE \
	$(VENDOR_LICENSE_PRODUCTS) $(VENDOR_INSTALLED_LIBS)
ifneq ($(call zig_is_static,$(ZIG_PLATFORM)),)
BUILD_PRODUCTS += $(ZIG_LIBC_DIR)/libc.a
endif

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

# A version-independent alias for the newest archive, so the quick-install URL
# in the README (tomo@latest-<platform>.tar.xz) never needs a version bump.
# Upload it alongside the archive it points to.
DIST_LATEST = $(DIST_DIR)/tomo@latest-$(ZIG_PLATFORM).tar.xz
$(DIST_LATEST): $(DIST_ARCHIVE)
	ln -sf $(notdir $<) $@

# Build the distribution archive for the current platform only:
archive: $(DIST_ARCHIVE) $(DIST_LATEST)

version:
	@echo $(TOMO_VERSION)

check-zig:
	@$(CC) -v 2>/dev/null >/dev/null \
		|| { printf '\033[91;1m%s\033[m\n' "I can't run '$(CC)'! Tomo is built with Zig; please install it (https://ziglang.org/download/) and make sure 'zig' is on your PATH."; exit 1; }

tags:
	ctags src/*.{c,h} src/cmd/*.{c,h} src/stdlib/*.{c,h} src/stdlib/datatypes/*.h src/compile/*.{c,h} src/parse/*.{c,h} src/formatter/*.{c,h}

$(OBJ_DIR)/%.o: %.c config.mk | deps
	@mkdir -p $(dir $@)
	@$(ECHO) $(CC) $(CFLAGS_PLACEHOLDER) -c $< -o $@
	@$(CC) $(CFLAGS) -c $< -o $@

# Per-object header dependencies, generated by the compiler (see -MMD above):
-include $(COMPILER_OBJS:.o=.d) $(STDLIB_OBJS:.o=.d)

test-tm: build test/api.tm
	@mkdir -p test/results
	@if ! COLOR=1 LC_ALL=C ./local-tomo test $(TEST_FILES) 2>&1 | tee $(TEST_LOG); then \
		rm -f $(TEST_LOG); \
		false; \
	fi

# The Num type's C-level test suite. Built with the *host* compiler
# rather than `zig cc`, because it runs under ASan/UBSan and the static musl
# builds everything else uses can't host the sanitizer runtimes. It needs
# only number.c, so it links the host's gmp/gc rather than the vendored ones.
$(BUILD_BASE)/number_test: test/c/number_test.c src/stdlib/number.c src/stdlib/number.h src/stdlib/datatypes/num.h
	@mkdir -p $(dir $@)
	@$(ECHO) cc -fsanitize=address,undefined -o $@ test/c/number_test.c src/stdlib/number.c
	@# -iquote, not -I: src/stdlib/ has its own stdlib.h, which -I would make
	@# `#include <stdlib.h>` resolve to. -iquote only affects "..." includes.
	@cc -std=gnu23 -O1 -g -fsanitize=address,undefined -iquote src/stdlib \
	    -o $@ test/c/number_test.c src/stdlib/number.c -lm -lgmp -lgc

test-number: $(BUILD_BASE)/number_test
	@$(BUILD_BASE)/number_test

# The argument parser's C-level test suite. Unlike number_test, cli.c pulls in
# most of the stdlib, so this links the same objects and vendored libraries the
# `tomo` binary does, with the project toolchain (and therefore runs only when
# building for the host platform).
$(BUILD_BASE)/cli_test: test/c/cli_test.c $(STDLIB_OBJS) $(VENDORED_LIBS) | deps
	@mkdir -p $(dir $@)
	@$(ECHO) $(CC) $(CFLAGS_PLACEHOLDER) $(LDFLAGS) -o $@ test/c/cli_test.c
	@$(CC) $(filter-out -MMD -MP,$(CFLAGS)) -iquote src/stdlib $(LDFLAGS) \
	    -o $@ $< $(STDLIB_OBJS) $(VENDORED_LIBS) $(LDLIBS)

test-cli: $(BUILD_BASE)/cli_test
	@printf '\033[1m Testing CLI argument parsing... \033[m\n'
	@$(BUILD_BASE)/cli_test

# A compilation database for editors and language servers (clangd, ccls). The
# build's own flags are what let this tree parse at all (`$$` in identifiers,
# the vendored headers in place of the system ones, gnu23), so those are what
# it records. Generated rather than checked in: it names absolute paths and the
# host platform. Regenerated when the flags or the file list change.
LSP_SOURCES=$(wildcard src/*.c src/cmd/*.c src/compile/*.c src/parse/*.c src/formatter/*.c src/stdlib/*.c test/c/*.c)
# The flags go through the shell on their way to the script, which splits them
# exactly as it does for the compiler, so -DSUDO='"doas"' arrives as the one
# argument the compiler sees, quotes and all.
compile_commands.json: Makefile config.mk | ./scripts/compile_commands.py
	@./scripts/compile_commands.py $(LSP_SOURCES) -- \
	    $(filter-out -MMD -MP,$(CFLAGS)) -iquote src/stdlib > $@
	@printf 'Wrote \033[1m%s\033[m for %s files\n' $@ $(words $(LSP_SOURCES))

.PHONY: lsp
lsp: compile_commands.json

# Round-trips every .tm file in the tree through `tomo format` and checks that the
# formatter neither changes what the code means nor leaves it unsettled. The
# file list is gathered at recipe time, not parse time, so a generated file like
# test/api.tm is picked up on a fresh checkout:
test-format: build test/api.tm
	@printf '\033[1m Testing formatter... \033[m\n'
	@./local-tomo format --check $$(find test examples benchmarks -name '*.tm' \
	    -not -path 'test/parse/*' | sort)

# Snapshot tests for the parser: every test/parse/*.tm is parsed and its output
# (a parse tree, or the full text of a parse error) compared against the
# snapshot checked in beside it. The err_*.tm fixtures don't parse on purpose,
# which is why test-format skips this directory.
test-parse: build
	@printf '\033[1m Testing parser... \033[m\n'
	@./scripts/parse_tests.sh ./local-tomo

# Rewrites every parser snapshot from current behavior. It can't tell a fixed
# bug from a newly introduced one, so review `git diff test/parse` afterwards.
.PHONY: regen-parse-tests
regen-parse-tests: build
	@./scripts/parse_tests.sh ./local-tomo --regen

test: test-tm test-number test-cli test-parse test-format
	@printf '\033[92;7m ALL TESTS PASSED! \033[m\n'

# Remove just the (target-specific) Tomo object files:
# Remove the compiled Tomo object files (all platforms):
clean-obj:
	rm -rf build/*/obj

clean: clean-obj
	rm -rf build/*/tomo@*/{bin,lib,libexec} test/results test/.tomo lib/*/.tomo examples/.tomo examples/*/.tomo

%.md: %.yaml scripts/api_gen.py
	./scripts/api_gen.py $< >$@

api/api.md: $(API_YAML)
	./scripts/api_gen.py $^ >$@

test/api.tm: $(API_YAML) | ./scripts/api_tests.py
	./scripts/api_tests.py $^ >$@

.PHONY: test-format test-tm test-cli

.PHONY: api-docs
api-docs: $(API_MD) api/api.md

.PHONY: manpages
manpages: $(API_YAML) man/man1/tomo.1
	./scripts/mandoc_gen.py $(API_YAML)

man/man1/tomo.1: docs/tomo.1.md
	pandoc --lua-filter=docs/.pandoc/bold-code.lua -s $< -t man -o $@

examples:
	./local-tomo examples/learnxiny.tm

# --- Cross-language benchmarks (benchmarks/) ------------------------------
# `make benchmarks` re-times every language, re-measures static binary sizes,
# and re-renders every graph (results*.png/svg + sizes.png/svg) from the fresh
# numbers. The suite compiles the Tomo ports with ./local-tomo, so the
# compiler is rebuilt first. Reference programs for the other languages are
# downloaded into benchmarks/fetched/ (git-ignored) on first use, so that step
# needs network access; afterwards `make benchmark-run` skips it.
#
# The BENCH_* knobs documented in benchmarks/README.md are forwarded when set,
# either as environment or as make variables:
#   make benchmarks BENCH_LANGS=tomo           # re-time Tomo only, keep the
#                                              # other languages' stored rows
#   make benchmarks BENCH_ARGS=1000 BENCH_REPEATS=1   # quick smoke run
#   make benchmark-graphs                      # just re-render from the JSON
BENCH_DIR = benchmarks
BENCH_VARS = BENCH_LANGS BENCH_ARGS BENCH_REPEATS BENCH_CPU BENCH_BUILD_TIMEOUT \
             BENCH_ALLOW_BATTERY ZIG
# Only forward the ones that actually have a value: exporting e.g. BENCH_CPU=""
# would override the driver's default with an empty string.
BENCH_ENV = $(foreach v,$(BENCH_VARS),$(if $($(v)),$(v)='$($(v))'))
PYTHON ?= python3

benchmarks: benchmark-run benchmark-sizes benchmark-graphs
	@printf '\033[92;7m Benchmark graphs regenerated in %s/ \033[m\n' "$(BENCH_DIR)"

# Download the other languages' reference programs, once. The stamp is just
# the fetched/ directory itself; force a re-download with `make benchmark-refetch`.
benchmark-fetch: $(BENCH_DIR)/fetched
$(BENCH_DIR)/fetched:
	$(PYTHON) $(BENCH_DIR)/bench.py fetch

benchmark-refetch:
	$(PYTHON) $(BENCH_DIR)/bench.py fetch

benchmark-run: build benchmark-fetch
	$(BENCH_ENV) $(PYTHON) $(BENCH_DIR)/bench.py run

benchmark-sizes: build benchmark-fetch
	$(BENCH_ENV) $(PYTHON) $(BENCH_DIR)/bench.py sizes

# Re-render the graphs from whatever results.json/sizes.json are on disk.
benchmark-graphs:
	$(PYTHON) $(BENCH_DIR)/plot.py
	@if [ -f $(BENCH_DIR)/sizes.json ]; then \
	    $(PYTHON) $(BENCH_DIR)/plot.py --sizes; \
	else \
	    echo "(no $(BENCH_DIR)/sizes.json yet; skipping the size graph)"; \
	fi

benchmark-list:
	@$(PYTHON) $(BENCH_DIR)/bench.py list

deps: $(VENDORED_LIBS) $(VENDOR_LICENSES)

# "&:" (grouped targets) tells make that ONE invocation of this recipe produces
# all of these files. With a plain multi-target rule, parallel make would run
# the recipe once per missing file, with several concurrent `make -C vendor`
# instances racing to extract and configure the same source trees.
$(VENDORED_LIBS) $(VENDOR_LICENSES) &:
	$(MAKE) -C vendor ZIG_PLATFORM='$(ZIG_PLATFORM)' ZIG_TARGET='$(ZIG_TARGET)' BUILD_BASE='$(CURDIR)/$(BUILD_BASE)'

install-files: build check-zig
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

# After the files are in place, compile and run a hello-world through the
# installed tomo: the first compile makes the bundled zig build its libc
# cache (a one-time, ~10s setup), so it's better spent here than on the
# user's first real program. Runs as the invoking user (not the prefix
# owner), since the cache belongs to the user's home directory.
install: install-files
	@printf 'func main()\n\tsay("Tomo installation finished!")\n' | "$(PREFIX)/bin/tomo"
	@$(MAKE) --no-print-directory pch

# Pre-warm the precompiled-header cache. The first time tomo compiles at a
# given optimization level it builds a PCH of tomo.h and caches it (see
# get_precompiled_header() in src/cmd/compilation.c); doing that here spends
# the one-time cost now rather than on the user's first program. -O0 is what
# `tomo run` and `tomo test` compile at (already warmed by the hello-world
# above, but cheap to confirm), -O3 what `tomo build` and `tomo package` use.
# Runs as the invoking user, not the prefix owner, since the cache lives under
# their XDG cache directory. Warming is only an optimization, so a failure here
# warns rather than failing an installation that is otherwise complete.
pch:
	@tmp=$$(mktemp -d) || exit 0; \
	printf 'func main()\n\tsay("")\n' > "$$tmp/warmup.tm"; \
	status=0; \
	for opt in 0 3; do \
		"$(PREFIX)/bin/tomo@$(TOMO_VERSION)" build -O$$opt "$$tmp/warmup.tm" >/dev/null || status=1; \
	done; \
	rm -rf "$$tmp"; \
	if [ $$status -ne 0 ]; then \
		printf "\033[93;1mWarning: couldn't pre-warm the precompiled-header cache.\033[m\n" >&2; \
	fi

# Uninstalling is the compiler's job (`tomo uninstall` with no arguments): it
# knows where every file of its version went and how to fix up the shared
# symlinks.
uninstall:
	"$(PREFIX)/bin/tomo@$(TOMO_VERSION)" uninstall --yes

.SUFFIXES:
.PHONY: test-number test-cli all build clean clean-obj dist archive install install-files install-targets uninstall test tags examples deps check-zig version pch \
	benchmarks benchmark-fetch benchmark-refetch benchmark-run benchmark-sizes benchmark-graphs benchmark-list
