# Live demo: a Mandelbrot set, computed in C → WebAssembly → the browser

C renders the fractal into WebAssembly **linear memory**; JavaScript reads those exact bytes through a typed-array view and paints them onto a `<canvas>`. **Click the image to zoom.** Raise the detail slider and watch the render time climb: real compute, in a sandbox, next to your JavaScript.

## Run it

```bash
# 1. (Optional) Compile C to WASM. fractal.wasm is checked in, so you can skip this.
./build.sh

# 2. Serve the folder (WASM must load over http, not file://)
python3 -m http.server 8000

# 3. Open http://localhost:8000 and click the fractal to zoom
```

## Files

- `fractal.c` — `render()` writes RGBA pixels into a static buffer in linear memory; `buffer()`, `width()`, `height()` tell JS where it is; `add()` is the function from the slides.
- `build.sh` — `clang --target=wasm32 -O3 -nostdlib` → `fractal.wasm`, then an optional `wasm-opt -Oz`.
- `index.html` — one `fetch`, one `WebAssembly.instantiateStreaming`, then calls `render()`, reads the pixels out of `WebAssembly.Memory` and paints the canvas. Click-to-zoom, detail slider, "Surprise me", live render time.
- `fractal.wasm` — the built module (947 bytes). The 1.3 MB pixel buffer is zero-initialized memory, not bytes in the file.

## What it shows

- **The memory story.** JS does not receive the pixels; it creates a `Uint8ClampedArray` view onto the same `ArrayBuffer` the C code wrote into. The only copy is onto the canvas.
- **The boundary.** One call into WASM per frame does all the work. Cross once, work a lot.
- **The cost of compute.** Deeper zoom and higher detail mean more iterations per pixel. The number on screen is honest.

## Toolchain

- macOS: `brew install llvm lld binaryen` and `export CLANG="$(brew --prefix llvm)/bin/clang"`. Apple's clang has no WebAssembly backend, and `lld` (which provides `wasm-ld`) is a separate Homebrew formula.
- Linux: `sudo apt-get install clang lld binaryen`.

If you'd rather use Emscripten:

```bash
emcc fractal.c -O3 \
  -s EXPORTED_FUNCTIONS='["_render","_buffer","_width","_height","_add"]' \
  -s EXPORTED_RUNTIME_METHODS='["cwrap"]' -s ALLOW_MEMORY_GROWTH=1 -o fractal.js
```

and load `fractal.js` instead (it fetches the wasm for you).

## Tips

- Click on the **edge** of the set. Clicking inside the black region zooms into more black.
- "Surprise me" jumps to five coordinates that always look good.
- The render time barely moves with zoom at the default detail; the **detail slider** is where the milliseconds climb.
