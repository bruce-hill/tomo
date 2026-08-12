# Pinned versions + SHA-256 checksums for every vendored dependency (shared
# with the top-level Makefile, which embeds the versions into binaries):
include versions.mk

# Keep the build hermetic: don't let the user's ambient include/library search
# paths leak non-musl (e.g. system glibc) headers into the cross builds.
unexport C_INCLUDE_PATH
unexport CPLUS_INCLUDE_PATH
unexport CPATH
unexport LIBRARY_PATH
unexport LD_LIBRARY_PATH

# Pinned Zig version + per-platform checksums and platform lists:
include zig-checksums.mk

# ---------------------------------------------------------------------------
# Target selection
#
# ZIG_PLATFORM is a Zig platform key (e.g. "x86_64-linux"); ZIG_TARGET is the
# corresponding musl compile triple (e.g. "x86_64-linux-musl"). Both default to
# the host. The vendored libraries and the bundled Zig for a given platform are
# built/staged under BUILD_BASE (build/<platform>/), so multiple platforms can
# coexist. CC is the *host* Zig used as the (cross-)compiler.
# ---------------------------------------------------------------------------
ZIG_PLATFORM ?= $(ZIG_HOST_PLATFORM)
ZIG_TARGET ?= $(call zig_target,$(ZIG_PLATFORM))
# The toolchain is not configurable: Tomo requires Zig. zig's llvm ar/ranlib are
# used because they understand every target object format (incl. Mach-O), unlike
# the host's GNU binutils.
CC = zig cc
AR = zig ar
RANLIB = zig ranlib
DEP_CC = $(CC) -target $(ZIG_TARGET)
BUILD_BASE ?= ../build/$(ZIG_PLATFORM)

# A build is "cross" when the target CPU *or* OS differs from the host. For a
# genuine cross build, configure must be told --host (a canonical GNU triple) and
# can't run target test programs. For a same-arch, same-OS build (e.g. host
# x86_64-linux -> x86_64 musl) we deliberately omit --host: the target binaries
# run natively, configure's feature tests succeed, and passing --host would make
# gnulib mis-detect things like off64_t.
HOST_ARCH := $(shell uname -m)
HOST_OS := $(shell uname -s | tr A-Z a-z)
TARGET_ARCH = $(call zig_arch,$(ZIG_PLATFORM))
TARGET_OS = $(call zig_os,$(ZIG_PLATFORM))

# For OpenBSD targets, libtool invokes `$RANLIB -t lib.a` at install time, an
# OpenBSD-ranlib flag that llvm's `zig ranlib` rejects. The call is redundant
# anyway (`ar s` already writes the symbol index), so use a no-op. `override`
# because the top-level make passes RANLIB on the command line.
ifeq ($(TARGET_OS),openbsd)
override RANLIB = :
endif
ifneq ($(HOST_ARCH)|$(HOST_OS),$(TARGET_ARCH)|$(TARGET_OS))
    CROSS_HOST = --host=$(call zig_gnu_triple,$(ZIG_PLATFORM))
endif
# Vendored-library frames never appear in Tomo stacktraces, and gdb variable
# inspection matters for libtomo and generated code, not these libraries'
# internals — so full debug info for them is dead weight in every linked
# program (autoconf's default `-g -O2` put ~2MB of DWARF into even a
# hello-world binary). -g1 keeps just enough (function names + line tables) to
# make sense of a gdb backtrace into these libs; set -g here if you ever need
# to step through their code with locals visible. libbacktrace overrides this
# with its own CFLAGS (further down).
# -ffunction-sections/-fdata-sections so that the --gc-sections in every
# program link (see tomo.c) can drop the individual functions and tables these
# libraries' linked-in objects don't actually use:
VENDOR_CFLAGS = -O2 -g1 -ffunction-sections -fdata-sections
CONFIGURE_FLAGS = CC="$(DEP_CC)" AR="$(AR)" RANLIB="$(RANLIB)" $(CROSS_HOST) \
                  CFLAGS="$(VENDOR_CFLAGS)" --enable-static --disable-shared

# OpenBSD's native linker provides a `__data_start` symbol that Boehm GC's
# OpenBSD configuration uses as the start of the data segment, but the lld
# bundled with zig does not define it (and zig rejects --defsym). Aliasing it to
# `_etext` (which lld does provide) at the preprocessor level is safe: GC's
# OpenBSD GC_register_data_segments() probes readable regions between DATASTART
# and DATAEND and skips holes, so starting at the end of text merely scans
# rodata conservatively as well.
GC_EXTRA = $(if $(filter openbsd,$(TARGET_OS)),CPPFLAGS=-D__data_start=_etext,)

