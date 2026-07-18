#!/bin/sh
# shellcheck disable=2086
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

printgreen() {
    if [ -z "$NO_COLOR" ] && [ -t 1 ]; then
        printf '\033[1;32m%s\033[0m\n' "$1"
    else
        printf '%s\n' "$1"
    fi
    printf 'result: %s\n' "$1" >> tmp/config.log
}

printred() {
    if [ -z "$NO_COLOR" ] && [ -t 1 ]; then
        printf '\033[1;31m%s\033[0m\n' "$1"
    else
        printf '%s\n' "$1"
    fi
    printf 'result: %s\n' "$1" >> tmp/config.log
}

printyes() {
    printgreen 'yes'
}

printno() {
    printred 'no'
}

configlog() {
    printf "%s: " "$1"
    printf "%s:\n" "$1" >> tmp/config.log
}

define() {
    config "DEFINES += \$(DEFINE)$1"
}

include() {
    config "INCLUDES += \$(INCLUDE)$1"
}

check() {
    configlog "checking $1"
    shift
    output="$output_exe"
    [ -n "$nolink" ] && output="$compile_obj $output_obj" && nolink=
    printf 'cmd: %s\n' "$CC $cflags tmp/test.c ${output}tmp/a.out $*" >> tmp/config.log
    if $CC $cflags tmp/test.c ${output}tmp/a.out "$@" >> tmp/config.log 2>&1; then
        printyes
        return 0
    else
        printno
        return 1
    fi
}

checkdefine() {
    printf '%s' "\
#ifndef $1
#error not defined
#endif
int main(void){return 0;}
" > tmp/test.c

    nolink=1 check "if $1 is defined"
    return $?
}

printf '%s' "\
int main(void){return 0;}
" > tmp/test.c

configlog 'checking the C compiler CLI syntax'
if $CC /nologo tmp/test.c /Fe:tmp/a.out >> tmp/config.log 2>&1; then
    printgreen 'msvc'
    syntax=msvc
    CC="$CC /nologo"
    cflags='/Oi-' # equivalent to -fno-builtin
    compile_obj='/c'
    output_obj='/Fo:'
    output_exe='/Fe:'
    config "OUTPUT_OBJ := $output_obj"
    config "OUTPUT_EXE := $output_exe"
    config 'MSVC := 1'
    config 'OBJ_EXT := obj'
    config "_CC := \$(CC) /nologo"
    config 'CFLAGS := /O2 /DNDEBUG'
    config 'INCLUDE := /I'
    config 'DEFINE := /D'
elif $CC tmp/test.c -o tmp/a.out >> tmp/config.log 2>&1; then
    printgreen 'gcc'
    syntax=gcc
    lm='-lm'
    compile_obj='-c'
    output_obj='-o '
    output_exe='-o '
    config "OUTPUT_OBJ := -o\$(space)"
    config "OUTPUT_EXE := -o\$(space)"
    config 'OBJ_EXT := o'
    config "_CC := \$(CC)"
    config 'CFLAGS := -O2 -DNDEBUG'
    config 'INCLUDE := -I'
    config 'DEFINE := -D'
else
    printred 'unknown'
    printf 'unable to find a working compiler syntax, this is probably because your compiler is broken.\n'
    rm -f config.mk
    exit 1
fi
config "COMPILE_OBJ := $compile_obj"

configlog 'checking if we are cross compiling'
chmod +x tmp/a.out
if tmp/a.out > /dev/null 2>&1; then
    printno
else
    printyes
    cross_compiling=1
fi

ccname="${CC##*/}"
target="${ccname%-*}"
if [ "$ccname" = "$target" ]; then
    target=
fi

if [ -n "$target" ]; then
    configlog "checking for $target-pkg-config"
    if command -v "$target-pkg-config"; then
        printyes
        config "PKG_CONFIG := $target-pkg-config"
    else
        printno
    fi
fi

configlog 'checking the target OS'
if checkdefine '_WIN32' > /dev/null; then
    printgreen 'windows'
    config 'OS := Windows'
elif checkdefine '__APPLE__' > /dev/null; then
    printgreen 'darwin'
    config 'OS := Darwin'
else
    printgreen 'unix'
fi

printf '%s' "\
int main(void){return 0;}
" > tmp/test.c

