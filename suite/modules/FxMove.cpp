// (SF-000, the hundred and ninth and hundred and tenth builds) Two insert effects of SF-000's own, in the suite's frame:
//
// PHASER: notches that sweep. PHASER is a chain of first-order all-passes (STAGES: 4, 6, 8 or 12) swept by its own slow
// wave, its output fed back into its input; mixed with the sound as it came in, the two cancel where the chain has
// turned the phase half round (MIX half way is the deepest). FLANGER, the other MODE: a short delay swept the same way,
// fed back into itself (FEEDBACK bipolar: the other way round is the hollow one), against the sound as it came in.
//   RATE the wave's speed (free, in Hz: for one in time with the bars, RATE at its slowest with DEPTH down and a MOTION on
//   CENTER), DEPTH how far it sweeps (up to three octaves either way of CENTER, or 1.5 octaves of delay), CENTER where (a
//   phaser's 100 Hz to 8 kHz; a flanger's 10 ms to 0.3 ms, up for higher), SPREAD the right side's wave behind the left's
//   (up to half a turn: up is wider), OUTPUT the effect's own level before MIX, LOW KEEP in CONFIG the lows under it kept
//   out of the sweep, steady. A change of MODE or STAGES goes out and back in over 10 ms.
//
// SHIFT: frequency shifting. FREQ moves every component of the sound by the same number of Hz (SHIFT, FINE), up or down,
// so the harmonics no longer stand in their ratios: bells, metal, a sound that leans. It is done as it should be: the
// sound and a copy turned a quarter round (a Hilbert pair of all-pass chains), one side of the spectrum kept. RING, the
// other MODE, is a ring modulator: the sound times a sine at SHIFT's size (its sign does nothing there). SPREAD is the
// difference between the two sides (left lower, right higher). FEEDBACK goes back in after TIME (in CONFIG) through TONE
// and LO CUT: each repeat shifted again, so the repeats climb or fall. It is bounded (at most 95 %, and softly limited).
// Neither has any delay of its own: the rig's is what it was.
#include "Kit.h"

