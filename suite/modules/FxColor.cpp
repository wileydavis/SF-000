// DRIVE, RADIO, DUCK, ENERGY
#include "Kit.h"
#include "../core/Oversample.h"

namespace sf
{
// =====================================================================================  DRIVE
// One CHARACTER knob from smooth to fuzz: TAPE -> TUBE -> FUZZ -> INDUSTRIAL. 4x oversampled.
class DriveModule : public Module
{
public:
    enum { DRIVE, CHAR, TONE, LOWKEEP, MIX, LEVEL, AUTO };
    std::string id() const override { return "DRIVE"; }
    std::vector<ParamSpec> params() const override
    {
        return { pct ("drive", "DRIVE", 0.2f).prim().dir (1),
                 pct ("character", "CHARACTER", 0.0f).prim().dir (1),
                 knob ("tone", "TONE", -1.0f, 1.0f, 0.0f, "%").prim(),
                 knob ("low_keep", "LOW KEEP", 0.0f, 300.0f, 0.0f, "Hz").prim(),
                 pct ("mix", "MIX", 1.0f).prim(),
                 levelParam().prim(),
                 toggle ("auto_gain", "AUTO GAIN", true).prim() };
    }
    int latency() const override { return lat; }
    void prepare (double rate, int) override
    {
        sr = rate;
        os.prepare (rate, 4);
        osDry.prepare (rate, 4);
        // Low-frequency group delay of the 4x path (impulse centroid = group delay at DC),
        // reported to the host.
        os.reset();
        double num = 0.0, den = 0.0;
        for (int b = 0, idx = 0; b < 16; ++b)
        {
            float x[Oversampler::kBlock] {};
            if (b == 0) x[0] = 1.0f;
            os.process (0, x, Oversampler::kBlock, [] (float v) { return v; });
            for (int i = 0; i < Oversampler::kBlock; ++i, ++idx) { num += (double) idx * x[i]; den += x[i]; }
        }
        lat = den > 1e-6 ? juce::jlimit (0, 32, (int) std::lround (num / den)) : 0;
        inF.set (0.05f, 0.4f, rate); outF.set (0.05f, 0.4f, rate);
        reset();
    }
    void reset() override
    {
        os.reset(); osDry.reset();
        for (auto& x : xo) x.reset();
        for (auto& x : xoDry) x.reset();
        for (auto& t : tilt) for (auto& b : t) b.reset();
        inF.z = outF.z = 0.0f;
        autoG = 1.0f;
        keepAmt = 0.0f;
    }
    static float shape (float x, float ch, float bias)
    {
        // Four stages blended by CHARACTER.
        const float tape = fastTanh (x);
        const float tube = x >= 0 ? fastTanh (x * 1.2f) : fastTanh (x * 0.7f) * 1.1f;
        const float fuzz = hardClip ((x + bias) * 2.5f, 0.8f) - hardClip (bias * 2.5f, 0.8f);
        const float fold = std::sin (x * 1.7f);
        const float pos = ch * 3.0f;
        if (pos < 1.0f) return lerp (tape, tube, pos);
        if (pos < 2.0f) return lerp (tube, fuzz, pos - 1.0f);
        return lerp (fuzz, fold * 0.9f, pos - 2.0f);
    }
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        const int n = std::min (c.n, Oversampler::kBlock);
        const float drive = c.v (DRIVE), ch = c.v (CHAR), mix = c.v (MIX);
        const float pre = dbToGain (drive * 36.0f);
        const float lowKeep = c.v (LOWKEEP);
        // The dry signal runs through an identical 4x up/down pair (no shaping), so dry and driven
        // have exactly the same phase: MIX and LOW KEEP blend without combing in the highs, and
        // MIX 0 is the clean input (only the oversampler's filters, flat to 20 kHz).
        float dry[2][Oversampler::kBlock];
        for (int k = 0; k < 2; ++k)
        {
            std::copy (io.getReadPointer (k), io.getReadPointer (k) + n, dry[k]);
            osDry.process (k, dry[k], n, [] (float x) { return x; });
        }
        const float bias = ch > 0.5f ? 0.15f * (ch - 0.5f) * 2.0f : 0.0f;
        for (int k = 0; k < 2; ++k)
            os.process (k, io.getWritePointer (k), n, [=] (float x) { return shape (x * pre, ch, bias); });
        // Tone tilt around 800 Hz (on the driven signal).
        const float t = c.v (TONE);
        for (int k = 0; k < 2; ++k)
        {
            tilt[k][0].lowShelf (800.0f, 0.6f, -t * 6.0f, sr);
            tilt[k][1].highShelf (800.0f, 0.6f, t * 6.0f, sr);
        }
        // LOW KEEP: the lows come from the dry signal, the highs blend dry and driven by MIX, and
        // all of it comes out of one crossover (dry and driven split alike), so the bands sum
        // flat at every MIX. (Blending that sum with the unsplit dry cancelled at the crossover
        // frequency: -100 dB at MIX 50 %.) Switching it on or off crossfades over 10 ms.
        const float keepTarget = lowKeep > 20.0f ? 1.0f : 0.0f;
        const float keepStep = 1.0f / (0.01f * (float) sr);
        if (lowKeep > 20.0f) { for (auto& x : xo) x.set (lowKeep, sr); for (auto& x : xoDry) x.set (lowKeep, sr); }
        // AUTO GAIN, per sample: the driven signal is brought back to the dry signal's level
        // (stereo peak followers, ~30 ms smoothing). It applies to the driven signal only; the
        // dry part of MIX and the clean lows of LOW KEEP are never touched.
        const bool autoOn = c.b (AUTO);
        const float sm = std::exp (-1.0f / (0.03f * (float) sr));
        float g0; const float g1 = levelDb (c, LEVEL, g0);
        auto* xl = io.getWritePointer (0); auto* xr = io.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            float y[2] = { tilt[0][1].tick (tilt[0][0].tick (xl[i])), tilt[1][1].tick (tilt[1][0].tick (xr[i])) };
            const float d[2] = { dry[0][i], dry[1][i] };
            const float inLvl = inF.tick (std::max (std::fabs (d[0]), std::fabs (d[1])));
            const float outLvl = outF.tick (std::max (std::fabs (y[0]), std::fabs (y[1])));
            const float target = autoOn && outLvl > 1e-4f && inLvl > 1e-4f ? juce::jlimit (0.1f, 2.0f, inLvl / outLvl) : 1.0f;
            autoG = target + sm * (autoG - target);
            keepAmt = keepTarget > keepAmt ? std::min (keepTarget, keepAmt + keepStep) : std::max (keepTarget, keepAmt - keepStep);
            for (int k = 0; k < 2; ++k)
            {
                const float wet = y[k] * autoG;
                float out = d[k] * (1.0f - mix) + wet * mix;
                if (keepAmt > 0.0f)
                {
                    float lo, hi, dlo, dhi;
                    xo[k].split (wet, lo, hi);
                    xoDry[k].split (d[k], dlo, dhi);
                    out += keepAmt * (dlo + dhi * (1.0f - mix) + hi * mix - out);
                }
                y[k] = out;
            }
            const float g = g0 + (g1 - g0) * (float) i / (float) n;
            xl[i] = y[0] * g;
            xr[i] = y[1] * g;
        }
        if (keepAmt == 0.0f) { for (auto& x : xo) x.reset(); for (auto& x : xoDry) x.reset(); }   // off: start clean next time
        dispCh = ch;
    }
    int displayHeight() const override { return 64; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        ui::frame (g, r, "CHARACTER");
        auto track = r.reduced (12, 18).removeFromTop (18);
        drawPosition (g, track, dispCh.load(), {}, {}, ui::kText);
        // The four characters under the line, where they sit on CHARACTER.
        auto names = r.reduced (12, 0).withTop (track.getBottom() + 2).withHeight (12);
        ui::label (g, "TAPE", names, ui::kFaint, 9.0f);
        ui::label (g, "TUBE", names.withTrimmedLeft (names.getWidth() * 0.30f), ui::kFaint, 9.0f);
        ui::label (g, "FUZZ", names.withTrimmedLeft (names.getWidth() * 0.60f), ui::kFaint, 9.0f);
        ui::label (g, "INDUSTRIAL", names, ui::kFaint, 9.0f, juce::Justification::centredRight);
    }
