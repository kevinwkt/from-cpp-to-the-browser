#!/usr/bin/env bash
# build.sh — compile fractal.c to a standalone .wasm using clang's wasm target.
# No Emscripten required. Produces fractal.wasm next to this script.
#
# Requirements: clang with the wasm32 target + wasm-ld (ship with modern LLVM).
#   macOS:  brew install llvm lld binaryen   (lld is a separate formula; it provides wasm-ld)
#           export CLANG="$(brew --prefix llvm)/bin/clang"   # use brewed clang, not Apple's
#   Linux:  sudo apt-get install clang lld binaryen
#
# Usage: ./build.sh
set -euo pipefail
cd "$(dirname "$0")"

CLANG="${CLANG:-clang}"

echo "==> Compiling fractal.c -> fractal.wasm (target: wasm32)"
"$CLANG" \
  --target=wasm32 \
  -O3 \
  -nostdlib \
  -Wl,--no-entry \
  -Wl,--export=render \
  -Wl,--export=buffer \
  -Wl,--export=width \
  -Wl,--export=height \
  -Wl,--export=add \
  -Wl,--export=__heap_base \
  -Wl,--allow-undefined \
  -o fractal.wasm \
  fractal.c

echo "==> Done:"
ls -lh fractal.wasm

# Optional: shrink with Binaryen and show the size win (a nice on-stage detail).
if command -v wasm-opt >/dev/null 2>&1; then
  echo "==> wasm-opt -Oz (Binaryen):"
  wasm-opt -Oz fractal.wasm -o fractal.opt.wasm && mv fractal.opt.wasm fractal.wasm
  ls -lh fractal.wasm
fi

echo "==> Serve this folder and open index.html:"
echo "    python3 -m http.server 8000   # then visit http://localhost:8000"
