// TITLE: Computed at build time
// BLURB: The same colour, two ways. red_runtime() does the arithmetic every call. red() reads a table the compiler filled in: 768 bytes in the .wasm data section, no math left. Constant folding, taken to its limit.
using u8 = unsigned char;

struct Palette { u8 rgb[256][3]; };

constexpr Palette makePalette() {
    Palette p{};
    for (int i = 0; i < 256; i++) {
        double t = i / 255.0;
        p.rgb[i][0] = (u8)(9.0  * (1 - t) * t * t * t * 255.0);
        p.rgb[i][1] = (u8)(15.0 * (1 - t) * (1 - t) * t * t * 255.0);
        p.rgb[i][2] = (u8)(8.5  * (1 - t) * (1 - t) * (1 - t) * t * 255.0);
    }
    return p;
}
static constexpr Palette PAL = makePalette();

extern "C" int red(int i) {
    return PAL.rgb[i & 255][0];
}

extern "C" int red_runtime(int i) {
    double t = (i & 255) / 255.0;
    return (u8)(9.0 * (1 - t) * t * t * t * 255.0);
}
