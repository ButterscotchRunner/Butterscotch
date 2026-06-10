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
: > tmp/config.log

config() {
    printf '%s\n' "$1" >> config.mk
}

check() {
    printf 'checking %s: ' "$1"
    printf 'checking %s:\n' "$1" >> tmp/config.log
    shift
    if $CC tmp/test.c -o tmp/a.out "$@" >> tmp/config.log 2>&1; then
        printf 'yes\n'
        printf 'result: yes\n' >> tmp/config.log
        return 0
    else
        printf 'no\n'
        printf 'result: no\n' >> tmp/config.log
        return 1
    fi
}

printf '%s' "\
int main(void){return 0;}
" > tmp/test.c

check 'if the C compiler works' || exit 1

printf 'checking if we are cross compiling: '
printf 'checking if we are cross compiling:\n' >> tmp/config.log
if tmp/a.out; then
    printf 'no\n'
    printf 'result: no\n' >> tmp/config.log
else
    printf 'yes\n'
    printf 'result: yes\n' >> tmp/config.log
    cross_compiling=1
fi

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

if [ -z "$cross_compiling" ]; then
    printf 'checking if /usr/X11R6/include exists: '
    if [ -d /usr/X11R6/include ]; then
        printf 'yes\n'
        config 'INCLUDES += -I/usr/X11R6/include'
    else
        printf 'no\n'
    fi

    printf 'checking if /usr/X11R6/lib exists: '
    if [ -d /usr/X11R6/lib ]; then
        printf 'yes\n'
        config 'LIBS += -L/usr/X11R6/lib'
    else
        printf 'no\n'
    fi
fi

printf '%s' "\
#include <stdbool.h>
int main(void){return 0;}
" > tmp/test.c

if ! check 'if stdbool.h works'; then
    # Needed for GCC 2.95, where stdbool.h doesn't work in C++ mode
    config 'INCLUDES += -Icompat/stdbool'
fi

rm -f tmp/test.c tmp/a.out
