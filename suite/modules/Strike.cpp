// STRIKE: blast-excited resonant objects.
// A pressure wave (Friedlander waveform: sharp overpressure, then a negative phase) from a charge
// of POWER at DISTANCE hits an OBJECT (bell, plate, bar, tube, membrane, gong) modelled as up to
// 16 modes. The blast scales like the real thing: its duration grows with the cube root of the
// charge, its level falls with distance, and distance takes the highs away (air absorption).
// Big hits raise the pitch for a moment (TENSION); above ~70 % POWER the object FRACTURES:
// modes split and detune, debris rattles, and the output saturates (2x oversampled).
#include "Kit.h"
#include "../core/Oversample.h"
#include "Calibration.h"

namespace sf
{
namespace
{
struct ObjectModel
{
    const char* name;
    int modes;
    float ratio[16];
    float amp[16];
    float pitchRatio;        // partial heard as the pitch (KEY snaps it to the key root)
    float decayMul, tension, gain;
};
const ObjectModel kObjects[] = {
    { "BELL", 11, { 0.5f, 1.0f, 1.183f, 1.506f, 2.0f, 2.514f, 2.662f, 3.011f, 4.166f, 5.433f, 6.796f },
                  { 0.6f, 0.9f, 0.8f, 0.45f, 1.0f, 0.5f, 0.35f, 0.4f, 0.3f, 0.2f, 0.12f }, 2.0f, 1.4f, 0.25f, 1.0f },
    { "PLATE", 14, { 1.0f, 2.5f, 2.52f, 4.0f, 5.0f, 5.04f, 6.5f, 6.53f, 8.5f, 9.0f, 10.0f, 10.06f, 12.5f, 16.0f },
                   { 1.0f, 0.8f, 0.7f, 0.6f, 0.55f, 0.5f, 0.45f, 0.4f, 0.35f, 0.33f, 0.3f, 0.28f, 0.22f, 0.16f }, 1.0f, 0.8f, 0.35f, 1.0f },
    { "BAR", 6, { 1.0f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f },
                { 1.0f, 0.5f, 0.3f, 0.2f, 0.12f, 0.08f }, 1.0f, 0.6f, 0.05f, 1.0f },
    { "TUBE", 7, { 1.0f, 2.72f, 5.33f, 8.8f, 13.1f, 18.4f, 24.5f },
                 { 0.7f, 1.0f, 0.8f, 0.6f, 0.4f, 0.25f, 0.15f }, 2.72f, 1.6f, 0.08f, 1.0f },
    { "MEMBRANE", 10, { 1.0f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f, 3.6f, 3.652f },
                      { 1.0f, 0.7f, 0.55f, 0.5f, 0.4f, 0.35f, 0.3f, 0.25f, 0.2f, 0.2f }, 1.0f, 0.35f, 0.6f, 1.0f },
    { "GONG", 16, { 1.0f, 1.1f, 1.38f, 1.5f, 1.73f, 1.96f, 2.24f, 2.6f, 2.83f, 3.2f, 3.6f, 4.1f, 4.7f, 5.3f, 6.1f, 7.0f },
                  { 1.0f, 0.8f, 0.75f, 0.7f, 0.65f, 0.6f, 0.55f, 0.5f, 0.45f, 0.42f, 0.38f, 0.34f, 0.3f, 0.26f, 0.22f, 0.18f }, 1.0f, 2.2f, 1.0f, 1.0f } };
constexpr int kNumObjects = 6;
// (SF-000) SAMPLE, after the objects (a model's number never moves): a file, as a hit or as a loop on the rig's bars.
constexpr int kStrikeSample = kNumObjects;
std::vector<std::string> objectNames() { std::vector<std::string> v; for (auto& o : kObjects) v.push_back (o.name); v.push_back ("SAMPLE"); return v; }
}

class StrikeModule : public Module
{
public:
    enum { POWER, DISTANCE, SIZE, MATERIAL, DECAY, DENS, FILT, LEVEL,
           MODEL, POSITION, SPREAD, TENSION, TUNE, KEYTRACK, RES, ESPACE, ESWING, VELSENS, ALLOCON, KEYFOLLOW,
           SSTART, SGAIN = SSTART + 4, SNORM, SSLICE, SCYCLE, SBARS };      // (SF-000) the sample's: START, END, the fades, GAIN, NORMALISE, SLICE, CYCLE, LOOP
    std::string id() const override { return "STRIKE"; }
    bool instrument() const override { return true; }
    double tailSeconds() const override { return 12.0; }
    std::vector<ParamSpec> params() const override
    {
        return withLoop (withSlices (withSampleGain (withRegion ({ pct ("power", "POWER", 0.4f).prim().dir (1),
                 pct ("distance", "DISTANCE", 0.3f).prim(),
                 pct ("size", "SIZE", 0.5f).prim(),
                 pct ("material", "MATERIAL", 0.5f).prim(),
                 pct ("decay", "DECAY", 0.5f).prim().dir (1),
                 pct ("density", "DENSITY", 0.0f).prim().dir (1),
                 bip ("filter", "FILTER").prim(),
                 levelParam (-6.0f).prim(),
                 choice ("model", "MODEL", objectNames(), 0),      // (SF-000) MODEL, as on every other lane, now that SAMPLE is one of them (the suite: OBJECT)
                 pct ("position", "POSITION", 0.3f).slow(),
                 pct ("spread", "SPREAD", 0.5f).slow(),
                 pct ("tension", "TENSION", 0.3f).slow(),
                 knob ("tune", "TUNE", -24.0f, 24.0f, 0.0f, "st").slow(),
                 toggle ("key_track", "KEY TRACK"),
                 pct ("res", "FILTER RES", 0.1f),
                 choice ("echo_space", "ECHO SPACE", echoSpaceNames(), 0),
                 pct ("echo_swing", "ECHO SWING", 0.0f),
                 pct ("vel_sens", "VEL SENS", 0.5f),
                 toggle ("alloc", "ALLOC", true).noPreset().forget(),
                 toggle ("key_follow", "KEY FOLLOW", true).noPreset().forget() }))));
    }
    // (SF-000) SAMPLE's file
    bool acceptsFiles() const override { return true; }
    juce::String fileHint() const override { return "DROP A SAMPLE (MODEL: SAMPLE)"; }
    void loadFiles (const juce::StringArray& f) override { if (f.size() > 0) sample.set (loadSampleFile (juce::File (f[0]), sr, 300.0)); }
    void save (juce::ValueTree& t) const override { sample.save (t, "SAMPLE"); }
    void load (const juce::ValueTree& t) override { sample.load (t, "SAMPLE", sr); }
    void prepare (double rate, int mb) override
    {
        if (auto s = sample.get(); s != nullptr && std::abs (s->sr - rate) > 1 && s->flac.getSize() > 0) sample.set (loadSampleFlac (s->flac, s->name, rate, s->flacGain));
        sr = rate;
        out.prepare (rate, mb, ROLE_PERC, "STRIKE", false);
        os.prepare (rate, 2);
        events.reserve (2048);
        reset();
    }
    void reset() override
    {
        echoes.reset();
        for (auto& v : voices) v.active = false;
        for (auto& h : shots) h.active = false;     // (SF-000)
        lp.reset();
        out.reset();
        os.reset();
        fractureSm = 0.0f;
    }