private:
    double sr = 48000.0;
    Oversampler os, osDry;
    int lat = 0;
    Biquad tilt[2][2];
    Crossover xo[2], xoDry[2];
    Follower inF, outF;
    float autoG = 1.0f, keepAmt = 0.0f;
    std::atomic<float> dispCh { 0 };
};
ModulePtr makeDRIVE() { return std::make_unique<DriveModule>(); }

// =====================================================================================  RADIO
// Transmission processor. SIGNAL 0 = barely there, 1 = clear: it narrows the band, adds drive and
// hiss, makes the dropouts, and (squared) takes CRUSH and RATE from their setting at 0 to clean at 1.
// A dropout fades out and back in on an S-curve (two one-poles, ~10 ms). SQUELCH: just before a
// dropout ends, a squeal: a sine that slides down from about the band's upper edge (a receiver
// pulling back onto the station) with a little band-passed breath around it; 6 ms raised-cosine
// onset, raised-cosine fall over 0.1-0.2 s (longer with more SQUELCH). Nothing starts or stops on a step.
class RadioModule : public Module
{
public:
    enum { SIGNAL, BAND, WIDTH, NOISE, DROPOUT, SQUELCH, RING, MIX, CRUSH, RATE };
    std::string id() const override { return "RADIO"; }
    std::vector<ParamSpec> params() const override
    {
        return { pct ("signal", "SIGNAL", 0.5f).prim().dir (1),
                 knob ("band", "BAND", 300.0f, 3000.0f, 1200.0f, "Hz", 1000.0f).prim(),
                 pct ("width", "WIDTH", 0.5f).prim(),
                 pct ("noise", "NOISE", 0.3f).prim(),
                 pct ("dropout", "DROPOUT", 0.2f).prim(),
                 pct ("squelch", "SQUELCH", 0.4f).prim(),
                 pct ("ring", "RING", 0.0f).prim(),
                 pct ("mix", "MIX", 1.0f).prim(),
                 knob ("crush", "CRUSH", 4.0f, 16.0f, 16.0f, "bit"),
                 knob ("rate", "RATE", 2000.0f, 48000.0f, 48000.0f, "Hz", 12000.0f) };
    }
    void prepare (double rate, int) override { sr = rate; reset(); }
    void reset() override
    {
        for (auto& b : bp) for (auto& f : b) f.reset();
        sq = {};
        dropLeft = 0; sqAt = -1;
        g1 = g2 = 1.0f; held[0] = held[1] = 0.0f; holdCount = 0.0f;
    }
    // CRUSH / RATE as SIGNAL sets them (squared: the crunch starts as soon as SIGNAL leaves the top).
    static float bitsAt (float crush, float sig) { return crush + (16.0f - crush) * sig * sig; }
    static float rateAt (float rate, float sig, double sr) { return rate * std::pow (std::max (48000.0f, (float) sr) / rate, sig * sig); }
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        const float sig = c.v (SIGNAL), mix = c.v (MIX);
        const float widthOct = 0.6f + c.v (WIDTH) * 3.0f * (0.4f + 0.6f * sig);
        const float lo = c.v (BAND) / std::pow (2.0f, widthOct * 0.5f), hi = c.v (BAND) * std::pow (2.0f, widthOct * 0.5f);
        for (int k = 0; k < 2; ++k) { bp[k][0].set (lo, 0.8f, sr); bp[k][1].set (lo, 0.8f, sr); bp[k][2].set (std::min (hi, 18000.0f), 0.8f, sr); bp[k][3].set (std::min (hi, 18000.0f), 0.8f, sr); }
        const float noiseAmt = c.v (NOISE) * (1.0f - sig * 0.75f) * 0.12f;
        const float dropProb = c.v (DROPOUT) * (1.0f - sig) * 6.0f / (float) sr;   // events per sample
        const float ring = c.v (RING);
        const float sq0 = c.v0 (SQUELCH), sq1 = c.v (SQUELCH);
        const float bits = std::pow (2.0f, bitsAt (c.v (CRUSH), sig) - 1.0f);
        const float hold = (float) (sr / rateAt (c.v (RATE), sig, sr));
        const bool sampleHold = hold > 1.0001f;
        const float gk = 1.0f - std::exp (-1.0f / (0.003f * (float) sr));         // hole fade: 2 x 3 ms
        const float sqTop = juce::jlimit (700.0f, 4200.0f, hi);                     // the squeal starts at the band's top
        const float breathNorm = 6.0f * std::sqrt ((float) sr / 48000.0f);         // white noise per Hz, any rate
        const float invN = 1.0f / (float) c.n;
        for (int i = 0; i < c.n; ++i)
        {
            if (dropLeft <= 0 && rng.uni() < dropProb)
            {
                dropLeft = (int) ((0.02 + rng.uni() * 0.13) * sr);
                sqAt = std::min (dropLeft / 2, (int) (0.03 * sr));                  // the squeal leads the signal back in
            }
            float gate = 1.0f;
            if (dropLeft > 0)
            {
                gate = 0.0f;
                if (dropLeft == sqAt) startSqueal (sqTop, sq1);
                --dropLeft;
            }
            g1 += gk * (gate - g1);
            g2 += gk * (g1 - g2);
            ringPh += 47.0f / (float) sr; if (ringPh > 1.0f) ringPh -= 1.0f;
            const float rm = lerp (1.0f, std::sin (kTwoPi * ringPh), ring);
            const float squeal = sq.active ? tickSqueal (breathNorm) * (sq0 + (sq1 - sq0) * (float) i * invN) * 0.17f : 0.0f;
            bool take = true;
            if (sampleHold)
            {
                holdCount += 1.0f;
                take = holdCount >= hold;
                if (take) { holdCount -= hold; if (holdCount >= hold) holdCount = 0.0f; }
            }
            if (take) for (int k = 0; k < 2; ++k) held[k] = io.getSample (k, i);
            for (int k = 0; k < 2; ++k)
            {
                const float dry = io.getSample (k, i);
                float x = held[k];
                x = bp[k][1].tickHP (bp[k][0].tickHP (x));
                x = bp[k][3].tickLP (bp[k][2].tickLP (x));
                x = fastTanh (x * (2.0f - sig)) * rm;
                if (bits < 32000.0f) x = std::round (x * bits) / bits;
                x = x * g2 + rng.next() * noiseAmt + squeal;
                io.setSample (k, i, dry * (1.0f - mix) + x * mix);
            }
        }
        dispDrop = g2;
    }
    int displayHeight() const override { return 50; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        ui::frame (g, r, "CARRIER");
        ui::label (g, dispDrop.load() > 0.5f ? "LOCK" : "DROP", r.reduced (10, 14), dispDrop.load() > 0.5f ? ui::kGreen : ui::kAmber, 14.0f, juce::Justification::centred);
    }
