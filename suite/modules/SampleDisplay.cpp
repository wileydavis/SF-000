// (SF-000, the hundredth build) SAMPLE's display: the file, what reads it now, and what reaches it next.
//
//   header    18  METHOD and NOTES, the voices sounding
//   overview  11  the whole file: the region bracketed, the part shown below lit, every playhead a tick
//   tabs      17  each slice: its number, and the note that plays it; lit as it is struck
//   wave      64  the part shown: coloured by what is in it (warm the lows, green the middle, blue the highs), its
//                 loudness solid inside its peak; START, END and the fades; the loop; the slices; a playhead for every
//                 voice that sounds, its trail as long as it is fast, and what it plays; GRAIN's grains; STRETCH's bars
//                 and beats and its warp markers
//   steps     20  the lane's sixteen steps, each with what it plays; the one playing lit, the next outlined
//   footer    17  the file
// Everything on it follows a knob; the mouse is a second way in. Message thread; it reads what the audio publishes
// (Seen) and the module's parameters, and keeps nothing of its own but where it is zoomed.
#include "SampleEngine.h"

namespace sf
{
using namespace smp;
namespace
{
constexpr float kHead = 0, kOv = 20, kOvH = 11, kTab = 33, kTabH = 17, kWave = 52, kWaveH = 64, kSteps = 118, kStepsH = 20, kFoot = 139;      // (156 high: 196 left CONFIG's grids no room above the EQ)
constexpr float kX = 8, kW = 528;
const juce::Colour kYellow { 0xffe2c33c };
const juce::Colour kRedMark { 0xffff3b30 };
juce::Colour bandColour (int lo, int mid, int hi)
{
    const float t = (float) std::max (1, lo + mid + hi);
    const float r = (lo * 255.0f + mid * 128.0f + hi * 86.0f) / t, g = (lo * 112.0f + mid * 226.0f + hi * 168.0f) / t, b = (lo * 64.0f + mid * 98.0f + hi * 255.0f) / t;
    return juce::Colour ((juce::uint8) r, (juce::uint8) g, (juce::uint8) b);
}
juce::Font font (float h, bool bold = true) { return ui::mono (h, bold); }

// What the display needs of the parameters, read from the module as they stand.
struct View
{
    int method = 0, notes = 0, sliceBy = 0, count = 16, base = 36, play = 0, lock = 0;
    bool loop = false, warp = false, rev = false;
    float scan = 0.5f, pos = 0.0f, spread = 0.0f;
    SampleRegion reg;
    SliceMap slices;
    WarpMap map;
    double beats = 4.0, bars = 1.0, loopA = 0, loopB = 0, xfade = 0;
    float cuts[kMarkers] {}, ws[kWarps] {}, wd[kWarps] {};
    bool beatsAcross = false;         // the wave across is beats (STRETCH): the warp markers are on it
};
View viewOf (const SampleEngine& e, const Module& m, const SampleData& s, float bpm, float bpb)
{
    View v;
    auto P = [&m] (int i) { return i >= 0 && m.getParam ? m.getParam (i) : 0.0f; };
    const auto& ids = e.ids;
    v.method = (int) std::lround (P (ids.method)); v.notes = (int) std::lround (P (ids.notes)); v.sliceBy = (int) std::lround (P (ids.sliceby));
    v.count = std::max (1, (int) std::lround (P (ids.count))); v.base = (int) std::lround (P (ids.base)); v.play = (int) std::lround (P (ids.play)); v.lock = (int) std::lround (P (ids.lock));
    v.loop = P (ids.loop) > 0.5f; v.warp = P (ids.warp) > 0.5f; v.rev = P (ids.rev) > 0.5f;
    v.scan = P (ids.scan); v.pos = P (ids.pos); v.spread = P (ids.spread);
    v.reg = SampleRegion::of (s, P (ids.start), P (ids.end), P (ids.fin), P (ids.fout));
    v.bars = SampleEngine::barsOf ((int) std::lround (P (ids.bars)), v.reg.length() / s.sr, bpm, bpb);
    v.beats = v.bars * bpb;
    for (int k = 0; k < kMarkers; ++k) v.cuts[k] = P (ids.cut0 + k);
    for (int k = 0; k < kWarps; ++k) { v.ws[k] = P (ids.ws0 + k); v.wd[k] = P (ids.wd0 + k); }
    v.map = SampleEngine::warpOf (s, v.reg, v.beats, v.warp, v.ws, v.wd);
    v.slices = SampleEngine::slicesOf (s, v.reg, v.sliceBy, v.count, P (ids.sens), v.cuts, v.beats, bpb);
    const double len = v.reg.length();
    v.loopA = v.reg.start + len * juce::jlimit (0.0, 0.99, (double) P (ids.lstart));
    v.loopB = std::min (v.reg.end, std::max (v.loopA + 0.01 * s.sr, v.reg.start + len * juce::jlimit (0.0, 1.0, (double) P (ids.lend))));
    v.xfade = (v.loopB - v.loopA) * 0.5 * (double) clamp01 (P (ids.xfade));
    v.beatsAcross = v.method == kStretch;
    return v;
}
// The nearest place the file crosses nought to `at`, within two milliseconds (so a START or a marker moved there never clicks).
double zeroNear (const SampleData& s, double at)
{
    const int n = s.length(), r = (int) (0.002 * s.sr), c = (int) at;
    const float* a = s.buf.getReadPointer (0); const float* b = s.buf.getReadPointer (s.buf.getNumChannels() > 1 ? 1 : 0);
    auto mono = [&] (int i) { return a[i] + b[i]; };
    for (int d = 0; d <= r; ++d)
        for (int sgn : { 1, -1 })
        {
            const int i = c + d * sgn;
            if (i > 0 && i < n && mono (i - 1) * mono (i) <= 0.0f) return (double) i;
        }
    return at;
}
} // namespace

// ------------------------------------------------------------------------------------------------ the mapping across
// The wave's x for a place in the file, and back (frac: of the file). Across beats (STRETCH) the whole region is shown,
// each beat as wide as the next, and a place in the file is where its warp puts it.
struct Across
{
    juce::Rectangle<float> r;
    double za = 0, zb = 1, n = 1;
    const View* v = nullptr;
    float xOfSrc (double src) const
    {
        if (v->beatsAcross) return r.getX() + kX + (float) (v->map.beatAt (src) / std::max (1.0e-9, v->beats)) * kW;
        return r.getX() + kX + (float) ((src / n - za) / std::max (1.0e-9, zb - za)) * kW;
    }
    double srcOfX (float x) const
    {
        const double f = (double) (x - r.getX() - kX) / kW;
        if (v->beatsAcross) return v->map.srcAt (juce::jlimit (0.0, 1.0, f) * v->beats);
        return (za + f * (zb - za)) * n;
    }
    float xOfBeat (double b) const { return r.getX() + kX + (float) (b / std::max (1.0e-9, v->beats)) * kW; }
    double beatOfX (float x) const { return juce::jlimit (0.0, 1.0, (double) (x - r.getX() - kX) / kW) * v->beats; }
};

void SampleEngine::paint (juce::Graphics& g, juce::Rectangle<float> r, const Module& m, const SampleData* s, bool lit)
{
    juce::ignoreUnused (lit);
    g.setColour (juce::Colours::black); g.fillRect (r);
    if (s == nullptr || s->length() < 64 || ! ids.ok())
    {
        ui::label (g, "SAMPLE: NO FILE. DROP ONE ON THIS LANE, OR LOAD.", r.reduced (10.0f), ui::kWords, 13.0f, juce::Justification::centred);
        return;
    }
    const float bpm = seen.bpm.load(), bpb = std::max (1.0f, seen.beatsPerBar.load());
    const View v = viewOf (*this, m, *s, bpm, bpb);
    const double n = (double) s->length();
    // the part shown: the whole file, unless zoomed by hand with the wheel. (It followed START and END until the hundred and
    // second build, so dragging END in zoomed the wave right in. Wiley, Nov 21: "Zoom should be a manual intentional zoom.")
    if (! zoomed) { zoomA = 0.0f; zoomB = 1.0f; }
    Across ac { r, zoomA, zoomB, n, &v };
    const float X = r.getX(), Y = r.getY();
    const juce::Rectangle<float> wave (X + kX, Y + kWave, kW, kWaveH);
    static const char* const methods[] = { "REPITCH", "STRETCH", "GRAIN" };
    static const char* const bys[] = { "MARKERS", "ONSETS", "EQUAL", "BEATS" };
    // ---- header
    {
        juce::String h = juce::String (juce::CharPointer_UTF8 ("SAMPLE \xc2\xb7 ")) + juce::String (      /* (the hundred and second build: read as UTF-8; it drew as two letters) */methods[juce::jlimit (0, 2, v.method)]) + " \xc2\xb7 NOTES: " + (v.notes == 1 ? "SLICES" : "PITCH");
        if (v.notes == 1) h << " \xc2\xb7 BY " << bys[juce::jlimit (0, 3, v.sliceBy)];
        if (v.method == kStretch) h << " \xc2\xb7 " << juce::String (v.bars, v.bars < 1.0 ? 2 : 0) << (v.bars == 1.0 ? " BAR" : " BARS") << (v.warp ? " \xc2\xb7 WARP" : "");
        if (v.loop) h << " \xc2\xb7 LOOP";
        if (v.rev) h << " \xc2\xb7 REVERSE";
        g.setColour (ui::kDim); g.setFont (font (11.0f));
        g.drawText (h, juce::Rectangle<float> (X + 8, Y + 2, 420, 15), juce::Justification::centredLeft, false);
        g.drawText ("VOICES " + juce::String (seen.voices.load()) + " / 16", juce::Rectangle<float> (X + 400, Y + 2, 136, 15), juce::Justification::centredRight, false);
        g.setColour (juce::Colour (0xff202020)); g.fillRect (X, Y + 18.0f, r.getWidth(), 1.0f);
    }
    // ---- overview: the whole file
    {
        const juce::Rectangle<float> ov (X + kX, Y + kOv, kW, kOvH);
        const int blocks = s->view.blocks();
        float top = 1.0e-6f; for (auto p : s->view.peak) top = std::max (top, p);
        const float cy = ov.getCentreY(), half = ov.getHeight() * 0.5f;
        for (int x = 0; x < (int) kW; ++x)
        {
            const int b0 = (int) ((int64_t) blocks * x / (int) kW), b1 = std::max (b0 + 1, (int) ((int64_t) blocks * (x + 1) / (int) kW));
            float pk = 0.0f; for (int b = b0; b < b1 && b < blocks; ++b) pk = std::max (pk, s->view.peak[(size_t) b]);
            const double f = ((double) x + 0.5) / kW * n;
            g.setColour (juce::Colour (0xffd8d8d4).withAlpha (f >= v.reg.start && f < v.reg.end ? 0.85f : 0.3f));
            const float h = std::max (0.5f, pk / top * half);
            g.fillRect (ov.getX() + (float) x, cy - h, 1.0f, 2.0f * h);
        }
        const float z0 = ov.getX() + zoomA * kW, z1 = ov.getX() + zoomB * kW;
        g.setColour (juce::Colours::white.withAlpha (0.07f)); g.fillRect (z0, ov.getY() - 1.0f, std::max (2.0f, z1 - z0), ov.getHeight() + 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.35f)); g.drawRect (z0, ov.getY() - 1.0f, std::max (2.0f, z1 - z0), ov.getHeight() + 2.0f, 0.6f);
        g.setColour (ui::kText);
        for (const double p : { v.reg.start, v.reg.end })
        {
            const float x = ov.getX() + (float) (p / n) * kW;
            g.fillRect (x - 0.5f, ov.getY() - 1.0f, 1.0f, ov.getHeight() + 2.0f);
        }
        for (int k = 0; k < kVoices; ++k)
            if (const float p = seen.v[k].pos.load(); p >= 0.0f) { g.setColour (juce::Colours::white); g.fillRect (ov.getX() + p * kW - 0.5f, ov.getY() - 1.0f, 1.2f, ov.getHeight() + 2.0f); }
    }
    // ---- the wave
    {
        g.setColour (juce::Colour (0xff1c1c1c)); g.fillRect (wave.getX(), wave.getCentreY(), wave.getWidth(), 1.0f);
        // the bars and beats (STRETCH, a lock, or slices by beats)
        if (v.method == kStretch || v.lock == 1 || (v.notes == 1 && v.sliceBy == kByBeats))
        {
            const int beats = (int) std::lround (v.beats);
            if (beats > 0 && beats <= 256)
                for (int k = 0; k <= beats; ++k)
                {
                    const float x = v.beatsAcross ? ac.xOfBeat (k) : ac.xOfSrc (v.reg.start + v.reg.length() * k / beats);
                    if (x < wave.getX() - 1.0f || x > wave.getRight() + 1.0f) continue;
                    const bool bar = k % (int) std::lround (bpb) == 0;
                    g.setColour (juce::Colours::white.withAlpha (bar ? 0.42f : 0.14f));
                    if (bar) g.fillRect (x, wave.getY(), 1.0f, wave.getHeight());
                    else { const float dash[] = { 2.0f, 3.0f }; g.drawDashedLine (juce::Line<float> (x, wave.getY(), x, wave.getBottom()), dash, 2, 1.0f); }
                    if (bar && k < beats && beats / (int) std::lround (bpb) <= 32)
                    {
                        g.setColour (juce::Colours::black); g.fillRoundedRectangle (x + 1, wave.getY() + 1, 30, 11, 2.0f);
                        g.setColour (juce::Colours::white.withAlpha (0.5f)); g.drawRoundedRectangle (x + 1, wave.getY() + 1, 30, 11, 2.0f, 0.6f);
                        g.setColour (ui::kText); g.setFont (font (9.0f)); g.drawText ("BAR " + juce::String (k / (int) std::lround (bpb) + 1), juce::Rectangle<float> (x + 3, wave.getY() + 1, 28, 11), juce::Justification::centredLeft, false);
                    }
                }
        }
        // GRAIN: POSITION, and SPREAD about it
        if (v.method == kGrain)
        {
            const double len = v.reg.length(), at = v.reg.start + (double) v.pos * len, sp = (double) v.spread * 0.5 * len;
            const float x0 = ac.xOfSrc (at - sp), x1 = ac.xOfSrc (at + sp);
            g.setColour (kYellow.withAlpha (0.10f)); g.fillRect (juce::Rectangle<float> (x0, wave.getY(), std::max (1.0f, x1 - x0), wave.getHeight()).getIntersection (wave));
        }
        // the file, coloured: a column of the wave at a time, its peak faint and its loudness solid
        const int blocks = s->view.blocks();
        const int cols = (int) kW;
        std::vector<std::array<float, 2>> pr ((size_t) cols);
        std::vector<juce::Colour> co ((size_t) cols);
        float top = 1.0e-6f;
        const float* ca = s->buf.getReadPointer (0); const float* cb = s->buf.getReadPointer (s->buf.getNumChannels() > 1 ? 1 : 0);
        for (int x = 0; x < cols; ++x)
        {
            const double s0 = ac.srcOfX (wave.getX() + (float) x), s1 = ac.srcOfX (wave.getX() + (float) x + 1.0f);
            const double lo = std::max (0.0, std::min (s0, s1)), hi = std::min (n, std::max (s0, s1));
            float pk = 0.0f, rm = 0.0f; int cl = 0, cm = 0, ch = 0;
            if (hi - lo >= SampleView::kBlock)
            {
                const int b0 = (int) (lo / SampleView::kBlock), b1 = std::min (blocks, std::max (b0 + 1, (int) (hi / SampleView::kBlock)));
                double q = 0.0;
                for (int b = b0; b < b1; ++b) { pk = std::max (pk, s->view.peak[(size_t) b]); q += (double) s->view.rms[(size_t) b] * s->view.rms[(size_t) b]; cl += s->view.lo[(size_t) b]; cm += s->view.mid[(size_t) b]; ch += s->view.hi[(size_t) b]; }
                rm = (float) std::sqrt (q / std::max (1, b1 - b0));
            }
            else if (hi > lo)
            {
                const int i0 = (int) lo, i1 = std::max (i0 + 1, std::min ((int) n, (int) std::ceil (hi)));
                double q = 0.0;
                for (int i = i0; i < i1; ++i) { pk = std::max (pk, std::max (std::abs (ca[i]), std::abs (cb[i]))); const float mm = 0.5f * (ca[i] + cb[i]); q += (double) mm * mm; }
                rm = (float) std::sqrt (q / std::max (1, i1 - i0));
                const int b = juce::jlimit (0, blocks - 1, i0 / SampleView::kBlock);
                cl = s->view.lo[(size_t) b]; cm = s->view.mid[(size_t) b]; ch = s->view.hi[(size_t) b];
            }
            pr[(size_t) x] = { pk, rm };
            co[(size_t) x] = bandColour (cl, cm, ch);
            top = std::max (top, pk);
        }
        const float gain = 0.95f / top, cy = wave.getCentreY(), half = wave.getHeight() * 0.5f - 2.0f;
        for (int x = 0; x < cols; ++x)
        {
            const double mid = ac.srcOfX (wave.getX() + (float) x + 0.5f);
            const bool in = mid >= v.reg.start && mid < v.reg.end;
            const float ph = std::max (0.4f, std::min (1.0f, pr[(size_t) x][0] * gain) * half), rh = std::max (0.3f, std::min (1.0f, pr[(size_t) x][1] * gain * 1.9f) * half);
            g.setColour (co[(size_t) x].withAlpha (in ? 0.38f : 0.10f)); g.fillRect (wave.getX() + (float) x, cy - ph, 1.0f, 2.0f * ph);
            g.setColour (co[(size_t) x].withAlpha (in ? 0.95f : 0.22f)); g.fillRect (wave.getX() + (float) x, cy - rh, 1.0f, 2.0f * rh);
        }
        // a sample at full scale in the file: red under the wave
        g.setColour (kRedMark);
        for (int x = 0; x < cols; ++x)
        {
            const double s0 = ac.srcOfX (wave.getX() + (float) x), s1 = ac.srcOfX (wave.getX() + (float) x + 1.0f);
            const int b0 = juce::jlimit (0, blocks - 1, (int) (std::min (s0, s1) / SampleView::kBlock)), b1 = juce::jlimit (0, blocks - 1, (int) (std::max (s0, s1) / SampleView::kBlock));
            bool c = false; for (int b = b0; b <= b1 && ! c; ++b) c = s->view.clip[(size_t) b] != 0;
            if (c) g.fillRect (wave.getX() + (float) x, wave.getBottom() - 2.0f, 1.5f, 3.0f);
        }
        // the loop: a bracket over it, its crossfade hatched
        if (v.loop)
        {
            const float l0 = ac.xOfSrc (v.loopA), l1 = ac.xOfSrc (v.loopB), xf0 = ac.xOfSrc (v.loopB - v.xfade);
            g.setColour (ui::kText.withAlpha (0.35f));
            for (float x = xf0 - wave.getHeight(); x < l1; x += 4.0f)
            {
                juce::Line<float> ln (x, wave.getBottom(), x + wave.getHeight(), wave.getY());
                const float ax = std::max (xf0, ln.getStartX()), bx = std::min (l1, ln.getEndX());
                if (bx > ax) { const float t0 = (ax - ln.getStartX()) / wave.getHeight(), t1 = (bx - ln.getStartX()) / wave.getHeight(); g.drawLine (ax, wave.getBottom() - t0 * wave.getHeight(), bx, wave.getBottom() - t1 * wave.getHeight(), 0.8f); }
            }
            g.setColour (kYellow);
            juce::Path br; br.startNewSubPath (l0, wave.getY() + 6); br.lineTo (l0, wave.getY() + 1); br.lineTo (l1, wave.getY() + 1); br.lineTo (l1, wave.getY() + 6);
            g.strokePath (br, juce::PathStrokeType (1.4f));
            g.setFont (font (9.0f)); g.drawText ("LOOP", juce::Rectangle<float> (l0 + 4, wave.getY() + 3, 40, 11), juce::Justification::centredLeft, false);
        }
        // START, END and the fades
        g.setColour (ui::kText);
        {
            const float xs = ac.xOfSrc (v.reg.start), xe = ac.xOfSrc (v.reg.end);
            if (v.reg.fadeIn > 0.0) g.drawLine (xs, wave.getBottom(), ac.xOfSrc (v.reg.start + v.reg.fadeIn), wave.getY(), 1.0f);
            if (v.reg.fadeOut > 0.0) g.drawLine (ac.xOfSrc (v.reg.end - v.reg.fadeOut), wave.getY(), xe, wave.getBottom(), 1.0f);
            for (int k = 0; k < 2; ++k)
            {
                const float x = k == 0 ? xs : xe;
                if (x < wave.getX() - 2.0f || x > wave.getRight() + 2.0f) continue;
                g.setColour (ui::kText); g.fillRect (x - 0.7f, wave.getY(), 1.4f, wave.getHeight());
                const juce::Rectangle<float> tab (k == 0 ? x : x - 10.0f, wave.getBottom() - 10.0f, 10.0f, 10.0f);
                g.fillRect (tab);
                g.setColour (juce::Colours::black); g.setFont (font (9.0f)); g.drawText (k == 0 ? "S" : "E", tab, juce::Justification::centred, false);
            }
        }
    }
    // ---- the slices: lines through the tabs and the wave, and the tabs
    {
        const juce::Rectangle<float> tabs (X + kX, Y + kTab, kW, kTabH);
        const bool hand = v.sliceBy == kByMarkers;
        const auto& sl = v.slices;
        const int struck = (int) juce::Time::getMillisecondCounter() - (int) seen.struckAt.load() < 160 ? seen.lastSlice.load() : -1;
        bool playing[kMaxSlices] {};
        for (int k = 0; k < kVoices; ++k) if (seen.v[k].pos.load() >= 0.0f) if (const int x = seen.v[k].slice.load(); x >= 0 && x < kMaxSlices) playing[x] = true;
        for (int i = 0; i < sl.n; ++i)
        {
            const float x = ac.xOfSrc (sl.at[i]), xe = ac.xOfSrc (sl.at[i + 1]);
            if (xe < tabs.getX() || x > tabs.getRight()) continue;
            if (i > 0)
            {
                g.setColour (ui::kText.withAlpha (hand ? 0.8f : 0.45f));
                g.fillRect (x - 0.4f, tabs.getY(), hand ? 1.0f : 0.8f, wave.getBottom() - tabs.getY());
                if (hand) g.fillRect (x - 2.0f, wave.getBottom() - 4.0f, 5.0f, 5.0f);
            }
            if (sl.n < 2 && v.notes == 0) continue;
            const float tx = std::max (tabs.getX(), x + 0.5f), tw = std::min (60.0f, std::max (6.0f, std::min (xe, tabs.getRight()) - tx - 1.0f));
            const bool on = i == struck, sounding = playing[i];
            g.setColour (on ? kYellow : sounding ? kYellow.withAlpha (0.35f) : juce::Colour (0xff1b1b1b)); g.fillRoundedRectangle (tx, tabs.getY(), tw, kTabH, 1.5f);
            g.setColour (on ? kYellow : juce::Colour (0xff3a3a3a)); g.drawRoundedRectangle (tx, tabs.getY(), tw, kTabH, 1.5f, 0.6f);
            if (tw > 9.0f)
            {
                g.setColour (on ? juce::Colours::black : ui::kText); g.setFont (font (9.0f));
                g.drawText (juce::String (i + 1), juce::Rectangle<float> (tx + 2, tabs.getY() + 1, tw - 3, 8), juce::Justification::centredLeft, false);
                if (v.notes == 1 && tw > 15.0f && pitched)
                {
                    g.setColour (on ? juce::Colour (0xff222222) : ui::kDim); g.setFont (font (8.0f, false));
                    int at = i - (int) std::floor (v.pos * (float) sl.n * 0.9999f); at = ((at % sl.n) + sl.n) % sl.n;
                    g.drawText (noteName (v.base + at), juce::Rectangle<float> (tx + 2, tabs.getY() + 9, tw - 3, 8), juce::Justification::centredLeft, false);
                }
            }
        }
    }
    // ---- the warp markers (STRETCH): where each is in the file, drawn on the beat it is put on
    if (v.beatsAcross && v.warp)
        for (int k = 0; k < kWarps; ++k)
        {
            if (v.ws[k] <= 0.0f) continue;
            const float x = ac.xOfBeat ((double) v.wd[k] * v.beats);
            g.setColour (kYellow);
            const float dash[] = { 3.0f, 2.0f };
            g.drawDashedLine (juce::Line<float> (x, wave.getY() + 7, x, wave.getBottom()), dash, 2, 1.0f);
            juce::Path t; t.addTriangle (x - 5.0f, wave.getY(), x + 5.0f, wave.getY(), x, wave.getY() + 7.0f);
            g.fillPath (t);
        }
    // ---- GRAIN's grains: a dash each, as long as it is, as high as its pitch, as bright as it is loud
    if (v.method == kGrain)
    {
        for (int k = 0; k < kGrains; ++k)
        {
            const float p = seen.g[k].pos.load();
            if (p < 0.0f) continue;
            const float x0 = ac.xOfSrc ((double) p * n), x1 = ac.xOfSrc ((double) (p + seen.g[k].len.load()) * n);
            const float y = juce::jlimit (wave.getY() + 2.0f, wave.getBottom() - 4.0f, wave.getCentreY() - seen.g[k].st.load() * 3.2f);
            g.setColour (juce::Colours::white.withAlpha (juce::jlimit (0.15f, 0.95f, seen.g[k].amp.load())));
            g.fillRoundedRectangle (std::min (x0, x1), y - 1.4f, std::max (1.5f, std::abs (x1 - x0)), 2.8f, 1.4f);
        }
        const float x = ac.xOfSrc (v.reg.start + (double) v.pos * v.reg.length());
        g.setColour (kYellow); g.fillRect (x - 0.8f, wave.getY(), 1.6f, wave.getHeight());
        g.fillRoundedRectangle (x - 22.0f, wave.getBottom() - 12.0f, 44.0f, 11.0f, 2.0f);
        g.setColour (juce::Colours::black); g.setFont (font (8.5f)); g.drawText ("POSITION", juce::Rectangle<float> (x - 22.0f, wave.getBottom() - 12.0f, 44.0f, 11.0f), juce::Justification::centred, false);
    }
    // ---- the playheads: every voice that sounds
    {
        int row = 0;
        for (int k = 0; k < kVoices; ++k)
        {
            const float p = seen.v[k].pos.load();
            if (p < 0.0f) continue;
            const float x = ac.xOfSrc ((double) p * n);
            if (x < wave.getX() - 1.0f || x > wave.getRight() + 1.0f) continue;
            const float speed = seen.v[k].speed.load(), lv = juce::jlimit (0.25f, 1.0f, seen.v[k].level.load());
            const float pxPerSample = kW / (float) std::max (1.0, (v.beatsAcross ? v.reg.length() : (zoomB - zoomA) * n));
            const float trail = juce::jlimit (4.0f, 46.0f, std::abs (speed) * pxPerSample * 4800.0f) * (speed < 0.0f ? -1.0f : 1.0f);
            const float x2 = x - trail;
            juce::ColourGradient cg (juce::Colours::white.withAlpha (0.0f), x2, 0.0f, juce::Colours::white.withAlpha (0.16f * lv), x, 0.0f, false);
            g.setGradientFill (cg); g.fillRect (std::min (x, x2), wave.getY(), std::abs (trail), wave.getHeight());
            g.setColour (juce::Colours::white.withAlpha (lv)); g.fillRect (x - 0.7f, wave.getY(), 1.4f, wave.getHeight());
            juce::Path t; t.addTriangle (x - 3.5f, wave.getY(), x + 3.5f, wave.getY(), x, wave.getY() + 5.0f); g.fillPath (t);
            // what it plays
            juce::String what, sub;
            const int key = seen.v[k].key.load(), sl = seen.v[k].slice.load();
            const float st = seen.v[k].st.load();
            if (v.notes == 1 && sl >= 0) what = "SLICE " + juce::String (sl + 1);
            else if (pitched && key >= 0) what = noteName (key);
            if (v.method == kRepitch) sub = (st >= 0.0f ? "+" : "") + juce::String (st, std::abs (st - std::round (st)) < 0.05f ? 0 : 1) + " ST \xc2\xb7 " + juce::String (std::abs (speed), 2) + "\xc3\x97";
            else if (v.method == kStretch) sub = juce::String (std::abs (speed) * (float) spb / (float) std::max (1.0, v.reg.length() / std::max (1.0e-6, v.beats)), 2) + "\xc3\x97 \xc2\xb7 PITCH HELD";
            if (what.isNotEmpty() && row < 4)
            {
                const float ly = wave.getY() + (float) row * 24.0f;
                g.setFont (font (9.0f));
                const float lw = 8.0f + g.getCurrentFont().getStringWidthFloat (what);
                g.setColour (juce::Colours::black.withAlpha (0.85f)); g.fillRoundedRectangle (x + 3, ly + 1, lw, 11, 2.0f);
                g.setColour (juce::Colours::white.withAlpha (0.6f * lv)); g.drawRoundedRectangle (x + 3, ly + 1, lw, 11, 2.0f, 0.6f);
                g.setColour (juce::Colours::white.withAlpha (lv)); g.drawText (what, juce::Rectangle<float> (x + 6, ly + 1, lw, 11), juce::Justification::centredLeft, false);
                if (sub.isNotEmpty())
                {
                    g.setFont (font (8.0f, false));
                    const float sw = 8.0f + g.getCurrentFont().getStringWidthFloat (sub);
                    g.setColour (juce::Colours::black.withAlpha (0.85f)); g.fillRoundedRectangle (x + 3, ly + 13, sw, 10, 2.0f);
                    g.setColour (juce::Colour (0xffb8b8b2).withAlpha (lv)); g.drawText (sub, juce::Rectangle<float> (x + 6, ly + 13, sw, 10), juce::Justification::centredLeft, false);
                }
                ++row;
            }
        }
    }
    // ---- the steps
    {
        Module::StepsView sv;
        if (m.stepsOf) m.stepsOf (sv);
        const float sw = kW / 16.0f, yy = Y + kSteps;
        const int page = sv.now >= 0 ? sv.now / 16 : 0;
        for (int k = 0; k < 16; ++k)
        {
            const int i = page * 16 + k;
            const bool has = i < sv.count && sv.on[i];
            const bool now = sv.now == i, next = sv.now >= 0 && sv.count > 0 && (sv.now + 1) % sv.count == i;
            const float x = X + kX + (float) k * sw;
            g.setColour (now ? kYellow : has ? juce::Colour (0xff1a1a1a) : juce::Colour (0xff0b0b0b)); g.fillRoundedRectangle (x + 1, yy + 2, sw - 2, kStepsH - 3, 2.0f);
            g.setColour (next ? kYellow : has ? juce::Colour (0xff383838) : juce::Colour (0xff1a1a1a)); g.drawRoundedRectangle (x + 1, yy + 2, sw - 2, kStepsH - 3, 2.0f, next ? 1.2f : 0.6f);
            if (k % 4 == 0) { g.setColour (ui::kDim.withAlpha (0.5f)); g.fillRect (x + 1, yy + kStepsH - 2, sw - 2, 1.2f); }
            if (! has) continue;
            juce::String t;
            if (v.notes == 1)
            {
                int sl = (pitched ? sv.note[i] - v.base : 0) + (int) std::floor (v.pos * (float) v.slices.n * 0.9999f);
                sl = ((sl % v.slices.n) + v.slices.n) % v.slices.n;
                t = juce::String (sl + 1);
            }
            else if (pitched) t = noteName (sv.note[i]);
            g.setColour (now ? juce::Colours::black : ui::kText);
            if (t.isEmpty()) { g.fillEllipse (x + sw * 0.5f - 2.5f, yy + kStepsH * 0.5f - 1.5f, 5.0f, 5.0f); continue; }      // (a drum's hit)
            g.setFont (font (t.length() < 4 ? 10.0f : 9.0f));
            g.drawText (t, juce::Rectangle<float> (x, yy + 2, sw, kStepsH - 3), juce::Justification::centred, false);
        }
    }
    // ---- footer
    {
        g.setColour (juce::Colour (0xff202020)); g.fillRect (X, Y + kFoot - 1.0f, r.getWidth(), 1.0f);
        juce::String f = s->name.toUpperCase() + " \xc2\xb7 " + juce::String (n / s->sr, 2) + " S";
        if (v.method == kStretch || v.lock == 1) f << " \xc2\xb7 " << juce::String (v.bars, v.bars < 1.0 ? 2 : 0) << (v.bars == 1.0 ? " BAR @ " : " BARS @ ") << juce::String (v.reg.length() / s->sr > 0.0 ? 60.0 * v.beats / (v.reg.length() / s->sr) : 0.0, 1);
        f << " \xc2\xb7 " << (s->wide ? "STEREO" : "MONO");
        if (s->rootHz > 0.0f) f << " \xc2\xb7 ROOT " << noteName ((int) std::lround (s->rootNote));
        f << " \xc2\xb7 PEAK " << juce::String (gainToDb (s->chanPeak), 1) << " DB";
        g.setColour (ui::kDim); g.setFont (font (10.0f, false));
        g.drawText (f, juce::Rectangle<float> (X + 8, Y + kFoot + 1, 420, 16), juce::Justification::centredLeft, true);
        juce::String chip = zoomed ? "ZOOM " + juce::String ((int) std::lround (1.0 / std::max (1.0e-6f, zoomB - zoomA))) + "\xc3\x97" : v.notes == 1 ? juce::String (v.slices.n) + (v.slices.n == 1 ? " SLICE" : " SLICES") : juce::String();
        if (v.method == kStretch && v.warp) { int w = 0; for (int k = 0; k < kWarps; ++k) w += v.ws[k] > 0.0f ? 1 : 0; chip = juce::String (w) + " WARP MARKERS"; }
        if (chip.isNotEmpty())
        {
            g.setFont (font (10.0f));
            const float cw = 14.0f + g.getCurrentFont().getStringWidthFloat (chip);
            const juce::Rectangle<float> cr (X + r.getWidth() - 8.0f - cw, Y + kFoot + 2.0f, cw, 13.0f);
            g.setColour (kYellow); g.fillRoundedRectangle (cr, 2.0f);
            g.setColour (juce::Colours::black); g.drawText (chip, cr, juce::Justification::centred, false);
        }
    }
}

