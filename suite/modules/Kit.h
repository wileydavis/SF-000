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
// (SF-000) The sample's own level, in the SAMPLE group with START and END: GAIN, and NORMALISE (the sample's loudest
// point brought to full scale; GAIN is from there). Last in a module's list (appended, never moved): gainAt is GAIN's
// index and NORMALISE follows it. A sample is otherwise as loud as its file, and no louder.
inline std::vector<ParamSpec> withSampleGain (std::vector<ParamSpec> v)
{
    v.push_back (knob ("smp_gain", "GAIN", -24.0f, 24.0f, 0.0f, "dB").noPreset().sampleGroup());
    v.push_back (toggle ("smp_norm", "NORMALISE").noPreset().sampleGroup());
    return v;
}
inline float sampleLevel (const Ctx& c, int gainAt, const SampleData& s)
{
    const float g = std::pow (10.0f, c.v (gainAt) * 0.05f);
    return c.v (gainAt + 1) > 0.5f && s.chanPeak > 1.0e-4f ? g / s.chanPeak : g;
}
inline SampleRegion regionFrom (const Ctx& c, int first, const SampleData& s, int gainAt)
{
    auto r = regionFrom (c, first, s);
    r.level = sampleLevel (c, gainAt, s);
    return r;
}

// (SF-000) Slices: the parts of a sample's START..END that its own onsets cut it into (every one found as the file was
// read: SampleData::onsets), or eight equal parts where there are fewer than two. START begins the first where the first
// onset is not just after it. One recording is then a kit or a set of phrases: each hit or note plays one slice, from
// its onset to the next. Nothing here allocates: it is called as a note starts.
struct SliceSpan { int i0 = 0, i1 = 0; bool lead = true; int count = 8; bool own = false; };
inline SliceSpan sliceSpan (const SampleData& s, const SampleRegion& r)
{
    const auto b = s.onsets.begin(), e = s.onsets.end();
    auto below = [] (int a, double v) { return (double) a < v; };
    SliceSpan o;
    o.i0 = (int) (std::lower_bound (b, e, r.start, below) - b);
    o.i1 = std::max (o.i0, (int) (std::lower_bound (b, e, r.end - 0.01 * s.sr, below) - b));
    o.lead = o.i1 == o.i0 || (double) s.onsets[(size_t) o.i0] > r.start + 0.04 * s.sr;
    const int n = o.i1 - o.i0 + (o.lead ? 1 : 0);
    o.own = n >= 2;
    o.count = o.own ? n : 8;
    return o;
}
inline int sliceCount (const SampleData& s, const SampleRegion& r) { return sliceSpan (s, r).count; }
// The slice of that number (from 0; past the last, round again), as a region of its own: faded a thousandth of a second
// in and two out where it was cut from its neighbours, and at the sample's level.
inline SampleRegion sliceRegion (const SampleData& s, const SampleRegion& r, int index)
{
    const auto o = sliceSpan (s, r);
    index = ((index % o.count) + o.count) % o.count;
    double a = r.start, b = r.end;
    if (o.own)
    {
        const int k = o.i0 + index - (o.lead ? 1 : 0);            // (the onset it starts on; before the first: START)
        if (k >= o.i0) a = (double) s.onsets[(size_t) k];
        if (k + 1 < o.i1) b = (double) s.onsets[(size_t) (k + 1)];
    }
    else { const double len = (r.end - r.start) / 8.0; a = r.start + len * (double) index; b = a + len; }
    const double one = 0.001 * s.sr;
    SampleRegion out = r;
    out.start = a; out.end = std::max (a + 4.0 * one, std::min (b, r.end));
    out.fadeIn = a > r.start + 0.5 ? one : r.fadeIn;
    out.fadeOut = out.end < r.end - 0.5 ? 2.0 * one : r.fadeOut;
    if (const double len = out.end - out.start, both = out.fadeIn + out.fadeOut; both > len && both > 0.0) { out.fadeIn *= len / both; out.fadeOut *= len / both; }
    return out;
}
// A drum's two: SLICE, which slice a hit plays (all the way down: the whole of START..END, as before), and CYCLE, whether
// each hit moves on from it (ROUND: the next one each time. RANDOM: any). Appended last, after GAIN and NORMALISE.
inline std::vector<ParamSpec> withSlices (std::vector<ParamSpec> v)
{
    v.push_back (pct ("smp_slice", "SLICE", 0.0f).noPreset().sampleGroup());
    v.push_back (choice ("smp_cycle", "CYCLE", { "OFF", "ROUND", "RANDOM" }, 0).noPreset().sampleGroup());
    return v;
}
// (SF-000) A drum's LOOP: the sample as a loop on the rig's bars and not a hit (PERC: the hats and the clap). OFF: hits, as
// before. AUTO: as many bars as it is nearest to at the tempo (a power of two: a quarter of a bar to 64). Or a count.
// Appended last, after SLICE and CYCLE.
inline std::vector<ParamSpec> withLoop (std::vector<ParamSpec> v)
{
    v.push_back (choice ("smp_bars", "LOOP", { "OFF", "AUTO", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS", "32 BARS" }, 0).noPreset().sampleGroup());
    return v;
}
// DECAY as a gate on samples: 0 = 10 ms, 90 %+ = the whole sample, exponential in between;
// echo generations shorter. The cut is an 8 ms fade, never a click.
inline float sampleGateSeconds (float decayKnob, float sampleSeconds, int gen)
{
    const float full = std::max (0.01f, sampleSeconds);
    if (decayKnob >= 0.9f && gen == 0) return 1.0e9f;
    const float t = 0.01f * std::pow (full / 0.01f, clamp01 (decayKnob / 0.9f));
    return t * std::pow (0.65f, (float) gen);
}
inline float sampleGateGain (float t, float gateSeconds)
{
    if (t <= gateSeconds) return 1.0f;
    return std::max (0.0f, 1.0f - (t - gateSeconds) / 0.008f);
}
inline double loopBars (int choice, double seconds, double bpm, double beatsPerBar)
{
    if (choice >= 2) return (double) (1 << std::min (5, choice - 2));
    const double bars = seconds * bpm / (60.0 * std::max (1.0, beatsPerBar));
    return std::pow (2.0, (double) std::min (6, std::max (-2, (int) std::lround (std::log2 (std::max (1.0e-3, bars))))));
}
// (SF-000) The loop itself. A sample on a drum lane is a hit: every step starts it again, and DECAY cuts it. With LOOP on
// (SAMPLE the model, a file loaded) it is a loop instead: START..END is taken to be a number of bars (loopBars) and goes
// round on the rig's bars while the clock runs, at the rig's tempo, so its pitch moves with the tempo's ratio to its own,
// as a record's does. Where it is in the file is where the clock is in those bars, whenever it is heard: stopping,
// starting or opening it late never puts it out. TUNE turns it by whole octaves only (twice as fast, half as fast: still
// on the bars). DECAY right up (90 % and over): it plays through, at one level. Under that the lane's steps open it, each
// for as long as DECAY says (10 ms to a bar) and as loud as the hit is: the loop, chopped by the pattern. In stereo, if
// the file is; TONE's tilt and DRIVE on each side. Stopped: silence. It opens over 1 ms and shuts over 8.
struct SampleLoop
{
    bool on = false, want = false, wide = false, through = true;
    double pos = 0.0, step = 0.0, start = 0.0, len = 0.0, rate = 96000.0;
    float gate = 0.0f, up = 0.01f, down = 0.001f, level = 0.7f, levelTo = 0.7f, cal = 1.0f, width = 1.0f;
    int open = 0;                           // ticks the steps' gate still has
    float tilt = 0.0f, tiltA = 0.0f, tiltLo = 1.0f, tiltHi = 1.0f, tl = 0.0f, tr = 0.0f;
    SampleRegion reg;
    void reset() { *this = SampleLoop(); }
    bool live() const { return gate > 0.0f || want; }
    // Each block. sel: LOOP's choice. tickRate: how often tick is called a second. calGain: the model's level (a hit's,
    // in the middle). widthGain: what the left less the right is turned by (1: as recorded). True: the loop is on, and
    // the lane's hits are its to have (hit), not the drum's.
    bool setup (const Ctx& c, int sel, bool sampleModel, const SampleData* smp, const SampleRegion& region, double tickRate, float calGain,
                float tone, float decay, float tuneSemis, float widthGain = 1.0f)
    {
        const bool loop = sel > 0 && sampleModel && smp != nullptr && smp->length() > 256;
        if (smp == nullptr || smp->length() <= 256) { gate = 0.0f; on = want = false; return false; }
        if (! loop && gate <= 0.0f) { on = want = false; return false; }
        reg = region; rate = tickRate;
        start = reg.start; len = std::max (64.0, reg.length());
        wide = smp->wide; width = widthGain;
        up = 1.0f / (0.001f * (float) rate); down = 1.0f / (0.008f * (float) rate);
        cal = calGain * 0.7071f;            // (a hit in the middle: each side has this much of it)
        tilt = (tone - 0.5f) * 2.0f;
        tiltA = 1.0f - std::exp (-kTwoPi * 2000.0f / (float) rate); tiltLo = std::pow (10.0f, -tilt * 0.6f); tiltHi = std::pow (10.0f, tilt * 0.6f);
        through = decay >= 0.9f;
        if (through) levelTo = 0.7f;
        if (loop)
        {
            const double bars = loopBars (sel, len / smp->sr, c.bpm, c.beatsPerBar);
            const double beats = bars * c.beatsPerBar / std::pow (2.0, (double) std::min (2, std::max (-2, (int) std::lround (tuneSemis / 12.0f))));
            double ph = (c.ppq - c.barPhase) / beats; ph -= std::floor (ph);
            const double at = start + ph * len;
            step = len / (beats * c.samplesPerBeat()) * (c.sr / rate);
            // where the clock says: held to it each block (a hair), or, if it is somewhere else (the clock moved, the
            // bars or the part changed, it has only now come on), taken there from silence
            double d = at - pos; d -= len * std::round (d / len);
            if (! on || std::abs (d) > 64.0) { pos = at; gate = 0.0f; tl = tr = 0.0f; if (! on) { open = 0; level = levelTo; } }
            else pos += d;
        }
        on = loop;
        want = loop && c.playing;
        return loop;
    }
    // A hit of the lane's, while the loop is on: level, what the hit has of ACCENT, DENSITY and the echoes.
    void hit (const Ctx& c, float decay, float hitLevel, int gen)
    {
        if (through) return;
        const float bar = (float) (c.beatsPerBar * c.samplesPerBeat() / c.sr);
        open = (int) (sampleGateSeconds (decay, bar, gen) * (float) rate);
        levelTo = hitLevel;
        if (gate <= 0.0f) level = levelTo;      // (from shut: at this hit's level; over an open one: toward it)
    }
    // One tick of it, added to l and r. drv: DRIVE (dg, dn: its gain in and out, as the drum has them).
    void tick (const SampleData* smp, float drv, float dg, float dn, float& l, float& r)
    {
        const bool openNow = want && (through || open > 0);
        gate = openNow ? std::min (1.0f, gate + up) : std::max (0.0f, gate - down);
        if (open > 0) --open;
        level += (levelTo - level) * down * 2.0f;
        if (gate > 0.0f)
        {
            const double at = std::min (pos, (double) (smp->length() - 3));
            float a = smp->read (0, at, step), b = wide ? smp->read (1, at, step) : a;
            if (wide && width != 1.0f) { const float m = 0.5f * (a + b), sd = 0.5f * (a - b) * width; a = m + sd; b = m - sd; }
            const float g = reg.gain (pos) * gate * level;
            a *= g; b *= g;
            if (std::abs (tilt) > 0.002f)
            {
                tl += tiltA * (a - tl); a = tl * tiltLo + (a - tl) * tiltHi;
                tr += tiltA * (b - tr); b = tr * tiltLo + (b - tr) * tiltHi;
            }
            if (drv > 0.001f) { a = fastTanh (a * dg) * dn; b = fastTanh (b * dg) * dn; }
            l += a * cal; r += b * cal;
        }
        pos += step;
        if (pos >= start + len) pos -= len;
    }
};
// The region a hit plays: the whole of it, or the slice SLICE and CYCLE say (hits: how many this drum has played, counted
// by its module; chance: 0 to 1, drawn for this hit).
inline SampleRegion hitRegion (const Ctx& c, const SampleData& s, const SampleRegion& whole, int sliceAt, int hits, float chance)
{
    const float sl = c.v (sliceAt);
    const int cycle = c.ci (sliceAt + 1);
    if (sl <= 0.001f && cycle == 0) return whole;
    const int n = sliceCount (s, whole);
    int index = sl > 0.001f ? std::min (n - 1, (int) (sl * (float) n * 0.9999f)) : 0;
    if (cycle == 1) index += hits; else if (cycle == 2) index += (int) (chance * (float) n);
    return sliceRegion (s, whole, index);
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
    // (SF-000) The mono tick's state as the stereo one's too: before the first stereo tick after mono ones (KICK, when a
    // stereo sample starts to play), so that the right continues where the left is.
    void widen() { xo[1] = xo[0]; xo[3] = xo[2]; }
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
