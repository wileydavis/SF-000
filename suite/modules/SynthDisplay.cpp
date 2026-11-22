// (SF-000, the hundred and second build) SYNTH's display: what the sound is made of, and what every voice is doing now.
//
//   header      18  PATCH, CIRCUIT, the voices sounding, UNISON
//   three panes 72  WAVE: one cycle of A, of B and of their MIX, and the table's frames with SHAPE lit on them.
//                   FILTER: the circuit's curve from 20 Hz to 20 kHz, a tick where each voice's cutoff is now, BODY's modes.
//                   ENVELOPES: the AMP envelope (solid) and the MOD envelope (dashed) as set, a dot for each voice on them.
//   voices      18  sixteen cells, a voice each: the note it plays, lit as loud as it is
//   steps       20  the lane's sixteen steps, the note each plays
//   footer      17  what the lane's voices cost now
// Everything on it follows a knob; the mouse is a second way in. Message thread: it reads what the audio publishes (Seen)
// and the module's parameters.
#include "SynthEngine.h"

namespace sf
{
using namespace syn;
namespace
{
constexpr float kPaneY = 21, kPaneH = 72, kVoxY = 97, kVoxH = 18, kStepY = 117, kStepH = 20, kFootY = 139;
constexpr float kPaneX[3] = { 8, 186, 364 }, kPaneW = 172;
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
    for (int k = 0; k < 3; ++k)
    {
        g.setColour (juce::Colour (0xff1c1c1c)); g.drawRect (juce::Rectangle<float> (X + kPaneX[k], Y + kPaneY, kPaneW, kPaneH), 1.0f);
        g.setColour (kLabel); g.setFont (font (9.5f, true));
        const juce::String dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));       // (read as UTF-8: a plain literal drew it as two letters)
        const juce::String t = k == 0 ? "WAVE" + dot + juce::String (tableNames()[(size_t) juce::jlimit (0, (int) tableNames().size() - 1, b.tableA)])
                             : k == 1 ? "FILTER" + dot + juce::String (circuitName (b.circuit)) : juce::String ("ENVELOPES");
        g.drawText (t, juce::Rectangle<float> (X + kPaneX[k] + 4, Y + kPaneY + 1, kPaneW - 8, 11), juce::Justification::centredLeft, true);
    }
    // ---- WAVE
    {
        const float x0 = X + kPaneX[0] + 8, wd = kPaneW - 16, cy = Y + kPaneY + 34, amp = 17.0f;
        g.setColour (juce::Colour (0xff1e1e1e)); g.fillRect (x0, cy, wd, 1.0f);
        const Wavetable* ta = tableFor (b.tableA);
        const Wavetable* tb = tableFor (b.tableB);
        const bool useB = b.mix > 0.001f;
        const bool onlyNoise = b.noise > 0.5f && b.mix < 0.001f && b.sub < 0.001f;
        auto curve = [&] (std::function<float (double)> f, juce::Colour c, float thick)
        {
            juce::Path p;
            constexpr int N = 96;
            float peak = 1.0e-3f; for (int i = 0; i < N; ++i) peak = std::max (peak, std::abs (f ((double) i / N)));
            for (int i = 0; i <= N; ++i) { const float y = cy - amp * f ((double) i / N) / peak; const float x = x0 + wd * (float) i / N; if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y); }
            g.setColour (c); g.strokePath (p, juce::PathStrokeType (thick));
        };
        if (ta != nullptr && ! onlyNoise)
        {
            const float pa = shapePlace (b.shape), pb = shapePlace (b.shapeB);
            if (useB && tb != nullptr)
            {
                curve ([&] (double ph) { return ta->at (pa, 0, ph); }, juce::Colour (0xff80e262).withAlpha (0.75f), 0.9f);
                curve ([&] (double ph) { return tb->at (pb, 0, ph); }, juce::Colour (0xff56a8ff).withAlpha (0.75f), 0.9f);
            }
            curve ([&] (double ph) { return ta->at (pa, 0, ph) * (1.0f - b.mix) + (useB && tb != nullptr ? tb->at (pb, 0, ph) * b.mix : 0.0f); }, ui::kText, 1.4f);
            // the table's frames, SHAPE lit
            const int fr = std::max (1, ta->frames());
            const float ty = Y + kPaneY + kPaneH - 11;
            const int shown = std::min (64, fr);
            for (int k = 0; k < shown; ++k)
            {
                const float fx = x0 + wd * (float) k / (float) std::max (1, shown - 1);
                g.setColour (juce::Colour (0xff4a4a48)); g.fillRect (fx - 0.4f, ty + 3.0f, 0.8f, 5.0f);
            }
            const float sx = x0 + wd * pa;
            g.setColour (kYellow); g.fillRect (sx - 0.8f, ty, 1.6f, 8.0f);
        }
        else
        {
            Noise nz { 0x1234567u };
            curve ([&nz] (double) { return nz.next(); }, ui::kText, 1.1f);
        }
    }
    // ---- FILTER
    {
        const float fx0 = X + kPaneX[1] + 6, fwd = kPaneW - 12, top = Y + kPaneY + 13, bot = Y + kPaneY + kPaneH - 6;
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
        const float ex0 = X + kPaneX[2] + 6, ewd = kPaneW - 12, etop = Y + kPaneY + 14, ebot = Y + kPaneY + kPaneH - 6;
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
    // ---- VOICES
    {
        const float cw = 528.0f / 16.0f;
        int shown = 0;
        g.setFont (font (9.0f));
        for (int k = 0; k < kSlots && shown < 16; ++k)
        {
            const auto& o = seen.v[k];
            const int key = o.key.load();
            if (key < 0) continue;
            const float x = X + 8 + (float) shown * cw;
            const float lv = clamp01 (o.level.load());
            const auto c = juce::Colour::greyLevel (0.16f + 0.84f * lv);
            g.setColour (c); g.fillRoundedRectangle (x + 1, Y + kVoxY, cw - 2, kVoxH, 2.0f);
            g.setColour (lv > 0.45f ? juce::Colours::black : ui::kText);
            g.drawText (noteNameOf (key), juce::Rectangle<float> (x, Y + kVoxY, cw, kVoxH), juce::Justification::centred, false);
            ++shown;
        }
        for (; shown < 16; ++shown)
        {
            const float x = X + 8 + (float) shown * cw;
            g.setColour (juce::Colour (0xff0b0b0b)); g.fillRoundedRectangle (x + 1, Y + kVoxY, cw - 2, kVoxH, 2.0f);
            g.setColour (juce::Colour (0xff1a1a1a)); g.drawRoundedRectangle (x + 1, Y + kVoxY, cw - 2, kVoxH, 2.0f, 0.6f);
        }
    }
    // ---- STEPS
    {
        Module::StepsView sv;
        if (m.stepsOf) m.stepsOf (sv);
        const float sw = 528.0f / 16.0f, yy = Y + kStepY;
        const int page = sv.now >= 0 ? sv.now / 16 : 0;
        for (int k = 0; k < 16; ++k)
        {
            const int i = page * 16 + k;
            const bool has = i < sv.count && sv.on[i];
            const bool now = sv.now == i, next = sv.now >= 0 && sv.count > 0 && (sv.now + 1) % sv.count == i;
            const float x = X + 8 + (float) k * sw;
            g.setColour (now ? kYellow : has ? juce::Colour (0xff1a1a1a) : juce::Colour (0xff0b0b0b)); g.fillRoundedRectangle (x + 1, yy + 1, sw - 2, kStepH - 3, 2.0f);
            g.setColour (next ? kYellow : has ? juce::Colour (0xff383838) : juce::Colour (0xff1a1a1a)); g.drawRoundedRectangle (x + 1, yy + 1, sw - 2, kStepH - 3, 2.0f, next ? 1.2f : 0.6f);
            if (k % 4 == 0) { g.setColour (ui::kDim.withAlpha (0.5f)); g.fillRect (x + 1, yy + kStepH - 2, sw - 2, 1.2f); }
            if (! has) continue;
            g.setColour (now ? juce::Colours::black : ui::kText);
            if (! pitched) { g.fillEllipse (x + sw * 0.5f - 2.5f, yy + kStepH * 0.5f - 2.5f, 5.0f, 5.0f); continue; }
            const juce::String t = noteNameOf (sv.note[i]);
            g.setFont (font (t.length() < 4 ? 10.0f : 9.0f));
            g.drawText (t, juce::Rectangle<float> (x, yy + 1, sw, kStepH - 3), juce::Justification::centred, false);
            if (sv.vel[i] > 0.85f) { g.setColour (now ? juce::Colours::black : kYellow); g.fillRect (x + sw * 0.5f - 5.0f, yy + kStepH - 5.5f, 10.0f, 1.6f); }
        }
    }
    // ---- footer
    {
        g.setColour (juce::Colour (0xff202020)); g.fillRect (X, Y + kFootY - 1.0f, r.getWidth(), 1.0f);
        g.setColour (kLabel); g.setFont (font (10.5f, true));
        juce::String f = (monoNow || b.voices == 1 ? "MONO" : "POLY " + juce::String (b.voices)) + " \xc2\xb7 " + juce::String (seen.cost.load() * 100.0f, 1) + " % OF A CORE";
        if (b.tableA >= kTableFile)
        {
            const auto* t = fromFile.audio();
            f << " \xc2\xb7 " << (t == nullptr ? juce::String ("NO FILE ON THIS LANE: BASIC PLAYS") : b.tableA == kTableFile ? (t->file != nullptr ? t->fileSays : juce::String ("NOT A WAVETABLE FILE: BASIC PLAYS")) : scanState->slot.audio() != nullptr && scanState->slot.audio()->table != nullptr ? juce::String (scanState->slot.audio()->table->frames()) + " FRAMES OF " + juce::String (scanState->slot.audio()->seconds, 1) + " S" + (b.tableA == kTableRecording ? ", ITS LEVELS KEPT" : "")
                                     : scanState->slot.audio() != nullptr ? juce::String ("TOO SHORT A RECORDING: BASIC PLAYS") : juce::String ("MAKING ITS FRAMES: BASIC PLAYS TILL THEN"));
        }
        g.drawText (f, juce::Rectangle<float> (X + 8, Y + kFootY + 1, 520, 16), juce::Justification::centredLeft, true);
    }
}

