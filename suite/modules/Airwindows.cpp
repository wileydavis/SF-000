// (SF-000, the ninety-first build) Seven of Chris Johnson's Airwindows effects as SF-000's own modules: WEAVE (Weave),
// DEREZ (DeRez5), FILTER (Dattorro), ENSEMBLE (TakeCare), TAPE (ToTape9) and CLIP (ADClip9) for a lane's insert slots,
// and GALAXY (Galactic3) as a third reverb SPACE can be. His sources are in suite/airwindows, unedited, under his MIT
// licence (LICENSE, there); AirKernels.cpp compiles them. This file is the one adapter all seven go through.
//
// What the adapter does, the same for each:
//   - the knobs are SF-000's (0..1, a name, a default) and each is one of the kernel's controls, over the part of its
//     range the table gives. A kernel's control that cannot move while it sounds (a delay's length, a reverb's size) is a
//     list in CONFIG instead: lists do not glide, morph, take MOTION or a step lock. A change of it is made at the bottom
//     of a short dip of the wet sound, and the kernel starts again there, empty.
//   - MIX and LEVEL are SF-000's, not the kernel's: MIX is one straight crossfade between the sound as it came in and
//     the kernel's, the same for every one of them, and at nought the kernel is not in the sound at all (to the sample).
//     The kernel's own dry/wet, where it has one, is held at wet. Where the kernel is late by a fixed count of samples
//     (CLIP and TAPE: one), the dry sound waits that long and the count is reported.
//   - a kernel that ever gives something that is not a number is started again and the block is silent (none has: every
//     corner of every one was driven with noise).
//   - PANIC (reset) makes the kernel new in the memory it already has. Its constructor allocates a few short strings, on
//     the audio thread at that moment; nothing else here allocates after prepare.
#include "Kit.h"
#include "AirKernels.h"

