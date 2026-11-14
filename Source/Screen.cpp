#include "Screen.h"
#include "Recorder.h"
#include "Online.h"
#include "Midi.h"
#include "Help.h"
#include "BinaryData.h"
#include "core/Processor.h"
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
// The dark instrument (the lit skin, with D): its plates, its print, a pale display's ground, its
// keys, a faint outline, a muted mark, and the room round the case.
namespace night
{
    const Colour plate (0xff3B3C3A), plate2 (0xff303231), ink (0xffD8D3C8), paper (0xff1A1C1B), key (0xff565955), keyOff (0xff484A47),
                 faint (0xff5A605D), dim (0xff7C807A), room (0xff0D0E0F);
}
static const char* const kNotes[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

// The embedded typefaces. They are let go while JUCE is still up (releaseFonts, from the app's
// shutdown): left to the end of the process, macOS's typeface is destroyed after the JUCE objects
// it leans on, and the app aborts as it quits.
static std::map<const char*, juce::Typeface::Ptr>& faceCache()
{
    static auto* cache = new std::map<const char*, juce::Typeface::Ptr>();     // never destroyed: emptied by releaseFonts
    return *cache;
}

static juce::Font face (const char* data, int size, float px, float tracking)
{
    auto& t = faceCache()[data];
    if (t == nullptr) t = juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    return juce::Font (juce::FontOptions (t).withPointHeight (px).withKerningFactor (tracking));
}

void Screen::releaseFonts() { faceCache().clear(); }

Screen::Screen (Session& s)
    : ses (s), eng (s.engine),
      fLbl    (face (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize, 14.0f, 0.05f)),
      fLegend (face (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize, 18.0f, 0.04f)),
      fNote   (face (BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize, 14.0f, 0.05f)),
      fRead   (face (BinaryData::BarlowSemiCondensedMedium_ttf, BinaryData::BarlowSemiCondensedMedium_ttfSize, 21.0f, 0.0f)),
      fFlag   (face (BinaryData::BarlowSemiCondensedSemiBold_ttf, BinaryData::BarlowSemiCondensedSemiBold_ttfSize, 36.0f, 0.0f)),
      cfg (s.cfg), cfgOpen (s.cfgOpen)
{
    scopeOn.fill (true);
    setOpaque (true);
    setWantsKeyboardFocus (true);
    lastShown.fill (-1.0f);
    ses.onChange = [this] { cfgStale = true; repaint(); };
    startTimerHz (30);
}

Screen::~Screen() { cancelPendingUpdate(); stage = nullptr; ses.onChange = nullptr; eng.watchPockets (false); }

void Screen::setPage (Page p)
{
    endEdit (true);
    if (p != pLibrary && page == pLibrary && cueMode) ses.cue (0);
    page = p; ses.showing = (int) p;
    eng.watchPockets (p == pPockets);           // (POCKETS looks at the lanes while its page is up, whatever AMOUNT is)
    pkSig.fill (0);
    repaint();
}

void Screen::resized()
{
    if (stage != nullptr && wantThree) stage->setHome (stage->home(), false);       // (a named view is fitted to the window's new shape)
    const float s = std::min ((float) getWidth() / kW, (float) getHeight() / kH);
    xf = juce::AffineTransform::scale (s).translated (((float) getWidth() - kW * s) * 0.5f, ((float) getHeight() - kH * s) * 0.5f);
}

void Screen::dirty (R r)
{
    if (three && stage != nullptr)
    {
        // in 3D: that much of the sheet is printed again, and the parts recorded again, before the next frame
        sheetDirty.add ((r.expanded (2.0f) * stage->sheetScale()).getSmallestIntegerContainer());
        triggerAsyncUpdate();
        return;
    }
    juce::Component::repaint (r.transformedBy (xf).getSmallestIntegerContainer().expanded (2));
}
void Screen::repaint() { if (three) { sheetAll = true; triggerAsyncUpdate(); } else juce::Component::repaint(); }
void Screen::repaint (juce::Rectangle<int> r) { if (three) { sheetAll = true; triggerAsyncUpdate(); } else juce::Component::repaint (r); }
void Screen::hit (R r, int kind, int a, int b) { if (collect) hits.push_back ({ r.translated (shift, 0.0f), kind, a, b }); }
// Lit, a thing's picture reaches past its own edge (the shade it casts, its glow), so what is
// near a part-paint is drawn again with it: the part then matches what a whole paint would give.
// (Recording for the 3D stage, everything is "in what is being painted": the record is of the whole screen.)
bool Screen::vis (G& g, R r) const { return recording || g.clipRegionIntersects (r.expanded (lit ? 12.0f : 0.0f).getSmallestIntegerContainer()); }
bool Screen::seen (G& g, R r) const { return recording || g.clipRegionIntersects (r.getSmallestIntegerContainer()); }

const Screen::Hit* Screen::hitAt (juce::Point<float> p) const
{
    for (size_t i = hits.size(); i-- > 0;) if (hits[i].r.contains (p)) return &hits[i];
    return nullptr;
}

// ---------------------------------------------------------------------------------- drawing kit
void Screen::text (G& g, const String& s, R r, const juce::Font& f, Colour c, juce::Justification j)
{
    textRaw (g, s, r, f, c == col::black ? tone (c) : c, j);
}

void Screen::textRaw (G& g, const String& s, R r, const juce::Font& f, Colour c, juce::Justification j)
{
    if (recording) return;          // (the record is of solid things; print is the sheet's)
    g.setColour (c); g.setFont (f);
    g.drawText (s, r, j, false);
}

// The dark instrument turns the light one's roles over: print and marks are light, a pale
// display's ground is dark, plates are dark. Lights, lit keys and the yellow keys are as they were.
Colour Screen::tone (Colour c) const
{
    if (! night()) return c;
    return c == col::black ? night::ink : c == col::white ? night::paper : c == col::putty ? night::plate : c == col::grey ? night::plate2 : c == col::sea ? night::faint : c;
}

Colour Screen::glass (Colour c) const { return night() && c == col::white ? night::paper : c; }

Colour Screen::cap (Colour c) const
{
    if (! night()) return c;
    return c == col::white ? night::key : c == col::putty ? night::keyOff : c == col::black ? night::ink : c;
}

Colour Screen::inkOn (Colour surface, Colour asked) const
{
    if (! night()) return asked;
    if (surface.getPerceivedBrightness() > 0.40f) return col::black;
    return asked == col::black ? night::ink : asked;
}

float Screen::tw (const juce::Font& f, const String& s) const
{
    // Measuring text shapes it, which is slow: each string on each of the five fonts is measured once.
    const char which = &f == &fLbl ? 'a' : &f == &fLegend ? 'b' : &f == &fNote ? 'c' : &f == &fRead ? 'd' : &f == &fFlag ? 'e' : 0;
    if (which == 0) return juce::GlyphArrangement::getStringWidth (f, s);
    std::string key (1, which);
    key += s.toStdString();
    const auto it = widths.find (key);
    if (it != widths.end()) return it->second;
    if (widths.size() > 4000) widths.clear();
    const float w = juce::GlyphArrangement::getStringWidth (f, s);
    widths[key] = w;
    return w;
}

float Screen::plate (G& g, float x, float y, const String& s, const juce::Font& f, float padX, Colour bg, Colour fg, float h)
{
    const float w = std::ceil (tw (f, s)) + padX * 2.0f;
    const R r (x, y, w, h);
    if (vis (g, r)) { window (g, r, bg); textRaw (g, s, r, f, inkOn (glass (bg), fg)); }
    return w;
}

void Screen::legend (G& g, float cx, float y, const String& label)
{
    // A long legend of two words takes two lines, where there is a second line's room under it on
    // its plate (oneLine: there is not, as on the strips at the foot of a page: it stays on one).
    const int sp = label.indexOfChar (' ');
    const bool two = ! oneLine && label.length() > 7 && sp > 0;
    if (two)
    {
        text (g, label.substring (0, sp), R (cx - 60, y, 120, 14), fLbl, col::black);
        text (g, label.substring (sp + 1), R (cx - 60, y + 14, 120, 14), fLbl, col::black);
    }
    else text (g, label, R (cx - 60, y, 120, 14), fLbl, col::black);
    if (recording)
    {
        // (for the checks: where its letters are, to be held against the plate it is printed on)
        // (one that has slid out of sight, past the edge of what shows, is not printed anywhere)
        const float w = two ? std::max (tw (fLbl, label.substring (0, sp)), tw (fLbl, label.substring (sp + 1))) : tw (fLbl, label);
        const R at = R (cx - w * 0.5f, y + 1.0f, w, two ? 26.0f : 12.0f).translated (shift, 0.0f);
        if (skin.clipNow().has (at.getCentreX(), at.getY() + 3.0f)) legendRects.push_back (at);
    }
}

void Screen::knob (G& g, float cx, float cy, int size, float v, Kind kind, int steps, Colour litCol, float target)
{
    // LEARN: a knob moved since it was pressed is lit yellow, and so is the knob being taught
    if (knobCtl >= 0 && ses.learning >= 0 && (knobCtl == cx::kSnap + ses.learning || ses.learnMoved (knobCtl))) { litCol = col::yellow; knobLight = col::yellow; }
    const float body = (float) size * 0.5f, ro = size >= 40 ? 17.0f : 13.0f, ri = size >= 40 ? 10.0f : 7.0f;
    const float pi = juce::MathConstants<float>::pi, seg = pi / 8.0f;
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
    if (lit && recording)
    {
        skin.recordKnob (cx, cy, size, knobCtl, (int) kind, steps, knobCtl >= 0 ? knobLight : litCol, a, b, target >= 0.0f ? juce::jlimit (0.0f, 0.9999f, target) : -1.0f);
        return;
    }
    if (lit)
    {
        // The knob as modelled: a dark cap in a ring of sixteen lights. The lights say where it stands.
        skin.knob (g, cx, cy, size);
        if (b - a > 0.0005f) skin.ringLit (g, cx, cy, size, a, b, litCol);
        if (kind == Kind::Bipolar) { g.setColour (tone (col::black)); g.fillRect (cx - 1.0f, cy - body - 4.5f, 2.0f, 3.5f); }       // its middle, printed on the plate
        if (target >= 0.0f && ! printing)       // (in 3D the light it is going to is lit instead)
        {
            const float an = pi + juce::jlimit (0.0f, 1.0f, target) * 2.0f * pi, sx = std::sin (an), cs = -std::cos (an);
            const float r0 = size >= 40 ? 12.0f : 8.3f;
            g.setColour (col::lit);
            g.drawLine (cx + sx * r0, cy + cs * r0, cx + sx * (body - 0.5f), cy + cs * (body - 0.5f), 3.0f);
        }
        return;
    }
    g.setColour (col::black);
    g.fillEllipse (cx - body, cy - body, body * 2, body * 2);
    juce::Path ring;
    ring.addPieSegment (cx - ro, cy - ro, ro * 2, ro * 2, 0.0f, pi * 2.0f, ri / ro);
    g.setColour (col::unlit);
    g.fillPath (ring);
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
    if (target >= 0.0f)
    {
        // where it is going: a mark across the ring
        const float an = pi + juce::jlimit (0.0f, 1.0f, target) * 2.0f * pi, sx = std::sin (an), cs = -std::cos (an);
        g.setColour (col::lit);
        g.drawLine (cx + sx * (ri - 2.0f), cy + cs * (ri - 2.0f), cx + sx * body, cy + cs * body, 3.0f);
    }
}

void Screen::hollow (G& g, float cx, float cy, float d, bool round)
{
    if (lit) { skin.hollow (g, cx, cy, d, round); return; }
    g.setColour (col::sea);
    const R r (cx - d * 0.5f + 0.5f, cy - d * 0.5f + 0.5f, d - 1.0f, d - 1.0f);
    if (round) g.drawEllipse (r, 1.0f); else g.drawRect (r, 1.0f);
}

void Screen::key (G& g, R r, Colour fill, float border, bool on)
{
    // Where it stands. Under the hand (the press began on it, on this page) it is all the way in,
    // whatever kind of key it is; latched on, it sits low and stays; otherwise it is up.
    const bool held = border > 2.5f || (mouseIsDown && drag.kind != 0 && drag.page == (int) page && r.translated (shift, 0.0f).contains (drag.start));
    if (lit) skin.key (g, r, cap (fill), held ? Parts::kHeld : on ? Parts::kOn : Parts::kUp);
    else flatKey (g, r, fill, border);
}

// An action's key is down while the hand is on it, and for a moment after the action is done from
// somewhere else (a key on the keyboard): a press is seen whoever made it.
bool Screen::pressed (int a) const
{
    return (mouseIsDown && drag.kind == hAct && drag.a == a) || (tapAct == a && juce::Time::getMillisecondCounter() - tapAt < kTapMs);
}

void Screen::flatKey (G& g, R r, Colour fill, float border)
{
    g.setColour (tone (col::black)); g.fillRect (r);
    g.setColour (fill == col::white ? tone (fill) : fill); g.fillRect (r.reduced (border));
}

void Screen::window (G& g, R r, Colour glass)
{
    if (lit) skin.window (g, r.reduced (1.0f), this->glass (glass));
    else { g.setColour (glass); g.fillRect (r); }
}

void Screen::tag (G& g, R r, const String& s)
{
    if (! vis (g, r)) return;
    window (g, r, col::black);
    text (g, s, r, fLegend, col::lit);
}

// A panel's name. Lit, it is a dark display with light letters, as on the model's module.
float Screen::title (G& g, float x, float y, const String& s, bool inFront)
{
    // (in front it is lit up, as the active lane's name is)
    const bool darkPlate = lit != inFront;
    return plate (g, x, y, s, fLegend, 8, darkPlate ? col::black : lit ? col::lit : col::white, darkPlate ? col::lit : col::black);
}

// Where one of the pointer's stops stands on the screen: the most of it that shows (the rows go
// round while CONFIG is open, and a panel can be drawn twice, part of it at each edge).
void Screen::stopAt (int stop, R drawn)
{
    if (! collect || stop < 0 || stop >= Session::kPlaces) return;
    auto& at = stopRects[(size_t) stop];
    if (drawn.getWidth() * drawn.getHeight() > at.getWidth() * at.getHeight()) at = drawn;
}

Screen::R Screen::frontBox() const
{
    R box = stopRects[(size_t) Session::placeOf (ses.stopNow())];
    if (cfgOpen && ! cfgRect.isEmpty()) box = box.isEmpty() ? cfgRect : box.getUnion (cfgRect);
    return box.isEmpty() ? box : box.expanded (10.0f).getIntersection (R (0, 0, kW, kH));
}

void Screen::card (G& g, R r, bool selected)
{
    if (lit)
    {
        skin.panel (g, r, tone (col::putty));
        if (selected) { g.setColour (tone (col::black)); g.drawRoundedRectangle (r.reduced (3.0f), 5.0f, 3.0f); }
        return;
    }
    g.setColour (col::black); g.fillRect (r);
    g.setColour (col::putty); g.fillRect (r.reduced (selected ? 4.0f : 1.0f));
}

void Screen::lamp (G& g, float cx, float cy, Colour fill, float d)
{
    if (lit) { skin.lamp (g, cx, cy, fill == col::putty ? col::green : fill, d, fill != col::putty); return; }
    g.setColour (col::black); g.fillEllipse (cx - d * 0.5f, cy - d * 0.5f, d, d);
    g.setColour (fill); g.fillEllipse (cx - d * 0.5f + 2, cy - d * 0.5f + 2, d - 4, d - 4);
}

void Screen::readout (G& g, R r, const String& label, const String& value)
{
    if (! vis (g, r)) return;
    window (g, r, col::white);
    text (g, label, R (r.getX() + 8, r.getY() + 3, r.getWidth() - 10, 15), fNote, col::black, juce::Justification::centredLeft);
    text (g, value, R (r.getX() + 8, r.getY() + 16, r.getWidth() - 10, r.getHeight() - 18), fRead, col::black, juce::Justification::centredLeft);
}

void Screen::panel (G& g, R r, Colour ground)
{
    if (! vis (g, r)) return;
    if (lit) { skin.panel (g, r, tone (ground)); return; }
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
    if (vis (g, R (cellX - 10, y, cellW + 20, 43 + legendH)))
    {
        if (seen (g, lit ? kr : R (cellX - 10, y, cellW + 20, 43 + legendH))) lastShown[(size_t) ctl] = v;        // (only what was drawn: a change that was not is still owed a repaint)
        if (c.kind == Kind::Toggle) key (g, kr, v > 0.5f ? col::green : col::putty, 2.0f, v > 0.5f);
        else if (c.kind == Kind::Hold) key (g, kr, col::yellow, v > 0.5f ? 8.0f : 2.0f);
        else
        {
            knobCtl = ctl; knobLight = litCol;
            if (eng.isGliding (ctl)) knob (g, cxm, cym, size, v, c.kind, c.steps(), col::green, eng.target (ctl));
            else knob (g, cxm, cym, size, v, c.kind, c.steps(), litCol);
            knobCtl = -1;
        }
        if (info)
        {
            if (c.kind == Kind::Toggle) number (g, ctl, kr, inkOn (cap (v > 0.5f ? col::green : col::putty), col::black));
            else if (c.kind == Kind::Hold) number (g, ctl, kr, col::black);
            else number (g, ctl, R (cxm - 14, cym - 10 - (lit && ! three ? (size >= 40 ? 3.0f : 2.0f) : 0.0f), 28, 20), col::lit);       // (lit, the cap's top stands a little up the screen)
        }
        legend (g, cxm, y + (legendH < 20.0f ? 45.0f : 43.0f), c.label);
    }
    hit (kr.expanded (4, 2), c.kind == Kind::Toggle ? hToggle : c.kind == Kind::Hold ? hHold : hKnob, ctl, 0);
}

// CONFIG, MUTE and SOLO: three small lettered keys at the right of a header
void Screen::headKeys (G& g, float right, float y, int kind, int lane, int slot, int muteCtl, int soloCtl)
{
    const bool open = cfgOpen && cfg.kind == kind && (kind == 2 ? cfg.slot == slot : cfg.lane == lane && (kind == 0 || cfg.slot == slot));
    const Colour fills[3] = { open ? col::black : col::white, eng.shown (muteCtl) > 0.5f ? col::amber : col::putty, eng.shown (soloCtl) > 0.5f ? col::green : col::putty };
    static const char* const letters[3] = { "C", "M", "S" };
    for (int k = 0; k < 3; ++k)
    {
        const R kr (right - 72.0f + (float) k * 25.0f, y, 22, 22);
        if (vis (g, kr))
        {
            String letter (letters[k]);
            if (int ch = 0, num = 0; info && k > 0 && ccx::addressOf (k == 1 ? muteCtl : soloCtl, ch, num)) letter = String (num);      // INFO: its number
            key (g, kr, fills[k], 2.0f, k == 0 ? open : eng.shown (k == 1 ? muteCtl : soloCtl) > 0.5f);
            textRaw (g, letter, kr, fLbl, inkOn (cap (fills[k]), k == 0 && open ? col::lit : col::black));
            if (k == 1 && seen (g, kr)) lastShown[(size_t) muteCtl] = eng.shown (muteCtl);
            if (k == 2 && seen (g, kr)) lastShown[(size_t) soloCtl] = eng.shown (soloCtl);
        }
        if (k == 0) hit (kr.expanded (1), hConfig, kind, lane * 8 + slot);
        else hit (kr.expanded (1), hToggle, k == 1 ? muteCtl : soloCtl);
    }
}

void Screen::actKey (G& g, int a, float cellX, float y, float cellW, const String& label, Colour fill, float legendGap, bool present, bool on)
{
    const R kr (cellX + (cellW - 40) * 0.5f, y, 40, 40);
    if (! present) { if (vis (g, kr)) hollow (g, kr.getCentreX(), kr.getCentreY(), 40, false); return; }
    if (vis (g, R (cellX - 10, y, cellW + 20, 62)))
    {
        key (g, kr, fill, pressed (a) ? 6.0f : 2.0f, on);
        legend (g, kr.getCentreX(), y + 40 + legendGap, label);
    }
    hit (kr.expanded (3), hAct, a, 0);
}

// ----------------------------------------------------------------------------------------- INFO
void Screen::number (G& g, int ctl, R on, Colour ink)
{
    int ch = 0, num = 0;
    if (ccx::addressOf (ctl, ch, num)) textRaw (g, String (num), on, fLbl, ink);
}

float Screen::chan (G& g, float x, float y, int channel)
{
    if (! info) return 0.0f;
    return plate (g, x, y, "CH " + String (channel), fNote, 6, col::teal, col::lit);
}

juce::StringArray Screen::wrap (const String& s, const juce::Font& f, float width) const
{
    juce::StringArray lines, words;
    words.addTokens (s, " ", "");
    String line;
    for (auto& w : words)
    {
        const String longer = line.isEmpty() ? w : line + " " + w;
        if (line.isNotEmpty() && juce::GlyphArrangement::getStringWidth (f, longer) > width) { lines.add (line); line = w; }
        else line = longer;
    }
    if (line.isNotEmpty()) lines.add (line);
    return lines;
}

// A control: where it is, its name, what it does.
bool Screen::describe (int ctl, String& where, String& label, const char*& what) const
{
    what = nullptr;
    if (ctl < 0 || ctl >= cx::kCount) return false;
    label = eng.control (ctl).label;
    if (ctl >= cx::kSnap) return false;
    if (ctl == cx::kGlide)
    {
        label = "GLIDE";
        what = "HOW LONG A TURNED KNOB OR A RECALL TAKES TO GET THERE. SOUND KNOBS TRAVEL OVER THIS TIME, SETTING OFF ON THE NEXT BEAT WHILE THE CLOCK RUNS. OFF: AT ONCE.";
        return true;
    }
    if (ctl == cx::kTempo) { label = "TEMPO"; what = "60 TO 200 BPM: DRAG IT. FOLLOWING AN OUTSIDE CLOCK, THE TEMPO IS THAT CLOCK'S."; return true; }
    if (ctl == cx::kKey) { label = "KEY"; what = "THE KEY EVERY PITCHED LANE FOLLOWS: DRAG IT. PARTS ARE WRITTEN IN C."; return true; }
    if (cx::isMot (ctl))
    {
        static const char* const ids[cx::kMotKnobs] = { "wave", "speed", "knob", "amount" };
        where = "SNAPSHOT KNOBS, MOTION " + String ((ctl - cx::kMot) / cx::kMotKnobs + 1); what = help::param ("KNOBMOTION", ids[(ctl - cx::kMot) % cx::kMotKnobs]);
        return true;
    }
    if (ctl >= cx::kMaster)
    {
        static const char* const ids[3] = { "tape", "comp", "limit" };
        where = "MASTER"; what = help::param ("MASTER", ids[ctl - cx::kMaster]);
        return true;
    }
    if (ctl >= cx::kRetMS) { where = ctl - cx::kRetMS < 2 ? "RETURN A, THE DUB DELAY" : "RETURN B, THE SPACE"; what = help::param ("LANE", (ctl - cx::kRetMS) % 2 == 0 ? "mute" : "solo"); return true; }
    if (ctl >= cx::kPkG)
    {
        static const char* const ids[kPkGlobals] = { "amount", "clear", "speed", "returns" };
        where = "MASTER, POCKETS"; what = help::param ("POCKETS", ids[ctl - cx::kPkG]);
        return true;
    }
    if (ctl >= cx::kBus)
    {
        where = "MASTER, THE DRUM BUS"; what = help::param (drumBus().module, drumBus().cells[ctl - cx::kBus].param);
        return true;
    }
    if (ctl >= cx::kRet)
    {
        const int j = ctl - cx::kRet;
        const auto& fx = j < 4 ? returnA() : returnB ((j - 4) / 4);
        const auto& cell = fx.cells[j < 4 ? j : (j - 4) % 4];
        where = j < 4 ? "RETURN A, THE DUB DELAY" : "RETURN B, THE SPACE";
        if (cell.param != nullptr) what = help::param (fx.module, cell.param);
        return true;
    }
    const int l = ctl / cx::kPerLane, j = ctl % cx::kPerLane;
    const auto& d = lanes()[(size_t) l];
    where = String (l + 1) + " " + d.name;
    if (j < kLaneKnobs) what = help::param (d.module, d.knobs[j].param);
    else if (j == cx::kSendA) what = help::param ("LANE", "send_a");
    else if (j == cx::kSendB) what = help::param ("LANE", "send_b");
    else if (j == cx::kEqLow) what = help::param ("LANE", "eq_low");
    else if (j == cx::kEqMid) what = help::param ("LANE", "eq_mid");
    else if (j == cx::kEqHigh) what = help::param ("LANE", "eq_high");
    else if (j < cx::kGen)
    {
        const int slot = (j - cx::kIns) / kCells, cell = (j - cx::kIns) % kCells, type = eng.insertType (l, slot);
        if (type < 0) return false;
        const auto& fx = effects()[(size_t) type];
        where << ", " << fx.name;
        if (fx.cells[cell].param != nullptr) what = help::param (fx.module, fx.cells[cell].param);
    }
    else if (j < cx::kMute) { static const char* const ids[4] = { "hits", "rotate", "accents", "seed" }; what = help::param ("LANE", ids[j - cx::kGen]); }
    else if (j == cx::kMute) { label = "MUTE"; what = "SILENCES THE LANE."; }
    else if (j == cx::kSolo) { label = "SOLO"; what = "ONLY THE SOLOED LANES PLAY."; }
    else if (j < cx::kSeqMute)
    {
        const int slot = (j - cx::kInsMS) / 2, type = eng.insertType (l, slot);
        if (type >= 0) where << ", " << effects()[(size_t) type].name;
        label = (j - cx::kInsMS) % 2 == 0 ? "MUTE" : "SOLO";
        what = (j - cx::kInsMS) % 2 == 0 ? "PASSES THE EFFECT BY." : "PLAYS ONLY WHAT THE EFFECT ADDS.";
    }
    else if (j == cx::kSeqMute) { label = "SEQUENCER MUTE"; what = help::param ("LANE", "seq_mute"); }
    else
    {
        static const char* const ids[kPkLane] = { "pk_home", "pk_width", "pk_hold", "pk_yield" };
        where << ", ITS POCKET"; what = help::param ("POCKETS", ids[j - cx::kPk]);
    }
    return true;
}

// Where MIDI finds a control, and what its values mean.
String Screen::midiOf (int ctl) const
{
    int ch = 0, num = 0;
    if (! ccx::addressOf (ctl, ch, num)) return "NOT ON MIDI.";
    const auto& c = eng.control (ctl);
    String s = "MIDI: CHANNEL " + String (ch) + ", CC " + String (num) + (ch == e16::kFxChannel ? " (THE ACTIVE LANE'S)." : ".");
    if (c.kind == Kind::Bipolar) s << " 64 IS THE MIDDLE.";
    else if (c.kind == Kind::Toggle || c.kind == Kind::Hold) s << " 64 AND OVER IS ON.";
    else if (c.kind == Kind::List && c.steps() >= 2 && c.steps() <= 16)
    {
        s << " VALUES:";
        for (int i = 0; i < c.steps(); ++i) s << (i > 0 ? ", " : " ") << juce::roundToInt ((float) i * 127.0f / (float) (c.steps() - 1)) << " " << c.choices[i];
        s << ".";
    }
    return s;
}

Screen::Tip Screen::tipFor (const Hit& h) const
{
    Tip t;
    String body, midi;
    bool below = false;                                 // a knob has its name under it: the box goes under that
    const int l = ses.activeLane;
    switch (h.kind)
    {
        case hKnob: case hToggle: case hHold: case hRead:
        {
            String where, label; const char* what = nullptr;
            if (! describe (h.a, where, label, what)) return t;
            t.title = where.isEmpty() ? label : where + ": " + label;
            if (what != nullptr) body = what;
            midi = midiOf (h.a);
            below = h.kind != hRead && h.a != cx::kGlide;
            break;
        }
        case hSnapKnob:
        {
            const auto& kn = ses.knobs[(size_t) h.a];
            int held = 0;
            for (auto id : kn.snaps) if (ses.findSnap (id) != nullptr) ++held;
            t.title = "SNAPSHOT KNOB " + String (h.a + 1) + (kn.name.isNotEmpty() ? ": " + kn.name : String());
            body = "TURN IT TO PUSH THE RIG FROM WHERE IT IS TOWARD THE " + String (held) + " SNAPSHOTS IT HOLDS, IN THE ORDER THE MACRO DESIGNER SHOWS. DOUBLE-CLICK: RECALL THE NEAREST AS THE NEW BASE SCENE.";
            if (held < 2 && ! kn.links.empty())
                body = "TIED TO " + String ((int) kn.links.size()) + (kn.links.size() == 1 ? " KNOB" : " KNOBS") + " (LEARN, IN THE MACRO DESIGNER, WHICH LISTS THEM). TURN IT AND EACH GOES ITS OWN WAY, FROM WHERE IT STANDS. "
                       "DOUBLE-CLICK: PAST HALF WAY, WHAT IT HAS PUSHED IS KEPT AND THE KNOB GOES HOME; SHORT OF IT, THEY GO BACK.";
            else if (! kn.links.empty()) body += " IT IS ALSO TIED TO " + String ((int) kn.links.size()) + (kn.links.size() == 1 ? " KNOB." : " KNOBS.");
            midi = "MIDI: CHANNEL 16. CC " + String (e16::kKnobCc + h.a) + " TURNS IT. CC " + String (e16::kPressCc + h.a) + " (64 AND OVER) RECALLS ITS NEAREST. CC " + String (ccx::kRecall + h.a)
                 + " RECALLS THE ONE ITS VALUE NAMES: 1 THE FIRST IT HOLDS, " + String (std::max (2, held)) + " THE LAST. A VALUE SAID AGAIN DOES NOTHING; 0 IS NONE.";
            below = true;
            break;
        }
        case hSetup:
        {
            // a CONFIG control: what it is, in the box it is in
            if (h.a >= (int) cfgParams.size()) return t;
            const auto& sp = cfgParams[(size_t) h.a];
            const char* module = cfg.kind == 0 ? lanes()[(size_t) cfg.lane].module
                               : cfg.kind == 1 ? (eng.insertType (cfg.lane, cfg.slot) >= 0 ? effects()[(size_t) eng.insertType (cfg.lane, cfg.slot)].module : nullptr)
                               : cfg.kind == 3 ? drumBus().module
                               : (cfg.slot == 0 ? returnA() : returnB (eng.spaceModel())).module;
            t.title = (sp.group.isNotEmpty() && sp.group != "SOUND" && sp.group != "EFFECT" ? sp.group + ": " : String()) + sp.name;
            if (const char* what = module != nullptr ? help::param (module, sp.id.toRawUTF8()) : nullptr) body = what;
            body << (body.isEmpty() ? "" : " ") << (sp.kind == Kind::Toggle ? "CLICK TO SWITCH IT." : sp.kind == Kind::Hold ? "HOLD IT DOWN." : "DRAG UP AND DOWN (SHIFT: FINELY). DOUBLE-CLICK: BACK TO WHERE IT STARTS.");
            midi = sp.kind == Kind::Hold ? String ("KEPT IN SNAPSHOTS. A KEY THAT IS HELD: NOT ON THE E16.")
                 : h.a < e16::kCfgPer * e16::kCfgPages ? "KEPT IN SNAPSHOTS. ON THE E16 WHILE CONFIG IS OPEN: PAGE CFG " + String (h.a / e16::kCfgPer + 1) + ", KNOB " + String (h.a % e16::kCfgPer + 1)
                                                             + " (CHANNEL " + String ((int) e16::kFxChannel) + ", CC " + String (e16::cfgCc (h.a)) + ")."
                                                       : String ("KEPT IN SNAPSHOTS. NOT ON THE E16: ITS THREE CONFIG PAGES HOLD THE FIRST 36.");
            below = true;
            break;
        }
        case hScope:
            t.title = "WHAT RECALL TAKES: " + (h.a < kLanes ? String (h.a + 1) + " " + lanes()[(size_t) h.a].name : h.a == kLanes ? String ("THE RETURNS") : h.a == kLanes + 1 ? String ("THE DRUM BUS") : String ("THE KEY"));
            body = "LIT: THIS PART COMES FROM THE SNAPSHOT WHEN RECALL IS PRESSED. OUT: IT STAYS AS IT IS NOW. CLICK TO PUT IT OUT OR BACK. AFTER A RECALL, AND FOR ANOTHER SNAPSHOT, ALL ARE LIT AGAIN.";
            break;
        case hPocket:
            t.title = String (h.a + 1) + " " + lanes()[(size_t) h.a].name + ": ITS POCKET";
            body = "THE YELLOW SHAPE IS THE PART OF THE SPECTRUM THAT IS THIS LANE'S OWN: AS TALL AS ITS HOLD. DRAG IT LEFT AND RIGHT TO MOVE IT (HOME), UP AND DOWN TO WIDEN IT (WIDTH); DOUBLE-CLICK: BACK TO WHERE IT STARTS. "
                   "THE PALE BARS ARE WHAT THE LANE IS PLAYING, BAND BY BAND, 31 HZ TO 16 KHZ; THE RED AT A BAR'S TOP IS WHAT POCKETS IS TAKING FROM IT NOW.";
            below = true;
            break;
        case hLane:
            t.title = String (h.a + 1) + " " + lanes()[(size_t) h.a].name;
            body = String (help::lane (h.a)) + " CLICK THE NAME TO MAKE IT THE ACTIVE LANE: THE INSERTS AND THE SEQUENCER STRIP SHOW IT.";
            midi = "MIDI: NOTES ON CHANNEL " + String (h.a + 1) + " PLAY IT, FROM ANY PORT. ITS KNOBS AND ITS INSERTS ARE ON THAT CHANNEL TOO.";
            break;
        case hConfig:
            t.title = "CONFIG";
            body = "OPENS THE PANEL OF WHAT IS SET ONCE: THE MODEL AND THE REST OF ITS SETTINGS. C AGAIN, X OR ESC CLOSES IT.";
            break;
        case hInsHead:
        {
            const int type = eng.insertType (l, h.a);
            t.title = "INSERT " + String (h.a + 1) + " OF " + String (l + 1) + " " + lanes()[(size_t) l].name + ": " + (type >= 0 ? effects()[(size_t) type].name : "EMPTY");
            body = (type >= 0 ? String (help::effect (effects()[(size_t) type].name)) + " " : String()) + "CLICK THE NAME TO STEP THE SLOT THROUGH DELAY, REVERB, CONVOLUTION, DISTORTION, COMPRESSOR, RADIO AND EMPTY.";
            midi = "MIDI: CHANNEL " + String (l + 1) + ", CC " + String (ccx::kIns + h.a * kCells) + " TO " + String (ccx::kIns + h.a * kCells + kCells - 1) + ": ITS EIGHT PLACES, WHATEVER EFFECT IS IN IT.";
            break;
        }
        case hStep:
            t.title = "STEP " + String (h.a + 1);
            body = "CLICK: LIGHT AND SELECT IT; LIT AND SELECTED, PUT IT OUT. DRAG UP OR DOWN: VELOCITY. DRAG LEFT OR RIGHT: GATE (PAST THE STEP'S EDGE IT RUNS ON, AND TIES INTO THE NEXT LIT STEP).";
            midi = "ON THE E16'S SEQ PAGE THE CHOSEN STEP IS SIX KNOBS: STEP GOES THROUGH THE PATTERN (PRESSED: LIT, OR OUT), AND VEL, GATE, RATCH, NOTE AND OCT ARE THAT STEP'S. CHANNEL " + String ((int) e16::kFxChannel) + ", CC " + String ((int) e16::kStepCc) + " TO " + String ((int) e16::kStepCc + 5) + ".";
            break;
        case hPageSel:
            t.title = "STEPS " + String (h.a * 16 + 1) + " TO " + String (h.a * 16 + 16);
            body = "A PAGE OF THE PATTERN. CLICK TO SEE ITS 16 STEPS (THE LEFT AND RIGHT ARROW KEYS TOO).";
            break;
        case hLen:
            t.title = "LENGTH";
            body = "THE PATTERN'S LENGTH, 1 TO 128 STEPS.";
            midi = "MIDI: CHANNEL " + String ((int) e16::kFxChannel) + ", CC " + String ((int) e16::kLenCc) + " (THE ACTIVE LANE'S). THE VALUE AND ONE IS THE STEPS.";
            below = true;
            break;
        case hAct:
        {
            struct A { int a; const char* title; const char* body; };
            static const A acts[] = {
                { aRun, "RUN", "STARTS AND STOPS THE CLOCK (THE SPACE BAR TOO)." },
                { aPanic, "PANIC", "SILENCES EVERY VOICE AND TAIL." },
                { aAudio, "SETUP", "WHAT IS SET ONCE, BEFORE THE SET: THE INTERFACE, ITS OUTPUTS AND BUFFER, MIDI AND THE CLOCK, FULL SCREEN, AND THE COLOUR AND BRIGHTNESS OF THE GLOW UNDER THE CASE. (THE KEY IS GREEN AND READS PHONES WHILE PHONES IS ON.)" },
                { aRec, "REC", "PRESS TO START A TAKE; HOLD TO END IT. THE SET, EVERY LANE AND THE TWO RETURNS ARE RECORDED." },
                { aSnapshot, "SNAPSHOT", "KEEPS EVERYTHING ON THE MAIN SCREEN AS A NEW SNAPSHOT, WITH THE BARS IT WAS HEARD OVER." },
                { aLibrary, "LIBRARY", "THE SNAPSHOT LIBRARY: TAP TO RECALL, RENAME, HEAR ONE IN THE HEADPHONES." },
                { aMacros, "MACROS", "THE MACRO DESIGNER: WHICH SNAPSHOTS EACH SNAPSHOT KNOB HOLDS, IN SWEEP ORDER, AND LEARN, WHICH TIES A KNOB STRAIGHT TO OTHER KNOBS. A KNOB'S SNAPSHOTS ARE ALSO WHAT RECALL (CC 80 TO 95 ON CHANNEL 16) PICKS FROM." },
                { aMain, "MAIN", "BACK TO THE MAIN SCREEN." },
                { aNudgeL, "NUDGE", "MOVES THE WHOLE PATTERN ONE STEP EARLIER." },
                { aNudgeR, "NUDGE", "MOVES THE WHOLE PATTERN ONE STEP LATER." },
                { aRatchet, "RATCHET", "THE SELECTED STEP PLAYS ONE TO FOUR HITS." },
                { aNote, "NOTE", "THE SELECTED STEP'S NOTE." },
                { aOctave, "OCTAVE", "THE SELECTED STEP'S OCTAVE." },
                { aGen0, "RANDOM", "WRITES A NEW PATTERN AT EVERY PRESS (SEED GOES TO A NEW PLACE). WHILE IT IS LIT, HITS, ROTATE, ACCENTS AND SEED REWRITE IT. A RHYTHM CHANGED BY HAND IS YOURS: THE KEY GOES OUT, AND THE KNOBS LET GO." },
                { aGen1, "EUCLID", "WRITES A PATTERN WITH ITS HITS SPREAD EVENLY. WHILE IT IS LIT, HITS, ROTATE AND ACCENTS REWRITE IT; PRESSED AGAIN, OR ONCE THE RHYTHM IS CHANGED BY HAND, IT LETS GO." },
                { aRename, "RENAME", "NAMES THE CHOSEN SNAPSHOT." },
                { aDelete, "DELETE", "HOLD TO DELETE THE CHOSEN SNAPSHOT." },
                { aRenameKnob, "RENAME", "NAMES THIS SNAPSHOT KNOB." },
                { aClearKnob, "CLEAR", "TAKES EVERY SNAPSHOT OFF THIS KNOB, AND UNTIES IT FROM EVERY KNOB." },
                { aKnobPick, "STEPS", "THIS KNOB HOLDS A STACK: IT STEPS THROUGH ITS SNAPSHOTS AND DOES NOT SWEEP. A TURN COMES TO THE NEXT AND PLAYS IT IN THE HEADPHONES; A PRESS RECALLS THE ONE IT IS ON, OVER THE GLIDE TIME. LIT AMBER HERE AND ON THE MAIN SCREEN. DEPTH AND MOTION ARE NOT FOR IT." },
                { aLearn, "LEARN", "TIES THIS KNOB STRAIGHT TO OTHER KNOBS. IT GOES TO ITS FULL TURN AND THE MAIN SCREEN COMES UP: TURN THE KNOBS IT SHOULD MOVE (HERE OR ON A CONTROLLER) TO WHERE THE FULL TURN TAKES THEM, AND KEEP. LEARN AGAIN ADDS TO WHAT IT HAS; A KNOB TURNED BACK TO WHERE IT WAS IS LET GO." },
                { aLearnKeep, "KEEP", "TIES THE KNOBS MOVED SINCE LEARN TO THE KNOB BEING TAUGHT. NOTHING CHANGES IN THE SOUND: THE KNOB IS AT ITS FULL TURN, AND TURNED BACK THEY GO BACK TO WHERE THEY WERE." },
                { aLearnDrop, "DROP", "PUTS BACK THE KNOBS MOVED SINCE LEARN AND TIES NOTHING." },
                { aCfgClose, "X", "CLOSES CONFIG." },
                { aClipReset, "CLIP LAMPS", "RED: THE OUTPUT REACHED FULL SCALE. CLICK TO CLEAR." },
                { aCue, "CUE", "WHILE IT IS LIT, A TAPPED SNAPSHOT'S BARS ARE HEARD IN THE HEADPHONES, IN TIME WITH THE CLOCK, AND THE SET IS LEFT ALONE." },
                { aRecallCue, "RECALL", "TAKES THE SNAPSHOT IN THE HEADPHONES (OR THE ONE CHOSEN) TO THE SET." },
                { aCueSet, "IN SET", "LIT: UNDER THE LANES A NARROWED CUE PLAYS, THE REST OF THE SET AS IT IS NOW. THE HEADPHONES HAVE WHAT THE SET WOULD BE AFTER THE RECALL. OUT: THE SNAPSHOT'S LANES ALONE." },
                { aNotes, "NOTES", "WHETHER A RECALL BRINGS A SNAPSHOT'S NOTES. LIT: EACH LANE'S SEQUENCER IS GIVEN WHAT THE LANE WAS PLAYING WHEN THE SNAPSHOT WAS TAKEN (ITS OWN STEPS, AND THE NOTES THAT WERE COMING IN FROM OUTSIDE, AS STEPS) AND PLAYS IT: THE SNAPSHOT SOUNDS AS IT DID WITH NOTHING PLAYING INTO THE RIG. OUT: A RECALL CHANGES THE SOUND AND LEAVES EVERY SEQUENCER AS IT IS, FOR WHEN A SEQUENCER OUTSIDE IS PLAYING THE LANES. N ON THE KEYBOARD TOO. EVERY RECALL GOES BY IT: A TAP IN THE LIBRARY, A SNAPSHOT KNOB'S PRESS, A SEQUENCER'S RECALL." },
                { aOnline, "ONLINE", "EACH SNAPSHOT GOES TO THE LIBRARY ON THE WEB AS IT IS TAKEN. GREEN: IT HOLDS THEM ALL. YELLOW: SENDING. AMBER: WAITING. RED: TURNED AWAY." },
                { aOnlineSetup, "SET UP", "THE WEB LIBRARY'S ADDRESS AND ITS KEY." },
                { aFetch, "LOAD", "BRINGS WHAT IS IN THE WEB LIBRARY'S TRAY: ITS STACKS, THE MACROS ON ITS KNOBS (ONTO KNOBS THAT HOLD NOTHING), A SONG AS THE ARRANGEMENT (IF THERE IS NONE), AND THE SNAPSHOTS THIS LIBRARY LACKS. IT TAKES NOTHING AWAY." },
                { aStack, "STACK", "THE STACKS LOAD BROUGHT, ONE AFTER ANOTHER, THEN EVERY SNAPSHOT AGAIN." },
                { aArrange, "ARRANGE", "THE ARRANGEMENT: SNAPSHOTS IN A LINE, EACH FOR SO MANY BARS, EACH REACHED BY A MORPH FROM THE ONE BEFORE. PLAYED FROM THE CLOCK, AND PRINTED TO FILES FOR A RELEASE. A ON THE KEYBOARD TOO. LIT GREEN WHILE IT PLAYS." },
                { aArrPlay, "PLAY", "PLAYS THE ARRANGEMENT FROM ITS FIRST PART. FROM STOPPED: THAT PART IS RECALLED AT ONCE AND THE CLOCK STARTS FROM THE TOP. WITH THE CLOCK RUNNING: IT COMES IN ON THE NEXT BAR LINE. EACH PART AFTER IS RECALLED SO AS TO SET OFF ON ITS OWN BAR LINE; AT THE END THE CLOCK STOPS. PRESSED WHILE IT PLAYS: STOP." },
                { aArrHere, "FROM HERE", "PLAYS IT FROM THE PART THAT IS CHOSEN." },
                { aArrEarlier, "EARLIER", "MOVES THE CHOSEN PART ONE PLACE EARLIER." },
                { aArrLater, "LATER", "MOVES THE CHOSEN PART ONE PLACE LATER." },
                { aArrRemove, "REMOVE", "TAKES THE CHOSEN PART OUT OF THE ARRANGEMENT. ITS SNAPSHOT STAYS IN THE LIBRARY." },
                { aArrClear, "HOLD: CLEAR", "HELD: TAKES EVERY PART OUT." },
                { aArrPrint, "PRINT", "RENDERS THE ARRANGEMENT TO FILES, FASTER THAN IT PLAYS AND THE SAME EVERY TIME: THE TRACK (24-BIT, BAR 1 ON ITS FIRST SAMPLE, WHAT RINGS ON AFTER THE LAST BAR KEPT) AND EVERY LANE AND RETURN BESIDE IT, IN A FOLDER OF ITS OWN WHERE TAKES GO. IT IS MADE ON A RIG OF ITS OWN FROM THE SESSION AS IT STANDS, SO THIS ONE IS NOT TOUCHED; THE SOUND STOPS UNTIL IT IS DONE. EACH PART BRINGS ITS OWN NOTES; THE SNAPSHOT KNOBS STAND WHERE THEY ARE AND MOTION RUNS. PRESSED WHILE IT PRINTS: STOP." },
                { aPockets, "POCKETS", "THE MASTER'S PAGE FOR WHO HAS WHICH PART OF THE SPECTRUM: EVERY LANE'S POCKET ON A MAP OF THE MIX, WHAT EACH IS PLAYING AND WHAT IS BEING TAKEN FROM IT. P ON THE KEYBOARD TOO; ON THE E16, OPEN TWICE WITH THE MASTER IN FRONT (ITS CONFIG PAGES THEN HOLD EVERY LANE'S HOME, WIDTH, YIELD AND HOLD). THE NUMBER BESIDE IT IS AMOUNT." },
                { aPkAuto, "AUTO", "PLACES THE POCKETS FROM WHAT IS PLAYING. EVERY LANE HEARD IN THE LAST SECONDS IS GIVEN A HOME AND A WIDTH: THE LANES WITH MOST HOLD CHOOSE FIRST, EACH TAKING THE STRETCH THAT HAS MOST OF ITS OWN SOUND AND LEAST OF WHAT THOSE BEFORE IT TOOK. HOLD AND YIELD STAY AS THEY ARE. THE POCKETS TRAVEL THERE OVER THE GLIDE TIME." },
                { aPkReset, "RESET", "EVERY LANE'S POCKET BACK TO WHERE THE RIG STARTS IT: HOME, WIDTH, HOLD AND YIELD. AMOUNT, CLEAR, SPEED AND RETURNS STAY." },
                { aPhones, "PHONES", "FOR HEADPHONES ON THE ONE PAIR OF OUTPUTS. WHILE A CUE PLAYS, THE SET IS IN THE LEFT EAR AND THE CUE IN THE RIGHT; WITH NONE PLAYING, THE SET IS AS ALWAYS. H ON THE KEYBOARD TOO. IT IS OFF EACH TIME SF-000 OPENS, SO IT IS NEVER LEFT ON FOR A PA." },
            };
            for (const auto& a : acts) if (a.a == h.a) { t.title = a.title; body = a.body; }
            if (t.title.isEmpty()) return t;
            below = true;
            break;
        }
        default: return t;
    }
    const float W = 340.0f, inner = W - 22.0f;
    if (body.isNotEmpty()) t.body = wrap (body, fLbl, inner);
    if (midi.isNotEmpty()) t.midi = wrap (midi, fLbl, inner);
    const float H = 12.0f + 22.0f + (float) t.body.size() * 16.0f + (t.midi.isEmpty() ? 0.0f : 5.0f + (float) t.midi.size() * 16.0f) + 8.0f;
    float y = h.r.getBottom() + (below ? 34.0f : 8.0f);
    if (y + H > kH - 16.0f) y = h.r.getY() - H - 8.0f;
    t.box = R (juce::jlimit (14.0f, kW - W - 14.0f, h.r.getCentreX() - W * 0.5f), std::max (14.0f, y), W, H);
    t.any = true;
    return t;
}

// The mouse rests somewhere: what is there, and its box.
void Screen::point (juce::Point<float> p)
{
    Hit found; Tip now;
    if (info && ! mouseIsDown)
        for (size_t i = hits.size(); i-- > 0;)
        {
            if (! hits[i].r.contains (p)) continue;
            now = tipFor (hits[i]);
            if (now.any) { found = hits[i]; break; }
            if (hits[i].kind == hBlock) break;                  // (CONFIG covers what is under it)
        }
    if (now.any == hovering && (! now.any || (found.kind == hover.kind && found.a == hover.a && found.b == hover.b && found.r == hover.r))) return;
    if (hovering) dirty (tipRect);
    hovering = now.any; hover = found; tip = now; tipRect = now.any ? now.box.expanded (2.0f) : R();
    if (hovering) dirty (tipRect);
}

void Screen::mouseMove (const juce::MouseEvent& e)
{
    pressAt = hoverAt = e.position;
    if (! info) return;
    const auto at = where (e);
    // the list of keys steps aside: to the right while the pointer is where it stands on the left
    // (on the 3D stage it is a tag at the foot of the window, so the window's own points are what count there)
    const auto kb = keysBox();
    const R left (16.0f, (three ? (float) getHeight() : kH) - 16.0f - kb.getHeight(), kb.getWidth(), kb.getHeight());
    if (const bool right = left.expanded (12.0f).contains (three ? e.position : at); right != keysRight) { keysRight = right; repaint(); }
    point (at);
}

// ---- the keyboard's keys, listed while INFO is on
namespace
{
const char* const kKeys[][2] = {
    { "SPACE", "RUN AND STOP" },
    { "I", "INFO: WHAT A CONTROL IS, AND THIS LIST" },
    { "F", "FULL SCREEN" },
    { "ESC", "BACK OUT: OF LOAD'S LIST, CONFIG, THE LIBRARY, POCKETS, FULL SCREEN" },
    { "H", "PHONES" },
    { "N", "NOTES: WHETHER A RECALL BRINGS A SNAPSHOT'S NOTES" },
    { "UP  DOWN", "THE ACTIVE LANE" },
    { "TAB", "THE NEXT MODULE TO THE FRONT (SHIFT: THE ONE BEFORE)" },
    { "C", "CONFIG FOR WHAT IS IN FRONT, AND SHUT" },
    { "P", "POCKETS: WHO HAS WHICH PART OF THE SPECTRUM" },
    { "RETURN", "LEARN: KEEP WHAT THE KNOB HAS BEEN SHOWN" },
    { "LEFT  RIGHT", "THE SEQUENCER'S PAGE OF STEPS" },
    { "3", "THE 3D STAGE, OR THE LIT SKIN" },
    { "V", "3D: THE NEXT VIEW" },
    { "K", "3D: UNLOCK THE VIEW, AND LOCK IT WHERE IT IS PUT" },
    { "M", "3D: THE EYE FOLLOWS THE CONTROLLER, OR STAYS HOME" },
    { "D", "THE DARK INSTRUMENT" },
    { "L", "THE LIT SKIN, OR THE FLAT DRAWING" },
};
constexpr int kKeyCount = (int) (sizeof (kKeys) / sizeof (kKeys[0]));
}

juce::Rectangle<float> Screen::keysBox() const
{
    const float w = 500.0f, h = 40.0f + 17.0f * (float) kKeyCount + 8.0f;
    return { keysRight ? kW - 16.0f - w : 16.0f, kH - 16.0f - h, w, h };
}

void Screen::drawKeys (G& g, juce::Rectangle<float> b, bool onStage)
{
    if (! onStage && ! g.clipRegionIntersects (b.getSmallestIntegerContainer())) return;
    g.setColour (Colour (0xf4181918)); g.fillRoundedRectangle (b, 5.0f);
    g.setColour (col::yellow); g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.0f);
    auto line = [&] (const String& t, R r, const juce::Font& f, Colour c)
    {
        g.setColour (c); g.setFont (f);
        g.drawText (t, r, juce::Justification::centredLeft, false);
    };
    float y = b.getY() + 8.0f;
    line ("KEYS", R (b.getX() + 11, y, b.getWidth() - 22, 20), fLegend, col::lit);
    y += 26.0f;
    for (int i = 0; i < kKeyCount; ++i)
    {
        line (kKeys[i][0], R (b.getX() + 11, y, 92, 16), fLbl, col::yellow);
        line (kKeys[i][1], R (b.getX() + 108, y, b.getWidth() - 119, 16), fLbl, col::putty);
        y += 17.0f;
    }
}
void Screen::mouseExit (const juce::MouseEvent&) { if (hovering) { hovering = false; dirty (tipRect); tipRect = {}; } }

