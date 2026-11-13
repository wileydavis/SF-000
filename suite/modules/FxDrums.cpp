// DRUM BUS (SF-000: this module is SF-000's own, not the suite's)
#include "Kit.h"

namespace sf
{
// ==================================================================================  DRUM BUS
// What a drum bus does to drums that play through it together: a gentle compressor, drive, an
// extra distortion on the highs alone, a transient shaper that leaves the lows be, and a tuned
// resonance under it all. Each stage is mild; together they make the drums one thing: denser,
// punchier and heavier.
//
// At rest it is not there. With DRIVE, CRUNCH, PUNCH, BOOM and DAMP at nought (and LEVEL at 0 dB)
// the sound is left exactly as it came, sample for sample; the first knob to move brings the bus
// in over a hundredth of a second.
//
// Everything is worked out against the bus's own level (a slow reading of its peaks: up at once,
// down over half a minute, held through silence), so the knobs do the same whatever the lanes'
// levels are, and DRIVE makes the drums denser without raising their peaks: what goes on to the
// master is no hotter. No look-ahead and no oversampling: the shapers are anti-aliased by their
// antiderivatives (ADAA), half a sample of delay, so the drums are not held back.
//
//   in -> COMP (comes in with DRIVE) -> DRIVE (SOFT / MEDIUM / HARD) -> CRUNCH (the highs)
//      -> PUNCH (above the sub) -> + BOOM (a resonance at BOOM TUNE, rung by the lows) -> DAMP -> MIX -> LEVEL
// Nothing in it splits the sound into bands and puts it together again: CRUNCH adds what it does
// to the highs, PUNCH is a shelf above the sub whose gain moves with the hits. So a knob at nought
// leaves no trace, and a hard-driven drum's peaks are not thrown about by a crossover's phase.
//
// LANE 1 to 11 are read by the rig, not here: which lanes play through the bus.
namespace
{
// A waveshaper with first-order antiderivative anti-aliasing: the output is the average of the
// curve between one input and the next.
struct Shaper
{
    enum { soft, medium, hard, sine };
    int kind = soft;
    double x1 = 0.0, f1 = 0.0;