    struct Mode { float re = 0, im = 0, a = 0, b = 0, r = 0, theta = 0, g = 0, panL = 0.7f, panR = 0.7f; };
    struct Voice
    {
        bool active = false;
        int object = 0, numModes = 0;
        Mode m[16];
        double t = 0.0;
        float vel = 1.0f, blastT = 0.001f, blastAmp = 0.0f, blastLp = 0.0f, lpA = 0.0f, debris = 0.0f, debrisZ = 0.0f;
        float tension = 0.0f, tensionTau = 0.05f, fracture = 0.0f, level = 0.0f, peak = 1.0e-9f, fade = 1.0f, fadeStep = 0.0f, mix = 1.0f;
        int ticks = 0;
        float lvl() const { return active ? level : 0.0f; }
    };

    void process (Ctx& c, juce::AudioBuffer<float>& io, juce::AudioBuffer<float>*, juce::AudioBuffer<float>*, juce::MidiBuffer& midi) override
    {
        echoes.run (c, midi, events, c.v (DENS), echoSpaceBeats (c.ci (ESPACE)), c.v (ESWING), 1.0f, false);
        auto* L = io.getWritePointer (0); auto* R = io.getWritePointer (1);
        // (SF-000) SAMPLE: the file as a hit (every step starts it; the one before is cut, as a hat's is; DECAY its gate),
        // or, with LOOP on, as a loop on the rig's bars (SampleLoop, Kit.h). In stereo. The sound knobs then: SPREAD is
        // its WIDTH (the middle: as recorded; down: to one channel; up: wider), MATERIAL its TONE (a tilt about 2 kHz),
        // POWER its DRIVE (2x oversampled, with FRACTURE's), VEL SENS how much a step's velocity counts.
        const SampleData* smp = sample.audio();
        const bool sampleModel = c.ci (MODEL) == kStrikeSample;
        const float width = c.v (SPREAD) * 2.0f, sCal = cal::at (cal::perc, 12);
        const bool loop = lp.setup (c, c.ci (SBARS), sampleModel, smp, smp != nullptr ? regionFrom (c, SSTART, *smp, SGAIN) : SampleRegion(),
                                    sr, sCal * 1.41f, c.v (MATERIAL), c.v (DECAY), c.v (TUNE), width);
        const float sTilt = (c.v (MATERIAL) - 0.5f) * 2.0f, sTiltA = 1.0f - std::exp (-kTwoPi * 2000.0f / (float) sr),
                    sTiltLo = std::pow (10.0f, -sTilt * 0.6f), sTiltHi = std::pow (10.0f, sTilt * 0.6f);
        size_t e = 0;
        for (int i = 0; i < c.n; ++i)
        {
            while (e < events.size() && events[e].offset <= i)
            {
                const auto& ev = events[e++];
                if (! ev.on) continue;
                if (! sampleModel) { strike (c, ev); continue; }
                const float velK = (ev.gen == 0 ? lerp (1.0f, ev.vel, c.v (VELSENS)) : ev.vel) * std::pow (0.7f, (float) ev.gen) * ev.gain;
                if (loop) lp.hit (c, c.v (DECAY), velK, ev.gen);
                else if (smp != nullptr) shoot (c, ev, *smp, velK);
            }
            float l = 0.0f, r = 0.0f;
            for (auto& v : voices) if (v.active) render (v, l, r);
            if (lp.live()) lp.tick (smp, 0.0f, 1.0f, 1.0f, l, r);
            if (smp != nullptr)
                for (auto& h : shots)
                {
                    if (! h.active) continue;
                    if ((int) h.pos + 2 >= (h.reg.end > 0.0 ? std::min (smp->length(), h.reg.last()) : smp->length())) { h.active = false; continue; }
                    float g = sampleGateGain ((float) h.t, h.gate);
                    if (h.fadeStep > 0.0f) { h.fade -= h.fadeStep; g *= std::max (0.0f, h.fade); }
                    if (g <= 0.0f) { h.active = false; continue; }
                    float a = smp->read (0, h.pos, h.step), b = smp->wide ? smp->read (1, h.pos, h.step) : a;
                    if (smp->wide && width != 1.0f) { const float m = 0.5f * (a + b), sd = 0.5f * (a - b) * width; a = m + sd; b = m - sd; }
                    g *= h.reg.gain (h.pos) * h.vel * sCal;
                    a *= g; b *= g;
                    if (std::abs (sTilt) > 0.002f)
                    {
                        h.tl += sTiltA * (a - h.tl); a = h.tl * sTiltLo + (a - h.tl) * sTiltHi;
                        h.tr += sTiltA * (b - h.tr); b = h.tr * sTiltLo + (b - h.tr) * sTiltHi;
                    }
                    l += a; r += b;
                    h.pos += h.step; h.t += 1.0 / sr;
                }
            else for (auto& h : shots) h.active = false;
            L[i] = l; R[i] = r;
        }
        // FRACTURE: the object distorts (2x oversampled). The amount follows the fractured voices'
        // own envelopes (full at the blast, gone by the time they die), smoothed (2 ms up, 50 ms
        // down) and ramped across the chunk, and it is blended in from exactly clean at 0: no level
        // steps for anything else still ringing. Always through the oversampler, so the delay never
        // changes.
        float target = 0.0f;
        for (auto& v : voices)
            if (v.active && v.fracture > 0.0f)
                target = std::max (target, v.fracture * std::min (1.0f, v.level / v.peak) * (v.fadeStep > 0.0f ? v.fade : 1.0f) * v.mix);
        const float f0 = fractureSm;
        const float coef = std::exp (-(float) c.n / ((target > fractureSm ? 0.002f : 0.05f) * (float) sr));
        fractureSm = target + coef * (fractureSm - target);
        if (fractureSm < 1.0e-5f) fractureSm = 0.0f;
        const float f1 = fractureSm, span = 1.0f / (float) (2 * c.n);
        // (SF-000) SAMPLE's DRIVE (POWER's knob), across the chunk from where it was: each side through it
        const float d1 = sampleModel ? c.v (POWER) : 0.0f, d0 = driveWas;
        driveWas = d1;
        for (int ch = 0; ch < 2; ++ch)
        {
            int k = 0;
            os.process (ch, io.getWritePointer (ch), c.n, [&] (float x)
            {
                const float f = f0 + (f1 - f0) * (float) (k) * span;
                if (const float d = d0 + (d1 - d0) * (float) (k) * span; d > 0.001f) x = fastTanh (x * (1.0f + d * 8.0f)) / (1.0f + d * 2.0f);
                ++k;
                if (f <= 0.0f) return x;
                const float drv = 1.0f + 4.0f * f;
                return x + f * (fastTanh (x * drv) / std::sqrt (drv) - x);
            });
        }
        out.process (c, io, FILT, RES, 0.0f, 0.0f, c.b (ALLOCON), LEVEL);
        dispPower = c.v (POWER); dispDistance = c.v (DISTANCE); dispObject = c.ci (MODEL);
        int act = 0; for (auto& v : voices) act += v.active && v.fadeStep == 0.0f ? 1 : 0;
        activeVoices = act;
    }

