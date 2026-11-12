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
      fFlag   (face (BinaryData::BarlowSemiCondensedSemiBold_ttf, BinaryData::BarlowSemiCondensedSemiBold_ttfSize, 36.0f, 0.0f))
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    lastShown.fill (-1.0f);
    ses.onChange = [this] { cfgStale = true; repaint(); };
    startTimerHz (30);
}

Screen::~Screen() { ses.onChange = nullptr; }

void Screen::setPage (Page p) { endEdit (true); if (p != pLibrary && page == pLibrary && cueMode) ses.cue (0); page = p; repaint(); }

void Screen::resized()
{
    const float s = std::min ((float) getWidth() / kW, (float) getHeight() / kH);
    xf = juce::AffineTransform::scale (s).translated (((float) getWidth() - kW * s) * 0.5f, ((float) getHeight() - kH * s) * 0.5f);
}

void Screen::dirty (R r) { repaint (r.transformedBy (xf).getSmallestIntegerContainer().expanded (2)); }
void Screen::hit (R r, int kind, int a, int b) { if (collect) hits.push_back ({ r.translated (shift, 0.0f), kind, a, b }); }
// Lit, a thing's picture reaches past its own edge (the shade it casts, its glow), so what is
// near a part-paint is drawn again with it: the part then matches what a whole paint would give.
bool Screen::vis (G& g, R r) const { return g.clipRegionIntersects (r.expanded (lit ? 12.0f : 0.0f).getSmallestIntegerContainer()); }
bool Screen::seen (G& g, R r) const { return g.clipRegionIntersects (r.getSmallestIntegerContainer()); }

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
    const int sp = label.indexOfChar (' ');
    if (label.length() > 7 && sp > 0)
    {
        text (g, label.substring (0, sp), R (cx - 60, y, 120, 14), fLbl, col::black);
        text (g, label.substring (sp + 1), R (cx - 60, y + 14, 120, 14), fLbl, col::black);
    }
    else text (g, label, R (cx - 60, y, 120, 14), fLbl, col::black);
}