    static double lncosh (double x)     { const double a = std::fabs (x); return a > 18.0 ? a - 0.6931471805599453 : std::log (std::cosh (a)); }
    static double curve (int k, double x)
    {
        switch (k)
        {
            case medium: return x >= 0.0 ? std::tanh (1.3 * x) / 1.3 : std::tanh (0.8 * x) / 0.8;      // lopsided, as a valve is: even harmonics
            case hard:   return std::max (-1.0, std::min (1.0, x));
            case sine:   return std::sin (x);
            default:     return std::tanh (x);
        }
    }
    static double integral (int k, double x)
    {
        switch (k)
        {
            case medium: return x >= 0.0 ? lncosh (1.3 * x) / 1.69 : lncosh (0.8 * x) / 0.64;
            case hard:   { const double a = std::fabs (x); return a <= 1.0 ? 0.5 * x * x : a - 0.5; }
            case sine:   return -std::cos (x);
            default:     return lncosh (x);
        }
    }
    void set (int k)                    { if (k != kind) { kind = k; f1 = integral (kind, x1); } }
    void reset()                        { x1 = 0.0; f1 = integral (kind, 0.0); }
    float tick (float xf)
    {
        const double x = xf, fx = integral (kind, x), d = x - x1;
        const double y = std::fabs (d) < 1.0e-6 ? curve (kind, 0.5 * (x + x1)) : (fx - f1) / d;
        x1 = x; f1 = fx;
        return (float) y;
    }
};

// A high shelf whose gain may move on every sample (a trapezoidal state-variable filter, after
// Simper): at a gain of one what comes out is what went in, exactly. One gain for both channels.
struct MovingShelf
{
    float g0 = 0.02f, k = 1.4142f, ic1[2] = { 0, 0 }, ic2[2] = { 0, 0 };
    float a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;
    void set (float hz, double sr)      { g0 = std::tan (kPi * hz / (float) sr); }
    void reset()                        { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }
    void gain (float top)               // the gain above the shelf, as an amplitude
    {
        const float A = std::sqrt (std::max (1.0e-3f, top)), g = g0 * std::sqrt (A);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        m0 = A * A; m1 = k * (1.0f - A) * A; m2 = 1.0f - A * A;
    }
    float tick (float x, int ch)
    {
        const float v3 = x - ic2[ch], v1 = a1 * ic1[ch] + a2 * v3, v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
        ic1[ch] = 2.0f * v1 - ic1[ch]; ic2[ch] = 2.0f * v2 - ic2[ch];
        return m0 * x + m1 * v1 + m2 * v2;
    }
};
} // namespace

class DrumBusModule : public Module
{
public:
    static constexpr int kIns = 11;
    enum { DRIVE, CRUNCH, PUNCH, BOOM, DAMP, TUNE, DECAY, MIX, TYPE, COMP, TOKEY, LEVEL, IN1 };
    std::string id() const override { return "DRUMBUS"; }
    std::vector<ParamSpec> params() const override
    {
        std::vector<ParamSpec> p = {
                 pct ("drive", "DRIVE", 0.0f).prim().dir (1),
                 pct ("crunch", "CRUNCH", 0.0f).prim().dir (1),
                 knob ("punch", "PUNCH", -1.0f, 1.0f, 0.0f, "%").prim(),
                 pct ("boom", "BOOM", 0.0f).prim().dir (1),
                 pct ("damp", "DAMP", 0.0f),
                 knob ("boom_tune", "BOOM TUNE", 30.0f, 90.0f, 50.0f, "Hz"),
                 pct ("boom_decay", "BOOM DECAY", 0.4f),
                 pct ("mix", "MIX", 1.0f),
                 choice ("type", "TYPE", { "SOFT", "MEDIUM", "HARD" }, 0),
                 toggle ("comp", "COMP", true),
                 toggle ("to_key", "BOOM TO KEY", true),
                 levelParam() };
        static const char* const ids[kIns] = { "in_1", "in_2", "in_3", "in_4", "in_5", "in_6", "in_7", "in_8", "in_9", "in_10", "in_11" };
        static const char* const names[kIns] = { "LANE 1", "LANE 2", "LANE 3", "LANE 4", "LANE 5", "LANE 6", "LANE 7", "LANE 8", "LANE 9", "LANE 10", "LANE 11" };
        for (int k = 0; k < kIns; ++k) p.push_back (toggle (ids[k], names[k], k < 5));       // (the rig's first five lanes are its drums)
        return p;
    }
    double tailSeconds() const override { return 2.0; }
    void prepare (double rate, int) override
    {
        sr = rate;
        attRef = 1.0f - std::exp (-1.0f / (0.005f * (float) sr));
        relRef = 1.0f - std::exp (-1.0f / (30.0f * (float) sr));
        compAtt = std::exp (-1.0f / (0.008f * (float) sr));
        compRel = std::exp (-1.0f / (0.090f * (float) sr));
        engStep = 1.0f / (0.010f * (float) sr);
        eFast.set (0.0003f, 0.020f, sr);
        eMed.set (0.015f, 0.020f, sr);
        eHold.set (0.0003f, 0.250f, sr);
        gUp = 1.0f - std::exp (-1.0f / (0.0004f * (float) sr));
        gDown = 1.0f - std::exp (-1.0f / (0.004f * (float) sr));
        for (auto& f : body) f.set (100.0f, 0.7071f, sr);
        shelf.set (160.0f, sr);
        for (auto& f : sub) f.set (120.0f, 0.7071f, sr);
        for (auto& f : hi) f.set (1600.0f, 0.7071f, sr);
        for (auto& f : dc) f.setHz (8.0f, sr);
        for (auto& f : dcDry) f.setHz (8.0f, sr);
        reset();
    }
    void reset() override
    {
        ref = kRefStart; eng = 0.0f;
        still();
    }

