#!/bin/sh
TOMO_PREFIX="$(awk -F= '/PREFIX/{print $2}' config.mk)"
cd "$TOMO_PREFIX/bin"

commands="$(ls | awk -F '[v.]' '
    /^tomo_v/{
        if ($2 >= max_major) max_major=$2;
        if ($3 >= max_minor[$2]) max_minor[$2] = $3;
        link_tomo=1
    }
    END {
        for (major in max_minor) {
            if (max_major > 0) print "ln -fvs tomo_v"major"."max_minor[major]" tomo"major
        }
        if (link_tomo) print "ln -fvs tomo_v"max_major"."max_minor[max_major]" tomo"
    }')"
eval "$commands"
