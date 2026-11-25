// (SF-000, the hundred and second build) SYNTH's display: what the sound is made of, and what every voice is doing now.
// (the hundred and nineteenth build: Wiley, Nov 25, "making the synth screen more visual to aid in parameter adjustment,
// especially for wavetables" and "the goal is using the movement through a wavetable (or a small section of it) to create
// movment and rythmic evolution in the synth parts". The table is the most of it now.)
//
//   header      18  PATCH, CIRCUIT, the voices sounding, UNISON
//   the table  112  TABLE A's frames stacked, front to back as they go along it: the section the place moves in (SPAN, from
//                   SHAPE) lit, SHAPE's frame yellow, the frame each voice is on now white and moving as it moves. Beside it,
//                   HARMONICS: the frame heard now (the loudest voice's, else SHAPE's), harmonic by harmonic, in dB.
//   the strip   22  for SAMPLE and RECORDING: the recording itself, where along it the frames are, the section on it, SHAPE
//                   and the voices; for the other tables how bright each frame is. Pressed: SHAPE there; its section's right
//                   edge dragged: SPAN.
//   two panes   48  FILTER: the circuit's curve, a tick where each voice's cutoff is. ENVELOPES: AMP solid, MOD dashed.
//   footer      17  what the voices cost; the table's frames; the section and SCAN SYNC.
// Everything on it follows a knob; the mouse is a second way in (and turns the knob: the hundred and thirteenth build).
// Message thread: it reads what the audio publishes (Seen) and the module's parameters.
#include "SynthEngine.h"