void Screen::drawTip (G& g)
{
    const R b = tip.box;
    if (! g.clipRegionIntersects (tipRect.getSmallestIntegerContainer())) return;
    g.setColour (Colour (0xf4181918)); g.fillRoundedRectangle (b, 5.0f);
    g.setColour (col::yellow); g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.0f);
    float y = b.getY() + 8.0f;
    textRaw (g, tip.title, R (b.getX() + 11, y, b.getWidth() - 22, 20), fLegend, col::lit, juce::Justification::centredLeft);
    y += 24.0f;
    for (auto& s : tip.body) { textRaw (g, s, R (b.getX() + 11, y, b.getWidth() - 22, 16), fLbl, col::putty, juce::Justification::centredLeft); y += 16.0f; }
    if (! tip.midi.isEmpty()) y += 5.0f;
    for (auto& s : tip.midi) { textRaw (g, s, R (b.getX() + 11, y, b.getWidth() - 22, 16), fLbl, col::yellow, juce::Justification::centredLeft); y += 16.0f; }
}

// ------------------------------------------------------------------------------------ the pages
void Screen::paint (G& g)
{
    if (three) { g.fillAll (juce::Colours::black); return; }       // (the stage covers it)
    collect = hits.empty() || g.getClipBounds().contains (getLocalBounds());
    g.fillAll (night() ? night::room : col::black);
    g.addTransform (xf);
    drawAll (g);
}

