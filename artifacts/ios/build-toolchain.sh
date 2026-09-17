#!/bin/sh
# shellcheck disable=2086
set -e

[ "${0%/*}" = "$0" ] && scriptroot="." || scriptroot="${0%/*}"
cd "$scriptroot"

workdir="$PWD/build"
mkdir -p "$workdir"
cd "$workdir"

# Increase this if we ever make a change to the SDK, for example
# using a newer SDK version, and we need to invalidate the cache.
sdkver=1
mkdir -p sdks
if ! [ -d sdks/ios8-sdk ] || [ "$(cat sdks/sdkver 2>/dev/null)" != "$sdkver" ]; then
    # The iOS 8 SDK supports arm64, armv7s, and armv7 and is small.
    # It also doesn't use tbd stubs so we don't need to link ld64 with libtapi.
    printf '\nDownloading iOS SDK...\n\n'
    [ -d sdks/ios8-sdk ] && rm -rf sdks/ios8-sdk
    rm -f iPhoneOS8.0.sdk.tar.lzma
    wget https://invoxiplaygames.uk/sdks/iPhoneOS8.0.sdk.tar.lzma
    tar -x --lzma -f iPhoneOS8.0.sdk.tar.lzma
    mv iPhoneOS8.0.sdk sdks/ios8-sdk
    rm iPhoneOS8.0.sdk.tar.lzma
    printf '%s' "$sdkver" > sdks/sdkver
    cp ../SDKSettings.json sdks/ios8-sdk
fi

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

make() {
    command "$_MAKE" "$@"
}

if [ -z "$LLVM_CONFIG" ]; then
    if command -v llvm-config >/dev/null; then
        export LLVM_CONFIG=llvm-config
    else
        export LLVM_CONFIG=false
    fi
fi

# toolchainver should be increased if we ever make a change to the toolchain,
# for example using a newer cctools version, and we need to invalidate the cache.
toolchainver=2
if [ "$(cat toolchain/toolchainver 2>/dev/null)" != "$toolchainver" ]; then
    outdated_toolchain=1
fi

# invalidate toolchain cache if settings change
"$LLVM_CONFIG" --version > toolchainsettings || true
if ! cmp -s toolchainsettings toolchain/lasttoolchainsettings; then
    outdated_toolchain=1
fi

if [ -z "$outdated_toolchain" ]; then
    printf 'Toolchain already built! :)\n'
    exit 0
fi

rm -rf toolchain
mkdir -p toolchain/bin
mv toolchainsettings toolchain/lasttoolchainsettings

printf '\nBuilding toolchain...\n\n'

# this step is needed even on macOS since newer versions of Xcode will straight up not let you link for old iOS versions anymore
cctools_commit=fee8115127bb849d7481ea0015f181d3ebbd33cf
rm -rf cctools-port-*
wget -O- "https://github.com/Un1q32/cctools-port/archive/$cctools_commit.tar.gz" | tar -xz

cd "cctools-port-$cctools_commit/cctools"
./configure \
    --enable-silent-rules \
    --with-llvm-config="$LLVM_CONFIG"
make -C ld64 -j"$ncpus"
strip ld64/src/ld/ld
mv ld64/src/ld/ld ../../toolchain/bin/ld64.ld64
make -C libmacho -j"$ncpus"
make -C libstuff -j"$ncpus"
make -C misc strip lipo -j"$ncpus"
strip misc/strip misc/lipo
mv misc/strip ../../toolchain/bin/cctools-strip
mv misc/lipo ../../toolchain/bin/lipo
cd ../..
rm -rf "cctools-port-$cctools_commit" &

if [ "$(uname -s)" != "Darwin" ] && ! command -v ldid >/dev/null; then
    printf '\nBuilding ldid...\n\n'

    ldid_commit=ef330422ef001ef2aa5792f4c6970d69f3c1f478
    rm -rf ldid-*
    wget -O- "https://github.com/ProcursusTeam/ldid/archive/$ldid_commit.tar.gz" | tar -xz

    cd "ldid-$ldid_commit"
    make
    strip ldid
    mv ldid ../toolchain/bin
    cd ..
    rm -rf "ldid-$ldid_commit" &
fi

ln -s ../../../ios-cc toolchain/bin

printf '%s' "$toolchainver" > toolchain/toolchainver
wait