# GMP detects its assembly symbol prefix by running the host `nm` on a compiled
# object. That fails for Mach-O (macOS) objects, which GNU nm can't read, so for
# macOS targets GMP is built without assembly (portable C) and told the symbol
# prefix directly (Mach-O prefixes globals with an underscore) -- exactly the
# workaround GMP's own configure recommends when nm can't be used.
GMP_EXTRA = $(if $(filter macos,$(TARGET_OS)),--disable-assembly gmp_cv_asm_underscore=yes gmp_cv_asm_w32=.long,)

# Per-platform extraction/build directories for the C dependencies:
DEPS_DIR = $(BUILD_BASE)/deps

# License texts for each vendored library, staged next to its build products
# so the top-level Makefile can ship them in share/licenses/. GMP is
# dual-licensed (LGPLv3+ or GPLv2+), so both of its texts ship; libunistring
# (same dual license) ships only COPYING.LIB, the license text in its tarball
# that covers the library.
LICENSE_FILES = $(BUILD_BASE)/gc/LICENSE \
	$(BUILD_BASE)/gmp/COPYING.LESSERv3 $(BUILD_BASE)/gmp/COPYINGv2 \
	$(BUILD_BASE)/unistring/COPYING.LIB \
	$(BUILD_BASE)/backtrace/LICENSE \
	$(BUILD_BASE)/miniz/LICENSE

# Each vendored dependency "foo" installs into $(BUILD_BASE)/foo/{lib,include}:
VENDOR_DEPS = gc gmp unistring backtrace miniz
VENDORED_LIBS = $(foreach d,$(VENDOR_DEPS),$(BUILD_BASE)/$(d)/lib/lib$(d).a)

all: $(VENDORED_LIBS) $(LICENSE_FILES)

# Boehm GC ships no standalone LICENSE file; README.QUICK opens with the
# canonical permissive license text (the same file distros install as its
# license).
$(BUILD_BASE)/gc/LICENSE: $(DEPS_DIR)/gc-$(GC_VERSION)/configure
	@mkdir -p $(dir $@)
	cp $(DEPS_DIR)/gc-$(GC_VERSION)/README.QUICK $@

$(BUILD_BASE)/gmp/COPYING.LESSERv3 $(BUILD_BASE)/gmp/COPYINGv2 &: $(DEPS_DIR)/gmp-$(GMP_VERSION)/configure
	@mkdir -p $(BUILD_BASE)/gmp
	cp $(DEPS_DIR)/gmp-$(GMP_VERSION)/COPYING.LESSERv3 $(DEPS_DIR)/gmp-$(GMP_VERSION)/COPYINGv2 $(BUILD_BASE)/gmp/

$(BUILD_BASE)/unistring/COPYING.LIB: $(DEPS_DIR)/libunistring-$(UNISTRING_VERSION)/configure
	@mkdir -p $(dir $@)
	cp $(DEPS_DIR)/libunistring-$(UNISTRING_VERSION)/COPYING.LIB $@

$(BUILD_BASE)/backtrace/LICENSE: $(DEPS_DIR)/libbacktrace-$(LIBBACKTRACE_VERSION)/configure
	@mkdir -p $(dir $@)
	cp $(DEPS_DIR)/libbacktrace-$(LIBBACKTRACE_VERSION)/LICENSE $@

# ===========================================================================
# Zig toolchain
# ===========================================================================
ZIG_MIRROR ?= https://ziglang.org/download
ZIG_CACHE ?= zig-cache

# zig_ext(platform) -> "zip" for Windows platforms, otherwise "tar.xz"
zig_ext = $(if $(filter $(1),$(ZIG_ZIP_PLATFORMS)),zip,tar.xz)

# Download + checksum-verify a Zig archive into the cache. The stem ($*) is the
# platform key, so $(zig_sha_$*) selects the pinned SHA-256 for that platform.
$(ZIG_CACHE)/zig-%-$(ZIG_VERSION).tar.xz:
	@mkdir -p $(ZIG_CACHE)
	curl -fL -o $@.tmp '$(ZIG_MIRROR)/$(ZIG_VERSION)/zig-$*-$(ZIG_VERSION).tar.xz'
	@echo '$(zig_sha_$*)  $@.tmp' | sha256sum -c - || { rm -f $@.tmp; false; }
	@mv $@.tmp $@