void Screen::drawAll (G& g)
{
    if (collect) { hits.clear(); for (auto& r : stopRects) r = {}; }
    if (lit) { skin.begin (g); if (vis (g, R (0, 0, kW, kH))) skin.body (g, kW, kH); }
    else if (vis (g, R (0, 0, kW, kH)))
    {
        g.setColour (col::sea);
        g.fillRoundedRectangle (R (8, 12, 1712, 1092), 8.0f);
    }
    drawBar (g);
    if (page == pMain) drawMain (g);
    else if (page == pLibrary) drawLibrary (g);
    else if (page == pPockets) drawPockets (g);
    else if (page == pArrange) drawArrange (g);
    else drawMacros (g);
    keysDrawn = false;
    if (info && ! three && ! recording && ! printing) { drawKeys (g, keysBox(), false); keysDrawn = true; }      // (in 3D it is a tag at the foot of the window: see the timer)
    if (info && hovering && ! mouseIsDown && ! three) drawTip (g);      // (in 3D the box floats by the pointer: see the timer)
}

void Screen::drawBar (G& g)
{
    const R bar (8, 12, 1712, 52);
    if (vis (g, bar))
    {
        const float w = std::ceil (tw (fFlag, "SF-000")) + 28.0f;
        if (lit) skin.badge (g, R (23, 17.5f, w, 40), "SF-000", fFlag);       // (the bar is the case itself, and the name stands on it)
        else
        {
            juce::Path p;
            p.addRoundedRectangle (bar.getX(), bar.getY(), bar.getWidth(), bar.getHeight(), 8.0f, 8.0f, true, true, false, false);
            g.setColour (col::bluegrey); g.fillPath (p);
            g.setColour (col::black); g.fillRect (R (8, 63, 1712, 1));
            g.fillRect (R (23, 17.5f, w, 40));
            text (g, "SF-000", R (23, 17.5f, w, 40), fFlag, col::lit);
        }
        if (page != pMain) title (g, 23 + w + 12, 24.5f, page == pLibrary ? "SNAPSHOT LIBRARY" : page == pPockets ? "POCKETS" : page == pArrange ? "ARRANGEMENT" : "MACRO DESIGNER");
        else if (ses.learning >= 0)
        {
            // LEARN: which knob is being taught, how many knobs it has been shown, and the two ways out
            title (g, 23 + w + 12, 24.5f, learnTitle(), true);
            const float kx = learnKeysX();
            key (g, R (kx, 17.5f, 40, 40), col::yellow, pressed (aLearnKeep) ? 6.0f : 2.0f);
            text (g, "KEEP", R (kx + 46, 17.5f, 44, 40), fLegend, col::black, juce::Justification::centredLeft);
            key (g, R (kx + 96, 17.5f, 40, 40), col::white, pressed (aLearnDrop) ? 6.0f : 2.0f);
            text (g, "DROP", R (kx + 142, 17.5f, 44, 40), fLegend, col::black, juce::Justification::centredLeft);
            const int n = ses.learnMoves();
            const float tx = kx + 196, tw2 = std::max (60.0f, 932.0f - tx);
            text (g, "SET THE KNOBS AS ITS FULL TURN SHOULD LEAVE THEM.", R (tx, 20, tw2, 17), fLbl, col::black, juce::Justification::centredLeft);
            text (g, n == 0 ? String ("NONE MOVED YET.") : String (n) + " MOVED (LIT YELLOW).", R (tx, 38, tw2, 17), fLbl, col::black, juce::Justification::centredLeft);
        }
        else if (info)
        {
            text (g, "INFO (I): THE NUMBER ON A CONTROL IS ITS MIDI CC. A LANE'S NUMBER IS ITS CHANNEL, AND ITS INSERTS ARE ON IT TOO.", R (23 + w + 16, 20, 760, 17), fLbl, col::black, juce::Justification::centredLeft);
            text (g, "THE RETURNS, THE MASTER, GLIDE AND THE SNAPSHOT KNOBS ARE ON CHANNEL 16. REST THE MOUSE ON A CONTROL FOR WHAT IT DOES.", R (23 + w + 16, 38, 760, 17), fLbl, col::black, juce::Justification::centredLeft);
        }
        // REC: press to start a take, hold to end it. The time it has run stands in for its name.
        {
            const bool on = recorder != nullptr && recorder->recording();
            key (g, R (950, 17.5f, 40, 40), on ? (recorder->lostSeconds() > 0.0 ? col::amber : col::red) : col::putty,
                 pressed (aRec) ? 6.0f : 2.0f, on);
            String t ("REC");
            if (on) { const auto s = (juce::int64) recorder->seconds(); t = String (s / 3600) + ":" + String ((s / 60) % 60).paddedLeft ('0', 2) + ":" + String (s % 60).paddedLeft ('0', 2); }
            text (g, t, R (996, 17.5f, 70, 40), fLegend, col::black, juce::Justification::centredLeft);
            if (page == pMain && recNote.isNotEmpty() && juce::Time::getMillisecondCounter() - recNoteAt < 8000)
                plate (g, 940.0f - (std::ceil (tw (fNote, recNote)) + 12.0f), 24.5f, recNote, fNote, 6, col::white, col::black);
        }
        // (PHONES lit: this output is split while a cue plays, and the key says so wherever one is)
        text (g, eng.phones() ? "PHONES" : "SETUP", R (1116, 17.5f, 60, 40), fLegend, col::black, juce::Justification::centredLeft);
        text (g, "RUN", R (1222, 17.5f, 40, 40), fLegend, col::black, juce::Justification::centredLeft);
        text (g, "PANIC", R (1314, 17.5f, 56, 40), fLegend, col::black, juce::Justification::centredLeft);
        key (g, R (1070, 17.5f, 40, 40), eng.phones() ? col::green : col::white, pressed (aAudio) ? 6.0f : 2.0f);
        key (g, R (1176, 17.5f, 40, 40), eng.isRunning() ? col::green : col::putty, pressed (aRun) ? 6.0f : 2.0f, eng.isRunning());      // (RUN latches: down while it runs)
        key (g, R (1268, 17.5f, 40, 40), col::red, pressed (aPanic) ? 6.0f : 2.0f);
        // following an outside clock, the tempo is that clock's and says so
        readout (g, R (1373, 17.5f, 96, 40), eng.following() ? "CLOCK IN" : "TEMPO", eng.valueText (cx::kTempo));
        barSig = barNow();
        readout (g, R (1487, 17.5f, 72, 40), "KEY", eng.valueText (cx::kKey));
        knobCtl = cx::kGlide; knobLight = col::lit;
        knob (g, 1597, 37.5f, 40, eng.shown (cx::kGlide), Kind::List, 12, col::lit);
        knobCtl = -1;
        if (info) number (g, cx::kGlide, R (1597 - 14, 37.5f - 10 - (lit ? 3.0f : 0.0f), 28, 20), col::lit);
        readout (g, R (1621, 17.5f, 84, 40), "GLIDE", eng.valueText (cx::kGlide));
        if (seen (g, bar)) lastShown[(size_t) cx::kGlide] = eng.shown (cx::kGlide);
    }
    if (! collect) return;
    if (page == pMain && ses.learning >= 0)
    {
        hit (R (learnKeysX(), 17.5f, 40, 40).expanded (3), hAct, aLearnKeep, 0);
        hit (R (learnKeysX() + 96, 17.5f, 40, 40).expanded (3), hAct, aLearnDrop, 0);
    }
    hit (R (950, 17.5f, 40, 40), hAct, aRec, 0);
    hit (R (1070, 17.5f, 40, 40), hAct, aAudio, 0);
    hit (R (1176, 17.5f, 40, 40), hAct, aRun, 0);
    hit (R (1268, 17.5f, 40, 40), hAct, aPanic, 0);
    hit (R (1373, 17.5f, 96, 40), hRead, cx::kTempo, 0);
    hit (R (1487, 17.5f, 72, 40), hRead, cx::kKey, 0);
    hit (R (1573, 15.5f, 132, 44), hKnob, cx::kGlide, 0);
}

float Screen::slideGoal() const
{
    // the column of the panel CONFIG is open on goes to the third column, the one beside CONFIG
    if (! cfgOpen || page != pMain) return 0.0f;
    const int col = cfg.kind == 0 ? (cfg.lane < 5 ? cfg.lane : cfg.lane - 5) : cfg.kind == 1 ? cfg.slot : cfg.kind == 3 ? 4 : 3;
    return (float) (2 - col) * 282.0f;
}

void Screen::drawMain (G& g)
{
    // CONFIG slides in from the left over two columns; the top three rows move to bring its panel beside it
    const float ease = cfgT * cfgT * (3.0f - 2.0f * cfgT);
    const R cfgR (23.0f - 564.0f * (1.0f - ease), 78, 554, 894);
    const float left = cfgT > 0.001f ? std::max (9.0f, std::floor (cfgR.getRight())) : 9.0f;
    // (lit: the rows pass out of sight short of the case's rolled edge)
    rowsClip = lit ? R (std::max (left, 17.0f), 64, 1712.0f - std::max (left, 17.0f), 912) : R (left, 64, 1710.0f - left, 912);
    if (collect) masterRect = {};
    {
        const juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (rowsClip.getSmallestIntegerContainer());
        skin.setClip (&rowsClip);
        const size_t first = hits.size();
        for (int copy = -1; copy <= 1; ++copy)
        {
            const float dx = slideX + (float) copy * 1692.0f;       // six columns of 282
            if (23.0f + dx >= rowsClip.getRight() || 1705.0f + dx <= rowsClip.getX()) continue;
            const juce::Graphics::ScopedSaveState moved (g);
            g.addTransform (juce::AffineTransform::translation (dx, 0.0f));
            shift = dx; skin.setShift (dx);
            for (int l = 0; l < kLanes; ++l)
            {
                const int col = l < 5 ? l : l - 5, row = l < 5 ? 0 : 1;
                drawLane (g, l, R (23.0f + (float) col * 282.0f, row == 0 ? 78.0f : 430.0f, 272, 344));
            }
            drawSnapPanel (g, R (1433, 78, 272, 344));
            for (int sl = 0; sl < kSlots; ++sl) drawInsert (g, sl, R (23.0f + (float) sl * 282.0f, 782, 272, 190));
            drawReturns (g, R (869, 782, 272, 190));
            drawMaster (g, R (1151, 782, 554, 190));
            shift = 0.0f; skin.setShift (0.0f);
        }
        skin.setClip (nullptr);
        // what is off the screen or behind CONFIG cannot be touched
        if (collect) hits.erase (std::remove_if (hits.begin() + (long) first, hits.end(), [this] (const Hit& h) { return ! h.r.intersects (rowsClip); }), hits.end());
    }
    drawSequencer (g, R (23, 980, 1682, 110));
    // the pointer (a controller's wheel): a frame round the module it is on
    if (ses.pointing)
        if (const R pr = stopRects[(size_t) Session::placeOf (ses.pointAt)]; ! pr.isEmpty() && vis (g, pr))
        {
            // (read from the whole rig's distance: the eye is home while it moves)
            g.setColour (col::black);  g.drawRect (pr.expanded (1.0f), 12.0f);
            g.setColour (col::yellow); g.drawRect (pr.reduced (1.0f), 8.0f);
        }
    if (cfgT > 0.001f && (collect || vis (g, cfgR)))
    {
        const juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (juce::Rectangle<int> (9, 64, 1710, 912));
        const R cfgClip (9, 64, 1710, 912);
        skin.setClip (&cfgClip);
        drawConfig (g, cfgR);
        skin.setClip (nullptr);
    }
}

void Screen::openConfig (Engine::Target t)
{
    ses.openConfig (t);
    if (cfgOpen) { cfgStale = true; cfgScroll = 0; cfgName = {}; cfgNote = {}; loadOpen = false; }
    cfgSeen = cfg; cfgOpenSeen = cfgOpen;
}

void Screen::drawConfig (G& g, R r)
{
    cfgRect = r;
    hit (r, hBlock);
    panel (g, r, col::grey);
    const float x0 = r.getX() + 5, y0 = r.getY() + 5;
    auto* p = eng.processorFor (cfg);
    String tag, name;
    if (cfg.kind == 0) { tag = String (cfg.lane + 1); name = lanes()[(size_t) cfg.lane].name; }
    else if (cfg.kind == 1)
    {
        tag = String (cfg.lane + 1) + "." + String (cfg.slot + 1);
        const int t = eng.insertType (cfg.lane, cfg.slot);
        name = t >= 0 ? String (effects()[(size_t) t].name) : String ("EMPTY");
    }
    else if (cfg.kind == 3) { tag = "M"; name = "DRUM BUS"; }
    else { tag = cfg.slot == 0 ? "A" : "B"; name = cfg.slot == 0 ? "DUB" : "SPACE"; }
    const float wTag = std::max (30.0f, std::ceil (tw (fLegend, tag)) + 10.0f);
    this->tag (g, R (x0, y0, wTag, 26), tag);
    float x = x0 + wTag;
    x += plate (g, x, y0, name, fLegend, 8, col::white, col::black) + 2;
    plate (g, x, y0, "CONFIG", fNote, 6, col::white, col::black);
    const R cr (r.getRight() - 27, y0 + 2, 22, 22);
    key (g, cr, col::white);
    text (g, "X", cr, fLbl, col::black);
    hit (cr.expanded (2), hAct, aCfgClose, 0);

    if (cfgStale || ++cfgAge > 12) { cfgParams = p != nullptr ? eng.setupParams (cfg) : std::vector<Engine::SetupParam>(); cfgStale = false; cfgAge = 0; }
    // the parameter last touched, with its value
    for (auto& sp : cfgParams)
        if (sp.id == cfgName)
        {
            const String v = (sp.group.startsWith ("MOTION") ? sp.group + " " : String()) + sp.name + "   " + sp.text.toUpperCase();
            plate (g, cr.getX() - 8 - (std::ceil (tw (fNote, v)) + 12), y0, v, fNote, 6, col::white, col::black);
            break;
        }

    float y = y0 + 34;
    cfgDisplay = {};
    if (p == nullptr)
    {
        text (g, "THIS SLOT IS EMPTY. CLICK ITS NAME ON THE MAIN SCREEN TO PUT AN EFFECT IN IT.", R (x0, y, 544, 40), fLbl, col::black, juce::Justification::centredLeft);
        return;
    }
    auto& m = p->mod();
    if (const int dh = juce::jmin (230, m.displayHeight()); dh > 0)
    {
        // the module's own display: waveform, in and out points, cue points. It takes the mouse.
        cfgDisplay = R (x0, y, 544, (float) dh);
        if (vis (g, cfgDisplay))
        {
            if (recording) skin.window (g, cfgDisplay, juce::Colours::black);       // (in 3D it is a display like the others: what it shows is light)
            g.setColour (juce::Colours::black); g.fillRect (cfgDisplay);
            if (! recording)
            {
                const juce::Graphics::ScopedSaveState state (g);
                g.reduceClipRegion (cfgDisplay.getSmallestIntegerContainer());
                m.paint (g, cfgDisplay);
            }
            if (lit && ! three)
            {
                // set into the plate: the cut shades its upper and left edges
                g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.6f), 0.0f, cfgDisplay.getY(), juce::Colours::transparentBlack, 0.0f, cfgDisplay.getY() + 7.0f, false));
                g.fillRect (cfgDisplay.withHeight (7.0f));
                g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.4f), cfgDisplay.getX(), 0.0f, juce::Colours::transparentBlack, cfgDisplay.getX() + 5.0f, 0.0f, false));
                g.fillRect (cfgDisplay.withWidth (5.0f));
                g.setColour (juce::Colours::white.withAlpha (0.45f));
                g.fillRect (R (cfgDisplay.getX(), cfgDisplay.getBottom(), cfgDisplay.getWidth(), 1.0f));
            }
        }
        hit (cfgDisplay, hDisplay);
        y += (float) dh + 6;
    }
    // what the module says of itself, and where a dropped file goes: onto this panel, for this panel
    String status = m.statusLine();
    if (cfg.kind == 0 && m.acceptsFiles())
    {
        const String f = juce::File (eng.laneFile (cfg.lane)).getFileName();
        status << (status.isEmpty() ? "" : "   ") << (f.isNotEmpty() ? "FILE: " + f : m.fileHint() + " ON THIS LANE");
    }
    else if (cfg.kind != 0 && eng.acceptsFiles (cfg) && status.isEmpty())
        status = m.acceptsFiles() ? m.fileHint() + ", ONTO THIS PANEL" : String ("DROP AN IMPULSE RESPONSE ONTO THIS PANEL TO MAKE SPACE A CONVOLUTION REVERB");
    if (cfgNote.isNotEmpty()) status = cfgNote;
    const bool takesFile = eng.acceptsFiles (cfg);
    if (status.isNotEmpty() || takesFile)
    {
        status = status.toUpperCase();
        if (status.isNotEmpty())
        {
            const R sr (x0, y, std::min (takesFile ? 468.0f : 544.0f, std::ceil (tw (fNote, status)) + 12.0f), 22);
            window (g, sr, col::white);
            g.setColour (tone (col::black)); g.setFont (fNote);
            g.drawText (status, sr.reduced (6, 0), juce::Justification::centredLeft, true);
        }
        if (takesFile)
        {
            // LOAD: the same as dropping a file here, from a list in this panel (so with the window full screen too)
            const R lr (x0 + 474, y - 1, 70, 24);
            key (g, lr, loadOpen ? col::green : col::yellow, pressed (aLoad) ? 6.0f : 2.0f, loadOpen);
            text (g, "LOAD", lr, fLbl, col::black);
            hit (lr.expanded (2), hAct, aLoad, 0);
        }
        y += 28;
    }
    if (loadOpen && takesFile) { drawLoad (g, R (x0, y, 544, r.getBottom() - 8 - y)); return; }
    loadOpen = false;

    // The controls, in boxes by what they belong to. A box goes in the first row with room for
    // it; one longer than a row runs on into the next; MOTION's two have rows of their own, last.
    const int cols = 6, total = (int) cfgParams.size();
    const float cellW = 90.0f, rowH = 126.0f, titleH = 17.0f;         // (a row's box is 106 tall: 20 between one box and the next below)
    struct Seg { int first, count, row, col; bool head; };
    std::vector<Seg> segs;
    std::vector<int> used;                                  // columns taken in each row
    for (int i = 0; i < total;)
    {
        int n = 1;
        while (i + n < total && cfgParams[(size_t) (i + n)].group == cfgParams[(size_t) i].group) ++n;
        const bool own = cfgParams[(size_t) i].ownRow;
        for (int done = 0; done < n;)
        {
            const int take = std::min (cols, n - done);
            int row = -1;
            if (! own && n <= cols) for (size_t q = 0; q < used.size() && row < 0; ++q) if (used[q] + take <= cols) row = (int) q;
            if (row < 0 && ! own && done > 0 && ! used.empty() && used.back() == 0) row = (int) used.size() - 1;
            if (row < 0) { row = (int) used.size(); used.push_back (0); }
            segs.push_back ({ i + done, take, row, used[(size_t) row], done == 0 });
            used[(size_t) row] = own ? cols : used[(size_t) row] + take;
            done += take;
        }
        i += n;
    }
    const int rows = (int) used.size();
    const int rowsVis = std::max (1, (int) ((r.getBottom() - 6 - y) / rowH));
    cfgScroll = juce::jlimit (0, std::max (0, rows - rowsVis), cfgScroll);
    for (const auto& sg : segs)
    {
        if (sg.row < cfgScroll || sg.row >= cfgScroll + rowsVis) continue;
        const float bx = x0 + (float) sg.col * cellW, by = y + (float) (sg.row - cfgScroll) * rowH;
        const R box (bx + 4, by + 7, (float) sg.count * cellW - 8, 106.0f);          // (8 between one box and the next beside it)
        const String title = cfgParams[(size_t) sg.first].group;
        if (title.isNotEmpty() && vis (g, box.expanded (2, 8)))
        {
            // the box, and its name on the box's upper edge (a box run on from the row above has none).
            // The line stops either side of the name: it does not run under it.
            const R chip (bx + 12, by, std::ceil (tw (fLbl, title)) + 10.0f, 15.0f);
            {
                const juce::Graphics::ScopedSaveState state (g);
                if (sg.head) g.excludeClipRegion (chip.expanded (5.0f, 3.0f).getSmallestIntegerContainer());
                g.setColour (tone (col::black).withAlpha (0.30f));
                g.drawRoundedRectangle (box, 5.0f, 1.0f);
            }
            if (sg.head) plate (g, chip.getX(), chip.getY(), title, fLbl, 5, col::black, col::white, 15);
        }
        for (int q = 0; q < sg.count; ++q)
        {
            const int i = sg.first + q;
            const auto& sp = cfgParams[(size_t) i];
            const float cxl = bx + (float) q * cellW, cy = by + titleH + 3;
            const R kr (cxl + 25, cy, 40, 40);
            if (vis (g, R (cxl - 16, cy, 122, rowH - titleH)))
            {
                if (sp.kind == Kind::Toggle) key (g, kr, sp.value > 0.5f ? col::green : col::putty, 2.0f, sp.value > 0.5f);
                else if (sp.kind == Kind::Hold) key (g, kr, col::yellow, sp.value > 0.5f ? 8.0f : 2.0f);
                else knob (g, cxl + 45, cy + 20, 40, sp.value, sp.kind, sp.choices.size(), col::lit);
                legend (g, cxl + 45, cy + 43, sp.name);
                // INFO: its number while CONFIG is open (the E16's CONFIG pages hold the controls in this order, twelve to a page)
                if (info && sp.kind != Kind::Hold && i < e16::kCfgPer * e16::kCfgPages)
                    textRaw (g, String (e16::cfgCc (i)), sp.kind == Kind::Toggle ? kr : R (cxl + 31, cy + 10 - (lit ? 3.0f : 0.0f), 28, 20), fLbl, sp.kind == Kind::Toggle ? inkOn (cap (sp.value > 0.5f ? col::green : col::putty), col::black) : col::lit);
                if (sp.kind == Kind::List)
                {
                    // (inside its box with room to spare; a long name is drawn narrower before it is cut short)
                    const bool first = q == 0, last = q == sg.count - 1;
                    const R vr (cxl + (first ? 9.0f : 3.0f), cy + 73, 84.0f - (first ? 6.0f : 0.0f) - (last ? 6.0f : 0.0f), 16);
                    if (lit) skin.window (g, vr, glass (col::white)); else { g.setColour (col::white); g.fillRect (vr); }
                    g.setColour (tone (col::black)); g.setFont (fLbl);
                    g.drawFittedText (sp.text.toUpperCase(), vr.reduced (2, 0).getSmallestIntegerContainer(), juce::Justification::centred, 1, 0.72f);
                }
            }
            hit (kr.expanded (4, 2), hSetup, i, 0);
        }
    }
    if (rows > rowsVis)
    {
        const String more = "ROWS " + String (cfgScroll + 1) + " TO " + String (cfgScroll + rowsVis) + " OF " + String (rows) + ": SCROLL";
        plate (g, r.getRight() - 5 - (std::ceil (tw (fNote, more)) + 12), r.getBottom() - 27, more, fNote, 6, col::white, col::black, 22);
    }
}