namespace sf
{
namespace
{
// A sine wave and a triangle, from a phase (0..1).
inline float waveAt (double ph, int shape)
{
    const float p = (float) (ph - std::floor (ph));
    if (shape == 1) return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
    return std::sin (kTwoPi * p);
}

// Out over 10 ms and back: what a module does when its structure changes (a MODE, a STAGES), so it never clicks.
struct Swap
{
    float g = 1.0f, step = 0.002f;
    int want = -1, now = -1;
    void prepare (double sr) { step = 1.0f / (float) (0.01 * sr); }
    // per sample: true when the structure is to change now (at the bottom of the dip)
    bool tick()
    {
        if (want != now) { g -= step; if (g <= 0.0f) { g = 0.0f; now = want; return true; } return false; }
        g = std::min (1.0f, g + step);
        return false;
    }
};
}

class PhaserModule : public Module
{
public:
    enum { RATE, DEPTH, CENTER, FEEDBACK, SPREAD, OUTPUT, MIX, LEVEL, MODE, STAGES, SHAPE, LOWKEEP };
    static constexpr int kMaxStages = 12;
    std::string id() const override { return "PHASER"; }
    double tailSeconds() const override { return 1.0; }
    std::vector<ParamSpec> params() const override
    {
        return { knob ("rate", "RATE", 0.02f, 10.0f, 0.15f, "Hz", 0.5f).prim(),
                 pct ("depth", "DEPTH", 0.6f).prim().dir (1),
                 pct ("center", "CENTER", 0.5f).prim(),
                 knob ("feedback", "FEEDBACK", -0.95f, 0.95f, 0.0f, "%").prim(),
                 pct ("spread", "SPREAD", 0.5f).prim(),
                 knob ("output", "OUTPUT", -24.0f, 6.0f, 0.0f, "dB", -6.0f).prim(),
                 pct ("mix", "MIX", 0.5f).prim().dir (1),
                 levelParam().prim(),
                 choice ("mode", "MODE", { "PHASER", "FLANGER" }, 0),
                 choice ("stages", "STAGES", { "4", "6", "8", "12" }, 1),
                 choice ("shape", "SHAPE", { "SINE", "TRIANGLE" }, 0),
                 knob ("low_keep", "LOW KEEP", 0.0f, 400.0f, 0.0f, "Hz", 120.0f) };
    }
    void prepare (double rate, int) override
    {
        sr = rate;
        for (auto& d : line) d.prepare ((int) (0.025 * rate) + 8);
        swap.prepare (rate);
        reset();
    }
    void reset() override
    {
        for (int ch = 0; ch < 2; ++ch) { for (int k = 0; k < kMaxStages; ++k) apX[ch][k] = apY[ch][k] = 0.0f; fb[ch] = 0.0f; low1[ch] = low2[ch] = 0.0f; line[ch].clear(); }
        phase = 0.0; swap.now = swap.want = -1; swap.g = 1.0f;
    }
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        static const int stagesOf[] = { 4, 6, 8, 12 };
        const int mode = juce::jlimit (0, 1, c.ci (MODE)), stages = stagesOf[juce::jlimit (0, 3, c.ci (STAGES))];
        swap.want = mode * 16 + stages;
        if (swap.now < 0) swap.now = swap.want;
        const int shape = c.ci (SHAPE);
        const float keepHz = c.v (LOWKEEP), keepA = keepHz < 1.0f ? 0.0f : 1.0f - std::exp (-kTwoPi * keepHz / (float) sr);
        const double inc = (double) c.v (RATE) / sr;
        float* io2[2] = { io.getWritePointer (0), io.getWritePointer (1) };
        for (int i = 0; i < c.n; ++i)
        {
            const float t = (float) (i + 1) / (float) c.n;
            auto at = [&] (int k) { return c.v0 (k) + (c.v (k) - c.v0 (k)) * t; };
            const float depth = at (DEPTH), center = at (CENTER), fbk = at (FEEDBACK), spread = at (SPREAD), mix = at (MIX);
            const float outG = juce::Decibels::decibelsToGain (at (OUTPUT));
            // (the flanger's line is fed in both modes, so a flanger come in reads what was played, not a silence it then
            // steps out of)
            if (swap.tick()) for (int ch = 0; ch < 2; ++ch) { for (int k = 0; k < kMaxStages; ++k) apX[ch][k] = apY[ch][k] = 0.0f; fb[ch] = 0.0f; }
            const int modeNow = swap.now / 16, stagesNow = swap.now % 16;
            phase += inc; if (phase >= 1.0) phase -= 1.0;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float lfo = waveAt (phase + (ch == 1 ? 0.5 * spread : 0.0), shape);
                float x = io2[ch][i];
                // LOW KEEP: the lows under it are left as they are, the rest goes through
                float low = 0.0f;
                if (keepA > 0.0f) { low1[ch] += keepA * (x - low1[ch]); low2[ch] += keepA * (low1[ch] - low2[ch]); low = low2[ch]; }
                const float high = x - low;
                float wet;
                if (modeNow == 0)
                {
                    const float hz = juce::jlimit (20.0f, 0.45f * (float) sr, 100.0f * std::pow (80.0f, center) * std::pow (2.0f, 3.0f * depth * lfo));
                    const float tn = std::tan (kPi * hz / (float) sr), a = (tn - 1.0f) / (tn + 1.0f);
                    float y = high + fbk * fb[ch];
                    for (int k = 0; k < stagesNow; ++k)
                    {
                        const float o = a * y + apX[ch][k] - a * apY[ch][k];
                        apX[ch][k] = y; apY[ch][k] = o; y = o;
                    }
                    fb[ch] = fastTanh (y);
                    wet = y;
                    line[ch].push (high);
                }
                else
                {
                    const float ms = juce::jlimit (0.1f, 20.0f, 10.0f * std::pow (0.03f, center) * std::pow (2.0f, 1.5f * depth * lfo));
                    const float d = line[ch].read (ms * 0.001f * (float) sr);
                    line[ch].push (fastTanh (high + fbk * d));
                    wet = d;
                }
                wet *= outG * swap.g;
                io2[ch][i] = low + high * (1.0f - mix) + wet * mix;
            }
        }
        float g0; const float g1 = levelDb (c, LEVEL, g0);
        applyGain (io, g0, g1);
    }

private:
    double sr = 48000.0, phase = 0.0;
    float apX[2][kMaxStages] {}, apY[2][kMaxStages] {}, fb[2] {}, low1[2] {}, low2[2] {};
    DelayLine line[2];
    Swap swap;
};

