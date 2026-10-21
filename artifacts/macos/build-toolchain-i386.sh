#!/bin/sh
# shellcheck disable=2086
set -e

# cd to the directory this script is in
[ "${0%/*}" = "$0" ] && scriptroot="." || scriptroot="${0%/*}"
cd "$scriptroot"

platformdir=$PWD

workdir="$PWD/build"
sdk="$workdir/sdk"
./dlsdk.sh "$sdk" || exit 1
cd "$workdir"

if command -v nproc >/dev/null; then
    ncpus="$(nproc)"
else
    ncpus="$(sysctl -n hw.ncpu)"
fi

for dep in clang make cmp; do
    if ! command -v "$dep" >/dev/null; then
        printf '%s not found!\n' "$dep"
        exit 1
    fi
done

printf '%s' "$workdir" > workdir
if ! cmp -s workdir lastworkdir; then
    rm -rf toolchain-i386
fi
mv workdir lastworkdir

mkdir -p toolchain-i386/bin
export PATH="$PWD/toolchain-i386/bin:$PATH"

# Increase this if we ever make a change to the toolchain, for example
# using a newer GCC version, and we need to invalidate the cache.
ppctoolchainver=3
triple='powerpc-apple-darwin8'
if [ "$(cat toolchain-i386/toolchainver 2>/dev/null)" != "$ppctoolchainver" ]; then
    printf '\nBuilding powerpc toolchain...\n\n'

    rm -rf toolchain-i386
    mkdir -p toolchain-i386/bin

    cctools_commit=264424571c57ad345b8db3fda347a747e04ef160
    rm -rf cctools-port-*
    wget -O- "https://github.com/Un1q32/cctools-port/archive/$cctools_commit.tar.gz" | tar -xz

    cd "cctools-port-$cctools_commit/cctools"
    ./configure \
        --target=i386 \
        --enable-silent-rules \
        --with-llvm-config="${LLVM_CONFIG:-llvm-config}"
    make -C ld64 -j"$ncpus"
    strip ld64/src/ld/ld
    mv ld64/src/ld/ld ../../toolchain-i386/bin/i386-apple-darwin8-ld
    cd ../..
    rm -rf "cctools-port-$cctools_commit" &

    cp "$platformdir/clang-wrapper-i386.sh" toolchain-i386/bin/i386-apple-darwin8-gcc

    rm -rf toolchain-i386/share
    printf '%s' "$ppctoolchainver" > toolchain-i386/toolchainver
    wait
else
    printf 'Toolchain already built! :)\n'
fi
