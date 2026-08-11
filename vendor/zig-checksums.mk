# Pinned Zig release checksums.
#
# This file is data, generated from https://ziglang.org/download/index.json for
# the pinned ZIG_VERSION. To update it for a new Zig version, regenerate with:
#
#   curl -s https://ziglang.org/download/index.json | jq -r '
#     ."<VERSION>" | to_entries[]
#     | select((.value|type)=="object" and .value.tarball != null
#              and (.key|IN("src","bootstrap")|not))
#     | "zig_sha_\(.key) = \(.value.shasum)"'
#
# The SHA-256 sums below are verified against every downloaded Zig archive.

ZIG_VERSION = 0.16.0

zig_sha_x86_64-macos = 0387557ed1877bc6a2e1802c8391953baddba76081876301c522f52977b52ba7
zig_sha_aarch64-macos = b23d70deaa879b5c2d486ed3316f7eaa53e84acf6fc9cc747de152450d401489
zig_sha_x86_64-linux = 70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00
zig_sha_aarch64-linux = ea4b09bfb22ec6f6c6ceac57ab63efb6b46e17ab08d21f69f3a48b38e1534f17
zig_sha_arm-linux = f85116bf2f9189bb6ae280c7f92f03b89c2551a88e17881c0c2df86bf4e42c50
zig_sha_riscv64-linux = bc069b0f2f568f54bafbdfc1d65b12fd386ed6a652044a37aee6a4f72f14076e
zig_sha_powerpc64le-linux = 18800b45c08bf40b335ca5ab79aea70aca287ca969036e938155772becaeebeb
zig_sha_x86-linux = 4e34e279a9f856358de420490b531974c3d37f8f3707eef9f0342e92c14c301f
zig_sha_loongarch64-linux = 2503be8ecc5965f1f7962471d267d9f83fcb3cc2f7ff78ac34093b9722bbea93
zig_sha_s390x-linux = 14db4884b31414bd637879036c1e6e335f3ba44ccac59df104163728a0cab30e
zig_sha_x86_64-windows = 68659eb5f1e4eb1437a722f1dd889c5a322c9954607f5edcf337bc3684a75a7e
zig_sha_aarch64-windows = aee38316ee4111717900f45dd3130145c39289e105541d737eb8c5ed653c78ef
zig_sha_x86-windows = 8aee7e8a8deb998ba96cb95d89aa5fcdf32933fdc67de51d280d9e4d7396f1a0
zig_sha_aarch64-freebsd = 0a441f50696a34cfb9f9a0b3c510fd49a56b18b364e03c26525cb7999959c969
zig_sha_arm-freebsd = 78b2a7bfa74f504075dd4b5c00418347a21b893f0eae580cf4e56c553cf181f6
zig_sha_powerpc64le-freebsd = 46b3fa0239be42669d86ce8394f8de2d608a4f51e3e65cb8222bfd543b95e310
zig_sha_riscv64-freebsd = aefbaea44f32bde07118f5522b7bb21de9967a6d09183e3e37b1f5be2c78f948
zig_sha_x86_64-freebsd = 390379bfaf6b89b001c6bb4035b8fcbc5c12193fd9fd5048c3a0fe39e5d1cf72
zig_sha_aarch64-netbsd = 4767425d72c779275ebbc5a6319f5a3c5d109664d3d8c880125d550e87d9d46a
zig_sha_arm-netbsd = 07aaa403c18e9fa33687a3eb3109bb484a96aece95a4f6792a911d2eefac07c5
zig_sha_x86-netbsd = aa36c02caad52d92e6354baec16dd866642e64b7c30e4d515806c8296d65586f
zig_sha_x86_64-netbsd = e235ca96f63034a009aca831c69394a1c14498a8e06ff5f358fc3fb4bed73dfb
zig_sha_aarch64-openbsd = f3c868ccc1bc30e2d2b68128186b2153d86c3efc41190f4170a43024a3cd7809
zig_sha_arm-openbsd = 33786d6487aaeca9f300e5a932d5ee5365d9254be0add44b3af8f6db8441ca2f
zig_sha_riscv64-openbsd = ffb74aefa8184a4b886f8c0a9105db00ac9562d502330f4e764a7a49171e4f8e
zig_sha_x86_64-openbsd = 25b72946e8b2dde5c93bc03cbebeb17e4d90eea8040acce8bb78cefdeee97337

# Every platform Zig ships a binary release for (used by `make download-all-zig`):
ZIG_ALL_PLATFORMS = x86_64-macos aarch64-macos \
    x86_64-linux aarch64-linux arm-linux riscv64-linux powerpc64le-linux \
    x86-linux loongarch64-linux s390x-linux \
    x86_64-windows aarch64-windows x86-windows \
    aarch64-freebsd arm-freebsd powerpc64le-freebsd riscv64-freebsd x86_64-freebsd \
    aarch64-netbsd arm-netbsd x86-netbsd x86_64-netbsd \
    aarch64-openbsd arm-openbsd riscv64-openbsd x86_64-openbsd

