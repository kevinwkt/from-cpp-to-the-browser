#!/usr/bin/env bash
# build.sh — compile fractal.cpp five ways and regenerate the pipeline-explorer data.
#
#   fractal-O0.wasm        no optimization: literal translation of the source
#   fractal-O1.wasm
#   fractal-O2.wasm
#   fractal-O3.wasm
#   fractal-O3-simd.wasm   -O3 -msimd128: two pixels per instruction (f64x2 lanes)
#   pipeline.json          source / LLVM IR (-O0, -O2) / WASM text / bytes for each specimen
#
# Requirements: clang++ with the wasm32 target, wasm-ld (lld), wasm2wat + wasm-objdump (wabt).
#   macOS:  brew install llvm lld wabt && export CLANG="$(brew --prefix llvm)/bin/clang"
#   Linux:  sudo apt-get install clang lld wabt
#
# Usage: ./build.sh
set -euo pipefail
cd "$(dirname "$0")"

CLANG="${CLANG:-clang}"
CXX="${CLANG%clang}clang++"

COMMON=(--target=wasm32 -std=c++17 -nostdlib -fno-exceptions -fno-rtti -Wl,--no-entry)

build() {  # name, flags...
  local name=$1; shift
  "$CXX" "${COMMON[@]}" "$@" -o "fractal-$name.wasm" fractal.cpp
  printf "  fractal-%-8s %6s bytes\n" "$name.wasm" "$(wc -c < "fractal-$name.wasm" | tr -d ' ')"
}

echo "==> Compiling fractal.cpp (target: wasm32)"
build O0      -O0
build O1      -O1
build O2      -O2
build O3      -O3
build O3-simd -O3 -msimd128 -DUSE_SIMD

echo "==> Generating pipeline.json from specimens/"
CLANG="$CLANG" python3 gen.py

echo "==> Serve this folder and open index.html:"
echo "    python3 -m http.server 8000   # then visit http://localhost:8000"
