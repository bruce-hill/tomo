#!/bin/sh
TOMO_PREFIX="$(awk -F= '/PREFIX/{print $2}' config.mk)"
cd "$TOMO_PREFIX/bin"
top_version="$(printf '%s\n' 'tomo@'* | sort -r | head -1)"
ln -fs "$top_version" tomo
