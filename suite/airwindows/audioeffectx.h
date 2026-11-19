// (SF-000, the ninety-first build) A stand-in for the VST 2 header the Airwindows sources are written against: just
// enough of AudioEffectX for a kernel to be made, told the sample rate, given its controls and run. The Airwindows
// files beside this one are as they are in github.com/airwindows/airwindows (MIT: LICENSE, here), unedited; they are
// compiled each inside a namespace of its own by suite/modules/Airwindows.cpp, which is all that includes this.
#ifndef __audioeffect__
#define __audioeffect__

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <math.h>
#include <set>
#include <string>

typedef int32_t VstInt32;
typedef int VstPlugCategory;
typedef void* audioMasterCallback;
enum { kPlugCategEffect = 1 };
enum { kVstMaxProgNameLen = 24, kVstMaxParamStrLen = 64, kVstMaxProductStrLen = 64, kVstMaxVendorStrLen = 64 };

inline void vst_strncpy (char* to, const char* from, size_t most) { std::strncpy (to, from, most); to[most] = 0; }

class AudioEffect
{
public:
    virtual ~AudioEffect() {}
};

class AudioEffectX : public AudioEffect
{
public:
    AudioEffectX (audioMasterCallback, VstInt32, VstInt32) {}
    void setNumInputs (VstInt32) {}
    void setNumOutputs (VstInt32) {}
    void setUniqueID (VstInt32) {}
    void canProcessReplacing (bool = true) {}
    void canDoubleReplacing (bool = true) {}
    void programsAreChunks (bool = true) {}
    float getSampleRate() const { return (float) sfRate; }
    void float2string (float v, char* text, VstInt32 most) { std::snprintf (text, (size_t) most, "%.4f", (double) v); }
    void dB2string (float v, char* text, VstInt32 most) { std::snprintf (text, (size_t) most, "%.2f", v <= 0.0f ? -100.0 : 20.0 * std::log10 ((double) v)); }
    void int2string (VstInt32 v, char* text, VstInt32 most) { std::snprintf (text, (size_t) most, "%d", (int) v); }
    virtual void processReplacing (float** inputs, float** outputs, VstInt32 sampleFrames) = 0;
    virtual void setParameter (VstInt32 index, float value) = 0;
    double sfRate = 44100.0;        // (SF-000: the rate it runs at)
};

#endif
