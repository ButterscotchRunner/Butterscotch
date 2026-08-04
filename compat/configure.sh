#!/bin/sh
# shellcheck disable=2086
set -e

if [ -z "$CC" ]; then
    printf "Don't run this directly\n"
    exit 1
fi

export MSYS2_ARG_CONV_EXCL='*'

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
    srcname=$2
    shift
    shift
    output="$output_exe"
    [ -n "$nolink" ] && output="$compile_obj $output_obj" && nolink=
    printf 'cmd: %s\n' "$CC $cflags ${srcflag}tmp/${srcname}.c ${output}tmp/a.out $*" >> tmp/config.log
    if $CC $cflags ${srcflag}tmp/${srcname}.c ${output}tmp/a.out "$@" >> tmp/config.log 2>&1; then
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
" > "tmp/checkdefine_$1.c"

    nolink=1 check "if $1 is defined" "checkdefine_$1"
    return $?
}

printf '%s' "\
int main(void){return 0;}
" > tmp/nothing.c

configlog 'checking the C compiler CLI syntax'
if $CC /nologo tmp/nothing.c /Fetmp/a.out >> tmp/config.log 2>&1; then
    printgreen 'msvc'
    syntax=msvc
    CC="$CC /nologo"
    cflags='/Oi-' # equivalent to -fno-builtin
    compile_obj='/c'
    output_obj='/Fo'
    output_exe='/Fe'
    config "OUTPUT_OBJ := $output_obj"
    config "OUTPUT_EXE := $output_exe"
    config 'OBJ_EXT := obj'
    config 'CFLAGS := /O2 /DNDEBUG'
    config 'INCLUDE := /I'
    config 'DEFINE := /D'
elif $CC tmp/nothing.c -o tmp/a.out >> tmp/config.log 2>&1; then
    printgreen 'gcc'
    syntax=gcc
    lm='-lm'
    compile_obj='-c'
    output_obj='-o '
    output_exe='-o '
    config "OUTPUT_OBJ := -o\$(space)"
    config "OUTPUT_EXE := -o\$(space)"
    config 'OBJ_EXT := o'
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
config "SYNTAX := $syntax"

configlog 'checking if we are cross compiling'
chmod +x tmp/a.out
if tmp/a.out > /dev/null 2>&1; then
    printno
else
    printyes
    cross_compiling=1
fi

printf '%s' "\
int main(void){
    int a = 0;
    ++a;
    int b = a;
    return b;
}
" > tmp/mixed.c

if ! nolink=1 check 'if C supports mixed declarations and code' mixed; then
    if [ "$syntax" = 'msvc' ]; then
        # compile all sources as C++
        srcflag='/Tp'
        config 'SRCFLAG := /Tp'
    else
        printf 'Support for mixed declarations and code is required, maybe try building in C++ mode.\n'
        exit 1
    fi
fi

config "_CC := $CC"

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

if [ "$syntax" = 'gcc' ] && nolink=1 check 'if the compiler supports -fno-builtin' nothing -fno-builtin; then
    # function tests might have false positives without this
    cflags='-fno-builtin'
fi

if [ "$syntax" != 'gcc' ] || ! nolink=1 check 'if the compiler supports -MMD -MP -MF test.d' nothing -MMD -MP -MF tmp/test.d; then
    config 'DISABLE_MMD := 1'
fi
rm -f tmp/test.d

if [ "$syntax" != 'msvc' ] && check 'for librt' nothing -lrt; then
    # sometimes needed for clock_gettime
    config 'LIBS += -lrt'
fi

if [ "$syntax" != 'msvc' ] && check 'for libdl' nothing -ldl; then
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
" > tmp/stdbool.c

if ! nolink=1 check 'if stdbool.h works' stdbool; then
    # Needed for GCC 2.95, where stdbool.h doesn't work in C++ mode
    include 'compat/stdbool'
    config 'HEADERS += compat/stdbool/stdbool.h'
fi

printf '%s' "\
#include <stdint.h>
int main(void){return 0;}
" > tmp/stdint.c

if ! nolink=1 check 'if stdint.h works' stdint; then
    include 'compat/stdint'
    config 'HEADERS += compat/stdint/stdint.h'
    if [ "$syntax" != 'msvc' ]; then
        printf '%s' "\
#include <sys/types.h>
int main(void){return 0;}
" > tmp/systypes.c
        if nolink=1 check 'if sys/types.h works' systypes; then
            define 'HAVE_SYS_TYPES_H'
        fi
    fi
fi

printf '%s' "\
#include <strings.h>
int main(void){return 0;}
" > tmp/strings.c

if ! nolink=1 check 'if strings.h works' strings; then
    define 'NO_STRINGS_H'
    no_strings_h=1
