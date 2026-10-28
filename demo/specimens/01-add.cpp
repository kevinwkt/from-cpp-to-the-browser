// TITLE: The specimen: add()
// BLURB: One line of C++. At -O0 every variable lives in memory and a*2 is a mul. At -O2 it is three instructions and a shift.
extern "C" int add(int a, int b) {
    return a * 2 + b;
}
