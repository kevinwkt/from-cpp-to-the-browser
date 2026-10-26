#!/usr/bin/env bash
# Regenerates every compiler snippet shown on the slides from add.cpp / add.c.
# Needs clang with the wasm32 target, wasm-ld (lld) and wasm2wat (wabt).
#   macOS: brew install llvm lld wabt && export CLANG="$(brew --prefix llvm)/bin/clang"
set -euo pipefail
cd "$(dirname "$0")"
CLANG="${CLANG:-clang}"; CLANGXX="${CLANG%clang}clang++"

"$CLANGXX" --target=wasm32 -O0 -S -emit-llvm -o add-O0.ll add.cpp      # slide 8
"$CLANGXX" --target=wasm32 -O2 -S -emit-llvm -o add-O2.ll add.cpp      # slide 11
"$CLANG"   --target=wasm32 -O2 -nostdlib -Wl,--no-entry -Wl,--export=add -o add.wasm add.c
wasm2wat add.wasm -o add.wat                                           # slide 13
"$CLANG"   --target=wasm32 -O2 -nostdlib -Wl,--no-entry -Wl,--export=add -Wl,--strip-all -o add-stripped.wasm add.c

echo "add.wasm:          $(wc -c < add.wasm | tr -d ' ') bytes"
echo "add-stripped.wasm: $(wc -c < add-stripped.wasm | tr -d ' ') bytes"
"$CLANG" --version | head -1
