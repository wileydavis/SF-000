// FILTER, GATE, DENSITY, DELAY, REVERB, REPEAT
#include "Kit.h"
#include "../core/BandLimited.h"
#include "../core/Oversample.h"

namespace sf
{
// =====================================================================================  FILTER
class FilterModule : public Module
{
public:
    enum { FILT, RES, DRIVE, ENVAMT, ENVSPD, THROW, THROWTIME, LEVEL, THROWTO };
    std::string id() const override { return "FILTER"; }
    std::vector<ParamSpec> params() const override
    {
        return { bip ("filter", "FILTER").prim().dir (1),
                 pct ("res", "RES", 0.2f).prim(),
                 pct ("drive", "DRIVE", 0.0f).prim(),
                 knob ("env_amt", "ENV AMT", -1.0f, 1.0f, 0.0f, "%").prim(),
                 pct ("env_speed", "ENV SPEED", 0.5f).prim(),
                 momentary ("throw", "THROW").prim(),
                 choice ("throw_time", "THROW TIME", divisionNames (divIndex ("4 BAR"), divIndex ("1/16")), 4).prim(),
                 levelParam().prim(),
                 choice ("throw_to", "THROW TO", { "HP", "LP" }, 0) };
    }
    void prepare (double rate, int) override { sr = rate; filt.prepare (rate); os.prepare (rate, 2); throwPos = 0; }
    void reset() override { filt.reset(); os.reset(); fol.z = 0.0f; }
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        const int n = c.n;
        const float speed = c.v (ENVSPD);
        fol.set (0.002f + (1.0f - speed) * 0.02f, 0.05f + (1.0f - speed) * 0.6f, sr);
        float env = 0.0f;
        for (int i = 0; i < n; ++i)
            env = fol.tick (0.5f * (io.getSample (0, i) + io.getSample (1, i)));
        envDisplay = env;
        // Throw: ramps the filter to the extreme over THROW TIME, returns in a quarter of that.
        const double tBeats = divisionBeats (c.ci (THROWTIME), divIndex ("4 BAR"));
        const float step = (float) (n / (tBeats * c.samplesPerBeat()));
        throwPos = c.b (THROW) ? std::min (1.0f, throwPos + step) : std::max (0.0f, throwPos - step * 4.0f);
        const float target = c.ci (THROWTO) == 0 ? 0.97f : 0.03f;
        auto knobAt = [&] (float base)
        {
            float k = clamp01 (base + c.v (ENVAMT) * clamp01 (env * 2.0f) * 0.5f);
            return lerp (k, target, throwPos * throwPos);
        };
        const float k1 = knobAt (c.v (FILT));
        const float k0 = lastK < 0 ? k1 : lastK;
        lastK = k1;
        posDisplay = k1;
        // Drive before the filter, 2x oversampled. Always through the oversampler (no delay
        // switching in or out) and blended in from exactly clean at 0 (no level step).
        const float dAmt = c.v (DRIVE);
        const float dg = 1.0f + dAmt * 10.0f, comp = 1.0f / (1.0f + dAmt * 3.0f) * 1.6f;
        for (int ch = 0; ch < 2; ++ch)
            os.process (ch, io.getWritePointer (ch), n, [dAmt, dg, comp] (float x) { return x + dAmt * (fastTanh (x * dg) * comp - x); });
        filt.process (io.getArrayOfWritePointers(), 2, n, k0, k1, c.v (RES));
        float g0; const float g1 = levelDb (c, LEVEL, g0);
        applyGain (io, g0, g1);
    }
    int displayHeight() const override { return 70; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        ui::frame (g, r, "CUTOFF");
        const float k = posDisplay.load();
        drawPosition (g, r.reduced (12, 22), k, "LP", "HP", std::abs (k - 0.5f) < 0.02f ? ui::kDim : ui::kText);
        juce::String hz = std::abs (k - 0.5f) < 0.02f ? "OPEN" : k < 0.5f ? "LP " + juce::String (juce::roundToInt (DJFilter::lpHz (k))) + " Hz"
                                                                         : "HP " + juce::String (juce::roundToInt (DJFilter::hpHz (k))) + " Hz";
        ui::label (g, hz, r.reduced (8, 4).removeFromBottom (14), ui::kText, 11.0f, juce::Justification::centred);
        ui::levelBar (g, r.reduced (8, 4).removeFromBottom (4).withWidth (80), envDisplay.load());
    }
private:
    double sr = 48000.0;
    DJFilter filt;
    Oversampler os;
    Follower fol;
    float throwPos = 0, lastK = -1;
    std::atomic<float> posDisplay { 0.5f }, envDisplay { 0.0f };
};
ModulePtr makeFILTER() { return std::make_unique<FilterModule>(); }