    // (SF-000) under its own display, with a sample loaded: the sample (its START and END to drag)
    int displayHeight() const override { return kOwn + (sample.get() != nullptr ? SampleStrip::kHeight : 0); }
    void displayMouse (juce::Point<float> p, juce::Rectangle<float> r, bool down) override
    {
        if (const auto st = SampleStrip::frameIn (r, kOwn); ! st.isEmpty() && sample.get() != nullptr) strip.mouse (p, SampleStrip::wave (st), down, *this, SSTART);
        else if (down) strip.up();
    }
    void displayMouseUp() override { strip.up(); }
    void paint (juce::Graphics& g, juce::Rectangle<float> r) override
    {
        if (auto smp = sample.get())
            if (const auto st = SampleStrip::frameIn (r, kOwn); ! st.isEmpty()) strip.paint (g, st, *smp, *this, SSTART);
        r = r.withHeight ((float) kOwn);
        if (dispObject.load() == kStrikeSample)     // (SF-000) SAMPLE: no blast and no modes
        {
            ui::frame (g, r, "SAMPLE");
            auto smp = sample.get();
            ui::label (g, smp != nullptr ? smp->name.toUpperCase() : juce::String ("NO SAMPLE: LOAD ONE, OR DROP IT HERE"), r.reduced (10, 20), ui::kFaint, 11.0f, juce::Justification::centred);
            return;
        }
        const float power = dispPower.load(), dist = dispDistance.load();
        auto left = r.removeFromLeft (r.getWidth() * 0.45f);
        ui::frame (g, left, "BLAST (FRIEDLANDER)");
        auto in = left.reduced (10, 20);
        // Pressure-time curve for the current POWER / DISTANCE.
        float T, amp, lp; blastShape (power, dist, T, amp, lp);
        juce::Path p;
        const float span = 0.012f;
        for (int k = 0; k <= 120; ++k)
        {
            const float t = span * (float) k / 120.0f;
            const float y = friedlander (t, T) * amp;
            const float x = in.getX() + in.getWidth() * (float) k / 120.0f;
            const float yy = in.getCentreY() - juce::jlimit (-1.0f, 1.0f, y) * in.getHeight() * 0.45f;
            if (k == 0) p.startNewSubPath (x, yy); else p.lineTo (x, yy);
        }
        g.setColour (ui::kLine); g.drawHorizontalLine ((int) in.getCentreY(), in.getX(), in.getRight());
        g.setColour (ui::kText); g.strokePath (p, juce::PathStrokeType (1.4f));
        const bool fract = power > kFracture;
        ui::label (g, fract ? "FRACTURE" : juce::String (T * 1000.0f, 2) + " MS", left.reduced (8, 2).removeFromBottom (14),
                   fract ? ui::kAmber : ui::kFaint, 10.0f, juce::Justification::centredRight);
        r.removeFromLeft (8);
        const auto& o = kObjects[juce::jlimit (0, kNumObjects - 1, dispObject.load())];
        ui::frame (g, r, juce::String ("MODES  ") + o.name);
        auto mi = r.reduced (10, 20);
        const float maxR = o.ratio[o.modes - 1];
        for (int k = 0; k < o.modes; ++k)
        {
            const float x = mi.getX() + mi.getWidth() * std::log2 (o.ratio[k] / o.ratio[0] + 1.0f) / std::log2 (maxR / o.ratio[0] + 1.0f);
            const float h = o.amp[k] * mi.getHeight();
            g.setColour (std::abs (o.ratio[k] - o.pitchRatio) < 1e-3f ? ui::kText : ui::kDim);
            g.fillRect (x - 1.0f, mi.getBottom() - h, 2.0f, h);
        }
        ui::label (g, juce::String (activeVoices.load()) + " / 4", r.reduced (8, 2).removeFromBottom (14), ui::kFaint, 10.0f, juce::Justification::centredRight);
    }

private:
    static constexpr float kFracture = 0.7f;

