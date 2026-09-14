#!/bin/sh
SDKROOT="${0%/*}/../../sdk" exec clang -target i386-apple-darwin8 "$@"