namespace sf
{
using namespace syn;
namespace
{
constexpr float kBigY = 21, kStackX = 8, kStackW = 348, kHarmX = 364, kHarmW = 172;
constexpr float kStripH = 22, kStripX = 8, kStripW = 528;
constexpr float kPaneH = 48;
// (the screen may give it less than its 230, where a lane's EQ and its four rows of knobs need the room: the table's row
// gives it up, the rest moved up with it)
struct Rows { float bigH, stripY, paneY, footY; };
Rows rowsFor (juce::Rectangle<float> r)
{
    const float sq = juce::jlimit (0.0f, 60.0f, (float) SynthEngine::kDisplayHeight - r.getHeight());
    return { 112.0f - sq, 137.0f - sq, 163.0f - sq, 213.0f - sq };
}
constexpr float kPaneX[2] = { 8, 276 }, kPaneW = 260;
// the section, as the voices have it: from SHAPE's place, SPAN long, moved back to fit (the whole table where SPAN is full
// and SCAN is not in note values)
struct Section { float lo = 0.0f, hi = 1.0f; bool whole = true; };
Section sectionOf (const SynthEngine::Block& b)
{
    const float scanD = (b.scan - 0.5f) * 2.0f;
    const bool synced = b.scanSync > 0 && std::abs (scanD) >= 0.02f;
    const float span = juce::jlimit (0.0f, 1.0f, b.span);
    if (span >= 0.999f && ! synced) return {};
    const float lo = std::min (clamp01 (shapePlace (b.shape)), 1.0f - span);
    return { lo, lo + span, false };
}
const juce::Colour kYellow { 0xffe2c33c };
// (Wiley, Nov 21: "make sure the display screens for the synth has high contrast labeling against the black of the display
// portion.") Every word on the display: near white, not the dim grey; the scale's figures a light grey; lines stay dark.
const juce::Colour kLabel { 0xfff4f4f0 }, kScale { 0xffc8c8c4 };
juce::Font font (float h, bool bold = true) { return ui::mono (h, bold); }
juce::String noteNameOf (int n) { static const char* const k[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return juce::String (k[((n % 12) + 12) % 12]) + juce::String (n / 12 - 2); }
juce::Colour circuitColour (int c)
{
    switch (c)
    {
        case kLadder: return juce::Colour (0xff80e262);
        case kDiode: return juce::Colour (0xffff9a40);
        case kMs20: return juce::Colour (0xffff5f57);
        default: return juce::Colour (0xffe8e8e4);
    }
}
const char* circuitName (int c) { static const char* const n[] = { "CLEAN LP", "CLEAN BP", "CLEAN HP", "LADDER", "DIODE", "MS-20" }; return n[juce::jlimit (0, 5, c)]; }
// the circuit's response at f (dB), for the drawing (the filter's own shape, not measured)
float responseDb (int circuit, float f, float fc, float res)
{
    const std::complex<float> s (0.0f, f / std::max (1.0f, fc));
    std::complex<float> h;
    switch (circuit)
    {
        case kCleanLP: case kCleanBP: case kCleanHP:
        {
            const float q = 0.6f + res * 9.4f;
            const auto den = s * s + s / q + 1.0f;
            h = circuit == kCleanLP ? 1.0f / den : circuit == kCleanBP ? (s / q) / den * (0.6f + res * 2.0f) : (s * s) / den;
            break;
        }
        case kMs20: { const float q = 0.6f + res * 12.0f; h = 1.0f / (s * s + s / q + 1.0f); break; }
        default:
        {
            const int poles = circuit == kLadder ? 4 : 3;
            const float k = (circuit == kLadder ? 3.9f : 3.4f) * res;
            const auto g = 1.0f / (1.0f + s);
            auto gp = g; for (int i = 1; i < poles; ++i) gp *= g;
            h = gp / (1.0f + k * gp);
        }
    }
    return 20.0f * std::log10 (std::max (1.0e-6f, std::abs (h)));
}
float envAt (float t, float a, float d, float s, float r, float gate)
{
    auto held = [&] (float x) { return x < a ? x / std::max (a, 1.0e-4f) : s + (1.0f - s) * std::exp (-3.0f * (x - a) / std::max (d, 1.0e-4f)); };
    if (t < gate) return held (t);
    return held (gate) * std::exp (-6.9f * (t - gate) / std::max (r, 1.0e-4f));
}
} // namespace

void SynthEngine::paint (juce::Graphics& g, juce::Rectangle<float> r, const Module& m, bool lit)
{
    juce::ignoreUnused (lit);
    g.setColour (juce::Colours::black); g.fillRect (r);
    if (! ids.ok() || ! m.getParam) return;
    const Rows rows = rowsFor (r); const float kBigH = rows.bigH, kStripY = rows.stripY, kPaneY = rows.paneY, kFootY = rows.footY;
    const Block b = blockOf (ids, [&m] (int i) { return m.getParam (i); });
    const float X = r.getX(), Y = r.getY();
    const juce::Colour col = circuitColour (b.circuit);
    const auto& pn = patches();
    const int patch = ids.patch >= 0 ? juce::jlimit (0, (int) pn.size() - 1, (int) std::lround (m.getParam (ids.patch))) : 0;
    int sounding = 0; for (int k = 0; k < kSlots; ++k) sounding += seen.v[k].key.load() >= 0 ? 1 : 0;
    // ---- header
    g.setFont (font (10.5f, true)); g.setColour (kLabel);
    g.drawText ("PATCH " + juce::String (pn[(size_t) patch].name) + " \xc2\xb7 CIRCUIT " + circuitName (b.circuit) + (b.body > 0 ? " \xc2\xb7 BODY " + juce::String (b.body == 1 ? "MEMBRANE" : b.body == 2 ? "BAR" : b.body == 3 ? "PLATE" : b.body == 4 ? "TUBE" : b.body == 5 ? "BELL" : "GONG") : juce::String()),
                juce::Rectangle<float> (X + 8, Y + 2, 380, 14), juce::Justification::centredLeft, true);
    g.drawText ("VOICES " + juce::String (sounding) + " / " + juce::String (monoNow ? 1 : b.voices) + " \xc2\xb7 UNISON " + juce::String (b.unison),
                juce::Rectangle<float> (X + r.getWidth() - 200, Y + 2, 192, 14), juce::Justification::centredRight, true);
    g.setColour (juce::Colour (0xff202020)); g.fillRect (X, Y + 18.0f, r.getWidth(), 1.0f);
    const juce::String dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));       // (read as UTF-8: a plain literal drew it as two letters)
    const bool chop = b.frames == 1 && (b.tableA == kTableSample || b.tableA == kTableRecording);
    const Wavetable* ta = tableFor (b.tableA, b.frames == 1);
    const Wavetable* tb = tableFor (b.tableB, b.frames == 1);
    const bool useB = b.mix > 0.001f && tb != nullptr;
    const Section sec = sectionOf (b);
    const float placeA = shapePlace (b.shape);
    // the voices' places now, and the loudest's
    float vPlace[kSlots]; float vLevel[kSlots]; int nv = 0; float hearAt = placeA, loudest = -1.0f;
    for (int k = 0; k < kSlots; ++k)
    {
        const auto& o = seen.v[k];
        if (o.key.load() < 0 || o.part.load() != 0) continue;
        const float pl = o.place.load(), lv = clamp01 (o.level.load());
        if (pl < 0.0f) continue;
        vPlace[nv] = pl; vLevel[nv] = lv; ++nv;
        if (lv > loudest) { loudest = lv; hearAt = pl; }
    }
    const int nFrames = ta != nullptr ? std::max (1, ta->frames()) : 1;
    const bool levels = b.tableA == kTableRecording;
    auto frameVal = [&] (float place, double ph) { return ta->at (place, 0, ph) * (levels ? ta->levelAt (place) : 1.0f); };
    // ---- the table, stacked
    {
        const float x0 = X + kStackX, top = Y + kBigY;
        g.setColour (juce::Colour (0xff1c1c1c)); g.drawRect (juce::Rectangle<float> (x0, top, kStackW, kBigH), 1.0f);
        g.setColour (kLabel); g.setFont (font (9.5f, true));
        const int frameNow = (int) std::lround (clamp01 (hearAt) * (float) (nFrames - 1)) + 1;
        g.drawText ("WAVE" + dot + juce::String (tableNames()[(size_t) juce::jlimit (0, (int) tableNames().size() - 1, b.tableA)]) + (chop ? " (CHOP)" : "")
                        + dot + juce::String (nFrames) + (nFrames == 1 ? " FRAME" : " FRAMES") + (nFrames > 1 ? dot + "AT " + juce::String (frameNow) : juce::String()),
                    juce::Rectangle<float> (x0 + 4, top + 1, kStackW - 8, 11), juce::Justification::centredLeft, true);
        if (ta != nullptr)
        {
            // front to back: the first frame at the front, low and left; the last at the back, high and right
            constexpr int kPts = 72;
            const float k = kBigH / 112.0f;
            const float w = 250.0f, dx = 84.0f, dy = 60.0f * k, amp = 12.0f * k;
            const float bx = x0 + 6.0f, by = top + kBigH - 8.0f - amp * 1.2f;
            const int shown = std::min (nFrames, 24);        // (more, and a recording's frames are a thicket)
            auto placeOf = [&] (int k) { return shown < 2 ? 0.0f : (float) k / (float) (shown - 1); };
            float peak = 1.0e-3f;
            for (int k = 0; k < shown; ++k) for (int j = 0; j < kPts; j += 3) peak = std::max (peak, std::abs (frameVal (placeOf (k), (double) j / kPts)));
            auto pathOf = [&] (float place, juce::Path& line, juce::Path& under)
            {
                const float ox = bx + dx * place, oy = by - dy * place;
                for (int j = 0; j <= kPts; ++j)
                {
                    const float x = ox + w * (float) j / kPts, y = oy - amp * frameVal (place, (double) j / kPts) / peak;
                    if (j == 0) { line.startNewSubPath (x, y); under.startNewSubPath (x, oy + amp + 2.0f); under.lineTo (x, y); } else { line.lineTo (x, y); under.lineTo (x, y); }
                }
                under.lineTo (ox + w, oy + amp + 2.0f); under.closeSubPath();
            };
            // the spine: where along the table, and the section on it
            const float sx0 = bx - 2.0f, sy0 = by + amp + 4.0f;
            g.setColour (juce::Colour (0xff303030)); g.drawLine (sx0, sy0, sx0 + dx, sy0 - dy, 1.0f);
            for (int k = shown - 1; k >= 0; --k)
            {
                const float pl = placeOf (k);
                juce::Path line, under;
                pathOf (pl, line, under);
                const bool in = pl >= sec.lo - 1.0e-4f && pl <= sec.hi + 1.0e-4f;
                // (the far frames darker, so it reads as depth: a ridge in front hides what is behind it)
                const float near = 1.0f - pl;
                g.setColour (juce::Colour::greyLevel (0.03f + 0.05f * near)); g.fillPath (under);
                const juce::Colour lineCol = in ? (sec.whole ? juce::Colour (0xff9a9a96) : juce::Colour (0xffe0d6a8)) : juce::Colour (0xff4a4a46);
                g.setColour (lineCol.withMultipliedBrightness (0.45f + 0.55f * near));
                g.strokePath (line, juce::PathStrokeType (in && ! sec.whole ? 1.1f : 0.9f));
            }
            // SHAPE's frame, yellow; each voice's, white, as loud as it is
            { juce::Path line, under; pathOf (clamp01 (placeA), line, under); g.setColour (kYellow); g.strokePath (line, juce::PathStrokeType (1.7f)); }
            for (int k = 0; k < nv; ++k)
            {
                juce::Path line, under; pathOf (clamp01 (vPlace[k]), line, under);
                g.setColour (juce::Colours::white.withAlpha (0.35f + 0.65f * vLevel[k])); g.strokePath (line, juce::PathStrokeType (1.3f));
            }
            // the section on the spine: a bracket, SHAPE a mark, the voices dots
            auto spine = [&] (float pl) { return juce::Point<float> (sx0 + dx * pl, sy0 - dy * pl); };
            if (! sec.whole)
            {
                const auto a1 = spine (sec.lo), a2 = spine (sec.hi);
                g.setColour (kYellow.withAlpha (0.8f)); g.drawLine (a1.x, a1.y, a2.x, a2.y, 3.0f);
            }
            { const auto sp = spine (clamp01 (placeA)); g.setColour (kYellow); g.fillEllipse (sp.x - 3.0f, sp.y - 3.0f, 6.0f, 6.0f); }
            for (int k = 0; k < nv; ++k) { const auto vp = spine (clamp01 (vPlace[k])); g.setColour (juce::Colours::white); g.fillEllipse (vp.x - 2.2f, vp.y - 2.2f, 4.4f, 4.4f); }
        }
        // B and NOISE, as they are added over A: at the header's right
        {
            juce::String more;
            if (useB) more << "+ B " << juce::String (tableNames()[(size_t) juce::jlimit (0, (int) tableNames().size() - 1, b.tableB)]) << " " << juce::String ((int) std::lround (b.mix * 100.0f)) << "%";
            if (b.noise > 0.001f) more << (more.isEmpty() ? "" : "  ") << "+ NOISE " << juce::String ((int) std::lround (b.noise * 100.0f)) << "%";
            if (more.isNotEmpty()) { g.setColour (juce::Colour (0xff56a8ff)); g.setFont (font (8.5f, true)); g.drawText (more, juce::Rectangle<float> (x0 + kStackW - 154, top + 1, 150, 11), juce::Justification::centredRight, true); }
        }
    }
    // ---- HARMONICS of the frame heard now
    {
        const float hx = X + kHarmX, top = Y + kBigY;
        g.setColour (juce::Colour (0xff1c1c1c)); g.drawRect (juce::Rectangle<float> (hx, top, kHarmW, kBigH), 1.0f);
        g.setColour (kLabel); g.setFont (font (9.5f, true));
        g.drawText ("HARMONICS" + dot + (nv > 0 ? juce::String ("PLAYING") : juce::String ("SHAPE")), juce::Rectangle<float> (hx + 4, top + 1, kHarmW - 8, 11), juce::Justification::centredLeft, true);
        if (ta != nullptr)
        {
            constexpr int kH = 40, kN = 256;
            std::vector<double> cyc ((size_t) kN);
            for (int i = 0; i < kN; ++i) cyc[(size_t) i] = ta->at (clamp01 (hearAt), 0, ((double) i + 0.5) / kN);
            std::vector<float> am, ph;
            Mipmap::harmonicsOf (cyc, kH, am, ph);
            float most = 1.0e-9f; for (auto v : am) most = std::max (most, v);
            const float bx = hx + 6, bw = (kHarmW - 12) / (float) kH, bot = top + kBigH - 14, hgt = kBigH - 30;
            for (int db : { -20, -40 }) { const float y = bot - hgt * (1.0f + (float) db / 60.0f); g.setColour (juce::Colour (0xff1e1e1e)); g.fillRect (bx, y, kHarmW - 12, 1.0f); g.setColour (kScale); g.setFont (font (7.5f, true)); g.drawText (juce::String (db), juce::Rectangle<float> (hx + kHarmW - 24, y - 9, 20, 9), juce::Justification::centredRight, false); }
            for (int h = 0; h < kH; ++h)
            {
                const float db = 20.0f * std::log10 (std::max (1.0e-6f, am[(size_t) h] / most));
                const float hh = hgt * juce::jlimit (0.0f, 1.0f, 1.0f + db / 60.0f);
                g.setColour (h % 2 == 0 ? kLabel : juce::Colour (0xffbdb6a0));          // (odd harmonics, the 1st, 3rd ...: the light ones)
                g.fillRect (bx + (float) h * bw + 0.5f, bot - hh, std::max (1.0f, bw - 1.0f), hh);
            }
            g.setColour (kScale); g.setFont (font (7.5f, true));
            g.drawText ("1", juce::Rectangle<float> (bx, bot + 1, 12, 9), juce::Justification::centredLeft, false);
            g.drawText (juce::String (kH), juce::Rectangle<float> (bx + kHarmW - 32, bot + 1, 20, 9), juce::Justification::centredRight, false);
        }
    }
    // ---- the strip: the recording (SAMPLE, RECORDING), or how bright each frame is; the section, SHAPE, the voices
    {
        const float x0 = X + kStripX, top = Y + kStripY;
        g.setColour (juce::Colour (0xff0c0c0c)); g.fillRect (x0, top, kStripW, kStripH);
        const auto* sc = scanState->slot.audio();
        const bool rec = (b.tableA == kTableSample || b.tableA == kTableRecording) && sc != nullptr && ! sc->overview.empty();
        // (how much of the strip the frames are: CHOP's are the first seconds of it)
        const auto cv = stripCover (b);
        const float cover = rec ? cv.second : 1.0f, from = rec ? cv.first : 0.0f;
        auto SX = [&] (float pl) { return x0 + kStripW * (from + cover * clamp01 (pl)); };
        if (rec)
        {
            const int cols = (int) sc->overview.size();
            for (int i = 0; i < (int) kStripW; ++i)
            {
                const float v = sc->overview[(size_t) std::min (cols - 1, i * cols / (int) kStripW)];
                const float hh = (kStripH - 4) * std::min (1.0f, v);
                g.setColour ((float) i >= kStripW * from && (float) i <= kStripW * (from + cover) ? juce::Colour (0xff6a6a66) : juce::Colour (0xff2a2a28));
                g.fillRect (x0 + (float) i, top + kStripH * 0.5f - hh * 0.5f, 1.0f, std::max (1.0f, hh));
            }
        }
        else if (ta != nullptr)
        {
            // how bright each frame is: the share of its sound above its fundamental, as height and as light
            const int cols = std::min (nFrames, 128);
            for (int c = 0; c < cols; ++c)
            {
                const float pl = cols < 2 ? 0.0f : (float) c / (float) (cols - 1);
                double e = 0.0, d = 0.0, prev = ta->at (pl, 0, 0.0);
                for (int j = 1; j <= 64; ++j) { const double v = ta->at (pl, 0, j / 64.0); e += v * v; d += (v - prev) * (v - prev); prev = v; }
                const float bright = (float) juce::jlimit (0.0, 1.0, std::sqrt (d / std::max (1.0e-9, e)) / 3.0);
                const float cw = kStripW / (float) cols, hh = (kStripH - 4) * (0.15f + 0.85f * bright);
                g.setColour (juce::Colour::greyLevel (0.25f + 0.6f * bright));
                g.fillRect (x0 + (float) c * cw, top + kStripH - 2 - hh, std::max (1.0f, cw - 0.5f), hh);
            }
        }
        if (! sec.whole)
        {
            g.setColour (kYellow.withAlpha (0.18f)); g.fillRect (SX (sec.lo), top, std::max (1.0f, SX (sec.hi) - SX (sec.lo)), kStripH);
            g.setColour (kYellow.withAlpha (0.8f)); g.drawRect (juce::Rectangle<float> (SX (sec.lo), top, std::max (1.0f, SX (sec.hi) - SX (sec.lo)), kStripH), 1.0f);
        }
        // SPAN's handle: the section's right edge (where the table ends, with SPAN full)
        g.setColour (kYellow); g.fillRect (SX (sec.whole ? 1.0f : sec.hi) - 2.0f, top + kStripH * 0.5f - 5.0f, 4.0f, 10.0f);
        g.setColour (kYellow); g.fillRect (SX (clamp01 (placeA)) - 0.9f, top, 1.8f, kStripH);
        for (int k = 0; k < nv; ++k) { g.setColour (juce::Colours::white.withAlpha (0.4f + 0.6f * vLevel[k])); g.fillRect (SX (clamp01 (vPlace[k])) - 0.7f, top + 2.0f, 1.4f, kStripH - 4.0f); }
    }
    for (int k = 0; k < 2; ++k)
    {
        g.setColour (juce::Colour (0xff1c1c1c)); g.drawRect (juce::Rectangle<float> (X + kPaneX[k], Y + kPaneY, kPaneW, kPaneH), 1.0f);
        g.setColour (kLabel); g.setFont (font (9.5f, true));
        const juce::String t = k == 0 ? "FILTER" + dot + juce::String (circuitName (b.circuit)) : juce::String ("ENVELOPES");
        g.drawText (t, juce::Rectangle<float> (X + kPaneX[k] + 4, Y + kPaneY + 1, kPaneW - 8, 11), juce::Justification::centredLeft, true);
    }
    // ---- FILTER
    {
        const float fx0 = X + kPaneX[0] + 6, fwd = kPaneW - 12, top = Y + kPaneY + 13, bot = Y + kPaneY + kPaneH - 4;
        auto FX = [&] (float hz) { return fx0 + fwd * std::log (hz / 20.0f) / std::log (1000.0f); };
        auto FY = [&] (float db) { return top + (bot - top) * (12.0f - juce::jlimit (-48.0f, 12.0f, db)) / 60.0f; };
        g.setFont (font (8.5f, true));
        for (float hz : { 100.0f, 1000.0f, 10000.0f })
        {
            g.setColour (juce::Colour (0xff1e1e1e)); g.fillRect (FX (hz), top, 1.0f, bot - top);
            g.setColour (kScale); g.drawText (hz < 500 ? "100" : hz < 5000 ? "1K" : "10K", juce::Rectangle<float> (FX (hz) + 2, top - 1, 30, 9), juce::Justification::centredLeft, false);
        }
        const float fc = cutoffHz (b.cutoff);
        juce::Path p;
        for (int i = 0; i <= 120; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / 120.0f);
            const float y = FY (responseDb (b.circuit, f, b.circuit == kDiode ? fc * 1.3f : fc, b.res));
            if (i == 0) p.startNewSubPath (FX (f), y); else p.lineTo (FX (f), y);
        }
        g.setColour (col); g.strokePath (p, juce::PathStrokeType (1.5f));
        const float pky = FY (responseDb (b.circuit, fc, b.circuit == kDiode ? fc * 1.3f : fc, b.res));
        g.setColour (juce::Colours::black); g.fillEllipse (FX (fc) - 3.2f, pky - 3.2f, 6.4f, 6.4f);
        g.setColour (col); g.drawEllipse (FX (fc) - 3.2f, pky - 3.2f, 6.4f, 6.4f, 1.3f);
        for (int k = 0; k < kSlots; ++k)
        {
            const auto& o = seen.v[k];
            if (o.key.load() < 0) continue;
            const float c = juce::jlimit (20.0f, 20000.0f, o.cutoff.load());
            juce::Path t; t.addTriangle (FX (c), bot - 5.0f, FX (c) - 2.6f, bot, FX (c) + 2.6f, bot);
            g.setColour (ui::kText.withAlpha (0.35f + 0.65f * clamp01 (o.level.load()))); g.fillPath (t);
        }
    }
    // ---- ENVELOPES
    {
        const float ex0 = X + kPaneX[1] + 6, ewd = kPaneW - 12, etop = Y + kPaneY + 14, ebot = Y + kPaneY + kPaneH - 4;
        const float a = b.attack * 0.001f, d = decaySeconds (b.decay), rl = b.release * 0.001f, ma = b.mattack * 0.001f;
        const float gate = std::max (a + 0.6f * d, 0.05f), tmax = gate + std::max (0.05f, std::min (2.5f, rl * 0.8f));
        auto EX = [&] (float t) { return ex0 + ewd * t / tmax; };
        auto EY = [&] (float v) { return ebot - (ebot - etop) * v; };
        g.setColour (juce::Colour (0xff1e1e1e)); g.fillRect (EX (gate), etop, 1.0f, ebot - etop);
        juce::Path am, md;
        for (int i = 0; i <= 120; ++i)
        {
            const float t = tmax * (float) i / 120.0f;
            const float ya = EY (envAt (t, a, d, b.sustain, rl, gate)), ym = EY (envAt (t, ma, d, 0.0f, d * 0.5f, gate));
            if (i == 0) { am.startNewSubPath (EX (t), ya); md.startNewSubPath (EX (t), ym); } else { am.lineTo (EX (t), ya); md.lineTo (EX (t), ym); }
        }
        juce::Path dashed; const float dl[2] = { 3.0f, 2.0f };
        juce::PathStrokeType (1.1f).createDashedStroke (dashed, md, dl, 2);
        g.setColour (col); g.fillPath (dashed);
        g.setColour (ui::kText); g.strokePath (am, juce::PathStrokeType (1.4f));
        for (int k = 0; k < kSlots; ++k)
        {
            const auto& o = seen.v[k];
            if (o.key.load() < 0) continue;
            const float rel = o.rel.load();
            const float t = rel >= 0.0f ? gate + rel : std::min (o.t.load(), gate);
            if (t > tmax) continue;
            const float rr = o.accent.load() ? 3.4f : 2.4f;
            g.setColour (o.accent.load() ? kYellow : ui::kText);
            g.fillEllipse (EX (t) - rr, EY (clamp01 (o.level.load())) - rr, 2 * rr, 2 * rr);
        }
    }
    // ---- footer
    {
        g.setColour (juce::Colour (0xff202020)); g.fillRect (X, Y + kFootY - 1.0f, r.getWidth(), 1.0f);
        g.setColour (kLabel); g.setFont (font (10.5f, true));
        juce::String f = (monoNow || b.voices == 1 ? "MONO" : "POLY " + juce::String (b.voices)) + " \xc2\xb7 " + juce::String (seen.cost.load() * 100.0f, 1) + " % OF A CORE";
        {
            static const char* const syncName[] = { "FREE", "1/32", "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BARS", "4 BARS" };
            const float scanD = (b.scan - 0.5f) * 2.0f;
            if (b.span < 0.999f) f << " \xc2\xb7 SPAN " << juce::String ((int) std::lround (b.span * 100.0f)) << "%";
            if (std::abs (scanD) >= 0.02f) f << " \xc2\xb7 SCAN " << (b.scanSync > 0 ? juce::String (syncName[juce::jlimit (0, 8, b.scanSync)]) : juce::String ("FREE")) << (scanD > 0.0f ? " FWD" : " BACK");
        }
        if (b.tableA >= kTableFile)
        {
            const auto* t = fromFile.audio();
            f << " \xc2\xb7 " << (t == nullptr ? juce::String ("NO FILE ON THIS LANE: BASIC PLAYS") : b.tableA == kTableFile ? (t->file != nullptr ? t->fileSays : juce::String ("NOT A WAVETABLE FILE: BASIC PLAYS")) : scanState->slot.audio() != nullptr && scanState->slot.audio()->table != nullptr ? (b.frames == 1 && scanState->slot.audio()->chop != nullptr ? juce::String (scanState->slot.audio()->chop->frames()) + " SLICES OF " + juce::String (scanState->slot.audio()->chopSeconds, 1) + " S" : juce::String (scanState->slot.audio()->table->frames()) + " FRAMES OF " + juce::String (scanState->slot.audio()->seconds, 1) + " S") + (b.tableA == kTableRecording ? ", ITS LEVELS KEPT" : "")
                                     : scanState->slot.audio() != nullptr ? juce::String ("TOO SHORT A RECORDING: BASIC PLAYS") : juce::String ("MAKING ITS FRAMES: BASIC PLAYS TILL THEN"));
        }
        g.drawText (f, juce::Rectangle<float> (X + 8, Y + kFootY + 1, 528, 16), juce::Justification::centredLeft, true);
    }
}

