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

config() {
    printf '%s\n' "$1" >> config.mk
}

printgreen() {
    if [ -z "$NO_COLOR" ] && [ -t 1 ]; then
        printf '\033[1;32m%s\033[0m\n' "$1"
    else
        printf '%s\n' "$1"
    fi
}

printred() {
    if [ -z "$NO_COLOR" ] && [ -t 1 ]; then
        printf '\033[1;31m%s\033[0m\n' "$1"
    else
        printf '%s\n' "$1"
    fi
}

printyes() {
    printgreen 'yes'
}

printno() {
    printred 'no'
}

checklog() {
    printf "checking %s: " "$1"
}

define() {
    config "DEFINES += \$(DEFINE)$1"
}

include() {
    config "INCLUDES += \$(INCLUDE)$1"
}

check() {
    checklog "$1"
    srcname=$2
    shift
    shift
    output="$output_exe"
    [ -n "$nolink" ] && output="$compile_obj $output_obj" && nolink=
    if $CC $cflags ${srcflag}tmp/${srcname}.c ${output}tmp/a.out "$@" > /dev/null 2>&1; then
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

checkend() {
    wait "$2"
    ret=$?
    checklog "$1"
    if [ "$ret" = 0 ]; then
        printyes
    else
        printno
    fi
    return "$ret"
}

printf '%s' "\
int main(void){return 0;}
" > tmp/nothing.c

checklog 'the C compiler CLI syntax'
if $CC /nologo tmp/nothing.c /Fetmp/a.out > /dev/null 2>&1; then
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
elif $CC tmp/nothing.c -o tmp/a.out > /dev/null 2>&1; then
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

checklog 'if we are cross compiling'
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

checklog 'the target OS'
if checkdefine '_WIN32' > /dev/null; then
    printgreen 'windows'
    config 'OS := Windows'
elif checkdefine '__APPLE__' > /dev/null; then
    printgreen 'darwin'
    config 'OS := Darwin'
else
    printgreen 'unix'
fi

if [ -z "$cross_compiling" ] && [ "$syntax" != 'msvc' ]; then
    checklog 'if /usr/X11R6/include exists'
    if [ -d /usr/X11R6/include ]; then
        printyes
        include '/usr/X11R6/include'
    else
        printno
    fi

    checklog 'if /usr/X11R6/lib exists'
    if [ -d /usr/X11R6/lib ]; then
        printyes
        config 'LIBS += -L/usr/X11R6/lib'
    else
        printno
    fi
fi

if [ "$syntax" = 'gcc' ] && nolink=1 check 'if the compiler supports -fno-builtin' nothing -fno-builtin; then
    # function tests might have false positives without this
    cflags='-fno-builtin'
fi

if [ "$syntax" = 'gcc' ]; then
    nolink=1 check '' nothing -MMD -MP -MF tmp/test.d > /dev/null &
    mmd_pid=$!
fi

if [ "$syntax" != 'msvc' ]; then
    check '' nothing -lrt > /dev/null &
    librt_pid=$!
    check '' nothing -ldl > /dev/null &
    libdl_pid=$!
fi

printf '%s' "\
#include <stdint.h>
int main(void){return 0;}
" > tmp/stdint.c

nolink=1 check '' stdint > /dev/null &
stdint_pid=$!

printf '%s' "\
#include <stdbool.h>
int main(void){return 0;}
" > tmp/stdbool.c

nolink=1 check '' stdbool > /dev/null &
stdbool_pid=$!

printf '%s' "\
#include <strings.h>
int main(void){return 0;}
" > tmp/strings.c

nolink=1 check '' strings > /dev/null &
strings_pid=$!

printf '%s' "\
#include <stdio.h>
int main(void){
    puts(__func__);
    return 0;
}
" > tmp/__func__.c

nolink=1 check '' __func__ > /dev/null &
func_pid=$!

printf '%s' "\
#include <math.h>
int main(void){return fmin(0,0);}
" > tmp/fmin.c

check '' fmin $lm > /dev/null &
fmin_pid=$!

printf '%s' "\
#include <math.h>
int main(void){return fmax(0,0);}
" > tmp/fmax.c

check '' fmax $lm > /dev/null &
fmax_pid=$!

printf '%s' "\
#include <math.h>
int main(void){return round(0);}
" > tmp/round.c

check '' round $lm > /dev/null &
round_pid=$!

printf '%s' "\
#include <math.h>
int main(void){return log2(1);}
" > tmp/log2.c

check '' log2 $lm > /dev/null &
log2_pid=$!

printf '%s' "\
#include <math.h>
int main(void){return lround(0);}
" > tmp/lround.c

check '' lround $lm > /dev/null &
lround_pid=$!

printf '%s' "\
#include <math.h>
int main(void){return sqrtf(0);}
" > tmp/sqrtf.c

check '' sqrtf $lm > /dev/null &
sqrtf_pid=$!

printf '%s' "\
#include <math.h>
int main(void){return fabsf(0);}
" > tmp/fabsf.c

check '' fabsf $lm > /dev/null &
fabsf_pid=$!

printf '%s' "\
#include <math.h>
int main(void){return fmodf(1,1);}
" > tmp/fmodf.c

check '' fmodf $lm > /dev/null &
fmodf_pid=$!

printf '%s' "\
#include <math.h>
int main(void){return sinf(0);}
" > tmp/sinf.c

check '' sinf $lm > /dev/null &
sinf_pid=$!

printf '%s' "\
#include <math.h>
int main(void){return cosf(0);}
" > tmp/cosf.c

check '' cosf $lm > /dev/null &
cosf_pid=$!

printf '%s' "\
#include <math.h>
int main(void){return floorf(0);}
" > tmp/floorf.c

check '' floorf $lm > /dev/null &
floorf_pid=$!

printf '%s' "\
#include <math.h>
int main(void){return roundf(0);}
" > tmp/roundf.c

check '' roundf $lm > /dev/null &
roundf_pid=$!

printf '%s' "\
#include <math.h>
int main(void){return isinf(0.0);}
" > tmp/isinf.c

check '' isinf $lm > /dev/null &
isinf_pid=$!

printf '%s' "\
#include <math.h>
int main(void){return isnan(0.0);}
" > tmp/isnan.c

check '' isnan $lm > /dev/null &
isnan_pid=$!

printf '%s' "\
#include <string.h>
int main(void){
    char *saveptr;
    strtok_r(NULL, \"\", &saveptr);
    return 0;
}
" > tmp/strtok_r.c

check '' strtok_r > /dev/null &
strtok_r_pid=$!

printf '%s' "\
#include <getopt.h>
int main(int argc,char *argv[]){
    static struct option opts[]={{0,0,0,0}};
    int idx=0;
    getopt_long(argc,argv,\"\",opts,&idx);
    return 0;
}
" > tmp/getopt_long.c

check '' getopt_long > /dev/null &
getopt_long_pid=$!

printf '%s' "\
#include <stdio.h>
int main(void){
    char buf[8];
    return snprintf(buf, sizeof(buf), \"test\");
}
" > tmp/snprintf.c

check '' snprintf > /dev/null &
snprintf_pid=$!

if [ "$syntax" != 'gcc' ] || ! checkend 'if the compiler supports -MMD -MP -MF test.d' "$mmd_pid"; then
    config 'DISABLE_MMD := 1'
fi

if [ "$syntax" != 'msvc' ]; then
    # sometimes needed for clock_gettime
    if checkend 'for librt' "$librt_pid"; then
        config 'LIBS += -lrt'
    fi
    # sometimes needed for glad or miniaudio
    if checkend 'for libdl' "$libdl_pid"; then
        config 'LIBS += -ldl'
    fi
fi

if ! checkend 'if stdint.h works' "$stdint_pid"; then
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

if ! checkend 'if stdbool.h works' "$stdbool_pid"; then
    # Needed for GCC 2.95, where stdbool.h doesn't work in C++ mode
    include 'compat/stdbool'
    config 'HEADERS += compat/stdbool/stdbool.h'
fi

if ! checkend 'if strings.h works' "$strings_pid"; then
    define 'NO_STRINGS_H'
    no_strings_h=1
fi

if ! checkend 'if __func__ works' "$func_pid"; then
    define '__func__=\"unknown\"'
fi

if ! checkend 'for fmin' "$fmin_pid"; then
    define 'NO_FMIN'
fi

if ! checkend 'for fmax' "$fmax_pid"; then
    define 'NO_FMAX'
fi

if ! checkend 'for round' "$round_pid"; then
    define 'NO_ROUND'
fi

if ! checkend 'for log2' "$log2_pid"; then
    define 'NO_LOG2'
fi

if ! checkend 'for lround' "$lround_pid"; then
    define 'NO_LROUND'
fi

if ! checkend 'for sqrtf' "$sqrtf_pid"; then
    define 'NO_SQRTF'
fi

if ! checkend 'for fabsf' "$fabsf_pid"; then
    define 'NO_FABSF'
fi

if ! checkend 'for fmodf' "$fmodf_pid"; then
    define 'NO_FMODF'
fi

if ! checkend 'for sinf' "$sinf_pid"; then
    define 'NO_SINF'
fi

if ! checkend 'for cosf' "$cosf_pid"; then
    define 'NO_COSF'
fi

if ! checkend 'for floorf' "$floorf_pid"; then
    define 'NO_FLOORF'
fi

if ! checkend 'for roundf' "$roundf_pid"; then
    define 'NO_ROUNDF'
fi

if ! checkend 'for isinf' "$isinf_pid"; then
    define 'NO_ISINF'
fi

if ! checkend 'for isnan' "$isnan_pid"; then
    define 'NO_ISNAN'
fi

if ! checkend 'for strtok_r' "$strtok_r_pid"; then
    define 'NO_STRTOK_R'
fi

if ! checkend 'for getopt_long' "$getopt_long_pid"; then
    include 'compat/getopt'
    config 'HEADERS += compat/getopt/getopt.h'
fi

if ! checkend 'for snprintf' "$snprintf_pid"; then
    include 'compat/stdio'
    define 'NO_SNPRINTF'
    config 'SRCS += compat/stdio/printf.c'
    config 'HEADERS += compat/stdio/printf.h'
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

rm -f tmp/*.c *.obj tmp/a.out test/test.d