fi

printf '%s' "\
#include <stdio.h>
int main(void){
    puts(__func__);
    return 0;
}
" > tmp/__func__.c

if ! check 'if __func__ works' __func__; then
    define '__func__=\"unknown\"'
fi

printf '%s' "\
#include <math.h>
int main(void){return fmin(0,0);}
" > tmp/fmin.c

if ! check 'for fmin' fmin $lm; then
    define 'NO_FMIN'
fi

printf '%s' "\
#include <math.h>
int main(void){return fmax(0,0);}
" > tmp/fmax.c

if ! check 'for fmax' fmax $lm; then
    define 'NO_FMAX'
fi

printf '%s' "\
#include <math.h>
int main(void){return round(0);}
" > tmp/round.c

if ! check 'for round' round $lm; then
    define 'NO_ROUND'
fi

printf '%s' "\
#include <math.h>
int main(void){return log2(1);}
" > tmp/log2.c

if ! check 'for log2' log2 $lm; then
    define 'NO_LOG2'
fi

printf '%s' "\
#include <math.h>
int main(void){return lround(0);}
" > tmp/lround.c

if ! check 'for lround' lround $lm; then
    define 'NO_LROUND'
fi

printf '%s' "\
#include <math.h>
int main(void){return sqrtf(0);}
" > tmp/sqrtf.c

if ! check 'for sqrtf' sqrtf $lm; then
    define 'NO_SQRTF'
fi

printf '%s' "\
#include <math.h>
int main(void){return fabsf(0);}
" > tmp/fabsf.c

if ! check 'for fabsf' fabsf $lm; then
    define 'NO_FABSF'
fi

printf '%s' "\
#include <math.h>
int main(void){return fmodf(1,1);}
" > tmp/fmodf.c

if ! check 'for fmodf' fmodf $lm; then
    define 'NO_FMODF'
fi

printf '%s' "\
#include <math.h>
int main(void){return sinf(0);}
" > tmp/sinf.c

if ! check 'for sinf' sinf $lm; then
    define 'NO_SINF'
fi

printf '%s' "\
#include <math.h>
int main(void){return cosf(0);}
" > tmp/cosf.c

if ! check 'for cosf' cosf $lm; then
    define 'NO_COSF'
fi

printf '%s' "\
#include <math.h>
int main(void){return floorf(0);}
" > tmp/floorf.c

if ! check 'for floorf' floorf $lm; then
    define 'NO_FLOORF'
fi

printf '%s' "\
#include <math.h>
int main(void){return roundf(0);}
" > tmp/roundf.c

if ! check 'for roundf' roundf $lm; then
    define 'NO_ROUNDF'
fi

printf '%s' "\
#include <math.h>
int main(void){return isinf(0.0);}
" > tmp/isinf.c

if ! check 'for isinf' isinf $lm; then
    define 'NO_ISINF'
fi

printf '%s' "\
#include <math.h>
int main(void){return isnan(0.0);}
" > tmp/isnan.c

if ! check 'for isnan' isnan $lm; then
    define 'NO_ISNAN'
fi

printf '%s' "\
#include <string.h>
int main(void){
    char *saveptr;
    strtok_r(NULL, \"\", &saveptr);
    return 0;
}
" > tmp/strtok_r.c

if ! check 'for strtok_r' strtok_r; then
    define 'NO_STRTOK_R'
fi

if [ -n "$no_strings_h" ]; then
    printf '#include <string.h>\n' > tmp/strcasecmp.c
else
    printf '#include <strings.h>\n' > tmp/strcasecmp.c
fi

printf '%s' "\
int main(void){
    return strcasecmp(\"\", \"\");
}
" >> tmp/strcasecmp.c

if ! check 'for strcasecmp' strcasecmp; then
    define 'NO_STRCASECMP'
fi

printf '%s' "\
#include <getopt.h>
int main(int argc,char *argv[]){
    static struct option opts[]={{0,0,0,0}};
    int idx=0;
    getopt_long(argc,argv,\"\",opts,&idx);
    return 0;
}
" > tmp/getopt_long.c

if ! check 'for getopt_long' getopt_long; then
    include 'compat/getopt'
    config 'HEADERS += compat/getopt/getopt.h'
fi

printf '%s' "\
#include <stdio.h>
int main(void){
    char buf[8];
    return snprintf(buf, sizeof(buf), \"test\");
}
" > tmp/snprintf.c

if ! check 'for snprintf' snprintf; then
    include 'compat/stdio'
    define 'NO_SNPRINTF'
    config 'SRCS += compat/stdio/printf.c'
    config 'HEADERS += compat/stdio/printf.h'
fi

rm -f tmp/*.c *.obj tmp/a.out
