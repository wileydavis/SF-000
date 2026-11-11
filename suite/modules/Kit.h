#pragma once
// Everything a module file needs.
#include "../core/AllocTap.h"
#include "../core/Bus.h"
#include "../core/Dsp.h"
#include "../core/Instrument.h"
#include "../core/KickGroove.h"
#include "../core/Module.h"
#include "../core/Pocket.h"
#include "../core/Registry.h"
#include "../core/Sample.h"
#include "../core/Ui.h"

namespace sf
{
inline void applyGain (juce::AudioBuffer<float>& b, float g0, float g1)
{
    for (int c = 0; c < b.getNumChannels(); ++c) b.applyGainRamp (c, 0, b.getNumSamples(), g0, g1);
}
inline float levelDb (const Ctx& c, int i, float& g0) { g0 = dbToGain (c.v0 (i)); return dbToGain (c.v (i)); }
inline ParamSpec levelParam (float def = 0.0f) { return knob ("level", "LEVEL", -36.0f, 6.0f, def, "dB", -9.0f); }
// A sampler's region of its sample (SampleRegion): START, END and the fades at them. Appended to every
// sampler's parameters, in the editor's SAMPLE group; with the sample, not presets (a preset carries
// no sample: choosing one leaves the part of yours that plays alone). Songs keep them, as any knob.
inline std::vector<ParamSpec> sampleRegionParams()
{
    return { pct ("smp_start", "START", 0.0f).noPreset().sampleGroup(),
             pct ("smp_end", "END", 1.0f).noPreset().sampleGroup(),
             knob ("smp_fade_in", "FADE IN", 0.0f, 2000.0f, 0.0f, "ms", 200.0f).noPreset().sampleGroup(),
             knob ("smp_fade_out", "FADE OUT", 0.0f, 2000.0f, 0.0f, "ms", 200.0f).noPreset().sampleGroup() };
}
// first: the module's index of START (END, FADE IN, FADE OUT follow it).
inline SampleRegion regionFrom (const Ctx& c, int first, const SampleData& s)
{
    return SampleRegion::of (s, c.v (first), c.v (first + 1), c.v (first + 2), c.v (first + 3));
}
inline std::vector<ParamSpec> withRegion (std::vector<ParamSpec> v)
{
    const auto r = sampleRegionParams();
    v.insert (v.end(), r.begin(), r.end());
    return v;
}

// DRY: off on a return track (wet only), on as an insert. Routing, so songs and presets leave it alone.
inline ParamSpec dryParam() { auto p = toggle ("dry", "DRY", true).noPreset().forget(); p.song = false; return p; }
// DRY on / off: a 5 ms ramp counted in samples, so it is the same however the chunks fall.
struct DryRamp
{
    int pos = -1, len = 240;
    void prepare (double sr) { len = std::max (1, (int) std::lround (0.005 * sr)); pos = -1; }
    float tick (float target)
    {
        const int t = target > 0.5f ? len : 0;
        if (pos < 0) pos = t;
        else if (pos != t) pos += t > pos ? 1 : -1;
        return (float) pos / (float) len;
    }
};

// Atomic float history for displays.
template <int N>
struct History
{
    std::atomic<float> v[N] {};
    int w = 0;
    void push (float x) { v[w].store (x); w = (w + 1) % N; }
};

// A 0..1 position marker on a line, for filter-type displays.
inline void drawPosition (juce::Graphics& g, juce::Rectangle<float> r, float pos, const juce::String& left, const juce::String& right, juce::Colour c)
{
    const float y = r.getCentreY();
    g.setColour (ui::kLine);
    g.drawHorizontalLine ((int) y, r.getX(), r.getRight());
    g.drawVerticalLine ((int) r.getCentreX(), y - 6, y + 6);
    const float x = r.getX() + pos * r.getWidth();
    g.setColour (c);
    g.fillRect (x - 2.0f, y - 8.0f, 4.0f, 16.0f);
    ui::label (g, left, r.withHeight (14), ui::kFaint, 10.0f);
    ui::label (g, right, r.withHeight (14), ui::kFaint, 10.0f, juce::Justification::centredRight);
}
} // namespace sf