namespace sf
{
namespace
{
struct AirKnob
{
    const char* id;
    const char* name;
    int index;                          // the kernel's control (its A is 0)
    float def;                          // where SF-000 starts it: 0..1 of the knob, or the step of a list
    float lo = 0.0f, hi = 1.0f;         // the kernel's value at the knob's two ends
    bool face = true;                   // on the effect's eight knobs (else CONFIG only)
    std::vector<const char*> steps {};  // a list, where there are any...
    std::vector<float> at {};           // ...and the kernel's value at each step
    bool clears = false;                // a list the kernel cannot take while it sounds: it starts again, empty
};

struct AirDef
{
    const char* id;
    air::Kind kind;
    std::vector<AirKnob> knobs;
    std::vector<std::pair<int, float>> held;    // the kernel's controls SF-000 holds (its own dry/wet, at wet)
    float mixDef = 1.0f;
    int lag = 0;                        // the kernel's fixed delay in samples
    bool space = false;                 // SPACE's: DUCK, DUCK SHAPE and LO CUT, and the kick's pocket in its tail
    float trim = 1.0f;                  // (space) on the wet sound, so SPACE is as loud on it as on the REVERB
    double tail = 2.0;
    float levelDef = 0.0f;              // where LEVEL starts, in dB
};

const AirDef& airDef (int which)
{
    static const AirDef defs[] = {
        // Weave: its input is ten times at full, and unity at a tenth: DRIVE is from unity up. Its output at 79 % is
        // where what goes in quietly comes out as loud (the kernel lifts by 1.27); it starts at 30 %, where with DRIVE
        // as it starts (a quarter: 10 dB in) the effect is about as loud as the sound that went in.
        { "AIRWEAVE", air::Weave,
          { { "drive", "DRIVE", 0, 0.25f, 0.1f, 1.0f },
            { "output", "OUTPUT", 2, 0.3f },
            { "smooth", "SMOOTH", 1, 0.5f, 0.0f, 1.0f, false } },
          {}, 1.0f, 0, false, 1.0f, 1.0 },
        // DeRez5: its first control is clean at full; RATE is that the other way up, so up is more, as SF-000's are. At
        // full exactly the kernel steps aside and is a sample earlier than it is anywhere else, so RATE's nought is a
        // hair under that (0.999), where it is the sound as it came in, one sample late, to the last bit: the one sample
        // is the same over the whole knob, the dry sound waits it, and it is reported.
        { "AIRDEREZ", air::DeRez5,
          { { "rate", "RATE", 0, 0.4f, 0.999f, 0.0f },
            { "square", "SQUARE", 2, 0.0f },
            { "trim", "TRIM", 1, 1.0f },
            { "bright", "BRIGHT", 3, 1.0f },
            { "bassis", "BASSIS", 4, 0.0f },
            { "output", "OUTPUT", 5, 1.0f } },
          {}, 1.0f, 1, false, 1.0f, 1.0 },
        // Dattorro: what passes under the cutoff is quieter the higher RESO is (6 dB down half way, where Airwindows
        // starts it, so that the ring at the cutoff is not louder than the sound was). It starts lower, with LEVEL up
        // 2 dB, so that as it starts out it is about as loud as what went in.
        { "AIRFILTER", air::Dattorro,
          { { "freq", "FREQ", 0, 0.7f },
            { "reso", "RESO", 1, 0.3f },
            { "output", "OUTPUT", 2, 1.0f } },
          {}, 1.0f, 0, false, 1.0f, 2.0, 2.0f },
        // TakeCare: nine lines that sweep, feeding each other. BUFFER is the lines' length, which is the effect's time:
        // measured at 48 kHz with DEPTH half way, 25 ms at nought, 50 ms a fifth of the way, 110 ms at three tenths,
        // 0.4 s half way and 3 s at full (the kernel slides there itself, a sample a sample). Airwindows starts it half
        // way and all wet, which is an echo 0.4 s late and nothing else; SF-000 starts it as the chorus it is named for:
        // BUFFER short and MIX half. DEREZ stops a tenth short of the kernel's ends, where it runs at a two-thousandth
        // of the rate and is silent. (To the left the kernel goes in whole steps of the rate, and at 48 kHz the first,
        // a half, is a quarter turn left of the middle: between there and the middle nothing changes. Its own doing.)
        { "AIRENSEMBLE", air::TakeCare,
          { { "speed", "SPEED", 0, 0.15f },
            { "depth", "DEPTH", 2, 0.5f },
            { "buffer", "BUFFER", 5, 0.15f },
            { "regen", "REGEN", 3, 0.5f },
            { "derez", "DEREZ", 4, 0.5f, 0.1f, 0.9f },
            { "output", "OUTPUT", 6, 1.0f },
            { "rando", "RANDO", 1, 0.5f, 0.0f, 1.0f, false } },
          { { 7, 1.0f } }, 0.5f, 0, false, 1.0f, 8.0, 4.0f },      // (LEVEL starts 4 dB up: half dry and half of nine wandering lines is that much quieter than the sound)
        // ToTape9: SHAPE and BIAS stop short of the kernel's ends, where it is not a tape any more (SHAPE's last twentieth
        // either way is 8 and 12 dB louder; BIAS at its top is silence, 17 dB down a twentieth below that).
        { "AIRTAPE", air::ToTape9,
          { { "input", "INPUT", 0, 0.5f },
            { "tilt", "TILT", 1, 0.5f },
            { "shape", "SHAPE", 2, 0.5f, 0.05f, 0.95f },
            { "flutter", "FLUTTER", 3, 0.5f },
            { "head_bump", "HEAD BUMP", 6, 0.5f },
            { "output", "OUTPUT", 8, 0.5f },
            { "flutter_speed", "FLUTTER SPEED", 4, 0.5f, 0.0f, 1.0f, false },
            { "bias", "BIAS", 5, 0.5f, 0.1f, 0.9f, false },
            { "head_freq", "HEAD FREQ", 7, 0.5f, 0.0f, 1.0f, false } },
          {}, 1.0f, 1, false, 1.0f, 1.0 },
        // ADClip9: MODE is its own three (BOOST: louder into the clipper; MATCH: into it by MATCH and back down by the
        // same, so only the clipping is heard; CLIP ONLY: what the clipper took off, alone).
        { "AIRCLIP", air::ADClip9,
          { { "boost", "BOOST", 0, 0.0f },
            { "ceiling", "CEILING", 3, 0.75f },
            { "noise", "NOISE", 2, 0.7f },
            { "match", "MATCH", 1, 0.0f, 0.0f, 1.0f, false },
            { "mode", "MODE", 4, 0.0f, 0.0f, 1.0f, false, { "BOOST", "MATCH", "CLIP ONLY" }, { 0.0f, 0.5f, 1.0f } } },
          {}, 1.0f, 1, false, 1.0f, 1.0 },
        // Galactic3, SPACE's third reverb. Its knobs sit where the other two reverbs have theirs: DECAY is its Replace the
        // other way up (at full nothing new comes in and what is there stays), DARK its Brightness the other way up.
        // BIGNESS is the size of the space, which it cannot change while it sounds. DEREZ is up for more.
        { "AIRGALAXY", air::Galactic3,
          { { "detune", "DETUNE", 2, 0.5f },
            { "decay", "DECAY", 0, 0.5f, 1.0f, 0.0f },
            { "dark", "DARK", 1, 0.5f, 1.0f, 0.0f },
            { "derez", "DEREZ", 3, 0.0f, 1.0f, 0.1f, false },
            { "bigness", "BIGNESS", 4, 3.0f, 0.0f, 1.0f, false, { "SMALL", "MEDIUM", "LARGE", "HUGE" }, { 0.25f, 0.5f, 0.75f, 1.0f }, true } },
          { { 5, 1.0f } }, 0.25f, 0, true, 1.41f, 30.0 },      // (3 dB up: as SPACE starts out, that is where it is as loud as the REVERB)
    };
    return defs[which];
}

class AirModule : public Module
{
public:
    explicit AirModule (int which) : def (airDef (which)), k (air::make (def.kind))
    {
        nK = (int) def.knobs.size();
        int at = nK;
        if (def.space) { DUCK = at++; SHAPE = at++; LOCUT = at++; }
        MIX = at++;
        LEVEL = def.space ? -1 : at++;
        stepNow.assign ((size_t) nK, -1);
    }
    std::string id() const override { return def.id; }
    std::vector<ParamSpec> params() const override
    {
        std::vector<ParamSpec> v;
        for (auto& kn : def.knobs)
        {
            if (! kn.steps.empty())
            {
                std::vector<std::string> names (kn.steps.begin(), kn.steps.end());
                v.push_back (choice (kn.id, kn.name, names, (int) std::lround (kn.def)));
            }
            else
            {
                auto p = pct (kn.id, kn.name, kn.def);
                if (kn.face) p.prim();
                v.push_back (p);
            }
        }
        if (def.space)
        {
            v.push_back (pct ("duck", "DUCK", 0.3f).prim());
            v.push_back (pct ("duck_shape", "DUCK SHAPE", 0.5f));
            v.push_back (knob ("lo_cut", "LO CUT", 20.0f, 800.0f, 120.0f, "Hz", 160.0f));
        }
        v.push_back (pct ("mix", "MIX", def.mixDef).prim());
        if (! def.space) v.push_back (levelParam (def.levelDef).prim());
        return v;
    }
    int latency() const override { return lagNow(); }
    double tailSeconds() const override { return def.tail; }
    void prepare (double rate, int mb) override
    {
        sr = rate;
        wet.setSize (2, std::max (mb, 64), false, true, false);
        for (auto& d : dry) d.prepare (lagNow() + 4);
        if (def.space) pocket.prepare (rate, mb);
        dipLen = std::max (64, (int) std::lround (rate * 0.010));
        reset();
    }
    void reset() override
    {
        k->fresh (sr);
        for (auto& h : def.held) k->set (h.first, h.second);
        std::fill (stepNow.begin(), stepNow.end(), -1);
        primed = false;
        for (auto& d : dry) d.clear();
        for (auto& f : lo) f.reset();
        if (def.space) pocket.reset();
        dip = 0; dipAt = 0;
    }
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        const int n = std::min (c.n, wet.getNumSamples());
        if (n <= 0 || io.getNumChannels() < 1) return;
        auto* L = io.getWritePointer (0);
        auto* R = io.getWritePointer (io.getNumChannels() > 1 ? 1 : 0);
        auto* wl = wet.getWritePointer (0);
        auto* wr = wet.getWritePointer (1);