    // The boom's pitch: BOOM TUNE, or with BOOM TO KEY the nearest note of the rig's key to it.
    static float boomHz (float tune, bool toKey, int keyRoot)
    {
        if (! toKey || keyRoot < 0) return tune;
        return juce::jlimit (28.0f, 95.0f, tune * semis (keySnap (tune, keyRoot)));
    }

    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer&) override
    {
        const int n = c.n;
        auto* L = io.getWritePointer (0); auto* R = io.getWritePointer (1);
        float lv0; const float lv1 = levelDb (c, LEVEL, lv0);
        const float dr0 = c.v0 (DRIVE), dr1 = c.v (DRIVE);
        const float cr0 = c.v0 (CRUNCH), cr1 = c.v (CRUNCH);
        const float punch = c.v (PUNCH);
        const float bm0 = c.v0 (BOOM), bm1 = c.v (BOOM);
        const float damp = c.v (DAMP);
        auto nought = [] (float a, float b) { return std::fabs (a) < 1.0e-5f && std::fabs (b) < 1.0e-5f; };
        const bool rest = nought (dr0, dr1) && nought (cr0, cr1) && nought (punch, c.v0 (PUNCH)) && nought (bm0, bm1) && nought (damp, c.v0 (DAMP))
                       && std::fabs (lv0 - 1.0f) < 1.0e-5f && std::fabs (lv1 - 1.0f) < 1.0e-5f;
        if (rest && eng <= 0.0f)
        {
            // at rest: nothing is touched. The level is still read, so the bus knows it the moment it comes in.
            for (int i = 0; i < n; ++i) level (std::max (std::fabs (L[i]), std::fabs (R[i])));
            if (! wasStill) { still(); wasStill = true; }
            grNow = 0.0f; boomNow = 0.0f;
            return;
        }
        wasStill = false;

        const int type = juce::jlimit (0, 2, c.ci (TYPE));
        for (auto& s : drive) s.set (type);
        for (auto& s : fold) s.set (Shaper::sine);
        const bool comp = c.b (COMP);
        // DRIVE: up to +24 dB into the shaper, against a signal whose peaks stand at one. What
        // comes out is scaled so a full-scale peak comes out a full-scale peak: denser, no hotter.
        auto driveGain = [] (float d) { return dbToGain (24.0f * d); };
        auto makeUp = [type] (float g)
        {
            const double top = type == Shaper::medium ? 0.5 * (Shaper::curve (type, g) - Shaper::curve (type, -g)) : Shaper::curve (type, g);
            return (float) (1.0 / std::max (0.05, top));
        };
        const float g0 = driveGain (dr0), g1 = driveGain (dr1);
        const float m0 = makeUp (g0), m1 = makeUp (g1);
        const float mix0 = c.v0 (MIX), mix1 = c.v (MIX);
        // BOOM: a resonance that rings for BOOM DECAY (the time to fall 60 dB: 80 ms to 1.5 s).
        // A longer ring is fed as hard as a short one, so BOOM DECAY lengthens it and does not thin it.
        const float hz = boomHz (c.v (TUNE), c.b (TOKEY), c.keyRoot);
        const float t60 = expMap (c.v (DECAY), 0.08f, 1.5f);
        const float q = std::max (0.7f, kPi * hz * t60 / 6.91f);
        for (auto& f : res) f.set (hz, q, sr);
        constexpr float feed = 0.25f;
        // DAMP: a low-pass on what has been made, from open down to 700 Hz
        const bool damped = damp > 1.0e-5f;
        const float dampIn = smoothstep (0.0f, 0.06f, damp);
        if (damped) for (auto& f : lp) f.set (expMap (1.0f - damp, 700.0f, 18000.0f), 0.7071f, sr);

        float grMax = 0.0f, boomPk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) (i + 1) / (float) n;
            const float in[2] = { L[i], R[i] };
            const float d = std::max (std::fabs (in[0]), std::fabs (in[1]));
            level (d);
            const float inv = 1.0f / ref;
            const float dr = dr0 + (dr1 - dr0) * t;