class ShiftModule : public Module
{
public:
    enum { SHIFT, FINE, FEEDBACK, TONE, SPREAD, OUTPUT, MIX, LEVEL, MODE, TIME, LOCUT };
    std::string id() const override { return "SHIFT"; }
    double tailSeconds() const override { return 10.0; }
    std::vector<ParamSpec> params() const override
    {
        return { knob ("shift", "SHIFT", -2000.0f, 2000.0f, 0.0f, "Hz").prim(),
                 knob ("fine", "FINE", -10.0f, 10.0f, 0.0f, "Hz").prim(),
                 knob ("feedback", "FEEDBACK", 0.0f, 0.95f, 0.0f, "%").prim().dir (1),
                 bip ("tone", "TONE").prim(),
                 knob ("spread", "SPREAD", 0.0f, 50.0f, 0.0f, "Hz", 10.0f).prim(),
                 knob ("output", "OUTPUT", -24.0f, 6.0f, 0.0f, "dB", -6.0f).prim(),
                 pct ("mix", "MIX", 0.5f).prim().dir (1),
                 levelParam().prim(),
                 choice ("mode", "MODE", { "FREQ", "RING" }, 0),
                 knob ("time", "TIME", 2.0f, 1000.0f, 120.0f, "ms", 120.0f),
                 knob ("lo_cut", "LO CUT", 20.0f, 600.0f, 80.0f, "Hz", 120.0f) };
    }
    void prepare (double rate, int) override
    {
        sr = rate;
        for (auto& d : line) d.prepare ((int) (1.05 * rate) + 8);
        tone.prepare (rate);
        swap.prepare (rate);
        reset();
    }
    void reset() override
    {
        for (auto& h : hil) h = {};
        for (auto& d : line) d.clear();
        for (int ch = 0; ch < 2; ++ch) { ph[ch] = 0.0; loX[ch] = loY[ch] = 0.0f; }
        tone.reset();
        swap.now = swap.want = -1; swap.g = 1.0f;
    }
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        swap.want = juce::jlimit (0, 1, c.ci (MODE));
        if (swap.now < 0) swap.now = swap.want;
        const float loA = std::exp (-kTwoPi * c.v (LOCUT) / (float) sr);
        // (the hundred and eleventh build) never shorter than a chunk: every repeat is read from before the chunk began, so
        // what comes back does not hang on the chunk's size (2 ms is 88 samples at 44.1 kHz; a chunk is at most 64)
        const float delay = juce::jlimit ((float) kChunkMax, (float) sr, c.v (TIME) * 0.001f * (float) sr);
        float* io2[2] = { io.getWritePointer (0), io.getWritePointer (1) };
        // the feedback, read for the whole chunk first (it is at least a millisecond back: a chunk is 64 samples), through
        // TONE and LO CUT
        jassert (c.n <= kChunkMax);
        float back[2][kChunkMax];
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < c.n; ++i)
            {
                // (the delay line has this chunk's own pushes still to come: read from where it will be)
                back[ch][i] = line[ch].read (std::max (1.0f, delay - (float) i));
            }
        {
            float* bk[2] = { back[0], back[1] };
            tone.process (bk, 2, c.n, c.v0 (TONE), c.v (TONE), 0.15f);
        }
        for (int i = 0; i < c.n; ++i)
        {
            const float t = (float) (i + 1) / (float) c.n;
            auto at = [&] (int k) { return c.v0 (k) + (c.v (k) - c.v0 (k)) * t; };
            const float hz = at (SHIFT) + at (FINE), spread = at (SPREAD), fbk = at (FEEDBACK), mix = at (MIX);
            const float outG = juce::Decibels::decibelsToGain (at (OUTPUT));
            if (swap.tick()) for (int ch = 0; ch < 2; ++ch) hil[ch] = {};      // (the repeats' line kept: they come back through the new MODE)
            const bool ring = swap.now == 1;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float side = ch == 0 ? -0.5f : 0.5f;
                const float f = ring ? std::abs (hz) + side * spread : hz + side * spread;
                ph[ch] += (double) f / sr;
                ph[ch] -= std::floor (ph[ch]);
                // the feedback's lows out (LO CUT: a one-pole high-pass), bounded
                float fbIn = back[ch][i];
                loY[ch] = loA * (loY[ch] + fbIn - loX[ch]); loX[ch] = fbIn; fbIn = loY[ch];
                const float x = io2[ch][i];
                const float in = x + fbk * fbIn;
                float wet;
                const float s = std::sin (kTwoPi * (float) ph[ch]), co = std::cos (kTwoPi * (float) ph[ch]);
                if (ring) wet = in * s;
                else
                {
                    float i0, q0;
                    hil[ch].tick (in, i0, q0);
                    wet = i0 * co + q0 * s;      // (this side: up for a shift up)
                }
                wet = fastTanh (wet);
                line[ch].push (wet);
                io2[ch][i] = x * (1.0f - mix) + wet * outG * swap.g * mix;
            }
        }
        float g0; const float g1 = levelDb (c, LEVEL, g0);
        applyGain (io, g0, g1);
    }

private:
    static constexpr int kChunkMax = 64;         // a chunk is at most 64 samples (Module.h)
    // The Hilbert pair: two chains of four second-order all-passes (Olli Niemitalo's), a quarter turn apart over 20 Hz
    // to 20 kHz at 44.1 kHz and up; the first a sample late, as the pair is made.
    struct Hilbert
    {
        float x1[8] {}, x2[8] {}, y1[8] {}, y2[8] {}, late = 0.0f;
        void tick (float x, float& i, float& q)
        {
            static const float a[8] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f,
                                        0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
            float p = x, r = x;
            for (int k = 0; k < 4; ++k) { const float a2 = a[k] * a[k], y = a2 * (p + y2[k]) - x2[k]; x2[k] = x1[k]; x1[k] = p; y2[k] = y1[k]; y1[k] = y; p = y; }
            for (int k = 4; k < 8; ++k) { const float a2 = a[k] * a[k], y = a2 * (r + y2[k]) - x2[k]; x2[k] = x1[k]; x1[k] = r; y2[k] = y1[k]; y1[k] = y; r = y; }
            i = late; late = p; q = r;
        }
    };
    double sr = 48000.0, ph[2] {};
    Hilbert hil[2];
    DelayLine line[2];
    DJFilter tone;
    float loX[2] {}, loY[2] {};
    Swap swap;
};

ModulePtr makePHASER() { return std::make_unique<PhaserModule>(); }
ModulePtr makeSHIFT()  { return std::make_unique<ShiftModule>(); }
} // namespace sf