if [ "$syntax" != 'msvc' ] && nolink=1 check 'if the compiler supports -fno-builtin' -fno-builtin; then
    # function tests might have false positives without this
    cflags='-fno-builtin'
fi

if [ "$syntax" = 'msvc' ] || ! nolink=1 check 'if the compiler supports -MMD -MP -MF test.d' -MMD -MP -MF tmp/test.d; then
    config 'DISABLE_MMD := 1'
fi
rm -f tmp/test.d

if [ "$syntax" != 'msvc' ] && check 'for librt' -lrt; then
    # sometimes needed for clock_gettime
    config 'LIBS += -lrt'
fi

if [ "$syntax" != 'msvc' ] && check 'for libdl' -ldl; then
    # sometimes needed for glad or miniaudio
    config 'LIBS += -ldl'
fi

if [ -z "$cross_compiling" ] && [ "$syntax" != 'msvc' ]; then
    configlog 'checking if /usr/X11R6/include exists'
    if [ -d /usr/X11R6/include ]; then
        printyes
        include '/usr/X11R6/include'
    else
        printno
    fi

    configlog 'checking if /usr/X11R6/lib exists'
    if [ -d /usr/X11R6/lib ]; then
        printyes
        config 'LIBS += -L/usr/X11R6/lib'
    else
        printno
    fi
fi

printf '%s' "\
#include <stdbool.h>
int main(void){return 0;}
" > tmp/test.c

if ! nolink=1 check 'if stdbool.h works'; then
    # Needed for GCC 2.95, where stdbool.h doesn't work in C++ mode
    include 'compat/stdbool'
fi

printf '%s' "\
#include <stdint.h>
int main(void){return 0;}
" > tmp/test.c

if ! nolink=1 check 'if stdint.h works'; then
    include 'compat/stdint'
    printf '%s' "\
#include <sys/types.h>
int main(void){return 0;}
" > tmp/test.c
    if nolink=1 check 'if sys/types.h works'; then
        define 'HAVE_SYS_TYPES_H'
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
    define '__func__=\"unknown\"'
fi

printf '%s' "\
#include <math.h>
int main(void){return fmin(0,0);}
" > tmp/test.c

if ! check 'for fmin' $lm; then
    define 'NO_FMIN'
fi

printf '%s' "\
#include <math.h>
int main(void){return fmax(0,0);}
" > tmp/test.c

if ! check 'for fmax' $lm; then
    define 'NO_FMAX'
fi

printf '%s' "\
#include <math.h>
int main(void){return round(0);}
" > tmp/test.c

if ! check 'for round' $lm; then
    define 'NO_ROUND'
fi

printf '%s' "\
#include <math.h>
int main(void){return log2(1);}
" > tmp/test.c

if ! check 'for log2' $lm; then
    define 'NO_LOG2'
fi

printf '%s' "\
#include <math.h>
int main(void){return lround(0);}
" > tmp/test.c

if ! check 'for lround' $lm; then
    define 'NO_LROUND'
fi

printf '%s' "\
#include <math.h>
int main(void){return sqrtf(0);}
" > tmp/test.c

if ! check 'for sqrtf' $lm; then
    define 'NO_SQRTF'
fi

printf '%s' "\
#include <math.h>
int main(void){return fabsf(0);}
" > tmp/test.c

if ! check 'for fabsf' $lm; then
    define 'NO_FABSF'
fi

printf '%s' "\
#include <math.h>
int main(void){return fmodf(1,1);}
" > tmp/test.c

if ! check 'for fmodf' $lm; then
    define 'NO_FMODF'
fi

printf '%s' "\
#include <math.h>
int main(void){return sinf(0);}
" > tmp/test.c

if ! check 'for sinf' $lm; then
    define 'NO_SINF'
fi

printf '%s' "\
#include <math.h>
int main(void){return cosf(0);}
" > tmp/test.c

if ! check 'for cosf' $lm; then
    define 'NO_COSF'
fi

printf '%s' "\
#include <math.h>
int main(void){return roundf(0);}
" > tmp/test.c

if ! check 'for roundf' $lm; then
    define 'NO_ROUNDF'
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
    define 'NO_STRTOK_R'
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
    include 'compat/getopt'
fi

rm -f tmp/test.c tmp/a.out test.obj