// the knob that puts SHAPE at a place along the table (shapePlace turned round: its flat ground at the quarters kept)
static float knobAt (float place)
{
    const float q = clamp01 (place) * 4.0f;
    const int i = juce::jlimit (0, 4, (int) std::lround (q));
    const float s = q - (float) i;
    constexpr float flat = 0.08f;
    if (std::abs (s) < 1.0e-4f) return (float) i / 4.0f;
    const float d = (s > 0.0f ? flat : -flat) + 2.0f * s * (0.5f - flat);
    return clamp01 (((float) i + d) / 4.0f);
}

std::pair<float, float> SynthEngine::stripCover (const Block& b) const
{
    const auto* sc = scanState->slot.audio();
    if (b.frames != 1 || (b.tableA != kTableSample && b.tableA != kTableRecording) || sc == nullptr || sc->chop == nullptr || sc->chopSeconds <= 0.0) return { 0.0f, 1.0f };
    const double whole = std::max (0.01, sc->seconds);
    const float from = (float) juce::jlimit (0.0, 0.98, sc->chopStart / whole);
    return { from, (float) juce::jlimit (0.02, 1.0 - (double) from, sc->chopSeconds / whole) };
}

void SynthEngine::mouse (juce::Point<float> p, juce::Rectangle<float> r, bool down, Module& m)
{
    if (! ids.ok() || ! m.getParam || ! m.setParam) return;
    const float X = r.getX(), Y = r.getY();
    const Rows rows = rowsFor (r); const float kBigH = rows.bigH, kStripY = rows.stripY, kPaneY = rows.paneY;
    const Block b = blockOf (ids, [&m] (int i) { return m.getParam (i); });
    const auto cv = stripCover (b);
    auto stripPlace = [&] { return clamp01 (((p.x - X - kStripX) / kStripW - cv.first) / cv.second); };
    if (down)
    {
        drag = 0;
        if (p.y >= Y + kBigY && p.y <= Y + kBigY + kBigH && p.x >= X + kStackX && p.x <= X + kStackX + kStackW) drag = 1;
        else if (p.y >= Y + kStripY - 3 && p.y <= Y + kStripY + kStripH + 3 && p.x >= X + kStripX - 4 && p.x <= X + kStripX + kStripW + 4)
        {
            // the section's right edge (SPAN), else SHAPE
            const Section sec = sectionOf (b);
            const float edge = X + kStripX + kStripW * (cv.first + cv.second * (sec.whole ? 1.0f : sec.hi));
            drag = std::abs (p.x - edge) <= 6.0f ? 4 : 5;
        }
        else if (p.y >= Y + kPaneY && p.y <= Y + kPaneY + kPaneH)
            for (int k = 0; k < 2; ++k) if (p.x >= X + kPaneX[k] && p.x <= X + kPaneX[k] + kPaneW) drag = k + 2;
    }
    if (drag == 1)
    {
        // the stack: SHAPE at the place across it (the first frame at the left, the last at the right)
        m.setParam (ids.shape, knobAt ((p.x - X - kStackX - 8) / (kStackW - 16)));
    }
    else if (drag == 5)
        m.setParam (ids.shape, knobAt (stripPlace()));
    else if (drag == 4)
    {
        // SPAN: from SHAPE's place to here (a section at least 2% of the table)
        m.setParam (ids.span, juce::jlimit (0.02f, 1.0f, stripPlace() - clamp01 (shapePlace (b.shape))));
    }
    else if (drag == 2)
    {
        // the filter's point: across is CUTOFF, up and down RES
        const float fx0 = X + kPaneX[0] + 6, fwd = kPaneW - 12;
        const float hz = 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (p.x - fx0) / fwd));
        m.setParam (ids.cutoff, clamp01 (std::log2 (hz / 30.0f) / 9.23f));
        const float top = Y + kPaneY + 13, bot = Y + kPaneY + kPaneH - 6;
        m.setParam (ids.res, juce::jlimit (0.0f, 1.0f, (bot - p.y) / (bot - top)));
    }
    else if (drag == 3)
    {
        // the envelopes: across is ENV DECAY, up and down SUSTAIN
        const float f = juce::jlimit (0.0f, 1.0f, (p.x - X - kPaneX[1] - 6) / (kPaneW - 12));
        m.setParam (ids.decay, f);
        const float top = Y + kPaneY + 14, bot = Y + kPaneY + kPaneH - 6;
        m.setParam (ids.sustain, juce::jlimit (0.0f, 1.0f, (bot - p.y) / (bot - top)));
    }
}

bool SynthEngine::wheel (juce::Point<float> p, juce::Rectangle<float> r, float dy, Module& m)
{
    if (! ids.ok() || ! m.getParam || ! m.setParam || std::abs (dy) < 1.0e-4f) return false;
    const float X = r.getX(), Y = r.getY();
    const Rows rows = rowsFor (r); const float kBigH = rows.bigH, kStripY = rows.stripY, kPaneY = rows.paneY;
    int id = -1;
    if (p.y >= Y + kBigY && p.y <= Y + kBigY + kBigH && p.x <= X + kStackX + kStackW) id = ids.shape;
    else if (p.y >= Y + kStripY && p.y <= Y + kStripY + kStripH) id = ids.span;        // (the strip: the section's length)
    else if (p.y >= Y + kPaneY && p.y <= Y + kPaneY + kPaneH) id = p.x < X + kPaneX[1] ? ids.cutoff : ids.decay;
    if (id < 0) return false;
    m.setParam (id, clamp01 (m.getParam (id) + dy * 0.05f));
    return true;
}
} // namespace sf