// ------------------------------------------------------------------------------------------------ the mouse
void SampleEngine::mouse (juce::Point<float> p, juce::Rectangle<float> r, bool down, Module& m, const SampleData* s)
{
    if (s == nullptr || ! ids.ok() || ! m.getParam || ! m.setParam) return;
    const float bpm = seen.bpm.load(), bpb = std::max (1.0f, seen.beatsPerBar.load());
    const View v = viewOf (*this, m, *s, bpm, bpb);
    const double n = (double) s->length();
    Across ac { r, zoomA, zoomB, n, &v };
    const float y = p.y - r.getY();
    const juce::Rectangle<float> wave (r.getX() + kX, r.getY() + kWave, kW, kWaveH);
    if (down)
    {
        drag = Drag();
        if (y >= kOv - 2 && y < kOv + kOvH + 2) { if (zoomed) drag.what = 5; }
        else if (y >= kTab && y < kTab + kTabH)
        {
            // a tab: hear its slice
            const double src = ac.srcOfX (p.x);
            const int k = v.slices.sliceAt (src);
            int at = k - (int) std::floor (v.pos * (float) v.slices.n * 0.9999f);
            at = ((at % v.slices.n) + v.slices.n) % v.slices.n;
            seen.audition.store (pitched && v.notes == 1 ? at : k);
            return;
        }
        else if (wave.expanded (0, 4).contains (p))
        {
            auto near = [&] (float x, float tol) { return std::abs (p.x - x) <= tol; };
            if (v.beatsAcross && v.warp)
                for (int k = 0; k < kWarps && drag.what == 0; ++k)
                    if (v.ws[k] > 0.0f && near (ac.xOfBeat ((double) v.wd[k] * v.beats), 5.0f)) { drag.what = 4; drag.index = k; }
            if (drag.what == 0 && v.loop && y < kWave + 12)
            {
                if (near (ac.xOfSrc (v.loopA), 6.0f)) drag.what = 6;
                else if (near (ac.xOfSrc (v.loopB), 6.0f)) drag.what = 7;
            }
            if (drag.what == 0 && near (ac.xOfSrc (v.reg.start), 6.0f)) drag.what = 1;
            if (drag.what == 0 && near (ac.xOfSrc (v.reg.end), 6.0f)) drag.what = 2;
            if (drag.what == 0 && v.sliceBy == kByMarkers)
                for (int k = 0; k < kMarkers && drag.what == 0; ++k)
                    if (v.cuts[k] > 0.0f && near (ac.xOfSrc ((double) v.cuts[k] * n), 4.0f)) { drag.what = 3; drag.index = k; }
            // (SF-000, the hundred and fourteenth build) GRAIN: anywhere else on the wave places POSITION, the yellow band, and
            // drags it. Wiley, Nov 24: "does the position handle in the grain sampler also get stored? it doesn't seem to move
            // the knob?" (the handles were START and END; POSITION could only be turned). It turns the POSITION knob.
            if (drag.what == 0 && v.method == kGrain) drag.what = 8;
        }
        if (drag.what == 0) return;
    }
    switch (drag.what)
    {
        case 1: case 2:
        {
            double src = zeroNear (*s, juce::jlimit (0.0, n, ac.srcOfX (p.x)));
            float f = (float) (src / n);
            if (f < 0.002f) f = 0.0f;
            if (f > 0.998f) f = 1.0f;
            const float a = m.getParam (ids.start), b = m.getParam (ids.end);
            f = drag.what == 1 ? std::min (f, b - 0.002f) : std::max (f, a + 0.002f);
            m.setParam (drag.what == 1 ? ids.start : ids.end, juce::jlimit (0.0f, 1.0f, f));
            break;
        }
        case 3:
        {
            const double src = zeroNear (*s, juce::jlimit (0.0, n, ac.srcOfX (p.x)));
            m.setParam (ids.cut0 + drag.index, juce::jlimit (0.001f, 0.999f, (float) (src / n)));
            break;
        }
        case 4:
        {
            // a warp marker: the beat it is put on (onto a sixteenth when within 5 pixels of one), between its neighbours
            double b = ac.beatOfX (p.x);
            const double six = std::round (b * 4.0) / 4.0;
            if (std::abs (ac.xOfBeat (six) - p.x) <= 5.0f) b = six;
            const double me = (double) v.ws[drag.index];
            double lo = 0.0, hi = v.beats;
            for (int k = 0; k < kWarps; ++k)
            {
                if (k == drag.index || v.ws[k] <= 0.0f) continue;
                const double o = (double) v.wd[k] * v.beats;
                if (v.ws[k] < me) lo = std::max (lo, o); else hi = std::min (hi, o);
            }
            b = juce::jlimit (lo + 0.01, hi - 0.01, b);
            m.setParam (ids.wd0 + drag.index, (float) (b / std::max (1.0e-9, v.beats)));
            break;
        }
        case 5:
        {
            const float w = zoomB - zoomA, c = juce::jlimit (w * 0.5f, 1.0f - w * 0.5f, (p.x - r.getX() - kX) / kW);
            zoomA = c - w * 0.5f; zoomB = c + w * 0.5f;
            break;
        }
        case 6: case 7:
        {
            const double len = std::max (1.0, v.reg.length());
            const double src = zeroNear (*s, juce::jlimit (v.reg.start, v.reg.end, ac.srcOfX (p.x)));
            float f = (float) ((src - v.reg.start) / len);
            if (drag.what == 6) m.setParam (ids.lstart, juce::jlimit (0.0f, std::max (0.0f, m.getParam (ids.lend) - 0.01f), f));
            else m.setParam (ids.lend, juce::jlimit (std::min (1.0f, m.getParam (ids.lstart) + 0.01f), 1.0f, f));
            break;
        }
        case 8:
        {
            const double len = std::max (1.0, v.reg.length());
            m.setParam (ids.pos, juce::jlimit (0.0f, 1.0f, (float) ((juce::jlimit (v.reg.start, v.reg.end, ac.srcOfX (p.x)) - v.reg.start) / len)));
            break;
        }
        default: break;
    }
}

