#!/bin/sh
set -e

# cd to the directory this script is in
[ "${0%/*}" = "$0" ] && scriptroot="." || scriptroot="${0%/*}"
cd "$scriptroot"

platformdir=$PWD

sdk="$1"
mkdir -p "$sdk"

# Increase this if we ever make a change to the SDK, for example
# using a newer SDK version, and we need to invalidate the cache.
sdkver=1
if ! [ -d "$sdk" ] || [ "$(cat "$sdk/sdkver" 2>/dev/null)" != "$sdkver" ]; then
    printf '\nDownloading macOS SDK...\n\n'
    (
    # for old stuff
    [ -d "$sdk" ] && rm -rf "$sdk" &
    rm -f MacOSX10.5.sdk.tar.xz
    wget -q https://github.com/phracker/MacOSX-SDKs/releases/download/11.3/MacOSX10.5.sdk.tar.xz
    wait
    tar -xJf MacOSX10.5.sdk.tar.xz
    mv MacOSX10.5.sdk "$sdk"
    cd "$sdk"
    patch -fNp1 < "$platformdir/leopard-sdk-fix.patch"
    )
    wait
    rm ./*.tar.xz
    printf '%s' "$sdkver" > "$sdk/sdkver"
fi
