#include "Screen.h"
#include "BinaryData.h"
#include <map>

namespace sf0
{
using juce::Colour;
using juce::String;
namespace col
{
    const Colour putty (0xffCFC8BC), grey (0xffABACA7), bluegrey (0xffA4B1B7), sea (0xff8D9794), white (0xffE8E5E1), black (0xff2B2B28),
                 green (0xff428F47), red (0xffB54142), amber (0xffC5843F), yellow (0xffD7C352), blue (0xff447BB5), teal (0xff4490A1),
                 unlit (0xff4D514E), lit (0xffE9E6E2);
}
static const char* const kNotes[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static juce::Font face (const char* data, int size, float px, float tracking)
{
    static std::map<const char*, juce::Typeface::Ptr> cache;
    auto& t = cache[data];
    if (t == nullptr) t = juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    return juce::Font (juce::FontOptions (t).withPointHeight (px).withKerningFactor (tracking));
}

Screen::Screen (Session& s)
    : ses (s), eng (s.engine),
      fLbl    (face (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize, 14.0f, 0.05f)),
      fLegend (face (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize, 18.0f, 0.04f)),
      fNote   (face (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize, 14.0f, 0.05f)),
      fRead   (face (BinaryData::BarlowSemiCondensedMedium_ttf, BinaryData::BarlowSemiCondensedMedium_ttfSize, 21.0f, 0.0f)),
      fFlag   (face (BinaryData::BarlowSemiCondensedSemiBold_ttf, BinaryData::BarlowSemiCondensedSemiBold_ttfSize, 36.0f, 0.0f))
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    lastShown.fill (-1.0f);
    ses.onChange = [this] { repaint(); };
    startTimerHz (30);
}

Screen::~Screen() { ses.onChange = nullptr; }

void Screen::setPage (Page p) { endEdit (true); page = p; repaint(); }

void Screen::resized()
{
    const float s = std::min ((float) getWidth() / kW, (float) getHeight() / kH);
    xf = juce::AffineTransform::scale (s).translated (((float) getWidth() - kW * s) * 0.5f, ((float) getHeight() - kH * s) * 0.5f);
}

void Screen::dirty (R r) { repaint (r.transformedBy (xf).getSmallestIntegerContainer().expanded (2)); }
bool Screen::vis (G& g, R r) const { return g.clipRegionIntersects (r.getSmallestIntegerContainer()); }

const Screen::Hit* Screen::hitAt (juce::Point<float> p) const
{
    for (size_t i = hits.size(); i-- > 0;) if (hits[i].r.contains (p)) return &hits[i];
    return nullptr;
}

// ---------------------------------------------------------------------------------- drawing kit
void Screen::text (G& g, const String& s, R r, const juce::Font& f, Colour c, juce::Justification j)
{
    g.setColour (c); g.setFont (f);
    g.drawText (s, r, j, false);
}

float Screen::tw (const juce::Font& f, const String& s) const { return juce::GlyphArrangement::getStringWidth (f, s); }

float Screen::plate (G& g, float x, float y, const String& s, const juce::Font& f, float padX, Colour bg, Colour fg, float h)
{
    const float w = std::ceil (tw (f, s)) + padX * 2.0f;
    const R r (x, y, w, h);
    if (vis (g, r)) { g.setColour (bg); g.fillRect (r); text (g, s, r, f, fg); }
    return w;
}

void Screen::legend (G& g, float cx, float y, const String& label)
{
    const int sp = label.indexOfChar (' ');
    if (label.length() > 7 && sp > 0)
    {
        text (g, label.substring (0, sp), R (cx - 60, y, 120, 14), fLbl, col::black);
        text (g, label.substring (sp + 1), R (cx - 60, y + 14, 120, 14), fLbl, col::black);
    }
    else text (g, label, R (cx - 60, y, 120, 14), fLbl, col::black);
}

void Screen::knob (G& g, float cx, float cy, int size, float v, Kind kind, int steps, Colour litCol)
{
    const float body = (float) size * 0.5f, ro = size >= 40 ? 17.0f : 13.0f, ri = size >= 40 ? 10.0f : 7.0f;
    const float pi = juce::MathConstants<float>::pi, seg = pi / 8.0f;
    g.setColour (col::black);
    g.fillEllipse (cx - body, cy - body, body * 2, body * 2);
    juce::Path ring;
    ring.addPieSegment (cx - ro, cy - ro, ro * 2, ro * 2, 0.0f, pi * 2.0f, ri / ro);
    g.setColour (col::unlit);
    g.fillPath (ring);
    float a = 0.0f, b = 0.0f;       // the lit arc, in turns from six o'clock, clockwise
    v = juce::jlimit (0.0f, 1.0f, v);
    if (kind == Kind::Bipolar) { a = std::min (v, 0.5f); b = std::max (v, 0.5f); }
    else if (kind == Kind::List)
    {
        const int n = std::max (2, steps), m = (int) std::lround (v * (float) (n - 1));
        const float per = n <= 16 ? 1.0f / 16.0f : 1.0f / (float) n;
        a = (float) m * per; b = a + per;
    }
    else b = v;
    if (b - a > 0.0005f)
    {
        juce::Path p;
        p.addPieSegment (cx - ro, cy - ro, ro * 2, ro * 2, pi + a * 2.0f * pi, pi + b * 2.0f * pi, ri / ro);
        g.setColour (litCol);
        g.fillPath (p);
    }
    g.setColour (col::black);
    for (int k = 0; k < 16; ++k)
    {
        const float an = (float) k * seg, sx = std::sin (an), cs = -std::cos (an);
        g.drawLine (cx + sx * (ri - 0.5f), cy + cs * (ri - 0.5f), cx + sx * (ro + 0.5f), cy + cs * (ro + 0.5f), size >= 40 ? 1.7f : 1.4f);
    }
    if (kind == Kind::Bipolar) { g.setColour (col::lit); g.fillRect (cx - 1.5f, cy - body, 3.0f, body - ro); }
}

void Screen::hollow (G& g, float cx, float cy, float d, bool round)
{
    g.setColour (col::sea);
    const R r (cx - d * 0.5f + 0.5f, cy - d * 0.5f + 0.5f, d - 1.0f, d - 1.0f);
    if (round) g.drawEllipse (r, 1.0f); else g.drawRect (r, 1.0f);
}

void Screen::key (G& g, R r, Colour fill, float border)
{
    g.setColour (col::black); g.fillRect (r);
    g.setColour (fill); g.fillRect (r.reduced (border));
}

void Screen::lamp (G& g, float cx, float cy, Colour fill, float d)
{
    g.setColour (col::black); g.fillEllipse (cx - d * 0.5f, cy - d * 0.5f, d, d);
    g.setColour (fill); g.fillEllipse (cx - d * 0.5f + 2, cy - d * 0.5f + 2, d - 4, d - 4);
}

void Screen::readout (G& g, R r, const String& label, const String& value)
{
    if (! vis (g, r)) return;
    g.setColour (col::white); g.fillRect (r);
    text (g, label, R (r.getX() + 8, r.getY() + 3, r.getWidth() - 10, 15), fNote, col::black, juce::Justification::centredLeft);
    text (g, value, R (r.getX() + 8, r.getY() + 16, r.getWidth() - 10, r.getHeight() - 18), fRead, col::black, juce::Justification::centredLeft);
}

void Screen::panel (G& g, R r, Colour ground)
{
    if (! vis (g, r)) return;
    g.setColour (col::black); g.fillRect (r);
    g.setColour (ground); g.fillRect (r.reduced (1.0f));
}

void Screen::control (G& g, int ctl, float cellX, float y, float cellW, Colour litCol, int size, float legendH)
{
    const auto& c = eng.control (ctl);
    const float cxm = cellX + cellW * 0.5f, cym = y + 20.0f;
    const R kr (cxm - 20, y, 40, 40);
    if (! c.used) { if (vis (g, kr)) hollow (g, cxm, cym, 40, true); return; }
    const float v = eng.shown (ctl);
    lastShown[(size_t) ctl] = v;
    if (vis (g, R (cellX - 10, y, cellW + 20, 43 + legendH)))
    {
        if (c.kind == Kind::Toggle) key (g, kr, v > 0.5f ? col::green : col::putty);
        else if (c.kind == Kind::Hold) key (g, kr, col::yellow, v > 0.5f ? 8.0f : 2.0f);
        else knob (g, cxm, cym, size, v, c.kind, c.steps(), litCol);
        legend (g, cxm, y + (legendH < 20.0f ? 45.0f : 43.0f), c.label);
    }
    hits.push_back ({ kr.expanded (4, 2), c.kind == Kind::Toggle ? hToggle : c.kind == Kind::Hold ? hHold : hKnob, ctl, 0 });
}

void Screen::actKey (G& g, int a, float cellX, float y, float cellW, const String& label, Colour fill, float legendGap, bool present)
{
    const R kr (cellX + (cellW - 40) * 0.5f, y, 40, 40);
    if (! present) { if (vis (g, kr)) hollow (g, kr.getCentreX(), kr.getCentreY(), 40, false); return; }
    if (vis (g, R (cellX - 10, y, cellW + 20, 62)))
    {
        key (g, kr, fill, mouseIsDown && drag.kind == hAct && drag.a == a ? 6.0f : 2.0f);
        legend (g, kr.getCentreX(), y + 40 + legendGap, label);
    }
    hits.push_back ({ kr.expanded (3), hAct, a, 0 });
}

// ------------------------------------------------------------------------------------ the pages
void Screen::paint (G& g)
{
    g.fillAll (col::black);
    g.addTransform (xf);
    hits.clear();
    if (vis (g, R (0, 0, kW, kH)))
    {
        g.setColour (col::sea);
        g.fillRoundedRectangle (R (8, 12, 1712, 1092), 8.0f);
    }
    drawBar (g);
    if (page == pMain) drawMain (g);
    else if (page == pLibrary) drawLibrary (g);
    else drawMacros (g);
}

void Screen::drawBar (G& g)
{
    const R bar (8, 12, 1712, 52);
    if (vis (g, bar))
    {
        juce::Path p;
        p.addRoundedRectangle (bar.getX(), bar.getY(), bar.getWidth(), bar.getHeight(), 8.0f, 8.0f, true, true, false, false);
        g.setColour (col::bluegrey); g.fillPath (p);
        g.setColour (col::black); g.fillRect (R (8, 63, 1712, 1));
        const float w = std::ceil (tw (fFlag, "SF-000")) + 28.0f;
        g.fillRect (R (23, 17.5f, w, 40));
        text (g, "SF-000", R (23, 17.5f, w, 40), fFlag, col::lit);
        if (page != pMain) plate (g, 23 + w + 12, 24.5f, page == pLibrary ? "SNAPSHOT LIBRARY" : "MACRO DESIGNER", fLegend, 8, col::white, col::black);
        text (g, "AUDIO", R (1116, 17.5f, 60, 40), fLegend, col::black, juce::Justification::centredLeft);
        text (g, "RUN", R (1222, 17.5f, 40, 40), fLegend, col::black, juce::Justification::centredLeft);
        text (g, "PANIC", R (1314, 17.5f, 56, 40), fLegend, col::black, juce::Justification::centredLeft);
        key (g, R (1070, 17.5f, 40, 40), col::white);
        key (g, R (1176, 17.5f, 40, 40), eng.isRunning() ? col::green : col::putty);
        key (g, R (1268, 17.5f, 40, 40), col::red, mouseIsDown && drag.kind == hAct && drag.a == aPanic ? 6.0f : 2.0f);
        readout (g, R (1373, 17.5f, 96, 40), "TEMPO", eng.valueText (cx::kTempo));
        readout (g, R (1487, 17.5f, 72, 40), "KEY", eng.valueText (cx::kKey));
        knob (g, 1597, 37.5f, 40, eng.shown (cx::kGlide), Kind::List, 12, col::lit);
        readout (g, R (1621, 17.5f, 84, 40), "GLIDE", eng.valueText (cx::kGlide));
    }
    hits.push_back ({ R (1070, 17.5f, 40, 40), hAct, aAudio, 0 });
    hits.push_back ({ R (1176, 17.5f, 40, 40), hAct, aRun, 0 });
    hits.push_back ({ R (1268, 17.5f, 40, 40), hAct, aPanic, 0 });
    hits.push_back ({ R (1373, 17.5f, 96, 40), hRead, cx::kTempo, 0 });
    hits.push_back ({ R (1487, 17.5f, 72, 40), hRead, cx::kKey, 0 });
    hits.push_back ({ R (1573, 15.5f, 132, 44), hKnob, cx::kGlide, 0 });
    lastShown[(size_t) cx::kGlide] = eng.shown (cx::kGlide);
}

void Screen::drawMain (G& g)
{
    for (int l = 0; l < kLanes; ++l)
    {
        const int col = l < 5 ? l : l - 5, row = l < 5 ? 0 : 1;
        drawLane (g, l, R (23.0f + (float) col * 282.0f, row == 0 ? 78.0f : 430.0f, 272, 344));
    }
    drawSnapPanel (g, R (1433, 78, 272, 344));
    for (int s = 0; s < kSlots; ++s) drawInsert (g, s, R (23.0f + (float) s * 282.0f, 782, 272, 190));
    drawReturns (g, R (869, 782, 272, 190));
    drawMaster (g, R (1151, 782, 554, 190));
    drawSequencer (g, R (23, 980, 1682, 110));
}

void Screen::drawLane (G& g, int l, R r)
{
    const auto& d = lanes()[(size_t) l];
    const float x0 = r.getX() + 5, y0 = r.getY() + 5, body = y0 + 32;
    const bool active = l == ses.activeLane;
    hits.push_back ({ r, hLanePanel, l, 0 });
    panel (g, r, col::putty);
    const String num (l + 1);
    const float wTag = std::max (30.0f, std::ceil (tw (fLegend, num)) + 8.0f);
    if (vis (g, R (x0, y0, 262, 26)))
    {
        g.setColour (col::black); g.fillRect (R (x0, y0, wTag, 26));
        text (g, num, R (x0, y0, wTag, 26), fLegend, col::lit);
        const float wn = plate (g, x0 + wTag, y0, d.name, fLegend, 8, active ? col::black : col::white, active ? col::lit : col::black);
        hits.push_back ({ R (x0, y0, wTag + wn, 26), hLane, l, 0 });
        const float xr = r.getRight() - 7;
        for (int k = 0; k < kSlots; ++k) lamp (g, xr - 6 - (float) (2 - k) * 15, y0 + 13, eng.insertType (l, k) >= 0 ? col::green : col::putty);
        text (g, "INS", R (xr - 47 - 40, y0, 40, 26), fLbl, col::black, juce::Justification::centredRight);
    }
    else hits.push_back ({ R (x0, y0, wTag + std::ceil (tw (fLegend, d.name)) + 16, 26), hLane, l, 0 });
    for (int k = 0; k < kLaneKnobs; ++k)
        control (g, cx::lane (l, k), x0 + (float) (k % 4) * 66, body + (float) (k / 4) * 77, 64, k < 8 ? col::lit : k < 11 ? col::red : col::blue);
    control (g, cx::lane (l, cx::kSendA), x0, body + 231, 64, col::blue);
    control (g, cx::lane (l, cx::kSendB), x0 + 66, body + 231, 64, col::blue);
    for (int e = 0; e < 3; ++e) control (g, cx::lane (l, cx::kEqLow + e), x0 + 132 + (float) e * 44, body + 231, 42, col::blue, 30);
}

void Screen::drawSnapPanel (G& g, R r)
{
    const float x0 = r.getX() + 5, y0 = r.getY() + 5, body = y0 + 32;
    panel (g, r, col::putty);
    if (vis (g, R (x0, y0, 262, 26)))
    {
        const float w = plate (g, x0, y0, "SNAPSHOTS", fLegend, 8, col::white, col::black);
        plate (g, x0 + w + 2, y0, "PAGE 1", fNote, 6, col::white, col::black);
        const String last = "LAST " + ses.lastTakenAt;
        plate (g, r.getRight() - 5 - (std::ceil (tw (fNote, last)) + 12), y0, last, fNote, 6, col::white, col::black);
    }
    for (int k = 0; k < 16; ++k)
    {
        const float cxm = x0 + (float) (k % 4) * 66 + 32, y = body + (float) (k / 4) * 61;
        const R kr (cxm - 20, y, 40, 40);
        if (ses.knobs[(size_t) k].snaps.size() >= 2)
        {
            const float v = eng.shown (cx::kSnap + k);
            lastShown[(size_t) (cx::kSnap + k)] = v;
            if (vis (g, R (cxm - 40, y, 80, 58)))
            {
                knob (g, cxm, y + 20, 40, v, Kind::Knob, 0, col::teal);
                text (g, ses.knobs[(size_t) k].name, R (cxm - 40, y + 42, 80, 14), fLbl, col::black);
            }
            hits.push_back ({ kr.expanded (4, 2), hSnapKnob, k, 0 });
        }
        else if (vis (g, kr)) hollow (g, cxm, y + 20, 40, true);
    }
    const float ky = r.getBottom() - 62;
    const R bar (x0, ky, 130, 40);
    if (vis (g, R (x0, ky, 262, 60)))
    {
        key (g, bar, col::yellow, mouseIsDown && drag.kind == hAct && drag.a == aSnapshot ? 6.0f : 2.0f);
        text (g, "SNAPSHOT", R (x0, ky + 43, 130, 14), fLbl, col::black);
    }
    hits.push_back ({ bar, hAct, aSnapshot, 0 });
    actKey (g, aLibrary, x0 + 132, ky, 64, "LIBRARY", col::white);
    actKey (g, aMacros, x0 + 198, ky, 64, "MACROS", col::white);
}

void Screen::drawInsert (G& g, int slot, R r)
{
    const float x0 = r.getX() + 5, y0 = r.getY() + 5, body = y0 + 32;
    const int l = ses.activeLane, t = eng.insertType (l, slot);
    panel (g, r, col::putty);
    const String name = t >= 0 ? String (effects()[(size_t) t].name) : String ("EMPTY");
    const float w = plate (g, x0, y0, name, fLegend, 8, col::white, col::black);
    hits.push_back ({ R (x0, y0, w, 26), hInsHead, slot, 0 });
    for (int c = 0; c < kCells; ++c) control (g, cx::ins (l, slot, c), x0 + (float) (c % 4) * 66, body + (float) (c / 4) * 77, 64, col::lit);
}

void Screen::drawReturns (G& g, R r)
{
    const float x0 = r.getX() + 5, y0 = r.getY() + 5, body = y0 + 32;
    panel (g, r, col::putty);
    if (vis (g, R (x0, y0, 262, 26)))
    {
        plate (g, x0, y0, "RETURNS", fLegend, 8, col::white, col::black);
        const String note = juce::CharPointer_UTF8 ("A DUB \xc2\xb7 B SPACE");
        plate (g, r.getRight() - 5 - (std::ceil (tw (fNote, note)) + 12), y0, note, fNote, 6, col::white, col::black);
    }
    for (int k = 0; k < 8; ++k) control (g, cx::kRet + k, x0 + (float) (k % 4) * 66, body + (float) (k / 4) * 77, 64, col::lit);
}

void Screen::drawMaster (G& g, R r)
{
    masterRect = r;
    const float x0 = r.getX() + 5, y0 = r.getY() + 5, body = y0 + 32;
    panel (g, r, col::grey);
    plate (g, x0, y0, "MASTER", fLegend, 8, col::white, col::black);
    for (int k = 0; k < 3; ++k) control (g, cx::kMaster + k, x0 + (float) k * 66, body, 64, col::lit);
    if (! vis (g, R (x0, body + 60, 544, 100))) return;
    // FAULT, gain reduction, loudness
    const float fy = body + 77;
    g.setColour (col::black); g.fillRect (R (x0 + 6, fy + 8, 14, 26));
    g.setColour (eng.fault.load() ? col::amber : col::grey); g.fillRect (R (x0 + 8, fy + 10, 10, 22));
    text (g, "FAULT", R (x0 + 25, fy, 40, 42), fLbl, col::black, juce::Justification::centredLeft);
    readout (g, R (x0 + 66, fy, 64, 42), "GR", String (eng.gainRed.load(), 1).replaceCharacter ('-', (juce::juce_wchar) 0x2212));
    const float lu = eng.lufs.load();
    readout (g, R (x0 + 132, fy, 64, 42), "LUFS", lu < -60.0f ? String ("--") : String (lu, 1).replaceCharacter ('-', (juce::juce_wchar) 0x2212));
    // the meter: two bars on a dial plate
    const R mp (x0 + 216, body + 17, 328, 84);
    g.setColour (col::white); g.fillRect (mp);
    const float bx = mp.getX() + 26, len = 264.0f;
    auto xOf = [len] (float db) { return juce::jlimit (0.0f, len, (db + 60.0f) / 66.0f * len); };
    const float dbs[2] = { eng.peakL.load(), eng.peakR.load() };
    const bool clips[2] = { eng.clipL.load(), eng.clipR.load() };
    for (int ch = 0; ch < 2; ++ch)
    {
        const float y = mp.getY() + 8 + (float) ch * 22;
        text (g, ch == 0 ? "L" : "R", R (mp.getX() + 10, y, 10, 18), fLbl, col::black, juce::Justification::centredLeft);
        g.setColour (col::black); g.fillRect (R (bx, y, len + 4, 18));
        g.setColour (col::white); g.fillRect (R (bx + 2, y + 2, len, 14));
        g.setColour (col::black); g.fillRect (R (bx + 2, y + 2, xOf (dbs[ch]), 14));
        key (g, R (bx + len + 10, y, 18, 18), clips[ch] ? col::red : col::white);
    }
    const float sy = mp.getY() + 52, ox = bx + 2;
    g.setColour (col::black); g.fillRect (R (ox + xOf (-6), sy, xOf (0) - xOf (-6), 7));
    g.setColour (col::amber); g.fillRect (R (ox + xOf (-6) + 1, sy + 1, xOf (0) - xOf (-6) - 2, 5));
    g.setColour (col::black); g.fillRect (R (ox + xOf (0), sy, xOf (6) - xOf (0), 7));
    g.setColour (col::red); g.fillRect (R (ox + xOf (0) + 1, sy + 1, xOf (6) - xOf (0) - 2, 5));
    static const int ticks[] = { -48, -24, -12, -6, 0 };
    for (int t : ticks)
    {
        g.setColour (col::black); g.fillRect (R (ox + xOf ((float) t) - 1, sy, 2, 9));
        if (t != -6) text (g, t == 0 ? String ("0") : String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + String (-t), R (ox + xOf ((float) t) - 20, sy + 10, 40, 14), fLbl, col::black);
    }
    text (g, "CLIP", R (bx + len + 8, sy + 1, 40, 14), fLbl, col::black, juce::Justification::centredLeft);
}

void Screen::drawSequencer (G& g, R r)
{
    seqRect = r;
    panel (g, r, col::putty);
    const int l = ses.activeLane;
    const auto& d = lanes()[(size_t) l];
    const Pattern pat = ses.pattern (l);
    const int len = pat.length;
    const float x0 = r.getX() + 5, y0 = r.getY() + 5, ky = y0 + 40;

    // the lane, its length, the selected step
    const String num (l + 1);
    const float wTag = std::max (30.0f, std::ceil (tw (fLegend, num)) + 8.0f);
    g.setColour (col::black); g.fillRect (R (x0, y0, wTag, 26));
    text (g, num, R (x0, y0, wTag, 26), fLegend, col::lit);
    const float wn = plate (g, x0 + wTag, y0, d.name, fLegend, 8, col::white, col::black);
    plate (g, x0 + wTag + wn + 2, y0, String (len) + (len == 1 ? " STEP" : " STEPS"), fNote, 6, col::white, col::black);
    const bool hasSel = ses.selStep >= 0 && ses.selStep < len;
    const String st = hasSel ? "STEP " + String (ses.selStep + 1) : String ("NO STEP");
    plate (g, x0 + 356 - (std::ceil (tw (fNote, st)) + 12), y0, st, fNote, 6, col::white, col::black);

    // pattern keys, then step keys
    knob (g, x0 + 28, ky + 20, 40, (float) len / 128.0f, Kind::Knob, 0, col::lit);
    legend (g, x0 + 28, ky + 45, "LENGTH");
    hits.push_back ({ R (x0 + 4, ky - 2, 48, 44), hLen, 0, 0 });
    actKey (g, aNudgeL, x0 + 58, ky, 56, "< NUDGE", col::yellow, 5);
    actKey (g, aNudgeR, x0 + 116, ky, 56, "NUDGE >", col::yellow, 5);
    actKey (g, aRatchet, x0 + 184, ky, 56, "RATCHET", col::yellow, 5);
    actKey (g, aNote, x0 + 242, ky, 56, "NOTE", col::yellow, 5, d.pitched);
    actKey (g, aOctave, x0 + 300, ky, 56, "OCTAVE", col::yellow, 5, d.pitched);

    // the overview: eight pages of sixteen
    const float x1 = x0 + 372;
    const int playing = eng.isRunning() ? (int) (((juce::int64) std::floor (eng.songPos() / kStepBeats)) % len) : -1;
    lastStep = playing;
    for (int w = 0; w < 8; ++w)
    {
        const R pr (x1 + (float) w * 93, y0, 85, 18);
        if (vis (g, pr))
        {
            if (w == ses.page) { g.setColour (col::black); g.drawRect (pr, 2.0f); }
            for (int j = 0; j < 16; ++j)
            {
                const int i = w * 16 + j;
                const R c (pr.getX() + 3 + (float) j * 5, pr.getY() + 3, 4, 12);
                const bool in = i < len;
                g.setColour (in ? col::black : col::sea); g.fillRect (c);
                if (! in) { g.setColour (col::putty); g.fillRect (c.reduced (1)); }
                else if (i == playing) { g.setColour (col::green); g.fillRect (c.reduced (1)); }
                else if (! pat.steps[(size_t) i].on) { g.setColour (col::white); g.fillRect (c.reduced (1)); }
            }
        }
        hits.push_back ({ pr.expanded (0, 3), hPageSel, w, 0 });
    }

    // sixteen steps
    for (int j = 0; j < 16; ++j)
    {
        const int i = ses.page * 16 + j;
        const auto& x = pat.steps[(size_t) i];
        const bool in = i < len, on = in && x.on;
        const float sx = x1 + (float) j * 45 + (float) (j / 4) * 7;
        const R kr (sx, ky, 40, 40), inner = kr.reduced (2);
        hits.push_back ({ kr, hStep, i, 0 });
        if (! vis (g, R (sx - 16, y0 + 20, 72, 82))) continue;
        // a gate over one step runs on: through unlit steps as a tie, into a lit one as legato
        int k = -1;
        if (in) for (int q = i - 1; q >= 0; --q) if (pat.steps[(size_t) q].on) { k = q; break; }
        const bool tied = k >= 0 && pat.steps[(size_t) k].ratchet == 1;
        const float end = tied ? std::min ((float) k + pat.steps[(size_t) k].gate, (float) len) : 0.0f;
        float carry = 0.0f; bool bridge = false;
        if (tied)
        {
            if (on) bridge = end >= (float) i - 0.001f;
            else { carry = juce::jlimit (0.0f, 1.0f, end - (float) i); if (carry < 0.03f) carry = 0.0f; bridge = carry > 0.0f; }
        }
        lamp (g, sx + 20, y0 + 30, i == playing ? col::green : col::putty);
        if (in) { g.setColour (col::black); g.fillRect (kr); g.setColour (col::white); g.fillRect (inner); }
        else { g.setColour (col::sea); g.drawRect (kr.reduced (0.5f), 1.0f); }
        g.setColour (col::black);
        if (on)
        {
            const int rr = juce::jlimit (1, 4, x.ratchet);
            const float slot = 36.0f / (float) rr, h = std::round (x.vel * 36.0f);
            for (int q = 0; q < rr; ++q)
            {
                const float w = std::max (2.0f, std::round (std::min (x.gate, 1.0f) * slot) - (rr > 1 ? 1.0f : 0.0f));
                g.fillRect (R (inner.getX() + std::round ((float) q * slot), inner.getBottom() - h, w, h));
            }
        }
        else if (carry > 0.0f)
        {
            const float h = std::round (pat.steps[(size_t) k].vel * 36.0f);
            g.fillRect (R (inner.getX(), inner.getBottom() - h, std::round (carry * 36.0f), h));
        }
        if (bridge)
        {
            const float gap = 5.0f + (j > 0 && j % 4 == 0 ? 7.0f : 0.0f), hk = std::round (pat.steps[(size_t) k].vel * 36.0f);
            const float h = on ? std::min (6.0f, hk) : hk;
            g.fillRect (R (sx - gap - 2, inner.getBottom() - h, gap + 4, h));
        }
        if (j == 15 && i + 1 < len)
        {
            const int ks = on ? i : k;
            if (ks >= 0 && pat.steps[(size_t) ks].on && pat.steps[(size_t) ks].ratchet == 1)
            {
                const float e2 = std::min ((float) ks + pat.steps[(size_t) ks].gate, (float) len);
                const bool nextOn = pat.steps[(size_t) (i + 1)].on;
                if (nextOn ? e2 >= (float) (i + 1) - 0.001f : e2 - (float) (i + 1) > 0.03f)
                {
                    const float hk = std::round (pat.steps[(size_t) ks].vel * 36.0f), h = nextOn ? std::min (6.0f, hk) : hk;
                    g.fillRect (R (sx + 38, inner.getBottom() - h, 9, h));
                }
            }
        }
        const bool sel = in && i == ses.selStep;
        const R nb (sx, ky + 44, 40, 16);
        if (sel) { g.setColour (col::black); g.fillRect (nb); }
        text (g, d.pitched && on ? String (kNotes[juce::jlimit (0, 11, x.note)]) + String (x.octave) : String (i + 1), nb, fLbl, sel ? col::lit : col::black);
    }

    // generators
    const float x2 = x1 + 752;
    const int armed = ses.genType[(size_t) l];
    plate (g, x2, y0, "GENERATE", fLegend, 8, col::white, col::black);
    actKey (g, aGen0, x2, ky, 64, "RANDOM", armed == 0 ? col::green : col::putty, 5);
    actKey (g, aGen1, x2 + 66, ky, 64, "EUCLID", armed == 1 ? col::green : col::putty, 5);
    hollow (g, x2 + 164, ky + 20, 40, false);
    hollow (g, x2 + 230, ky + 20, 40, false);
    for (int k = 0; k < 4; ++k)
    {
        const float cxl = x2 + 286 + (float) k * 66;
        if (k == 3 && armed == 1) hollow (g, cxl + 32, ky + 20, 40, true);
        else control (g, cx::gen (l, k), cxl, ky, 64, col::lit, 40, 14);
    }
}

// -------------------------------------------------------------------------------------- library
void Screen::drawSnapCard (G& g, const Snapshot& s, int index, R r, bool selected, int hitKind)
{
    hits.push_back ({ r, hitKind, index, 0 });
    if (! vis (g, r)) return;
    g.setColour (col::black); g.fillRect (r);
    g.setColour (col::putty); g.fillRect (r.reduced (selected ? 4.0f : 1.0f));
    const float x0 = r.getX() + 6, y0 = r.getY() + 6;
    const String num (index + 1);
    const float wTag = std::max (30.0f, std::ceil (tw (fLegend, num)) + 8.0f);
    g.setColour (col::black); g.fillRect (R (x0, y0, wTag, 24));
    text (g, num, R (x0, y0, wTag, 24), fLegend, col::lit);
    const bool isBase = s.id == ses.baseId;
    const float wn = plate (g, x0 + wTag, y0, s.name, fLegend, 8, isBase ? col::black : col::white, isBase ? col::lit : col::black, 24);
    if (selected && hitKind == hCard) selNameRect = R (x0 + wTag, y0, std::max (wn, 150.0f), 24);
    text (g, s.time.formatted ("%b %d  %H:%M").toUpperCase(), R (r.getRight() - 110, y0, 104, 24), fLbl, col::black, juce::Justification::centredRight);
    // how busy each lane's pattern is
    for (int l = 0; l < kLanes; ++l)
    {
        const auto& p = s.patterns[(size_t) l];
        int on = 0;
        for (int i = 0; i < p.length; ++i) on += p.steps[(size_t) i].on ? 1 : 0;
        const float f = juce::jlimit (0.0f, 1.0f, (float) on / (float) std::max (1, std::min (p.length, 16)));
        const R b (x0 + (float) l * 23.6f, y0 + 30, 20, 16);
        g.setColour (col::black); g.fillRect (b);
        g.setColour (col::white); g.fillRect (b.reduced (1));
        g.setColour (col::black); g.fillRect (R (b.getX() + 1, b.getBottom() - 1 - std::round (f * 14), 18, std::round (f * 14)));
    }
}

void Screen::drawLibrary (G& g)
{
    const int total = (int) ses.library.size(), perPage = 6 * 12;
    libScroll = juce::jlimit (0, std::max (0, (total - 1) / 6 - 11), libScroll);
    selNameRect = {};
    for (int n = 0; n < perPage; ++n)
    {
        const int i = libScroll * 6 + n;
        if (i >= total) break;
        const auto& s = ses.library[(size_t) i];
        drawSnapCard (g, s, i, R (23.0f + (float) (n % 6) * 282, 78.0f + (float) (n / 6) * 74, 272, 66), s.id == selSnap, hCard);
    }
    const R strip (23, 980, 1682, 110);
    panel (g, strip, col::putty);
    const float x0 = strip.getX() + 5, y0 = strip.getY() + 5, ky = y0 + 40;
    plate (g, x0, y0, String (total) + (total == 1 ? " SNAPSHOT" : " SNAPSHOTS"), fLegend, 8, col::white, col::black);
    actKey (g, aMain, x0, ky, 64, "MAIN", col::white, 5);
    actKey (g, aMacros, x0 + 66, ky, 64, "MACROS", col::white, 5);
    actKey (g, aSnapshot, x0 + 160, ky, 64, "SNAPSHOT", col::yellow, 5);
    const bool has = ses.findSnap (selSnap) != nullptr;
    actKey (g, aRename, x0 + 226, ky, 64, "RENAME", col::yellow, 5, has);
    actKey (g, aDelete, x0 + 292, ky, 64, "HOLD: DELETE", col::red, 5, has);
    const String help = "TAP A SNAPSHOT TO RECALL IT. SOUND KNOBS TRAVEL OVER THE GLIDE TIME. THE BLACK NAME IS THE BASE SCENE.";
    text (g, help, R (x0 + 400, ky, 1200, 40), fLbl, col::black, juce::Justification::centredLeft);
}

// ------------------------------------------------------------------------------ macro designer
void Screen::drawMacros (G& g)
{
    selNameRect = {};
    for (int k = 0; k < 16; ++k)
    {
        const R r (23.0f + (float) (k % 4) * 282, 78.0f + (float) (k / 4) * 224, 272, 216);
        const auto& kn = ses.knobs[(size_t) k];
        const bool sel = k == selKnob;
        hits.push_back ({ r, hKnobCard, k, 0 });
        if (vis (g, r))
        {
            g.setColour (col::black); g.fillRect (r);
            g.setColour (col::putty); g.fillRect (r.reduced (sel ? 4.0f : 1.0f));
        }
        const float x0 = r.getX() + 6, y0 = r.getY() + 6;
        const String num (k + 1);
        const float wTag = std::max (30.0f, std::ceil (tw (fLegend, num)) + 8.0f);
        g.setColour (col::black); g.fillRect (R (x0, y0, wTag, 26));
        text (g, num, R (x0, y0, wTag, 26), fLegend, col::lit);
        const float wn = plate (g, x0 + wTag, y0, kn.name.isEmpty() ? String ("UNNAMED") : kn.name, fLegend, 8, col::white, col::black);
        if (sel) selNameRect = R (x0 + wTag, y0, std::max (wn, 150.0f), 26);
        const int scope = (int) ses.knobScope (k).size();
        const String mv = "MOVES " + String (scope);
        plate (g, r.getRight() - 6 - (std::ceil (tw (fNote, mv)) + 12), y0, mv, fNote, 6, col::white, col::black);
        // the knob itself, its depth, its snapshots in sweep order
        const float ky = y0 + 40;
        if (kn.snaps.size() >= 2)
        {
            const float v = eng.shown (cx::kSnap + k);
            lastShown[(size_t) (cx::kSnap + k)] = v;
            knob (g, x0 + 32, ky + 20, 40, v, Kind::Knob, 0, col::teal);
            hits.push_back ({ R (x0 + 8, ky - 2, 48, 44), hSnapKnob, k, 0 });
        }
        else hollow (g, x0 + 32, ky + 20, 40, true);
        legend (g, x0 + 32, ky + 43, "TURN");
        knob (g, x0 + 98, ky + 20, 40, kn.depth * 0.5f, Kind::Knob, 0, col::lit);
        hits.push_back ({ R (x0 + 74, ky - 2, 48, 44), hDepth, k, 0 });
        legend (g, x0 + 98, ky + 43, "DEPTH");
        for (int s = 0; s < 10; ++s)
        {
            const R sr (x0 + 134 + (float) (s % 5) * 25.6f, ky + (float) (s / 5) * 30, 23, 26);
            if (s < (int) kn.snaps.size())
            {
                const int li = ses.indexOf (kn.snaps[(size_t) s]);
                g.setColour (col::black); g.fillRect (sr);
                text (g, String (li + 1), sr, fLbl, col::lit);
                hits.push_back ({ sr, hSlot, k, s });
            }
            else { g.setColour (col::sea); g.drawRect (sr.reduced (0.5f), 1.0f); }
        }
        String order;
        for (size_t s = 0; s < kn.snaps.size(); ++s)
            if (auto* sn = ses.findSnap (kn.snaps[s])) order << (s > 0 ? "  >  " : "") << sn->name;
        g.setColour (col::black); g.setFont (fLbl);
        g.drawFittedText (order.isEmpty() ? String ("NO SNAPSHOTS. PICK THIS KNOB, THEN TAP SNAPSHOTS ON THE RIGHT.") : order,
                          juce::Rectangle<int> ((int) x0, (int) (ky + 68), 258, 30), juce::Justification::topLeft, 2, 1.0f);
        // what it moves, lane by lane
        String moves;
        int lastLane = -2;
        for (int i : ses.knobScope (k))
        {
            const int ln = i < cx::kRet ? i / cx::kPerLane : -1;
            if (ln != lastLane) { moves << (moves.isEmpty() ? "" : "   ") << (ln >= 0 ? String (lanes()[(size_t) ln].name) : String ("RETURNS")) << ": "; lastLane = ln; }
            else moves << ", ";
            moves << eng.control (i).label;
        }
        g.drawFittedText (moves, juce::Rectangle<int> ((int) x0, (int) (ky + 102), 258, 64), juce::Justification::topLeft, 4, 0.8f);
    }
    // the library, to pick from
    const int total = (int) ses.library.size();
    libScroll = juce::jlimit (0, std::max (0, (total - 1) / 2 - 11), libScroll);
    for (int n = 0; n < 24; ++n)
    {
        const int i = libScroll * 2 + n;
        if (i >= total) break;
        drawSnapCard (g, ses.library[(size_t) i], i, R (1151.0f + (float) (n % 2) * 282, 78.0f + (float) (n / 2) * 74, 272, 66), false, hPick);
    }
    const R strip (23, 980, 1682, 110);
    panel (g, strip, col::putty);
    const float x0 = strip.getX() + 5, y0 = strip.getY() + 5, ky = y0 + 40;
    plate (g, x0, y0, "KNOB " + String (selKnob + 1), fLegend, 8, col::white, col::black);
    actKey (g, aMain, x0, ky, 64, "MAIN", col::white, 5);
    actKey (g, aLibrary, x0 + 66, ky, 64, "LIBRARY", col::white, 5);
    actKey (g, aRenameKnob, x0 + 160, ky, 64, "RENAME", col::yellow, 5);
    actKey (g, aClearKnob, x0 + 226, ky, 64, "CLEAR", col::yellow, 5);
    const String help = "PICK A KNOB. TAP SNAPSHOTS ON THE RIGHT TO ADD THEM IN SWEEP ORDER (2 TO 10). TAP A NUMBERED SLOT TO TAKE IT OUT. "
                        "THE KNOB MOVES ONLY WHAT DIFFERS AMONG ITS SNAPSHOTS, AS AN OFFSET FROM THE BASE SCENE.";
    g.setColour (col::black); g.setFont (fLbl);
    g.drawFittedText (help, juce::Rectangle<int> ((int) x0 + 330, (int) ky - 4, 1300, 48), juce::Justification::centredLeft, 3, 1.0f);
}

// ---------------------------------------------------------------------------------------- input
void Screen::act (int a)
{
    const int l = ses.activeLane;
    switch (a)
    {
        case aRun:      eng.setRunning (! eng.isRunning()); break;
        case aPanic:    eng.panic(); break;
        case aAudio:    if (onAudioSetup) onAudioSetup(); break;
        case aFull:     if (onFullScreen) onFullScreen(); break;
        case aSnapshot: selSnap = ses.take(); break;
        case aLibrary:  setPage (pLibrary); break;
        case aMacros:   setPage (pMacros); break;
        case aMain:     setPage (pMain); break;
        case aNudgeL:   ses.nudgeLane (l, -1); break;
        case aNudgeR:   ses.nudgeLane (l, 1); break;
        case aGen0:     ses.pressGenerator (l, 0); break;
        case aGen1:     ses.pressGenerator (l, 1); break;
        case aRatchet: case aNote: case aOctave:
        {
            Pattern p = ses.pattern (l);
            const int i = ses.selStep;
            if (i < 0 || i >= p.length) break;
            auto& s = p.steps[(size_t) i];
            if (a == aRatchet) { if (! s.on) break; s.ratchet = s.ratchet % 4 + 1; s.gate = std::min (s.gate, 1.0f); }
            else if (! lanes()[(size_t) l].pitched) break;
            else if (a == aNote) s.note = (s.note + 1) % 12;
            else s.octave = s.octave >= 5 ? 1 : s.octave + 1;
            ses.setPattern (l, p);
            break;
        }
        case aRename:
            if (auto* s = ses.findSnap (selSnap); s != nullptr && ! selNameRect.isEmpty())
                beginEdit (selNameRect, s->name, [this, id = selSnap] (String t) { ses.rename (id, t); });
            return;
        case aRenameKnob:
            if (! selNameRect.isEmpty())
                beginEdit (selNameRect, ses.knobs[(size_t) selKnob].name, [this, k = selKnob] (String t) { ses.knobs[(size_t) k].name = t.trim().toUpperCase().substring (0, 10); ses.touch(); });
            return;
        case aClearKnob:
            ses.knobs[(size_t) selKnob].snaps.clear();
            ses.turnSnapKnob (selKnob, 0.0f);
            break;
        default: break;
    }
    repaint();
}

void Screen::stepTap (int i)
{
    // An unlit step lights and is selected; a lit step is selected first, and a second tap puts it out.
    const int l = ses.activeLane;
    Pattern p = ses.pattern (l);
    if (i >= p.length) return;
    auto& s = p.steps[(size_t) i];
    if (! s.on) { s.on = true; ses.selStep = i; ses.setPattern (l, p); }
    else if (ses.selStep != i) ses.selStep = i;
    else { s.on = false; ses.setPattern (l, p); }
}

void Screen::mouseDown (const juce::MouseEvent& e)
{
    if (editor != nullptr) endEdit (true);
    grabKeyboardFocus();
    const auto p = logical (e.position);
    const auto* h = hitAt (p);
    drag = {};
    mouseIsDown = true;
    if (h == nullptr) return;
    drag.kind = h->kind; drag.a = h->a; drag.b = h->b; drag.start = p; drag.t0 = juce::Time::getMillisecondCounter();
    switch (h->kind)
    {
        case hKnob: case hRead: drag.v0 = ses.value (h->a); break;
        case hSnapKnob: drag.v0 = ses.value (cx::kSnap + h->a); if (page == pMacros) selKnob = h->a; break;
        case hDepth:    drag.v0 = ses.knobs[(size_t) h->a].depth; selKnob = h->a; break;
        case hLen:      drag.v0 = (float) ses.pattern (ses.activeLane).length; break;
        case hToggle:   ses.userSet (h->a, ses.value (h->a) > 0.5f ? 0.0f : 1.0f); break;
        case hHold:     ses.userSet (h->a, 1.0f); break;
        case hLane:     ses.activeLane = h->a; ses.selStep = -1; ses.page = 0; ses.touch(); break;
        case hInsHead:  ses.stepInsert (ses.activeLane, h->a); break;
        case hPageSel:  ses.page = h->a; break;
        case hStep:
        {
            const Pattern pat = ses.pattern (ses.activeLane);
            if (h->a >= pat.length) { drag.kind = 0; break; }
            const auto& s = pat.steps[(size_t) h->a];
            drag.v0 = s.vel; drag.g0 = s.gate; drag.r0 = s.ratchet;
            break;
        }
        case hAct:      if (h->a != aDelete) { act (h->a); return; } break;
        case hCard:     selSnap = ses.library[(size_t) h->a].id; ses.recall (selSnap); break;
        case hKnobCard: selKnob = h->a; break;
        case hSlot:
        {
            auto& sn = ses.knobs[(size_t) h->a].snaps;
            selKnob = h->a;
            if (h->b < (int) sn.size()) { sn.erase (sn.begin() + h->b); ses.turnSnapKnob (h->a, ses.value (cx::kSnap + h->a)); }
            break;
        }
        case hPick:
        {
            auto& sn = ses.knobs[(size_t) selKnob].snaps;
            if (sn.size() < 10) { sn.push_back (ses.library[(size_t) h->a].id); ses.turnSnapKnob (selKnob, ses.value (cx::kSnap + selKnob)); }
            break;
        }
        default: break;
    }
    repaint();
}

void Screen::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = logical (e.position);
    const float dx = p.x - drag.start.x, dy = drag.start.y - p.y;
    switch (drag.kind)
    {
        case hKnob: case hRead: case hSnapKnob:
        {
            const int ctl = drag.kind == hSnapKnob ? cx::kSnap + drag.a : drag.a;
            const auto& c = eng.control (ctl);
            float v;
            if (c.kind == Kind::List && c.steps() > 1) v = drag.v0 + std::round (dy / 14.0f) / (float) (c.steps() - 1);
            else if (ctl == cx::kTempo) v = drag.v0 + dy * (e.mods.isShiftDown() ? 0.1f : 0.5f) / 140.0f;       // half a bpm a pixel
            else v = drag.v0 + dy / (e.mods.isShiftDown() ? 800.0f : 160.0f);
            ses.userSet (ctl, v);
            if (drag.kind == hSnapKnob || ctl >= cx::kTempo) repaint();
            break;
        }
        case hDepth:
            ses.knobs[(size_t) drag.a].depth = juce::jlimit (0.0f, 2.0f, drag.v0 + dy / 80.0f);
            ses.turnSnapKnob (drag.a, ses.value (cx::kSnap + drag.a));
            repaint();
            break;
        case hLen:
        {
            Pattern pat = ses.pattern (ses.activeLane);
            const int n = juce::jlimit (1, kMaxSteps, (int) std::lround (drag.v0 + dy / 3.0f));
            if (n != pat.length) { pat.length = n; ses.setPattern (ses.activeLane, pat); dirty (seqRect); }
            break;
        }
        case hStep:
        {
            if (! drag.moved && std::max (std::abs (dx), std::abs (dy)) < 5.0f) break;
            drag.moved = true;
            if (drag.axis == 0) drag.axis = std::abs (dy) >= std::abs (dx) ? 'v' : 'g';
            Pattern pat = ses.pattern (ses.activeLane);
            const int i = drag.a;
            auto& s = pat.steps[(size_t) i];
            s.on = true;
            if (drag.axis == 'v') s.vel = juce::jlimit (0.15f, 1.0f, drag.v0 + dy / 60.0f);
            else
            {
                // the gate follows the hand: a step's pitch is a step of gate; past the edge it runs on to the next lit step
                float mx = 1.0f;
                if (drag.r0 == 1)
                {
                    int nx = pat.length;
                    for (int k = i + 1; k < pat.length; ++k) if (pat.steps[(size_t) k].on) { nx = k; break; }
                    mx = (float) std::max (1, nx - i);
                }
                float gt = drag.g0 + dx / 45.0f;
                if (gt > 0.93f && gt < 1.07f) gt = 1.0f;
                s.gate = juce::jlimit (0.15f, mx, gt);
            }
            ses.selStep = i;
            ses.setPattern (ses.activeLane, pat);
            dirty (seqRect);
            break;
        }
        default: break;
    }
}