private:
    struct Squeal
    {
        bool active = false;
        int stage = 0, t = 0, onsetN = 1, fallN = 1, coefIn = 0;   // stage 1: onset, 2: fall
        float amp = 0.0f, a0 = 0.0f;
        float oct = 11.0f, octEnd = 10.0f, glideK = 0.001f;     // pitch as log2 Hz
        double phase = 0.0;
        float vib = 0.0f;
        Svf breath[2];
    };
    // A new squeal takes over from wherever the last one is (level, phase), so it never jumps.
    void startSqueal (float topHz, float amount)
    {
        if (amount <= 0.0f) return;
        const float len = 0.10f + 0.12f * amount;                                   // seconds, onset to silence
        const float startHz = std::min (4200.0f, topHz * (0.85f + 0.4f * rng.uni()));
        const float endHz = std::max (220.0f, startHz * (0.35f + 0.25f * rng.uni()));
        sq.active = true;
        sq.stage = 1; sq.t = 0;
        sq.a0 = sq.amp;
        sq.onsetN = std::max (1, (int) (0.006 * sr));
        sq.fallN = std::max (1, (int) (len * sr) - sq.onsetN);
        sq.oct = std::log2 (startHz);
        sq.octEnd = std::log2 (endHz);
        sq.glideK = 1.0f - std::exp (-1.0f / (len * 0.33f * (float) sr));
        sq.coefIn = 0;
    }
    // Onset: raised cosine from where it was to full in 6 ms. Fall: raised cosine from full to
    // silence (it ends on zero, with zero slope). The pitch slides down all the way.
    float tickSqueal (float breathNorm)
    {
        ++sq.t;
        if (sq.stage == 1)
        {
            sq.amp = sq.a0 + (1.0f - sq.a0) * 0.5f * (1.0f - std::cos (kPi * (float) sq.t / (float) sq.onsetN));
            if (sq.t >= sq.onsetN) { sq.stage = 2; sq.t = 0; }
        }
        else
        {
            if (sq.t >= sq.fallN) { sq.active = false; sq.stage = 0; sq.amp = 0.0f; return 0.0f; }
            sq.amp = 0.5f * (1.0f + std::cos (kPi * (float) sq.t / (float) sq.fallN));
        }
        sq.oct += sq.glideK * (sq.octEnd - sq.oct);
        sq.vib += 6.5f / (float) sr; if (sq.vib > 1.0f) sq.vib -= 1.0f;
        const float hz = std::exp2 (sq.oct) * (1.0f + 0.008f * std::sin (kTwoPi * sq.vib));
        sq.phase += hz / sr; if (sq.phase >= 1.0) sq.phase -= 1.0;
        if (sq.coefIn-- <= 0) { for (auto& f : sq.breath) f.set (hz, 6.0f, sr); sq.coefIn = 15; }
        // Two band-passes: breath close around the tone, nothing hissing above it.
        const float breath = sq.breath[1].tickBP (sq.breath[0].tickBP (rng.next()) * (1.0f / 6.0f)) * (1.0f / 6.0f) * breathNorm;
        return (0.8f * std::sin (kTwoPi * (float) sq.phase) + breath) * sq.amp;
    }
    double sr = 48000.0;
    Svf bp[2][4];
    Squeal sq;
    Noise rng;
    int dropLeft = 0, sqAt = -1;
    float g1 = 1.0f, g2 = 1.0f, ringPh = 0.0f, holdCount = 0.0f, held[2] {};
    std::atomic<float> dispDrop { 1.0f };
};
ModulePtr makeRADIO() { return std::make_unique<RadioModule>(); }

