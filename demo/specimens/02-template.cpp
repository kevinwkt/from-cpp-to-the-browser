// TITLE: A template that disappears
// BLURB: Complex<T> with operator overloads, the fractal's inner loop. At -O0 you see calls to operator* and operator+. At -O2 they are gone: one loop of f64.mul and f64.add. "Zero-cost abstraction" is inlining plus SROA.
template <typename T>
struct Complex {
    T re, im;
    Complex operator*(Complex o) const { return {re * o.re - im * o.im, re * o.im + im * o.re}; }
    Complex operator+(Complex o) const { return {re + o.re, im + o.im}; }
    T norm2() const { return re * re + im * im; }
};

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

extern "C" int iterations(double re, double im, int maxIter) {
    return escape(Complex<double>{re, im}, maxIter);
}
