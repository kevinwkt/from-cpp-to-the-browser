# The pipeline, precisely

Technical notes behind the talk and the demo. Every claim here is something you can check against the files in this repo: the specimens in `demo/specimens/`, the five builds of `demo/fractal.cpp`, and the output of `demo/gen.py`. Where a claim is engine-specific it says so.

Contents: [Frontend](#1-frontend-clang) · [Middle end](#2-middle-end-the-optimizer) · [Backend](#3-backend-llvm-ir--webassembly) · [Linker](#4-wasm-ld) · [Binaryen](#5-binaryen-wasm-opt) · [Binary format](#6-the-binary-format) · [Engine](#7-inside-the-engine-v8) · [Boundary](#8-the-js--wasm-boundary) · [SIMD](#9-simd) · [Why the race comes out the way it does](#10-why-the-race-comes-out-the-way-it-does) · [Reproducibility](#11-reproducibility-act-ii)

---

## 1. Frontend (Clang)

**What it does.** Lex → parse → AST → semantic analysis → CodeGen → LLVM IR. Everything that is *C++* (templates, overload resolution, constexpr, name mangling, vtable layout) is decided here. The optimizer never sees C++; it sees IR.

**Name mangling.** `int add(int, int)` becomes `_Z3addii` under the Itanium C++ ABI: `_Z` prefix, length-prefixed name `3add`, parameter types `ii`. Overloads get different names, which is the whole point. `extern "C"` turns it off, which is why exported functions are declared that way (or carry `__attribute__((export_name("…")))`, which sets the WASM export name regardless of the symbol).

**Template instantiation is a frontend job.** `Complex<double>` is stamped out by Clang; `escape<double>` too. At -O0 the IR for specimen 02 contains real calls to `_ZNK7ComplexIdEmlES0_` (`Complex<double>::operator*`) and friends: 108 instructions. The "zero-cost abstraction" has not happened yet. It happens in §2.

**constexpr is evaluated by the frontend, not the optimizer.** `static constexpr Palette PAL = makePalette();` runs the loop inside Clang's constant evaluator and emits a 768-byte constant initializer. This is different from constant folding in §2: no optimization level is needed, it is required by the language (specimen 03 shows the table in the IR even at -O0).

**-O0 is literal on purpose.** Every local gets an `alloca`; every use is a `load`; `a * 2` is `mul`. Functions carry `optnone noinline`. This is what makes -O0 debuggable: one source line maps to a few instructions and every variable has an address.

**SSA.** LLVM IR is in static single assignment form: each `%n` is assigned exactly once. At -O0 Clang sidesteps the hard part of SSA by keeping mutable variables in memory (`alloca`), so only temporaries are SSA values. Promoting those allocas to registers, and inserting `phi` nodes where control flow merges, is the first thing the optimizer does.

## 2. Middle end (the optimizer)

**Pass pipeline.** `-O0/-O1/-O2/-O3/-Os/-Oz` select a pipeline of passes over the IR (LLVM's new pass manager). Roughly, -O2 runs: SROA and mem2reg (allocas → SSA), early CSE, the inliner (with a size threshold), instcombine (peephole canonicalization), SimplifyCFG, loop passes (LICM, rotation, unrolling), GVN, SCCP, and at -O2/-O3 the loop and SLP vectorizers. -O3 raises inlining and unrolling thresholds. -Os/-Oz trade speed for size.

**What happened to `add()`** (specimen 01, 9 → 3 instructions):
- `mem2reg` (inside SROA) removed the two allocas, two stores and two loads.
- `instcombine` rewrote `mul nsw i32 %a, 2` to `shl nsw i32 %a, 1`. This is *canonicalization* first and cost second: later passes only need to recognize one form. On modern CPUs the cost difference is small; the engine re-selects instructions anyway. Don't oversell "a shift is cheaper".
- `nsw` ("no signed wrap") is the frontend recording that signed overflow is UB in C++, which lets the optimizer assume it doesn't happen.

**What happened to the template** (specimen 02, 108 → 21): the inliner pasted `operator*`, `operator+`, `norm2` and `escape<double>` into `iterations`; SROA split the `Complex` structs into scalar `double`s; mem2reg made them SSA values with `phi` nodes at the loop header. What is left is one loop of `fmul`/`fadd`/`fcmp`: exactly what you would write by hand in C. That is the mechanism behind "zero-cost abstraction".

**FP contraction.** At -O2 the loop contains `llvm.fmuladd.f64`. Clang's default `-ffp-contract=on` allows `a*b + c` within one expression to become a fused multiply-add *if the target has one*. WebAssembly's core spec has no scalar FMA (relaxed-simd adds `f64x2.relaxed_madd`), so the backend lowers `fmuladd` back to `f64.mul` + `f64.add`. Consequence: all five builds, and the JavaScript port, compute bit-identical pixels. The demo's "identical pixels from all 6" badge depends on this.

**Devirtualization.** Specimen 04 keeps its `call_indirect` because `area(const Shape*)` cannot know the dynamic type. If the optimizer *can* see it (`area(&circle)` after inlining), it replaces the indirect call with a direct one. Whole-program LTO can do more; we aren't using it.

**Vectorization needs a target feature.** The loop vectorizer only produces `v128` operations when `-msimd128` is on. Without it, `-O3` and `-O2` differ mainly in unrolling and inlining thresholds, which is why their render times are within noise of each other.

## 3. Backend (LLVM IR → WebAssembly)

**Instruction selection.** The WebAssembly backend uses SelectionDAG to map IR to WASM opcodes (`i32.add`, `f64.mul`, `i32.load offset=8`, …). Types map directly: `i32`, `i64`, `f32`, `f64`, `v128`; pointers are `i32` on wasm32.

**Why the code reads as push/pop.** After ISel the backend has virtual registers. The `WebAssemblyRegStackify` pass rewrites single-use values into operand-stack form, which is why `add` becomes `local.get 0 / i32.const 1 / i32.shl / local.get 1 / i32.add`: no locals needed. Multi-use values become `local`s (`local.tee` when the value is both stored and left on the stack). Locals are not memory; engines map them to machine registers.

**Structured control flow.** WASM has no `goto`, only `block`/`loop`/`if` with `br` to enclosing labels. `WebAssemblyCFGStackify` turns the IR's arbitrary CFG into that nesting; irreducible loops are made reducible first (`FixIrreducibleControlFlow`). This is the property that lets the engine validate and compile in a single pass (§7).

**Object files.** `clang -c` emits a *relocatable* `.wasm` object with a `linking` custom section and relocations, not a runnable module. That is what `wasm-ld` consumes.

## 4. wasm-ld

LLD's WebAssembly port. It resolves symbols across objects (and static archives), garbage-collects unreferenced functions and data (`--gc-sections` is on by default, which is why nothing survives unless it is exported or reachable), lays out memory, and writes the final module.

**Memory layout** (defaults): static data from `__global_base` = 1024; then `__data_end`; a 64 KiB shadow stack (`-z stack-size`), grown downward via the `__stack_pointer` global; then `__heap_base`. The shadow stack exists because C++ needs address-taken locals and the WASM operand stack is not addressable. At -O0 *every* local is on it, which is why -O0 code does loads and stores for things -O2 keeps in locals.

**Why the fractal module has 23 pages.** `pix[720*480*4]` is 1,382,400 bytes of zero-initialized data (BSS: declared in the memory section, not stored in the file; the whole module is 2 KB), plus the scratch array, data, and the stack: 23 × 64 KiB. Static buffers are BSS, so size on the wire stays tiny.

**Tables and `call_indirect`.** Function pointers in WASM are *indices into a table*, not addresses. Specimen 04's vtables are data segments containing `\01` and `\02`: table indices. `area()` loads the vtable pointer, loads the slot, and executes `call_indirect (type 0)`. The engine checks at runtime that the table entry's signature equals type 0 and traps otherwise. Index 0 is kept null so a null function pointer traps. This is control-flow integrity by construction.

**Exports.** `-Wl,--export=name` or `export_name` attributes. Memory is exported as `memory` by default. `--no-entry` because there is no `main`.

## 5. Binaryen (wasm-opt)

A separate optimizer that works on the *final* WASM module rather than on LLVM IR. It runs after linking, so it sees the whole program and WASM-specific shapes: it can do module-wide DCE, inline across what used to be object-file boundaries, simplify locals and control flow, and apply transforms LLVM cannot express (Asyncify, which rewrites code to make it pausable, is a Binaryen pass). Emscripten runs it automatically for optimized builds. On this demo, `-O3` from LLVM was already tight enough that `wasm-opt -Oz` did not change the size of the simple version. Say "polish", not "the real optimizer".

## 6. The binary format

A module is `\0asm` + version `1`, then sections, each `id | LEB128 size | payload`. Section ids 1–13 are known; 0 is custom (name-prefixed). The explorer colours them. Things worth saying out loud:

- The **function section** holds only type indices; the **code section** holds bodies. The split lets an engine start compiling bodies while later bytes are still downloading (streaming compilation), and compile functions in parallel.
- **Integers are LEB128**: variable-length, 7 bits per byte. `i32.const 65560` costs 3 bytes, not 4.
- The **data section** carries initializers (specimen 03's 770-byte palette). Zero-initialized memory is not in the file.
- **Custom sections** are ignored by engines: `producers` (which compiler made this; it is why `add.wasm` is 219 bytes with Clang 18 and 258 with Clang 23), `target_features`, and `name` (debug names, absent here). `-Wl,--strip-all` removes them: 209 bytes.
- `add()`'s whole code section is 14 bytes; 12 of them are the function body.

## 7. Inside the engine (V8)

**Validation first.** Before anything runs, the engine type-checks every function: it walks the bytecode with an abstract operand stack, checking that every instruction's inputs have the right types and that every `br` targets an enclosing label. Structured control flow makes this a single linear pass. A module that fails validation never executes. This is the first layer of "safe".

**Two compilers, dynamic tiering.** V8 compiles each function with **Liftoff**, a single-pass baseline compiler that emits machine code about as fast as it can read the bytes, so instantiation is near-instant. Each function has an execution budget; when it runs hot, **TurboFan** (optimizing tier; Turboshaft is its newer IR) recompiles it on a background thread and the engine swaps the code in. Only hot functions tier up (dynamic tiering, V8 since 2021). Cold code stays Liftoff forever. SpiderMonkey (Ion/baseline) and JavaScriptCore (BBQ/OMG) have the same shape.

**Linear memory and bounds checks.** Memory is one contiguous `ArrayBuffer`. Every load/store is conceptually bounds-checked. On 64-bit hosts engines reserve a 4 GiB + guard region of virtual address space for each 32-bit memory and make out-of-range pages inaccessible, so the "check" is the hardware page fault, turned into a trap. No explicit check instructions, no measurable cost. The module cannot name any address outside its memory: there is no instruction for it.

**No ambient authority.** A module can only call what it *imports* (our fractal imports nothing: `{}`) and only touch its own memory, tables and globals. The DOM, the network and the file system are reachable only through JavaScript functions you choose to pass in. That is the second layer of "safe", and why "sandboxed" is not marketing.

**Why -O0 is still slow under TurboFan.** -O0 keeps every local on the shadow stack in linear memory. TurboFan can eliminate some redundant loads/stores, but memory operations can alias and must respect bounds and ordering, so most of the traffic stays. Locals, by contrast, are registers. Source-level optimization and engine optimization are complementary; the engine cannot recover what the toolchain threw away.

**`memory.grow()` detaches views.** Growing may move the buffer. The JS API specifies that the old `ArrayBuffer` is detached (length 0) and `memory.buffer` returns a new one. Any `Uint8Array` you held is dead. The lab shows it live.

## 8. The JS ↔ WASM boundary

**Value conversion** (the JS API's ToWebAssemblyValue / ToJSValue): `i32` ↔ Number via ToInt32; `f32`/`f64` ↔ Number; `i64` ↔ **BigInt** (passing a Number to an i64 parameter throws a TypeError); `externref`/`funcref` ↔ any JS value / exported function. Nothing else crosses. No strings, no objects, no arrays.

**So strings and arrays are memory.** Encode (`TextEncoder`), write into linear memory through a typed-array view, pass pointer + length. The callee reads bytes. To return, write bytes and return a pointer + length; the caller decodes. Emscripten's `ccall`/`cwrap`/`UTF8ToString` and wasm-bindgen's glue are exactly this, generated. The lab's `upcase()` leaves bytes ≥ 0x80 alone so multi-byte UTF-8 survives; a real implementation would use a proper Unicode library.

**Cost model.** A call itself is cheap (tens of nanoseconds in modern engines; the JS→WASM call path has been optimized to not need a trampoline for simple signatures). What costs is per-call conversion and copying: encoding strings, building views, copying pixel buffers. Design for *cross once, work a lot*: one `render()` call per frame, not one call per pixel.

**Pixels in the demo.** The C++ writes RGBA into a static buffer; JavaScript creates a `Uint8ClampedArray` *view* on `memory.buffer` at that address (no copy), then copies it into an `ImageData` and `putImageData` copies it to the canvas. One avoidable copy; a production renderer would draw via WebGL from the same bytes.

## 9. SIMD

WASM SIMD (Wasm 2.0) adds a fixed-width 128-bit `v128` type and lane-wise ops. `-msimd128` enables it in clang; `<wasm_simd128.h>` exposes intrinsics. The SIMD build processes two pixels per instruction with `f64x2` lanes. There is no per-lane early exit: both lanes iterate until both have escaped (or `maxIter`), and a lane mask from `f64x2.le` decides which lanes still count. The lane that escaped early keeps computing garbage that is masked out; that waste is why the speedup is ~1.8×, not 2×. Relaxed SIMD (2024) adds FMA and faster conversions with implementation-defined rounding; we don't use it, which keeps the outputs bit-identical.

## 10. Why the race comes out the way it does

Measured on this laptop (Apple Silicon, Chromium 152, 720×480, 250 iterations, best of 3 after warm-up):

| Build | Time | vs -O3 |
|---|---|---|
| JavaScript | 39 ms | 1.07× |
| -O0 | 160 ms | 4.35× |
| -O1 | 41 ms | 1.11× |
| -O2 | 37 ms | 1.01× |
| -O3 | 37 ms | 1.00× |
| -O3 -msimd128 | 20 ms | 0.54× |

Read it honestly on stage:

- **JavaScript ties -O3.** A monomorphic double-precision loop is exactly what TurboFan is best at: after warm-up it emits the same machine code the WASM path does. This is not a failure of WASM; it is the "not always a win" slide, measured. WASM's advantages are elsewhere: no warm-up or deoptimization cliffs, no GC pauses, predictable performance on large codebases, access to SIMD and 64-bit integers, and the ability to compile existing C++ at all.
- **-O0 is 4× slower.** Same source, same engine, same optimizing JIT. The difference is entirely what the toolchain did before the bytes reached the browser (§7, last paragraph). This is the thesis in one bar: the toolchain is the product.
- **-O1 ≈ -O2 ≈ -O3.** The big win is mem2reg + inlining, which -O1 already does. Beyond that the loop is tight; the extra unrolling at -O3 doesn't pay here.
- **SIMD is the only thing that beats the JIT**, because JavaScript has no way to express it.

Timings in a background browser tab are throttled; run the race with the window in front.

## 11. Reproducibility (Act II)

- **Determinism of the output.** All six renderers agree byte-for-byte because IEEE-754 arithmetic is deterministic and no FMA contraction survives to the target. Turn on relaxed-SIMD FMA, or compile for x86 with `-ffast-math`, and they would not. "Same inputs → same bits" is a property you have to defend, not one you get for free.
- **Determinism of the build.** The `producers` section records the exact compiler. Two engineers with different Clang versions get different bytes for the same source (219 vs 258 for `add.wasm`) and, after an optimizer change, different code. Pinned, hermetic toolchains exist so that "it worked on my machine" is not a sentence anyone has to say.
- **Correctness you can trust.** The war story on slide 20 is the failure mode of everything in §2: an optimizer transform that is valid under the IR's rules (`nsw`, aliasing, FP contraction) but not under what the program actually relied on. Bisecting that across a large codebase is only possible because builds are reproducible.

## Sources

- LLVM: [Writing an LLVM Pass](https://llvm.org/docs/WritingAnLLVMNewPMPass.html), [LangRef](https://llvm.org/docs/LangRef.html) (SSA, `nsw`, `fmuladd`), [WebAssembly backend](https://github.com/llvm/llvm-project/tree/main/llvm/lib/Target/WebAssembly)
- [wasm-ld documentation](https://lld.llvm.org/WebAssembly.html) (memory layout, exports, GC)
- [WebAssembly specification](https://webassembly.github.io/spec/core/) (binary format, validation, `call_indirect`), [JS API](https://webassembly.github.io/spec/js-api/) (value conversion, `memory.grow` detaching)
- V8: [Liftoff](https://v8.dev/blog/liftoff), [dynamic tiering](https://v8.dev/blog/wasm-dynamic-tiering), [faster JS-to-Wasm calls (v9.0 notes)](https://v8.dev/blog/v8-release-90), [BigInt and i64](https://v8.dev/features/wasm-bigint)
- [Binaryen](https://github.com/WebAssembly/binaryen), [Emscripten and the LLVM WebAssembly backend](https://v8.dev/blog/emscripten-llvm-wasm)
- [WASM SIMD](https://github.com/WebAssembly/simd), [relaxed SIMD](https://github.com/WebAssembly/relaxed-simd), [Wasm 3.0 announcement](https://webassembly.org/news/2025-09-17-wasm-3.0/)