namespace sf
{
// Standard instrument output stage: DJ FILTER -> (POCKET) -> ALLOC tap -> LEVEL.
struct RoleOutput
{
    DJFilter filter;
    Pocket pocket;
    AllocTap tap;
    bool usePocket = false;
    void prepare (double sr, int mb, int role, const char* name, bool pocketOn)
    {
        filter.prepare (sr);
        usePocket = pocketOn;
        pocket.prepare (sr, mb);
        tap.setIdentity (role, name);
        tap.prepare (sr);
    }
    int latency() const { return usePocket ? Pocket::kLatency : 0; }
    void reset() { filter.reset(); pocket.reset(); tap.reset(); }
    void process (const Ctx& c, juce::AudioBuffer<float>& b, int filtIdx, int resIdx, float pocketDepth, float pocketShape,
                  bool allocOn, int levelIdx, bool delayAudio = true)
    {
        auto ch = b.getArrayOfWritePointers();
        filter.process (ch, 2, c.n, c.v0 (filtIdx), c.v (filtIdx), resIdx >= 0 ? c.v (resIdx) : 0.15f);
        if (usePocket) pocket.process (c, ch, 2, c.n, pocketDepth, pocketShape, nullptr, pocketDepth > 0.001f, false, delayAudio);
        tap.process (ch, 2, c.n, allocOn, &c);
        float g0; const float g1 = levelDb (c, levelIdx, g0);
        applyGain (b, g0, g1);
    }
};

// LAYER SPLIT [Nov 10] (KICK, BASS, LEAD, PAD): which of the two layers plays the lows and which
// the mids and highs, split at SPLIT HZ by a 4th-order Linkwitz-Riley crossover on each layer.
//   SPLIT centre: off, the plain sum (no crossover: nothing changes);
//         left:   MODEL owns the lows, the LAYER MODEL the mids and highs;
//         right:  the LAYER MODEL owns the lows, MODEL the rest.
// Turning it fades the other layer's lows and the owner's mids and highs out (at full SPLIT each band
// is only its owner's) and brings the bands up so that at full SPLIT each is as loud as the layer
// playing it is on its own: MODEL as it is alone, the layer at LAYER's level against it (at 100 %
// as loud as MODEL). The crossover turns the phase of everything near SPLIT HZ (its two bands add up
// flat, to an all-pass), so leaving or reaching the centre fades between the plain sum and the split
// over 10 ms rather than switching (after a reset, in silence, it starts where SPLIT is). monoLows: the stereo synths' lows under SPLIT HZ go mono with
// SPLIT (their unison, spread and ensemble stay in the mids and highs): the side signal high-passed.
struct LayerSplit
{
    static constexpr float kDead = 0.02f;      // OFF, as the editor shows it (the E16's centre, 64, is 0.504)
    // SPLIT (0..1) as a signed amount: -1 MODEL owns the lows .. 0 off .. +1 the layer does.
    static float amount (float knob)
    {
        const float d = knob - 0.5f, a = std::abs (d);
        return a < kDead ? 0.0f : std::copysign (std::min (1.0f, (a - kDead) / (0.5f - kDead)), d);
    }
    void prepare (double rate) { sr = rate; rStep = 1.0f / (float) (0.01 * rate); hz = -1.0f; reset(); }
    void reset() { for (auto& x : xo) x.reset(); for (auto& f : side) f.reset(); r = 0.0f; sideOn = false; fresh = true; }
    // Per block: SPLIT HZ; MODEL's gain against the layer as the voices have it (1 with no layer);
    // the amount at the block's end (the first block after a reset starts there: nothing sounds).
    void set (float splitHz, float mainGain, float sv1)
    {
        if (splitHz != hz) { hz = splitHz; for (auto& x : xo) x.set (hz, sr); for (auto& f : side) f.set (hz, 0.7071f, sr); }
        a = mainGain;
        if (fresh) { r = sv1 != 0.0f ? 1.0f : 0.0f; fresh = false; }
    }
    // Off for the whole block (both ends in the centre, nothing fading): the plain sum.
    bool off (float sv0, float sv1) const { return sv0 == 0.0f && sv1 == 0.0f && r == 0.0f; }
    // The layers, stereo: MODEL's in (l, rr) on entry, the layer's in (bl, br); the result in (l, rr).
    void tick (float& l, float& rr, float bl, float br, float sv)
    {
        if (! ramp (sv)) { l += bl; rr += br; return; }
        float al, ah, arl, arh, bll, blh, brl, brh;
        xo[0].split (l, al, ah); xo[1].split (rr, arl, arh); xo[2].split (bl, bll, blh); xo[3].split (br, brl, brh);
        const G g = gains (sv);
        const float yl = g.aLo * al + g.aHi * ah + g.bLo * bll + g.bHi * blh;
        const float yr = g.aLo * arl + g.aHi * arh + g.bLo * brl + g.bHi * brh;
        l = l + bl + r * (yl - l - bl);
        rr = rr + br + r * (yr - rr - br);
    }
    // The layers, mono (KICK).
    float tick (float ma, float mb, float sv)
    {
        if (! ramp (sv)) return ma + mb;
        float al, ah, bl, bh;
        xo[0].split (ma, al, ah); xo[2].split (mb, bl, bh);
        const G g = gains (sv);
        return ma + mb + r * (g.aLo * al + g.aHi * ah + g.bLo * bl + g.bHi * bh - ma - mb);
    }
    // After everything the synth makes stereo: the side signal under SPLIT HZ out, as far as SPLIT
    // is turned (either way). At the centre it is exactly what came in.
    void monoLows (float* L, float* R, int n, float knob0, float knob1)
    {
        for (int i = 0; i < n; ++i)
        {
            const float s = std::abs (amount (knob0 + (knob1 - knob0) * (float) i / (float) n));
            if (s == 0.0f) { if (sideOn) { for (auto& f : side) f.reset(); sideOn = false; } continue; }
            sideOn = true;
            const float m = 0.5f * (L[i] + R[i]), d = 0.5f * (L[i] - R[i]);
            const float dHi = side[1].tickHP (side[0].tickHP (d));
            const float d2 = d + s * (dHi - d);
            L[i] = m + d2; R[i] = m - d2;
        }
    }
private:
    struct G { float aLo, aHi, bLo, bHi; };
    // Each band's owner up, the other down: s = 1 leaves only the owner, at n = 1 / a (MODEL as it
    // is alone, the layer at LAYER's level against it).
    G gains (float sv) const
    {
        const float s = std::abs (sv), n = 1.0f / (1.0f - s * (1.0f - a)), keep = n * (1.0f - s);
        return sv > 0.0f ? G { keep, n, n, keep } : G { n, keep, keep, n };
    }
    // The fade between the plain sum and the split; false while it is all plain sum.
    bool ramp (float sv)
    {
        if (sv != 0.0f) r = std::min (1.0f, r + rStep);
        else if (r > 0.0f && (r -= rStep) <= 0.0f) { r = 0.0f; for (auto& x : xo) x.reset(); }
        return r > 0.0f;
    }
    Crossover xo[4];                           // MODEL L, R; the layer L, R (mono: 0 and 2)
    Svf side[2];
    float r = 0.0f;                            // the split's share against the plain sum (0..1)
    bool sideOn = false, fresh = true;
    float hz = -1.0f, a = 1.0f, rStep = 0.001f;
    double sr = 48000.0;
};

// KEY: semitones that move a pitch of `hz` to the nearest note of pitch class keyRoot (C = 0).
inline float keySnap (float hz, int keyRoot)
{
    if (keyRoot < 0 || hz <= 0.0f) return 0.0f;
    const float note = 69.0f + 12.0f * std::log2 (hz / 440.0f);
    float d = std::fmod ((float) keyRoot - note, 12.0f);
    if (d < -6.0f) d += 12.0f;
    if (d >= 6.0f) d -= 12.0f;
    return d;
}

// A sample on a module's display (message thread): its waveform, the part that plays bright and the
// rest dim, the fades as ramps over it, START and END as lines with a tab at the top. A press takes
// the nearer of the two lines and the pointer moves it (near either end of the sample it snaps
// there: the whole again). first: the module's index of START (END, FADE IN, FADE OUT follow).
// The strip sits at the bottom of the display, under the module's own part (own pixels high):
// displayHeight() adds kHeight while a sample is loaded. Until the editor has made that room the
// display is the module's own part alone and the strip is not there (frameIn: empty).
struct SampleStrip
{
    static constexpr int kHeight = 50;              // with a 4 px gap above its frame
    int dragging = -1;                              // 0: START, 1: END (between a press and its release)

