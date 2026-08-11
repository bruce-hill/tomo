#!/bin/sh

if command -v doas >/dev/null 2>&1; then  
    SUDO="doas"  
elif command -v sudo >/dev/null 2>&1; then  
    SUDO="sudo"  
else  
    echo "Neither doas nor sudo found." >&2  
    exit 1  
fi  

# Manually specify package manager:
if [ -n "$1" ]; then
    PKG_MGR="$1"
# Autodetect package manager:
elif command -v dnf >/dev/null 2>&1; then
    PKG_MGR="dnf"
elif command -v yay >/dev/null 2>&1; then
    PKG_MGR="yay"
elif command -v paru >/dev/null 2>&1; then
    PKG_MGR="paru"
elif command -v pacman >/dev/null 2>&1; then
    PKG_MGR="pacman"
elif command -v xbps-install >/dev/null 2>&1; then
    PKG_MGR="xbps"
elif command -v pkg_add >/dev/null 2>&1; then
    PKG_MGR="pkg_add"
elif command -v pkg >/dev/null 2>&1; then
    PKG_MGR="freebsd-pkg"
elif command -v brew >/dev/null 2>&1; then
    PKG_MGR="brew"
elif command -v port >/dev/null 2>&1; then
    PKG_MGR="macports"
elif command -v zypper >/dev/null 2>&1; then
    PKG_MGR="zypper"
elif command -v nix-env >/dev/null 2>&1; then
    PKG_MGR="nix"
elif command -v spack >/dev/null 2>&1; then
    PKG_MGR="spack"
elif command -v conda >/dev/null 2>&1; then
    PKG_MGR="conda"
elif command -v apt >/dev/null 2>&1; then
    PKG_MGR="apt"
else
    echo "Unsupported package manager" >&2
    exit 1
fi

# Tomo is built with `zig cc` and builds its library dependencies from vendored
# source, so the only build dependencies are Zig, binutils (nm, for autoconf
# probes), and curl (to download the vendored sources and the bundled Zig).
# tar + xz are part of the base system on all supported platforms.
#
# NOTE: Zig must be recent enough for the pinned toolchain (see
# vendor/zig-checksums.mk). On distributions that don't package a new enough Zig
# (notably Debian/Ubuntu apt), install it from https://ziglang.org/download/ .
case "$PKG_MGR" in
    apt) $SUDO apt install binutils curl xz-utils; echo "NOTE: install a recent 'zig' from https://ziglang.org/download/ (apt's is usually too old or missing)" ;;
    dnf) $SUDO dnf install zig binutils curl xz ;;
    pacman) $SUDO pacman -S zig binutils curl xz ;;
    yay|paru) $PKG_MGR -S zig binutils curl xz ;;
    xbps) $SUDO xbps-install -S zig binutils curl xz ;;
    pkg_add) $SUDO pkg_add zig curl ;;
    freebsd-pkg) $SUDO pkg install zig binutils curl ;;
    brew) brew install zig binutils curl xz ;;
    macports) $SUDO port install zig binutils curl xz ;;
    zypper) $SUDO zypper install zig binutils curl xz ;;
    nix) nix-env -iA nixpkgs.zig nixpkgs.binutils nixpkgs.curl nixpkgs.xz ;;
    spack) spack install zig binutils curl xz ;;
    conda) conda install -c conda-forge zig binutils curl xz ;;
    *)
        echo "Unknown package manager: $PKG_MGR" >&2
        exit 1
        ;;
esac