// =====================================================================================  GATE
static const std::vector<std::pair<const char*, uint16_t>>& gatePatterns()
{
    // bit 15 = step 1
    static const std::vector<std::pair<const char*, uint16_t>> p {
        { "FULL", 0xFFFF }, { "8THS", 0xAAAA }, { "OFFBEAT", 0x2222 }, { "OFF 8THS", 0x5555 }, { "TRESILLO", 0x9292 },
        { "GALLOP", 0xB6DB }, { "SKIP 1", 0xEEEE }, { "SKIP 2", 0xDDDD }, { "STAB", 0x8888 }, { "PUMP", 0x7777 },
        { "BROKEN", 0xA94A }, { "CLAVE", 0x9228 }, { "3-3-2", 0x9292 ^ 0x0012 }, { "HALF", 0xFF00 }, { "SPARSE", 0x8020 }, { "DOTTED", 0x9249 } };
    return p;
}
static std::vector<std::string> gatePatternNames() { std::vector<std::string> v; for (auto& p : gatePatterns()) v.push_back (p.first); return v; }

class GateModule : public Module
{
public:
    enum { DEPTH, LENGTH, SHAPE, SWING, PATA, PATB, MORPH, RATE };
    std::string id() const override { return "GATE"; }
    std::vector<ParamSpec> params() const override
    {
        return { pct ("depth", "DEPTH", 0.0f).prim().dir (1),
                 pct ("length", "LENGTH", 0.5f).prim(),
                 pct ("shape", "SHAPE", 0.3f).prim(),
                 pct ("swing", "SWING", 0.0f).prim(),
                 choice ("pattern_a", "PATTERN A", gatePatternNames(), 1).prim(),
                 choice ("pattern_b", "PATTERN B", gatePatternNames(), 2).prim(),
                 pct ("morph", "MORPH", 0.0f).prim(),
                 choice ("rate", "RATE", { "1/8", "1/16", "1/32", "1/16T" }, 1).prim() };
    }
    void prepare (double rate, int) override { sr = rate; z = 1.0f; }
    void reset() override { z = 1.0f; }
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        static const double rates[] = { 0.5, 0.25, 0.125, 1.0 / 6.0 };
        const double stepB = rates[juce::jlimit (0, 3, c.ci (RATE))];
        const uint16_t a = gatePatterns()[(size_t) c.ci (PATA)].second, b = gatePatterns()[(size_t) c.ci (PATB)].second;
        const float morph = c.v (MORPH), len = 0.05f + c.v (LENGTH) * 0.95f, shape = c.v (SHAPE), depth = c.v (DEPTH);
        const float sw = c.v (SWING) / 3.0f;
        const float ramp = 0.02f + shape * 0.43f;
        const double late = groove.beats (*c.bus, c.bpm);     // the steps on the groove's grid (KickGroove)
        const float smoothA = std::exp (-1.0f / (float) (0.0008 * sr));
        for (int i = 0; i < c.n; ++i)
        {
            double pos = (c.ppqAt (i) - late) / stepB;
            const double whole = std::floor (pos);
            const int step = (int) ((long long) whole % 16 + 16) % 16;
            double frac = pos - whole;
            if (step & 1) frac = (frac - sw) / (1.0 - sw);         // late odd steps
            const float onA = (a >> (15 - step)) & 1 ? 1.0f : 0.0f, onB = (b >> (15 - step)) & 1 ? 1.0f : 0.0f;
            const float on = lerp (onA, onB, morph);
            float gte = 0.0f;
            if (frac >= 0.0)
            {
                const float f = (float) frac;
                const float up = clamp01 (f / std::max (0.004f, ramp * len));
                const float down = clamp01 ((len - f) / std::max (0.004f, ramp * len));
                gte = std::min (up, down) * on;
            }
            const float target = 1.0f - depth * (1.0f - gte);
            z = target + smoothA * (z - target);
            io.getWritePointer (0)[i] *= z;
            io.getWritePointer (1)[i] *= z;
            if (i == 0) playStep = step;
        }
        patA = a; patB = b; morphD = morph;
    }
    int displayHeight() const override { return 80; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        ui::frame (g, r, "PATTERN");
        auto in = r.reduced (10, 20);
        const float w = in.getWidth() / 16.0f;
        for (int row = 0; row < 2; ++row)
        {
            const uint16_t p = row == 0 ? patA.load() : patB.load();
            const float alpha = row == 0 ? 1.0f - morphD.load() * 0.7f : 0.3f + morphD.load() * 0.7f;
            for (int s = 0; s < 16; ++s)
            {
                auto cell = juce::Rectangle<float> (in.getX() + s * w, in.getY() + row * in.getHeight() * 0.5f, w, in.getHeight() * 0.5f).reduced (2);
                const bool on = (p >> (15 - s)) & 1;
                g.setColour (on ? ui::kText.withAlpha (alpha) : ui::kTrack);
                g.fillRect (cell);
                if (s == playStep.load()) { g.setColour (ui::kGreen); g.drawRect (cell, 1.0f); }
            }
        }
    }
