#!/bin/bash
set -e

[ "${0%/*}" = "$0" ] && scriptroot="." || scriptroot="${0%/*}"
cd "$scriptroot"

PREFIX="$PWD/build/toolchain"
STAGING_DIR="/tmp/illumos-toolchain-build"
TARGET_TRIPLE="x86_64-pc-solaris2.11"
ILLUMOS_SYSROOT_URL="https://github.com/illumos/sysroot/releases/download/20210501-e0b4275f34-v0/illumos-sysroot-i386-20210501-e0b4275f34-v0.tar.gz"

if command -v nproc > /dev/null; then
    ncpus="$(nproc)"
else
    ncpus="$(sysctl -n hw.ncpu)"
fi

if command -v gmake > /dev/null; then
    MAKE="gmake"
elif command -v make > /dev/null; then
    _make_version="$(command make --version 2>/dev/null)"
    case "$_make_version" in
        (*GNU*) MAKE="make" ;;
        (*)
            printf 'Missing dependency: GNU make\n'
            exit 1
        ;;
    esac
else
    printf 'Missing dependency: GNU make\n'
    exit 1
fi


export PATH="$PREFIX/bin:$PATH"

toolchainver=1
if [ "$(cat "$PREFIX/toolchainver" 2>/dev/null)" = "$toolchainver" ]; then
    printf 'Toolchain already built! :)\n'
    exit 0
fi

mkdir -p "$PREFIX/sysroot" "$STAGING_DIR"
cd "$STAGING_DIR"

echo "Downloading and extracting Illumos sysroot..."
curl -L "$ILLUMOS_SYSROOT_URL" | tar xz -C "$PREFIX/sysroot"

BINUTILS_VER="2.47"
echo "Downloading binutils $BINUTILS_VER..."
rm -rf binutils-*
curl -L "https://ftp.gnu.org/gnu/binutils/binutils-${BINUTILS_VER}.tar.xz" | tar xJ

GCC_VER="16.2.0"
echo "Downloading gcc $GCC_VER..."
rm -rf gcc-*
curl -L "https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VER}/gcc-${GCC_VER}.tar.xz" | tar xJ

echo "Building binutils $BINUTILS_VER..."
mkdir -p build-binutils && cd build-binutils
../binutils-${BINUTILS_VER}/configure \
    --target="$TARGET_TRIPLE" \
    --prefix="$PREFIX" \
    --with-sysroot="$PREFIX/sysroot" \
    --disable-nls

$MAKE -j"$ncpus"
$MAKE install
cd ..

echo "Building GCC $GCC_VER, this'll take a bit..."
cd gcc-${GCC_VER}
./contrib/download_prerequisites
cd ..
mkdir -p build-gcc && cd build-gcc
../gcc-${GCC_VER}/configure \
    --prefix="$PREFIX" \
    --target="$TARGET_TRIPLE" \
    --with-sysroot="$PREFIX/sysroot" \
    --with-native-system-header-dir="/usr/include" \
    --with-gnu-as \
    --with-gnu-ld \
    --disable-shared \
    --disable-libgcov \
    --disable-libgomp \
    --disable-nls \
    --disable-fixincludes \
    --with-system-zlib \
    --enable-languages=c

$MAKE -j"$ncpus" all-gcc all-target-libgcc
$MAKE -j"$ncpus" install-strip-gcc install-strip-target-libgcc
cd ..

echo "Cleaning up..."
rm -rf "$STAGING_DIR"

echo $toolchainver > $PREFIX/toolchainver
echo "Done! You can now use the toolchain by running:"
echo "export PATH=$PREFIX/bin:\$PATH"
echo "export CC=$TARGET_TRIPLE-gcc"
