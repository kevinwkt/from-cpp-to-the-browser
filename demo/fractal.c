// fractal.c — the fun demo: a Mandelbrot set, computed in C, rendered in the browser.
//
// The interesting part for the talk: render() writes RGBA pixels directly into
// WebAssembly *linear memory*. JavaScript then reads those exact bytes into a
// <canvas>. No copying across a network, no JSON — JS and WASM literally share
// one block of memory. Click to zoom; watch the milliseconds as compute grows.
//
// Builds with plain clang, no libc, no math.h. See build.sh and the README.

#define W 720
#define H 480

// The pixel buffer lives in linear memory. WebAssembly zero-initializes it.
static unsigned char pix[W * H * 4];

// Exports JS uses to find the buffer and its size.
unsigned char *buffer(void) { return pix; }
int width(void)  { return W; }
int height(void) { return H; }

// Render the Mandelbrot set centered at (cx, cy), with half-width `scale`,
// iterating up to maxIter. Returns the average iteration count — a stand-in
// for "how much work did we just do", which we show on stage.
int render(double cx, double cy, double scale, int maxIter) {
    double aspect = (double)H / (double)W;
    int idx = 0;
    long long totalIters = 0;

    for (int py = 0; py < H; py++) {
        double y0 = cy + (((double)py / H) * 2.0 - 1.0) * scale * aspect;
        for (int px = 0; px < W; px++) {
            double x0 = cx + (((double)px / W) * 2.0 - 1.0) * scale;

            // z = z^2 + c, starting at z = 0
            double x = 0.0, y = 0.0;
            int it = 0;
            while (x * x + y * y <= 4.0 && it < maxIter) {
                double xt = x * x - y * y + x0;
                y = 2.0 * x * y + y0;
                x = xt;
                it++;
            }
            totalIters += it;

            unsigned char r, g, b;
            if (it >= maxIter) {
                r = g = b = 0;                 // inside the set: black
            } else {
                // Smooth Bernstein-polynomial palette (no pow/log needed).
                double t = (double)it / (double)maxIter;
                r = (unsigned char)(9.0  * (1 - t) * t * t * t * 255.0);
                g = (unsigned char)(15.0 * (1 - t) * (1 - t) * t * t * 255.0);
                b = (unsigned char)(8.5  * (1 - t) * (1 - t) * (1 - t) * t * 255.0);
            }
            pix[idx++] = r;
            pix[idx++] = g;
            pix[idx++] = b;
            pix[idx++] = 255;                  // alpha
        }
    }
    return (int)(totalIters / (W * H));
}

// The tiny "specimen" function from the slides — handy to call live too.
int add(int a, int b) { return a * 2 + b; }
