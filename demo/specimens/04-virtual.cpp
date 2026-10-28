// TITLE: A virtual call becomes call_indirect
// BLURB: Shape::area() through a base pointer. The vtable is data, the targets live in a function table, and the call is call_indirect with a type: the engine checks the signature at every call. That check is part of what makes it safe.
struct Shape {
    virtual double area() const = 0;
};

struct Circle : Shape {
    double r;
    constexpr Circle(double r) : r(r) {}
    double area() const override { return 3.14159265 * r * r; }
};

struct Square : Shape {
    double s;
    constexpr Square(double s) : s(s) {}
    double area() const override { return s * s; }
};

static Circle circle{1.0};
static Square square{2.0};

extern "C" const Shape* pick(int which) {
    return which ? static_cast<const Shape*>(&square) : static_cast<const Shape*>(&circle);
}

extern "C" double area(const Shape* s) {
    return s->area();          // -> call_indirect
}

// -nostdlib: we provide the one runtime hook a pure virtual needs.
extern "C" void __cxa_pure_virtual() { __builtin_trap(); }
