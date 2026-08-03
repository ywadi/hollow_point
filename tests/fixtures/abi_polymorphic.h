// A polymorphic engine type, compiled independently into both the engine and
// the module -- which is how any engine base class reaches gameplay code.
//
// Default visibility on the class is what gives the vtable and typeinfo
// external linkage; with -fvisibility=hidden (which both fixtures use) they
// would otherwise be local to each module and RTTI across the boundary could
// not work. Whether that is sufficient under this toolchain is the thing being
// measured.
#ifndef HP_ABI_POLYMORPHIC_H
#define HP_ABI_POLYMORPHIC_H

#if defined(_WIN32)
#  define HP_ABI_TYPE
#else
#  define HP_ABI_TYPE __attribute__((visibility("default")))
#endif

namespace hp_abi {

class HP_ABI_TYPE Base {
public:
    virtual ~Base() = default;
    virtual int tag() const { return 1; }
};

class HP_ABI_TYPE Derived : public Base {
public:
    int tag() const override { return 2; }
};

} // namespace hp_abi
#endif