void SynthEngine::mouse (juce::Point<float> p, juce::Rectangle<float> r, bool down, Module& m)
{
    if (! ids.ok() || ! m.getParam || ! m.setParam) return;
    const float X = r.getX(), Y = r.getY();
    if (down)
    {
        drag = 0;
        if (p.y < Y + kPaneY || p.y > Y + kPaneY + kPaneH) return;
        for (int k = 0; k < 3; ++k) if (p.x >= X + kPaneX[k] && p.x <= X + kPaneX[k] + kPaneW) drag = k + 1;
    }
    if (drag == 1)
    {
        // the table's strip: SHAPE where it is pressed
        const float f = juce::jlimit (0.0f, 1.0f, (p.x - X - kPaneX[0] - 8) / (kPaneW - 16));
        m.setParam (ids.shape, f);
    }
    else if (drag == 2)
    {
        // the filter's point: across is CUTOFF, up and down RES
        const float fx0 = X + kPaneX[1] + 6, fwd = kPaneW - 12;
        const float hz = 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (p.x - fx0) / fwd));
        m.setParam (ids.cutoff, clamp01 (std::log2 (hz / 30.0f) / 9.23f));
        const float top = Y + kPaneY + 13, bot = Y + kPaneY + kPaneH - 6;
        m.setParam (ids.res, juce::jlimit (0.0f, 1.0f, (bot - p.y) / (bot - top)));
    }
    else if (drag == 3)
    {
        // the envelopes: across is ENV DECAY, up and down SUSTAIN
        const float f = juce::jlimit (0.0f, 1.0f, (p.x - X - kPaneX[2] - 6) / (kPaneW - 12));
        m.setParam (ids.decay, f);
        const float top = Y + kPaneY + 14, bot = Y + kPaneY + kPaneH - 6;
        m.setParam (ids.sustain, juce::jlimit (0.0f, 1.0f, (bot - p.y) / (bot - top)));
    }
}

bool SynthEngine::wheel (juce::Point<float> p, juce::Rectangle<float> r, float dy, Module& m)
{
    if (! ids.ok() || ! m.getParam || ! m.setParam || std::abs (dy) < 1.0e-4f) return false;
    const float X = r.getX();
    int which = -1;
    for (int k = 0; k < 3; ++k) if (p.x >= X + kPaneX[k] && p.x <= X + kPaneX[k] + kPaneW) which = k;
    const int id = which == 0 ? ids.shape : which == 1 ? ids.cutoff : which == 2 ? ids.decay : -1;
    if (id < 0) return false;
    m.setParam (id, clamp01 (m.getParam (id) + dy * 0.05f));
    return true;
}
} // namespace sf