// ---- LOAD: folders and sound files, in CONFIG's panel
void Screen::openLoad()
{
    const bool fx = cfg.kind != 0;
    juce::File start = fx ? loadFxDir : loadLaneDir;
    if (! start.isDirectory() && cfg.kind == 0) start = juce::File (ses.cameFrom (eng.laneFile (cfg.lane))).getParentDirectory();      // (where the lane's sample came from, not the library's copy of it)
    if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    if (! start.isDirectory()) start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    loadDir = start; loadScroll = 0; loadOpen = true;
    readLoad();
}

void Screen::readLoad()
{
    loadFolders = loadDir.findChildFiles (juce::File::findDirectories | juce::File::ignoreHiddenFiles, false);
    loadFiles = loadDir.findChildFiles (juce::File::findFiles | juce::File::ignoreHiddenFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.caf");
    struct ByName { static int compareElements (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()); } } byName;
    loadFolders.sort (byName); loadFiles.sort (byName);
}

void Screen::pickLoad (int i)
{
    if (i < 0) return;
    if (i < loadFolders.size()) { loadDir = loadFolders[i]; loadScroll = 0; readLoad(); repaint(); return; }
    i -= loadFolders.size();
    if (i >= loadFiles.size()) return;
    const juce::File f = loadFiles[i];
    const bool ok = eng.loadFiles (cfg, juce::StringArray (f.getFullPathName()));
    if (ok)
    {
        (cfg.kind != 0 ? loadFxDir : loadLaneDir) = loadDir;        // (next time it opens where this one was)
        loadOpen = false; cfgNote = {}; cfgStale = true;
        ses.touch();
        if (onLook) onLook();
    }
    else cfgNote = "COULD NOT READ " + f.getFileName().toUpperCase() + ". IF MACOS ASKED FOR ACCESS TO THE FOLDER, ALLOW IT AND LOAD IT AGAIN.";
    repaint();
}

void Screen::drawLoad (G& g, R a)
{
    // where it is looking, and the way up
    const R up (a.getX(), a.getY(), 44, 24), path (a.getX() + 50, a.getY() + 1, a.getWidth() - 50, 22);
    key (g, up, col::white);
    text (g, "UP", up, fLbl, col::black);
    hit (up.expanded (2), hAct, aLoadUp, 0);
    window (g, path, col::white);
    g.setColour (tone (col::black)); g.setFont (fNote);
    g.drawText (loadDir.getFullPathName().toUpperCase(), path.reduced (6, 0), juce::Justification::centredRight, true);
    // the folders, then the files it can read
    const R list (a.getX(), a.getY() + 30, a.getWidth(), a.getHeight() - 30);
    window (g, list, col::white);
    const float lineH = 24.0f;
    const int total = loadFolders.size() + loadFiles.size();
    const int lines = std::max (1, (int) ((list.getHeight() - 26) / lineH));
    loadScroll = juce::jlimit (0, std::max (0, total - lines), loadScroll);
    for (int n = 0; n < lines; ++n)
    {
        const int i = loadScroll + n;
        if (i >= total) break;
        const bool folder = i < loadFolders.size();
        const juce::File f = folder ? loadFolders[i] : loadFiles[i - loadFolders.size()];
        const R line (list.getX() + 4, list.getY() + 3 + (float) n * lineH, list.getWidth() - 8, lineH);
        if (vis (g, line))
        {
            g.setColour (tone (col::black).withAlpha (folder ? 1.0f : 0.82f)); g.setFont (fNote);
            g.drawText ((folder ? "> " : "   ") + f.getFileName().toUpperCase(), line.reduced (6, 0), juce::Justification::centredLeft, true);
        }
        hit (line, hFile, i, 0);
    }
    const String foot = total == 0 ? String ("NO FOLDERS AND NO SOUND FILES HERE")
                      : total > lines ? String (loadScroll + 1) + " TO " + String (std::min (total, loadScroll + lines)) + " OF " + String (total) + ": SCROLL"
                      : String (loadFiles.size()) + " SOUND FILES.  CLICK ONE TO LOAD IT";
    g.setColour (tone (col::black).withAlpha (0.6f)); g.setFont (fNote);
    g.drawText (foot, R (list.getX() + 10, list.getBottom() - 24, list.getWidth() - 20, 22), juce::Justification::centredRight, true);
}

void Screen::drawLane (G& g, int l, R r)
{
    if (off (g, r)) return;
    const auto& d = lanes()[(size_t) l];
    const float x0 = r.getX() + 5, y0 = r.getY() + 5, body = y0 + 32;
    const bool active = l == ses.activeLane;
    stopAt (l < 5 ? l : l + 1, r.translated (shift, 0.0f).getIntersection (rowsClip));
    hit (r, hLanePanel, l, 0);
    panel (g, r, col::putty);
    const String num (l + 1);
    const float wTag = std::max (30.0f, std::ceil (tw (fLegend, num)) + 8.0f);
    if (lit)
    {
        // The module's display, as modelled: one dark pane with the lane's number and name. The
        // active lane's is lit up.
        const R dr (x0, y0, 126, 26);
        if (vis (g, dr))
        {
            window (g, dr, active ? col::lit : col::black);         // (lit on the dark instrument too: the one bright display)
            textRaw (g, num, R (x0 + 2, y0, 26, 26), fLegend, active ? col::black : col::lit);
            textRaw (g, d.name, R (x0 + 32, y0, 92, 26), fLegend, active ? col::black : col::lit, juce::Justification::centredLeft);
        }
        hit (dr, hLane, l, 0);
        chan (g, x0 + 129, y0, l + 1);
    }
    else
    {
        tag (g, R (x0, y0, wTag, 26), num);
        const float wn = plate (g, x0 + wTag, y0, d.name, fLegend, 8, active ? col::black : col::white, active ? col::lit : col::black);
        hit (R (x0, y0, wTag + wn, 26), hLane, l, 0);
        if (wTag + wn + 54 < 190) chan (g, x0 + wTag + wn + 2, y0, l + 1);
    }
    headKeys (g, r.getRight() - 5, y0 + 2, 0, l, 0, cx::lane (l, cx::kMute), cx::lane (l, cx::kSolo));
    for (int k = 0; k < kLaneKnobs; ++k)
        control (g, cx::lane (l, k), x0 + (float) (k % 4) * 66, body + (float) (k / 4) * 77, 64, k < 8 ? col::lit : k < 11 ? col::red : col::blue);
    control (g, cx::lane (l, cx::kSendA), x0, body + 231, 64, col::blue);
    control (g, cx::lane (l, cx::kSendB), x0 + 66, body + 231, 64, col::blue);
    for (int e = 0; e < 3; ++e) control (g, cx::lane (l, cx::kEqLow + e), x0 + 132 + (float) e * 44, body + 231, 42, col::blue, 30);
}

void Screen::drawSnapPanel (G& g, R r)
{
    if (off (g, r)) return;
    const float x0 = r.getX() + 5, y0 = r.getY() + 5, body = y0 + 32;
    stopAt (5, r.translated (shift, 0.0f).getIntersection (rowsClip));
    hit (r, hFront, Session::fSnap, 0);
    panel (g, r, col::putty);
    if (vis (g, R (x0, y0, 262, 26)))
    {
        const float w = title (g, x0, y0, "SNAPSHOTS", ses.front.kind == Session::fSnap);
        // NOTES: whether a recall brings a snapshot's notes (lit) or its sound alone
        const R kr (x0 + w + 4, y0 + 2, 46, 22);
        key (g, kr, ses.recallNotes ? col::white : col::putty, pressed (aNotes) ? 6.0f : 2.0f, ses.recallNotes);
        if (ses.recallNotes) text (g, "NOTES", kr, fLbl, col::black);
        else textRaw (g, "NOTES", kr, fLbl, inkOn (glass (col::putty), col::black).withAlpha (0.5f));
        // (INFO: the page's channel, where the time of the last snapshot was)
        const String last = "LAST " + ses.lastTakenAt;
        if (info) chan (g, kr.getRight() + 4, y0, e16::kSnapChannel);
        else plate (g, r.getRight() - 5 - (std::ceil (tw (fNote, last)) + 12), y0, last, fNote, 6, col::white, col::black);
    }
    hit (R (x0 + std::ceil (tw (fLegend, "SNAPSHOTS")) + 16 + 4, y0 + 2, 46, 22).expanded (1), hAct, aNotes, 0);
    for (int k = 0; k < 16; ++k)
    {
        const float cxm = x0 + (float) (k % 4) * 66 + 32, y = body + (float) (k / 4) * 61;
        const R kr (cxm - 20, y, 40, 40);
        if (ses.knobLive (k))
        {
            const float v = eng.shown (cx::kSnap + k);
            if (vis (g, R (cxm - 40, y, 80, 58)))
            {
                if (seen (g, lit ? kr : R (cxm - 40, y, 80, 58))) lastShown[(size_t) (cx::kSnap + k)] = v;
                // (a knob that holds a stack is lit amber, and while the snapshot it has come to plays in the headphones it says which)
                const auto& kn = ses.knobs[(size_t) k];
                const auto light = kn.pick ? col::amber : col::teal;
                knobCtl = cx::kSnap + k; knobLight = light;
                knob (g, cxm, y + 20, 40, v, Kind::Knob, 0, light);
                knobCtl = -1;
                if (info) number (g, cx::kSnap + k, R (cxm - 14, y + 10 - (lit ? 3.0f : 0.0f), 28, 20), col::lit);
                const Snapshot* on = kn.pick && ses.cueing() != 0 && ses.cueing() == kn.picked ? ses.findSnap (kn.picked) : nullptr;
                text (g, on != nullptr ? on->name : kn.name, R (cxm - 40, y + 42, 80, 14), fLbl, col::black);
            }
            hit (kr.expanded (4, 2), hSnapKnob, k, 0);
        }
        else if (vis (g, kr)) hollow (g, cxm, y + 20, 40, true);
    }
    const float ky = r.getBottom() - 62;
    const R bar (x0, ky, 84, 40);
    if (vis (g, R (x0, ky, 262, 60)))
    {
        key (g, bar, col::yellow, pressed (aSnapshot) ? 6.0f : 2.0f);
        text (g, "SNAPSHOT", R (x0, ky + 43, 84, 14), fLbl, col::black);
    }
    hit (bar, hAct, aSnapshot, 0);
    actKey (g, aLibrary, x0 + 88, ky, 56, "LIBRARY", col::white);
    actKey (g, aMacros, x0 + 146, ky, 56, "MACROS", col::white);
    actKey (g, aArrange, x0 + 204, ky, 56, "ARRANGE", ses.arrPlaying() ? col::green : col::white);
}

void Screen::drawInsert (G& g, int slot, R r)
{
    if (off (g, r)) return;
    const float x0 = r.getX() + 5, y0 = r.getY() + 5, body = y0 + 32;
    const int l = ses.shownLane(), t = eng.insertType (l, slot);       // (the active lane's; while the pointer is on a lane or its inserts, that lane's)
    stopAt (12 + slot, r.translated (shift, 0.0f).getIntersection (rowsClip));
    hit (r, hDropFx, 1, slot);
    panel (g, r, col::putty);
    const String name = t >= 0 ? String (effects()[(size_t) t].name) : String ("EMPTY");
    const float w = title (g, x0, y0, name, ses.front == Session::Front { Session::fInsert, slot });
    hit (R (x0, y0, w, 26), hInsHead, slot, 0);
    if (t >= 0 && w + 54 < 190) chan (g, x0 + w + 2, y0, l + 1);
    if (t >= 0) headKeys (g, r.getRight() - 5, y0 + 2, 1, l, slot, cx::insMute (l, slot), cx::insSolo (l, slot));
    for (int c = 0; c < kCells; ++c) control (g, cx::ins (l, slot, c), x0 + (float) (c % 4) * 66, body + (float) (c / 4) * 77, 64, col::lit);
}

void Screen::drawReturns (G& g, R r)
{
    if (off (g, r)) return;
    const float x0 = r.getX() + 5, y0 = r.getY() + 5, body = y0 + 32;
    stopAt (15, r.translated (shift, 0.0f).getIntersection (rowsClip));
    hit (r, hDropFx, 2, 1);
    panel (g, r, col::putty);
    title (g, x0, y0, "RETURNS", ses.front.kind == Session::fReturns);
    const float xr = r.getRight() - 5;
    text (g, "A", R (xr - 180, y0, 14, 26), fLegend, col::black);
    headKeys (g, xr - 94, y0 + 2, 2, 0, 0, cx::kRetMS, cx::kRetMS + 1);
    text (g, "B", R (xr - 86, y0, 14, 26), fLegend, col::black);
    headKeys (g, xr, y0 + 2, 2, 0, 1, cx::kRetMS + 2, cx::kRetMS + 3);
    // A's four, then B's four for the reverb it is now
    const int sm = eng.spaceModel();
    for (int k = 0; k < 4; ++k)
    {
        control (g, cx::kRet + k, x0 + (float) k * 66, body, 64, col::lit);
        control (g, cx::space (sm, k), x0 + (float) k * 66, body + 77, 64, col::lit);
    }
}

void Screen::drawMaster (G& g, R r)
{
    if (off (g, r)) return;
    // the copy of it that is on the screen (the columns go round while CONFIG is open)
    if (collect) if (const R mr = r.translated (shift, 0.0f).getIntersection (rowsClip); ! mr.isEmpty()) masterRect = masterRect.isEmpty() ? mr : masterRect.getUnion (mr);
    meterSig = metersNow();
    const float x0 = r.getX() + 5, y0 = r.getY() + 5, body = y0 + 32;
    stopAt (16, r.translated (shift, 0.0f).getIntersection (rowsClip));
    hit (r, hFront, Session::fMaster, 0);
    panel (g, r, col::grey);
    const float wTitle = title (g, x0, y0, "MASTER", ses.front.kind == Session::fMaster);
    chan (g, x0 + wTitle + 2, y0, e16::kSnapChannel);
    for (int k = 0; k < 3; ++k) control (g, cx::kMaster + k, x0 + (float) k * 66, body, 64, col::lit);
    // the drum bus: its four knobs, and C for the rest of it
    {
        const float bx = x0 + 204;
        if (vis (g, R (bx, y0, 132, 26))) text (g, "DRUM BUS", R (bx + 4, y0, 90, 26), fLegend, col::black, juce::Justification::centredLeft);
        const R kr (bx + 106, y0 + 2, 22, 22);
        const bool open = cfgOpen && cfg.kind == 3;
        if (vis (g, kr)) { key (g, kr, open ? col::black : col::white, 2.0f, open); textRaw (g, "C", kr, fLbl, inkOn (cap (open ? col::black : col::white), open ? col::lit : col::black)); }
        hit (kr.expanded (1), hConfig, 3, 0);
        for (int k = 0; k < kBusKnobs; ++k) control (g, cx::kBus + k, bx + (float) (k % 2) * 66, body + (float) (k / 2) * 77, 64, col::lit);
    }
    if (! vis (g, R (x0, body + 60, 544, 100))) return;
    // FAULT, gain reduction, loudness
    const float fy = body + 77;
    g.setColour (tone (col::black)); g.fillRect (R (x0 + 6, fy + 8, 14, 26));
    g.setColour (eng.fault.load() ? col::amber : tone (col::grey)); g.fillRect (R (x0 + 8, fy + 10, 10, 22));
    text (g, "FAULT", R (x0 + 25, fy, 40, 42), fLbl, col::black, juce::Justification::centredLeft);
    readout (g, R (x0 + 66, fy, 64, 42), "GR", String (eng.gainRed.load(), 1).replaceCharacter ('-', (juce::juce_wchar) 0x2212));
    const float lu = eng.lufs.load();
    readout (g, R (x0 + 132, fy, 64, 42), "LUFS", lu < -60.0f ? String ("--") : String (lu, 1).replaceCharacter ('-', (juce::juce_wchar) 0x2212));
    // POCKETS: its page, and how much of it is on
    {
        const R kr (x0 + 6, fy + 49, 22, 22);
        key (g, kr, eng.target (cx::kPkG) > 0.0f ? col::green : col::white, pressed (aPockets) ? 6.0f : 2.0f);
        text (g, "POCKETS", R (x0 + 34, fy + 49, 60, 22), fLbl, col::black, juce::Justification::centredLeft);
        text (g, eng.valueText (cx::kPkG), R (x0 + 96, fy + 49, 40, 22), fLbl, col::black, juce::Justification::centredLeft);
        hit (R (x0 + 4, fy + 47, 130, 26), hAct, aPockets, 0);
    }
    // meters: every lane as it reaches the mix, then the mix itself
    const R mp (x0 + 342, y0, 202, 180);
    window (g, mp, col::white);
    const float top = mp.getY() + 26, H = 122.0f, bot = top + H;
    auto yOf = [bot, H] (float db) { return bot - juce::jlimit (0.0f, H, (db + 60.0f) / 66.0f * H); };
    auto bar = [&] (float x, float w, float db)
    {
        g.setColour (tone (col::black)); g.fillRect (R (x, top - 2, w, H + 4));
        g.setColour (tone (col::white)); g.fillRect (R (x + 2, top, w - 4, H));
        g.setColour (tone (col::black)); g.fillRect (R (x + 2, yOf (db), w - 4, bot - yOf (db)));
    };
    for (int l = 0; l < kLanes; ++l)
    {
        const float x = mp.getX() + 7 + (float) l * 12;
        bar (x, 11, eng.laneDb[(size_t) l].load());
        const R nb (x - 1, bot + 8, 13, 16);
        const bool act = l == ses.activeLane;
        if (act) { g.setColour (tone (col::black)); g.fillRect (nb); }
        textRaw (g, String (l + 1), nb, fLbl, act ? (night() ? col::black : col::lit) : tone (col::black));
    }
    const float mx = mp.getX() + 144;
    bar (mx, 13, eng.peakL.load());
    bar (mx + 14, 13, eng.peakR.load());
    flatKey (g, R (mx, mp.getY() + 6, 13, 12), eng.clipL.load() ? col::red : col::white);       // (flags on the display, not keys)
    flatKey (g, R (mx + 14, mp.getY() + 6, 13, 12), eng.clipR.load() ? col::red : col::white);
    text (g, "MIX", R (mx - 4, bot + 8, 35, 16), fLbl, col::black);
    const float sx = mx + 29;
    g.setColour (col::amber); g.fillRect (R (sx, yOf (0), 3, yOf (-6) - yOf (0)));
    g.setColour (col::red); g.fillRect (R (sx, yOf (6), 3, yOf (0) - yOf (6)));
    static const int ticks[] = { -48, -24, -12, 0 };
    for (int t : ticks)
    {
        g.setColour (tone (col::black)); g.fillRect (R (sx, yOf ((float) t) - 1, 6, 2));
        text (g, t == 0 ? String ("0") : String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + String (-t), R (sx + 7, yOf ((float) t) - 7, 22, 14), fLbl, col::black, juce::Justification::centredLeft);
    }
    hit (R (mx - 2, mp.getY() + 2, 32, 20), hAct, aClipReset, 0);
}

void Screen::drawSequencer (G& g, R r)
{
    seqRect = r;
    stopAt (17, r);
    if (off (g, r)) return;
    hit (r, hFront, Session::fSeq, 0);
    panel (g, r, col::putty);
    const int l = ses.activeLane;
    const auto& d = lanes()[(size_t) l];
    const Pattern pat = ses.pattern (l);
    const int len = pat.length;
    const float x0 = r.getX() + 5, y0 = r.getY() + 5, ky = y0 + 40;

    // the lane, its length, the selected step
    const String num (l + 1);
    const float wTag = std::max (30.0f, std::ceil (tw (fLegend, num)) + 8.0f);
    tag (g, R (x0, y0, wTag, 26), num);
    const bool seqFront = ses.front.kind == Session::fSeq;       // (in front: its name lit up)
    const float wn = plate (g, x0 + wTag, y0, d.name, fLegend, 8, seqFront ? (lit ? col::lit : col::black) : col::white, seqFront && ! lit ? col::lit : col::black);
    const float ws = plate (g, x0 + wTag + wn + 2, y0, String (len) + (len == 1 ? " STEP" : " STEPS"), fNote, 6, col::white, col::black);
    // M: this lane's own sequencer muted. Its steps stay and go unplayed; notes from outside still sound.
    const int muteCtl = cx::lane (l, cx::kSeqMute);
    const bool seqOff = eng.shown (muteCtl) > 0.5f;
    const R mk (x0 + wTag + wn + 2 + ws + 4, y0 + 2, 22, 22);
    if (vis (g, mk))
    {
        key (g, mk, seqOff ? col::amber : col::putty, 2.0f, seqOff);
        String letter ("M");
        if (int ch = 0, num = 0; info && ccx::addressOf (muteCtl, ch, num)) letter = String (num);
        textRaw (g, letter, mk, fLbl, inkOn (cap (seqOff ? col::amber : col::putty), col::black));
        if (seen (g, mk)) lastShown[(size_t) muteCtl] = eng.shown (muteCtl);
    }
    hit (mk.expanded (1), hToggle, muteCtl);
    const bool hasSel = ses.selStep >= 0 && ses.selStep < len;
    const String st = hasSel ? "STEP " + String (ses.selStep + 1) : String ("NO STEP");
    plate (g, x0 + 356 - (std::ceil (tw (fNote, st)) + 12), y0, st, fNote, 6, col::white, col::black);

    // pattern keys, then step keys
    knob (g, x0 + 28, ky + 20, 40, (float) len / 128.0f, Kind::Knob, 0, col::lit);
    legend (g, x0 + 28, ky + 45, "LENGTH");
    hit (R (x0 + 4, ky - 2, 48, 44), hLen, 0, 0);
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
            if (lit) skin.window (g, pr.reduced (0.5f), glass (col::white));
            if (w == ses.page) { g.setColour (tone (col::black)); g.drawRect (pr, 2.0f); }
            for (int j = 0; j < 16; ++j)
            {
                const int i = w * 16 + j;
                const R c (pr.getX() + 3 + (float) j * 5, pr.getY() + 3, 4, 12);
                const bool in = i < len;
                g.setColour (tone (in ? col::black : col::sea)); g.fillRect (c);
                if (! in) { g.setColour (tone (col::putty)); g.fillRect (c.reduced (1)); }
                else if (i == playing) { g.setColour (col::green); g.fillRect (c.reduced (1)); }
                else if (! pat.steps[(size_t) i].on) { g.setColour (tone (col::white)); g.fillRect (c.reduced (1)); }
            }
        }
        hit (pr.expanded (0, 3), hPageSel, w, 0);
    }

    // sixteen steps
    for (int j = 0; j < 16; ++j)
    {
        const int i = ses.page * 16 + j;
        const auto& x = pat.steps[(size_t) i];
        const bool in = i < len, on = in && x.on;
        const float sx = x1 + (float) j * 45 + (float) (j / 4) * 7;
        const R kr (sx, ky, 40, 40), inner = kr.reduced (2);
        hit (kr, hStep, i, 0);
        if (! vis (g, R (sx - 16, y0 + 20, 72, 82))) continue;
        // a gate over one step runs on: through unlit steps as a tie, into a lit one as legato
        int k = -1;
        if (in) for (int q = i - 1; q >= 0; --q) if (pat.steps[(size_t) q].on) { k = q; break; }
        const bool tied = k >= 0 && pat.steps[(size_t) k].ratchet == 1;
        const float end = tied ? std::min ((float) k + pat.steps[(size_t) k].gate, (float) len) : 0.0f;
        float carry = 0.0f; bool bridge = false;
        if (tied)
        {
            if (on) bridge = end >= (float) i + kLegatoOver - 0.001f;
            else { carry = juce::jlimit (0.0f, 1.0f, end - (float) i); if (carry < 0.03f) carry = 0.0f; bridge = carry > 0.0f; }
        }
        lamp (g, sx + 20, y0 + 30, i == playing ? col::green : col::putty);
        if (lit) { if (in) key (g, kr, col::white); else hollow (g, sx + 20, ky + 20, 40, false); }
        else if (in) { g.setColour (col::black); g.fillRect (kr); g.setColour (col::white); g.fillRect (inner); }
        else { g.setColour (col::sea); g.drawRect (kr.reduced (0.5f), 1.0f); }
        g.setColour (seqOff ? (night() ? night::dim : col::grey) : tone (col::black));      // muted: the steps are there, in grey
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
                if (nextOn ? e2 >= (float) (i + 1) + kLegatoOver - 0.001f : e2 - (float) (i + 1) > 0.03f)
                {
                    const float hk = std::round (pat.steps[(size_t) ks].vel * 36.0f), h = nextOn ? std::min (6.0f, hk) : hk;
                    g.fillRect (R (sx + 38, inner.getBottom() - h, 9, h));
                }
            }
        }
        const bool sel = in && i == ses.selStep;
        const R nb (sx, ky + 44, 40, 16);
        if (sel) { g.setColour (tone (col::black)); g.fillRect (nb); }
        textRaw (g, d.pitched && on ? String (kNotes[juce::jlimit (0, 11, x.note)]) + String (x.octave) : String (i + 1), nb, fLbl, sel ? (night() ? col::black : col::lit) : tone (col::black));
    }

    // generators
    const float x2 = x1 + 752;
    const int armed = ses.genType[(size_t) l];
    title (g, x2, y0, "GENERATE");
    actKey (g, aGen0, x2, ky, 64, "RANDOM", armed == 0 ? col::green : col::putty, 5, true, armed == 0);
    actKey (g, aGen1, x2 + 66, ky, 64, "EUCLID", armed == 1 ? col::green : col::putty, 5, true, armed == 1);
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
    hit (r, hitKind, index, 0);
    if (! vis (g, r)) return;
    card (g, r, selected);
    const float x0 = r.getX() + 6, y0 = r.getY() + 6;
    const String num (index + 1);
    const float wTag = std::max (30.0f, std::ceil (tw (fLegend, num)) + 8.0f);
    tag (g, R (x0, y0, wTag, 24), num);
    const bool isBase = s.id == ses.baseId;
    // the base scene's name is the black one; on the dark instrument, the lit one
    const float wn = plate (g, x0 + wTag, y0, s.name, fLegend, 8, isBase ? (night() ? col::lit : col::black) : col::white, isBase ? (night() ? col::black : col::lit) : col::black, 24);
    if (selected && hitKind == hCard) selNameRect = R (x0 + wTag, y0, std::max (wn, 150.0f), 24);
    if (s.id == ses.cueing()) plate (g, r.getRight() - 6 - (std::ceil (tw (fLegend, "IN THE PHONES")) + 16), y0, "IN THE PHONES", fLegend, 8, col::green, col::lit, 24);
    else text (g, s.time.formatted ("%b %d  %H:%M").toUpperCase(), R (r.getRight() - 110, y0, 104, 24), fLbl, col::black, juce::Justification::centredRight);
    const float gy = y0 + 28, gh = 30;
    auto box = [&] (float x, float w)
    {
        const R b (x, gy, w, gh);
        if (lit) skin.window (g, b.reduced (0.5f), glass (col::white));
        else { g.setColour (col::black); g.fillRect (b); g.setColour (col::white); g.fillRect (b.reduced (1)); }
        return b.reduced (1);
    };
    if (s.profile.has)
    {
        // What it sounded like, from the bars kept with it. One block: as tall as its energy, as
        // hot as it is bright (deep red for dark, through amber and yellow, to white).
        const auto& p = s.profile;
        const R e = box (x0, 16);
        const float eh = std::round (juce::jlimit (0.0f, 1.0f, p.energy) * e.getHeight());
        g.setColour (heat (p.bright)); g.fillRect (R (e.getX(), e.getBottom() - eh, e.getWidth(), eh));
        // the spectrum's shape, low to high
        const R sp = box (x0 + 20, 82);
        g.setColour (tone (col::black));
        for (int k = 0; k < 16; ++k)
        {
            const float h = std::round (p.bands[(size_t) k] * sp.getHeight());
            g.fillRect (R (sp.getX() + (float) k * 5, sp.getBottom() - h, 4, h));
        }
        // where the hits fall in the bar: the top above, the low end below
        const R pu = box (x0 + 106, 98);
        for (int row = 0; row < 2; ++row)
            for (int k = 0; k < 16; ++k)
            {
                const float v = p.pulse[(size_t) ((row == 0 ? 16 : 0) + k)];
                if (v < 0.08f) continue;
                g.setColour (tone (col::black).withAlpha (juce::jlimit (0.0f, 1.0f, v)));
                g.fillRect (R (pu.getX() + (float) k * 6, pu.getY() + (float) row * 14, 5, 13));
            }
        // how much of each lane
        const R la = box (x0 + 208, 52);
        g.setColour (tone (col::black));
        for (int l = 0; l < kLanes; ++l)
        {
            const float h = std::round (p.lanes[(size_t) l] * la.getHeight());
            g.fillRect (R (la.getX() + 1 + (float) l * 4.5f, la.getBottom() - h, 3.5f, h));
        }
        return;
    }
    // no sound was kept with it: how busy each lane's pattern is
    for (int l = 0; l < kLanes; ++l)
    {
        const auto& p = s.patterns[(size_t) l];
        int on = 0;
        for (int i = 0; i < p.length; ++i) on += p.steps[(size_t) i].on ? 1 : 0;
        const float f = juce::jlimit (0.0f, 1.0f, (float) on / (float) std::max (1, std::min (p.length, 16)));
        const R b (x0 + (float) l * 23.6f, y0 + 30, 20, 16);
        g.setColour (tone (col::black)); g.fillRect (b);
        g.setColour (tone (col::white)); g.fillRect (b.reduced (1));
        g.setColour (tone (col::black)); g.fillRect (R (b.getX() + 1, b.getBottom() - 1 - std::round (f * 14), 18, std::round (f * 14)));
    }
}

