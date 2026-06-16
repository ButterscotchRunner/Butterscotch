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
    printf 'cmd: %s\n' "$CC $cflags tmp/test.c -o tmp/a.out $*" >> tmp/config.log
    if $CC $cflags tmp/test.c -o tmp/a.out "$@" >> tmp/config.log 2>&1; then
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
if tmp/a.out > /dev/null 2>&1; then
    printf 'no\n'
    printf 'result: no\n' >> tmp/config.log
else
    printf 'yes\n'
    printf 'result: yes\n' >> tmp/config.log
    cross_compiling=1
fi

if check 'if the compiler supports -fno-builtin' -fno-builtin; then
    # function tests might have false positives without this
    cflags='-fno-builtin'
fi

if ! check 'if the compiler supports -MMD -MP -MF test.d' -MMD -MP -MF tmp/test.d; then
    config 'DISABLE_MMD := 1'
fi
rm -f tmp/test.d

if check 'for librt' -lrt; then
    # sometimes needed for clock_gettime
    config 'LIBS += -lrt'
fi

if check 'for libdl' -ldl; then
    # sometimes needed for glad or miniaudio
    config 'LIBS += -ldl'
fi

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

printf '%s' "\
#include <stdint.h>
int main(void){return 0;}
" > tmp/test.c

if ! check 'if stdint.h works'; then
    config 'INCLUDES += -Icompat/stdint'
    printf '%s' "\
#include <sys/types.h>
int main(void){return 0;}
" > tmp/test.c
    if check 'if sys/types.h works'; then
        config 'DEFINES += -DHAVE_SYS_TYPES_H'
    fi
fi

printf '%s' "\
#include <stdio.h>
int main(void){
    puts(__func__);
    return 0;
}
" > tmp/test.c

if ! check 'if __func__ works'; then
    config 'DEFINES += -D__func__=\"unknown\"'
fi

printf '%s' "\
#include <math.h>
int main(void){return fmin(0,0);}
" > tmp/test.c

if ! check 'for fmin' -lm; then
    config 'DEFINES += -DNO_FMIN'
fi

printf '%s' "\
#include <math.h>
int main(void){return fmax(0,0);}
" > tmp/test.c

if ! check 'for fmax' -lm; then
    config 'DEFINES += -DNO_FMAX'
fi

printf '%s' "\
#include <math.h>
int main(void){return round(0);}
" > tmp/test.c

if ! check 'for round' -lm; then
    config 'DEFINES += -DNO_ROUND'
fi

printf '%s' "\
#include <math.h>
int main(void){return log2(1);}
" > tmp/test.c

if ! check 'for log2' -lm; then
    config 'DEFINES += -DNO_LOG2'
fi

printf '%s' "\
#include <math.h>
int main(void){return lround(0);}
" > tmp/test.c

if ! check 'for lround' -lm; then
    config 'DEFINES += -DNO_LROUND'
fi

printf '%s' "\
#include <math.h>
int main(void){return sqrtf(0);}
" > tmp/test.c

if ! check 'for sqrtf' -lm; then
    config 'DEFINES += -DNO_SQRTF'
fi

printf '%s' "\
#include <math.h>
int main(void){return fabsf(0);}
" > tmp/test.c

if ! check 'for fabsf' -lm; then
    config 'DEFINES += -DNO_FABSF'
fi

printf '%s' "\
#include <math.h>
int main(void){return fmodf(1,1);}
" > tmp/test.c

if ! check 'for fmodf' -lm; then
    config 'DEFINES += -DNO_FMODF'
fi

printf '%s' "\
#include <math.h>
int main(void){return sinf(0);}
" > tmp/test.c

if ! check 'for sinf' -lm; then
    config 'DEFINES += -DNO_SINF'
fi

printf '%s' "\
#include <math.h>
int main(void){return cosf(0);}
" > tmp/test.c

if ! check 'for cosf' -lm; then
    config 'DEFINES += -DNO_COSF'
fi

printf '%s' "\
#include <math.h>
int main(void){return roundf(0);}
" > tmp/test.c

if ! check 'for roundf' -lm; then
    config 'DEFINES += -DNO_ROUNDF'
fi

printf '%s' "\
#include <string.h>
int main(void){
    char *saveptr;
    strtok_r(NULL, \"\", &saveptr);
    return 0;
}
" > tmp/test.c

if ! check 'for strtok_r'; then
    config 'DEFINES += -DNO_STRTOK_R'
fi

printf '%s' "\
#include <getopt.h>
int main(int argc,char *argv[]){
    static struct option opts[]={{0,0,0,0}};
    int idx=0;
    getopt_long(argc,argv,\"\",opts,&idx);
    return 0;
}
" > tmp/test.c

if ! check 'for getopt_long'; then
    config 'INCLUDES += -Icompat/getopt'
fi

rm -f tmp/test.c tmp/a.out
