#!/bin/sh
exec clang -target i386-apple-darwin8 -isysroot "${0%/*}/../../sdk" "$@"
