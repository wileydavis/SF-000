// (SF-000, the hundredth build) SAMPLE: see SampleEngine.h.
#include "SampleEngine.h"

namespace sf
{
namespace smp
{
std::vector<ParamSpec> params()
{
    std::vector<std::string> notes, roots { "FOUND" };
    for (int n = 0; n < 128; ++n) { notes.push_back (noteName (n).toStdString()); roots.push_back (noteName (n).toStdString()); }
    std::vector<ParamSpec> v;
    auto add = [&v] (ParamSpec p) { v.push_back (p.noPreset()); };
    // the eight the main panel has (with the lane's last four: GATE or DECAY, DENSITY, FILTER, LEVEL)
    add (knob ("sp_tune", "TUNE", -24.0f, 24.0f, 0.0f, "st"));
    add (pct ("sp_pos", "POSITION", 0.0f).next());
    add (pct ("sp_size", "SIZE", 1.0f).quickly());      // (a note it cuts is let go over 3 ms: no ease of 30 ms on top)
    add (knob ("sp_scan", "SCAN", 0.0f, 1.0f, 0.5f, "BIP").ends ("SLOW", "FAST"));      // (SF-000: no filter, its sides by name)
    add (pct ("sp_spread", "SPREAD", 0.0f).next());
    add (knob ("sp_attack", "ATTACK", 0.0f, 2000.0f, 0.0f, "ms", 100.0f));
    add (knob ("sp_release", "RELEASE", 1.0f, 4000.0f, 30.0f, "ms", 300.0f));
    add (pct ("sp_drive", "DRIVE", 0.0f));
    // SOURCE: what of the file, and how it is cut
    add (pct ("sp_start", "START", 0.0f).next());
    add (pct ("sp_end", "END", 1.0f).quickly());
    add (knob ("sp_fin", "FADE IN", 0.0f, 2000.0f, 0.0f, "ms", 200.0f).next());
    add (knob ("sp_fout", "FADE OUT", 0.0f, 2000.0f, 0.0f, "ms", 200.0f));
    add (knob ("sp_gain", "GAIN", -24.0f, 24.0f, 0.0f, "dB"));
    add (toggle ("sp_norm", "NORMALISE"));
    add (toggle ("sp_rev", "REVERSE").next());
    add (choice ("sp_root", "ROOT", roots, 0).next());      // (a note's speed is set as it starts)
    add (toggle ("sp_loop", "LOOP"));
    add (pct ("sp_lstart", "LOOP START", 0.0f));
    add (pct ("sp_lend", "LOOP END", 1.0f));
    add (pct ("sp_xfade", "CROSSFADE", 0.1f));
    add (choice ("sp_sliceby", "SLICE BY", { "MARKERS", "ONSETS", "EQUAL", "BEATS" }, 0).next());
    add (knob ("sp_count", "COUNT", 1.0f, 64.0f, 16.0f, "").next().noGlide());
    add (pct ("sp_sens", "SENSITIVITY", 0.5f).next());
    add (choice ("sp_base", "BASE NOTE", notes, 36).next());
    // PLAY: how it is read
    add (choice ("sp_method", "METHOD", { "REPITCH", "STRETCH", "GRAIN" }, 0).next());
    add (choice ("sp_notes", "NOTES", { "PITCH", "SLICES" }, 0).next());
    add (choice ("sp_play", "PLAY", { "HELD", "THROUGH" }, 0).next());
    add (toggle ("sp_choke", "CHOKE").next());
    add (choice ("sp_cycle", "CYCLE", { "OFF", "ROUND", "RANDOM" }, 0).next());
    add (pct ("sp_vels", "VEL START", 0.0f).next());
    add (pct ("sp_vary", "VARIATION", 0.5f).next());
    add (knob ("sp_rate", "RATE", 1.0f, 200.0f, 24.0f, "/s", 24.0f));
    add (pct ("sp_pspread", "PITCH SPREAD", 0.0f));
    add (pct ("sp_shape", "GRAIN SHAPE", 0.5f));
    add (choice ("sp_gsync", "SYNC", { "FREE", "1/4", "1/8", "1/16", "1/32" }, 0));
    add (choice ("sp_bars", "BARS", { "AUTO", "1 BAR", "2 BARS", "4 BARS", "8 BARS", "16 BARS", "32 BARS" }, 0).next());
    add (toggle ("sp_warp", "WARP").next());
    add (choice ("sp_lock", "LOCK", { "OFF", "BAR" }, 0).next());
    // the markers the display sets: slices, and warp (each a place in the file, and the beat it is put on)
    for (int k = 0; k < kMarkers; ++k) { char id[16]; std::snprintf (id, sizeof id, "sp_cut%02d", k + 1); add (pct (id, "MARKER", 0.0f).next().hide().noGlide()); }
    for (int k = 0; k < kWarps; ++k) { char id[16]; std::snprintf (id, sizeof id, "sp_ws%02d", k + 1); add (pct (id, "WARP FROM", 0.0f).next().hide().noGlide()); }
    for (int k = 0; k < kWarps; ++k) { char id[16]; std::snprintf (id, sizeof id, "sp_wd%02d", k + 1); add (pct (id, "WARP TO", 0.0f).next().hide().noGlide()); }
    jassert ((int) v.size() == kParamCount);
    return v;
}

double WarpMap::srcAt (double b) const
{
    if (n < 2) return src[0];
    int k = 0;
    while (k + 2 < n && beat[k + 1] <= b) ++k;
    const double db = beat[k + 1] - beat[k];
    return src[k] + (b - beat[k]) * (db > 1.0e-9 ? (src[k + 1] - src[k]) / db : 0.0);
}
double WarpMap::beatAt (double s) const
{
    if (n < 2) return 0.0;
    int k = 0;
    while (k + 2 < n && src[k + 1] <= s) ++k;
    const double ds = src[k + 1] - src[k];
    return beat[k] + (s - src[k]) * (ds > 1.0e-9 ? (beat[k + 1] - beat[k]) / ds : 0.0);
}
double WarpMap::slopeAt (double b) const
{
    if (n < 2) return 1.0;
    int k = 0;
    while (k + 2 < n && beat[k + 1] <= b) ++k;
    const double db = beat[k + 1] - beat[k];
    return db > 1.0e-9 ? (src[k + 1] - src[k]) / db : 1.0;
}

// The interpolation for a read at a pitch at or under the file's own: 32 points of a windowed sinc (Kaiser, cut at 95 %
// of the file's highest), from a table of 1024 places a sample, the two nearest blended. What is not in the file stays
// under -100 dB of what is; at a whole sample it is the sample.
struct Sinc
{
    static constexpr int kTaps = 32, kPhases = 1024;
    std::vector<float> t;
    Sinc()
    {
        t.assign ((size_t) (kTaps * (kPhases + 1)), 0.0f);
        const double fc = 0.95, beta = 9.0;
        auto i0 = [] (double x) { double s = 1.0, d = 1.0; for (int k = 1; k < 30; ++k) { d *= (x / (2.0 * k)) * (x / (2.0 * k)); s += d; } return s; };
        const double norm = i0 (beta);
        for (int p = 0; p <= kPhases; ++p)
        {
            const double frac = (double) p / kPhases;
            double sum = 0.0;
            for (int k = 0; k < kTaps; ++k)
            {
                const double x = (double) (k - kTaps / 2 + 1) - frac;          // distance from the point read
                const double w = std::abs (x) >= kTaps / 2 ? 0.0 : i0 (beta * std::sqrt (1.0 - (x / (kTaps / 2)) * (x / (kTaps / 2)))) / norm;
                const double s = std::abs (x) < 1.0e-12 ? fc : std::sin (juce::MathConstants<double>::pi * fc * x) / (juce::MathConstants<double>::pi * x);
                t[(size_t) (p * kTaps + k)] = (float) (s * w);
                sum += s * w;
            }
            for (int k = 0; k < kTaps; ++k) t[(size_t) (p * kTaps + k)] /= (float) sum;
        }
        buildWide();
    }
    static const Sinc& get() { static const Sinc s; return s; }
    // (SF-000) Over the file's own pitch, up to twice it: the same kind of kernel, cut at 95 % of what the read leaves
    // (its own top over the speed), 16 of its zero crossings either side: up to 68 points at twice. Higher than twice, the
    // file's octave-down copies (Mips) are read instead, as readAt does, so it is never more.
    static constexpr int kZero = 16, kPerUnit = 2048;
    std::vector<float> k;
    void buildWide()
    {
        k.assign ((size_t) (kZero * kPerUnit + 2), 0.0f);
        const double beta = 10.0;
        auto i0 = [] (double x) { double s = 1.0, d = 1.0; for (int j = 1; j < 30; ++j) { d *= (x / (2.0 * j)) * (x / (2.0 * j)); s += d; } return s; };
        const double norm = i0 (beta);
        for (size_t i = 0; i < k.size(); ++i)
        {
            const double t = (double) i / kPerUnit;
            if (t >= kZero) { k[i] = 0.0f; continue; }
            const double w = i0 (beta * std::sqrt (1.0 - (t / kZero) * (t / kZero))) / norm;
            k[i] = (float) ((t < 1.0e-12 ? 1.0 : std::sin (juce::MathConstants<double>::pi * t) / (juce::MathConstants<double>::pi * t)) * w);
        }
    }
    float atRate (const float* x, int len, double pos, double rate) const
    {
        const double fc = 0.95 / rate, half = kZero / fc;
        const int lo = std::max (0, (int) std::ceil (pos - half)), hi = std::min (len - 1, (int) std::floor (pos + half));
        double acc = 0.0, sum = 0.0;
        const double step = fc * kPerUnit;
        for (int j = lo; j <= hi; ++j)
        {
            const double u = std::abs ((double) j - pos) * step;
            const int ui = (int) u;
            if (ui >= kZero * kPerUnit) continue;
            const float m = (float) (u - ui);
            const float w = k[(size_t) ui] + m * (k[(size_t) ui + 1] - k[(size_t) ui]);
            acc += (double) w * x[j]; sum += w;
        }
        return (float) (acc / std::max (sum, 0.25 / fc));
    }
    float at (const float* x, int len, double pos) const
    {
        const double fl = std::floor (pos);
        const int i = (int) fl;
        const double fr = (pos - fl) * kPhases;
        const int p = std::min (kPhases - 1, (int) fr);
        const float m = (float) (fr - p);
        const float* a = t.data() + (size_t) p * kTaps;
        const float* b = a + kTaps;
        const int first = i - kTaps / 2 + 1;
        float acc = 0.0f;
        if (first >= 0 && first + kTaps <= len)
            for (int k = 0; k < kTaps; ++k) acc += x[first + k] * (a[k] + m * (b[k] - a[k]));
        else
            for (int k = 0; k < kTaps; ++k) if (const int j = first + k; j >= 0 && j < len) acc += x[j] * (a[k] + m * (b[k] - a[k]));
        return acc;
    }
};
} // namespace smp

using namespace smp;

void SampleEngine::bind (const std::vector<ParamSpec>& specs, bool pitchedLane, Ends e)
{
    pitched = pitchedLane; ends = e;
    idList.clear();
    for (size_t i = 0; i < specs.size(); ++i) if (specs[i].id.rfind ("sp_", 0) == 0) idList.push_back ({ specs[i].id, (int) i });
    auto at = [this] (const char* id) { return indexOf (id); };
    ids.tune = at ("sp_tune"); ids.pos = at ("sp_pos"); ids.size = at ("sp_size"); ids.scan = at ("sp_scan"); ids.spread = at ("sp_spread");
    ids.attack = at ("sp_attack"); ids.release = at ("sp_release"); ids.drive = at ("sp_drive");
    ids.start = at ("sp_start"); ids.end = at ("sp_end"); ids.fin = at ("sp_fin"); ids.fout = at ("sp_fout"); ids.gain = at ("sp_gain");
    ids.norm = at ("sp_norm"); ids.rev = at ("sp_rev"); ids.root = at ("sp_root");
    ids.loop = at ("sp_loop"); ids.lstart = at ("sp_lstart"); ids.lend = at ("sp_lend"); ids.xfade = at ("sp_xfade");
    ids.sliceby = at ("sp_sliceby"); ids.count = at ("sp_count"); ids.sens = at ("sp_sens"); ids.base = at ("sp_base");
    ids.method = at ("sp_method"); ids.notes = at ("sp_notes"); ids.play = at ("sp_play"); ids.choke = at ("sp_choke"); ids.cycle = at ("sp_cycle");
    ids.vels = at ("sp_vels"); ids.vary = at ("sp_vary"); ids.rate = at ("sp_rate"); ids.pspread = at ("sp_pspread"); ids.shape = at ("sp_shape");
    ids.gsync = at ("sp_gsync"); ids.bars = at ("sp_bars"); ids.warp = at ("sp_warp"); ids.lock = at ("sp_lock");
    ids.cut0 = at ("sp_cut01"); ids.ws0 = at ("sp_ws01"); ids.wd0 = at ("sp_wd01");
}
int SampleEngine::indexOf (const char* id) const
{
    for (auto& p : idList) if (p.first == id) return p.second;
    return -1;
}

void SampleEngine::prepare (double rate, int maxBlock)
{
    sr = rate;
    for (auto& s : stretchers) { s.s.presetDefault (2, (float) rate); s.busy = false; s.lastSt = 1.0e9f; }
    const int seekMax = stretchers[0].s.outputSeekLength (8.0f) + 64;
    inBuf.setSize (2, std::max (seekMax, maxBlock * 16 + 256));
    outBuf.setSize (2, std::max (64, maxBlock));
    voiceBuf.setSize (4, std::max (64, maxBlock));
    Sinc::get();
    reset();
}
void SampleEngine::reset()
{
    for (auto& v : voices) v = Voice();
    for (auto& g : vg) for (auto& x : g.g) x.on = false;
    for (auto& s : stretchers) { s.s.reset(); s.busy = false; s.lastSt = 1.0e9f; }
    numPending = 0; active = 0; roundCount = 0; driveWas = 0.0f;
    publish();
}

double SampleEngine::barsOf (int choice, double regionSeconds, double bpm, double beatsPerBar)
{
    if (choice >= 1) return (double) (1 << std::min (5, choice - 1));
    return loopBars (1, regionSeconds, bpm, beatsPerBar);
}

smp::WarpMap SampleEngine::warpOf (const SampleData& s, const SampleRegion& reg, double beats, bool on, const float* ws, const float* wd)
{
    WarpMap m;
    m.beats = beats;
    m.src[0] = reg.start; m.beat[0] = 0.0; m.n = 1;
    const double len = (double) std::max (1, s.length());
    if (on)
    {
        struct P { double s, b; };
        P p[kWarps]; int n = 0;
        for (int k = 0; k < kWarps; ++k)
            if (ws[k] > 0.0f)
            {
                const double sp = (double) ws[k] * len, bp = (double) wd[k] * beats;
                if (sp > reg.start + 16.0 && sp < reg.end - 16.0 && bp > 1.0e-4 && bp < beats - 1.0e-4) p[n++] = { sp, bp };
            }
        std::sort (p, p + n, [] (const P& x, const P& y) { return x.s < y.s; });
        for (int k = 0; k < n; ++k)
            if (p[k].b > m.beat[m.n - 1] + 1.0e-4)          // (one that would make the beats go back is left out)
            { m.src[m.n] = p[k].s; m.beat[m.n] = p[k].b; ++m.n; }
    }
    m.src[m.n] = reg.end; m.beat[m.n] = std::max (beats, m.beat[m.n - 1] + 1.0e-3); ++m.n;
    return m;
}

smp::SliceMap SampleEngine::slicesOf (const SampleData& s, const SampleRegion& reg, int by, int count, float sens, const float* cuts, double beats, double beatsPerBar)
{
    SliceMap m;
    m.n = 0;
    auto add = [&m] (double p) { if (m.n < kMaxSlices) m.at[m.n++] = p; };
    add (reg.start);
    const double ms = 0.001 * s.sr;
    if (by == kByMarkers)
    {
        double c[kMarkers]; int n = 0;
        for (int k = 0; k < kMarkers; ++k) if (cuts[k] > 0.0f) { const double p = (double) cuts[k] * s.length(); if (p > reg.start + 4.0 * ms && p < reg.end - 10.0 * ms) c[n++] = p; }
        std::sort (c, c + n);
        for (int k = 0; k < n; ++k) if (c[k] > m.at[m.n - 1] + 2.0 * ms) add (c[k]);
    }
    else if (by == kByOnsets)
    {
        const float thr = 0.6f * (1.0f - clamp01 (sens));
        for (auto& h : s.hits)
            if (h.strength >= thr && h.at > reg.start + 40.0 * ms && h.at < reg.end - 10.0 * ms && (double) h.at > m.at[m.n - 1] + 30.0 * ms) add ((double) h.at);
    }
    else
    {
        const int parts = by == kByEqual ? juce::jlimit (1, 64, count) : juce::jlimit (1, kMaxSlices, (int) std::lround (beats / std::max (1.0, beatsPerBar) * juce::jlimit (1, 64, count)));
        for (int k = 1; k < parts; ++k) add (reg.start + reg.length() * (double) k / parts);
    }
    m.at[m.n] = reg.end;
    return m;
}

void SampleEngine::begin (const Ctx& c, const SampleData* smp, float decay, bool mono, int keyShift)
{
    smpNow = smp; decayKnob = decay; monoNow = mono; keyShiftNow = keyShift;
    spb = c.samplesPerBeat();
    seen.bpm.store ((float) c.bpm); seen.beatsPerBar.store ((float) c.beatsPerBar); seen.ppq.store (c.ppq); seen.playing.store (c.playing);
    // the transport stopped: what has no end of its own (GRAIN, a loop going round) lets go with RELEASE
    if (wasPlaying && ! c.playing)
        for (auto& v : voices) if (v.on && (v.method == kGrain || blk.loop)) release (v);
    wasPlaying = c.playing;
    if (! ids.ok()) return;
    auto& b = blk;
    b.method = c.ci (ids.method); b.notes = c.ci (ids.notes); b.play = c.ci (ids.play); b.cycle = c.ci (ids.cycle);
    b.sliceBy = c.ci (ids.sliceby); b.count = std::max (1, c.ci (ids.count)); b.base = c.ci (ids.base); b.gsync = c.ci (ids.gsync);
    b.barsChoice = c.ci (ids.bars); b.lock = c.ci (ids.lock);
    b.rev = c.b (ids.rev); b.loop = c.b (ids.loop); b.choke = c.b (ids.choke); b.warp = c.b (ids.warp); b.norm = c.b (ids.norm);
    b.tune = c.v (ids.tune) + c.bend; b.pos = c.v (ids.pos); b.size = c.v (ids.size); b.scan = c.v (ids.scan); b.spread = c.v (ids.spread);
    b.attack = c.v (ids.attack); b.release = c.v (ids.release); b.drive = c.v (ids.drive); b.sens = c.v (ids.sens);
    b.rate = c.v (ids.rate); b.pspread = c.v (ids.pspread); b.shape = c.v (ids.shape); b.vels = c.v (ids.vels); b.vary = c.v (ids.vary);
    b.xfade = c.v (ids.xfade); b.lstart = c.v (ids.lstart); b.lend = c.v (ids.lend);
    holdsNow = b.loop || b.method == kGrain;
    if (smp == nullptr || smp->length() < 64) return;
    b.reg = SampleRegion::of (*smp, c.v (ids.start), c.v (ids.end), c.v (ids.fin), c.v (ids.fout));
    b.reg.level = dbToGain (c.v (ids.gain)) * (b.norm && smp->chanPeak > 1.0e-4f ? 1.0f / smp->chanPeak : 1.0f);
    const int root = c.ci (ids.root);
    b.rootNote = root <= 0 ? (smp->rootHz > 0.0f ? smp->rootNote : 60.0f) : (float) (root - 1);
    const double bars = barsOf (b.barsChoice, b.reg.length() / smp->sr, c.bpm, c.beatsPerBar);
    b.beats = bars * c.beatsPerBar;
    b.srcPerBeat = b.reg.length() / std::max (1.0e-6, b.beats);
    float ws[kWarps], wd[kWarps], cuts[kMarkers];
    for (int k = 0; k < kWarps; ++k) { ws[k] = c.v (ids.ws0 + k); wd[k] = c.v (ids.wd0 + k); }
    for (int k = 0; k < kMarkers; ++k) cuts[k] = c.v (ids.cut0 + k);
    b.map = warpOf (*smp, b.reg, b.beats, b.warp, ws, wd);
    b.slices = slicesOf (*smp, b.reg, b.sliceBy, b.count, b.sens, cuts, b.beats, c.beatsPerBar);
    const double len = b.reg.length();
    b.loopA = b.reg.start + len * juce::jlimit (0.0, 0.99, (double) b.lstart);
    b.loopB = std::max (b.loopA + 0.01 * smp->sr, b.reg.start + len * juce::jlimit (0.0, 1.0, (double) b.lend));
    b.loopB = std::min (b.loopB, b.reg.end);
    b.xfadeLen = (b.loopB - b.loopA) * 0.5 * (double) clamp01 (b.xfade);
}

void SampleEngine::noteOn (int offset, int key, float pitch, float vel, float gain, int gen, int part, int hold)
{
    if (numPending >= (int) pending.size()) return;
    pending[(size_t) numPending++] = { offset, key, pitch, vel, gain, gen, part, hold, true };
}
void SampleEngine::noteOff (int offset, int key, int gen)
{
    if (numPending >= (int) pending.size()) return;
    pending[(size_t) numPending++] = { offset, key, 0.0f, 0.0f, 0.0f, gen, 0, -1, false };
}

void SampleEngine::gateNow (int hold, const int* down, int numDown)
{
    for (auto& v : voices)
    {
        if (! v.on || v.releasing || v.choked || ! v.gated) continue;
        if (hold >= 0)
        {
            const int left = hold - v.lived;
            if (left <= 0) release (v); else { v.holdLeft = left; v.held = false; }
        }
        else
        {
            bool isDown = false;
            for (int k = 0; k < numDown; ++k) isDown = isDown || down[k] == v.key;
            if (isDown) { v.holdLeft = -1; v.held = true; }
        }
    }
}

bool SampleEngine::keySounding (int key, int part) const
{
    for (auto& v : voices) if (v.on && ! v.releasing && ! v.choked && v.key == key && v.part == part) return true;
    return false;
}

void SampleEngine::release (Voice& v)
{
    if (v.releasing) return;
    v.releasing = true;
}

// LOCK: where the clock is in the file's bars. A note within a fiftieth of a beat of a sixteenth (10 ms at 120) is on it:
// a step is played a hair early or late by the groove and the lanes' own delays, and a step on a bar line is that bar's
// start, not the very end of the one before.
static double lockPhase (double ppq, double beats)
{
    double ph = std::fmod (ppq, beats);
    if (ph < 0.0) ph += beats;
    const double six = std::round (ph * 4.0) / 4.0;
    if (std::abs (ph - six) < 0.02) ph = six;
    return ph >= beats ? 0.0 : ph;
}

void SampleEngine::start (const Pending& p)
{
    const SampleData* s = smpNow;
    if (s == nullptr || s->length() < 64 || ! ids.ok()) return;
    const auto& b = blk;
    // MONO and CHOKE: what sounds of this part is let go at once (3 ms)
    if (monoNow || b.choke)
        for (auto& v : voices) if (v.on && v.part == p.part && ! v.choked) v.choked = true;
    // a voice: a free one, else the oldest (choked)
    int vi = -1;
    for (int k = 0; k < kVoices && vi < 0; ++k) if (! voices[(size_t) k].on) vi = k;
    if (vi < 0)
    {
        uint32_t oldest = 0xffffffffu;
        for (int k = 0; k < kVoices; ++k) if (voices[(size_t) k].order < oldest) { oldest = voices[(size_t) k].order; vi = k; }
        auto& o = voices[(size_t) vi];
        if (o.stretcher >= 0) stretchers[(size_t) o.stretcher].busy = false;
        o = Voice();
    }
    Voice v;
    v.on = true; v.key = p.key; v.gen = p.gen; v.part = p.part; v.method = b.method; v.order = orderNext++;
    v.vel = lerp (1.0f, clamp01 (p.vel), 0.7f); v.gain = p.gain;
    // VARIATION: at nought every note the same each time (its chances drawn from one seed); in the middle as chance has
    // it; over the middle, a little more of everything (pitch, level, where it starts)
    if (b.vary < 0.02f) v.rng.s = 0x2545F491u + (uint32_t) std::max (0, p.key) * 2654435761u;
    else { rng.next(); v.rng.s = rng.s ^ 0x9E3779B9u; if (v.rng.s == 0) v.rng.s = 1; }
    const float extra = std::max (0.0f, (b.vary - 0.5f) * 2.0f);
    v.varySt = extra * 0.3f * v.rng.next();
    v.varyLevel = dbToGain (extra * 2.0f * v.rng.next());
    v.dir = b.rev ? -1 : 1;
    // which part of the file: a slice, or the whole region
    const auto& sl = b.slices;
    const double ms = 0.001 * s->sr;
    if (b.notes == 1)
    {
        int idx = pitched ? p.key - b.base : 0;
        idx += (int) std::floor (b.pos * (float) sl.n * 0.9999f);
        if (b.cycle == 1) idx += roundCount++;
        else if (b.cycle == 2) idx = (int) (v.rng.uni() * (float) sl.n);
        if (b.spread > 0.0f && v.rng.uni() < b.spread) idx = (int) (v.rng.uni() * (float) sl.n);
        idx = ((idx % sl.n) + sl.n) % sl.n;
        v.slice = idx;
        v.a = sl.at[idx]; v.b = sl.at[idx + 1];
        // each slice its own fades: FADE IN and FADE OUT, and a millisecond in and two out at least where it is cut from its neighbours
        v.fadeIn = std::max ((double) b.reg.fadeIn, v.a > b.reg.start + 0.5 ? ms : 0.0);
        v.fadeOut = std::max ((double) b.reg.fadeOut, v.b < b.reg.end - 0.5 ? 2.0 * ms : 0.0);
        v.pitchSt = 0.0f;
        seen.lastSlice.store (idx); seen.struckAt.store (juce::Time::getMillisecondCounter());
    }
    else
    {
        v.a = b.reg.start; v.b = b.reg.end;
        v.fadeIn = b.reg.fadeIn; v.fadeOut = b.reg.fadeOut;
        v.pitchSt = pitched ? p.pitch + (float) keyShiftNow - b.rootNote : 0.0f;
    }
    if (const double both = v.fadeIn + v.fadeOut, len = v.b - v.a; both > len && both > 0.0) { v.fadeIn *= len / both; v.fadeOut *= len / both; }
    const double len = v.b - v.a;
    // where in it: POSITION (with PITCH; with SLICES it chose the slice), SPREAD, VEL START, VARIATION
    double off = 0.0;
    if (b.notes == 0)
    {
        off = (double) b.pos * len;
        if (b.method != kGrain && b.spread > 0.0f) off += (double) b.spread * 0.5 * len * (double) v.rng.next();
    }
    off += (double) b.vels * (1.0 - (double) clamp01 (p.vel)) * 0.5 * len;
    off += (double) extra * 0.01 * s->sr * (double) v.rng.uni();
    off = juce::jlimit (0.0, std::max (0.0, len - 2.0 * ms), off);
    v.from = v.dir > 0 ? v.a + off : v.b - off;
    if (v.from > v.a + 0.5 && v.from < v.b - 0.5) v.fadeIn = std::max (v.fadeIn, ms);     // (a start inside the part is never a click)
    // how long it is let sound
    const double playSec = std::max (0.01, (len - off) / s->sr);
    v.lived = 0; v.gated = false;
    if (b.play == 1) v.holdLeft = -1, v.held = false;          // THROUGH: to its end, whatever its key does
    else if (p.hold >= 0) v.holdLeft = p.hold, v.gated = ends == Ends::keys;
    else if (ends == Ends::keys) v.gated = true;
    else if (ends == Ends::hits)
    {
        const float g = sampleGateSeconds (decayKnob, (float) playSec, p.gen);
        v.holdLeft = g > 1.0e8f ? -1 : std::max (1, (int) (g * (float) sr));
        v.held = false;
    }
    // GRAIN has no end of its own (the place its grains are read about goes round the part): a note let play to its end
    // lasts as long as its part does at the file's own speed, then lets go with RELEASE
    if (b.method == kGrain && v.holdLeft < 0 && ! v.held) v.holdLeft = std::max (1, (int) (playSec * sr));
    v.env = b.attack <= 0.01f ? 1.0f : 0.0f;
    // the method's own start
    if (b.method == kStretch)
    {
        int si = -1;
        for (int k = 0; k < kStretchers && si < 0; ++k) if (! stretchers[(size_t) k].busy) si = k;
        if (si < 0)
        {
            // (the oldest STRETCH voice gives up its stretcher)
            uint32_t oldest = 0xffffffffu; int ov = -1;
            for (int k = 0; k < kVoices; ++k) { auto& o = voices[(size_t) k]; if (o.on && o.stretcher >= 0 && o.order < oldest) { oldest = o.order; ov = k; } }
            if (ov < 0) return;
            si = voices[(size_t) ov].stretcher;
            voices[(size_t) ov] = Voice();
        }
        auto& st = stretchers[(size_t) si];
        st.busy = true; v.stretcher = si;
        const auto& m = b.map;
        double beat = m.beatAt (v.from);
        if (b.lock == 1 && b.notes == 0)
        {
            // LOCK BAR: where the set's clock is in the file's bars (and POSITION from there)
            const double ph = lockPhase (seen.ppq.load() + (double) p.offset / spb, m.beats);
            beat = std::fmod (ph + (double) b.pos * m.beats, m.beats);
            v.from = m.srcAt (beat);
        }
        v.pos = beat;
        v.tail = v.dir > 0 ? m.beatAt (v.b) : m.beatAt (v.a);
        v.from = beat;
        const float pace = scanPace (b.scan);
        const double rate = std::max (0.01, m.slopeAt (beat) / spb * (double) pace);
        const int L = std::min (st.s.outputSeekLength ((float) rate), inBuf.getNumSamples());
        const double src0 = m.srcAt (beat);
        float* in[2] = { inBuf.getWritePointer (0), inBuf.getWritePointer (1) };
        for (int i = 0; i < L; ++i)
        {
            const double q = src0 + (double) (v.dir * i);
            const int qi = (int) q;
            const bool inside = qi >= (int) v.a && qi < (int) v.b && qi >= 0 && qi < s->length();
            in[0][i] = inside ? s->buf.getSample (0, qi) : 0.0f;
            in[1][i] = inside ? s->buf.getSample (s->wide ? 1 : 0, qi) : 0.0f;
        }
        st.s.outputSeek (in, L);
        st.lastSt = v.pitchSt + b.tune + v.varySt;
        st.s.setTransposeSemitones (st.lastSt);
        v.fed = src0 + (double) (v.dir * L);
    }
    else if (b.method == kGrain)
    {
        v.pos = v.from; v.grainClock = 0.0; v.posAt = b.pos;
        for (auto& g : vg[(size_t) vi].g) g.on = false;
    }
    else
    {
        v.pos = v.from;
        if (b.lock == 1 && b.notes == 0)
        {
            const double ph = lockPhase (seen.ppq.load() + (double) p.offset / spb, b.beats);
            v.pos = v.dir > 0 ? v.a + ph / b.beats * len : v.b - ph / b.beats * len;
            v.from = v.pos;
        }
    }
    voices[(size_t) vi] = v;
}

float SampleEngine::envTick (Voice& v)
{
    ++v.lived;
    if (v.holdLeft > 0 && --v.holdLeft == 0) release (v);
    if (v.releasing)
    {
        const float rs = std::max (0.001f, blk.release * relScale * 0.001f) * (float) sr;
        v.env *= std::exp (-6.9f / rs);
        if (v.env < 1.0e-4f) v.on = false;
    }
    else if (v.env < 1.0f)
    {
        const float as = std::max (1.0f, blk.attack * 0.001f * (float) sr);
        v.env = std::min (1.0f, v.env + 1.0f / as);
    }
    float g = v.env;
    if (v.choked) { v.chokeGain -= 1.0f / (0.003f * (float) sr); if (v.chokeGain <= 0.0f) { v.on = false; return 0.0f; } g *= v.chokeGain; }
    if (v.stopping) { v.stopGain -= 1.0f / (0.003f * (float) sr); if (v.stopGain <= 0.0f) { v.on = false; return 0.0f; } g *= v.stopGain; }
    return g;
}

float SampleEngine::read (int ch, double pos, double rate) const
{
    const SampleData* s = smpNow;
    if (rate > 1.0001)
    {
        // over its own pitch: an octave-down copy where it is over twice, then the wide kernel
        const juce::AudioBuffer<float>* src = &s->buf;
        for (size_t k = 0; rate > 2.0 && k < s->mips.lv.size(); ++k) { src = &s->mips.lv[k]; rate *= 0.5; pos *= 0.5; }
        return Sinc::get().atRate (src->getReadPointer (std::min (ch, src->getNumChannels() - 1)), src->getNumSamples(), pos, rate);
    }
    const float* x = s->buf.getReadPointer (std::min (ch, s->buf.getNumChannels() - 1));
    const double fl = std::floor (pos);
    if (pos == fl) { const int i = (int) fl; return i >= 0 && i < s->length() ? x[i] : 0.0f; }
    return Sinc::get().at (x, s->length(), pos);
}

void SampleEngine::renderRepitch (Voice& v, float* L, float* R, int n)
{
    const auto& b = blk;
    const SampleData* s = smpNow;
    const float pace = scanPace (b.scan);
    double rate = (double) semis (v.pitchSt + b.tune + v.varySt) * (double) pace;
    if (b.lock == 1 && b.notes == 0) rate = b.srcPerBeat / spb * (double) pace;
    const double len = v.b - v.a;
    const double span = (v.dir > 0 ? v.b - v.from : v.from - v.a) * (double) clamp01 (b.size);
    const double endAt = v.dir > 0 ? v.from + span : v.from - span;          // SIZE: where it stops (as SIZE is now)
    const bool partEnd = std::abs (endAt - (v.dir > 0 ? v.b : v.a)) < 0.5;
    const double fo = partEnd ? v.fadeOut : std::max (v.fadeOut, 0.002 * s->sr);
    // (a loop only where something will let it go: a key held, a gate, its release. A note played to its end with LOOP on
    // plays to its end)
    const bool loop = b.loop && b.notes == 0 && v.dir > 0 && b.loopB - b.loopA > 16.0 && (v.held || v.holdLeft > 0 || v.releasing);
    const double loopLen = b.loopB - b.loopA, xf = b.xfadeLen;
    const float level = b.reg.level * v.vel * v.gain * v.varyLevel;
    const bool wide = s->wide;
    juce::ignoreUnused (len);
    for (int i = 0; i < n; ++i)
    {
        if (! v.on) return;
        const float e = envTick (v);
        if (! v.on) return;
        double p = v.pos;
        if (loop && p >= b.loopB) { p -= loopLen; v.pos = p; }
        const double toEnd = v.dir > 0 ? endAt - p : p - endAt;
        if (toEnd <= 0.0 && ! (loop && endAt >= b.loopB) && ! v.stopping)
        {
            // its end, reached: over. Or END or SIZE moved in front of it: let go over 3 ms
            const bool reached = v.lastToEnd >= -1.0 && v.lastToEnd <= std::abs (rate) * 2.0 + 2.0;
            if (reached || p <= 0.0 || p >= (double) s->length() - 1.0) { v.on = false; return; }
            v.stopping = true;
        }
        v.lastToEnd = toEnd;
        float g = e * level;
        const double fromStart = v.dir > 0 ? p - v.from : v.from - p;
        if (v.fadeIn > 0.0 && fromStart < v.fadeIn) g *= SampleRegion::rc (fromStart / v.fadeIn);
        if (! loop && fo > 0.0 && toEnd < fo && ! v.stopping) g *= SampleRegion::rc (toEnd / fo);
        float l = read (0, p, rate), r = wide ? read (1, p, rate) : l;
        if (loop && xf > 0.0 && p > b.loopB - xf)
        {
            // the crossfade: what is before the loop's start comes in as its end goes (equal power)
            const double t = (p - (b.loopB - xf)) / xf;
            const float ga = (float) std::cos (t * juce::MathConstants<double>::halfPi), gb = (float) std::sin (t * juce::MathConstants<double>::halfPi);
            const double q = p - loopLen;
            const float l2 = read (0, q, rate), r2 = wide ? read (1, q, rate) : l2;
            l = l * ga + l2 * gb; r = r * ga + r2 * gb;
        }
        L[i] += l * g; R[i] += r * g;
        v.pos = p + rate * v.dir;
        ++v.age;
    }
}

void SampleEngine::renderStretch (Voice& v, float* L, float* R, int n)
{
    const auto& b = blk;
    const SampleData* s = smpNow;
    if (v.stretcher < 0) { v.on = false; return; }
    auto& st = stretchers[(size_t) v.stretcher];
    const auto& m = b.map;
    const float pace = scanPace (b.scan);
    const double dBeat = (double) pace / spb * v.dir;            // beats an output sample
    const float semisNow = v.pitchSt + b.tune + v.varySt;
    if (std::abs (semisNow - st.lastSt) > 0.001f) { st.lastSt = semisNow; st.s.setTransposeSemitones (semisNow); }
    // SIZE: how much of what was ahead of it plays
    const double endBeat = v.from + (v.tail - v.from) * (double) clamp01 (b.size);
    // (SF-000, the hundred and fourth build) as REPITCH's: a loop only where something will let it go (a key held, a gate,
    // its release). A note played THROUGH with LOOP on went round until the transport stopped
    const bool loop = b.loop && b.notes == 0 && v.dir > 0 && (v.held || v.holdLeft > 0 || v.releasing);
    const double la = m.beatAt (b.loopA), lb = m.beatAt (b.loopB);
    // what to give it: enough that what comes out now is where the beat is (as DECK keeps its decks)
    const double rate = std::max (0.0, m.slopeAt (v.pos) * std::abs (dBeat));
    const int Lat = st.s.outputSeekLength ((float) std::max (0.01, rate));
    const double want = m.srcAt (v.pos) + (double) (v.dir * Lat);
    const double err = (want - v.fed) * v.dir;
    const int need = juce::jlimit (0, inBuf.getNumSamples(), (int) std::lround ((double) n * rate + err * 0.1));
    float* in[2] = { inBuf.getWritePointer (0), inBuf.getWritePointer (1) };
    for (int i = 0; i < need; ++i)
    {
        double q = v.fed;
        if (loop && q >= b.loopB) { q -= (b.loopB - b.loopA); v.fed = q; }
        const int qi = (int) q;
        const bool inside = qi >= (int) b.reg.start && qi < (int) b.reg.end && qi >= 0 && qi < s->length();
        in[0][i] = inside ? s->buf.getSample (0, qi) : 0.0f;
        in[1][i] = inside ? s->buf.getSample (s->wide ? 1 : 0, qi) : 0.0f;
        v.fed = q + (double) v.dir;
    }
    float* out[2] = { outBuf.getWritePointer (0), outBuf.getWritePointer (1) };
    st.s.process (in, need, out, n);
    const float level = b.reg.level * v.vel * v.gain * v.varyLevel;
    for (int i = 0; i < n; ++i)
    {
        if (! v.on) break;
        const float e = envTick (v);
        if (! v.on) break;
        if (loop && v.pos >= lb && lb - la > 1.0e-3) v.pos -= lb - la;
        const double toEnd = (endBeat - v.pos) * v.dir;
        if (toEnd <= 0.0 && ! (loop && endBeat >= lb)) v.stopping = true;
        float g = e * level;
        const double srcHere = m.srcAt (v.pos);
        const double fromStart = std::abs (srcHere - m.srcAt (v.from));
        if (v.fadeIn > 0.0 && fromStart < v.fadeIn) g *= SampleRegion::rc (fromStart / v.fadeIn);
        if (! loop && v.fadeOut > 0.0) { const double te = std::abs (m.srcAt (endBeat) - srcHere); if (te < v.fadeOut) g *= SampleRegion::rc (te / v.fadeOut); }
        L[i] += out[0][i] * g; R[i] += out[1][i] * g;
        v.pos += dBeat;
        ++v.age;
    }
    if (! v.on) st.busy = false;
}

void SampleEngine::renderGrain (Voice& v, int vi, float* L, float* R, int n, int syncAt)
{
    const auto& b = blk;
    const SampleData* s = smpNow;
    auto& gs = vg[(size_t) vi].g;
    const float pace = scanPace (b.scan);
    const double span = (v.b - v.a);
    const double endAt = v.a + span * (double) std::max (0.02f, clamp01 (b.size));
    const int glen = std::max (16, (int) (grainSeconds (b.size) * (float) sr));
    const double interval = (double) sr / std::max (1.0f, b.rate);
    const float amp = 1.0f / std::sqrt (std::max (1.0f, b.rate * (float) glen / (float) sr)) * b.reg.level * v.vel * v.gain * v.varyLevel;
    const float shape = b.shape;
    const float jitter = 0.5f * std::min (1.0f, b.vary * 2.0f);
    const bool wide = s->wide;
    const double ms = 0.001 * s->sr;
    juce::ignoreUnused (endAt);
    for (int i = 0; i < n; ++i)
    {
        if (! v.on) break;
        const float e = envTick (v);
        if (! v.on) break;
        // a grain now?
        bool spawn = false;
        if (b.gsync == 0) { v.grainClock -= 1.0; if (v.grainClock <= 0.0) { spawn = true; v.grainClock += interval * (1.0 + 0.25 * (double) (v.rng.next() * jitter)); } }
        else spawn = i == syncAt;
        if (spawn)
            for (auto& g : gs)
                if (! g.on)
                {
                    g.on = true; g.voice = vi; g.age = 0; g.len = glen;
                    const double scatter = (double) b.spread * 0.5 * span * (double) v.rng.next();
                    g.st = v.pitchSt + b.tune + v.varySt + b.pspread * 12.0f * v.rng.next();
                    g.step = (double) semis (g.st) * v.dir;
                    const double room = std::abs (g.step) * glen;
                    // (SF-000, the hundred and fourth build) POSITION is heard at once in GRAIN, as its help says: a turn moves the
                    // place the grains are read about by as much of the part as it turns, from wherever SCAN has taken it (round the part)
                    double centre = v.pos;
                    if (b.notes == 0 && span > 1.0)
                    {
                        centre += (double) (b.pos - v.posAt) * span * (double) v.dir;
                        centre = v.a + std::fmod (std::fmod (centre - v.a, span) + span, span);
                    }
                    double p0 = centre + scatter;
                    p0 = juce::jlimit (v.a + ms, std::max (v.a + ms, v.b - room - ms), p0);
                    if (v.dir < 0) p0 = juce::jlimit (v.a + room + ms, std::max (v.a + room + ms, v.b - ms), p0);
                    g.pos = p0;
                    g.pan = 0.5f + 0.5f * v.rng.next() * jitter;
                    g.amp = amp;
                    break;
                }
        float l = 0.0f, r = 0.0f;
        for (auto& g : gs)
        {
            if (! g.on) continue;
            const float t = (float) g.age / (float) g.len;
            float w;
            if (shape <= 0.5f)
            {
                const float f = std::max (0.002f * (float) sr / (float) g.len, shape);       // (a taper of 2 ms at least)
                w = t < f ? (float) SampleRegion::rc (t / f) : t > 1.0f - f ? (float) SampleRegion::rc ((1.0f - t) / f) : 1.0f;
            }
            else
            {
                const float q = (shape - 0.5f) * 2.0f;
                const float hann = 0.5f - 0.5f * std::cos (kTwoPi * t);
                const float perc = t < 0.03f ? t / 0.03f : std::exp (-6.0f * (t - 0.03f));
                w = lerp (hann, perc, q) * std::min (1.0f, (1.0f - t) * 50.0f);
            }
            const double rr = std::abs (g.step);
            const float a = s->read (0, g.pos, rr), bb = wide ? s->read (1, g.pos, rr) : a;       // (a grain's own window hides what a plainer read leaves; it is a tenth of the work)
            const float gl = std::sqrt (1.0f - g.pan) * 1.41421f, gr = std::sqrt (g.pan) * 1.41421f;
            l += a * w * g.amp * gl; r += bb * w * g.amp * gr;
            g.pos += g.step;
            if (++g.age >= g.len) g.on = false;
        }
        L[i] += l * e; R[i] += r * e;
        // the place they are read about travels at SCAN's pace (round the part)
        v.pos += (double) pace * v.dir;
        if (v.pos >= v.b) v.pos = v.a + std::fmod (v.pos - v.a, std::max (1.0, span));
        if (v.pos < v.a) v.pos = v.b - std::fmod (v.a - v.pos, std::max (1.0, span));
        ++v.age;
    }
}

void SampleEngine::render (const Ctx& c, float* L, float* R, int n, float layerGain)
{
    // a slice the screen asked to hear
    if (const int a = seen.audition.exchange (-1); a >= 0 && smpNow != nullptr && numPending < (int) pending.size())
        pending[(size_t) numPending++] = { 0, blk.base + a, (float) (blk.base + a), 0.9f, 1.0f, 0, 0, (int) (0.6 * sr), true };
    // the notes in the order they come in the block
    std::stable_sort (pending.begin(), pending.begin() + numPending, [] (const Pending& x, const Pending& y) { return x.offset < y.offset; });
    const SampleData* s = smpNow;
    int syncAt = -1;
    if (blk.gsync > 0) { static const double div[] = { 1.0, 1.0, 0.5, 0.25, 0.125 }; syncAt = c.nextGrid (div[juce::jlimit (0, 4, blk.gsync)]); }
    float* part[2][2] = { { voiceBuf.getWritePointer (0), voiceBuf.getWritePointer (1) }, { voiceBuf.getWritePointer (2), voiceBuf.getWritePointer (3) } };
    n = std::min (n, voiceBuf.getNumSamples());
    for (int k = 0; k < 4; ++k) voiceBuf.clear (k, 0, n);
    int e = 0, i0 = 0;
    bool any[2] = { false, false };
    while (i0 < n || e < numPending)
    {
        while (e < numPending && pending[(size_t) e].offset <= i0)
        {
            const auto& p = pending[(size_t) e++];
            if (p.on) start (p);
            else
                for (auto& v : voices)
                    if (v.on && v.held && (p.key < 0 || (v.key == p.key && (p.gen == 0 || v.gen == p.gen)))) release (v);
        }
        if (i0 >= n) break;
        const int i1 = e < numPending ? std::min (n, std::max (i0 + 1, pending[(size_t) e].offset)) : n;
        if (s != nullptr)
            for (int k = 0; k < kVoices; ++k)
            {
                auto& v = voices[(size_t) k];
                if (! v.on) continue;
                any[v.part] = true;
                // END and FADE OUT reach a note that is sounding: the whole file's (a slice keeps its own end, inside END)
                if (v.slice < 0) { if (v.dir > 0) v.b = std::max (v.from + 1.0, blk.reg.end); v.fadeOut = blk.reg.fadeOut; }
                else v.b = std::min (v.b, std::max (v.a + 1.0, blk.reg.end));
                float* l = part[v.part][0] + i0; float* r = part[v.part][1] + i0;
                if (v.method == kStretch && v.stretcher >= 0) renderStretch (v, l, r, i1 - i0);
                else if (v.method == kGrain) renderGrain (v, k, l, r, i1 - i0, syncAt >= i0 && syncAt < i1 ? syncAt - i0 : -1);
                else renderRepitch (v, l, r, i1 - i0);
                if (! v.on && v.stretcher >= 0) { stretchers[(size_t) v.stretcher].busy = false; v.stretcher = -1; }
            }
        else for (auto& v : voices) v.on = false;
        i0 = i1;
    }
    numPending = 0;
    // DRIVE, across the block from where it was; then into the lane
    const float d0 = driveWas, d1 = blk.drive;
    driveWas = d1;
    for (int pt = 0; pt < 2; ++pt)
    {
        if (! any[pt]) continue;
        const float pg = pt == 1 ? layerGain : 1.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            float* x = part[pt][ch];
            float* o = ch == 0 ? L : R;
            for (int i = 0; i < n; ++i)
            {
                float y = x[i];
                if (const float d = d0 + (d1 - d0) * (float) i / (float) n; d > 0.0005f) y = fastTanh (y * (1.0f + d * 8.0f)) / (1.0f + d * 2.0f);
                o[i] += y * pg;
            }
        }
    }
    int act = 0;
    for (auto& v : voices) act += v.on ? 1 : 0;
    active = act;

