#!/bin/sh
set -e

if [ -z "$CC" ]; then
    printf "Don't run this directly\n"
    exit 1
fi

# cd to the directory this script is in
[ "${0%/*}" = "$0" ] && scriptroot="." || scriptroot="${0%/*}"
cd "$scriptroot"

: > config.mk

config() {
    printf '%s\n' "$1" >> config.mk
}

check() {
    printf 'checking %s: ' "$1"
    shift
    if $CC tmp/test.c -o tmp/a.out "$@" 2> /dev/null; then
        printf 'yes\n'
        rm -f tmp/a.out
        return 0
    else
        printf 'no\n'
        rm -f tmp/a.out
        return 1
    fi
}

printf '%s' "\
#include <stdbool.h>
int main(void){return 0;}
" > tmp/test.c

if ! check 'if stdbool.h works'; then
    # Needed for GCC 2.95, where stdbool.h doesn't work in C++ mode
    config 'INCLUDES += -Icompat/stdbool'
fi

printf '%s' "\
int main(void){return 0;}
" > tmp/test.c

if check 'for librt' -lrt; then
    # sometimes needed for clock_gettime
    config 'LIBS += -lrt'
fi

if check 'for libdl' -ldl; then
    # sometimes needed for glad
    config 'LIBS += -ldl'
fi

if ! check 'if -MMD -MP -MF test.d works' -MMD -MP -MF tmp/test.d; then
    config 'DISABLE_MMD := 1'
fi
rm -f tmp/test.d

rm -f tmp/test.c