void SampleEngine::doubleClick (juce::Point<float> p, juce::Rectangle<float> r, Module& m, const SampleData* s)
{
    if (s == nullptr || ! ids.ok() || ! m.getParam || ! m.setParam) return;
    const float bpm = seen.bpm.load(), bpb = std::max (1.0f, seen.beatsPerBar.load());
    const View v = viewOf (*this, m, *s, bpm, bpb);
    const double n = (double) s->length();
    Across ac { r, zoomA, zoomB, n, &v };
    const juce::Rectangle<float> wave (r.getX() + kX, r.getY() + kWave, kW, kWaveH);
    if (! wave.expanded (0, 4).contains (p)) return;
    if (v.beatsAcross)
    {
        // STRETCH: a warp marker. On one: it goes. Anywhere else: one is put on the moment there (on the transient
        // nearest within 20 ms, else where the file crosses nought), on the beat it is at now, so nothing moves until it is dragged.
        for (int k = 0; k < kWarps; ++k)
            if (v.warp && v.ws[k] > 0.0f && std::abs (ac.xOfBeat ((double) v.wd[k] * v.beats) - p.x) <= 5.0f) { m.setParam (ids.ws0 + k, 0.0f); m.setParam (ids.wd0 + k, 0.0f); return; }
        // (SF-000, the hundred and eleventh build) WARP off with markers kept: a double click turns WARP back on with them as
        // they were, and adds none (it cleared them, and began again)
        if (! v.warp) { bool any = false; for (int k = 0; k < kWarps; ++k) any = any || v.ws[k] > 0.0f; if (any) { m.setParam (ids.warp, 1.0f); return; } }
        double src = ac.srcOfX (p.x);
        double best = 0.02 * s->sr; double at = -1.0;
        for (auto& h : s->hits) if (std::abs ((double) h.at - src) < best) { best = std::abs ((double) h.at - src); at = (double) h.at; }
        src = at >= 0.0 ? at : zeroNear (*s, src);
        const double b = v.map.beatAt (src);
        for (int k = 0; k < kWarps; ++k)
            if (v.ws[k] <= 0.0f || ! v.warp)
            {
                if (! v.warp) for (int j = 0; j < kWarps; ++j) { m.setParam (ids.ws0 + j, 0.0f); m.setParam (ids.wd0 + j, 0.0f); }
                m.setParam (ids.ws0 + k, juce::jlimit (0.0001f, 0.9999f, (float) (src / n)));
                m.setParam (ids.wd0 + k, (float) (b / std::max (1.0e-9, v.beats)));
                if (! v.warp) m.setParam (ids.warp, 1.0f);
                return;
            }
        return;
    }
    // a slice marker: on one, it goes; anywhere else, one is put there (where the file crosses nought). With the slices
    // cut another way (ONSETS, EQUAL, BEATS), those cuts become the hand's markers first, and SLICE BY becomes MARKERS.
    if (v.sliceBy != kByMarkers)
    {
        int k = 0;
        for (int i = 1; i < v.slices.n && k < kMarkers; ++i) m.setParam (ids.cut0 + k++, juce::jlimit (0.001f, 0.999f, (float) (v.slices.at[i] / n)));
        for (; k < kMarkers; ++k) m.setParam (ids.cut0 + k, 0.0f);
        m.setParam (ids.sliceby, (float) kByMarkers);
        float cuts[kMarkers]; for (int j = 0; j < kMarkers; ++j) cuts[j] = m.getParam (ids.cut0 + j);
        juce::ignoreUnused (cuts);
    }
    for (int k = 0; k < kMarkers; ++k)
        if (const float c = m.getParam (ids.cut0 + k); c > 0.0f && std::abs (ac.xOfSrc ((double) c * n) - p.x) <= 4.0f) { m.setParam (ids.cut0 + k, 0.0f); return; }
    const double src = zeroNear (*s, juce::jlimit (0.0, n, ac.srcOfX (p.x)));
    for (int k = 0; k < kMarkers; ++k) if (m.getParam (ids.cut0 + k) <= 0.0f) { m.setParam (ids.cut0 + k, juce::jlimit (0.001f, 0.999f, (float) (src / n))); return; }
}

bool SampleEngine::wheel (juce::Point<float> p, juce::Rectangle<float> r, float dy, const SampleData* s)
{
    if (s == nullptr || std::abs (dy) < 1.0e-4f) return false;
    const float f = juce::jlimit (0.0f, 1.0f, (p.x - r.getX() - kX) / kW);
    const float at = zoomA + f * (zoomB - zoomA);
    float w = (zoomB - zoomA) * (dy > 0.0f ? 0.8f : 1.25f);
    const float least = 64.0f / (float) std::max (64, s->length());
    w = juce::jlimit (least, 1.0f, w);
    zoomA = juce::jlimit (0.0f, 1.0f - w, at - f * w); zoomB = zoomA + w;
    zoomed = w < 0.999f;
    return true;
}
} // namespace sf