void Screen::mouseUp (const juce::MouseEvent&)
{
    mouseIsDown = false;
    if (drag.kind == hHold) ses.userSet (drag.a, 0.0f);
    if (drag.kind == hStep && ! drag.moved) stepTap (drag.a);
    drag.kind = 0;
    repaint();
}

void Screen::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const auto* h = hitAt (logical (e.position)))
    {
        if (h->kind == hKnob) ses.userSet (h->a, eng.control (h->a).def);
        else if (h->kind == hSnapKnob) ses.pressSnapKnob (h->a);       // press: its nearest snapshot becomes the base
        else if (h->kind == hDepth) { ses.knobs[(size_t) h->a].depth = 1.0f; ses.turnSnapKnob (h->a, ses.value (cx::kSnap + h->a)); }
        repaint();
    }
}

void Screen::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (page != pMain) { libScroll = std::max (0, libScroll - (w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0)); repaint(); return; }
    if (const auto* h = hitAt (logical (e.position)))
        if (h->kind == hKnob || h->kind == hSnapKnob || h->kind == hRead)
        {
            const int ctl = h->kind == hSnapKnob ? cx::kSnap + h->a : h->a;
            const auto& c = eng.control (ctl);
            const float dir = w.deltaY > 0 ? 1.0f : w.deltaY < 0 ? -1.0f : 0.0f;
            const float step = c.kind == Kind::List && c.steps() > 1 ? 1.0f / (float) (c.steps() - 1) : ctl == cx::kTempo ? 1.0f / 140.0f : 0.02f;
            ses.userSet (ctl, ses.value (ctl) + dir * step);
            repaint();
        }
}

