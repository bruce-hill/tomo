#!/bin/env bash

# if [ "$1" = '--lazy' -a ! -f 'config.mk' ]; then
#     echo "Already configured!"
#     exit 0
# fi

error() {
    printf "\033[31;1m%s\033[m\n" "$@"
    exit 1
}

default_prefix='/usr/local'
if echo "$PATH" | tr ':' '\n' | grep -qx "$HOME/.local/bin"; then
    default_prefix="~/.local"
fi

printf '\033[1mChoose where to install Tomo (default: %s):\033[m ' "$default_prefix"
read PREFIX
if [ -z "$PREFIX" ]; then PREFIX="$default_prefix"; fi
PREFIX="${PREFIX/#\~/$HOME}"

if ! echo "$PATH" | tr ':' '\n' | grep -qx "$PREFIX/bin"; then
    error "Your \$PATH does not include this prefix, so you won't be able to run tomo!" \
        "Please put this in your .profile or .bashrc: export PATH=\"$PREFIX/bin:\$PATH\""
fi

if command -v doas >/dev/null; then
    SUDO=doas
else
    SUDO=sudo
fi

default_cc="cc"
printf '\033[1mChoose which C compiler to use by default (default: %s):\033[m ' "$default_cc"
read DEFAULT_C_COMPILER
if [ -z "$DEFAULT_C_COMPILER" ]; then DEFAULT_C_COMPILER="cc"; fi
DEFAULT_C_COMPILER="${DEFAULT_C_COMPILER/#\~/$HOME}"

printf '\033[1mDo you want to build the compiler with Link Time Optimization (LTO)?\033[m\n\033[2m(This makes building the Tomo compiler slower, but makes running Tomo programs faster)\033[m\n\033[1m[y/N]\033[m '
read USE_LTO
if [ "$USE_LTO" = "y" -o "$USE_LTO" = "Y" ]; then
    if $DEFAULT_C_COMPILER -v 2>&1 | grep -q "gcc version"; then
        LTO="-flto=auto -fno-fat-lto-objects -Wl,-flto";
    elif $DEFAULT_C_COMPILER -v 2>&1 | grep -q "clang version"; then
        LTO="-flto=thin";
    fi
fi

cat <<END >config.mk
PREFIX=$PREFIX
DEFAULT_C_COMPILER=$DEFAULT_C_COMPILER
SUDO=$SUDO
LTO=$LTO
END
