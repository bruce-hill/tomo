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

# Install packages
case "$PKG_MGR" in
    apt) $SUDO apt install libgc-dev libunistring-dev binutils libgmp-dev ;;
    dnf) $SUDO dnf install gc-devel libunistring-devel binutils gmp-devel ;;
    pacman) $SUDO pacman -S gc libunistring binutils gmp ;;
    yay|paru) $PKG_MGR -S gc libunistring binutils gmp ;;
    xbps) $SUDO xbps-install -S gc libunistring binutils gmp ;;
    pkg_add) $SUDO pkg_add boehm-gc libunistring binutils gmp ;;
    freebsd-pkg) $SUDO pkg install boehm-gc libunistring binutils gmp ;;
    brew) brew install bdw-gc libunistring binutils llvm gmp ;;
    macports) $SUDO port install boehm-gc libunistring binutils gmp ;;
    zypper) $SUDO zypper install gc-devel libunistring-devel binutils gmp-devel ;;
    nix) nix-env -iA nixpkgs.boehmgc.dev nixpkgs.libunistring nixpkgs.binutils nixpkgs.nixpkgs.gmp ;;
    spack) spack install boehm-gc libunistring binutils gmp ;;
    conda) conda install boehm-gc libunistring binutils gmp ;;
    *)
        echo "Unknown package manager: $PKG_MGR" >&2
        exit 1
        ;;
esac