        // the lists the kernel cannot take as it sounds: a change waits for the bottom of a dip
        bool waiting = false, clears = false;
        for (int i = 0; i < nK; ++i)
        {
            const auto& kn = def.knobs[(size_t) i];
            if (kn.steps.empty()) continue;
            const int step = juce::jlimit (0, (int) kn.steps.size() - 1, c.ci (i));
            if (stepNow[(size_t) i] < 0) stepNow[(size_t) i] = step;
            else if (stepNow[(size_t) i] != step) { waiting = true; clears = clears || kn.clears; }
        }
        if (waiting && dip == 0) { dip = 1; dipAt = 0; }
        if (dip == 1 && dipAt >= dipLen)
        {
            // the bottom: nothing of the kernel is heard here
            if (clears) { k->fresh (sr); for (auto& h : def.held) k->set (h.first, h.second); primed = false; }
            for (int i = 0; i < nK; ++i) if (! def.knobs[(size_t) i].steps.empty()) stepNow[(size_t) i] = juce::jlimit (0, (int) def.knobs[(size_t) i].steps.size() - 1, c.ci (i));
            dip = 2; dipAt = 0;
        }
        for (int i = 0; i < nK; ++i)
        {
            const auto& kn = def.knobs[(size_t) i];
            if (! kn.steps.empty()) k->set (kn.index, kn.at[(size_t) std::max (0, stepNow[(size_t) i])]);
            else                    k->set (kn.index, kn.lo + (kn.hi - kn.lo) * clamp01 (c.v (i)));
        }

