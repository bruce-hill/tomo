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

default_home="~/.local/share/tomo"
printf '\033[1mChoose where to install Tomo libraries (default: %s):\033[m ' "$default_home"
read TOMO_HOME
if [ -z "$TOMO_HOME" ]; then TOMO_HOME="$default_home"; fi
TOMO_HOME="${TOMO_HOME/#\~/$HOME}"

default_cc="cc"
printf '\033[1mChoose which C compiler to use by default (default: %s):\033[m ' "$default_cc"
read DEFAULT_C_COMPILER
if [ -z "$DEFAULT_C_COMPILER" ]; then DEFAULT_C_COMPILER="cc"; fi
DEFAULT_C_COMPILER="${DEFAULT_C_COMPILER/#\~/$HOME}"

cat <<END >config.mk
PREFIX=$PREFIX
TOMO_HOME=$TOMO_HOME
DEFAULT_C_COMPILER=$DEFAULT_C_COMPILER
END