private:
    double sr = 48000.0;
    float z = 1.0f;
    std::atomic<uint16_t> patA { 0 }, patB { 0 };
    std::atomic<float> morphD { 0 };
    std::atomic<int> playStep { 0 };
    KickGroove groove;                    // where the groove lands against Live's beat
};
ModulePtr makeGATE() { return std::make_unique<GateModule>(); }

// =====================================================================================  DENSITY (audio)
// Multi-tap echo built to fill sparse rhythms: tier 1 echo one SPACE later, tier 2 ghosts at
// half spacing. Each tier darker and quieter. PROB decides per incoming hit whether its echoes play.
class DensityModule : public Module
{
public:
    enum { DENS, SPACE, DARKEN, SWING, PROB, MIX, LEVEL, SHORTEN, DRYON };
    std::string id() const override { return "DENSITY"; }
    std::vector<ParamSpec> params() const override
    {
        return { pct ("density", "DENSITY", 0.0f).prim().dir (1),
                 choice ("space", "SPACE", echoSpaceNames(), 1).prim(),
                 pct ("darken", "DARKEN", 0.4f).prim(),
                 pct ("swing", "SWING", 0.0f).prim(),
                 pct ("prob", "PROB", 1.0f).prim(),
                 pct ("mix", "MIX", 1.0f).prim(),
                 levelParam().prim(),
                 pct ("shorten", "SHORTEN", 0.3f),
                 dryParam() };
    }
    void prepare (double rate, int) override
    {
        sr = rate;
        for (auto& d : line) d.prepare ((int) (rate * 4.5));
        for (auto& d : gateLine) d.prepare ((int) (rate * 4.5));
        fast.set (0.0005f, 0.01f, sr); slow.set (0.01f, 0.25f, sr);
        dryRamp.prepare (rate);
        reset();
    }
    void reset() override
    {
        for (auto& d : line) d.clear();
        for (auto& d : gateLine) d.clear();
        for (auto& t : taps) { for (auto& f : t.lp) f.reset(); t.g = 1.0f; }
        fast.z = slow.z = 0.0f;
        sinceOnset = 1 << 20;
    }
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        const double space = echoSpaceBeats (c.ci (SPACE)) * c.samplesPerBeat();
        const float d = c.v (DENS), sw = (float) (c.v (SWING) * space / 3.0);
        const float t1 = clamp01 (d * 2.0f), t2 = clamp01 ((d - 0.5f) * 2.0f);
        const float dark = c.v (DARKEN), mix = c.v (MIX), prob = c.v (PROB), shorten = c.v (SHORTEN);
        const float dryT = c.b (DRYON) ? 1.0f : 0.0f;
        const float delays[3] = { (float) space + sw, (float) (space * 0.5), (float) (space * 1.5) + sw };
        const float levels[3] = { 0.62f * t1, 0.38f * t2, 0.32f * t2 };
        const int tiers[3] = { 1, 2, 2 };
        for (int k = 0; k < 3; ++k)
            for (int ch = 0; ch < 2; ++ch)
                taps[k].lp[ch].set (expMap (1.0f - dark * (tiers[k] == 1 ? 0.6f : 0.85f), 600.0f, 18000.0f), 0.7071f, sr);
        const float gateSmooth = std::exp (-1.0f / (float) (0.001 * sr));
        for (int i = 0; i < c.n; ++i)
        {
            const float l = io.getSample (0, i), r = io.getSample (1, i);
            const float m = 0.5f * (l + r);
            const float f = fast.tick (m), s = slow.tick (m);
            if (f > s * 1.8f && f > 0.003f && sinceOnset > (int) (0.03 * sr))
            {
                sinceOnset = 0;
                pass1 = rng.uni() <= prob ? 1.0f : 0.0f;
                pass2 = rng.uni() <= prob ? 1.0f : 0.0f;
                envAtOnset = 0;
            }
            ++sinceOnset;
            // SHORTEN: echoes only carry the start of each hit.
            const float holdLen = (float) (space * (1.0f - shorten * 0.85f));
            const float carry = sinceOnset < holdLen ? 1.0f : std::max (0.0f, 1.0f - (sinceOnset - holdLen) / (float) (0.01 * sr));
            line[0].push (l * carry); line[1].push (r * carry);
            gateLine[0].push (pass1); gateLine[1].push (pass2);
            float wl = 0.0f, wr = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                if (levels[k] <= 0.0005f) continue;
                const float gt = gateLine[tiers[k] - 1].tap ((int) delays[k]);
                taps[k].g = gt + gateSmooth * (taps[k].g - gt);
                const float a = levels[k] * taps[k].g;
                wl += taps[k].lp[0].tickLP (line[0].read (delays[k])) * a;
                wr += taps[k].lp[1].tickLP (line[1].read (delays[k])) * a;
            }
            const float dg = dryRamp.tick (dryT);
            io.getWritePointer (0)[i] = l * dg + wl * mix;
            io.getWritePointer (1)[i] = r * dg + wr * mix;
        }
        float g0; const float g1 = levelDb (c, LEVEL, g0);
        applyGain (io, g0, g1);
        dens = d;
    }
    int displayHeight() const override { return 70; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        ui::frame (g, r, "TAPS");
        auto in = r.reduced (12, 20);
        const float d = dens.load();
        const float t1 = clamp01 (d * 2.0f), t2 = clamp01 ((d - 0.5f) * 2.0f);
        const float xs[4] = { 0.0f, 0.25f, 0.5f, 0.75f };
        const float hs[4] = { 1.0f, 0.38f * t2, 0.62f * t1, 0.32f * t2 };
        for (int k = 0; k < 4; ++k)
        {
            const float h = hs[k] * in.getHeight();
            g.setColour (k == 0 ? ui::kText : ui::kDim);
            g.fillRect (in.getX() + xs[k] * in.getWidth(), in.getBottom() - h, 6.0f, h);
        }
    }
