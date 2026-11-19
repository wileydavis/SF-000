// (SF-000, the ninety-first build) The one file that compiles the Airwindows sources in suite/airwindows. They are
// unedited; each is included into a namespace of its own, since every one of them declares the same names at file scope
// (kParamA.., kNumParameters, pinParameter, createEffectInstance), against a stand-in for the VST header they expect
// (suite/airwindows/audioeffectx.h).
//
// Two things are arranged from outside rather than by editing them:
//   - the sample rate: the stand-in's getSampleRate() gives what fresh() was told;
//   - the noise each kernel seeds for itself with rand() when it is made: here rand is a small generator of this file's
//     own, started from the same place for every kernel, so a kernel made twice is the same twice (a render is the same
//     render, and nothing here touches the C library's rand).
#include "AirKernels.h"
#include "../airwindows/audioeffectx.h"
#include <cstdint>
#include <new>

namespace sf::air
{
static thread_local uint32_t seedNow = 0x2545F491u;
static inline int airRand()
{
    seedNow = seedNow * 1664525u + 1013904223u;
    return (int) ((seedNow >> 1) & 0x7fffffffu);
}
}

#if defined (__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Weverything"
#elif defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wmultichar"
 #pragma GCC diagnostic ignored "-Wunused-function"
 #pragma GCC diagnostic ignored "-Wunused-variable"
 #pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif

#define rand ::sf::air::airRand

namespace sf_air_weave
{
#include "../airwindows/Weave/Weave.h"
#include "../airwindows/Weave/Weave.cpp"
#include "../airwindows/Weave/WeaveProc.cpp"
}
namespace sf_air_derez5
{
#include "../airwindows/DeRez5/DeRez5.h"
#include "../airwindows/DeRez5/DeRez5.cpp"
#include "../airwindows/DeRez5/DeRez5Proc.cpp"
}
namespace sf_air_dattorro
{
#include "../airwindows/Dattorro/Dattorro.h"
#include "../airwindows/Dattorro/Dattorro.cpp"
#include "../airwindows/Dattorro/DattorroProc.cpp"
}
namespace sf_air_takecare
{
#include "../airwindows/TakeCare/TakeCare.h"
#include "../airwindows/TakeCare/TakeCare.cpp"
#include "../airwindows/TakeCare/TakeCareProc.cpp"
}
namespace sf_air_totape9
{
#include "../airwindows/ToTape9/ToTape9.h"
#include "../airwindows/ToTape9/ToTape9.cpp"
#include "../airwindows/ToTape9/ToTape9Proc.cpp"
}
namespace sf_air_adclip9
{
#include "../airwindows/ADClip9/ADClip9.h"
#include "../airwindows/ADClip9/ADClip9.cpp"
#include "../airwindows/ADClip9/ADClip9Proc.cpp"
}
namespace sf_air_galactic3
{
#include "../airwindows/Galactic3/Galactic3.h"
#include "../airwindows/Galactic3/Galactic3.cpp"
#include "../airwindows/Galactic3/Galactic3Proc.cpp"
}

#undef rand

#if defined (__clang__)
 #pragma clang diagnostic pop
#elif defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif

namespace sf::air
{
namespace
{
template <class T>
struct Of final : Kernel
{
    Of()                                  { seedNow = 0x2545F491u; k = new T (nullptr); }
    ~Of() override                        { delete k; }
    void fresh (double rate) override     { k->~T(); seedNow = 0x2545F491u; new (k) T (nullptr); k->sfRate = rate; }
    void set (int index, float v) override { k->setParameter (index, v); }
    void run (float** in, float** out, int n) override { k->processReplacing (in, out, n); }
    size_t bytes() const override         { return sizeof (T); }
    T* k = nullptr;
};
}

Kernel* make (Kind kind)
{
    switch (kind)
    {
        case Weave:     return new Of<sf_air_weave::Weave>();
        case DeRez5:    return new Of<sf_air_derez5::DeRez5>();
        case Dattorro:  return new Of<sf_air_dattorro::Dattorro>();
        case TakeCare:  return new Of<sf_air_takecare::TakeCare>();
        case ToTape9:   return new Of<sf_air_totape9::ToTape9>();
        case ADClip9:   return new Of<sf_air_adclip9::ADClip9>();
        case Galactic3: return new Of<sf_air_galactic3::Galactic3>();
        default:        return nullptr;
    }
}
}