# Platforms whose archive is a .zip (Windows); all others are .tar.xz:
ZIG_ZIP_PLATFORMS = x86_64-windows aarch64-windows x86-windows

# ---------------------------------------------------------------------------
# Platform model
#
# A "platform" is a Zig platform key of the form <arch>-<os>, e.g. "x86_64-linux"
# or "aarch64-macos". Linux targets link a fully static musl libc; macOS and the
# BSDs cannot be statically linked (their libc requires dynamic linking), so they
# ship with a dynamic libc but statically bundle Tomo's vendored libraries.
#
# Windows is excluded: Tomo's runtime relies on POSIX facilities (fork, mmap,
# pthreads, dlfcn) that Windows doesn't provide.
# ---------------------------------------------------------------------------
# Only 64-bit targets: Tomo does not support 32-bit platforms (its runtime
# assumes 64-bit integers/pointers), so arm-linux and x86-linux are excluded.
# loongarch64-linux is excluded because the vendored Boehm GC (8.2.8) does not
# support that architecture (gcconfig.h: "Bad word size").
ZIG_LINUX_PLATFORMS = x86_64-linux aarch64-linux riscv64-linux \
    powerpc64le-linux s390x-linux
ZIG_MACOS_PLATFORMS = x86_64-macos aarch64-macos
ZIG_BSD_PLATFORMS = x86_64-freebsd aarch64-freebsd \
    x86_64-netbsd aarch64-netbsd \
    x86_64-openbsd aarch64-openbsd

# The default `make dist` matrix:
ZIG_DIST_PLATFORMS = $(ZIG_LINUX_PLATFORMS) $(ZIG_MACOS_PLATFORMS) $(ZIG_BSD_PLATFORMS)

# --- The host's platform key ------------------------------------------------
# uname spellings differ from Zig's: macOS reports "Darwin"/"arm64" and the
# BSDs report "amd64", so both components need normalizing.
host_os_map_Linux = linux
host_os_map_Darwin = macos
host_os_map_FreeBSD = freebsd
host_os_map_NetBSD = netbsd
host_os_map_OpenBSD = openbsd
host_arch_map_arm64 = aarch64
host_arch_map_amd64 = x86_64
host_uname_os := $(shell uname -s)
host_uname_arch := $(shell uname -m)
ZIG_HOST_PLATFORM := $(or $(host_arch_map_$(host_uname_arch)),$(host_uname_arch))-$(or $(host_os_map_$(host_uname_os)),$(host_uname_os))

# --- Helpers: derive the OS and arch from a platform key --------------------
# The arch never contains a dash (x86_64, aarch64, powerpc64le, loongarch64, ...)
# and the OS is the final "-"-separated component.
zig_os = $(lastword $(subst -, ,$(1)))
zig_arch = $(patsubst %-$(call zig_os,$(1)),%,$(1))

# --- The `zig cc -target` triple for a platform -----------------------------
# Linux needs an explicit musl ABI (32-bit arm uses hard-float musl); every other
# OS uses the platform key verbatim, which is already a valid Zig target.
zig_triple_x86_64-linux = x86_64-linux-musl
zig_triple_aarch64-linux = aarch64-linux-musl
zig_triple_arm-linux = arm-linux-musleabihf
zig_triple_riscv64-linux = riscv64-linux-musl
zig_triple_powerpc64le-linux = powerpc64le-linux-musl
zig_triple_x86-linux = x86-linux-musl
zig_triple_loongarch64-linux = loongarch64-linux-musl
zig_triple_s390x-linux = s390x-linux-musl
zig_target = $(or $(zig_triple_$(1)),$(1))

# --- The GNU triple used for autotools `configure --host` -------------------
# Zig's short triples (x86_64-macos) aren't valid autotools hosts, so the
# vendored libraries are configured with a canonical GNU triple instead.
gnu_os_macos = apple-darwin
gnu_os_freebsd = unknown-freebsd
gnu_os_netbsd = unknown-netbsd
gnu_os_openbsd = unknown-openbsd
# Linux: the musl target triple is already a valid GNU host triple.
zig_gnu_triple = $(if $(filter linux,$(call zig_os,$(1))),$(call zig_target,$(1)),$(call zig_arch,$(1))-$(gnu_os_$(call zig_os,$(1))))

# --- Whether a platform links fully statically ------------------------------
# Non-empty (truthy) only for Linux/musl; macOS and the BSDs link libc dynamically.
zig_is_static = $(filter linux,$(call zig_os,$(1)))
