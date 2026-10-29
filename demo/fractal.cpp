// fractal.cpp — the live demo, now in C++.
//
// What the talk wants you to see in this file:
//   1. A template, Complex<T>, with operator overloads. At -O2 it vanishes:
//      the inner loop is the same f64.mul / f64.add you'd write by hand.
//      ("Zero-cost abstraction" is the optimizer's work: monomorphization,
//      inlining, SROA/mem2reg.)
//   2. A constexpr palette. The 256 colours are computed by the compiler and
//      land in the .wasm *data section*. Constant folding, taken to its limit.
//   3. The same source compiled at -O0, -O1, -O2, -O3 and -O3 -msimd128.
//      Same meaning, very different machine. The page races them.
//   4. extern "C" exports with readable names (no _Z… mangling at the boundary),
//      plus a few helpers the "boundary lab" calls: i64 → BigInt, strings and
//      arrays as pointer + length, memory that JS can grow.
//   5. A 64-bit integer hash over the pixel buffer. One i64.mul per byte.
//      JavaScript has no 64-bit integer, so the page races this against
//      BigInt and against a hand-split 32-bit version.
//
// Builds with plain clang++ --target=wasm32 -nostdlib: no libc, no libc++,
// no exceptions, no RTTI. See build.sh.

#define W 720
#define H 480

using u8  = unsigned char;
using i64 = long long;

#define EXPORT(name) extern "C" __attribute__((export_name(name)))

// ---------------------------------------------------------------------------
// 1. A template that disappears at -O2.
// ---------------------------------------------------------------------------
template <typename T>
struct Complex {
    T re, im;
    constexpr Complex operator*(Complex o) const { return {re * o.re - im * o.im, re * o.im + im * o.re}; }
    constexpr Complex operator+(Complex o) const { return {re + o.re, im + o.im}; }
    constexpr T norm2() const { return re * re + im * im; }
};

// z = z^2 + c until |z| > 2 or we give up. Returns the iteration count.
template <typename T>
static inline int escape(Complex<T> c, int maxIter) {
    Complex<T> z{0, 0};
    int it = 0;
    while (z.norm2() <= T(4) && it < maxIter) {
        z = z * z + c;
        ++it;
    }
    return it;
}

// ---------------------------------------------------------------------------
// 2. A palette computed at compile time.
// ---------------------------------------------------------------------------
struct Palette { u8 rgb[256][3]; };

constexpr Palette makePalette() {
    Palette p{};
    for (int i = 0; i < 256; i++) {
        double t = i / 255.0;                       // Bernstein polynomials, no pow/log
        p.rgb[i][0] = (u8)(9.0  * (1 - t) * t * t * t * 255.0);
        p.rgb[i][1] = (u8)(15.0 * (1 - t) * (1 - t) * t * t * 255.0);
        p.rgb[i][2] = (u8)(8.5  * (1 - t) * (1 - t) * (1 - t) * t * 255.0);
    }
    return p;
}
static constexpr Palette PAL = makePalette();      // 768 bytes in the data section

// ---------------------------------------------------------------------------
// The pixel buffer lives in linear memory. WebAssembly zero-initializes it.
// ---------------------------------------------------------------------------
static u8 pix[W * H * 4];

EXPORT("buffer") u8* buffer() { return pix; }
EXPORT("width")  int width()  { return W; }
EXPORT("height") int height() { return H; }

static inline void put(int idx, int it, int maxIter) {
    if (it >= maxIter) {
        pix[idx] = pix[idx + 1] = pix[idx + 2] = 0;   // inside the set: black
    } else {
        const u8* c = PAL.rgb[(it * 255) / maxIter];
        pix[idx] = c[0]; pix[idx + 1] = c[1]; pix[idx + 2] = c[2];
    }
    pix[idx + 3] = 255;
}

#ifdef USE_SIMD
// ---------------------------------------------------------------------------
// 3b. The same loop, two pixels per instruction (f64x2 lanes).
// ---------------------------------------------------------------------------
#include <wasm_simd128.h>