// =====================================================================================  DUCK
class DuckModule : public Module
{
public:
    enum { DEPTH, SHAPE, SOURCE, RATE, CURVE, MIX };
    std::string id() const override { return "DUCK"; }
    bool sidechain() const override { return true; }
    std::vector<ParamSpec> params() const override
    {
        return { pct ("depth", "DEPTH", 0.6f).prim().dir (1),
                 pct ("shape", "SHAPE", 0.4f).prim().dir (1),
                 choice ("source", "SOURCE", { "KICK BUS", "SIDECHAIN", "GRID" }, 0).prim(),
                 choice ("rate", "RATE", { "1/2", "1/4", "1/8" }, 1).prim(),
                 pct ("curve", "CURVE", 0.5f).prim(),
                 pct ("mix", "MIX", 1.0f).prim() };
    }
    int latency() const override { return Pocket::kLatency; }
    void prepare (double rate, int mb) override { sr = rate; pocket.prepare (rate, mb); gz = 1.0f; }
    void reset() override { pocket.reset(); gz = 1.0f; }
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>* side, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        const int src = c.ci (SOURCE);
        const float depth = c.v (DEPTH) * c.v (MIX);
        const double late = groove.beats (*c.bus, c.bpm);         // the pump on the groove's beat (KickGroove), kept current
        pocket.process (c, io.getArrayOfWritePointers(), 2, c.n, depth, c.v (SHAPE), side, src != 2, src == 1);
        if (src == 2)
        {
            static const double rB[] = { 2.0, 1.0, 0.5 };
            const double per = rB[juce::jlimit (0, 2, c.ci (RATE))];
            const float pw = 0.3f + c.v (CURVE) * 3.0f;
            const double look = Pocket::kLatency * c.beatsPerSample();
            for (int i = 0; i < c.n; ++i)
            {
                double ph = std::fmod (c.ppqAt (i) - look - late, per) / per;
                if (ph < 0) ph += 1.0;
                const float shapeV = std::pow ((float) ph, pw);
                const float g = 1.0f - depth * (1.0f - shapeV);
                gz = g + 0.9f * (gz - g);
                io.getWritePointer (0)[i] *= gz;
                io.getWritePointer (1)[i] *= gz;
            }
        }
    }
    int displayHeight() const override { return 80; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override { pocket.paint (g, r); }
private:
    double sr = 48000.0;
    Pocket pocket;
    float gz = 1.0f;
    KickGroove groove;                    // where the groove lands against Live's beat
};
ModulePtr makeDUCK() { return std::make_unique<DuckModule>(); }