private:
    double sr = 48000.0;
    DelayLine line[2], gateLine[2];
    struct Tap { Svf lp[2]; float g = 1.0f; } taps[3];
    Follower fast, slow;
    Noise rng;
    int sinceOnset = 1 << 20, envAtOnset = 0;
    float pass1 = 1.0f, pass2 = 1.0f;
    DryRamp dryRamp;
    std::atomic<float> dens { 0.0f };
};
ModulePtr makeDENSITY() { return std::make_unique<DensityModule>(); }

// =====================================================================================  DELAY
// Tape-style dub delay. Changing TIME slews the read head (pitch bend). Feedback can pass 100%:
// the loop saturates (2x oversampled) instead of exploding. TONE sits inside the loop, so every
// repeat is filtered once more than the one before (darker / thinner). THROW sends only while
// held. FREEZE loops forever (lossless: no tone, no saturation).
class DelayModule : public Module
{
public:
    enum { TIME, FDBK, TONE, DRIVE, THROW, FREEZE, MIX, WIDTH, SLEW, MODE, INPUT, DUCK, DUCKSHAPE, WOW, LOCUT, DRYON };
    std::string id() const override { return "DELAY"; }
    std::vector<ParamSpec> params() const override
    {
        return { choice ("time", "TIME", divisionNames (divIndex ("1 BAR"), divIndex ("7/16")), divIndex ("1/8D") - divIndex ("1 BAR")).prim(),
                 knob ("feedback", "FEEDBACK", 0.0f, 1.15f, 0.45f, "%").prim().dir (1),
                 bip ("tone", "TONE").prim(),
                 pct ("drive", "DRIVE", 0.2f).prim(),
                 momentary ("throw", "THROW").prim(),
                 toggle ("freeze", "FREEZE").prim(),
                 pct ("mix", "MIX", 0.3f).prim().dir (1),
                 pct ("width", "WIDTH", 0.5f).prim(),
                 knob ("slew", "SLEW", 10.0f, 3000.0f, 400.0f, "ms", 400.0f),
                 choice ("mode", "MODE", { "STEREO", "PING-PONG" }, 1),
                 choice ("input", "INPUT", { "ALWAYS", "THROW ONLY" }, 0),
                 pct ("duck", "DUCK", 0.0f),
                 pct ("duck_shape", "DUCK SHAPE", 0.6f),
                 pct ("wow", "WOW", 0.1f),
                 knob ("lo_cut", "LO CUT", 20.0f, 600.0f, 80.0f, "Hz", 120.0f),
                 dryParam() };
    }
    int latency() const override { return Pocket::kLatency; }
    double tailSeconds() const override { return 20.0; }
    void prepare (double rate, int mb) override
    {
        sr = rate;
        for (auto& d : line) d.prepare ((int) (rate * 9.0));
        for (auto& d : dry) d.prepare (Pocket::kLatency + 4);
        loopFilt.prepare (rate);
        pocket.prepare (rate, mb);
        wet.setSize (2, mb);
        const auto hb = HalfbandDesign::forStage (rate, 0);
        for (int k = 0; k < 2; ++k) { upF[k].set (hb); dnF[k].set (hb); }
        osDelay = measureOsDelay (hb);
        curDelay = -1;
        dryRamp.prepare (rate);
        reset();
    }
    void reset() override
    {
        for (auto& d : line) d.clear();
        for (auto& d : dry) d.clear();
        for (auto& f : lo) f.reset();
        for (int k = 0; k < 2; ++k) { upF[k].reset(); dnF[k].reset(); }
        loopFilt.reset();
        pocket.reset();
    }
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        const int n = c.n;
        wet.setSize (2, n, false, false, true);
        const double beats = divisionBeats (c.ci (TIME), divIndex ("1 BAR"));
        const float target = (float) std::min (beats * c.samplesPerBeat(), sr * 8.5);
        if (curDelay < 0) curDelay = target;
        const float slewRate = std::abs (target - curDelay) / (float) std::max (1.0, c.v (SLEW) * 0.001 * sr);
        const bool freeze = c.b (FREEZE), throwOn = c.b (THROW), throwOnly = c.ci (INPUT) == 1;
        // FREEZE crossfades over ~10 ms into a lossless loop (feedback 1, no lo cut, no tone, no
        // saturation). The loop's 2x stage keeps running either way, so the read head never moves.
        const float fbkSet = c.v (FDBK), freezeTarget = freeze ? 1.0f : 0.0f;
        const float freezeK = 1.0f - std::exp (-1.0f / (0.01f * (float) sr));
        const float drive = c.v (DRIVE), mix = c.v (MIX), width = c.v (WIDTH);
        const bool ping = c.ci (MODE) == 1;
        const float wow = c.v (WOW);
        for (auto& f : lo) f.setHz (c.v (LOCUT), sr);
        float inGainTarget = freeze ? 0.0f : (throwOnly ? (throwOn ? 1.0f : 0.0f) : 1.0f);
        if (throwOn && ! throwOnly) inGainTarget = 1.6f;
        auto* L = io.getWritePointer (0); auto* R = io.getWritePointer (1);
        auto* wl = wet.getWritePointer (0); auto* wr = wet.getWritePointer (1);
        const float g = 1.0f + drive * 3.0f, invG = 1.0f / g;
        // Loop saturation: drive, then a soft ceiling (runaway becomes saturation).
        auto sat = [&] (float x)
        {
            if (drive > 0.0f) x = fastTanh (x * g) * invG;
            return fastTanh (x * 0.85f) * (1.0f / 0.85f);
        };
        const float k0 = c.v0 (TONE), k1 = c.v (TONE);
        for (int i = 0; i < n; ++i)
        {
            inGain += 0.002f * (inGainTarget - inGain);
            if (std::abs (target - curDelay) > 0.5f) curDelay += juce::jlimit (-slewRate, slewRate, target - curDelay) + 0.0f;
            freezeX += freezeK * (freezeTarget - freezeX);
            const float fz = freezeX < 1.0e-6f ? 0.0f : (freezeX > 0.999999f ? 1.0f : freezeX);
            // Frozen, wow stops where it is and the head moves onto a whole sample, so the loop
            // reads without interpolation loss (no darkening, no fade).
            wowPhase += (float) (0.6 / sr) * (1.0f - fz); if (wowPhase > 1.0f) wowPhase -= 1.0f;
            const float dLive = curDelay * (1.0f + wow * 0.004f * std::sin (kTwoPi * wowPhase)) - osDelay;
            // Onto the whole sample only once the head has settled: while TIME is still moving,
            // whole-sample reads would zipper. The snap fades out (and back in) over ~10 ms.
            const bool settled = std::abs (target - curDelay) <= 0.5f;
            snapX += freezeK * ((settled ? fz : 0.0f) - snapX);
            if (snapX > 0.999999f) snapX = 1.0f;
            if (settled) snapTo = std::round (dLive);
            const float d = snapX > 1.0e-6f ? dLive + snapX * (snapTo - dLive) : dLive;
            const float yl = line[0].read (d), yr = line[1].read (d);
            const float fbk = fbkSet + fz * (1.0f - fbkSet);
            float fl = yl * fbk, fr = yr * fbk;
            {
                fl += (1.0f - fz) * (lo[0].hp (fl) - fl);
                fr += (1.0f - fz) * (lo[1].hp (fr) - fr);
                auto satF = [&] (float x) { return fz >= 1.0f ? x : x + (1.0f - fz) * (sat (x) - x); };
                float a0, a1;
                upF[0].up (fl, a0, a1); fl = dnF[0].down (satF (a0), satF (a1));
                upF[1].up (fr, a0, a1); fr = dnF[1].down (satF (a0), satF (a1));
            }
            const float il = L[i] * inGain, ir = R[i] * inGain;
            float w0, w1;
            if (ping) { w0 = 0.5f * (il + ir) + fr; w1 = fl; }
            else      { w0 = il + fl; w1 = ir + fr; }
            if (fz < 1.0f)
            {
                float t0 = w0, t1 = w1;
                loopFilt.step (k0 + (k1 - k0) * (float) i / (float) n, 0.0f, t0, t1, false);
                w0 += (1.0f - fz) * (t0 - w0); w1 += (1.0f - fz) * (t1 - w1);
            }
            line[0].push (w0); line[1].push (w1);
            const float mid = 0.5f * (yl + yr), side = 0.5f * (yl - yr) * width * 2.0f;
            wl[i] = mid + side; wr[i] = mid - side;
        }
        pocket.process (c, wet.getArrayOfWritePointers(), 2, n, c.v (DUCK), c.v (DUCKSHAPE), nullptr, c.v (DUCK) > 0.001f);
        const float dryT = c.b (DRYON) ? 1.0f : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float dg = dryRamp.tick (dryT);
            dry[0].push (L[i]); dry[1].push (R[i]);
            L[i] = dry[0].tap (Pocket::kLatency) * dg + wl[i] * mix * 1.2f;
            R[i] = dry[1].tap (Pocket::kLatency) * dg + wr[i] * mix * 1.2f;
        }
        disp = curDelay / (float) c.samplesPerBeat();
    }
    int displayHeight() const override { return 70; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        auto left = r.removeFromLeft (r.getWidth() * 0.5f);
        ui::frame (g, left, "TIME");
        ui::label (g, juce::String (disp.load(), 3) + " BEAT", left.reduced (10, 18), ui::kText, 18.0f, juce::Justification::centred);
        pocket.paint (g, r.withTrimmedLeft (8));
    }
