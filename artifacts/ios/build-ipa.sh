#!/bin/sh
set -e

apppath="build/Payload/butterscotch.app"

if ! [ -f build/butterscotch ]; then
    printf 'Expected a binary at build/butterscotch\n'
    exit 1
fi

printf 'Creating butterscotch.ipa\n'

rm -rf "$apppath"
mkdir -p "$apppath"
if command -v ldid >/dev/null; then
    ldid -Sartifacts/ios/butterscotch.entitlements build/butterscotch
else
    codesign -f -s - --entitlements artifacts/ios/butterscotch.entitlements build/butterscotch
fi
cp build/butterscotch "$apppath/butterscotch"
cp -a artifacts/ios/assets/* "$apppath"
command -v plistutil >/dev/null && plistutil -i artifacts/ios/assets/Info.plist -o "$apppath/Info.plist" -f bin

cd build
rm -f butterscotch.ipa
zip -qr butterscotch.ipa Payload