            // COMP: 3:1 from 6 dB under the bus's peaks, slow enough on the way in to let the hit
            // through. It comes in with the first third of DRIVE: glue and heat on one knob.
            float cg = 1.0f;
            if (comp)
            {
                const float over = gainToDb (d * inv) + 6.0f;
                const float want = over <= -3.0f ? 0.0f : over >= 3.0f ? over * 0.6667f : 0.6667f * (over + 3.0f) * (over + 3.0f) / 12.0f;
                gr = want > gr ? want + compAtt * (gr - want) : want + compRel * (gr - want);
                const float amt = smoothstep (0.0f, 0.33f, dr);
                grMax = std::max (grMax, gr * amt);
                cg = dbToGain (amt * (2.0f - gr));
            }
            else gr = 0.0f;

            const float g = g0 + (g1 - g0) * t, mk = m0 + (m1 - m0) * t;
            const float cr = cr0 + (cr1 - cr0) * t, bm = bm0 + (bm1 - bm0) * t;
            const float drIn = smoothstep (0.0f, 0.1f, dr);
            const float crIn = smoothstep (0.0f, 0.15f, cr), crGain = 1.0f + 5.0f * cr;
            float y[2], v[2], top[2];
            for (int k = 0; k < 2; ++k)
            {
                const float x = in[k] * cg * inv;                       // peaks at about one
                // DRIVE
                v[k] = dc[k].hp (x + drIn * (drive[k].tick (x * g) * mk - x));
                // CRUNCH: the highs alone through a sine, harder as it rises; the rest is left as it is
                const float h = hi[k].tickHP (v[k]);
                const float shaped = fold[k].tick (1.3f * h * crGain) / 1.3f;
                v[k] += crIn * (shaped - h);
                top[k] = body[k].tickHP (v[k]);                        // what is above 100 Hz: what PUNCH listens to
            }
            // PUNCH, above the sub: either way the hit is brought out; right of centre what follows
            // it is lifted too (fuller), left of centre it is pulled down (tighter). It is a shelf
            // whose gain moves with the hits, so the sub is left as it is and at the centre nothing
            // has been done at all.
            const float e = std::max (std::fabs (top[0]), std::fabs (top[1]));
            // (the slower reading follows the quick one, not the sound: on a noisy tail the two agree, and only a new hit parts them)
            const float fast = eFast.tick (e), med = eMed.tick (fast), hold = eHold.tick (e);
            const float hit = juce::jlimit (0.0f, 1.5f, (fast - med) / (med + 0.02f));
            const float tail = juce::jlimit (0.0f, 1.0f, (hold - fast) / (hold + 0.02f));
            const float shapeDb = std::fabs (punch) * 6.0f * hit + (punch >= 0.0f ? 8.0f : 14.0f) * punch * tail;
            const float want = dbToGain (shapeDb);
            pg += (want > pg ? gUp : gDown) * (want - pg);              // up at once for the hit, down gently after it
            shelf.gain (pg);
            eng = rest ? std::max (0.0f, eng - engStep) : std::min (1.0f, eng + engStep);
            const float lv = lv0 + (lv1 - lv0) * t;
            for (int k = 0; k < 2; ++k)
            {
                // BOOM: the lows ring a resonance at BOOM TUNE; it is held in by a soft ceiling
                const float ring = res[k].tickBP (sub[k].tickLP (v[k])) * feed;
                const float boom = fastTanh (ring * 4.0f * bm);
                boomPk = std::max (boomPk, std::fabs (boom));
                float wet = ceiling (shelf.tick (v[k], k) + boom);
                if (damped) wet += dampIn * (lp[k].tickLP (wet) - wet);
                wet *= ref;                                             // back at the bus's own level
                // (the dry sound through the same blocker as the wet, so the two are in phase at every MIX)
                const float dry = dcDry[k].hp (in[k]);
                const float mix = mix0 + (mix1 - mix0) * t;
                y[k] = (dry + mix * (wet - dry)) * lv;
            }
            // coming in from rest, or going back to it: across from the sound exactly as it came
            L[i] = in[0] + eng * (y[0] - in[0]); R[i] = in[1] + eng * (y[1] - in[1]);
        }
        grNow = grMax; boomNow = boomPk; hzNow = hz;
    }

    int displayHeight() const override { return 50; }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        ui::frame (g, r, "DRUM BUS");
        // the boom's pitch as a note, and what the compressor and the boom are doing
        const float hz = hzNow.load();
        const int note = (int) std::lround (69.0f + 12.0f * std::log2 (std::max (1.0f, hz) / 440.0f));
        static const char* const names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const juce::String where = "BOOM " + juce::String (hz, 1) + " HZ  " + names[((note % 12) + 12) % 12] + juce::String (note / 12 - 1);
        auto in = r.reduced (8, 0).withTrimmedTop (18).withHeight (14);
        ui::label (g, where, in, ui::kText, 11.0f, juce::Justification::centredLeft);
        ui::label (g, "COMP " + juce::String (-grNow.load(), 1) + " dB", in, ui::kText, 11.0f, juce::Justification::centredRight);
        auto bar = r.reduced (8, 0).withTop (in.getBottom() + 3).withHeight (5);
        g.setColour (ui::kFaint); g.fillRect (bar);
        g.setColour (ui::kText); g.fillRect (bar.withWidth (bar.getWidth() * clamp01 (boomNow.load())));
    }