    static juce::Rectangle<float> frameIn (juce::Rectangle<float> display, int own)
    {
        if (display.getHeight() < (float) (own + kHeight) - 0.5f) return {};
        return display.withTrimmedTop (display.getHeight() - (float) kHeight + 4.0f);
    }
    static juce::Rectangle<float> wave (juce::Rectangle<float> frame) { return frame.reduced (8.0f, 0.0f).withTrimmedTop (18.0f).withTrimmedBottom (5.0f); }
    static SampleRegion regionOf (const SampleData& s, const Module& m, int first)
    {
        if (! m.getParam) return SampleRegion::whole (s);
        return SampleRegion::of (s, m.getParam (first), m.getParam (first + 1), m.getParam (first + 2), m.getParam (first + 3));
    }
    // "0.12 - 0.85 S": the part that plays, in the sample's own seconds.
    static juce::String times (const SampleData& s, const SampleRegion& reg)
    {
        return juce::String (reg.start / s.sr, 2) + " - " + juce::String (reg.end / s.sr, 2) + " S";
    }
    // The waveform, the region and its fades in w (no frame): also VOICE's phrase.
    static void drawRegion (juce::Graphics& g, juce::Rectangle<float> w, const SampleData& s, const SampleRegion& reg)
    {
        const int n = s.length(), cols = std::max (1, (int) w.getWidth());
        if (n <= 0 || s.overview.empty() || w.getHeight() < 2.0f) return;
        const float cy = w.getCentreY(), half = w.getHeight() * 0.5f;
        const size_t parts = s.overview.size();
        for (int x = 0; x < cols; ++x)
        {
            const size_t a = (size_t) x * parts / (size_t) cols, b = std::max (a + 1, (size_t) (x + 1) * parts / (size_t) cols);
            float pk = 0.0f;
            for (size_t k = a; k < b && k < parts; ++k) pk = std::max (pk, s.overview[k]);
            const double pos = ((double) x + 0.5) / cols * n;
            g.setColour (pos >= reg.start && pos < reg.end ? ui::kDim : ui::kFaint);
            const float h = std::max (0.5f, pk * half);
            g.drawVerticalLine ((int) w.getX() + x, cy - h, cy + h);
        }
        auto xAt = [&] (double pos) { return w.getX() + (float) (pos / n) * w.getWidth(); };
        g.setColour (ui::kText);
        if (reg.fadeIn > 0.0) g.drawLine (xAt (reg.start), w.getBottom(), xAt (reg.start + reg.fadeIn), w.getY(), 1.0f);
        if (reg.fadeOut > 0.0) g.drawLine (xAt (reg.end - reg.fadeOut), w.getY(), xAt (reg.end), w.getBottom(), 1.0f);
        for (const double p : { reg.start, reg.end })
        {
            const float x = juce::jlimit (w.getX(), w.getRight() - 1.0f, xAt (p));
            g.drawLine (x, w.getY() - 3.0f, x, w.getBottom(), 1.5f);
            g.fillRect (x - 2.0f, w.getY() - 4.0f, 4.0f, 4.0f);
        }
    }
    void paint (juce::Graphics& g, juce::Rectangle<float> frame, const SampleData& s, const Module& m, int first) const
    {
        if (frame.isEmpty()) return;
        const auto reg = regionOf (s, m, first);
        ui::frame (g, frame, "SAMPLE " + s.name.toUpperCase());
        ui::label (g, times (s, reg), frame.reduced (8.0f, 2.0f).removeFromTop (14.0f), ui::kFaint, 10.0f, juce::Justification::centredRight);
        drawRegion (g, wave (frame), s, reg);
    }
    // A press (down) or a drag at p, over the waveform w. True if it took the event.
    bool mouse (juce::Point<float> p, juce::Rectangle<float> w, bool down, Module& m, int first)
    {
        if (down) dragging = -1;
        if (! m.getParam || ! m.setParam || w.getWidth() <= 1.0f) return false;
        const float a = m.getParam (first), b = m.getParam (first + 1);
        if (down)
        {
            if (! w.expanded (4.0f, 6.0f).contains (p)) return false;
            const float xa = w.getX() + a * w.getWidth(), xb = w.getX() + b * w.getWidth();
            dragging = std::abs (p.x - xa) <= std::abs (p.x - xb) ? 0 : 1;
        }
        if (dragging < 0) return false;
        float f = juce::jlimit (0.0f, 1.0f, (p.x - w.getX()) / w.getWidth());
        if (f < 0.004f) f = 0.0f;
        if (f > 0.996f) f = 1.0f;
        f = dragging == 0 ? std::min (f, b - 0.002f) : std::max (f, a + 0.002f);
        m.setParam (first + dragging, juce::jlimit (0.0f, 1.0f, f));
        return true;
    }
    void up() { dragging = -1; }
};

// Voice-steal helper: index of the quietest / oldest voice.
template <typename V, size_t N>
int pickVoice (std::array<V, N>& v)
{
    for (size_t i = 0; i < N; ++i) if (! v[i].active) return (int) i;
    int best = 0; float lvl = 1e9f;
    for (size_t i = 0; i < N; ++i) if (v[i].level() < lvl) { lvl = v[i].level(); best = (int) i; }
    return best;
}
} // namespace sf