// The snapshots the library screen lists: every one, or the stack chosen with STACK, in the stack's order.
std::vector<int> Screen::listed() const
{
    std::vector<int> out;
    if (libStack >= 0 && libStack < (int) ses.stacks.size())
    {
        for (auto id : ses.stacks[(size_t) libStack].snaps)
            for (int i = 0; i < (int) ses.library.size(); ++i) if (ses.library[(size_t) i].id == id) { out.push_back (i); break; }
        return out;
    }
    for (int i = 0; i < (int) ses.library.size(); ++i) out.push_back (i);
    return out;
}

void Screen::drawLibrary (G& g)
{
    if (libStack >= (int) ses.stacks.size()) libStack = -1;         // (the stack it showed is no longer in the tray)
    const auto list = listed();
    const int total = (int) list.size(), perPage = 6 * 12;
    libScroll = juce::jlimit (0, std::max (0, (total - 1) / 6 - 11), libScroll);
    selNameRect = {};
    for (int n = 0; n < perPage; ++n)
    {
        const int at = libScroll * 6 + n;
        if (at >= total) break;
        const int i = list[(size_t) at];
        const auto& s = ses.library[(size_t) i];
        drawSnapCard (g, s, i, R (23.0f + (float) (n % 6) * 282, 78.0f + (float) (n / 6) * 74, 272, 66), s.id == selSnap, hCard);
    }
    const R strip (23, 980, 1682, 110);
    panel (g, strip, col::putty);
    const float x0 = strip.getX() + 5, y0 = strip.getY() + 5, ky = y0 + 40;
    const juce::ScopedValueSetter<bool> onOneLine (oneLine, true);      // (no room under these keys for a second line: a legend that ran on went off the plate)
    title (g, x0, y0, libStack >= 0 ? ses.stacks[(size_t) libStack].name + ": " + String (total) + " OF " + String ((int) ses.library.size())
                                    : String (total) + (total == 1 ? " SNAPSHOT" : " SNAPSHOTS"));
    actKey (g, aMain, x0, ky, 64, "MAIN", col::white, 5);
    actKey (g, aMacros, x0 + 66, ky, 64, "MACROS", col::white, 5);
    actKey (g, aSnapshot, x0 + 160, ky, 64, "SNAPSHOT", col::yellow, 5);
    const bool has = ses.findSnap (selSnap) != nullptr;
    actKey (g, aRename, x0 + 226, ky, 64, "RENAME", col::yellow, 5, has);
    actKey (g, aDelete, x0 + 308, ky, 64, "HOLD: DELETE", col::red, 5, has);
    // CUE: taps are heard in the headphones; RECALL then takes the one being heard (or the one chosen) to the set
    actKey (g, aCue, x0 + 386, ky, 64, "CUE", cueMode ? col::green : col::putty, 5, true, cueMode);
    actKey (g, aRecallCue, x0 + 452, ky, 64, "RECALL", col::yellow, 5, cueMode && (ses.cueing() != 0 || has));
    // What RECALL takes of it: every lane, the returns and the master, unless some are put out
    // first. Put out, a part stays as it is now. The next snapshot starts with all of them again.
    {
        const auto id = ses.cueing() != 0 ? ses.cueing() : selSnap;
        if (id != scopeFor) { scopeFor = id; scopeOn.fill (true); }
        if (cueMode && ses.findSnap (id) != nullptr)
        {
            text (g, "RECALL TAKES", R (x0 + 386, y0 + 4, 96, 24), fLbl, col::black, juce::Justification::centredLeft);
            for (int i = 0; i < kLanes + 3; ++i)
            {
                const R kr (x0 + 486 + (float) i * 40, y0 + 4, 36, 24);
                key (g, kr, scopeOn[(size_t) i] ? col::white : col::putty, 2.0f, scopeOn[(size_t) i]);
                const String name = i < kLanes ? String (i + 1) : i == kLanes ? String ("RET") : i == kLanes + 1 ? String ("BUS") : String ("KEY");
                if (scopeOn[(size_t) i]) text (g, name, kr, fLbl, col::black);
                else textRaw (g, name, kr, fLbl, inkOn (glass (col::putty), col::black).withAlpha (0.5f));        // (put out: still to be read, dimly)
                hit (kr.expanded (1), hScope, i, 0);
            }
            // IN SET: the lanes put out are heard too, as the set plays them now
            const R sr (x0 + 486 + (float) (kLanes + 3) * 40 + 12, y0 + 4, 60, 24);
            key (g, sr, ses.cueInSet ? col::green : col::putty, pressed (aCueSet) ? 6.0f : 2.0f, ses.cueInSet);
            if (ses.cueInSet) text (g, "IN SET", sr, fLbl, col::black);
            else textRaw (g, "IN SET", sr, fLbl, inkOn (glass (col::putty), col::black).withAlpha (0.5f));
            hit (sr.expanded (1), hAct, aCueSet, 0);
        }
    }
    // PHONES: one pair of outputs with headphones on it. While a cue plays: the set left, the cue right.
    actKey (g, aPhones, x0 + 518, ky, 64, "PHONES", eng.phones() ? col::green : col::putty, 5, true, eng.phones());
    // NOTES: whether a recall brings a snapshot's notes
    actKey (g, aNotes, x0 + 584, ky, 64, "NOTES", ses.recallNotes ? col::green : col::putty, 5, true, ses.recallNotes);
    const String help = ! cueMode ? (night() ? "TAP A SNAPSHOT TO RECALL IT. SOUND KNOBS TRAVEL OVER THE GLIDE TIME. THE LIT NAME IS THE BASE SCENE."
                                             : "TAP A SNAPSHOT TO RECALL IT. SOUND KNOBS TRAVEL OVER THE GLIDE TIME. THE BLACK NAME IS THE BASE SCENE.")
                      : eng.phones() ? "CUE WITH PHONES LIT: WHILE A SNAPSHOT'S BARS PLAY, THE SET IS IN THE LEFT EAR AND THE CUE IN THE RIGHT. TAP IT AGAIN TO STOP. RECALL TAKES IT TO THE SET."
                      : ! eng.hasCueOutput() ? "CUE NEEDS OUTPUTS 3 + 4 FOR THE HEADPHONES (TICK THEM IN SETUP), OR PHONES LIT: THEN THIS OUTPUT CARRIES THE SET LEFT AND THE CUE RIGHT."
                      : "CUE: TAP A SNAPSHOT TO HEAR ITS BARS IN THE HEADPHONES, IN TIME WITH THE CLOCK. TAP IT AGAIN TO STOP. RECALL TAKES IT TO THE SET.";
    // STACK: the stacks LOAD brought from the web library, one after another, then every snapshot again
    const bool stacked = ! ses.stacks.empty();
    if (stacked) actKey (g, aStack, x0 + 678, ky, 64, "STACK", libStack >= 0 ? col::green : col::putty, 5, true, libStack >= 0);
    if (online == nullptr) { text (g, help, R (x0 + (stacked ? 760.0f : 672.0f), ky, stacked ? 886.0f : 974.0f, 40), fLbl, col::black, juce::Justification::centredLeft); return; }
    // ONLINE: each snapshot goes to the library on the web as it is taken. The lamp: green when the
    // library holds them all, yellow while sending, amber when it waits (no network, or not set up), red when turned away.
    const auto st = online->status().state;
    onlineStamp = online->stamp();
    const Colour lamp = st == Online::State::off ? col::putty : st == Online::State::upToDate ? col::green : st == Online::State::working ? col::yellow
                      : st == Online::State::refused ? col::red : col::amber;
    actKey (g, aOnline, x0 + 744, ky, 64, "ONLINE", lamp, 5, true, st != Online::State::off);
    actKey (g, aOnlineSetup, x0 + 810, ky, 64, "SET UP", col::white, 5);
    // LOAD: what the web library's tray holds, brought here (its stacks, its macros, a song, and the snapshots this library lacks)
    const bool loading = online->loading();
    actKey (g, aFetch, x0 + 876, ky, 64, "LOAD", loading ? col::yellow : col::white, 5, online->setup().complete(), loading);
    const String said = online->loadLine();
    text (g, online->line(), R (x0 + 964, ky, 696, 20), fLbl, col::black, juce::Justification::centredLeft);
    text (g, said.isNotEmpty() ? said : help, R (x0 + 964, ky + 20, 696, 20), fLbl, col::black, juce::Justification::centredLeft);
}

// ------------------------------------------------------------------------------------- POCKETS
// The master's page for who has which part of the spectrum. A strip of what the whole rig is set
// to, then a row for every lane and the two returns: a map of the spectrum, 31 Hz to 16 kHz, with
// the lane's pocket on it, what the lane is playing and what is being taken from it; and the
// pocket's four knobs.
namespace
{
constexpr float kPkTopDb = -24.0f, kPkRangeDb = 60.0f;       // a map's height: a band's level from -84 to -24 dB
}

juce::uint64 Screen::pocketSig (int q) const
{
    // what a row's map shows, to the pixel: a change in this is a reason to draw it again
    juce::uint64 sig = 1469598103934665603ull;
    auto mix = [&sig] (int v) { sig = (sig ^ (juce::uint64) (v & 0xffff)) * 1099511628211ull; };
    for (int b = 0; b < Pockets::kBands; ++b)
    {
        mix ((int) std::lround (juce::jlimit (0.0f, 1.0f, (pkSeen.level[(size_t) q][(size_t) b] - (kPkTopDb - kPkRangeDb)) / kPkRangeDb) * 56.0f));
        mix ((int) std::lround (pkSeen.cut[(size_t) q][(size_t) b] * 56.0f / kPkRangeDb));
    }
    if (q < kLanes) for (int k = 0; k < 3; ++k) mix ((int) std::lround (eng.shown (cx::pk (q, k)) * 2000.0f));
    return sig;
}

void Screen::drawPocketMap (G& g, int q, R mr)
{
    pkMaps[(size_t) q] = mr;
    if (! vis (g, mr)) return;
    if (seen (g, mr)) pkSig[(size_t) q] = pocketSig (q);
    window (g, mr, col::black);
    if (recording) return;                      // (in 3D the pane is a display: what it shows is light, printed on the sheet)
    const R in = mr.reduced (3.0f, 3.0f);
    const float bw = in.getWidth() / (float) Pockets::kBands, bot = in.getBottom(), H = in.getHeight();
    auto xAt = [&] (float band) { return in.getX() + (band + 0.5f) * bw; };
    // the octaves, faintly
    g.setColour (col::lit.withAlpha (0.10f));
    for (int b = 0; b < Pockets::kBands; b += 3) g.fillRect (R (xAt ((float) b) - 0.5f, in.getY(), 1.0f, H));
    if (q < kLanes)
    {
        // the pocket: as tall as it is firmly held
        const float home = eng.shown (cx::pk (q, 0)), width = eng.shown (cx::pk (q, 1)), hold = eng.shown (cx::pk (q, 2));
        juce::Path shape;
        shape.startNewSubPath (in.getX(), bot);
        for (float x = in.getX(); x <= in.getRight() + 0.5f; x += 3.0f)
        {
            const float band = (x - in.getX()) / bw - 0.5f;
            shape.lineTo (x, bot - H * Pockets::claimOf (hold, Pockets::pocketAt (home, width, band)));
        }
        shape.lineTo (in.getRight(), bot);
        shape.closeSubPath();
        g.setColour (col::yellow.withAlpha (0.22f)); g.fillPath (shape);
        g.setColour (col::yellow.withAlpha (0.85f)); g.strokePath (shape, juce::PathStrokeType (1.5f));
        g.setColour (col::yellow); g.fillRect (R (xAt (Pockets::homeBand (home)) - 1.0f, in.getY(), 2.0f, H));       // its HOME
    }
    // what it is playing, band by band, and what is being taken from each
    for (int b = 0; b < Pockets::kBands; ++b)
    {
        const float lev = pkSeen.level[(size_t) q][(size_t) b], cut = pkSeen.cut[(size_t) q][(size_t) b];
        const float h = juce::jlimit (0.0f, 1.0f, (lev - (kPkTopDb - kPkRangeDb)) / kPkRangeDb) * H;
        if (h < 0.5f) continue;
        const float x = in.getX() + (float) b * bw + 3.0f, w = bw - 6.0f;
        const float took = std::min (h, cut * H / kPkRangeDb);
        g.setColour (col::lit.withAlpha (0.80f)); g.fillRect (R (x, bot - h + took, w, h - took));
        if (took >= 0.5f) { g.setColour (col::red); g.fillRect (R (x, bot - h, w, took)); }
    }
}

