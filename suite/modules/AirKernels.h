#pragma once
// (SF-000, the ninety-first build) The Airwindows kernels SF-000 carries (suite/airwindows: Chris Johnson's, MIT, as they
// are in his repository), behind one small interface. AirKernels.cpp is the one file that compiles them; Airwindows.cpp
// makes SF-000's modules of them.
#include <cstddef>

namespace sf::air
{
enum Kind { Weave, DeRez5, Dattorro, TakeCare, ToTape9, ADClip9, Galactic3, kKinds };

struct Kernel
{
    virtual ~Kernel() = default;
    // As new, at this rate: every line and filter empty, every control where the kernel starts it. (The kernel is made
    // again in the memory it has; the few small allocations its constructor makes are all there is.)
    virtual void fresh (double rate) = 0;
    virtual void set (int index, float value) = 0;         // a control, 0..1 (the kernel's A is 0)
    virtual void run (float** in, float** out, int n) = 0; // two channels; in and out may be the same
    virtual size_t bytes() const = 0;                       // what one of it holds
};

Kernel* make (Kind);     // (the caller's to delete)
}