private:
    // PUNCH and BOOM stand on top of what DRIVE has made, and with everything up they would hand
    // the master peaks several times the drums' own. What goes on is held under two and a half
    // times the bus's level: untouched up to one and a half, rounded over above it.
    static float ceiling (float x)
    {
        const float a = std::fabs (x);
        if (a <= 1.5f) return x;
        return std::copysign (1.5f + fastTanh (a - 1.5f), x);
    }
    // The bus's level: up to a peak at once, down slowly while it plays, held through silence.
    static constexpr float kRefStart = 0.02f;
    void level (float d)
    {
        if (d > ref) ref += attRef * (d - ref);
        else if (d > 0.05f * ref) ref += relRef * (d - ref);
        ref = std::max (ref, 1.0e-3f);
    }
    void still()
    {
        gr = 0.0f;
        for (auto& s : drive) s.reset();
        for (auto& s : fold) s.reset();
        for (auto& f : body) f.reset();
        for (auto& f : sub) f.reset();
        shelf.reset(); shelf.gain (1.0f);
        for (auto& f : dcDry) f.reset();
        for (auto& f : hi) f.reset();
        for (auto& f : res) f.reset();
        for (auto& f : lp) f.reset();
        for (auto& f : dc) f.reset();
        eFast.z = eMed.z = eHold.z = 0.0f;
        pg = 1.0f;
    }

    double sr = 48000.0;
    float ref = kRefStart, gr = 0.0f, eng = 0.0f, engStep = 0.002f, pg = 1.0f, gUp = 0.05f, gDown = 0.005f;
    bool wasStill = true;
    float attRef = 0.0f, relRef = 0.0f, compAtt = 0.0f, compRel = 0.0f;
    Shaper drive[2], fold[2];
    Svf hi[2], res[2], lp[2], body[2], sub[2];
    OnePole dc[2], dcDry[2];
    MovingShelf shelf;
    Follower eFast, eMed, eHold;
    std::atomic<float> grNow { 0.0f }, boomNow { 0.0f }, hzNow { 50.0f };
};
ModulePtr makeDRUMBUS() { return std::make_unique<DrumBusModule>(); }
} // namespace sf