private:
    // Group delay of the in-loop 2x stage at low frequencies (subtracted from the read head so
    // the repeats stay on the grid).
    static float measureOsDelay (const HalfbandDesign& d)
    {
        Halfband u, dn; u.set (d); dn.set (d);
        double num = 0.0, den = 0.0;
        for (int i = 0; i < 256; ++i)
        {
            float a, b; u.up (i == 0 ? 1.0f : 0.0f, a, b);
            const double y = dn.down (a, b);
            num += i * y; den += y;
        }
        return den != 0.0 ? (float) (num / den) : 0.0f;
    }
    double sr = 48000.0;
    DelayLine line[2], dry[2];
    DJFilter loopFilt;
    OnePole lo[2];
    Halfband upF[2], dnF[2];
    Pocket pocket;
    juce::AudioBuffer<float> wet;
    float curDelay = -1, inGain = 1.0f, wowPhase = 0, osDelay = 0.0f, freezeX = 0.0f, snapX = 0.0f, snapTo = 0.0f;
    DryRamp dryRamp;
    std::atomic<float> disp { 0 };
};
ModulePtr makeDELAY() { return std::make_unique<DelayModule>(); }

// =====================================================================================  REVERB
class ReverbModule : public Module
{
public:
    enum { SIZE, DECAY, DARK, PREDELAY, DUCK, FREEZE, THROW, MIX, MOD, WIDTH, GATED, GATETIME, LOCUT, DUCKSHAPE, INPUT, DRYON };
    std::string id() const override { return "REVERB"; }
    std::vector<ParamSpec> params() const override
    {
        return { pct ("size", "SIZE", 0.6f).prim().dir (1),
                 knob ("decay", "DECAY", 0.3f, 30.0f, 4.0f, "s", 4.0f).prim().dir (1),
                 pct ("dark", "DARK", 0.5f).prim(),
                 choice ("predelay", "PREDELAY", { "0", "1/64", "1/32", "1/16", "1/8", "1/4" }, 2).prim(),
                 pct ("duck", "DUCK", 0.3f).prim(),
                 toggle ("freeze", "FREEZE").prim(),
                 momentary ("throw", "THROW").prim(),
                 pct ("mix", "MIX", 0.25f).prim().dir (1),
                 pct ("mod", "MOD", 0.3f),
                 pct ("width", "WIDTH", 0.8f),
                 toggle ("gated", "GATED"),
                 choice ("gate_time", "GATE TIME", { "1/16", "1/8", "1/4", "1/2" }, 1),
                 knob ("lo_cut", "LO CUT", 20.0f, 800.0f, 120.0f, "Hz", 160.0f),
                 pct ("duck_shape", "DUCK SHAPE", 0.5f),
                 choice ("input", "INPUT", { "ALWAYS", "THROW ONLY" }, 0),
                 dryParam() };
    }
    int latency() const override { return Pocket::kLatency; }
    double tailSeconds() const override { return 30.0; }
    void prepare (double rate, int mb) override
    {
        sr = rate;
        fdn.prepare (rate);
        for (auto& d : pre) d.prepare ((int) (rate * 2.5));
        for (auto& d : dry) d.prepare (Pocket::kLatency + 4);
        pocket.prepare (rate, mb);
        wet.setSize (2, mb);
        dryFol.set (0.002f, 0.25f, rate);
        onsetFast.set (0.0005f, 0.01f, rate); onsetSlow.set (0.01f, 0.3f, rate);
        dryRamp.prepare (rate);
        reset();
    }
    void reset() override
    {
        fdn.clear();
        for (auto& d : pre) d.clear();
        for (auto& d : dry) d.clear();
        for (auto& f : lo) f.reset();
        pocket.reset();
        gateZ = 0.0f; gateCount = 0;
    }
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        const int n = c.n;
        wet.setSize (2, n, false, false, true);
        const bool freeze = c.b (FREEZE), throwOn = c.b (THROW);
        fdn.set (c.v (SIZE), c.v (DECAY), c.v (DARK), c.v (MOD), freeze, c.n);
        static const double preB[] = { 0.0, 1.0 / 16, 0.125, 0.25, 0.5, 1.0 };
        const float preS = (float) (preB[juce::jlimit (0, 5, c.ci (PREDELAY))] * c.samplesPerBeat());
        for (auto& f : lo) f.setHz (c.v (LOCUT), sr);
        static const double gB[] = { 0.25, 0.5, 1.0, 2.0 };
        const int gateLen = (int) (gB[juce::jlimit (0, 3, c.ci (GATETIME))] * c.samplesPerBeat());
        const bool gated = c.b (GATED), throwOnly = c.ci (INPUT) == 1;
        float inTarget = freeze ? 0.0f : (throwOnly ? (throwOn ? 1.0f : 0.0f) : (throwOn ? 1.8f : 1.0f));
        auto* L = io.getWritePointer (0); auto* R = io.getWritePointer (1);
        auto* wl = wet.getWritePointer (0); auto* wr = wet.getWritePointer (1);
        const float width = c.v (WIDTH);
        for (int i = 0; i < n; ++i)
        {
            inGain += 0.002f * (inTarget - inGain);
            pre[0].push (lo[0].hp (L[i]) * inGain); pre[1].push (lo[1].hp (R[i]) * inGain);
            const float il = preS > 1 ? pre[0].read (preS) : pre[0].tap (0), ir = preS > 1 ? pre[1].read (preS) : pre[1].tap (0);
            float ol, orr;
            fdn.tick (il, ir, ol, orr, freeze);
            if (gated)
            {
                const float m = 0.5f * (L[i] + R[i]);
                if (onsetFast.tick (m) > onsetSlow.tick (m) * 1.8f && onsetFast.z > 0.003f) gateCount = gateLen;
                const float gt = gateCount-- > 0 ? 1.0f : 0.0f;
                gateZ = gt + 0.995f * (gateZ - gt);
                ol *= gateZ; orr *= gateZ;
            }
            const float mid = 0.5f * (ol + orr), side = 0.5f * (ol - orr) * width;
            wl[i] = mid + side; wr[i] = mid - side;
        }
        // Duck: kick pocket + dry-follow (tails bloom in the gaps).
        const float duck = c.v (DUCK);
        pocket.process (c, wet.getArrayOfWritePointers(), 2, n, duck, c.v (DUCKSHAPE), nullptr, duck > 0.001f);
        const float mix = c.v (MIX);
        const float dryT = c.b (DRYON) ? 1.0f : 0.0f;
        for (int i = 0; i < n; ++i)
        {
            dry[0].push (L[i]); dry[1].push (R[i]);
            const float dl = dry[0].tap (Pocket::kLatency), dr = dry[1].tap (Pocket::kLatency);
            const float env = dryFol.tick (0.5f * (dl + dr));
            const float dg = 1.0f - duck * 0.6f * clamp01 (env * 4.0f);
            const float dryG = dryRamp.tick (dryT);
            L[i] = dl * dryG + wl[i] * mix * dg;
            R[i] = dr * dryG + wr[i] * mix * dg;
        }
    }
    int displayHeight() const override { return 70; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override { pocket.paint (g, r); }
private:
    double sr = 48000.0;
    Fdn fdn;
    DryRamp dryRamp;
    DelayLine pre[2], dry[2];
    OnePole lo[2];
    Pocket pocket;
    Follower dryFol, onsetFast, onsetSlow;
    juce::AudioBuffer<float> wet;
    float inGain = 1.0f, gateZ = 0.0f;
    int gateCount = 0;
};
ModulePtr makeREVERB() { return std::make_unique<ReverbModule>(); }