$(ZIG_CACHE)/zig-%-$(ZIG_VERSION).zip:
	@mkdir -p $(ZIG_CACHE)
	curl -fL -o $@.tmp '$(ZIG_MIRROR)/$(ZIG_VERSION)/zig-$*-$(ZIG_VERSION).zip'
	@echo '$(zig_sha_$*)  $@.tmp' | sha256sum -c - || { rm -f $@.tmp; false; }
	@mv $@.tmp $@

# Download + verify the Zig archive for the current platform:
.PHONY: download-zig
download-zig: $(ZIG_CACHE)/zig-$(ZIG_PLATFORM)-$(ZIG_VERSION).$(call zig_ext,$(ZIG_PLATFORM))

# Download + verify the Zig archives for *every* platform Zig ships (for
# mirroring / building distributions for other platforms):
.PHONY: download-all-zig
download-all-zig: $(foreach p,$(ZIG_ALL_PLATFORMS),$(ZIG_CACHE)/zig-$(p)-$(ZIG_VERSION).$(call zig_ext,$(p)))

# Extract the (verified) Zig toolchain for ZIG_PLATFORM into the platform build
# dir. This is the tree (zig binary + lib/ + LICENSE) that gets bundled into a
# Tomo distribution. Windows uses .zip, everything else .tar.xz.
$(BUILD_BASE)/zig/zig: $(ZIG_CACHE)/zig-$(ZIG_PLATFORM)-$(ZIG_VERSION).$(call zig_ext,$(ZIG_PLATFORM))
	rm -rf $(BUILD_BASE)/zig
	mkdir -p $(BUILD_BASE)/zig
	case '$<' in \
	    *.zip) cd $(BUILD_BASE)/zig && unzip -q "$$(realpath -m --relative-to=$(BUILD_BASE)/zig $(CURDIR)/$<)" && \
	           mv zig-$(ZIG_PLATFORM)-$(ZIG_VERSION)/* . && rmdir zig-$(ZIG_PLATFORM)-$(ZIG_VERSION) ;; \
	    *) tar xJf $< -C $(BUILD_BASE)/zig --strip-components=1 ;; \
	esac
	@test -x $@ || { echo "Zig binary not found after extraction: $@"; false; }
	@touch $@

.PHONY: zig
zig: $(BUILD_BASE)/zig/zig

# ===========================================================================
# Vendored C libraries (built statically for ZIG_TARGET with zig cc)
# ===========================================================================

# Boehm-Demers-Weiser Garbage collector
gc-$(GC_VERSION).tar.gz:
	curl -fL -o $@.tmp 'https://hboehm.info/gc/gc_source/gc-$(GC_VERSION).tar.gz'
	@echo '$(GC_SHA256)  $@.tmp' | sha256sum -c - || { rm -f $@.tmp; false; }
	@mv $@.tmp $@

$(DEPS_DIR)/gc-$(GC_VERSION)/configure: gc-$(GC_VERSION).tar.gz
	mkdir -p $(DEPS_DIR)
	tar xzfm $< -C $(DEPS_DIR)
	@touch $@

$(DEPS_DIR)/gc-$(GC_VERSION)/Makefile: $(DEPS_DIR)/gc-$(GC_VERSION)/configure
	prefix=$$(realpath $(BUILD_BASE))/gc; \
	cd $(DEPS_DIR)/gc-$(GC_VERSION); \
	./configure $(CONFIGURE_FLAGS) $(GC_EXTRA) --prefix="$$prefix" \
		--with-libatomic-ops=none
# --with-libatomic-ops=none: use the compiler's atomic intrinsics instead of an
# external libatomic_ops. Required for cross builds, where configure can't run
# feature tests and would otherwise demand the external library.

# CC="$(DEP_CC)" is re-asserted on every dependency `make` so the -target flag
# survives: the top-level `make` passes CC='zig cc' as a command-line override,
# which would otherwise propagate down and drop the musl target.
$(BUILD_BASE)/gc/lib/libgc.a: $(DEPS_DIR)/gc-$(GC_VERSION)/Makefile
	$(MAKE) -C $(DEPS_DIR)/gc-$(GC_VERSION) CC="$(DEP_CC)" -j install

# GNU Multiple Precision Arithmetic Library
gmp-$(GMP_VERSION).tar.xz:
	curl -fL -o $@.tmp 'https://gmplib.org/download/gmp/gmp-$(GMP_VERSION).tar.xz'
	@echo '$(GMP_SHA256)  $@.tmp' | sha256sum -c - || { rm -f $@.tmp; false; }
	@mv $@.tmp $@

$(DEPS_DIR)/gmp-$(GMP_VERSION)/configure: gmp-$(GMP_VERSION).tar.xz
	mkdir -p $(DEPS_DIR)
	tar xJfm $< -C $(DEPS_DIR)
	cd $(DEPS_DIR)/gmp-$(GMP_VERSION) && patch -p0 -N < $(CURDIR)/gmp-configure-fix.patch
	@touch $@

$(DEPS_DIR)/gmp-$(GMP_VERSION)/Makefile: $(DEPS_DIR)/gmp-$(GMP_VERSION)/configure
	prefix=$$(realpath $(BUILD_BASE))/gmp; \
	cd $(DEPS_DIR)/gmp-$(GMP_VERSION); \
	./configure $(CONFIGURE_FLAGS) $(GMP_EXTRA) --prefix="$$prefix"

# NOTE: `make check` is skipped: these are cross-compiled builds, so the target
# test binaries can't be run on the build host.
$(BUILD_BASE)/gmp/lib/libgmp.a: $(DEPS_DIR)/gmp-$(GMP_VERSION)/Makefile
	$(MAKE) -C $(DEPS_DIR)/gmp-$(GMP_VERSION) CC="$(DEP_CC)" -j
	$(MAKE) -C $(DEPS_DIR)/gmp-$(GMP_VERSION) CC="$(DEP_CC)" install

# Lib Unistring
libunistring-$(UNISTRING_VERSION).tar.gz:
	curl -fL -o $@.tmp 'https://ftp.gnu.org/gnu/libunistring/libunistring-$(UNISTRING_VERSION).tar.gz'
	@echo '$(UNISTRING_SHA256)  $@.tmp' | sha256sum -c - || { rm -f $@.tmp; false; }
	@mv $@.tmp $@

# Preserve archive timestamps (no `m` flag) so the pre-generated gperf output
# headers stay newer than their .gperf sources and aren't rebuilt (which would
# require `gperf` to be installed).
$(DEPS_DIR)/libunistring-$(UNISTRING_VERSION)/configure: libunistring-$(UNISTRING_VERSION).tar.gz
	mkdir -p $(DEPS_DIR)
	tar xzf $< -C $(DEPS_DIR)
	@touch $@

$(DEPS_DIR)/libunistring-$(UNISTRING_VERSION)/Makefile: $(DEPS_DIR)/libunistring-$(UNISTRING_VERSION)/configure
	prefix=$$(realpath $(BUILD_BASE))/unistring; \
	cd $(DEPS_DIR)/libunistring-$(UNISTRING_VERSION); \
	./configure $(CONFIGURE_FLAGS) --prefix="$$prefix"

# NOTE: `make check` is skipped: cross-compiled, and libunistring's test suite
# also exercises non-C locales that musl deliberately does not implement.
$(BUILD_BASE)/unistring/lib/libunistring.a: $(DEPS_DIR)/libunistring-$(UNISTRING_VERSION)/Makefile
	$(MAKE) -C $(DEPS_DIR)/libunistring-$(UNISTRING_VERSION) CC="$(DEP_CC)" -j
	$(MAKE) -C $(DEPS_DIR)/libunistring-$(UNISTRING_VERSION) CC="$(DEP_CC)" install

# libbacktrace: in-process DWARF symbolization for runtime stack traces (so
# installed programs don't need any external symbolization tool). Built without
# its own debug info (-g0) to keep its ~80KB of code from dragging in ~400KB of
# DWARF.
libbacktrace-$(LIBBACKTRACE_VERSION).tar.gz:
	curl -fL -o $@.tmp 'https://github.com/ianlancetaylor/libbacktrace/archive/$(LIBBACKTRACE_VERSION).tar.gz'
	@echo '$(LIBBACKTRACE_SHA256)  $@.tmp' | sha256sum -c - || { rm -f $@.tmp; false; }
	@mv $@.tmp $@

$(DEPS_DIR)/libbacktrace-$(LIBBACKTRACE_VERSION)/configure: libbacktrace-$(LIBBACKTRACE_VERSION).tar.gz
	mkdir -p $(DEPS_DIR)
	tar xzfm $< -C $(DEPS_DIR)
	# elf.c's zlib and lzma decompressors only fire for legacy ".zdebug"
	# sections, the ELFCOMPRESS_ZLIB case, and .gnu_debugdata (Fedora/Debian
	# MiniDebugInfo) -- none of which a tomo build ever produces (debug
	# sections are always --compress-debug-sections=zstd, and tomo never
	# emits .gnu_debugdata). They're direct calls from elf_add(), which is
	# always live, so --gc-sections can't drop them on its own; this patch
	# guards them behind USE_ZLIB/USE_LZMA (deliberately left undefined
	# below) to remove ~12KB of dead code from every tomo-built binary.
	cd $(DEPS_DIR)/libbacktrace-$(LIBBACKTRACE_VERSION) && patch -p0 -N < $(CURDIR)/libbacktrace-drop-unused-decompressors.patch
	@touch $@

$(DEPS_DIR)/libbacktrace-$(LIBBACKTRACE_VERSION)/Makefile: $(DEPS_DIR)/libbacktrace-$(LIBBACKTRACE_VERSION)/configure
	prefix=$$(realpath $(BUILD_BASE))/backtrace; \
	cd $(DEPS_DIR)/libbacktrace-$(LIBBACKTRACE_VERSION); \
	./configure $(CONFIGURE_FLAGS) CFLAGS="-O2 -g0 -fPIC" --prefix="$$prefix"

$(BUILD_BASE)/backtrace/lib/libbacktrace.a: $(DEPS_DIR)/libbacktrace-$(LIBBACKTRACE_VERSION)/Makefile
	$(MAKE) -C $(DEPS_DIR)/libbacktrace-$(LIBBACKTRACE_VERSION) CC="$(DEP_CC)" -j
	$(MAKE) -C $(DEPS_DIR)/libbacktrace-$(LIBBACKTRACE_VERSION) CC="$(DEP_CC)" install

# miniz: DEFLATE + ZIP archive reading/writing, used by the tomo compiler to
# embed (and extract) program source zips. The release asset is the two-file
# amalgamated distribution, so it's compiled directly -- no configure step.
# MINIZ_NO_TIME keeps zip archives deterministic (no timestamps); the same
# define is passed when compiling tomo so the header's ABI matches.
miniz-$(MINIZ_VERSION).zip:
	curl -fL -o $@.tmp 'https://github.com/richgel999/miniz/releases/download/$(MINIZ_VERSION)/miniz-$(MINIZ_VERSION).zip'
	@echo '$(MINIZ_SHA256)  $@.tmp' | sha256sum -c - || { rm -f $@.tmp; false; }
	@mv $@.tmp $@

$(DEPS_DIR)/miniz-$(MINIZ_VERSION)/miniz.c: miniz-$(MINIZ_VERSION).zip
	mkdir -p $(DEPS_DIR)/miniz-$(MINIZ_VERSION)
	cd $(DEPS_DIR)/miniz-$(MINIZ_VERSION) && unzip -oq "$$(realpath -m --relative-to=. $(CURDIR)/$<)"
	@touch $@

$(BUILD_BASE)/miniz/LICENSE: $(DEPS_DIR)/miniz-$(MINIZ_VERSION)/miniz.c
	@mkdir -p $(dir $@)
	cp $(DEPS_DIR)/miniz-$(MINIZ_VERSION)/LICENSE $@

$(BUILD_BASE)/miniz/lib/libminiz.a: $(DEPS_DIR)/miniz-$(MINIZ_VERSION)/miniz.c
	mkdir -p $(BUILD_BASE)/miniz/lib $(BUILD_BASE)/miniz/include
	$(DEP_CC) -O2 -fPIC -DMINIZ_NO_TIME -c $< -o $(DEPS_DIR)/miniz-$(MINIZ_VERSION)/miniz.o
	$(AR) rcs $@ $(DEPS_DIR)/miniz-$(MINIZ_VERSION)/miniz.o
	cp $(DEPS_DIR)/miniz-$(MINIZ_VERSION)/miniz.h $(BUILD_BASE)/miniz/include/

# Remove built artifacts for the current platform (keeps downloaded archives):
clean:
	rm -rf $(foreach d,$(VENDOR_DEPS),$(BUILD_BASE)/$(d)) $(BUILD_BASE)/deps $(BUILD_BASE)/zig

# Remove downloaded Zig archives too:
clean-cache: clean
	rm -rf $(ZIG_CACHE)

.PHONY: all clean clean-cache download-zig download-all-zig zig
