// TITLE: 64-bit integers: one i64.mul
// BLURB: FNV-1a, the hash the integer race runs. Per byte: xor, then multiply by a 64-bit prime. Look for i64.mul in the WebAssembly: one instruction. JavaScript has no 64-bit integer, so the same multiply is a BigInt or ten partial products.
extern "C" unsigned long long fnv64(const unsigned char* p, int n) {
    unsigned long long h = 14695981039346656037ULL;
    for (int i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}