    static float friedlander (float t, float T)
    {
        if (t < 0.0f) return 0.0f;
        const float x = t / std::max (1e-5f, T);
        return (1.0f - x) * std::exp (-1.6f * x);
    }
    // Charge W (POWER) and stand-off R (DISTANCE): duration ~ W^(1/3) (cube-root scaling),
    // peak ~ W^(1/3) / R (spherical spreading, softened), highs lost with distance.
    static void blastShape (float power, float distance, float& T, float& amp, float& lpHz)
    {
        const float W = expMap (power, 0.001f, 1.0f);           // relative charge
        const float R = expMap (distance, 1.0f, 60.0f);         // relative stand-off
        const float w3 = std::cbrt (W);
        T = 0.0003f + 0.0035f * w3 * (1.0f + 0.15f * std::log (R));
        amp = std::min (1.0f, 1.6f * w3 / std::pow (R, 0.45f));
        lpHz = 18000.0f / (1.0f + R * 0.18f);
    }

    // (SF-000) SAMPLE: a hit of the file
    struct Shot { bool active = false; double pos = 0.0, step = 1.0, t = 0.0; float gate = 1.0e9f, vel = 1.0f, fade = 1.0f, fadeStep = 0.0f, tl = 0.0f, tr = 0.0f; SampleRegion reg; };
    void shoot (Ctx& c, const NoteEvent& ev, const SampleData& s, float vel)
    {
        // a hit cuts the one before it (4 ms); a quiet DENSITY ghost plays over it
        if (ev.gen == 0 || ev.gain >= 0.5f) for (auto& h : shots) if (h.active && h.fadeStep == 0.0f) h.fadeStep = 1.0f / (0.004f * (float) sr);
        Shot* at = nullptr;
        for (auto& h : shots) if (! h.active) { at = &h; break; }
        if (at == nullptr) { at = &shots[0]; for (auto& h : shots) if (h.fade < at->fade) at = &h; }
        *at = Shot();
        at->active = true; at->vel = vel;
        at->step = semis (c.v (TUNE) + (c.b (KEYTRACK) ? (float) (ev.note - 60) : 0.0f));
        at->reg = hitRegion (c, s, regionFrom (c, SSTART, s, SGAIN), SSLICE, sliceHits++, sliceRng.uni());
        at->pos = at->reg.start;
        at->gate = sampleGateSeconds (c.v (DECAY), (float) (at->reg.length() / s.sr) / (float) at->step, ev.gen);
    }