static inline void escape2(double cr0, double cr1, double ci, int maxIter, int& it0, int& it1) {
    const v128_t cr   = wasm_f64x2_make(cr0, cr1);
    const v128_t cim  = wasm_f64x2_splat(ci);
    const v128_t four = wasm_f64x2_splat(4.0);
    const v128_t one  = wasm_f64x2_splat(1.0);
    v128_t zr = wasm_f64x2_splat(0.0), zi = zr, cnt = zr;
    for (int i = 0; i < maxIter; i++) {
        v128_t zr2 = wasm_f64x2_mul(zr, zr);
        v128_t zi2 = wasm_f64x2_mul(zi, zi);
        v128_t active = wasm_f64x2_le(wasm_f64x2_add(zr2, zi2), four);   // lane mask
        if (!wasm_v128_any_true(active)) break;
        cnt = wasm_f64x2_add(cnt, wasm_v128_and(active, one));           // count only live lanes
        v128_t zrzi = wasm_f64x2_mul(zr, zi);
        zr = wasm_f64x2_add(wasm_f64x2_sub(zr2, zi2), cr);
        zi = wasm_f64x2_add(wasm_f64x2_add(zrzi, zrzi), cim);
    }
    it0 = (int)wasm_f64x2_extract_lane(cnt, 0);
    it1 = (int)wasm_f64x2_extract_lane(cnt, 1);
}
#endif

// ---------------------------------------------------------------------------
// 3. Render the set centred at (cx, cy) with half-width `scale`.
//    Returns the average iteration count per pixel ("how much work was that").
// ---------------------------------------------------------------------------
EXPORT("render") int render(double cx, double cy, double scale, int maxIter) {
    const double aspect = (double)H / (double)W;
    i64 total = 0;
    int idx = 0;
    for (int py = 0; py < H; py++) {
        const double y0 = cy + (((double)py / H) * 2.0 - 1.0) * scale * aspect;
#ifdef USE_SIMD
        for (int px = 0; px < W; px += 2) {
            const double x0 = cx + (((double)px / W) * 2.0 - 1.0) * scale;
            const double x1 = cx + (((double)(px + 1) / W) * 2.0 - 1.0) * scale;
            int it0, it1;
            escape2(x0, x1, y0, maxIter, it0, it1);
            total += it0 + it1;
            put(idx, it0, maxIter); idx += 4;
            put(idx, it1, maxIter); idx += 4;
        }
#else
        for (int px = 0; px < W; px++) {
            const double x0 = cx + (((double)px / W) * 2.0 - 1.0) * scale;
            const int it = escape(Complex<double>{x0, y0}, maxIter);
            total += it;
            put(idx, it, maxIter);
            idx += 4;
        }
#endif
    }
    return (int)(total / (W * H));
}

// ---------------------------------------------------------------------------
// 4. The boundary lab.
// ---------------------------------------------------------------------------
// The specimen from the slides.
EXPORT("add") int add(int a, int b) { return a * 2 + b; }

// i64 crosses into JavaScript as a BigInt.
EXPORT("add64") i64 add64(i64 a, i64 b) { return a + b; }

// Arrays and strings are pointer + length. JS writes into this scratch area.
static int scratch_i32[1024];
EXPORT("scratch")     int* scratch()     { return scratch_i32; }
EXPORT("scratch_len") int  scratch_len() { return 1024; }

EXPORT("sum_i32") int sum_i32(const int* p, int n) {
    int s = 0;
    for (int i = 0; i < n; i++) s += p[i];
    return s;
}

// Uppercase ASCII in place. Bytes >= 0x80 (UTF-8 continuation etc.) are left alone,
// so "¡Hola!" stays valid UTF-8.
EXPORT("upcase") int upcase(u8* p, int n) {
    int changed = 0;
    for (int i = 0; i < n; i++)
        if (p[i] >= 'a' && p[i] <= 'z') { p[i] -= 32; changed++; }
    return changed;
}

// ---------------------------------------------------------------------------
// 5. The integer workload: FNV-1a, 64 bits, over the frame.
//    Per byte: xor, then multiply by a 64-bit prime. That multiply is one
//    i64.mul here. In JavaScript it is a BigInt (heap-allocated) or ten
//    16-bit partial products done by hand.
// ---------------------------------------------------------------------------
using u64 = unsigned long long;

EXPORT("fnv64") u64 fnv64(const u8* p, int n, int rounds) {
    u64 h = 14695981039346656037ULL;              // FNV offset basis
    for (int r = 0; r < rounds; r++)
        for (int i = 0; i < n; i++) {
            h ^= p[i];
            h *= 1099511628211ULL;                // FNV prime: 2^40 + 0x1b3
        }
    return h;
}