void Screen::knob (G& g, float cx, float cy, int size, float v, Kind kind, int steps, Colour litCol, float target)
{
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
    if (lit)
    {
        // The knob as modelled: a dark cap in a ring of sixteen lights. The lights say where it stands.
        skin.knob (g, cx, cy, size);
        if (b - a > 0.0005f) skin.ringLit (g, cx, cy, size, a, b, litCol);
        if (kind == Kind::Bipolar) { g.setColour (tone (col::black)); g.fillRect (cx - 1.0f, cy - body - 4.5f, 2.0f, 3.5f); }       // its middle, printed on the plate
        if (target >= 0.0f)
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

void Screen::key (G& g, R r, Colour fill, float border)
{
    if (lit) skin.key (g, r, cap (fill), border > 2.5f);        // (a thick border was the flat drawing's way to show a key held down)
    else flatKey (g, r, fill, border);
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
float Screen::title (G& g, float x, float y, const String& s)
{
    return plate (g, x, y, s, fLegend, 8, lit ? col::black : col::white, lit ? col::lit : col::black);
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
        if (c.kind == Kind::Toggle) key (g, kr, v > 0.5f ? col::green : col::putty);
        else if (c.kind == Kind::Hold) key (g, kr, col::yellow, v > 0.5f ? 8.0f : 2.0f);
        else if (eng.isGliding (ctl)) knob (g, cxm, cym, size, v, c.kind, c.steps(), col::green, eng.target (ctl));
        else knob (g, cxm, cym, size, v, c.kind, c.steps(), litCol);
        if (info)
        {
            if (c.kind == Kind::Toggle) number (g, ctl, kr, inkOn (cap (v > 0.5f ? col::green : col::putty), col::black));
            else if (c.kind == Kind::Hold) number (g, ctl, kr, col::black);
            else number (g, ctl, R (cxm - 14, cym - 10 - (lit ? (size >= 40 ? 3.0f : 2.0f) : 0.0f), 28, 20), col::lit);       // (lit, the cap's top stands a little up the screen)
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
            key (g, kr, fills[k]); textRaw (g, letter, kr, fLbl, inkOn (cap (fills[k]), k == 0 && open ? col::lit : col::black));
            if (k == 1 && seen (g, kr)) lastShown[(size_t) muteCtl] = eng.shown (muteCtl);
            if (k == 2 && seen (g, kr)) lastShown[(size_t) soloCtl] = eng.shown (soloCtl);
        }
        if (k == 0) hit (kr.expanded (1), hConfig, kind, lane * 8 + slot);
        else hit (kr.expanded (1), hToggle, k == 1 ? muteCtl : soloCtl);
    }
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
    if (ctl >= cx::kMaster)
    {
        static const char* const ids[3] = { "tape", "comp", "limit" };
        where = "MASTER"; what = help::param ("MASTER", ids[ctl - cx::kMaster]);
        return true;
    }
    if (ctl >= cx::kRetMS) { where = ctl - cx::kRetMS < 2 ? "RETURN A, THE DUB DELAY" : "RETURN B, THE SPACE"; what = help::param ("LANE", (ctl - cx::kRetMS) % 2 == 0 ? "mute" : "solo"); return true; }
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
    else { label = "SEQUENCER MUTE"; what = help::param ("LANE", "seq_mute"); }
    return true;
}

// Where MIDI finds a control, and what its values mean.
String Screen::midiOf (int ctl) const
{
    int ch = 0, num = 0;
    if (! ccx::addressOf (ctl, ch, num)) return "NOT ON MIDI.";
    const auto& c = eng.control (ctl);
    String s = "MIDI: CHANNEL " + String (ch) + ", CC " + String (num) + ".";
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
            midi = "MIDI: CHANNEL 16. CC " + String (e16::kKnobCc + h.a) + " TURNS IT. CC " + String (e16::kPressCc + h.a) + " (64 AND OVER) RECALLS ITS NEAREST. CC " + String (ccx::kRecall + h.a)
                 + " RECALLS THE ONE ITS VALUE NAMES: 1 THE FIRST IT HOLDS, " + String (std::max (2, held)) + " THE LAST. A VALUE SAID AGAIN DOES NOTHING; 0 IS NONE.";
            below = true;
            break;
        }
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
            break;
        case hPageSel:
            t.title = "STEPS " + String (h.a * 16 + 1) + " TO " + String (h.a * 16 + 16);
            body = "A PAGE OF THE PATTERN. CLICK TO SEE ITS 16 STEPS (THE LEFT AND RIGHT ARROW KEYS TOO).";
            break;
        case hLen:
            t.title = "LENGTH";
            body = "THE PATTERN'S LENGTH, 1 TO 128 STEPS.";
            below = true;
            break;
        case hAct:
        {
            struct A { int a; const char* title; const char* body; };
            static const A acts[] = {
                { aRun, "RUN", "STARTS AND STOPS THE CLOCK (THE SPACE BAR TOO)." },
                { aPanic, "PANIC", "SILENCES EVERY VOICE AND TAIL." },
                { aAudio, "AUDIO AND MIDI", "THE INTERFACE, ITS OUTPUTS, THE BUFFER AND THE CLOCK: SET ONCE, BEFORE THE SET. (THE KEY IS GREEN AND READS PHONES WHILE PHONES IS ON.)" },
                { aRec, "REC", "PRESS TO START A TAKE; HOLD TO END IT. THE SET, EVERY LANE AND THE TWO RETURNS ARE RECORDED." },
                { aSnapshot, "SNAPSHOT", "KEEPS EVERYTHING ON THE MAIN SCREEN AS A NEW SNAPSHOT, WITH THE BARS IT WAS HEARD OVER." },
                { aLibrary, "LIBRARY", "THE SNAPSHOT LIBRARY: TAP TO RECALL, RENAME, HEAR ONE IN THE HEADPHONES." },
                { aMacros, "MACROS", "THE MACRO DESIGNER: WHICH SNAPSHOTS EACH SNAPSHOT KNOB HOLDS, IN SWEEP ORDER. A KNOB'S SNAPSHOTS ARE ALSO WHAT RECALL (CC 80 TO 95 ON CHANNEL 16) PICKS FROM." },
                { aMain, "MAIN", "BACK TO THE MAIN SCREEN." },
                { aNudgeL, "NUDGE", "MOVES THE WHOLE PATTERN ONE STEP EARLIER." },
                { aNudgeR, "NUDGE", "MOVES THE WHOLE PATTERN ONE STEP LATER." },
                { aRatchet, "RATCHET", "THE SELECTED STEP PLAYS ONE TO FOUR HITS." },
                { aNote, "NOTE", "THE SELECTED STEP'S NOTE." },
                { aOctave, "OCTAVE", "THE SELECTED STEP'S OCTAVE." },
                { aGen0, "RANDOM", "WRITES A PATTERN AT ONCE. WHILE IT IS LIT, HITS, ROTATE, ACCENTS AND SEED REWRITE IT." },
                { aGen1, "EUCLID", "WRITES A PATTERN WITH ITS HITS SPREAD EVENLY. WHILE IT IS LIT, HITS, ROTATE, ACCENTS AND SEED REWRITE IT." },
                { aRename, "RENAME", "NAMES THE CHOSEN SNAPSHOT." },
                { aDelete, "DELETE", "HOLD TO DELETE THE CHOSEN SNAPSHOT." },
                { aRenameKnob, "RENAME", "NAMES THIS SNAPSHOT KNOB." },
                { aClearKnob, "CLEAR", "TAKES EVERY SNAPSHOT OFF THIS KNOB." },
                { aCfgClose, "X", "CLOSES CONFIG." },
                { aClipReset, "CLIP LAMPS", "RED: THE OUTPUT REACHED FULL SCALE. CLICK TO CLEAR." },
                { aCue, "CUE", "WHILE IT IS LIT, A TAPPED SNAPSHOT'S BARS ARE HEARD IN THE HEADPHONES, IN TIME WITH THE CLOCK, AND THE SET IS LEFT ALONE." },
                { aRecallCue, "RECALL", "TAKES THE SNAPSHOT IN THE HEADPHONES (OR THE ONE CHOSEN) TO THE SET." },
                { aOnline, "ONLINE", "EACH SNAPSHOT GOES TO THE LIBRARY ON THE WEB AS IT IS TAKEN. GREEN: IT HOLDS THEM ALL. YELLOW: SENDING. AMBER: WAITING. RED: TURNED AWAY." },
                { aOnlineSetup, "SET UP", "THE WEB LIBRARY'S ADDRESS AND ITS KEY." },
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

void Screen::mouseMove (const juce::MouseEvent& e) { if (info) point (logical (e.position)); }
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
    collect = hits.empty() || g.getClipBounds().contains (getLocalBounds());
    g.fillAll (night() ? night::room : col::black);
    g.addTransform (xf);
    if (collect) hits.clear();
    if (lit) { skin.begin (g); if (vis (g, R (0, 0, kW, kH))) skin.body (g, kW, kH); }
    else if (vis (g, R (0, 0, kW, kH)))
    {
        g.setColour (col::sea);
        g.fillRoundedRectangle (R (8, 12, 1712, 1092), 8.0f);
    }
    drawBar (g);
    if (page == pMain) drawMain (g);
    else if (page == pLibrary) drawLibrary (g);
    else drawMacros (g);
    if (info && hovering && ! mouseIsDown) drawTip (g);
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
        if (page != pMain) title (g, 23 + w + 12, 24.5f, page == pLibrary ? "SNAPSHOT LIBRARY" : "MACRO DESIGNER");
        else if (info)
        {
            text (g, "INFO (I): THE NUMBER ON A CONTROL IS ITS MIDI CC. A LANE'S NUMBER IS ITS CHANNEL, AND ITS INSERTS ARE ON IT TOO.", R (23 + w + 16, 20, 760, 17), fLbl, col::black, juce::Justification::centredLeft);
            text (g, "THE RETURNS, THE MASTER, GLIDE AND THE SNAPSHOT KNOBS ARE ON CHANNEL 16. REST THE MOUSE ON A CONTROL FOR WHAT IT DOES.", R (23 + w + 16, 38, 760, 17), fLbl, col::black, juce::Justification::centredLeft);
        }
        // REC: press to start a take, hold to end it. The time it has run stands in for its name.
        {
            const bool on = recorder != nullptr && recorder->recording();
            key (g, R (950, 17.5f, 40, 40), on ? (recorder->lostSeconds() > 0.0 ? col::amber : col::red) : col::putty,
                 mouseIsDown && drag.kind == hAct && drag.a == aRec ? 6.0f : 2.0f);
            String t ("REC");
            if (on) { const auto s = (juce::int64) recorder->seconds(); t = String (s / 3600) + ":" + String ((s / 60) % 60).paddedLeft ('0', 2) + ":" + String (s % 60).paddedLeft ('0', 2); }
            text (g, t, R (996, 17.5f, 70, 40), fLegend, col::black, juce::Justification::centredLeft);
            if (page == pMain && recNote.isNotEmpty() && juce::Time::getMillisecondCounter() - recNoteAt < 8000)
                plate (g, 940.0f - (std::ceil (tw (fNote, recNote)) + 12.0f), 24.5f, recNote, fNote, 6, col::white, col::black);
        }
        // (PHONES lit: this output is split while a cue plays, and the key says so wherever one is)
        text (g, eng.phones() ? "PHONES" : "AUDIO", R (1116, 17.5f, 60, 40), fLegend, col::black, juce::Justification::centredLeft);
        text (g, "RUN", R (1222, 17.5f, 40, 40), fLegend, col::black, juce::Justification::centredLeft);
        text (g, "PANIC", R (1314, 17.5f, 56, 40), fLegend, col::black, juce::Justification::centredLeft);
        key (g, R (1070, 17.5f, 40, 40), eng.phones() ? col::green : col::white);
        key (g, R (1176, 17.5f, 40, 40), eng.isRunning() ? col::green : col::putty);
        key (g, R (1268, 17.5f, 40, 40), col::red, mouseIsDown && drag.kind == hAct && drag.a == aPanic ? 6.0f : 2.0f);
        // following an outside clock, the tempo is that clock's and says so
        readout (g, R (1373, 17.5f, 96, 40), eng.following() ? "CLOCK IN" : "TEMPO", eng.valueText (cx::kTempo));
        barSig = barNow();
        readout (g, R (1487, 17.5f, 72, 40), "KEY", eng.valueText (cx::kKey));
        knob (g, 1597, 37.5f, 40, eng.shown (cx::kGlide), Kind::List, 12, col::lit);
        if (info) number (g, cx::kGlide, R (1597 - 14, 37.5f - 10 - (lit ? 3.0f : 0.0f), 28, 20), col::lit);
        readout (g, R (1621, 17.5f, 84, 40), "GLIDE", eng.valueText (cx::kGlide));
        if (seen (g, bar)) lastShown[(size_t) cx::kGlide] = eng.shown (cx::kGlide);
    }
    if (! collect) return;
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
    const int col = cfg.kind == 0 ? (cfg.lane < 5 ? cfg.lane : cfg.lane - 5) : cfg.kind == 1 ? cfg.slot : 3;
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
        const size_t first = hits.size();
        for (int copy = -1; copy <= 1; ++copy)
        {
            const float dx = slideX + (float) copy * 1692.0f;       // six columns of 282
            if (23.0f + dx >= rowsClip.getRight() || 1705.0f + dx <= rowsClip.getX()) continue;
            const juce::Graphics::ScopedSaveState moved (g);
            g.addTransform (juce::AffineTransform::translation (dx, 0.0f));
            shift = dx;
            for (int l = 0; l < kLanes; ++l)
            {
                const int col = l < 5 ? l : l - 5, row = l < 5 ? 0 : 1;
                drawLane (g, l, R (23.0f + (float) col * 282.0f, row == 0 ? 78.0f : 430.0f, 272, 344));
            }
            drawSnapPanel (g, R (1433, 78, 272, 344));
            for (int sl = 0; sl < kSlots; ++sl) drawInsert (g, sl, R (23.0f + (float) sl * 282.0f, 782, 272, 190));
            drawReturns (g, R (869, 782, 272, 190));
            drawMaster (g, R (1151, 782, 554, 190));
            shift = 0.0f;
        }
        // what is off the screen or behind CONFIG cannot be touched
        if (collect) hits.erase (std::remove_if (hits.begin() + (long) first, hits.end(), [this] (const Hit& h) { return ! h.r.intersects (rowsClip); }), hits.end());
    }
    drawSequencer (g, R (23, 980, 1682, 110));
    if (cfgT > 0.001f && (collect || vis (g, cfgR)))
    {
        const juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (juce::Rectangle<int> (9, 64, 1710, 912));
        drawConfig (g, cfgR);
    }
}

void Screen::openConfig (Engine::Target t)
{
    const bool same = cfgOpen && cfg.kind == t.kind && cfg.slot == t.slot && (t.kind == 2 || cfg.lane == t.lane);
    if (same) { cfgOpen = false; return; }
    cfg = t; cfgOpen = true; cfgStale = true; cfgScroll = 0; cfgName = {}; cfgNote = {};
    if (t.kind == 0 && ses.activeLane != t.lane) { ses.activeLane = t.lane; ses.selStep = -1; ses.page = 0; }
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
        if (sp.name == cfgName)
        {
            const String v = sp.name + "   " + sp.text.toUpperCase();
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
            g.setColour (juce::Colours::black); g.fillRect (cfgDisplay);
            {
                const juce::Graphics::ScopedSaveState state (g);
                g.reduceClipRegion (cfgDisplay.getSmallestIntegerContainer());
                m.paint (g, cfgDisplay);
            }
            if (lit)
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
    if (status.isNotEmpty())
    {
        status = status.toUpperCase();
        const R sr (x0, y, std::min (544.0f, std::ceil (tw (fNote, status)) + 12.0f), 22);
        window (g, sr, col::white);
        g.setColour (tone (col::black)); g.setFont (fNote);
        g.drawText (status, sr.reduced (6, 0), juce::Justification::centredLeft, true);
        y += 28;
    }

    const int cols = 6, total = (int) cfgParams.size(), rows = (total + cols - 1) / cols;
    const float rowH = 96.0f;
    const int rowsVis = std::max (1, (int) ((r.getBottom() - 8 - y) / rowH));
    cfgScroll = juce::jlimit (0, std::max (0, rows - rowsVis), cfgScroll);
    for (int n = 0; n < rowsVis * cols; ++n)
    {
        const int i = cfgScroll * cols + n;
        if (i >= total) break;
        const auto& sp = cfgParams[(size_t) i];
        const float cxl = x0 + 2 + (float) (n % cols) * 90, cy = y + (float) (n / cols) * rowH;
        const R kr (cxl + 25, cy, 40, 40);
        if (vis (g, R (cxl - 16, cy, 122, rowH)))
        {
            if (sp.kind == Kind::Toggle) key (g, kr, sp.value > 0.5f ? col::green : col::putty);
            else if (sp.kind == Kind::Hold) key (g, kr, col::yellow, sp.value > 0.5f ? 8.0f : 2.0f);
            else knob (g, cxl + 45, cy + 20, 40, sp.value, sp.kind, sp.choices.size(), col::lit);
            legend (g, cxl + 45, cy + 43, sp.name);
            if (sp.kind == Kind::List)
            {
                const R vr (cxl + 1, cy + 73, 88, 16);
                if (lit) skin.window (g, vr, glass (col::white)); else { g.setColour (col::white); g.fillRect (vr); }
                g.setColour (tone (col::black)); g.setFont (fLbl);
                g.drawText (sp.text.toUpperCase(), vr.reduced (2, 0), juce::Justification::centred, true);
            }
        }
        hit (kr.expanded (4, 2), hSetup, i, 0);
    }
    if (rows > rowsVis)
    {
        const String more = "ROWS " + String (cfgScroll + 1) + " TO " + String (cfgScroll + rowsVis) + " OF " + String (rows) + ": SCROLL";
        plate (g, r.getRight() - 5 - (std::ceil (tw (fNote, more)) + 12), r.getBottom() - 27, more, fNote, 6, col::white, col::black, 22);
    }
}

void Screen::drawLane (G& g, int l, R r)
{
    if (off (g, r)) return;
    const auto& d = lanes()[(size_t) l];
    const float x0 = r.getX() + 5, y0 = r.getY() + 5, body = y0 + 32;
    const bool active = l == ses.activeLane;
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
    panel (g, r, col::putty);
    if (vis (g, R (x0, y0, 262, 26)))
    {
        const float w = title (g, x0, y0, "SNAPSHOTS");
        if (info) chan (g, x0 + w + 2, y0, e16::kSnapChannel);
        else plate (g, x0 + w + 2, y0, "PAGE 1", fNote, 6, col::white, col::black);
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
            if (vis (g, R (cxm - 40, y, 80, 58)))
            {
                if (seen (g, lit ? kr : R (cxm - 40, y, 80, 58))) lastShown[(size_t) (cx::kSnap + k)] = v;
                knob (g, cxm, y + 20, 40, v, Kind::Knob, 0, col::teal);
                if (info) number (g, cx::kSnap + k, R (cxm - 14, y + 10 - (lit ? 3.0f : 0.0f), 28, 20), col::lit);
                text (g, ses.knobs[(size_t) k].name, R (cxm - 40, y + 42, 80, 14), fLbl, col::black);
            }
            hit (kr.expanded (4, 2), hSnapKnob, k, 0);
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
    hit (bar, hAct, aSnapshot, 0);
    actKey (g, aLibrary, x0 + 132, ky, 64, "LIBRARY", col::white);
    actKey (g, aMacros, x0 + 198, ky, 64, "MACROS", col::white);
}

void Screen::drawInsert (G& g, int slot, R r)
{
    if (off (g, r)) return;
    const float x0 = r.getX() + 5, y0 = r.getY() + 5, body = y0 + 32;
    const int l = ses.activeLane, t = eng.insertType (l, slot);
    hit (r, hDropFx, 1, slot);
    panel (g, r, col::putty);
    const String name = t >= 0 ? String (effects()[(size_t) t].name) : String ("EMPTY");
    const float w = title (g, x0, y0, name);
    hit (R (x0, y0, w, 26), hInsHead, slot, 0);
    if (t >= 0 && w + 54 < 190) chan (g, x0 + w + 2, y0, l + 1);
    if (t >= 0) headKeys (g, r.getRight() - 5, y0 + 2, 1, l, slot, cx::insMute (l, slot), cx::insSolo (l, slot));
    for (int c = 0; c < kCells; ++c) control (g, cx::ins (l, slot, c), x0 + (float) (c % 4) * 66, body + (float) (c / 4) * 77, 64, col::lit);
}

void Screen::drawReturns (G& g, R r)
{
    if (off (g, r)) return;
    const float x0 = r.getX() + 5, y0 = r.getY() + 5, body = y0 + 32;
    hit (r, hDropFx, 2, 1);
    panel (g, r, col::putty);
    title (g, x0, y0, "RETURNS");
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
    panel (g, r, col::grey);
    const float wTitle = title (g, x0, y0, "MASTER");
    chan (g, x0 + wTitle + 2, y0, e16::kSnapChannel);
    for (int k = 0; k < 3; ++k) control (g, cx::kMaster + k, x0 + (float) k * 66, body, 64, col::lit);
    if (! vis (g, R (x0, body + 60, 544, 100))) return;
    // FAULT, gain reduction, loudness
    const float fy = body + 77;
    g.setColour (tone (col::black)); g.fillRect (R (x0 + 6, fy + 8, 14, 26));
    g.setColour (eng.fault.load() ? col::amber : tone (col::grey)); g.fillRect (R (x0 + 8, fy + 10, 10, 22));
    text (g, "FAULT", R (x0 + 25, fy, 40, 42), fLbl, col::black, juce::Justification::centredLeft);
    readout (g, R (x0 + 66, fy, 64, 42), "GR", String (eng.gainRed.load(), 1).replaceCharacter ('-', (juce::juce_wchar) 0x2212));
    const float lu = eng.lufs.load();
    readout (g, R (x0 + 132, fy, 64, 42), "LUFS", lu < -60.0f ? String ("--") : String (lu, 1).replaceCharacter ('-', (juce::juce_wchar) 0x2212));
    // meters: every lane as it reaches the mix, then the mix itself
    const R mp (x0 + 212, y0, 332, 180);
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
        const float x = mp.getX() + 8 + (float) l * 23;
        bar (x, 18, eng.laneDb[(size_t) l].load());
        const R nb (x - 2, bot + 8, 22, 16);
        const bool act = l == ses.activeLane;
        if (act) { g.setColour (tone (col::black)); g.fillRect (nb); }
        textRaw (g, String (l + 1), nb, fLbl, act ? (night() ? col::black : col::lit) : tone (col::black));
    }
    const float mx = mp.getX() + 267;
    bar (mx, 16, eng.peakL.load());
    bar (mx + 18, 16, eng.peakR.load());
    flatKey (g, R (mx, mp.getY() + 6, 16, 12), eng.clipL.load() ? col::red : col::white);       // (flags on the display, not keys)
    flatKey (g, R (mx + 18, mp.getY() + 6, 16, 12), eng.clipR.load() ? col::red : col::white);
    text (g, "MIX", R (mx - 4, bot + 8, 42, 16), fLbl, col::black);
    const float sx = mx + 37;
    g.setColour (col::amber); g.fillRect (R (sx, yOf (0), 3, yOf (-6) - yOf (0)));
    g.setColour (col::red); g.fillRect (R (sx, yOf (6), 3, yOf (0) - yOf (6)));
    static const int ticks[] = { -48, -24, -12, 0 };
    for (int t : ticks)
    {
        g.setColour (tone (col::black)); g.fillRect (R (sx, yOf ((float) t) - 1, 6, 2));
        text (g, t == 0 ? String ("0") : String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + String (-t), R (sx + 7, yOf ((float) t) - 7, 22, 14), fLbl, col::black, juce::Justification::centredLeft);
    }
    hit (R (mx - 2, mp.getY() + 2, 40, 20), hAct, aClipReset, 0);
}

void Screen::drawSequencer (G& g, R r)
{
    seqRect = r;
    if (off (g, r)) return;
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
    const float wn = plate (g, x0 + wTag, y0, d.name, fLegend, 8, col::white, col::black);
    const float ws = plate (g, x0 + wTag + wn + 2, y0, String (len) + (len == 1 ? " STEP" : " STEPS"), fNote, 6, col::white, col::black);
    // M: this lane's own sequencer muted. Its steps stay and go unplayed; notes from outside still sound.
    const int muteCtl = cx::lane (l, cx::kSeqMute);
    const bool seqOff = eng.shown (muteCtl) > 0.5f;
    const R mk (x0 + wTag + wn + 2 + ws + 4, y0 + 2, 22, 22);
    if (vis (g, mk))
    {
        key (g, mk, seqOff ? col::amber : col::putty);
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
    title (g, x0, y0, String (total) + (total == 1 ? " SNAPSHOT" : " SNAPSHOTS"));
    actKey (g, aMain, x0, ky, 64, "MAIN", col::white, 5);
    actKey (g, aMacros, x0 + 66, ky, 64, "MACROS", col::white, 5);
    actKey (g, aSnapshot, x0 + 160, ky, 64, "SNAPSHOT", col::yellow, 5);
    const bool has = ses.findSnap (selSnap) != nullptr;
    actKey (g, aRename, x0 + 226, ky, 64, "RENAME", col::yellow, 5, has);
    actKey (g, aDelete, x0 + 292, ky, 64, "HOLD: DELETE", col::red, 5, has);
    // CUE: taps are heard in the headphones; RECALL then takes the one being heard (or the one chosen) to the set
    actKey (g, aCue, x0 + 386, ky, 64, "CUE", cueMode ? col::green : col::putty, 5);
    actKey (g, aRecallCue, x0 + 452, ky, 64, "RECALL", col::yellow, 5, cueMode && (ses.cueing() != 0 || has));
    // PHONES: one pair of outputs with headphones on it. While a cue plays: the set left, the cue right.
    actKey (g, aPhones, x0 + 518, ky, 64, "PHONES", eng.phones() ? col::green : col::putty, 5);
    const String help = ! cueMode ? (night() ? "TAP A SNAPSHOT TO RECALL IT. SOUND KNOBS TRAVEL OVER THE GLIDE TIME. THE LIT NAME IS THE BASE SCENE."
                                             : "TAP A SNAPSHOT TO RECALL IT. SOUND KNOBS TRAVEL OVER THE GLIDE TIME. THE BLACK NAME IS THE BASE SCENE.")
                      : eng.phones() ? "CUE WITH PHONES LIT: WHILE A SNAPSHOT'S BARS PLAY, THE SET IS IN THE LEFT EAR AND THE CUE IN THE RIGHT. TAP IT AGAIN TO STOP. RECALL TAKES IT TO THE SET."
                      : ! eng.hasCueOutput() ? "CUE NEEDS OUTPUTS 3 + 4 FOR THE HEADPHONES (TICK THEM IN AUDIO), OR PHONES LIT: THEN THIS OUTPUT CARRIES THE SET LEFT AND THE CUE RIGHT."
                      : "CUE: TAP A SNAPSHOT TO HEAR ITS BARS IN THE HEADPHONES, IN TIME WITH THE CLOCK. TAP IT AGAIN TO STOP. RECALL TAKES IT TO THE SET.";
    if (online == nullptr) { text (g, help, R (x0 + 606, ky, 1040, 40), fLbl, col::black, juce::Justification::centredLeft); return; }
    // ONLINE: each snapshot goes to the library on the web as it is taken. The lamp: green when the
    // library holds them all, yellow while sending, amber when it waits (no network, or not set up), red when turned away.
    const auto st = online->status().state;
    onlineStamp = online->stamp();
    const Colour lamp = st == Online::State::off ? col::putty : st == Online::State::upToDate ? col::green : st == Online::State::working ? col::yellow
                      : st == Online::State::refused ? col::red : col::amber;
    actKey (g, aOnline, x0 + 612, ky, 64, "ONLINE", lamp, 5);
    actKey (g, aOnlineSetup, x0 + 678, ky, 64, "SET UP", col::white, 5);
    text (g, online->line(), R (x0 + 766, ky, 894, 20), fLbl, col::black, juce::Justification::centredLeft);
    text (g, help, R (x0 + 766, ky + 20, 894, 20), fLbl, col::black, juce::Justification::centredLeft);
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
        plate (g, r.getRight() - 6 - (std::ceil (tw (fNote, mv)) + 12), y0, mv, fNote, 6, col::white, col::black);
        // the knob itself, its depth, its snapshots in sweep order
        const float ky = y0 + 40;
        if (kn.snaps.size() >= 2)
        {
            const float v = eng.shown (cx::kSnap + k);
            lastShown[(size_t) (cx::kSnap + k)] = v;
            knob (g, x0 + 32, ky + 20, 40, v, Kind::Knob, 0, col::teal);
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
            moves << eng.control (i).label << (i >= cx::space (1, 0) && i < cx::space (1, 4) ? " (CONV)" : "");
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
    title (g, x0, y0, "KNOB " + String (selKnob + 1));
    actKey (g, aMain, x0, ky, 64, "MAIN", col::white, 5);
    actKey (g, aLibrary, x0 + 66, ky, 64, "LIBRARY", col::white, 5);
    actKey (g, aRenameKnob, x0 + 160, ky, 64, "RENAME", col::yellow, 5);
    actKey (g, aClearKnob, x0 + 226, ky, 64, "CLEAR", col::yellow, 5);
    const String help = "PICK A KNOB. TAP SNAPSHOTS ON THE RIGHT TO ADD THEM IN SWEEP ORDER (2 TO 10). TAP A NUMBERED SLOT TO TAKE IT OUT. "
                        "THE KNOB MOVES ONLY WHAT DIFFERS AMONG ITS SNAPSHOTS, AS AN OFFSET FROM THE BASE SCENE.";
    g.setColour (tone (col::black)); g.setFont (fLbl);
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
        case aCfgClose: cfgOpen = false; break;
        case aClipReset: eng.clipL = false; eng.clipR = false; break;
        case aSnapshot: selSnap = ses.take(); break;
        case aCue:      cueMode = ! cueMode; if (! cueMode) ses.cue (0); break;
        case aPhones:   eng.setPhones (! eng.phones()); repaint(); break;
        case aOnline:   if (online != nullptr) online->setOn (! online->on()); break;
        case aOnlineSetup: if (onOnlineSetup) onOnlineSetup(); break;
        case aRecallCue:
            if (const auto id = ses.cueing() != 0 ? ses.cueing() : selSnap; ses.findSnap (id) != nullptr) { selSnap = id; ses.recall (id); ses.cue (0); }
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
    const auto p = logical (e.position);
    const auto* h = hitAt (p);
    drag = {};
    mouseIsDown = true;
    if (hovering) { hovering = false; dirty (tipRect); tipRect = {}; }      // (the box is out of the hand's way)
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
        case hLane:
            ses.activeLane = h->a; ses.selStep = -1; ses.page = 0; ses.touch();
            if (cfgOpen && cfg.kind != 2) { cfg.lane = h->a; cfgStale = true; cfgScroll = 0; }
            break;
        case hConfig:   openConfig ({ h->a, h->b / 8, h->b % 8 }); break;
        case hSetup:
            if (h->a < (int) cfgParams.size())
            {
                const auto& sp = cfgParams[(size_t) h->a];
                cfgName = sp.name;
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
        case hInsHead:  ses.stepInsert (ses.activeLane, h->a); cfgStale = true; break;
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
            if (h->a == aDelete || (h->a == aRec && recorder != nullptr && recorder->recording())) break;       // these two are held
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
    if (const auto* h = hitAt (logical (e.position)))
    {
        if (h->kind == hKnob) ses.userSet (h->a, eng.control (h->a).def);
        else if (h->kind == hSnapKnob) ses.pressSnapKnob (h->a);       // press: its nearest snapshot becomes the base
        else if (h->kind == hDepth) { ses.knobs[(size_t) h->a].depth = 1.0f; ses.turnSnapKnob (h->a, ses.value (cx::kSnap + h->a)); }
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
    if (page != pMain) { libScroll = std::max (0, libScroll - (w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0)); repaint(); return; }
    if (cfgOpen && cfgRect.contains (logical (e.position))) { cfgScroll = std::max (0, cfgScroll - (w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0)); repaint(); return; }
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
    if (k == juce::KeyPress::escapeKey)
    {
        if (page != pMain) setPage (pMain);
        else if (cfgOpen) { cfgOpen = false; repaint(); }
        else if (onFullScreen) onFullScreen();
        return true;
    }
    if (k.getTextCharacter() == 'f' || k.getTextCharacter() == 'F') { if (onFullScreen) onFullScreen(); return true; }
    if (k.getTextCharacter() == 'l' || k.getTextCharacter() == 'L') { setLit (! lit); if (onLook) onLook(); return true; }
    if (k.getTextCharacter() == 'i' || k.getTextCharacter() == 'I') { setInfo (! info); return true; }
    if (k.getTextCharacter() == 'h' || k.getTextCharacter() == 'H') { act (aPhones); return true; }
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
        ses.activeLane = juce::jlimit (0, kLanes - 1, ses.activeLane + (k == juce::KeyPress::upKey ? -1 : 1));
        ses.selStep = -1; ses.page = 0;
        if (cfgOpen && cfg.kind != 2) { cfg.lane = ses.activeLane; cfgStale = true; cfgScroll = 0; }
        repaint();
        return true;
    }
    return false;
}

void Screen::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (files.size() == 0 || page != pMain) return;
    const auto p = logical ({ (float) x, (float) y });
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

// What the top bar shows that can change without a hand on it: RUN and the tempo, under an outside clock.
juce::uint64 Screen::barNow() const
{
    const bool rec = recorder != nullptr && recorder->recording();
    const bool note = recNote.isNotEmpty() && juce::Time::getMillisecondCounter() - recNoteAt < 8000;
    return (juce::uint64) (eng.isRunning() ? 1 : 0) | (juce::uint64) (eng.following() ? 2 : 0)
         | ((juce::uint64) (juce::uint32) std::lround (eng.target (cx::kTempo) * 14000.0f) << 2)
         | ((juce::uint64) (rec ? 1 + ((juce::int64) recorder->seconds() & 0xfffff) : 0) << 20)
         | ((juce::uint64) (rec && recorder->lostSeconds() > 0.0 ? 1 : 0) << 41) | ((juce::uint64) (note ? 1 : 0) << 42);
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

void Screen::timerCallback()
{
    if (lit && skin.settled()) { warm = false; repaint(); }     // the window has a new size and has held it: the pictures are made again for its pixels
    if (lit && ! warm && skin.ready() && ! mouseIsDown)
    {
        // CONFIG's plate and the cards of the other pages, made while nothing is waiting on them
        warm = true;
        skin.makePanel (554, 894, tone (col::grey));
        skin.makePanel (272, 66, tone (col::putty));
        skin.makePanel (272, 216, tone (col::putty));
    }
    // repaint what moved on its own: gliding knobs, the meters, the step that is playing
    for (auto& h : hits)
        if (h.kind == hKnob || h.kind == hSnapKnob || h.kind == hToggle || h.kind == hHold)
        {
            const int ctl = h.kind == hSnapKnob ? cx::kSnap + h.a : h.a;
            if (ctl >= 0 && ctl < cx::kCount && eng.shown (ctl) != lastShown[(size_t) ctl])
                dirty (ctl == cx::lane (ses.activeLane, cx::kSeqMute) ? seqRect : h.r.expanded (14, 6));       // a sequencer's mute greys its steps
        }
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
    if (mouseIsDown && drag.kind == hAct && drag.a == aDelete && ! drag.fired && juce::Time::getMillisecondCounter() - drag.t0 > 700)
    {
        drag.fired = true;
        ses.remove (selSnap);
        selSnap = 0;
        repaint();
    }
}
} // namespace sf0