bool Screen::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey) { act (aRun); return true; }
    if (k == juce::KeyPress::escapeKey) { if (page != pMain) setPage (pMain); else if (onFullScreen) onFullScreen(); return true; }
    if (k.getTextCharacter() == 'f' || k.getTextCharacter() == 'F') { if (onFullScreen) onFullScreen(); return true; }
    if (k == juce::KeyPress::leftKey)  { ses.page = std::max (0, ses.page - 1); repaint(); return true; }
    if (k == juce::KeyPress::rightKey) { ses.page = std::min (7, ses.page + 1); repaint(); return true; }
    if (k == juce::KeyPress::upKey)    { ses.activeLane = std::max (0, ses.activeLane - 1); ses.selStep = -1; ses.page = 0; repaint(); return true; }
    if (k == juce::KeyPress::downKey)  { ses.activeLane = std::min (kLanes - 1, ses.activeLane + 1); ses.selStep = -1; ses.page = 0; repaint(); return true; }
    return false;
}

void Screen::filesDropped (const juce::StringArray& files, int x, int y)
{
    const auto p = logical ({ (float) x, (float) y });
    for (auto& h : hits)
        if (h.kind == hLanePanel && h.r.contains (p) && files.size() > 0) { eng.loadFile (h.a, juce::File (files[0])); break; }
}

