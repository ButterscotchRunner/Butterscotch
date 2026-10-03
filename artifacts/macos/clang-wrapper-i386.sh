#!/bin/sh
SDKROOT="${0%/*}/../../sdk" exec "${CLANG:-clang}" -target i386-apple-darwin8 "$@"