void Screen::drawPockets (G& g)
{
    // ---- the strip: the rig's four knobs, AUTO and RESET, the way back, and the map's scale
    const R strip (23, 78, 1682, 96);
    const float mapX = 190.0f, mapW = 1232.0f;                 // the maps: 28 bands of 44
    if (vis (g, strip))
    {
        panel (g, strip, col::grey);
        const float x0 = strip.getX() + 5, y0 = strip.getY() + 5;
        title (g, x0, y0, "POCKETS");
        text (g, "WHO HAS WHICH PART", R (x0, y0 + 30, 160, 16), fLbl, col::black, juce::Justification::centredLeft);
        text (g, "OF THE SPECTRUM", R (x0, y0 + 46, 160, 16), fLbl, col::black, juce::Justification::centredLeft);
        // the scale: a mark at every octave
        static const char* const marks[] = { "31", "63", "125", "250", "500", "1K", "2K", "4K", "8K", "16K" };
        const float bw = (mapW - 6.0f) / (float) Pockets::kBands;
        for (int k = 0; k < 10; ++k)
        {
            const float x = mapX + 3.0f + ((float) (k * 3) + 0.5f) * bw;
            text (g, marks[k], R (x - 24, strip.getBottom() - 19, 48, 14), fLbl, col::black);
            g.setColour (tone (col::black)); g.fillRect (R (x - 0.5f, strip.getBottom() - 5, 1.0f, 4.0f));
        }
        text (g, "HZ", R (mapX + mapW + 2, strip.getBottom() - 19, 30, 14), fLbl, col::black, juce::Justification::centredLeft);
        // what AUTO or RESET just did; else how to use the page
        const R words (mapX + 480, y0 + 2, 1025, 54);
        if (pkNote.isNotEmpty()) plate (g, words.getX(), y0 + 12, pkNote, fNote, 6, col::white, col::black);
        else
        {
            text (g, "EACH LANE'S POCKET (YELLOW) IS ITS OWN PART OF THE SPECTRUM. THERE THE OTHERS GIVE WAY TO IT, WHILE IT SOUNDS; ELSEWHERE IT GIVES WAY (RED).", R (words.getX(), y0 + 2, words.getWidth(), 16), fLbl, col::black, juce::Justification::centredLeft);
            text (g, "DRAG A POCKET: LEFT AND RIGHT ITS HOME, UP AND DOWN ITS WIDTH. HOLD: WHO WINS WHERE POCKETS MEET. YIELD: THE MOST A LANE GIVES.", R (words.getX(), y0 + 19, words.getWidth(), 16), fLbl, col::black, juce::Justification::centredLeft);
            text (g, "NOTHING IS CUT WITH AMOUNT AT NOUGHT. THE RETURNS HAVE NO POCKET: THEY GIVE WAY TO EVERY LANE.", R (words.getX(), y0 + 36, words.getWidth(), 16), fLbl, col::black, juce::Justification::centredLeft);
        }
    }
    {
        const float x0 = strip.getX() + 5, y0 = strip.getY() + 5;
        for (int k = 0; k < kPkGlobals; ++k) control (g, cx::kPkG + k, mapX + (float) k * 66, y0 + 2, 64, col::lit, 40, 16.0f);
        actKey (g, aPkAuto, mapX + 286, y0 + 2, 54, "AUTO", col::yellow, 3);
        actKey (g, aPkReset, mapX + 346, y0 + 2, 54, "RESET", col::white, 3);
        actKey (g, aMain, mapX + 406, y0 + 2, 54, "MAIN", col::white, 3);
        juce::ignoreUnused (x0);
    }
    // ---- a row for every lane, then the two returns
    for (int q = 0; q < Pockets::kParts; ++q)
    {
        const R r (23.0f, 180.0f + (float) q * 70.0f, 1682.0f, 66.0f);
        const float x0 = r.getX() + 5, y0 = r.getY() + 5;
        if (vis (g, r))
        {
            panel (g, r, col::putty);
            const bool lane = q < kLanes, active = lane && q == ses.activeLane;
            const String num = lane ? String (q + 1) : String (q == kLanes ? "A" : "B"), name = lane ? String (lanes()[(size_t) q].name) : String (q == kLanes ? "DUB" : "SPACE");
            const R dr (x0, y0, 126, 26);
            window (g, dr, active ? col::lit : col::black);
            textRaw (g, num, R (x0 + 2, y0, 26, 26), fLegend, active ? col::black : col::lit);
            textRaw (g, name, R (x0 + 32, y0, 92, 26), fLegend, active ? col::black : col::lit, juce::Justification::centredLeft);
        }
        // MUTE and SOLO, to hear a lane by itself as POCKETS leaves it (what is not heard covers nothing)
        {
            const int muteCtl = q < kLanes ? cx::lane (q, cx::kMute) : cx::kRetMS + (q - kLanes) * 2, soloCtl = muteCtl + 1;
            const Colour fills[2] = { eng.shown (muteCtl) > 0.5f ? col::amber : col::putty, eng.shown (soloCtl) > 0.5f ? col::green : col::putty };
            for (int k = 0; k < 2; ++k)
            {
                const R kr (x0 + (float) k * 25.0f, y0 + 32, 22, 22);
                const int ctl = k == 0 ? muteCtl : soloCtl;
                if (vis (g, kr))
                {
                    String letter (k == 0 ? "M" : "S");
                    if (int ch = 0, num = 0; info && ccx::addressOf (ctl, ch, num)) letter = String (num);
                    key (g, kr, fills[k], 2.0f, eng.shown (ctl) > 0.5f); textRaw (g, letter, kr, fLbl, inkOn (cap (fills[k]), col::black));
                    if (seen (g, kr)) lastShown[(size_t) ctl] = eng.shown (ctl);
                }
                hit (kr.expanded (1), hToggle, ctl);
            }
        }
        if (q < kLanes)
        {
            // where its pocket is and how wide, in figures
            const R vr (x0 + 54, y0 + 32, 104, 22);
            if (vis (g, vr)) text (g, eng.valueText (cx::pk (q, 0)) + "  " + eng.valueText (cx::pk (q, 1)), vr, fLbl, col::black, juce::Justification::centredLeft);
            if (seen (g, vr)) pkSaid[(size_t) q] = eng.target (cx::pk (q, 0)) + 2.0f * eng.target (cx::pk (q, 1));
        }
        drawPocketMap (g, q, R (mapX, r.getY() + 4, mapW, 58));
        if (q < kLanes)
        {
            hit (R (mapX, r.getY() + 4, mapW, 58), hPocket, q, 0);
            for (int k = 0; k < kPkLane; ++k) control (g, cx::pk (q, k), mapX + mapW + 8 + (float) k * 62, r.getY() + 3, 60, k < 2 ? col::yellow : col::lit, 40, 16.0f);
        }
        else if (vis (g, r))
        {
            text (g, "NO POCKET: IT GIVES WAY TO EVERY LANE,", R (mapX + mapW + 12, r.getY() + 14, 250, 16), fLbl, col::black, juce::Justification::centredLeft);
            text (g, "BY AS MUCH AS RETURNS LETS IT.", R (mapX + mapW + 12, r.getY() + 31, 250, 16), fLbl, col::black, juce::Justification::centredLeft);
        }
    }
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
        hit (r, hKnobCard, k, 0);
        if (vis (g, r)) card (g, r, sel);
        const float x0 = r.getX() + 6, y0 = r.getY() + 6;
        const String num (k + 1);
        const float wTag = std::max (30.0f, std::ceil (tw (fLegend, num)) + 8.0f);
        tag (g, R (x0, y0, wTag, 26), num);
        const float wn = plate (g, x0 + wTag, y0, kn.name.isEmpty() ? String ("UNNAMED") : kn.name, fLegend, 8, col::white, col::black);
        if (sel) selNameRect = R (x0 + wTag, y0, std::max (wn, 150.0f), 26);
        const int scope = (int) ses.knobScope (k).size();
        const String mv = "MOVES " + String (scope);
        const float mvX = r.getRight() - 6 - (std::ceil (tw (fNote, mv)) + 12);
        plate (g, mvX, y0, mv, fNote, 6, col::white, col::black);
        // (a MOTION on it says so, beside that: lit while its wave is running)
        if (const int on = ses.motionOn (k); on != 0)
        {
            String ms ("MOTION");
            for (int u = 0; u < cx::kMotUnits; ++u) if ((on >> u) & 1) ms << " " << String (u + 1);
            plate (g, mvX - 2 - (std::ceil (tw (fNote, ms)) + 12), y0, ms, fNote, 6, col::green, col::black);
        }
        // the knob itself, its depth, its snapshots in sweep order
        const float ky = y0 + 40;
        if (ses.knobLive (k))
        {
            const float v = eng.shown (cx::kSnap + k);
            lastShown[(size_t) (cx::kSnap + k)] = v;
            knob (g, x0 + 32, ky + 20, 40, v, Kind::Knob, 0, k == ses.learning ? col::yellow : kn.pick ? col::amber : col::teal);
            hit (R (x0 + 8, ky - 2, 48, 44), hSnapKnob, k, 0);
        }
        else hollow (g, x0 + 32, ky + 20, 40, true);
        legend (g, x0 + 32, ky + 43, "TURN");
        knob (g, x0 + 98, ky + 20, 40, kn.depth * 0.5f, Kind::Knob, 0, col::lit);
        hit (R (x0 + 74, ky - 2, 48, 44), hDepth, k, 0);
        legend (g, x0 + 98, ky + 43, "DEPTH");
        for (int s = 0; s < 10; ++s)
        {
            const R sr (x0 + 134 + (float) (s % 5) * 25.6f, ky + (float) (s / 5) * 30, 23, 26);
            if (s < (int) kn.snaps.size())
            {
                const int li = ses.indexOf (kn.snaps[(size_t) s]);
                window (g, sr, col::black);
                text (g, String (li + 1), sr, fLbl, col::lit);
                hit (sr, hSlot, k, s);
            }
            else { g.setColour (tone (col::sea)); g.drawRect (sr.reduced (0.5f), 1.0f); }
        }
        String order;
        for (size_t s = 0; s < kn.snaps.size(); ++s)
            if (auto* sn = ses.findSnap (kn.snaps[s])) order << (s > 0 ? "  >  " : "") << sn->name;
        g.setColour (tone (col::black)); g.setFont (fLbl);
        // (a knob tied straight to knobs says so; one being taught says that)
        const int tied = (int) kn.links.size();
        if (k == ses.learning) order = "BEING TAUGHT: " + String (ses.learnMoves()) + " MOVED. KEEP, OR DROP." + (order.isEmpty() ? String() : "   " + order);
        else if (tied > 0) order = "TIED TO " + String (tied) + (tied == 1 ? " KNOB" : " KNOBS") + (order.isEmpty() ? String (".") : ".   " + order);
        g.drawFittedText (order.isEmpty() ? String ("NOTHING ON IT. TAP SNAPSHOTS ON THE RIGHT, OR PRESS LEARN.") : order,
                          juce::Rectangle<int> ((int) x0, (int) (ky + 68), 258, 30), juce::Justification::topLeft, 2, 1.0f);
        // what it moves, lane by lane (a knob it is tied to: how far the full turn takes it, in hundredths of that knob's travel)
        String moves;
        int lastLane = -2;
        for (int i : ses.knobScope (k))
        {
            const int ln = i < cx::kRet ? i / cx::kPerLane : -1;
            if (ln != lastLane) { moves << (moves.isEmpty() ? "" : "   ") << (ln >= 0 ? String (lanes()[(size_t) ln].name) : String ("RETURNS")) << ": "; lastLane = ln; }
            else moves << ", ";
            moves << (i >= cx::kPkG && i < cx::kPkG + kPkGlobals ? "POCKETS " : cx::isPk (i) ? "POCKET " : "") << eng.control (i).label << (i >= cx::space (1, 0) && i < cx::space (1, 4) ? " (CONV)" : "");
            for (const auto& l : kn.links)
                if (l.ctl == i) { const int far = (int) std::lround (l.amount * kn.depth * 100.0f); moves << (far >= 0 ? " +" : " -") << std::abs (far); }
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
    const juce::ScopedValueSetter<bool> onOneLine (oneLine, true);      // (no room under these keys for a second line: a legend that ran on went off the plate)
    title (g, x0, y0, "KNOB " + String (selKnob + 1));
    actKey (g, aMain, x0, ky, 64, "MAIN", col::white, 5);
    actKey (g, aLibrary, x0 + 66, ky, 64, "LIBRARY", col::white, 5);
    actKey (g, aRenameKnob, x0 + 160, ky, 64, "RENAME", col::yellow, 5);
    actKey (g, aClearKnob, x0 + 226, ky, 64, "CLEAR", col::yellow, 5);
    // (a knob that holds a stack moves nothing, so there is nothing to teach it: LEARN is not there while STEPS is lit)
    actKey (g, aLearn, x0 + 292, ky, 64, ses.learning == selKnob ? "KEEP" : "LEARN", ses.learning == selKnob ? col::green : col::yellow, 5, ! ses.knobs[(size_t) selKnob].pick, ses.learning == selKnob);
    // STEPS: this knob holds a stack. It steps through its snapshots (a turn: the next, in the headphones; a press: recalled) and does not sweep.
    { const bool steps = ses.knobs[(size_t) selKnob].pick; actKey (g, aKnobPick, x0 + 358, ky, 64, "STEPS", steps ? col::amber : col::putty, 5, ses.learning != selKnob, steps); }
    // MOTION: three waves, each put on a snapshot knob. The knob swings in time with the clock, and what it moves goes with it.
    motSig = motionSig();
    for (int u = 0; u < cx::kMotUnits; ++u)
    {
        const float ux = x0 + 434 + (float) u * 254;
        const float w = title (g, ux, y0, "MOTION " + String (u + 1));
        const int target = (int) std::lround (eng.target (cx::mot (u, 2)) * 15.0f);
        const bool waveOn = eng.valueText (cx::mot (u, 0)) != "OFF", on = (ses.motionOn (target) >> u) & 1;
        // what it is set to, in words: its wave, how long, which knob (by its name), how much
        String says = eng.valueText (cx::mot (u, 0));
        if (waveOn)
        {
            const auto& nm = ses.knobs[(size_t) target].name;
            says << "  " << eng.valueText (cx::mot (u, 1)) << "  " << (nm.isEmpty() ? "KNOB " + String (target + 1) : String (target + 1) + " " + nm) << "  " << eng.valueText (cx::mot (u, 3));
        }
        const float room = 248.0f - w - 4.0f;
        while (says.length() > 4 && std::ceil (tw (fNote, says)) + 12.0f > room) says = says.dropLastCharacters (1);
        plate (g, ux + w + 2, y0, says, fNote, 6, on ? col::green : col::white, col::black);
        for (int k = 0; k < cx::kMotKnobs; ++k) control (g, cx::mot (u, k), ux + (float) k * 62, ky, 60, k == 3 ? col::teal : col::lit, 40, 15.0f);
    }
    const String help = "PICK A KNOB. TAP SNAPSHOTS ON THE RIGHT TO ADD THEM IN SWEEP ORDER (2 TO 10); TAP A NUMBERED SLOT TO TAKE IT OUT. OR PRESS LEARN, TURN THE KNOBS ITS FULL TURN SHOULD MOVE, AND KEEP. "
                        "STEPS: THE KNOB HOLDS A STACK: A TURN COMES TO THE NEXT SNAPSHOT AND PLAYS IT IN THE HEADPHONES, A PRESS RECALLS IT. "
                        "MOTION: A WAVE THAT TURNS A SNAPSHOT KNOB FOR YOU, IN TIME WITH THE CLOCK. CHOOSE ITS WAVE, HOW LONG IT IS, WHICH KNOB, AND HOW MUCH OF THE KNOB'S TURN.";
    g.setColour (tone (col::black)); g.setFont (fLbl);
    g.drawFittedText (help, juce::Rectangle<int> ((int) x0 + 1204, (int) y0 + 2, 466, 94), juce::Justification::centredLeft, 7, 0.8f);
}

// The arrangement: snapshots in a line. Along the top, the line itself: each part as long as its
// bars, the way into it a slope. Under it the parts as cards (their bars and their morph are
// dragged); beside them the library to take from; at the foot, PLAY and PRINT.
void Screen::drawArrange (G& g)
{
    const int n = (int) ses.arrangement.size();
    selPart = n == 0 ? -1 : juce::jlimit (-1, n - 1, selPart);
    const int total = std::max (1, ses.arrBars()), playing = ses.arrPart();
    const double tempo = eng.tempo();
    auto hms = [] (double sec) { const auto t = (juce::int64) std::llround (sec); return String (t / 60) + ":" + String (t % 60).paddedLeft ('0', 2); };
    auto fitted = [this] (String t, const juce::Font& f, float room) { while (t.length() > 1 && tw (f, t) > room) t = t.dropLastCharacters (1); return t; };
    arrSig = arrangeSig();
    const R line (23, 78, 1118, 98);
    panel (g, line, col::putty);
    {
        const float x0 = line.getX() + 8, w = line.getWidth() - 16, y0 = line.getY() + 24, h = 46;
        const R glass (x0, y0, w, h);
        window (g, glass, col::black);
        if (n == 0 && vis (g, glass)) text (g, "NOTHING IN IT YET. TAP SNAPSHOTS ON THE RIGHT: EACH BECOMES A PART, AFTER THE ONE THAT IS CHOSEN.", glass, fLbl, col::lit);
        for (int i = 0; i < n; ++i)
        {
            const auto& p = ses.arrangement[(size_t) i];
            const float bx = x0 + w * (float) ses.arrBarsBefore (i) / (float) total, bw = w * (float) p.bars / (float) total;
            const R br (bx, y0, bw, h);
            hit (br, hArrPart, i, 0);
            if (! vis (g, R (bx, line.getY(), bw, line.getHeight()))) continue;
            // the way into it: a slope as long as its morph, then level (the first part has no way in: it is simply there)
            const float mw = i == 0 ? 0.0f : bw * (float) (ses.arrMorphBars (i) / (double) p.bars);
            juce::Path way;
            way.startNewSubPath (bx + 1.0f, y0 + h - 3.0f); way.lineTo (bx + 1.0f + std::max (0.0f, mw - 2.0f), y0 + 7.0f); way.lineTo (bx + bw - 1.0f, y0 + 7.0f); way.lineTo (bx + bw - 1.0f, y0 + h - 3.0f);
            way.closeSubPath();
            g.setColour (i == playing ? col::green : i == selPart ? col::yellow : Colour (0xffB9B4AA));
            g.fillPath (way);
            text (g, String (ses.arrBarsBefore (i) + 1), R (bx + 2, line.getY() + 6, std::max (34.0f, bw), 16), fNote, col::black, juce::Justification::centredLeft);
            if (bw > 44.0f) if (auto* sn = ses.findSnap (p.snap))
            {
                // its name: on the level stretch where there is room for it, else over the slope (light, on the glass)
                const float level = bw - mw - 10.0f;
                if (level >= std::min (tw (fLbl, sn->name), 60.0f)) text (g, fitted (sn->name, fLbl, level), R (bx + mw + 5, y0 + 9, level + 4, h - 14), fLbl, col::black, juce::Justification::centredLeft);
                else text (g, fitted (sn->name, fLbl, bw * 0.55f), R (bx + 6, y0 + 5, bw * 0.6f, 16), fLbl, col::lit, juce::Justification::centredLeft);
            }
        }
        if (n > 0 && vis (g, line))
        {
            if (ses.arrPlaying())
            {
                // where it is
                const float px = x0 + w * (float) juce::jlimit (0.0, 1.0, ses.arrPos() / ((double) total * 4.0));
                g.setColour (col::lit); g.fillRect (R (px - 1.0f, y0 - 2.0f, 2.0f, h + 4.0f));
            }
            text (g, String (total) + (total == 1 ? " BAR  " : " BARS  ") + hms ((double) total * 4.0 * 60.0 / tempo) + " AT " + eng.valueText (cx::kTempo), R (x0, y0 + h + 4, w, 16), fNote, col::black, juce::Justification::centredRight);
            text (g, "THE LINE: EACH PART AS LONG AS ITS BARS. THE SLOPE IS ITS MORPH.", R (x0, y0 + h + 4, w, 16), fNote, col::black, juce::Justification::centredLeft);
        }
    }
    // the parts
    const int rows = 8;
    arrScroll = juce::jlimit (0, std::max (0, (n - 1) / 4 - (rows - 1)), arrScroll);
    for (int q = 0; q < rows * 4; ++q)
    {
        const int i = arrScroll * 4 + q;
        if (i >= n) break;
        const auto& p = ses.arrangement[(size_t) i];
        const R r (23.0f + (float) (q % 4) * 282, 184.0f + (float) (q / 4) * 98, 272, 92);
        hit (r, hArrPart, i, 0);
        const float x0 = r.getX() + 6, y0 = r.getY() + 6;
        const R br (x0, y0 + 34, 66, 40), mr (x0 + 70, y0 + 34, 108, 40);
        if (i > 0 || true) { hit (br, hArrBars, i, 0); if (i > 0) hit (mr, hArrMorph, i, 0); }
        if (! vis (g, r)) continue;
        card (g, r, i == selPart);
        const String num (i + 1);
        const float wTag = std::max (30.0f, std::ceil (tw (fLegend, num)) + 8.0f);
        tag (g, R (x0, y0, wTag, 24), num);
        const auto* sn = ses.findSnap (p.snap);
        const bool on = i == playing;
        plate (g, x0 + wTag, y0, fitted (sn != nullptr ? sn->name : String ("GONE"), fLegend, 150.0f), fLegend, 8, on ? col::green : col::white, col::black, 24);
        const String from = "BAR " + String (ses.arrBarsBefore (i) + 1);
        plate (g, r.getRight() - 6 - (std::ceil (tw (fNote, from)) + 12), y0, from, fNote, 6, col::white, col::black, 24);
        readout (g, br, "BARS", String (p.bars));
        // (a morph longer than the part is the whole of the part)
        const bool whole = glideBarsFor ((float) p.morph / 11.0f) > (double) p.bars;
        readout (g, mr, i == 0 ? "COMES IN" : "MORPH", i == 0 ? String ("AT ONCE") : whole ? String ("ALL OF IT") : Session::morphName (p.morph));
        text (g, hms ((double) ses.arrBarsBefore (i) * 4.0 * 60.0 / tempo), R (x0 + 184, y0 + 34, 76, 20), fLbl, col::black, juce::Justification::centredRight);
        text (g, "FOR " + hms ((double) p.bars * 4.0 * 60.0 / tempo), R (x0 + 184, y0 + 54, 76, 20), fLbl, col::black, juce::Justification::centredRight);
    }
    // the library, to take from
    const int lib = (int) ses.library.size();
    libScroll = juce::jlimit (0, std::max (0, (lib - 1) / 2 - 11), libScroll);
    for (int q = 0; q < 24; ++q)
    {
        const int i = libScroll * 2 + q;
        if (i >= lib) break;
        drawSnapCard (g, ses.library[(size_t) i], i, R (1151.0f + (float) (q % 2) * 282, 78.0f + (float) (q / 2) * 74, 272, 66), false, hPick);
    }
    const R strip (23, 980, 1682, 110);
    panel (g, strip, col::putty);
    const float x0 = strip.getX() + 5, y0 = strip.getY() + 5, ky = y0 + 40;
    const bool pr = ses.printing(), play = ses.arrPlaying();
    const juce::ScopedValueSetter<bool> onOneLine (oneLine, true);      // (no room under these keys for a second line)
    title (g, x0, y0, "ARRANGEMENT");
    actKey (g, aMain, x0, ky, 64, "MAIN", col::white, 5);
    actKey (g, aLibrary, x0 + 66, ky, 64, "LIBRARY", col::white, 5);
    actKey (g, aArrPlay, x0 + 160, ky, 64, play ? "STOP" : "PLAY", play ? col::green : col::putty, 5, n > 0, play);
    actKey (g, aArrHere, x0 + 226, ky, 64, "FROM HERE", col::yellow, 5, selPart >= 0);
    actKey (g, aArrEarlier, x0 + 320, ky, 64, "EARLIER", col::yellow, 5, selPart > 0);
    actKey (g, aArrLater, x0 + 386, ky, 64, "LATER", col::yellow, 5, selPart >= 0 && selPart < n - 1);
    actKey (g, aArrRemove, x0 + 452, ky, 64, "REMOVE", col::yellow, 5, selPart >= 0);
    actKey (g, aArrClear, x0 + 534, ky, 64, "HOLD: CLEAR", col::red, 5, n > 0);
    actKey (g, aArrPrint, x0 + 612, ky, 64, pr ? "STOP" : "PRINT", pr ? col::green : col::yellow, 5, n > 0, pr);
    if (vis (g, strip))
    {
        String now;
        if (pr) now = "PRINTING: " + String (juce::roundToInt (ses.printDone() * 100.0f)) + " OF 100. THE SOUND IS STOPPED UNTIL IT IS DONE. PRINT AGAIN, OR ESC, STOPS IT.";
        else if (play && playing >= 0)
        {
            const auto* sn = ses.findSnap (ses.arrangement[(size_t) playing].snap);
            now = "PLAYING PART " + String (playing + 1) + " OF " + String (n) + (sn != nullptr ? ": " + sn->name : String()) + ".  BAR " + String (juce::jlimit (1, total, (int) std::floor (std::max (0.0, ses.arrPos()) / 4.0) + 1)) + " OF " + String (total) + ".";
        }
        else if (play) now = "COMES IN ON THE NEXT BAR LINE.";
        else now = ses.printNote;
        g.setColour (tone (col::black)); g.setFont (fLegend);
        g.drawFittedText (now, juce::Rectangle<int> ((int) x0 + 700, (int) y0, 970, 24), juce::Justification::centredLeft, 1, 0.7f);
        const String help = "TAP A SNAPSHOT ON THE RIGHT TO ADD IT AFTER THE CHOSEN PART. DRAG A PART'S BARS, AND ITS MORPH: HOW LONG THE WAY INTO IT TAKES, FROM ITS FIRST BAR (CUT: IT IS SIMPLY THERE). "
                            "THE KNOBS TRAVEL OVER THE MORPH, PATTERNS CHANGE ON THE BAR, AND WHAT CANNOT TRAVEL IS CROSSED OVER, AS IN ANY RECALL. THE SNAPSHOT KNOBS AND THEIR MOTION PLAY OVER IT. "
                            "PRINT RENDERS IT TO FILES: THE TRACK AND ITS PARTS, IN THE FOLDER TAKES GO TO.";
        g.setFont (fLbl);
        g.drawFittedText (help, juce::Rectangle<int> ((int) x0 + 700, (int) y0 + 28, 970, 68), juce::Justification::topLeft, 4, 0.85f);
    }
}

String Screen::arrangeSig() const
{
    String s;
    s << ses.arrPart() << "|" << (ses.arrPlaying() ? (int) std::floor (ses.arrPos()) : -9) << "|" << (ses.printing() ? juce::roundToInt (ses.printDone() * 100.0f) : -1) << "|" << ses.printNote << "|" << (int) ses.arrangement.size();
    return s;
}

// What MOTION's strip says in words: when it differs, the strip is drawn again.
juce::String Screen::motionSig() const
{
    String s;
    for (int i = cx::kMot; i < cx::kSnap; ++i) s << eng.valueText (i) << "|";
    for (int k = 0; k < 16; ++k) s << ses.motionOn (k);
    return s;
}

// ---------------------------------------------------------------------------------------- input
void Screen::act (int a)
{
    const int l = ses.activeLane;
    tapAct = a; tapAt = juce::Time::getMillisecondCounter();        // (its key is seen to go down, whoever pressed it)
    switch (a)
    {
        case aRun:      eng.setRunning (! eng.isRunning()); break;
        case aPanic:    eng.panic(); break;
        case aRec:      // (a take under way is ended by holding the key: see the timer)
            if (recorder != nullptr && ! recorder->recording())
            {
                const String err = recorder->start();
                recNote = err.isNotEmpty() ? "NO RECORDING: " + err : recorder->note();
                recNoteAt = juce::Time::getMillisecondCounter();
                recWas = recorder->recording();
            }
            break;
        case aAudio:    if (onAudioSetup) onAudioSetup(); break;
        case aFull:     if (onFullScreen) onFullScreen(); break;
        case aLoad:     if (loadOpen) loadOpen = false; else openLoad(); repaint(); break;
        case aLoadUp:   if (loadDir.getParentDirectory() != loadDir) { loadDir = loadDir.getParentDirectory(); loadScroll = 0; readLoad(); } repaint(); break;
        case aCfgClose: cfgOpen = false; break;
        case aClipReset: eng.clipL = false; eng.clipR = false; break;
        case aSnapshot: selSnap = ses.take(); break;
        case aCue:      cueMode = ! cueMode; if (! cueMode) ses.cue (0); break;
        case aPhones:   eng.setPhones (! eng.phones()); repaint(); break;
        case aNotes:    ses.setRecallNotes (! ses.recallNotes); repaint(); break;
        case aCueSet:   ses.setCueInSet (! ses.cueInSet); repaint(); break;
        case aPockets:  setPage (page == pPockets ? pMain : pPockets); break;
        case aArrange:  setPage (page == pArrange ? pMain : pArrange); break;
        case aArrPlay:  if (ses.arrPlaying()) ses.arrStop(); else ses.arrPlay (0); break;
        case aArrHere:  ses.arrPlay (std::max (0, selPart)); break;
        case aArrEarlier: if (selPart > 0) { ses.arrMove (selPart, -1); --selPart; } break;
        case aArrLater: if (selPart >= 0 && selPart < (int) ses.arrangement.size() - 1) { ses.arrMove (selPart, 1); ++selPart; } break;
        case aArrRemove: if (selPart >= 0) { ses.arrRemove (selPart); selPart = std::min (selPart, (int) ses.arrangement.size() - 1); } break;
        case aArrPrint:
            if (ses.printing()) ses.printCancel();
            else
            {
                // (where takes go: the folder set in SETUP, or the usual one if that is not there)
                const juce::File root = recorder == nullptr ? Session::recordingsDir() : recorder->root.isDirectory() ? recorder->root : recorder->usual;
                if (const String err = ses.printBegin (root); err.isNotEmpty()) ses.printNote = err;
            }
            break;
        case aPkAuto:
        {
            const int n = ses.pocketsAuto();
            pkNote = n == 0 ? String ("NOTHING HAS BEEN PLAYING: AUTO GOES BY WHAT THE LANES HAVE PLAYED WHILE THIS PAGE WAS UP") : "PLACED " + String (n) + (n == 1 ? " LANE" : " LANES") + " FROM WHAT THEY HAVE BEEN PLAYING";
            pkNoteAt = juce::Time::getMillisecondCounter();
            break;
        }
        case aPkReset:  ses.pocketsReset(); pkNote = "EVERY POCKET IS WHERE THE RIG STARTS IT"; pkNoteAt = juce::Time::getMillisecondCounter(); break;
        case aOnline:   if (online != nullptr) online->setOn (! online->on()); break;
        case aOnlineSetup: if (onOnlineSetup) onOnlineSetup(); break;
        case aFetch:    if (online != nullptr && ! online->loading()) online->askLoad(); break;
        case aStack:    libStack = libStack + 1 >= (int) ses.stacks.size() ? -1 : libStack + 1; libScroll = 0; break;
        case aRecallCue:
            if (const auto id = ses.cueing() != 0 ? ses.cueing() : selSnap; ses.findSnap (id) != nullptr)
            {
                Session::Scope sc;
                if (id == scopeFor) { for (int i = 0; i < kLanes; ++i) sc.lane[(size_t) i] = scopeOn[(size_t) i]; sc.returns = scopeOn[(size_t) kLanes]; sc.bus = scopeOn[(size_t) kLanes + 1]; sc.master = scopeOn[(size_t) kLanes + 2]; }
                sc.notes = ses.recallNotes;
                selSnap = id; ses.recall (id, sc); ses.cue (0);
                scopeOn.fill (true);                    // (the next recall takes everything again)
                ses.cueWith (0, {});                    // (and the next cue is whole)
            }
            break;
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
        case aLearn:
            // LEARN on the knob that is being taught keeps it; on another, that one is kept and this one begun
            if (ses.learning == selKnob) ses.learnKeep();
            else { ses.learnBegin (selKnob); setPage (pMain); }
            break;
        case aLearnKeep: ses.learnKeep(); break;
        case aLearnDrop: ses.learnDrop(); break;
        case aClearKnob:
            if (ses.learning == selKnob) ses.learnDrop();
            ses.setPick (selKnob, false);
            ses.knobs[(size_t) selKnob].links.clear();
            ses.knobs[(size_t) selKnob].snaps.clear();
            ses.turnSnapKnob (selKnob, 0.0f);
            break;
        case aKnobPick: ses.setPick (selKnob, ! ses.knobs[(size_t) selKnob].pick); break;
        default: break;
    }
    repaint();
}

void Screen::stepTap (int i)
{
    // An unlit step lights, at velocity 75 and a whole step of gate, and is selected. A lit step is
    // selected first; a second tap puts it out, and it forgets everything it held.
    const int l = ses.activeLane;
    Pattern p = ses.pattern (l);
    if (i >= p.length) return;
    auto& s = p.steps[(size_t) i];
    const int oct = lanes()[(size_t) l].octave;
    if (! s.on) { clearStep (s, oct); s.on = true; ses.selStep = i; ses.setPattern (l, p); }
    else if (ses.selStep != i) ses.selStep = i;
    else { clearStep (s, oct); ses.setPattern (l, p); }
}

void Screen::mouseDown (const juce::MouseEvent& e)
{
    if (editor != nullptr) endEdit (true);
    grabKeyboardFocus();
    const auto p = where (e);
    const auto* h = hitAt (p);
    drag = {};
    pressAt = eyeLast = e.position;
    eyeDrag = 0;
    if (ses.printing())
    {
        // a print is being made: nothing is touched meanwhile but its own key, which stops it
        if (h != nullptr && h->kind == hAct && h->a == aArrPrint) { ses.printCancel(); repaint(); }
        return;
    }
    if (three && ! eyeLocked && (h == nullptr || e.mods.isRightButtonDown() || e.mods.isAltDown()))
    {
        // the view is unlocked and the hand is off the controls (or the right button is down): it moves the eye
        eyeDrag = e.mods.isRightButtonDown() || e.mods.isShiftDown() ? 2 : 1;
        eyeHeld = false;
        if (stage != nullptr) stage->lookAt (nullptr);
        return;
    }
    mouseIsDown = true;
    if (hovering) { hovering = false; dirty (tipRect); tipRect = {}; }      // (the box is out of the hand's way)
    if (h == nullptr) return;
    drag.kind = h->kind; drag.a = h->a; drag.b = h->b; drag.page = (int) page; drag.start = p; drag.t0 = juce::Time::getMillisecondCounter();
    switch (h->kind)
    {
        case hKnob: case hRead: drag.v0 = ses.value (h->a); break;
        case hPocket:   drag.v0 = ses.value (cx::pk (h->a, 0)); drag.g0 = ses.value (cx::pk (h->a, 1)); break;
        case hSnapKnob: drag.v0 = ses.value (cx::kSnap + h->a); if (page == pMacros) selKnob = h->a; break;
        case hDepth:    drag.v0 = ses.knobs[(size_t) h->a].depth; selKnob = h->a; break;
        case hLen:      drag.v0 = (float) ses.pattern (ses.activeLane).length; break;
        case hToggle:   ses.userSet (h->a, ses.value (h->a) > 0.5f ? 0.0f : 1.0f); break;
        case hHold:     ses.userSet (h->a, 1.0f); break;
        case hLane: case hLanePanel:    // its name, or its plate off the controls: the lane comes to the front
            if (h->kind == hLanePanel && h->a == ses.activeLane && ses.front.kind == Session::fLane) break;
            ses.bringLane (h->a, 0);
            if (cfgOpen && cfg.kind < 2) { cfgStale = true; cfgScroll = 0; }
            break;
        case hDropFx:   ses.bring ({ h->a == 1 ? (int) Session::fInsert : (int) Session::fReturns, h->a == 1 ? h->b : 0 }, 0); break;
        case hFront:    ses.bring ({ h->a, h->b }, 0); break;
        case hConfig:   openConfig ({ h->a, h->b / 8, h->b % 8 }); break;
        case hFile:     pickLoad (h->a); break;
        case hScope:
            if (h->a >= 0 && h->a < kLanes + 3)
            {
                scopeOn[(size_t) h->a] = ! scopeOn[(size_t) h->a];
                // what is in the headphones is what the recall would bring: the lanes that are lit, and no others
                Session::Scope sc;
                for (int i = 0; i < kLanes; ++i) sc.lane[(size_t) i] = scopeOn[(size_t) i];
                sc.returns = scopeOn[(size_t) kLanes]; sc.bus = scopeOn[(size_t) kLanes + 1];
                ses.cueWith (scopeFor, sc);
            }
            break;
        case hSetup:
            if (h->a < (int) cfgParams.size())
            {
                const auto& sp = cfgParams[(size_t) h->a];
                cfgName = sp.id;
                drag.v0 = sp.value;
                if (sp.kind == Kind::Toggle) eng.setSetup (cfg, sp.id, sp.value > 0.5f ? 0.0f : 1.0f);
                else if (sp.kind == Kind::Hold) eng.setSetup (cfg, sp.id, 1.0f);
                cfgStale = true;
                ses.touch();
            }
            break;
        case hDisplay:
            if (auto* pr = eng.processorFor (cfg)) pr->mod().displayMouse (p, cfgDisplay, true);
            ses.touch();
            break;
        case hInsHead:  ses.stepInsert (ses.shownLane(), h->a); cfgStale = true; break;
        case hPageSel:  ses.page = h->a; break;
        case hStep:
        {
            const Pattern pat = ses.pattern (ses.activeLane);
            if (h->a >= pat.length) { drag.kind = 0; break; }
            const auto& s = pat.steps[(size_t) h->a];
            const Step fresh;
            drag.v0 = s.on ? s.vel : fresh.vel; drag.g0 = s.on ? s.gate : fresh.gate; drag.r0 = s.on ? s.ratchet : 1;
            break;
        }
        case hAct:
            if (h->a == aDelete || h->a == aArrClear || (h->a == aRec && recorder != nullptr && recorder->recording())) break;       // these are held
            act (h->a);
            drag.fired = true;                  // (the press that starts a take is not also the hold that ends it)
            if (h->a == aRec) repaint();
            return;
        case hCard:
            selSnap = ses.library[(size_t) h->a].id;
            if (! cueMode) ses.recall (selSnap);
            else if (ses.cueing() == selSnap) ses.cue (0);
            else ses.cue (selSnap);
            break;
        case hKnobCard: selKnob = h->a; break;
        case hSlot:
        {
            auto& sn = ses.knobs[(size_t) h->a].snaps;
            selKnob = h->a;
            if (h->b < (int) sn.size()) { sn.erase (sn.begin() + h->b); ses.turnSnapKnob (h->a, ses.value (cx::kSnap + h->a)); }
            break;
        }
        case hArrPart:  selPart = h->a; break;
        case hArrBars:  selPart = h->a; drag.v0 = (float) ses.arrangement[(size_t) h->a].bars; break;
        case hArrMorph: selPart = h->a; drag.v0 = (float) ses.arrangement[(size_t) h->a].morph; break;
        case hPick:
        {
            if (page == pArrange)
            {
                // a snapshot tapped: a part after the one that is chosen (with none chosen, at the end)
                const int n = (int) ses.arrangement.size(), at = selPart >= 0 && selPart + 1 < n ? selPart + 1 : -1;
                if (ses.arrAdd (ses.library[(size_t) h->a].id, at)) selPart = at >= 0 ? at : n;
                break;
            }
            auto& sn = ses.knobs[(size_t) selKnob].snaps;
            if (sn.size() < (ses.knobs[(size_t) selKnob].pick ? 128u : 10u)) { sn.push_back (ses.library[(size_t) h->a].id); ses.turnSnapKnob (selKnob, ses.value (cx::kSnap + selKnob)); }       // (a stack: as many as the knob has places)
            break;
        }
        default: break;
    }
    repaint();
}

void Screen::mouseDrag (const juce::MouseEvent& e)
{
    if (eyeDrag != 0)
    {
        if (stage != nullptr)
        {
            const auto d = e.position - eyeLast;
            if (eyeDrag == 1) stage->turn (d.x, d.y); else stage->slide (d.x, d.y);
        }
        eyeLast = e.position;
        return;
    }
    const auto p = where (e);
    const float dx = p.x - drag.start.x, dy = drag.start.y - p.y;
    switch (drag.kind)
    {
        case hKnob: case hRead: case hSnapKnob:
        {
            const int ctl = drag.kind == hSnapKnob ? cx::kSnap + drag.a : drag.a;
            const auto& c = eng.control (ctl);
            float v;
            if (c.kind == Kind::List && c.steps() > 1) v = drag.v0 + std::round (dy / (c.steps() > 24 ? 3.0f : 14.0f)) / (float) (c.steps() - 1);       // (a long list: three pixels a place)
            else if (ctl == cx::kTempo) v = drag.v0 + dy * (e.mods.isShiftDown() ? 0.1f : 0.5f) / 140.0f;       // half a bpm a pixel
            else v = drag.v0 + dy / (e.mods.isShiftDown() ? 800.0f : 160.0f);
            ses.userSet (ctl, v);
            if (drag.kind == hSnapKnob || ctl >= cx::kTempo) repaint();
            break;
        }
        case hPocket:
        {
            // the pocket follows the hand along the map (its HOME), and widens as the hand goes up (its WIDTH)
            if (! drag.moved && std::max (std::abs (dx), std::abs (dy)) < 4.0f) break;
            drag.moved = true;
            if (drag.axis == 0) drag.axis = std::abs (dx) >= std::abs (dy) ? 'h' : 'w';
            const R mr = pkMaps[(size_t) drag.a];
            if (drag.axis == 'h' && mr.getWidth() > 1.0f) ses.userSet (cx::pk (drag.a, 0), drag.v0 + dx * (float) Pockets::kBands / (mr.getWidth() * (float) (Pockets::kBands - 1)));
            else if (drag.axis == 'w') ses.userSet (cx::pk (drag.a, 1), drag.g0 + dy / (e.mods.isShiftDown() ? 800.0f : 160.0f));
            dirty (mr);
            break;
        }
        case hSetup:
            if (drag.a < (int) cfgParams.size())
            {
                const auto& sp = cfgParams[(size_t) drag.a];
                if (sp.kind == Kind::Toggle || sp.kind == Kind::Hold) break;
                const int nc = sp.choices.size();
                const float v = sp.kind == Kind::List && nc > 1 ? drag.v0 + std::round (dy / 14.0f) / (float) (nc - 1)
                                                               : drag.v0 + dy / (e.mods.isShiftDown() ? 800.0f : 160.0f);
                eng.setSetup (cfg, sp.id, v);
                cfgStale = true;
                ses.touch();
                repaint();
            }
            break;
        case hDisplay:
            if (auto* pr = eng.processorFor (cfg)) pr->mod().displayMouse (p, cfgDisplay, false);
            dirty (cfgDisplay);
            break;
        case hDepth:
            ses.knobs[(size_t) drag.a].depth = juce::jlimit (0.0f, 2.0f, drag.v0 + dy / 80.0f);
            ses.turnSnapKnob (drag.a, ses.value (cx::kSnap + drag.a));
            repaint();
            break;
        case hArrBars:      // (ten points of the hand to a bar)
            if (drag.a < (int) ses.arrangement.size()) { ses.arrSet (drag.a, juce::jlimit (1, Session::kArrMaxBars, (int) std::lround (drag.v0 + dy / 10.0f)), -1); repaint(); }
            break;
        case hArrMorph:
            if (drag.a < (int) ses.arrangement.size()) { ses.arrSet (drag.a, -1, juce::jlimit (0, 11, (int) std::lround (drag.v0 + dy / 22.0f))); repaint(); }
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
            if (! s.on) { clearStep (s, lanes()[(size_t) ses.activeLane].octave); s.on = true; }
            if (drag.axis == 'v') s.vel = juce::jlimit (0.15f, 1.0f, drag.v0 + dy / 60.0f);
            else
            {
                // The gate follows the hand: a step's pitch is a step of gate. It stops at the step's own edge and at the
                // next lit step's edge; a little past that edge it is legato into that step.
                const float dist = (float) stepsToNext (pat, i);
                const float mx = drag.r0 == 1 ? dist + 0.25f : 1.0f;
                float gt = drag.g0 + dx / 45.0f;
                if (std::abs (gt - 1.0f) < 0.07f) gt = 1.0f;
                if (std::abs (gt - dist) < 0.07f) gt = dist;
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
    if (eyeDrag != 0)
    {
        eyeDrag = 0;
        if (stage != nullptr) eyePose = stage->home();
        if (onLook) onLook();
        return;
    }
    mouseIsDown = false;
    if (drag.kind == hHold) ses.userSet (drag.a, 0.0f);
    if (drag.kind == hSetup && drag.a < (int) cfgParams.size() && cfgParams[(size_t) drag.a].kind == Kind::Hold) eng.setSetup (cfg, cfgParams[(size_t) drag.a].id, 0.0f);
    if (drag.kind == hDisplay) if (auto* pr = eng.processorFor (cfg)) pr->mod().displayMouseUp();
    cfgStale = true;
    if (drag.kind == hStep && ! drag.moved) stepTap (drag.a);
    drag.kind = 0;
    repaint();
}

void Screen::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const auto* h = hitAt (where (e)))
    {
        if (h->kind == hKnob) ses.userSet (h->a, eng.control (h->a).def);
        else if (h->kind == hPocket) for (int k = 0; k < 2; ++k) ses.userSet (cx::pk (h->a, k), eng.control (cx::pk (h->a, k)).def);
        else if (h->kind == hSnapKnob) ses.pressSnapKnob (h->a);       // press: its nearest snapshot becomes the base
        else if (h->kind == hDepth) { ses.knobs[(size_t) h->a].depth = 1.0f; ses.turnSnapKnob (h->a, ses.value (cx::kSnap + h->a)); }
        else if (h->kind == hArrBars) ses.arrSet (h->a, 8, -1);            // (eight bars; cut)
        else if (h->kind == hArrMorph) ses.arrSet (h->a, -1, 0);
        else if (h->kind == hSetup && h->a < (int) cfgParams.size())
        {
            const auto& sp = cfgParams[(size_t) h->a];
            if (sp.kind == Kind::Knob || sp.kind == Kind::Bipolar || sp.kind == Kind::List) { eng.setSetup (cfg, sp.id, sp.def); cfgStale = true; ses.touch(); }
        }
        repaint();
    }
}

void Screen::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (page == pArrange && where (e).x < 1146.0f) { arrScroll = std::max (0, arrScroll - (w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0)); repaint(); return; }      // (its parts; the library beside them, below)
    if (page == pLibrary || page == pMacros || page == pArrange) { libScroll = std::max (0, libScroll - (w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0)); repaint(); return; }
    if (cfgOpen && loadOpen && cfgRect.contains (where (e))) { loadScroll = std::max (0, loadScroll - (w.deltaY > 0 ? 3 : w.deltaY < 0 ? -3 : 0)); repaint(); return; }
    if (cfgOpen && cfgRect.contains (where (e))) { cfgScroll = std::max (0, cfgScroll - (w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0)); repaint(); return; }
    const auto* h = hitAt (where (e));
    if (three && ! eyeLocked && stage != nullptr && (h == nullptr || ! (h->kind == hKnob || h->kind == hSnapKnob || h->kind == hRead)))
    {
        // the view is unlocked and the wheel is off the knobs: nearer or further
        stage->closer (w.deltaY * (w.isSmooth ? 6.0f : 2.5f));
        eyePose = stage->home();
        if (onLook) onLook();
        return;
    }
    if (h != nullptr)
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

void Screen::mouseMagnify (const juce::MouseEvent&, float by)
{
    // two fingers apart on the trackpad: nearer (with the view unlocked)
    if (! three || eyeLocked || stage == nullptr || by <= 0.0f) return;
    stage->closer (std::log (by) / std::log (1.0f / 0.9f));
    eyePose = stage->home();
    if (onLook) onLook();
}

bool Screen::keyPressed (const juce::KeyPress& k)
{
    if (ses.printing()) { if (k == juce::KeyPress::escapeKey) { ses.printCancel(); repaint(); } return true; }      // (a print is being made: nothing else is taken)
    if (k == juce::KeyPress::spaceKey) { act (aRun); return true; }
    // The 3D stage. 3: on and off. V: the next view (from above, the player's, from the corner).
    // K: unlock the view, so that a drag off the controls turns it, and lock it where it is put.
    // M: whether the eye goes to what a controller moves.
    if (k.getTextCharacter() == '3') { setThree (! wantThree); if (onLook) onLook(); return true; }
    if (wantThree && (k.getTextCharacter() == 'v' || k.getTextCharacter() == 'V'))
    {
        eyePose = Stage::Pose();
        eyePose.view = stage != nullptr && stage->home().view >= 0 ? (stage->home().view + 1) % 3 : 1;
        eyePose.turn = eyePose.view == 2 ? -30.0f : 0.0f; eyePose.tilt = eyePose.view == 0 ? 89.9f : eyePose.view == 1 ? 56.0f : 31.0f;
        if (stage != nullptr) { stage->lookAt (nullptr); stage->setHome (eyePose, true); }
        followOn = false; eyeHeld = false;
        say (eyePose.view == 0 ? "FROM ABOVE" : eyePose.view == 1 ? "THE PLAYER'S VIEW" : "FROM THE CORNER");
        if (onLook) onLook();
        return true;
    }
    if (wantThree && (k.getTextCharacter() == 'k' || k.getTextCharacter() == 'K')) {
        eyeLocked = ! eyeLocked;
        if (stage != nullptr) eyePose = stage->home();
        say (eyeLocked ? "VIEW LOCKED" : "VIEW UNLOCKED.  DRAG OFF THE CONTROLS: TURN IT.  SHIFT-DRAG: SLIDE IT.  SCROLL: NEARER.  K: LOCK IT.");
        if (onLook) onLook();
        return true;
    }
    if (wantThree && (k.getTextCharacter() == 'm' || k.getTextCharacter() == 'M')) {
        eyeFollow = ! eyeFollow;
        if (! eyeFollow && stage != nullptr) { followOn = false; eyeHeld = false; stage->lookAt (nullptr); }
        say (eyeFollow ? "THE EYE FOLLOWS THE CONTROLLER" : "THE EYE STAYS HOME");
        if (onLook) onLook();
        return true;
    }
    if (k == juce::KeyPress::returnKey && ses.learning >= 0) { act (aLearnKeep); repaint(); return true; }
    if (k == juce::KeyPress::escapeKey)
    {
        if (page != pMain) setPage (pMain);
        else if (cfgOpen && loadOpen) { loadOpen = false; repaint(); }
        else if (cfgOpen) { cfgOpen = false; repaint(); }
        else if (three && eyeHeld) eyeHeld = false;                    // (the eye goes home)
        else if (onLeaveFullScreen) onLeaveFullScreen();
        return true;
    }
    if (k.getTextCharacter() == 'f' || k.getTextCharacter() == 'F') { if (onFullScreen) onFullScreen(); return true; }
    if (k.getTextCharacter() == 'l' || k.getTextCharacter() == 'L') { if (wantThree) setThree (false); setLit (! lit); if (onLook) onLook(); return true; }
    if (k.getTextCharacter() == 'i' || k.getTextCharacter() == 'I') { setInfo (! info); return true; }
    if (k.getTextCharacter() == 'h' || k.getTextCharacter() == 'H') { act (aPhones); return true; }
    if (k.getTextCharacter() == 'n' || k.getTextCharacter() == 'N') { act (aNotes); return true; }
    if (k.getTextCharacter() == 'p' || k.getTextCharacter() == 'P') { act (aPockets); return true; }
    if (k.getTextCharacter() == 'a' || k.getTextCharacter() == 'A') { act (aArrange); return true; }
    if (k.getTextCharacter() == 'd' || k.getTextCharacter() == 'D')
    {
        // the dark instrument, which is the lit skin's: from the flat drawing D goes to it
        if (! lit) { setLit (true); setDark (true); } else setDark (! dark);
        if (onLook) onLook();
        return true;
    }
    if (k == juce::KeyPress::leftKey)  { ses.page = std::max (0, ses.page - 1); repaint(); return true; }
    if (k == juce::KeyPress::rightKey) { ses.page = std::min (7, ses.page + 1); repaint(); return true; }
    if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey)
    {
        ses.bringLane (juce::jlimit (0, kLanes - 1, ses.activeLane + (k == juce::KeyPress::upKey ? -1 : 1)), 1);
        if (cfgOpen && cfg.kind < 2) { cfgStale = true; cfgScroll = 0; }
        repaint();
        return true;
    }
    // TAB: the next module comes to the front, in the order the screen has them (SHIFT: the one
    // before). C: CONFIG for what is in front, and shut again.
    if (k.getKeyCode() == juce::KeyPress::tabKey && page == pMain)
    {
        const auto all = ses.stops();
        const int at = juce::jmax (0, Session::stopIndex (all, ses.stopNow()));
        ses.bringStop (all[(size_t) juce::jlimit (0, (int) all.size() - 1, at + (k.getModifiers().isShiftDown() ? -1 : 1))], 1);
        repaint();
        return true;
    }
    if ((k.getTextCharacter() == 'c' || k.getTextCharacter() == 'C') && page == pMain) { ses.configOfFront (1); repaint(); return true; }
    return false;
}