        float* w[2] = { wl, wr };
        if (! primed)
        {
            // A kernel that has just been made takes its controls from where it starts them to where they are set over
            // the first block it is given, however long that block is. Two blocks of no length take it there before it
            // has made a sound, so what it makes does not depend on how the sound is cut into blocks.
            k->run (w, w, 0); k->run (w, w, 0);
            primed = true;
        }
        if (def.space)
        {
            for (auto& f : lo) f.setHz (c.v (LOCUT), sr);
            for (int i = 0; i < n; ++i) { wl[i] = lo[0].hp (L[i]); wr[i] = lo[1].hp (R[i]); }
        }
        else
        {
            std::copy (L, L + n, wl);
            std::copy (R, R + n, wr);
        }
        k->run (w, w, n);

        bool sound = true;
        for (int i = 0; i < n; ++i) if (! std::isfinite (wl[i]) || ! std::isfinite (wr[i])) { sound = false; break; }
        if (! sound)
        {
            k->fresh (sr);
            for (auto& h : def.held) k->set (h.first, h.second);
            primed = false;
            std::fill (wl, wl + n, 0.0f); std::fill (wr, wr + n, 0.0f);
        }
        if (dip != 0)
        {
            const float step = 1.0f / (float) dipLen;
            for (int i = 0; i < n; ++i)
            {
                const float t = clamp01 ((float) dipAt * step);
                const float g = dip == 1 ? 1.0f - t : t;
                wl[i] *= g; wr[i] *= g;
                ++dipAt;
            }
            if (dip == 2 && dipAt >= dipLen) { dip = 0; dipAt = 0; }
        }
        if (def.space)
        {
            if (def.trim != 1.0f) { for (int i = 0; i < n; ++i) { wl[i] *= def.trim; wr[i] *= def.trim; } }
            const float duck = c.v (DUCK);
            pocket.process (c, w, 2, n, duck, c.v (SHAPE), nullptr, duck > 0.001f);
        }

        // MIX: the sound as it came in (as late as the kernel is) to the kernel's, in a straight line
        const int lag = lagNow();
        const float m0 = clamp01 (c.v0 (MIX)), m1 = clamp01 (c.v (MIX));
        const float step = (m1 - m0) / (float) n;
        float m = m0;
        for (int i = 0; i < n; ++i)
        {
            m += step;
            dry[0].push (L[i]); dry[1].push (R[i]);
            const float dl = dry[0].tap (lag), dr = dry[1].tap (lag);
            L[i] = dl + m * (wl[i] - dl);
            R[i] = dr + m * (wr[i] - dr);
        }
        if (LEVEL >= 0)
        {
            float g0 = 1.0f;
            const float g1 = levelDb (c, LEVEL, g0);
            if (g0 != 1.0f || g1 != 1.0f) applyGain (io, g0, g1);
        }
    }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override { if (def.space) pocket.paint (g, r); }
    int displayHeight() const override { return def.space ? 140 : 0; }
    float meter (int which) const override { return which == 30 ? (float) k->bytes() : 0.0f; }     // (30: what the kernel holds, in bytes)

private:
    int lagNow() const { return def.space ? Pocket::kLatency : def.lag; }

    const AirDef& def;
    std::unique_ptr<air::Kernel> k;
    int nK = 0, DUCK = -1, SHAPE = -1, LOCUT = -1, MIX = 0, LEVEL = -1;
    double sr = 48000.0;
    juce::AudioBuffer<float> wet;
    DelayLine dry[2];
    OnePole lo[2];
    Pocket pocket;
    std::vector<int> stepNow;           // the step of each list the kernel has now (-1: not told yet)
    int dip = 0, dipAt = 0, dipLen = 480;   // 0 none, 1 going down, 2 coming up
    bool primed = false;                // the kernel has its controls where they are set (not on its way there from where it starts them)
};
}

ModulePtr makeAIRWEAVE()    { return std::make_unique<AirModule> (0); }
ModulePtr makeAIRDEREZ()    { return std::make_unique<AirModule> (1); }
ModulePtr makeAIRFILTER()   { return std::make_unique<AirModule> (2); }
ModulePtr makeAIRENSEMBLE() { return std::make_unique<AirModule> (3); }
ModulePtr makeAIRTAPE()     { return std::make_unique<AirModule> (4); }
ModulePtr makeAIRCLIP()     { return std::make_unique<AirModule> (5); }
ModulePtr makeAIRGALAXY()   { return std::make_unique<AirModule> (6); }
}