// =====================================================================================  REPEAT
// Beat repeat. ROLL captures LENGTH of audio from the (quantised) start and loops it, each pass
// quieter / lower / darker / shorter. Release crossfades back to the input.
// The capture is its own buffer, recorded from the roll's start for up to 4 s and then protected:
// the loop never picks up later input, however long it is held (LENGTH can still grow mid-roll
// into audio recorded after the start, as the record position always runs ahead of the loop).
class RepeatModule : public Module
{
public:
    enum { ROLL, LENGTH, DECAY, PITCH, GATEP, FILT, MIX, QUANT };
    std::string id() const override { return "REPEAT"; }
    std::vector<ParamSpec> params() const override
    {
        return { momentary ("roll", "ROLL").prim(),
                 choice ("length", "LENGTH", { "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T" }, 2).prim(),
                 pct ("decay", "DECAY", 0.1f).prim(),
                 knob ("pitch", "PITCH", -12.0f, 0.0f, 0.0f, "st").prim(),
                 pct ("gate", "GATE", 1.0f).prim(),
                 pct ("filter", "FILTER", 0.0f).prim(),
                 pct ("mix", "MIX", 1.0f).prim(),
                 choice ("quant", "QUANT", { "OFF", "1/16", "1/8", "1/4" }, 1).prim() };
    }
    void prepare (double rate, int) override
    {
        sr = rate;
        for (auto& d : cap) d.assign ((size_t) (rate * 4) + 4, 0.0f);
        reset();
    }
    void reset() override
    {
        for (auto& d : cap) std::fill (d.begin(), d.end(), 0.0f);
        state = Idle; x = 0.0f; rec = 0;
        for (auto& f : lp) f.reset();
    }
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        static const double lenB[] = { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
        static const double qB[] = { 0.0, 0.25, 0.5, 1.0 };
        const int capSize = (int) cap[0].size() - 4;
        const int segLen = juce::jlimit (64, capSize, (int) (lenB[juce::jlimit (0, 5, c.ci (LENGTH))] * c.samplesPerBeat()));
        const bool want = c.b (ROLL);
        const double late = groove.beats (*c.bus, c.bpm);    // kept current, for the next roll
        int startAt = -1;
        if (want && state == Idle)
        {
            const double q = qB[juce::jlimit (0, 3, c.ci (QUANT))];
            startAt = q <= 0.0 ? 0 : c.nextGrid (q, late);   // on the groove's grid (KickGroove)
            if (startAt >= 0) state = Armed;
        }
        if (! want && (state == Rolling || state == Armed)) state = state == Armed ? Idle : Releasing;
        const float mix = c.v (MIX), decay = c.v (DECAY), pitch = c.v (PITCH), gate = 0.1f + 0.9f * c.v (GATEP), filt = c.v (FILT);
        for (int i = 0; i < c.n; ++i)
        {
            const float inL = io.getSample (0, i), inR = io.getSample (1, i);
            if (state == Armed && i >= startAt) { state = Rolling; rec = 0; pos = 0.0; pass = 0; passRate = 1.0f; passGain = 1.0f; }
            // Record from the roll's start until the capture is full; then it is protected.
            if ((state == Rolling || state == Releasing) && rec < capSize) { cap[0][(size_t) rec] = inL; cap[1][(size_t) rec] = inR; ++rec; }
            float oL = inL, oR = inR;
            if (state == Rolling || state == Releasing)
            {
                // The loop keeps playing while the release crossfades back to the input.
                if (pass > 0)
                {
                    const int ip = (int) pos;
                    const float fr = (float) (pos - ip);
                    auto at = [&] (int ch, int k) { return cap[(size_t) ch][(size_t) ((k % segLen + segLen) % segLen)]; };
                    auto herm = [&] (int ch)
                    {
                        const float xm1 = at (ch, ip - 1), x0 = at (ch, ip), x1 = at (ch, ip + 1), x2 = at (ch, ip + 2);
                        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                        return ((c3 * fr + c2) * fr + c1) * fr + x0;
                    };
                    const float l = herm (0), r = herm (1);
                    const float within = (float) (pos / segLen);
                    const float gt = within < gate ? std::min (1.0f, (gate - within) * 60.0f) * std::min (1.0f, within * 400.0f + 0.05f) : 0.0f;
                    const float cut = expMap (1.0f - filt * std::min (1.0f, pass / 8.0f), 300.0f, 20000.0f);
                    if (((c.samplePos + i) & 15) == 0) for (auto& f : lp) f.set (cut, 0.7071f, sr);
                    oL = lp[0].tickLP (l * passGain * gt); oR = lp[1].tickLP (r * passGain * gt);
                    pos += passRate;
                }
                else pos += 1.0;
                if (pos >= segLen) { pos -= segLen; ++pass; passRate = semis (pitch * (float) pass / 4.0f); passGain = std::pow (1.0f - decay * 0.5f, (float) pass); }
            }
            const float target = state == Rolling ? 1.0f : 0.0f;
            x += 0.004f * (target - x);
            if (state == Releasing && x < 0.001f) { state = Idle; x = 0.0f; }
            const float m = x * mix;
            io.getWritePointer (0)[i] = inL * (1.0f - m) + oL * m;
            io.getWritePointer (1)[i] = inR * (1.0f - m) + oR * m;
        }
        dispPass = state == Rolling ? pass : -1;
    }
    int displayHeight() const override { return 60; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        ui::frame (g, r, "ROLL");
        const int p = dispPass.load();
        ui::label (g, p < 0 ? "IDLE" : "PASS " + juce::String (p), r.reduced (10, 16), p < 0 ? ui::kDim : ui::kGreen, 16.0f, juce::Justification::centred);
    }
private:
    enum State { Idle, Armed, Rolling, Releasing };
    double sr = 48000.0;
    std::vector<float> cap[2];
    int rec = 0, pass = 0;
    double pos = 0;
    State state = Idle;
    float x = 0, passRate = 1.0f, passGain = 1.0f;
    Svf lp[2];
    std::atomic<int> dispPass { -1 };
    KickGroove groove;                    // where the groove lands against Live's beat
};
ModulePtr makeREPEAT() { return std::make_unique<RepeatModule>(); }
} // namespace sf