    void strike (Ctx& c, const NoteEvent& ev)
    {
        const int objIdx = juce::jlimit (0, kNumObjects - 1, c.ci (MODEL));
        const auto& o = kObjects[objIdx];
        // At most kSounding voices ring: the quietest of them fades out over 5 ms (never cut).
        // The new voice takes a free slot, else the most faded one.
        int sounding = 0, quietest = -1;
        for (int k = 0; k < kSlots; ++k)
        {
            const auto& w = voices[(size_t) k];
            if (! w.active || w.fadeStep > 0.0f) continue;
            ++sounding;
            if (quietest < 0 || w.level * w.mix < voices[(size_t) quietest].level * voices[(size_t) quietest].mix) quietest = k;
        }
        if (sounding >= kSounding && quietest >= 0) voices[(size_t) quietest].fadeStep = 1.0f / (0.005f * (float) sr);
        int idx = -1;
        for (int k = 0; k < kSlots && idx < 0; ++k) if (! voices[(size_t) k].active) idx = k;
        if (idx < 0)
        {
            // (Only with several strikes inside 5 ms: chords on a busy object.) The quietest fader.
            float lowest = 1.0e9f;
            for (int k = 0; k < kSlots; ++k)
            {
                const auto& w = voices[(size_t) k];
                if (w.fadeStep > 0.0f && w.fade * w.level < lowest) { lowest = w.fade * w.level; idx = k; }
            }
        }
        if (idx < 0) idx = quietest >= 0 ? quietest : 0;
        auto& v = voices[(size_t) idx];
        v = Voice();
        v.active = true; v.object = objIdx;
        const float velK = ev.gen == 0 ? lerp (1.0f, ev.vel, c.v (VELSENS)) : ev.vel;
        v.vel = velK * std::pow (0.7f, (float) ev.gen);
        v.mix = ev.gain;                    // DENSITY fade, after the blast (power follows velocity)
        // Velocity and echo generation scale the charge.
        const float power = clamp01 (c.v (POWER) * (0.55f + 0.45f * velK) * (ev.gen > 0 ? 0.8f : 1.0f));
        float T, amp, lp;
        blastShape (power, c.v (DISTANCE), T, amp, lp);
        v.blastT = T; v.blastAmp = amp * v.vel; v.lpA = std::exp (-kTwoPi * lp / (float) sr);
        v.fracture = power > kFracture ? (power - kFracture) / (1.0f - kFracture) : 0.0f;
        // Pitch: SIZE (bigger = lower), TUNE, KEY TRACK, KEY.
        float st = c.v (TUNE) + (c.b (KEYTRACK) ? (float) (ev.note - 60) : 0.0f);
        const float f0 = expMap (1.0f - c.v (SIZE), 45.0f, 1400.0f);
        if (c.b (KEYFOLLOW) && c.keyRoot >= 0) st += keySnap (f0 * o.pitchRatio, c.keyRoot);
        const float base = f0 * semis (st);
        // Decay: DECAY knob, object, material (wood loses its highs fast, glass keeps them).
        const float t60 = expMap (c.v (DECAY), 0.15f, 9.0f) * o.decayMul;
        const float mat = c.v (MATERIAL);
        const float hfLoss = expMap (1.0f - mat, 0.15f, 6.0f);
        const float pos = c.v (POSITION), spread = c.v (SPREAD);
        v.tension = c.v (TENSION) * o.tension * 0.12f * amp;
        v.tensionTau = 0.03f + 0.25f * T * 40.0f;
        int nm = 0;
        for (int k = 0; k < o.modes; ++k)
        {
            float f = base * o.ratio[k];
            // FRACTURE splits and detunes the modes.
            if (v.fracture > 0.0f) f *= 1.0f + v.fracture * 0.045f * rng.next();
            if (f > 0.45f * (float) sr || f < 20.0f) continue;
            auto& md = v.m[nm++];
            const float fRel = f / base;
            const float decay = t60 / (1.0f + hfLoss * 0.08f * (fRel - 1.0f) * (fRel - 1.0f) * (1.0f + (1.0f - mat)));
            md.r = std::pow (10.0f, -3.0f / (std::max (0.02f, decay) * (float) sr));
            md.theta = kTwoPi * f / (float) sr;
            md.a = md.r * std::cos (md.theta); md.b = md.r * std::sin (md.theta);
            // POSITION: striking near a node mutes some modes (mode-shape weighting).
            const float shape = std::fabs (std::sin (kPi * (float) (k + 1) * (0.08f + 0.84f * pos)));
            // Brightness: material lets the high modes through; the blast duration filters them
            // (a long, distant push excites the low modes, a sharp crack the high ones).
            const float exc = 1.0f / (1.0f + std::pow (f * T * 2.5f, 2.0f));
            md.g = o.amp[k] * (0.25f + 0.75f * shape) * (0.35f + 0.65f * exc) * (0.5f + 0.5f * mat + 0.5f * (1.0f - mat) * exc);
            const float pan = 0.5f + spread * 0.45f * ((k & 1) ? 1.0f : -1.0f) * (0.4f + 0.6f * rng.uni());
            md.panL = std::sqrt (1.0f - pan); md.panR = std::sqrt (pan);
        }
        v.numModes = nm;
        v.debris = v.fracture;
        v.level = v.blastAmp;
        v.peak = std::max (1.0e-9f, v.blastAmp);
    }

