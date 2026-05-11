#!/bin/sh
# shellcheck disable=2086
set -e

[ "${0%/*}" = "$0" ] && scriptroot="." || scriptroot="${0%/*}"
cd "$scriptroot"

arch="${ARCH:-x86_64}"
target="$arch-w64-mingw32"

platformdir=$PWD

workdir="$PWD/build"
mkdir -p "$workdir"
cd "$workdir"

if command -v nproc >/dev/null; then
    ncpus="$(nproc)"
else
    ncpus="$(sysctl -n hw.ncpu)"
fi

if command -v gmake > /dev/null; then
    _MAKE="gmake"
elif command -v make > /dev/null; then
    _make_version="$(command make --version 2>/dev/null)"
    case "$_make_version" in
        (*GNU*) _MAKE="make" ;;
        (*)
            printf 'Missing dependency: GNU make\n'
            exit 1
        ;;
    esac
else
    printf 'Missing dependency: GNU make\n'
    exit 1
fi

case $arch in
    (i?86) ;;
    (*)
        if ! command -v cmake > /dev/null; then
            printf 'Missing dependency: cmake\n'
            exit 1
        fi
    ;;
esac

make() {
    command "$_MAKE" "$@"
}

export PATH="$PWD/toolchain-$arch/bin:$PATH"

# toolchainver should be increased if we ever make a change to the toolchain,
# for example using a newer GCC version, and we need to invalidate the cache.
toolchainver=1
if [ "$(cat "toolchain-$arch/toolchainver" 2>/dev/null)" = "$toolchainver" ]; then
    printf 'Toolchain already built! :)\n'
    exit 0
fi

# adapted from https://github.com/DiscordMessenger/dm/blob/master/doc/pentium-toolchain/README.md

case $arch in
    (i?86)
        winnt=0x0400 # Windows NT 4.0 (We actually support lower, but this is the lowest this value is supposed to be)
    ;;
    (x86_64)
        winnt=0x0501 # Windows XP
    ;;
    (arm64|aarch64)
        printf 'aarch64 builds are currently unsupported.\n'
        exit 1
        # winnt=0x0A00 # Windows 10
    ;;
    (*)
        printf 'Unknown architecture!\n'
        exit 1
    ;;
esac

rm -rf "toolchain-$arch"
printf '\nBuilding %s toolchain...\n\n' "$arch"

binutils_version='2.46.0'
rm -rf binutils-*
wget -O- "https://ftp.gnu.org/gnu/binutils/binutils-$binutils_version.tar.xz" | tar -xJ

# The '-Wno-discarded-qualifiers' flag is unsupported on clang but required on gcc 15 to build binutils.
# This will probably be fixed when binutils is updated.
if command -v gcc >/dev/null; then
    cc=gcc
else
    cc=cc
fi
printf 'int nothing;\n' | "$cc" -xc - -c -o /dev/null -Werror -Wno-discarded-qualifiers 2>/dev/null &&
    warn='-Wno-discarded-qualifiers'

cd "binutils-$binutils_version"
./configure \
    --prefix="$workdir/toolchain-$arch" \
    --target="$target" \
    --disable-multilib \
    CFLAGS="-O2 $warn"
make -j"$ncpus"
make -j"$ncpus" install-strip
cd ..
rm -rf "binutils-$binutils_version" &

mingw_version='14.0.0'
rm -rf mingw-w64-*
wget -O- "https://sourceforge.net/projects/mingw-w64/files/mingw-w64/mingw-w64-release/mingw-w64-v$mingw_version.tar.bz2/download" | tar -xj

cd "mingw-w64-v$mingw_version/mingw-w64-headers"
./configure \
    --host="$target" \
    --prefix="$workdir/toolchain-$arch/$target" \
    --with-default-win32-winnt="$winnt" \
    --with-default-msvcrt=crtdll
make -j"$ncpus" install
cd ../..

gcc_version='16.1.0'
rm -rf gcc-*
wget -O- "https://ftp.gnu.org/gnu/gcc/gcc-$gcc_version/gcc-$gcc_version.tar.xz" | tar -xJ

cd "gcc-$gcc_version"
patch -fNp1 < "$platformdir/gcc.diff"
mkdir build
cd build
set --
[ -n "$GMP" ] && set -- --with-gmp="$GMP"
[ -n "$MPFR" ] && set -- "$@" --with-mpfr="$MPFR"
[ -n "$MPC" ] && set -- "$@" --with-mpc="$MPC"
../configure \
    --prefix="$workdir/toolchain-$arch" \
    --target="$target" \
    --disable-shared \
    --disable-libgcov \
    --disable-libgomp \
    --disable-multilib \
    --disable-nls \
    --with-system-zlib \
    --enable-languages=c \
    "$@"
make -j"$ncpus" all-gcc
make -j"$ncpus" install-strip-gcc
cd ../..

cd "mingw-w64-v$mingw_version/mingw-w64-crt"
./configure \
    --host="$target" \
    --prefix="$workdir/toolchain-$arch/$target" \
    --with-default-win32-winnt="$winnt" \
    --with-default-msvcrt=crtdll
make -j1
make -j1 install
cd ../..
rm -rf "mingw-w64-v$mingw_version" &

cd "gcc-$gcc_version/build"
make -j"$ncpus"
make -j"$ncpus" install-strip
cd ../..
rm -rf "gcc-$gcc_version" &

bzip2_version='1.0.8'
rm -rf bzip2-*
wget -O- "https://sourceware.org/pub/bzip2/bzip2-$bzip2_version.tar.gz" | tar -xz

cd "bzip2-$bzip2_version"
make libbz2.a CC="$target-gcc" AR="$target-ar" RANLIB="$target-ranlib" -j"$ncpus"
cp libbz2.a "$workdir/toolchain-$arch/$target/lib"
cp bzlib.h "$workdir/toolchain-$arch/$target/include"
cd ..
rm -rf "bzip2-$bzip2_version" &

glfw2_version='2.7.9'
rm -rf glfw-*
wget -O- "https://github.com/glfw/glfw-legacy/archive/refs/tags/$glfw2_version.tar.gz" | tar -xz

cd "glfw-legacy-$glfw2_version"
make -j"$ncpus" cross-mgw-install \
    TARGET="$target-" \
    PREFIX="$workdir/toolchain-$arch/$target"
cd ..
rm -rf "glfw-legacy-$glfw2_version" &

printf '%s' "$toolchainver" > "toolchain-$arch/toolchainver"
wait