void Screen::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (files.size() == 0 || page != pMain) return;
    const auto p = three && stage != nullptr ? stage->unitsAt ({ (float) x, (float) y }) : logical ({ (float) x, (float) y });
    // Where it lands. An open CONFIG panel takes it for whatever that panel is set to (a lane, an
    // insert, a return). Else the panel under the pointer: a lane, an insert that takes files, the
    // returns (for SPACE). Anywhere else, the active lane.
    Engine::Target t { 0, ses.activeLane, 0 };
    if (cfgOpen && cfgRect.contains (p)) t = cfg;
    else
        for (size_t i = hits.size(); i-- > 0;)
        {
            const auto& h = hits[i];
            if (! h.r.contains (p)) continue;
            if (h.kind == hLanePanel) { t = { 0, h.a, 0 }; break; }
            if (h.kind == hDropFx)
            {
                const Engine::Target fx { h.a, ses.activeLane, h.b };
                if (eng.acceptsFiles (fx)) t = fx;
                break;
            }
        }
    const bool takes = eng.acceptsFiles (t);
    const bool ok = takes && eng.loadFiles (t, t.kind == 0 ? juce::StringArray (files[0]) : files);
    cfgOpen = false;
    openConfig (t);
    if (ok) cfgNote = {};
    else if (! takes) cfgNote = t.kind == 0 ? "THIS LANE'S MODULE TAKES NO AUDIO FILE." : "THIS EFFECT TAKES NO AUDIO FILE.";
    else cfgNote = "COULD NOT READ " + juce::File (files[0]).getFileName().toUpperCase() + ". IF MACOS ASKED FOR ACCESS TO THE FOLDER, ALLOW IT AND DROP THE FILE AGAIN.";
    if (ok) ses.touch();
    repaint();
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
    if (three && stage != nullptr)
    {
        // Over the 3D stage nothing of the screen's own can be drawn, so the box is a small window
        // of its own, put where the name is.
        const float pts = pointsPerUnit();
        juce::Rectangle<int> box (0, 0, juce::roundToInt (where.getWidth() * pts), juce::roundToInt (where.getHeight() * pts));
        box.setCentre (stage->pointsOf (where.getCentreX(), where.getCentreY(), Parts::kPlate).roundToInt());
        box = box.constrainedWithin (getLocalBounds());
        editor->setFont (fLegend.withHeight ((float) box.getHeight() * 0.8f));
        editor->addToDesktop (juce::ComponentPeer::windowIsTemporary);
        editor->setBounds (localAreaToGlobal (box));
        editor->setVisible (true);
        editor->toFront (true);
    }
    else addAndMakeVisible (*editor);
    editor->grabKeyboardFocus();
}

void Screen::endEdit (bool keep)
{
    if (editor == nullptr) return;
    auto ed = std::move (editor);
    auto fn = std::move (editDone);
    editDone = nullptr;
    if (keep && fn) fn (ed->getText());
    if (ed->isOnDesktop()) ed->removeFromDesktop(); else removeChildComponent (ed.get());
    repaint();
}

// What the top bar shows that can change without a hand on it: RUN and the tempo, under an outside clock.
String Screen::learnTitle() const
{
    if (ses.learning < 0) return {};
    const auto& kn = ses.knobs[(size_t) ses.learning];
    return "LEARNING " + String (ses.learning + 1) + (kn.name.isNotEmpty() ? " " + kn.name : String());
}

float Screen::learnKeysX() const
{
    return 23.0f + std::ceil (tw (fFlag, "SF-000")) + 28.0f + 12.0f + std::ceil (tw (fLegend, learnTitle())) + 16.0f + 14.0f;
}

juce::uint64 Screen::barNow() const
{
    const bool rec = recorder != nullptr && recorder->recording();
    const bool note = recNote.isNotEmpty() && juce::Time::getMillisecondCounter() - recNoteAt < 8000;
    return (juce::uint64) (eng.isRunning() ? 1 : 0) | (juce::uint64) (eng.following() ? 2 : 0)
         | ((juce::uint64) (juce::uint32) std::lround (eng.target (cx::kTempo) * 14000.0f) << 2)
         | ((juce::uint64) (rec ? 1 + ((juce::int64) recorder->seconds() & 0xfffff) : 0) << 20)
         | ((juce::uint64) (rec && recorder->lostSeconds() > 0.0 ? 1 : 0) << 41) | ((juce::uint64) (note ? 1 : 0) << 42)
         | ((juce::uint64) (ses.learning + 1) << 43) | ((juce::uint64) (ses.learning >= 0 ? ses.learnMoves() : 0) << 48);      // (a knob being taught, and how many it has been shown)
}

// What the MASTER panel's meters and numbers show, to the pixel: it is drawn again only when this changes.
juce::uint64 Screen::metersNow() const
{
    juce::uint64 h = 1469598103934665603ull;
    auto add = [&h] (float v, float steps) { h = (h ^ (juce::uint64) (juce::int64) std::lround (std::max (-61.0f, v) * steps)) * 1099511628211ull; };
    for (auto& d : eng.laneDb) add (d.load(), 4.0f);
    add (eng.peakL.load(), 4.0f); add (eng.peakR.load(), 4.0f);
    add (eng.gainRed.load(), 10.0f); add (eng.lufs.load(), 10.0f);
    add (eng.fault.load() ? 1.0f : 0.0f, 1.0f); add (eng.clipL.load() ? 1.0f : 0.0f, 1.0f); add (eng.clipR.load() ? 1.0f : 0.0f, 1.0f);
    add ((float) ses.activeLane, 1.0f);
    return h;
}

// ---------------------------------------------------------------------------------- the 3D stage
void Screen::settled() { if (! settledSaid) { settledSaid = true; if (onThreeSettled) onThreeSettled(); } }
void Screen::sayOnBar (const juce::String& words) { recNote = words; recNoteAt = juce::Time::getMillisecondCounter(); juce::Component::repaint(); }

// Asked for, in front of the player, and still not up after six seconds: it is not coming (a
// context that was made and never drew would otherwise be a black window for good). The lit
// skin comes back and says so. True: it gave up just now.
bool Screen::watchStart (juce::uint32 now, bool inFront)
{
    if (! wantThree || three || stage == nullptr || stage->state() != Stage::State::starting) { startingSince = 0; return false; }
    if (! inFront) { startingSince = 0; return false; }
    if (startingSince == 0) { startingSince = now; return false; }
    if (now - startingSince <= 6000) return false;
    stage->giveUp ("OpenGL did not start within six seconds");
    stageState();
    return true;
}