void Screen::beginEdit (R where, const String& initial, std::function<void (String)> done)
{
    editor = std::make_unique<juce::TextEditor>();
    editDone = std::move (done);
    const auto b = where.transformedBy (xf).getSmallestIntegerContainer();
    editor->setBounds (b);
    editor->setFont (fLegend.withHeight ((float) b.getHeight() * 0.8f));
    editor->setColour (juce::TextEditor::backgroundColourId, col::white);
    editor->setColour (juce::TextEditor::textColourId, col::black);
    editor->setColour (juce::TextEditor::highlightColourId, col::yellow);
    editor->setColour (juce::TextEditor::outlineColourId, col::black);
    editor->setColour (juce::TextEditor::focusedOutlineColourId, col::black);
    editor->setText (initial, false);
    editor->selectAll();
    editor->onReturnKey = [this] { endEdit (true); };
    editor->onEscapeKey = [this] { endEdit (false); };
    editor->onFocusLost = [this] { endEdit (true); };
    addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();
}

void Screen::endEdit (bool keep)
{
    if (editor == nullptr) return;
    auto ed = std::move (editor);
    auto fn = std::move (editDone);
    editDone = nullptr;
    if (keep && fn) fn (ed->getText());
    removeChildComponent (ed.get());
    repaint();
}

void Screen::timerCallback()
{
    // repaint what moved on its own: gliding knobs, the meters, the step that is playing
    for (auto& h : hits)
        if (h.kind == hKnob || h.kind == hSnapKnob || h.kind == hToggle || h.kind == hHold)
        {
            const int ctl = h.kind == hSnapKnob ? cx::kSnap + h.a : h.a;
            if (ctl >= 0 && ctl < cx::kCount && eng.shown (ctl) != lastShown[(size_t) ctl]) dirty (h.r.expanded (14, 6));
        }
    if (page == pMain)
    {
        dirty (masterRect);
        const bool run = eng.isRunning();
        if (run || wasRunning)
        {
            const int len = std::max (1, ses.pattern (ses.activeLane).length);
            const int now = run ? (int) (((juce::int64) std::floor (eng.songPos() / kStepBeats)) % len) : -1;
            if (now != lastStep || run != wasRunning) dirty (seqRect);
        }
        wasRunning = run;
    }
    if (mouseIsDown && drag.kind == hAct && drag.a == aDelete && ! drag.fired && juce::Time::getMillisecondCounter() - drag.t0 > 700)
    {
        drag.fired = true;
        ses.remove (selSnap);
        selSnap = 0;
        repaint();
    }
}
} // namespace sf0