    publish();
}

void SampleEngine::publish()
{
    const SampleData* s = smpNow;
    const double len = s != nullptr ? (double) std::max (1, s->length()) : 1.0;
    int g = 0, nv = 0;
    for (int k = 0; k < kVoices; ++k)
    {
        auto& v = voices[(size_t) k];
        auto& o = seen.v[k];
        if (! v.on || s == nullptr) { o.pos.store (-1.0f); continue; }
        ++nv;
        double at = v.pos, speed = 0.0;
        const float pace = scanPace (blk.scan);
        if (v.method == kStretch) { at = blk.map.srcAt (v.pos); speed = blk.map.slopeAt (v.pos) / spb * pace * v.dir; }
        else if (v.method == kGrain) speed = pace * v.dir;
        else speed = (blk.lock == 1 && blk.notes == 0 ? blk.srcPerBeat / spb : (double) semis (v.pitchSt + blk.tune + v.varySt)) * pace * v.dir;
        o.pos.store ((float) (at / len)); o.speed.store ((float) speed);
        float lv = v.env * (v.choked ? std::max (0.0f, v.chokeGain) : 1.0f) * (v.releasing ? 0.6f : 1.0f);
        o.level.store (lv); o.key.store (v.key); o.slice.store (v.slice); o.part.store (v.part); o.st.store (v.pitchSt + blk.tune + v.varySt);
        if (v.method == kGrain)
            for (auto& gr : vg[(size_t) k].g)
                if (gr.on && g < kGrains)
                {
                    auto& og = seen.g[g++];
                    og.pos.store ((float) (gr.pos / len)); og.len.store ((float) (std::abs (gr.step) * gr.len / len)); og.st.store (gr.st);
                    og.amp.store (std::sin (kPi * (float) gr.age / (float) std::max (1, gr.len)));
                }
    }
    for (; g < kGrains; ++g) seen.g[g].pos.store (-1.0f);
    seen.voices.store (nv);
}
} // namespace sf