void Screen::setThree (bool on)
{
    wantThree = on;
    stageNote = {};
    if (on)
    {
        settledSaid = false; startingSince = 0;
        if (onThreeStarting) onThreeStarting();
        lit = true;
        if (stage == nullptr) { stage = std::make_unique<Stage> (eng); stage->onState = [this] { stageState(); }; }
        stage->setDark (dark);
        stage->setGlow (glowHue, glowBright, glowPulse);
        stage->setHome (eyePose, false);
        stage->attachTo (*this);
        stageState();
    }
    else
    {
        three = false;
        cancelPendingUpdate();
        if (stage != nullptr) stage->detach();
        settled();
        warm = false;
        setOpaque (true);
        juce::Component::repaint();
    }
}

void Screen::stageState()
{
    if (stage == nullptr || ! wantThree) return;
    if (stage->state() == Stage::State::failed)
    {
        // it could not start here: the lit skin stays, and says why
        stageNote = stage->whyNot();
        recNote = "3D COULD NOT START: " + stageNote.toUpperCase(); recNoteAt = juce::Time::getMillisecondCounter();        // (said on the bar, for a few seconds)
        wantThree = three = false;
        stage->detach();
        warm = false;
        juce::Component::repaint();
        if (onLook) onLook();
        settled();
        return;
    }
    if (stage->state() == Stage::State::running && three)
    {
        // (its picture was made again, as when the window moves to another screen: it is given everything again)
        sheetAll = true;
        refreshStage();
    }
    if (stage->state() == Stage::State::running && ! three)
    {
        three = true;
        sheetAll = true;
        for (int i = 0; i < cx::kCount; ++i) followWas[(size_t) i] = eng.stands (i);
        bySequencer.fill (false);
        followOn = false; tagsWere = {};
        refreshStage();
        say ("3D.  V: THE NEXT VIEW.  K: UNLOCK THE VIEW.  M: FOLLOW.  3: BACK.");
    }
}

Screen::Eye Screen::eye() const { return { stage != nullptr && wantThree ? stage->home() : eyePose, eyeLocked, eyeFollow }; }
void Screen::setEye (const Eye& e) { eyePose = e.pose; eyeLocked = e.locked; eyeFollow = e.follow; if (stage != nullptr) stage->setHome (eyePose, false); }

juce::Point<float> Screen::where (const juce::MouseEvent& e) const
{
    if (! three || stage == nullptr) return logical (e.position);
    // A knob in the hand turns by how far the hand has moved, as on the flat screen, however near
    // the eye is. Anything else is wherever the pointer is on the instrument.
    if (mouseIsDown && (drag.kind == hKnob || drag.kind == hRead || drag.kind == hSnapKnob || drag.kind == hDepth || drag.kind == hLen || drag.kind == hSetup || drag.kind == hArrBars || drag.kind == hArrMorph))
        return drag.start + (e.position - pressAt) / std::max (0.05f, pointsPerUnit());
    return stage->unitsAt (e.position);
}

void Screen::handleAsyncUpdate() { refreshStage(); }

Screen::R Screen::panelOf (juce::Point<float> p) const
{
    R found;
    for (const auto& pl : recorded.plates)
        if (pl.r.contains (p) && pl.clip.has (p.x, p.y))
            found = pl.r.getIntersection (R::leftTopRightBottom (std::max (pl.clip.x0, 0.0f), std::max (pl.clip.y0, 0.0f), std::min (pl.clip.x1, kW), std::min (pl.clip.y1, kH)));
    return found.isEmpty() ? R (p.x - 150.0f, p.y - 50.0f, 300.0f, 110.0f).getIntersection (R (0, 0, kW, kH)) : found;
}

void Screen::followMoves()
{
    if (stage == nullptr) return;
    const auto now = juce::Time::getMillisecondCounter();
    std::vector<R> direct, swept;
    for (const auto& h : hits)
    {
        if (h.kind != hKnob && h.kind != hSnapKnob) continue;
        const int ctl = h.kind == hSnapKnob ? cx::kSnap + h.a : h.a;
        if (ctl < 0 || ctl >= cx::kCount) continue;
        const auto i = (size_t) ctl;
        const float v = eng.stands (ctl);          // (not what MOTION adds: a wave on a snapshot knob moves lights, not the eye)
        const bool travelling = std::abs (v - followWas[i]) > 1.0e-4f || eng.isGliding (ctl);
        followWas[i] = v;
        const int by = ses.movedBy[i].load();
        const bool recent = now - ses.movedAt[i].load() < 900u;
        // a sequencer's own move is its own from its last message until the knob has come to rest
        if (by == 2 && recent) bySequencer[i] = true; else if (! travelling) bySequencer[i] = false;
        if (bySequencer[i] || ctl >= cx::kSnap) continue;       // (a snapshot knob itself: what it moves is what is shown)
        if (mouseIsDown && drag.kind == hKnob && drag.a == ctl) continue;       // (in the hand on the screen: the hand is already there)
        if (by == 1 && recent) direct.push_back (h.r);
        else if (travelling) swept.push_back (h.r);
    }
    // Something chosen from a controller or the keyboard (a page turned to, the wheel's second
    // press, TAB, CONFIG opened or shut): the eye goes to it and stays. (Not for the mouse: the
    // hand is already there. But a module the mouse brings to the front while the eye is held
    // is what it holds from then on.) While the pointer is up the eye is home, to show every
    // module; put down with nothing chosen, it goes back to what it held.
    if (ses.frontCount != frontSeen) { frontSeen = ses.frontCount; if (ses.frontBy != 0) { eyeHeld = true; heldBefore = true; } }
    if (ses.pointing != wasPointing)
    {
        wasPointing = ses.pointing;
        if (ses.pointing) { heldBefore = eyeHeld; eyeHeld = false; } else eyeHeld = heldBefore;
    }
    // several moving together with no hand on them: a snapshot. One or two under a hand on the controller: that module.
    const auto& use = swept.size() >= 3 ? swept : direct;
    const bool free = eyeFollow && eyeDrag == 0;
    R want;                                     // where the eye should be (empty: home)
    if (free && ! use.empty() && ! ses.pointing)
    {
        R box;
        for (const auto& r : use) { const R p = panelOf (r.getCentre()); box = box.isEmpty() ? p : box.getUnion (p); }
        box = box.expanded (10.0f).getIntersection (R (0, 0, kW, kH));
        // (nearly all of it, and home is a view of all of it: home)
        knobHas = ! (box.getWidth() * box.getHeight() > 0.7f * kW * kH && stage->home().view >= 0);
        knobAt = now; followAt = now;
        if (knobHas) want = box;
        // (a knob of what the eye already holds: it stays as it is, CONFIG and all)
        if (eyeHeld && page == pMain) if (const R held = frontBox(); ! held.isEmpty() && held.contains (box)) { want = held; knobHas = true; }
    }
    else if (free && ! ses.pointing && knobAt != 0 && now - knobAt <= 2200u) { if (knobHas) want = followBox; }       // (it stays a moment after the hand stops)
    else if (free && eyeHeld && ! ses.pointing && page == pMain) { knobAt = 0; want = frontBox(); }       // (looked at afresh each time: the rows travel for a moment after CONFIG opens or the lane changes)
    else knobAt = 0;
    if (want.isEmpty()) { if (followOn) { followOn = false; stage->lookAt (nullptr); } }
    else if (! followOn || want != followBox) { followBox = want; followOn = true; stage->lookAt (&want); }
}

void Screen::say (const String& words) { sayText = words; sayAt = juce::Time::getMillisecondCounter(); }

// A moved knob's tag: its name and where it is, its value as the engine reads it, and a bar for how far round it stands.
juce::Image Screen::tagPicture (int ctl)
{
    const auto& c = eng.control (ctl);
    String where, label; const char* what = nullptr;
    describe (ctl, where, label, what);
    if (ctl >= cx::kSnap) { label = ses.knobs[(size_t) (ctl - cx::kSnap)].name; where = "SNAPSHOTS"; }
    int ch = 0, num = 0;
    if (ccx::addressOf (ctl, ch, num)) where << (where.isEmpty() ? "" : "  ") << "CH " << ch << " CC " << num;
    const String value = ctl >= cx::kSnap ? String (juce::roundToInt (eng.shown (ctl) * 100.0f)) : eng.valueText (ctl);
    const float v = juce::jlimit (0.0f, 1.0f, eng.shown (ctl));
    const String key = label + "|" + value + "|" + where + "|" + String (juce::roundToInt (v * 200.0f)) + (eng.isGliding (ctl) ? "g" : "");
    auto& pic = tagPics[ctl];
    if (pic.key == key && pic.image.isValid()) return pic.image;
    // (light on dark whatever the instrument: it floats over it)
    Colour accent = col::lit;
    for (const auto& k : recorded.knobs) if (k.ctl == ctl) accent = Colour (k.light);
    if (eng.isGliding (ctl)) accent = col::green;
    if (accent != col::lit) accent = accent.withMultipliedSaturation (1.1f).withMultipliedBrightness (1.5f);
    const float s = stage != nullptr ? stage->tagScale() : 2.0f;
    const float wide = std::max (150.0f, std::max (tw (fLbl, label) + tw (fRead, value) + 44.0f, tw (fNote, where) + 24.0f)), tall = 58.0f;
    juce::Image img (juce::Image::ARGB, juce::roundToInt (wide * s), juce::roundToInt (tall * s), true, juce::SoftwareImageType());
    {
        G g (img);
        g.addTransform (juce::AffineTransform::scale (s));
        const R b (0.5f, 0.5f, wide - 1.0f, tall - 1.0f);
        g.setColour (Colour (0xee0D0E0E)); g.fillRoundedRectangle (b, 4.0f);
        g.setColour (Colour (0x38D8D3C8)); g.drawRoundedRectangle (b, 4.0f, 1.0f);
        g.setColour (accent); g.fillRoundedRectangle (R (0.5f, 0.5f, wide - 1.0f, 2.5f), 1.2f);
        g.setColour (night::ink); g.setFont (fLbl); g.drawText (label, R (11, 9, wide - 22, 16), juce::Justification::centredLeft, false);
        g.setColour (night::dim); g.setFont (fNote.withHeight (11.5f)); g.drawText (where, R (11, 26, wide - 22, 13), juce::Justification::centredLeft, false);
        g.setColour (accent); g.setFont (fRead.withHeight (24.0f)); g.drawText (value, R (11, 7, wide - 22, 30), juce::Justification::centredRight, false);
        const R track (11, tall - 12.0f, wide - 22, 3.0f);
        g.setColour (Colour (0x30D8D3C8)); g.fillRoundedRectangle (track, 1.5f);
        const float lo = c.kind == Kind::Bipolar ? std::min (v, 0.5f) : 0.0f, hi = c.kind == Kind::Bipolar ? std::max (v, 0.5f) : v;
        g.setColour (accent); g.fillRoundedRectangle (R (track.getX() + track.getWidth() * lo, track.getY(), std::max (2.0f, track.getWidth() * (hi - lo)), 3.0f), 1.5f);
    }
    pic.key = key; pic.image = img; pic.stamp = ++tagStamp;
    return img;
}

void Screen::showTags()
{
    if (stage == nullptr) return;
    const auto now = juce::Time::getMillisecondCounter();
    std::vector<Stage::Tag> out;
    String sig;
    tagsUp.clear();
    // knobs in the hand on the screen, or under a hand on the controller: the three moved last
    struct Moved { int ctl; R r; juce::uint32 at; };
    std::vector<Moved> moved;
    for (const auto& h : hits)
    {
        if (h.kind != hKnob && h.kind != hSnapKnob) continue;
        const int ctl = h.kind == hSnapKnob ? cx::kSnap + h.a : h.a;
        if (ctl < 0 || ctl >= cx::kCount) continue;
        const bool inHand = mouseIsDown && ((drag.kind == hKnob && drag.a == ctl) || (drag.kind == hSnapKnob && cx::kSnap + drag.a == ctl));
        const bool onController = ses.movedBy[(size_t) ctl].load() == 1 && now - ses.movedAt[(size_t) ctl].load() < 1300u;
        if (inHand || onController) moved.push_back ({ ctl, h.r, inHand ? now : ses.movedAt[(size_t) ctl].load() });
    }
    std::sort (moved.begin(), moved.end(), [] (const Moved& a, const Moved& b) { return a.at > b.at; });
    if (moved.size() > 3) moved.resize (3);
    for (const auto& m : moved)
    {
        Stage::Tag t;
        t.id = 100 + m.ctl;
        t.image = tagPicture (m.ctl);
        t.stamp = tagPics[m.ctl].stamp;
        t.x = m.r.getCentreX(); t.y = m.r.getCentreY();
        t.height = stage->heightAt (t.x, t.y);
        t.reach = 19.5f;
        for (const auto& k : recorded.knobs) if (std::abs (k.cx - t.x) < 3.0f && std::abs (k.cy - t.y) < 3.0f) t.reach = k.big ? 19.5f : 13.65f;
        sig << t.id << ":" << (int) t.stamp << " ";
        tagsUp.push_back (m.ctl);
        out.push_back (std::move (t));
    }
    // INFO's box, by the pointer
    if (info && hovering && ! mouseIsDown && tip.any)
    {
        const String key = tip.title + tip.body.joinIntoString ("|") + tip.midi.joinIntoString ("|");
        auto& pic = tagPics[-1];
        if (pic.key != key || ! pic.image.isValid())
        {
            const float s = stage->tagScale();
            const R b = tip.box;
            juce::Image img (juce::Image::ARGB, juce::roundToInt (b.getWidth() * s) + 2, juce::roundToInt (b.getHeight() * s) + 2, true, juce::SoftwareImageType());
            {
                G g (img);
                g.addTransform (juce::AffineTransform::translation (-b.getX(), -b.getY()).scaled (s));
                drawTip (g);
            }
            pic.key = key; pic.image = img; pic.stamp = ++tagStamp;
        }
        Stage::Tag t;
        t.id = 1; t.image = pic.image; t.stamp = pic.stamp; t.place = Stage::Tag::byPointer; t.px = hoverAt;
        sig << "tip:" << (int) t.stamp << ":" << juce::roundToInt (hoverAt.x) << "," << juce::roundToInt (hoverAt.y) << " ";
        tagsUp.push_back (-1);
        out.push_back (std::move (t));
    }
    // a line of words at the foot of the window, for a moment
    if (sayText.isNotEmpty() && now - sayAt < 3200u)
    {
        auto& pic = tagPics[-2];
        if (pic.key != sayText || ! pic.image.isValid())
        {
            const float s = stage->tagScale(), wide = tw (fLegend, sayText) + 36.0f, tall = 34.0f;
            juce::Image img (juce::Image::ARGB, juce::roundToInt (wide * s), juce::roundToInt (tall * s), true, juce::SoftwareImageType());
            {
                G g (img);
                g.addTransform (juce::AffineTransform::scale (s));
                g.setColour (Colour (0xee0D0E0E)); g.fillRoundedRectangle (R (0.5f, 0.5f, wide - 1.0f, tall - 1.0f), 4.0f);
                g.setColour (Colour (0x38D8D3C8)); g.drawRoundedRectangle (R (0.5f, 0.5f, wide - 1.0f, tall - 1.0f), 4.0f, 1.0f);
                g.setColour (night::ink); g.setFont (fLegend); g.drawText (sayText, R (0, 0, wide, tall), juce::Justification::centred, false);
            }
            pic.key = sayText; pic.image = img; pic.stamp = ++tagStamp;
        }
        Stage::Tag t;
        t.id = 2; t.image = pic.image; t.stamp = pic.stamp; t.place = Stage::Tag::fixed; t.px = { (float) getWidth() * 0.5f, (float) getHeight() - 22.0f };
        sig << "say:" << (int) t.stamp << " ";
        tagsUp.push_back (-2);
        out.push_back (std::move (t));
    }
    // the keyboard's keys, while INFO is on: a tag at the foot of the window, left or right
    if (info)
    {
        auto& pic = tagPics[-3];
        const auto kb = keysBox();
        if (! pic.image.isValid())
        {
            const float s = stage->tagScale();
            juce::Image img (juce::Image::ARGB, juce::roundToInt (kb.getWidth() * s), juce::roundToInt (kb.getHeight() * s), true, juce::SoftwareImageType());
            {
                G g (img);
                g.addTransform (juce::AffineTransform::scale (s));
                drawKeys (g, kb.withZeroOrigin(), true);
            }
            pic.key = "keys"; pic.image = img; pic.stamp = ++tagStamp;
        }
        Stage::Tag t;
        t.id = 3; t.image = pic.image; t.stamp = pic.stamp; t.place = Stage::Tag::fixed;
        const float half = kb.getWidth() * 0.5f + 16.0f;
        t.px = { keysRight ? (float) getWidth() - half : half, (float) getHeight() - 16.0f };
        sig << "keys:" << (keysRight ? 1 : 0) << " ";
        tagsUp.push_back (-3);
        out.push_back (std::move (t));
    }
    if (sig == tagsWere) return;        // nothing new: the stage has them
    tagsWere = sig;
    stage->setTags (std::move (out));
}

// The screen as it now stands, given to the stage: first the solid parts (one whole drawing of the
// screen with nothing drawn, only written down; it also lays out and gathers what the hand can
// touch), then the print (drawn for real, onto the sheet, where it has changed).
void Screen::refreshStage()
{
    if (! three || stage == nullptr) return;
    {
        juce::Image none (juce::Image::ARGB, 1, 1, true, juce::SoftwareImageType());
        G g (none);
        g.reduceClipRegion (juce::Rectangle<int>());
        recorded.clear();
        legendRects.clear();
        recorded.dark = dark;
        recording = true; collect = true;
        skin.setMode (Skin::Mode::record, &recorded);
        drawAll (g);
        skin.setMode (Skin::Mode::pictures);
        recording = false;
        recorded.settle();
        stage->setParts (recorded);
    }
    auto& img = stage->sheet();
    const juce::Rectangle<int> whole (0, 0, img.getWidth(), img.getHeight());
    juce::RectangleList<int> px;
    if (sheetAll) px.add (whole);
    else { px = sheetDirty; px.clipTo (whole); px.consolidate(); }
    sheetAll = false; sheetDirty.clear();
    if (px.isEmpty()) return;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    for (const auto& r : px) img.clear (r, juce::Colours::transparentBlack);
    {
        G g (img);
        g.reduceClipRegion (px);
        g.addTransform (juce::AffineTransform::scale (stage->sheetScale()));
        printing = true; collect = false;
        skin.setMode (Skin::Mode::print);
        drawAll (g);
        skin.setMode (Skin::Mode::pictures);
        printing = false;
    }
    const double t1 = juce::Time::getMillisecondCounterHiRes();
    for (const auto& r : px) stage->sheetChanged (r);
    printMs = t1 - t0; sendMs = juce::Time::getMillisecondCounterHiRes() - t1;
}

void Screen::timerCallback()
{
    // what a controller asked for: another screen; CONFIG opened, shut, or moved to another panel
    if (ses.wantShowing >= 0) { const int w = ses.wantShowing; ses.wantShowing = -1; if (w != (int) page && w <= (int) pArrange) setPage ((Page) w); }
    // A print: rendered here, a slice at a time, so the page can say how far it has got.
    if (ses.printing()) { ses.printStep (22.0); repaint(); }
    if (cfgOpen != cfgOpenSeen || cfg.kind != cfgSeen.kind || cfg.lane != cfgSeen.lane || cfg.slot != cfgSeen.slot)
    {
        if (cfgOpen) { cfgStale = true; cfgScroll = 0; cfgName = {}; cfgNote = {}; loadOpen = false; }
        cfgSeen = cfg; cfgOpenSeen = cfgOpen;
        repaint();
    }
    if (ses.cfgTouched.isNotEmpty()) { cfgName = ses.cfgTouched; ses.cfgTouched = {}; cfgStale = true; if (cfgOpen) dirty (cfgRect); }
    if (tapAct >= 0 && juce::Time::getMillisecondCounter() - tapAt >= kTapMs)
    {
        // the key an action pressed comes back up (in 3D only the parts are written down again: nothing is printed)
        tapAct = -1;
        if (three) triggerAsyncUpdate(); else juce::Component::repaint();
    }
    if (three) { followMoves(); showTags(); }
    if (three && ! settledSaid && stage != nullptr && stage->firstPictureSpread() != -1) settled();       // it is up and has drawn
    watchStart (juce::Time::getMillisecondCounter(), isShowing() && juce::Process::isForegroundProcess());
    if (lit && ! three && skin.settled()) { warm = false; repaint(); }     // the window has a new size and has held it: the pictures are made again for its pixels
    if (lit && ! three && ! warm && skin.ready() && ! mouseIsDown)
    {
        // CONFIG's plate and the cards of the other pages, made while nothing is waiting on them
        warm = true;
        skin.makePanel (554, 894, tone (col::grey));
        skin.makePanel (272, 66, tone (col::putty));
        skin.makePanel (272, 216, tone (col::putty));
    }
    // repaint what moved on its own: gliding knobs, the meters, the step that is playing
    bool ringsMoved = false;
    for (auto& h : hits)
        if (h.kind == hKnob || h.kind == hSnapKnob || h.kind == hToggle || h.kind == hHold)
        {
            const int ctl = h.kind == hSnapKnob ? cx::kSnap + h.a : h.a;
            if (ctl >= 0 && ctl < cx::kCount && eng.shown (ctl) != lastShown[(size_t) ctl])
            {
                // (on the 3D stage a knob's lights are the drawing thread's own business, read from
                // the engine: nothing of the sheet or the record changes when one travels, so none
                // of either is made again. A snapshot gliding moves a hundred at once.)
                if (three && h.kind == hKnob) { lastShown[(size_t) ctl] = eng.shown (ctl); ringsMoved = true; continue; }
                dirty (ctl == cx::lane (ses.activeLane, cx::kSeqMute) ? seqRect : h.r.expanded (14, 6));       // a sequencer's mute greys its steps
            }
        }
    if (ringsMoved && stage != nullptr) stage->kick();
    // a take that ended by itself (the disk) says so
    if (recorder != nullptr)
        if (const bool r = recorder->recording(); r != recWas) { recWas = r; recNote = recorder->note(); recNoteAt = juce::Time::getMillisecondCounter(); }
    if (barNow() != barSig) dirty (R (8, 12, 1712, 52));          // RUN and TEMPO, when an outside clock moves them; REC's time
    if (online != nullptr && page == pLibrary && online->stamp() != onlineStamp) dirty (R (23, 980, 1682, 110));    // what ONLINE has to say
    const float want = cfgOpen && page == pMain ? 1.0f : 0.0f;
    if (cfgT != want)
    {
        cfgT = want > cfgT ? std::min (want, cfgT + 0.2f) : std::max (want, cfgT - 0.2f);
        repaint();
    }
    // the rows travel to wherever CONFIG wants them, in step with its panel
    if (const float goal = slideGoal(); goal != slideTo) { slideFrom = slideX; slideTo = goal; slideT = 0.0f; }
    if (slideT < 1.0f)
    {
        slideT = std::min (1.0f, slideT + 0.2f);
        slideX = slideFrom + (slideTo - slideFrom) * slideT * slideT * (3.0f - 2.0f * slideT);
        repaint();
    }
    if (page == pArrange && arrangeSig() != arrSig) repaint();      // the part playing, the beat it is on, what the print last said
    if (page == pMain && ses.arrPlaying() != arrLit) { arrLit = ses.arrPlaying(); repaint(); }
    if (page == pMacros && motionSig() != motSig) repaint();       // MOTION turned from a controller: its words, and the marks on the knobs' cards
    if (page == pPockets)
    {
        // each row's map is drawn again when a bar of it has moved by a pixel, or its pocket has
        eng.pocketsSeen (pkSeen);
        for (int q = 0; q < Pockets::kParts; ++q)
        {
            if (const auto sig = pocketSig (q); sig != pkSig[(size_t) q] && ! pkMaps[(size_t) q].isEmpty()) dirty (pkMaps[(size_t) q]);
            if (q < kLanes && pkSaid[(size_t) q] != eng.target (cx::pk (q, 0)) + 2.0f * eng.target (cx::pk (q, 1))) dirty (R (82.0f, 217.0f + (float) q * 70.0f, 104.0f, 22.0f));
        }
        if (pkNote.isNotEmpty() && juce::Time::getMillisecondCounter() - pkNoteAt > 6000) { pkNote = {}; dirty (R (23, 78, 1682, 96)); }
    }
    if (page == pMain)
    {
        if (cfgOpen && ! cfgDisplay.isEmpty()) dirty (cfgDisplay);
        if (cfgOpen)
            if (auto* pr = eng.processorFor (cfg))
                if (const String st = pr->mod().statusLine(); st != cfgStatus) { cfgStatus = st; cfgStale = true; dirty (cfgRect); }     // an import finishing says so
        if (metersNow() != meterSig) dirty (masterRect);       // the meters, when a bar has moved by a pixel or a number has changed
        const bool run = eng.isRunning();
        if (run || wasRunning)
        {
            const int len = std::max (1, ses.pattern (ses.activeLane).length);
            const int now = run ? (int) (((juce::int64) std::floor (eng.songPos() / kStepBeats)) % len) : -1;
            if (now != lastStep || run != wasRunning) dirty (seqRect);
        }
        wasRunning = run;
    }
    if (mouseIsDown && drag.kind == hAct && drag.a == aRec && ! drag.fired && recorder != nullptr && recorder->recording()
        && juce::Time::getMillisecondCounter() - drag.t0 > 700)
    {
        drag.fired = true;
        recorder->stop();
        recWas = false; recNote = recorder->note(); recNoteAt = juce::Time::getMillisecondCounter();
        repaint();
    }
    if (mouseIsDown && drag.kind == hAct && drag.a == aArrClear && ! drag.fired && juce::Time::getMillisecondCounter() - drag.t0 > 700)
    {
        drag.fired = true;
        while (! ses.arrangement.empty()) ses.arrRemove ((int) ses.arrangement.size() - 1);
        selPart = -1;
        repaint();
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