// =====================================================================================  ENERGY
// Group energy: one bipolar knob, centre = neutral. Down: low-pass + space + softer.
// Up: high-pass with resonant edge + drive + 1/16 echo + chop. PROFILE sets how far each side goes.
// DRUMS [Nov 10]: the last eighth of the way down fades the part out, space and echo with it, to
// nothing at the bottom ("on the kick energy knob ... when it's at zero, the kick is entirely
// removed from the mix": a low-pass leaves a kick's thump).
class EnergyModule : public Module
{
public:
    enum { ENERGY, PROFILE, RES, DRIVEAMT, SPACEAMT, ECHOAMT, GATEAMT, LEVEL };
    std::string id() const override { return "ENERGY"; }
    std::vector<ParamSpec> params() const override
    {
        return { bip ("energy", "ENERGY").prim().dir (1),
                 choice ("profile", "PROFILE", { "DRUMS", "BASS", "MUSIC", "VOICE", "MASTER" }, 2).prim(),
                 pct ("res", "RES", 0.3f).prim(),
                 pct ("drive_amt", "DRIVE AMT", 0.4f).prim(),
                 pct ("space_amt", "SPACE AMT", 0.4f).prim(),
                 pct ("echo_amt", "ECHO AMT", 0.3f).prim(),
                 pct ("gate_amt", "GATE AMT", 0.0f).prim(),
                 levelParam().prim() };
    }
    void prepare (double rate, int mb) override
    {
        sr = rate; filt.prepare (rate); fdn.prepare (rate);
        for (auto& d : echo) d.prepare ((int) (rate * 2));
        os.prepare (rate, 2);
        juce::ignoreUnused (mb);
        reset();
    }
    void reset() override
    {
        filt.reset(); fdn.clear(); os.reset();
        for (auto& d : echo) d.clear();
        for (auto& f : echoLp) f.reset();
        echoFb[0] = echoFb[1] = 0.0f; chopZ = 1.0f; fdnTail = 0;
    }
    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        static const float lpTravel[] = { 0.85f, 1.0f, 1.0f, 0.8f, 0.7f };
        static const float hpTravel[] = { 0.7f, 0.5f, 1.0f, 0.75f, 0.6f };
        const int prof = juce::jlimit (0, 4, c.ci (PROFILE));
        auto mapKnob = [&] (float k)
        {
            const float e = k - 0.5f;
            return 0.5f + (e < 0 ? e * lpTravel[prof] : e * hpTravel[prof]);
        };
        const float e = c.v (ENERGY) - 0.5f;
        const float dn = clamp01 (-e * 2.0f), up = clamp01 (e * 2.0f);
        const int n = c.n;
        // Drive (up side) before the filter, 2x oversampled. Always through the oversampler (its
        // delay never switches in or out as the knob passes centre), and blended in from exactly
        // clean at 0, so the level rises smoothly with the drive instead of stepping.
        const float drv = up * c.v (DRIVEAMT);
        {
            const float gIn = 1.0f + drv * 6.0f, comp = 1.4f / (1.0f + drv * 2.0f);
            for (int k = 0; k < 2; ++k)
                os.process (k, io.getWritePointer (k), n, [drv, gIn, comp] (float x) { return x + drv * (fastTanh (x * gIn) * comp - x); });
        }
        filt.process (io.getArrayOfWritePointers(), 2, n, mapKnob (c.v0 (ENERGY)), mapKnob (c.v (ENERGY)), c.v (RES) * (0.4f + 0.6f * up));
        // Space (down side) and echo (up side).
        const float space = dn * c.v (SPACEAMT) * 0.5f, ech = up * c.v (ECHOAMT) * 0.5f;
        fdn.set (0.45f, 2.5f, 0.6f, 0.2f, false);
        const float echoD = (float) (0.25 * c.samplesPerBeat());
        for (auto& f : echoLp) f.set (3500.0f, 0.7071f, sr);
        const float chop = up * c.v (GATEAMT);
        const double late = groove.beats (*c.bus, c.bpm);
        for (int i = 0; i < n; ++i)
        {
            const float l = io.getSample (0, i), r = io.getSample (1, i);
            float rl = 0, rr = 0;
            if (space > 0.001f || fdnTail > 0) fdn.tick (l * space, r * space, rl, rr, false);
            echo[0].push (l + echoFb[0] * 0.35f); echo[1].push (r + echoFb[1] * 0.35f);
            echoFb[0] = echoLp[0].tickLP (echo[0].read (echoD)); echoFb[1] = echoLp[1].tickLP (echo[1].read (echoD));
            float g = 1.0f;
            if (chop > 0.001f)
            {
                double ph = std::fmod ((c.ppqAt (i) - late) * 4.0, 1.0);   // the groove's sixteenths (KickGroove)
                if (ph < 0.0) ph += 1.0;
                const float tgt = ph < 0.5 ? 1.0f : 1.0f - chop;
                chopZ = tgt + 0.995f * (chopZ - tgt);
                g = chopZ;
            }
            io.setSample (0, i, (l + rl + echoFb[0] * ech) * g);
            io.setSample (1, i, (r + rr + echoFb[1] * ech) * g);
        }
        // The space keeps ringing out after the knob leaves the down side (4 s = 2.5 s RT60 to -96 dB).
        fdnTail = space > 0.001f ? (int) (4.0 * sr) : std::max (0, fdnTail - n);
        // DRUMS: out of the mix at the bottom (raised cosine over the last eighth of the knob).
        if (prof == 0)
        {
            auto gone = [] (float knob) { const float x = clamp01 ((0.125f - knob) / 0.125f); return 0.5f + 0.5f * std::cos (3.14159265f * x); };
            const float a = gone (c.v0 (ENERGY)), b = gone (c.v (ENERGY));
            if (a < 1.0f || b < 1.0f)
                for (int i = 0; i < n; ++i)
                {
                    const float g = a + (b - a) * (float) (i + 1) / (float) n;
                    io.getWritePointer (0)[i] *= g;
                    io.getWritePointer (1)[i] *= g;
                }
        }
        float g0; const float g1 = levelDb (c, LEVEL, g0);
        applyGain (io, g0, g1);
        disp = c.v (ENERGY);
    }
    int displayHeight() const override { return 60; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        ui::frame (g, r, "ENERGY");
        drawPosition (g, r.reduced (12, 20), disp.load(), "DOWN", "UP", std::abs (disp.load() - 0.5f) < 0.02f ? ui::kDim : ui::kText);
    }
private:
    double sr = 48000.0;
    DJFilter filt;
    Oversampler os;
    Fdn fdn;
    int fdnTail = 0;
    DelayLine echo[2];
    Svf echoLp[2];
    float echoFb[2] {}, chopZ = 1.0f;
    std::atomic<float> disp { 0.5f };
    KickGroove groove;                    // where the groove lands against Live's beat
};
ModulePtr makeENERGY() { return std::make_unique<EnergyModule>(); }
} // namespace sf
