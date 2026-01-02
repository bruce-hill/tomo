#!/usr/bin/env bash
set -euo pipefail

OWNER="bruce-hill"
REPO="tomo"

# Fetch latest release tag
TAG=$(curl -s "https://api.github.com/repos/$OWNER/$REPO/releases/latest" \
      | grep -Po '"tag_name": "\K.*?(?=")')

if [[ -z "$TAG" ]]; then
    echo "Failed to get latest release tag"
    exit 1
fi

# Detect platform
OS="$(uname -s)"
ARCH="$(uname -m)"
case "$OS" in
    Linux)
        case "$ARCH" in
            x86_64) FILE="tomo-linux-x86_64.tar.gz" ;;
            aarch64|arm64) FILE="tomo-linux-aarch64.tar.gz" ;;
            *) echo "Unsupported architecture: $ARCH"; exit 1 ;;
        esac
        ;;
    Darwin)
        FILE="tomo-macos-universal.tar.gz"
        ;;
    *)
        echo "Unsupported OS: $OS"
        exit 1
        ;;
esac

# Download the artifact (if not present)
if ! [ -e "$FILE" ]; then
    URL="https://github.com/$OWNER/$REPO/releases/download/$TAG/$FILE"
    echo "Downloading $URL ..."
    curl -L -o "$FILE" "$URL"
    echo "Downloaded $FILE"
fi

# Download checksum (if not present)
if ! [ -e "$FILE.sha256" ]; then
    CHECKSUM_URL="$URL.sha256"
    echo "Downloading checksum $CHECKSUM_URL ..."
    curl -L -o "$FILE.sha256" "$CHECKSUM_URL"
fi

# Verify checksum
shasum --check "$FILE.sha256"
echo "Verified checksum"

# Configure `doas` vs `sudo`
if command -v doas >/dev/null 2>&1; then  
    SUDO="doas"  
elif command -v sudo >/dev/null 2>&1; then  
    SUDO="sudo"  
else  
    echo "Neither doas nor sudo found." >&2  
    exit 1  
fi  

# Autodetect package manager:
if [ -z "${PACKAGE_MANAGER:-}" ]; then
    if command -v dnf >/dev/null 2>&1; then
        PACKAGE_MANAGER="dnf"
    elif command -v yay >/dev/null 2>&1; then
        PACKAGE_MANAGER="yay"
    elif command -v paru >/dev/null 2>&1; then
        PACKAGE_MANAGER="paru"
    elif command -v pacman >/dev/null 2>&1; then
        PACKAGE_MANAGER="pacman"
    elif command -v xbps-install >/dev/null 2>&1; then
        PACKAGE_MANAGER="xbps"
    elif command -v pkg_add >/dev/null 2>&1; then
        PACKAGE_MANAGER="pkg_add"
    elif command -v pkg >/dev/null 2>&1; then
        PACKAGE_MANAGER="freebsd-pkg"
    elif command -v brew >/dev/null 2>&1; then
        PACKAGE_MANAGER="brew"
    elif command -v port >/dev/null 2>&1; then
        PACKAGE_MANAGER="macports"
    elif command -v zypper >/dev/null 2>&1; then
        PACKAGE_MANAGER="zypper"
    elif command -v nix-env >/dev/null 2>&1; then
        PACKAGE_MANAGER="nix"
    elif command -v spack >/dev/null 2>&1; then
        PACKAGE_MANAGER="spack"
    elif command -v conda >/dev/null 2>&1; then
        PACKAGE_MANAGER="conda"
    elif command -v apt >/dev/null 2>&1; then
        PACKAGE_MANAGER="apt"
    elif command -v apt-get >/dev/null 2>&1; then
        PACKAGE_MANAGER="apt-get"
    else
        echo "Unsupported package manager" >&2
        exit 1
    fi
fi

# Install packages
echo 'Installing dependencies...'
case "$PACKAGE_MANAGER" in
    apt) $SUDO apt install libgc-dev libunistring-dev binutils libgmp-dev ;;
    apt-get) $SUDO apt-get install libgc-dev libunistring-dev binutils libgmp-dev ;;
    dnf) $SUDO dnf install gc-devel libunistring-devel binutils gmp-devel ;;
    pacman) $SUDO pacman -S gc libunistring binutils gmp ;;
    yay|paru) $PACKAGE_MANAGER -S gc libunistring binutils gmp ;;
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
        echo "Unknown package manager: $PACKAGE_MANAGER" >&2
        exit 1
        ;;
esac

# Choose installation location
default_prefix='/usr/local'
if echo "$PATH" | tr ':' '\n' | grep -qx "$HOME/.local/bin"; then
    default_prefix="~/.local"
fi
printf '\033[1mChoose where to install Tomo (default: %s):\033[m ' "$default_prefix"
read DEST </dev/tty
if [ -z "$DEST" ]; then DEST="$default_prefix"; fi
DEST="${DEST/#\~/$HOME}"

# Install
if ! [ -w "$DEST" ]; then
    USER="$(ls -ld "$DEST" | awk '{print $$3}')"
    $(SUDO) -u "$USER" tar -xzf "$FILE" -C "$DEST" --strip-components=1 "tomo@$TAG"
else
    tar -xzf "$FILE" -C "$DEST" --strip-components=1 "tomo@$TAG"
fi
echo "Installed to $DEST"

rm -f "$FILE" "$FILE.sha256"