    void render (Voice& v, float& outL, float& outR)
    {
        const float t = (float) v.t;
        // Excitation: Friedlander pressure wave, softened by distance (air absorption).
        const float pw = friedlander (t, v.blastT) * v.blastAmp;
        v.blastLp = pw + v.lpA * (v.blastLp - pw);
        float x = v.blastLp;
        // FRACTURE debris: decaying random impulses rattling the object.
        if (v.debris > 0.0f && t < 0.35f)
        {
            const float rate = 0.002f + 0.02f * v.debris * std::exp (-t / 0.08f);
            if (rng.uni() < rate) v.debrisZ += rng.next() * v.debris * v.blastAmp * 0.8f;
            v.debrisZ *= 0.995f;
            x += v.debrisZ;
        }
        // Tension: a big hit sounds sharp, then settles (modes' angles scaled).
        if ((v.ticks++ & 15) == 0)
        {
            const float k = 1.0f + v.tension * std::exp (-t / v.tensionTau);
            for (int i = 0; i < v.numModes; ++i)
            {
                auto& md = v.m[i];
                const float th = std::min (3.0f, md.theta * k);
                md.a = md.r * std::cos (th); md.b = md.r * std::sin (th);
            }
        }
        float l = 0.0f, r = 0.0f, e = 0.0f;
        for (int i = 0; i < v.numModes; ++i)
        {
            auto& md = v.m[i];
            const float re = md.a * md.re - md.b * md.im + x * md.g;
            const float im = md.b * md.re + md.a * md.im;
            md.re = re; md.im = im;
            l += im * md.panL; r += im * md.panR;
            e = std::max (e, std::fabs (im));
        }
        // The crack itself (direct pressure wave) on top.
        const float crack = v.blastLp * 0.35f;
        float gain = kGain * kObjects[v.object].gain * cal::at (cal::strike, v.object) * v.mix;
        if (v.fadeStep > 0.0f) { v.fade -= v.fadeStep; if (v.fade <= 0.0f) { v.active = false; return; } gain *= v.fade; }
        outL += (l * 0.25f + crack) * gain;
        outR += (r * 0.25f + crack) * gain;
        v.level = std::max (e, std::fabs (pw));
        v.peak = std::max (v.peak, v.level);
        v.t += 1.0 / sr;
        if (t > 0.05f && v.level < 1.0e-5f) v.active = false;
    }

    static constexpr float kGain = 1.0f;
    double sr = 48000.0;
    static constexpr int kSlots = 8, kSounding = 4;
    std::array<Voice, kSlots> voices;
    EchoScheduler echoes;
    std::vector<NoteEvent> events;
    RoleOutput out;
    Oversampler os;
    Noise rng;
    SampleSlot sample;                      // (SF-000) SAMPLE
    SampleStrip strip;
    static constexpr int kOwn = 110;
    std::array<Shot, 6> shots;
    SampleLoop lp;
    int sliceHits = 0; Noise sliceRng;
    float driveWas = 0.0f;
    float fractureSm = 0.0f;
    std::atomic<float> dispPower { 0.4f }, dispDistance { 0.3f };
    std::atomic<int> dispObject { 0 }, activeVoices { 0 };
};
ModulePtr makeSTRIKE() { return std::make_unique<StrikeModule>(); }
} // namespace sf
