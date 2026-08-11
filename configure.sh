#!/bin/sh

error() {
    printf "\033[91;1m%s\033[m\n" "$@"
    exit 1
}

default_prefix='/usr/local'
if echo "$PATH" | tr ':' '\n' | grep -qx "$HOME/.local/bin"; then
    default_prefix="$HOME/.local"
fi

printf '\033[1mChoose where to install Tomo (default: %s):\033[m ' "$default_prefix"
read -r PREFIX
if [ -z "$PREFIX" ]; then PREFIX="$default_prefix"; fi

if ! echo "$PATH" | tr ':' '\n' | grep -qx "$PREFIX/bin"; then
    error "Your \$PATH does not include this prefix, so you won't be able to run tomo!" \
        "Please put this in your .profile or .bashrc: export PATH=\"$PREFIX/bin:\$PATH\""
fi

if command -v doas >/dev/null; then
    SUDO=doas
else
    SUDO=sudo
fi

# The Tomo compiler itself is built as a fully static executable (musl libc)
# using `zig cc`. This makes the resulting binary portable and free of runtime
# library dependencies.
if ! command -v zig >/dev/null; then
    error "Tomo is built as a static executable using 'zig cc', but I can't find 'zig' in your \$PATH." \
        "Please install Zig (https://ziglang.org/download/) and make sure it's on your \$PATH."
fi

default_build_cc="zig cc"
printf '\033[1mChoose which compiler to build Tomo with (default: %s):\033[m ' "$default_build_cc"
read -r CC
if [ -z "$CC" ]; then CC="$default_build_cc"; fi

# Note: the build always targets the host platform (a "<arch>-linux-musl" target
# derived automatically). Cross-platform distribution archives are produced with
# `make dist`, which selects each target's platform itself. The installed tomo
# compiles user programs with the Zig toolchain bundled into the installation, so
# there is no separate runtime C compiler to configure.

cat <<END >config.mk
PREFIX=$PREFIX
CC=$CC
SUDO=$SUDO
END
