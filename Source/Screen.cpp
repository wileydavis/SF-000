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
// What the M key of a stack chip says (on the open stack's row and on a knob's column).
static const char* const kBlendKeySays = "LIT: AS A KNOB TURNS TOWARD THIS CHIP, EACH LANE IT TAKES BLENDS FROM THE INSTRUMENT IT HAS TO THIS CHIP'S: ITS MODEL, ITS SAMPLE, THE REST OF ITS CONFIG. BOTH SOUND, MIXED BY HOW FAR THE KNOB HAS COME, AS A RECALL WITH GLIDE CROSSES THEM OVER; AT THE CHIP IT IS THE CHIP'S ALONE, AND TURNED BACK IT RETURNS. OUT: THE LANES KEEP THE MODELS AND SAMPLES THEY HAVE, AND THE SOUND KNOBS ALONE FOLLOW THE TURN. A PRESS ON THE CHIP BRINGS THEM EITHER WAY.";
using juce::Colour;
using juce::String;
namespace col
{
    const Colour putty (0xffCFC8BC), grey (0xffABACA7), bluegrey (0xffA4B1B7), sea (0xff8D9794), white (0xffE8E5E1), black (0xff2B2B28),
                 green (0xff428F47), red (0xffB54142), redLit (0xffE4483C), amber (0xffC5843F), yellow (0xffD7C352), blue (0xff447BB5), teal (0xff4490A1),
                 unlit (0xff4D514E), lit (0xffE9E6E2);
}
// The dark instrument (the lit skin, with D): its plates, its print, a pale display's ground, its
// keys, a faint outline, a muted mark, and the room round the case.
namespace night
{
    const Colour plate (0xff3B3C3A), plate2 (0xff303231), ink (0xffD8D3C8), paper (0xff1A1C1B), key (0xff565955), keyOff (0xff484A47),
                 faint (0xff5A605D), dim (0xff7C807A), room (0xff0D0E0F);
}

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
    if (p != page && handId != 0 && ses.cueing() == handId) ses.cue (0);       // (a snapshot pressed on the page being left stops)
    if (p != page) named = {};         // (RENAME is for what is pressed on the page that is up)
    if (p != page) { kbOn = false; kbSeekKind = 0; }       // (the keyboard's mark is put away with the page it was on)
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

void Screen::onGlass (G& g, const String& s, R r, const juce::Font& f, Colour glassOf, Colour asked, juce::Justification j)
{
    textRaw (g, s, r, f, inkOn (glass (glassOf), asked), j);
}

juce::Rectangle<float> Screen::chipNameRect (R card, int number) const
{
    const float wTag = std::max (30.0f, std::ceil (tw (fLegend, String (number))) + 8.0f);
    return R (card.getX() + 6.0f + wTag, card.getY() + 6.0f, std::max (120.0f, card.getWidth() - 12.0f - wTag - 56.0f), 24.0f);
}

bool Screen::namedThere (const Named& n) const
{
    switch (n.what)
    {
        case 1: return ses.findSnap (n.chip) != nullptr;
        case 2: return n.knob >= 0 && n.knob < 16 && ! Session::isCorner (n.knob);
        case 3: return ses.rackIndex (n.id) >= 0;
        case 4: for (auto& k : ses.keptKnobs) if (k.id == n.id) return true; return false;
        case 5: for (auto& m : ses.modules) if (m.id == n.id) return true; return false;
        case 6: for (auto& a : ses.arrs) if (a.id == n.id) return true; return false;
        default: return false;
    }
}

Screen::Named Screen::namedNow() const
{
    if (named.what != 0 && namedThere (named)) return named;
    // (nothing pressed on this page yet: the page's own)
    if (page == pMacros) return Named { 2, 0, selKnob, {} };
    if (page == pArrange) return Named { 6, 0, -1, ses.arrAtId };
    if (page == pLibrary) { const auto id = ses.findSnap (selSnap) != nullptr ? selSnap : handId; if (ses.findSnap (id) != nullptr) return Named { 1, id, -1, {} }; }
    return {};
}

// RENAME: a box over the name of the thing last pressed (where it was pressed, if it is shown in more than one place; at
// the head of the strip if its name is not in view), typed in, then Return.
void Screen::renameNamed()
{
    const Named n = namedNow();
    if (n.what == 0 || ! namedThere (n)) { rackSay ("PRESS WHAT IS TO BE NAMED FIRST (A CHIP, A KNOB, A POCKET), THEN RENAME"); return; }
    R at (28.0f, 985.0f, 300.0f, 26.0f);
    float best = 1.0e9f;
    for (const auto& na : namesAt)
        if (na.n.what == n.what && na.n.chip == n.chip && na.n.knob == n.knob && na.n.id == n.id)
        {
            const float d = na.r.getCentre().getDistanceFrom (namedAt);
            if (d < best) { best = d; at = na.r.withWidth (std::max (na.r.getWidth(), 150.0f)); }
        }
    switch (n.what)
    {
        case 1: beginEdit (at, ses.findSnap (n.chip)->name, [this, id = n.chip] (String t) { ses.rename (id, t); }); break;
        case 2: beginEdit (at, ses.knobs[(size_t) n.knob].name, [this, k = n.knob] (String t) { ses.knobs[(size_t) k].name = t.trim().toUpperCase().substring (0, 10); ses.touch(); }); break;
        case 3:
            if (const int i = ses.rackIndex (n.id); ! ses.rackCanChange (i)) rackSay ("THAT STACK IS THE WEB LIBRARY'S: IT IS NAMED THERE");
            else beginEdit (at, ses.stacks[(size_t) i].name, [this, id = n.id] (String t) { ses.rackRename (ses.rackIndex (id), t); });
            break;
        case 4:
            for (int i = 0; i < (int) ses.keptKnobs.size(); ++i)
                if (ses.keptKnobs[(size_t) i].id == n.id) { beginEdit (at, ses.keptKnobs[(size_t) i].name(), [this, id = n.id] (String t) { for (int q = 0; q < (int) ses.keptKnobs.size(); ++q) if (ses.keptKnobs[(size_t) q].id == id) { ses.keptRename (q, t); break; } }); break; }
            break;
        case 5:
            for (int i = 0; i < (int) ses.modules.size(); ++i)
                if (ses.modules[(size_t) i].id == n.id) { beginEdit (at, ses.modules[(size_t) i].name, [this, id = n.id] (String t) { for (int q = 0; q < (int) ses.modules.size(); ++q) if (ses.modules[(size_t) q].id == id) { ses.moduleRename (q, t); break; } }); break; }
            break;
        case 6:
            for (int i = 0; i < (int) ses.arrs.size(); ++i)
                if (ses.arrs[(size_t) i].id == n.id) { beginEdit (at, ses.arrs[(size_t) i].name, [this, id = n.id] (String t) { for (int q = 0; q < (int) ses.arrs.size(); ++q) if (ses.arrs[(size_t) q].id == id) { ses.arrsRename (q, t); break; } }); break; }
            break;
        default: break;
    }
}

float Screen::holdFill (int a) const
{
    if (! (mouseIsDown && drag.kind == hAct && drag.a == a && heldAct (a)) || drag.fired) return 0.0f;
    return juce::jlimit (0.0f, 1.0f, (float) (juce::Time::getMillisecondCounter() - drag.t0) / (float) kHoldMs);
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

float Screen::transX() const { return 23.0f + std::ceil (tw (fFlag, "SF-000")) + 28.0f + 16.0f + 46.0f + 112.0f; }

void Screen::drawTrans (G& g, float x)
{
    const double left = ses.transBeatsLeft();
    const bool going = left > 0.0, waits = ses.transWaits();
    key (g, R (x, 17.5f, 40, 40), going ? col::amber : col::white, pressed (aTransFire) ? 6.0f : 2.0f, going);
    // its name; on its way, the bars left to its line
    text (g, going ? String (left / 4.0, 1) + " BARS" : String ("FIRE"), R (x + 46, 16.5f, 64, 22), fLegend, col::black, juce::Justification::centredLeft);
    plate (g, x + 46, 39.5f, waits ? "ARM " + String (ses.transWaiting()) : String ("ARM"), fLbl, 4, ses.transArmed ? (waits ? col::red : col::amber) : col::putty, col::black, 18);
    // what it plays, each stepped by a press: the bars between its lines and its level; its lead-in and its hit; how much also goes to the two returns
    const float cols[3] = { x + 112, x + 178, x + 292 };
    const int says[3][2] = { { Session::tBars, Session::tLevel }, { Session::tLead, Session::tHit }, { Session::tDub, Session::tSpace } };
    for (int c = 0; c < 3; ++c)
    {
        plate (g, cols[c], 17.5f, ses.transSays (says[c][0]), fLbl, 4, col::putty, col::black, 18);
        plate (g, cols[c], 39.5f, ses.transSays (says[c][1]), fLbl, 4, col::putty, col::black, 18);
    }
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
        // (held: it goes dark and fills with light from the left, and what it does is done when it is full. On the 3D stage the stage lights it.)
        const float f = holdFill (a);
        key (g, kr, f > 0.0f ? fill.darker (0.55f) : fill, pressed (a) ? 6.0f : 2.0f, on);
        if (f > 0.0f)
        {
            if (! (recording || printing))
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (kr.withWidth (kr.getWidth() * f).getSmallestIntegerContainer());
                if (lit) skin.key (g, kr, col::redLit, Parts::kHeld);
                else { g.setColour (col::redLit); g.fillRect (kr.reduced (2.0f)); }
            }
        }
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
    if (ctl == cx::kKey) { label = "KEY"; what = "THE KEY EVERY PITCHED LANE FOLLOWS: DRAG IT. THE PARTS MOVE WITH IT, AND THE STEPS SAY THE NOTES AS THEY SOUND. A NEW STEP IS THE KEY'S OWN NOTE."; return true; }
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
    if (j < kLaneKnobs) what = help::param (d.module, ses.engine.laneKnob (l, j).param);      // (the knob as the lane's model has it)
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
            const bool stack = Session::isStack (h.a);
            t.title = String (stack ? "STACK KNOB " : "MACRO KNOB ") + String (h.a + 1) + (kn.name.isNotEmpty() ? ": " + kn.name : String());
            if (stack)
                body = "IT HOLDS " + String (held) + (held == 1 ? " SNAPSHOT" : " SNAPSHOTS") + ", IN THE ORDER THE MACRO DESIGNER SHOWS. "
                     + (ses.stackCue ? "CUE IS LIT: A TURN GOES THROUGH THEM IN THE HEADPHONES AND THE SET IS NOT TOUCHED. DOUBLE-CLICK: THE SET GOES TO THE ONE IT IS ON, OVER THE GLIDE TIME."
                                     : "CUE IS OUT: A TURN MORPHS THE SET FROM WHEREVER IT IS TOWARD THE NEXT ONE THE WAY THE KNOB IS GOING, AND ON TO THE ONE AFTER. DOUBLE-CLICK: ALL THE WAY TO THE NEAREST.")
                     + " IT TURNS FINELY, A CLICK AT A TIME OF ITS 127 (SHIFT: FAST), AND ITS TAG SAYS WHERE IT IS AT: THE FIGURE TO COME BACK TO.";
            else
                body = (kn.links.empty() ? String ("TIED TO NOTHING YET: LEARN, IN THE MACRO DESIGNER, TIES IT TO KNOBS. ")
                                         : "TIED TO " + String ((int) kn.links.size()) + (kn.links.size() == 1 ? " KNOB" : " KNOBS") + " (LEARN, IN THE MACRO DESIGNER, WHICH LISTS THEM). TURN IT AND EACH GOES ITS OWN WAY, FROM WHERE IT STANDS. ")
                     + (h.a == Session::kCueKnob ? String ("DOUBLE-CLICK (ITS PRESS ON THE E16): CUE, ON OR OFF.")
                                                 : String ("DOUBLE-CLICK: PAST HALF WAY, WHAT IT HAS PUSHED IS KEPT AND THE KNOB GOES HOME; SHORT OF IT, THEY GO BACK."));
            midi = "MIDI: CHANNEL 16. CC " + String (e16::kKnobCc + h.a) + " TURNS IT. CC " + String (e16::kPressCc + h.a) + " (64 AND OVER) IS ITS PRESS."
                 + (stack ? " CC " + String (ccx::kRecall + h.a) + " RECALLS THE ONE ITS VALUE NAMES: 1 THE FIRST IT HOLDS, " + String (std::max (2, held)) + " THE LAST. A VALUE SAID AGAIN DOES NOTHING; 0 IS NONE." : String());
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
        case hRackKeep:
            t.title = "KEEP: THE STACK";
            body = "KEEPS THE CHOSEN STACK KNOB'S SNAPSHOTS AS A STACK, UNDER THE KNOB'S NAME: A COPY, EACH SNAPSHOT WITH THE PARTS IT TAKES. KEPT AGAIN UNDER THE SAME NAME, IT IS THE SAME ONE AS IT IS NOW.";
            break;
        case hArrTrans:
            t.title = h.b == 0 ? "A LEAD-IN INTO THIS PART" : "A HIT ON THIS PART";
            body = String (h.b == 0 ? "LIT: WHEN THE ARRANGEMENT PLAYS, A LEAD-IN IS SET OFF BY ITSELF THAT ENDS ON THIS PART'S BAR LINE. IT STARTS THE TRANSITION'S BARS BEFORE THAT LINE, OR WHEN THE PART BEFORE BEGINS IF THAT ONE IS SHORTER. "
                                    : "LIT: WHEN THE ARRANGEMENT PLAYS, A HIT LANDS ON THIS PART'S BAR LINE, BY ITSELF. ")
                   + "AMBER: THE SOUND THE BAR'S PLATES SAY. GREEN: A FILE OF THIS PART'S OWN. DROP A SOUND FILE ON THE PLATE TO GIVE IT ONE; A PRESS STEPS THROUGH ITS OWN FILE, THE BAR'S SOUND, AND OFF."
                   + (h.a < (int) ses.arrangement.size() && (h.b == 0 ? ses.arrangement[(size_t) h.a].inFile : ses.arrangement[(size_t) h.a].hitFile).isNotEmpty()
                        ? " ITS OWN: " + juce::File (h.b == 0 ? ses.arrangement[(size_t) h.a].inFile : ses.arrangement[(size_t) h.a].hitFile).getFileName().toUpperCase() + "." : String());
            break;
        case hLibTab:
            t.title = String ("THE SHELF: ") + (h.a == 0 ? "STACKS" : h.a == 1 ? "KNOBS" : h.a == 2 ? "MODULES" : "ARRANGEMENTS");
            body = h.a == 0 ? "THE STACKS, EACH A PILE IN ITS POCKET: CHIPS IN AN ORDER, UNDER A NAME. A STACK HOLDS REFERENCES: ONE CHIP CAN BE IN MANY. THE LAST POCKET IS A NEW ONE."
                 : h.a == 1 ? "THE KNOBS KEPT BY THEMSELVES: A STACK KNOB IS A PILE WITH A KNOB ON ITS RIM, A MACRO KNOB A CAP ON ITS BASE. THEY ARE KEPT FROM MACROS (KEEP KNOB)."
                 : h.a == 2 ? "THE MODULES KEPT: EACH A WHOLE SET OF THE SNAPSHOT KNOBS, FOUR MACRO KNOBS OVER TEN STACK KNOBS. THE ONE IN THE CHASSIS IS GREEN."
                            : "THE ARRANGEMENTS KEPT: EACH A RAIL, ITS PARTS AS LONG AS THEIR BARS. THE ONE ON THE ARRANGEMENT'S PAGE IS GREEN.";
            break;
        case hWell:
            t.title = h.a == 0 ? "THE BIN: ALL CHIPS" : h.a == 1 ? "A STACK" : h.a == 2 ? "A KNOB KEPT" : h.a == 3 ? "A MODULE" : h.a == 4 ? "AN ARRANGEMENT" : "A NEW STACK";
            body = h.a == 0 ? "EVERY CHIP, ONCE: THE LIBRARY ITSELF. PRESSED: WHATEVER WAS OPEN IS PUT AWAY AND THE BENCH IS THE BIN."
                 : h.a == 1 ? "A PILE OF REFERENCES TO CHIPS: THE TOP ONE SHOWS ITS FACE, AND EVERY ONE ITS EDGE. PRESSED: OPEN, ITS CHIPS OUT ON THE BENCH IN ORDER, TO ADD TO, TAKE FROM AND MOVE. A CHIP CARRIED ONTO IT GOES IN. RINGED: IT HOLDS THE CHIP IN HAND."
                 : h.a == 5 ? "PRESSED: A NEW STACK, EMPTY, OUT ON THE BENCH TO BE FILLED. A CHIP CARRIED ONTO IT STARTS ONE."
                            : "PRESSED: OPEN. WHAT IT HOLDS IS LAID OUT ON THE BENCH, AND THE KEYS ON THE STRIP ARE FOR IT.";
            break;
        case hRowCard:
            t.title = "A CHIP OF THE OPEN STACK";
            body = "PRESSED: IN HAND, AND HEARD IN THE HEADPHONES WITH THE PARTS IT TAKES HERE. CARRIED ALONG THE ROW: MOVED. OFF THE ROW: OUT OF THIS STACK (IT STAYS IN THE BIN). ONTO ANOTHER POCKET: INTO THAT STACK TOO. TWICE, QUICKLY: TO THE SET.";
            break;
        case hRowKey:
            t.title = h.b < kLanes ? "LANE " + String (h.b + 1) : h.b == kLanes ? String ("R: THE RETURNS") : h.b == kLanes + 1 ? String ("B: THE DRUM BUS") : h.b == kLanes + 2 ? String ("K: THE KEY") : h.b == kLanes + 3 ? String ("N: ITS NOTES") : String ("M: ITS MODELS AND SAMPLES");
            body = h.b == kLanes + 4 ? kBlendKeySays
                 : h.b == kLanes + 3 ? "LIT: A KNOB COMING TO THIS CHIP BRINGS ITS NOTES. OUT: THE SOUND ALONE, AND EVERY SEQUENCER STAYS AS IT IS."
                                     : "LIT: THIS CHIP TAKES THAT PART OF THE RIG IN THIS STACK. OUT: THE PART STAYS AS IT IS. HEARD AT ONCE, IF IT IS THE ONE IN THE HEADPHONES.";
            break;
        case hBenchChip:
            t.title = "A CHIP";
            body = "PRESSED: HEARD IN THE HEADPHONES WITH THE PARTS IT TAKES HERE; PRESSED AGAIN, IT STOPS. TWICE, QUICKLY (OR RECALL): TO THE SET.";
            break;
        case hTie:
            t.title = "A TIE";
            body = "A KNOB OF THE RIG THIS MACRO KNOB MOVES, AND HOW FAR ITS FULL TURN TAKES IT, IN HUNDREDTHS OF THAT KNOB'S TRAVEL (DEPTH, IN MACROS, SCALES THEM ALL). PRESSED: THE CHOSEN ONE, AND TAKE OUT UNTIES IT. DRAGGED UP OR DOWN: HOW FAR. A NEW TIE IS MADE WITH LEARN, IN MACROS, ON THE MODULE IN THE CHASSIS.";
            break;
        case hModKnob:
            t.title = "A KNOB OF THE MODULE";
            body = "PRESSED: WHAT IT HOLDS IS LAID OUT UNDER THE ROW. A STACK KNOB: ITS PILE, IN ORDER. A MACRO KNOB: THE KNOBS IT IS TIED TO. IN THE LIBRARY BOTH ARE CHANGED THERE.";
            break;
        case hArrKey:
            t.title = "A PART OF THE RIG";
            body = "BLACK: WHEN THE ARRANGEMENT REACHES THIS PART, THIS PART OF THE RIG COMES FROM ITS SNAPSHOT. AN EMPTY FRAME: IT STAYS AS IT IS THEN. 1 TO 11 ARE THE LANES, R THE RETURNS, B THE DRUM BUS, K THE KEY. N IS THE PART'S NOTES: BLACK, EACH LANE'S SEQUENCER IS GIVEN WHAT THE LANE WAS PLAYING IN THE SNAPSHOT; EMPTY, THE SEQUENCERS STAY AS THEY ARE. (THE LIBRARY'S NOTES KEY DOES NOT DECIDE THIS: IT IS FOR A RECALL MADE BY HAND.) HEARD AT ONCE WHILE THE PART IS IN THE HEADPHONES. PRINT TAKES IT THE SAME WAY.";
            break;
        case hSlot:
            t.title = "A SNAPSHOT OF THIS KNOB'S STACK";
            body = "PRESSED: HEARD IN THE HEADPHONES WITH THE PARTS IT TAKES HERE (AMBER: NOT ALL OF THEM), AND THE PLACE IS CHOSEN, SO ITS STACK IS IN THE COLUMN: ITS KEYS, ITS ORDER AND TAKING IT OFF ARE THERE.";
            break;
        case hPickKey:
            t.title = "A PART OF THE RIG";
            body = "ON THE SNAPSHOT IN HAND (THE ONE LAST PRESSED): BLACK, THIS PART GOES WITH IT WHEN IT IS RECALLED, ADDED TO A STACK OR PUT IN THE ARRANGEMENT; AN EMPTY FRAME, IT DOES NOT (AND STAYS AS IT IS NOW). 1 TO 11 ARE THE LANES, R THE RETURNS, B THE DRUM BUS, K THE KEY. HEARD AT ONCE WHILE IT IS IN THE HEADPHONES.";
            break;
        case hFind:
            t.title = h.a == 0 ? "WHOSE ARE LISTED" : h.a == 1 ? "RECENT" : h.a == 2 ? "FITS NOW" : h.a == 3 ? "MORE" : h.a == 4 ? "LESS" : "FIND";
            body = h.a == 0 ? "EVERY SNAPSHOT, OR A STACK'S, AND HOW MANY ARE LISTED NOW. PRESSED: THE NEXT STACK, THEN EVERY SNAPSHOT AGAIN."
                 : h.a == 1 ? "THE NEWEST FIRST. PRESSED AGAIN: AS THEY ARE KEPT."
                 : h.a == 2 ? "NEAREST IN SOUND TO WHAT IS PLAYING, FIRST. THE SET IS LISTENED TO WHEN THIS IS PRESSED, AND THE LIST STAYS PUT UNTIL IT IS PRESSED AGAIN. ONLY SNAPSHOTS WITH A SOUND KEPT."
                 : h.a == 3 ? "A STEP UP IN ENERGY FROM WHAT IS PLAYING, THE NEAREST STEP FIRST: THE TWELVE NEAREST IT IN COLOUR. AS THE E16'S MORE KNOB."
                 : h.a == 4 ? "A STEP DOWN IN ENERGY FROM WHAT IS PLAYING, THE NEAREST STEP FIRST: THE TWELVE NEAREST IT IN COLOUR. AS THE E16'S LESS KNOB."
                 : "TYPE PART OF A NAME, THEN RETURN: ONLY SNAPSHOTS WHOSE NAMES HOLD IT ARE LISTED. PRESSED WHILE IT IS LIT: ALL OF THEM AGAIN.";
            break;
        case hKeptKeep:
            t.title = "KEEP: THE KNOB";
            body = "KEEPS THE CHOSEN KNOB WITH THE KNOBS KEPT, UNDER ITS NAME: A COPY, AS IT IS NOW. A MACRO KNOB WITH EVERY KNOB IT IS TIED TO AND HOW FAR IT TAKES EACH; A STACK KNOB WITH ITS SNAPSHOTS AND THEIR PARTS. KEPT AGAIN UNDER THE SAME NAME, IT IS THE SAME ONE AS IT IS NOW.";
            break;
        case hColKey:
            t.title = h.b == kLanes + 4 ? "M: ITS MODELS AND SAMPLES" : h.b == kLanes + 3 ? "N: ITS NOTES" : "A PART OF THE RIG";
            body = h.b == kLanes + 4 ? kBlendKeySays
                 : h.b == kLanes + 3 ? "BLACK: WHEN THE KNOB COMES TO THIS SNAPSHOT (OR IS PRESSED ON IT), EACH LANE IT TAKES IS GIVEN WHAT THAT LANE WAS PLAYING IN THE SNAPSHOT, ON THE NEXT BAR LINE: AS A PART OF THE ARRANGEMENT WITH N LIT. AN EMPTY FRAME: THE NOTES STAY AS THEY ARE, AND ONLY THE SOUND FOLLOWS THE KNOB."
                                     : "BLACK: THIS SNAPSHOT TAKES THIS PART OF THE RIG WHEN THE KNOB COMES TO IT, IS PRESSED ON IT, OR PLAYS IT IN THE HEADPHONES. AN EMPTY FRAME: THE PART STAYS AS IT IS. 1 TO 11 ARE THE LANES, R THE RETURNS, B THE DRUM BUS, K THE KEY.";
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
                { aModules, "MODULES", "THE MODULES KEPT ON THE INSTRUMENT: EACH A SET OF KNOBS (FOUR MACRO KNOBS, TEN STACK KNOBS) UNDER A NAME, ONE FOR EACH SET YOU PLAY. THE ONE IN THE CHASSIS IS ALWAYS ONE OF THEM, AND WHAT YOU CHANGE CHANGES IT. PRESSED AGAIN: BACK TO THE KNOB." },
                { aNewTrack, "NEW", "HELD UNTIL IT IS FULL OF LIGHT: A NEW TRACK. THE RIG AS IT STARTS: NO STEPS, NO EFFECTS IN THE RACKS, EVERY KNOB WHERE THE RIG STARTS IT, EVERY MUTE AND SOLO OUT, THE MACRO KNOBS AT REST. TEMPO, THE MASTER, THE SNAPSHOT MODULE'S KNOBS AND THE LIBRARY STAY. TAKE A SNAPSHOT FIRST IF WHAT IS PLAYING IS WORTH KEEPING." },
                { aAudio, "SETUP", "WHAT IS SET ONCE, BEFORE THE SET: THE INTERFACE, ITS OUTPUTS AND BUFFER, MIDI AND THE CLOCK, FULL SCREEN, AND THE COLOUR AND BRIGHTNESS OF THE GLOW UNDER THE CASE. (PHONES, UNDER ITS NAME, IS ITS OWN SMALL KEY.)" },
                { aRec, "REC", "PRESS TO START A TAKE; HOLD TO END IT. THE SET, EVERY LANE AND THE TWO RETURNS ARE RECORDED." },
                { aSnapshot, "SNAPSHOT", "KEEPS EVERYTHING ON THE MAIN SCREEN AS A NEW SNAPSHOT, WITH THE BARS IT WAS HEARD OVER." },
                { aLibrary, "LIBRARY", "THE SNAPSHOT LIBRARY: TAP TO RECALL, RENAME, HEAR ONE IN THE HEADPHONES." },
                { aMacros, "MACROS", "THE DESIGNER FOR THE SIXTEEN KNOBS. 1 TO 4 ARE MACRO KNOBS: LEARN TIES ONE STRAIGHT TO OTHER KNOBS. 5 TO 12, 14 AND 15 ARE STACK KNOBS (13 AND 16 ARE THE E16'S LANE WHEEL AND OPEN, AND HOLD NONE): EACH HOLDS SNAPSHOTS IN AN ORDER (THE WEB LIBRARY IS WHERE STACKS ARE BUILT; ONE CAN BE MADE HERE TOO). A STACK'S SNAPSHOTS ARE ALSO WHAT RECALL (CC 80 TO 95 ON CHANNEL 16) PICKS FROM." },
                { aStackCue, "CUE", "HOW THE STACK KNOBS (5 TO 12, 14, 15) ARE PLAYED. OUT: A TURN MORPHS THE SET ITSELF, FROM WHEREVER IT IS TOWARD THE NEXT SNAPSHOT THE WAY THE KNOB IS GOING, THEN ON TO THE ONE AFTER; A PRESS GOES ALL THE WAY TO THE NEAREST. LIT: A TURN GOES THROUGH THE STACK IN THE HEADPHONES AND THE SET IS NOT TOUCHED; A PRESS MORPHS THE SET TO THE ONE THE KNOB IS ON, OVER THE GLIDE TIME. KNOB 4 PRESSED (HERE AND ON THE E16) IS THIS KEY." },
                { aMain, "MAIN", "BACK TO THE MAIN SCREEN." },
                { aNudgeL, "NUDGE", "MOVES THE WHOLE PATTERN ONE STEP EARLIER." },
                { aNudgeR, "NUDGE", "MOVES THE WHOLE PATTERN ONE STEP LATER." },
                { aRatchet, "RATCHET", "THE SELECTED STEP PLAYS ONE TO FOUR HITS." },
                { aNote, "NOTE", "THE SELECTED STEP'S NOTE, AS IT SOUNDS. A NEW STEP IS THE KEY'S NOTE (C WITH KEY OFF): EACH PRESS IS A SEMITONE UP FROM THERE." },
                { aOctave, "OCTAVE", "THE SELECTED STEP'S OCTAVE." },
                { aGen0, "RANDOM", "WRITES A NEW PATTERN AT EVERY PRESS (SEED GOES TO A NEW PLACE). WHILE IT IS LIT, HITS, ROTATE, ACCENTS AND SEED REWRITE IT. A RHYTHM CHANGED BY HAND IS YOURS: THE KEY GOES OUT, AND THE KNOBS LET GO." },
                { aGen1, "EUCLID", "WRITES A PATTERN WITH ITS HITS SPREAD EVENLY. WHILE IT IS LIT, HITS, ROTATE AND ACCENTS REWRITE IT; PRESSED AGAIN, OR ONCE THE RHYTHM IS CHANGED BY HAND, IT LETS GO." },
                { aRename, "RENAME", "NAMES THE THING LAST PRESSED THAT HAS A NAME: A CHIP, A KNOB, A STACK, A KNOB KEPT, A MODULE, AN ARRANGEMENT. TYPE, THEN RETURN. THE SAME KEY, IN THE SAME PLACE, ON EVERY PAGE THAT HAS IT." },
                { aAddTo, "ADD TO", "LIT: THE NEXT STACK'S POCKET PRESSED TAKES THE CHIP IN HAND (A REFERENCE: THE CHIP STAYS IN THE BIN, AND IN EVERY OTHER STACK). PRESSED AGAIN: LEFT OFF." },
                { aRowAdd, "ADD", "THE CHIP IN HAND IN THE BIN GOES INTO THE OPEN STACK, LAST, WITH THE PARTS LIT ON ITS CARD. THE STACK HOLDS A REFERENCE: THE CHIP STAYS IN THE BIN." },
                { aRowOut, "TAKE OUT", "THE CHIP IN HAND IN THE ROW COMES OUT OF THIS PILE. IT STAYS IN THE BIN AND IN EVERY OTHER STACK. ON AN OPEN MACRO KNOB: THE TIE PRESSED IS TAKEN OUT, AND THAT KNOB OF THE RIG IS NO LONGER MOVED BY IT." },
                { aRowEarlier, "EARLIER", "THE CHIP IN HAND IN THE ROW, ONE PLACE EARLIER IN THE STACK'S ORDER." },
                { aRowLater, "LATER", "THE CHIP IN HAND IN THE ROW, ONE PLACE LATER IN THE STACK'S ORDER." },
                { aOpenNew, "NEW", "A NEW ONE, EMPTY, KEPT AND PUT TO WORK: A MODULE IN THE CHASSIS, AN ARRANGEMENT ON ITS PAGE. THE ONE THAT WAS THERE IS KEPT AS IT STOOD." },
                { aOpenCopy, "COPY", "KEPT AGAIN UNDER ANOTHER NAME: A STACK, THE ONE THAT IS OPEN; A MODULE OR AN ARRANGEMENT, THE ONE IN USE (WHICH THEN HOLDS THE COPY)." },
                { aOpenLoad, "PUT TO WORK", "WHAT IS OPEN IS PUT TO WORK: A STACK OR A KEPT KNOB ONTO THE FIRST KNOB OF ITS KIND THAT HOLDS NOTHING (A COPY), A MODULE INTO THE CHASSIS, AN ARRANGEMENT ONTO ITS PAGE." },
                { aOpenDelete, "DELETE", "HELD UNTIL IT IS FULL OF LIGHT: WHAT IS OPEN GOES FROM THOSE KEPT. ITS CHIPS STAY IN THE BIN. NEVER THE MODULE IN THE CHASSIS, NEVER THE LAST ARRANGEMENT." },
                { aDelete, "DELETE", "HELD UNTIL IT IS FULL OF LIGHT: THE CHOSEN CHIP IS DELETED, AND GOES FROM EVERY STACK THAT HELD IT." },
                { aRenameKnob, "RENAME", "NAMES THIS SNAPSHOT KNOB." },
                { aClearKnob, "CLEAR", "HELD UNTIL IT IS FULL OF LIGHT: THE CHOSEN KNOB THEN HOLDS NOTHING. A STACK KNOB'S CHIPS COME OFF IT (THEY STAY IN THE BIN), A MACRO KNOB IS UNTIED FROM EVERY KNOB." },
                { aLearn, "LEARN", "FOR THE MACRO KNOBS (1 TO 4). TIES THIS KNOB STRAIGHT TO OTHER KNOBS. IT GOES TO ITS FULL TURN AND THE MAIN SCREEN COMES UP: TURN THE KNOBS IT SHOULD MOVE (HERE OR ON A CONTROLLER) TO WHERE THE FULL TURN TAKES THEM, AND KEEP. LEARN AGAIN ADDS TO WHAT IT HAS; A KNOB TURNED BACK TO WHERE IT WAS IS LET GO." },
                { aLearnKeep, "KEEP", "TIES THE KNOBS MOVED SINCE LEARN TO THE KNOB BEING TAUGHT. NOTHING CHANGES IN THE SOUND: THE KNOB IS AT ITS FULL TURN, AND TURNED BACK THEY GO BACK TO WHERE THEY WERE." },
                { aLearnDrop, "DROP", "PUTS BACK THE KNOBS MOVED SINCE LEARN AND TIES NOTHING." },
                { aCfgClose, "X", "CLOSES CONFIG." },
                { aClipReset, "CLIP LAMPS", "RED: THE OUTPUT REACHED FULL SCALE. CLICK TO CLEAR." },
                { aRecallCue, "RECALL", "TAKES THE SNAPSHOT IN THE HEADPHONES (OR THE ONE CHOSEN) TO THE SET." },
                { aNotes, "NOTES", "WHETHER A RECALL BRINGS A SNAPSHOT'S NOTES. LIT: EACH LANE'S SEQUENCER IS GIVEN WHAT THE LANE WAS PLAYING WHEN THE SNAPSHOT WAS TAKEN (ITS OWN STEPS, AND THE NOTES THAT WERE COMING IN FROM OUTSIDE, AS STEPS) AND PLAYS IT: THE SNAPSHOT SOUNDS AS IT DID WITH NOTHING PLAYING INTO THE RIG. OUT: A RECALL CHANGES THE SOUND AND LEAVES EVERY SEQUENCER AS IT IS, FOR WHEN A SEQUENCER OUTSIDE IS PLAYING THE LANES. N ON THE KEYBOARD TOO. EVERY RECALL GOES BY IT: A TAP IN THE LIBRARY, A SNAPSHOT KNOB'S PRESS, A SEQUENCER'S RECALL." },
                { aOnline, "ONLINE", "EACH SNAPSHOT GOES TO THE LIBRARY ON THE WEB AS IT IS TAKEN. GREEN: IT HOLDS THEM ALL. YELLOW: SENDING. AMBER: WAITING. RED: TURNED AWAY." },
                { aOnlineSetup, "SET UP", "THE WEB LIBRARY'S ADDRESS AND ITS KEY." },
                { aFetch, "WEB LOAD", "MAKES THE SIXTEEN KNOBS WHAT THE WEB LIBRARY'S MODULE HOLDS NOW: ITS FOUR MACRO KNOBS, ITS TEN STACK KNOBS (EACH SNAPSHOT WITH THE LANES CHOSEN FOR IT THERE), AND THE SNAPSHOTS THIS LIBRARY LACKS. WHAT A KNOB HELD HERE IS SENT TO THE LIBRARY FIRST, SO NOTHING IS LOST. A SONG COMES AS THE ARRANGEMENT (IF THERE IS NONE)." },
                { aArrange, "ARRANGE", "THE ARRANGEMENT: SNAPSHOTS IN A LINE, EACH FOR SO MANY BARS, EACH REACHED BY A MORPH FROM THE ONE BEFORE. PLAYED FROM THE CLOCK, AND PRINTED TO FILES FOR A RELEASE. A ON THE KEYBOARD TOO. LIT GREEN WHILE IT PLAYS." },
                { aArrPlay, "PLAY", "PLAYS THE ARRANGEMENT FROM ITS FIRST PART. FROM STOPPED: THAT PART IS RECALLED AT ONCE AND THE CLOCK STARTS FROM THE TOP. WITH THE CLOCK RUNNING: IT COMES IN ON THE NEXT BAR LINE. EACH PART AFTER IS RECALLED SO AS TO SET OFF ON ITS OWN BAR LINE; AT THE END THE CLOCK STOPS. PRESSED WHILE IT PLAYS: STOP." },
                { aArrHere, "FROM HERE", "PLAYS IT FROM THE PART THAT IS CHOSEN, WITH WHAT EVERY PART BEFORE IT LEFT: A PART WITH A KEY OUT SOUNDS AS IT DOES WHEN THE ARRANGEMENT REACHES IT." },
                { aArrEdit, "EDIT", "THE PART THAT IS CHOSEN, IN THE MAIN VIEW. THE ARRANGEMENT STOPS, THE RIG IS MADE WHAT THE ARRANGEMENT HAS AT THAT PART (WHAT EVERY PART BEFORE IT LEFT, THEN IT, ARRIVED), AND IT PLAYS. CHANGE ANYTHING. KEEP, IN THE BAR THERE, MAKES THE RIG AS IT THEN STANDS A NEW SNAPSHOT IN THE LIBRARY, NAMED AFTER THE PART'S, AND PUTS IT IN THE PART'S PLACE: THE SNAPSHOT IT WAS IS NOT TOUCHED. THE PART KEEPS ITS KEYS AND GAINS THOSE OF WHATEVER YOU CHANGED. DROP KEEPS NOTHING. EDITED AGAIN, THE SNAPSHOT THE LAST EDIT MADE IS REPLACED, UNLESS SOMETHING ELSE USES IT." },
                { aArrStack, "TO STACK", "KEEPS THE ARRANGEMENT AS A STACK IN THE MACRO DESIGNER'S RACK: ITS PARTS' SNAPSHOTS IN ORDER, EACH WITH THE KEYS ITS PART HAS. PUT ON A STACK KNOB THERE, THE KNOB'S TURN IS THE TRACK FROM FIRST PART TO LAST. A PART WITH A KEY OUT LEANS ON WHAT CAME BEFORE IT, ON A KNOB AS HERE." },
                { aEditKeep, "KEEP", "EDIT: THE RIG AS IT STANDS BECOMES A NEW SNAPSHOT AND TAKES THE PART'S PLACE IN THE ARRANGEMENT (RETURN). THE SNAPSHOT THE PART HAD IS LEFT AS IT IS." },
                { aEditDrop, "DROP", "EDIT: NOTHING IS KEPT, AND THE ARRANGEMENT IS AS IT WAS. THE RIG STAYS AS YOU LEFT IT." },
                { aArrEarlier, "EARLIER", "MOVES THE CHOSEN PART ONE PLACE EARLIER." },
                { aArrLater, "LATER", "MOVES THE CHOSEN PART ONE PLACE LATER." },
                { aArrRemove, "REMOVE", "TAKES THE CHOSEN PART OUT OF THE ARRANGEMENT. ITS SNAPSHOT STAYS IN THE LIBRARY." },
                { aArrClear, "CLEAR", "HELD UNTIL IT IS FULL OF LIGHT: TAKES EVERY PART OUT." },
                { aArrPrint, "PRINT", "RENDERS THE ARRANGEMENT TO FILES, FASTER THAN IT PLAYS AND THE SAME EVERY TIME: THE TRACK (24-BIT, BAR 1 ON ITS FIRST SAMPLE, WHAT RINGS ON AFTER THE LAST BAR KEPT) AND EVERY LANE AND RETURN BESIDE IT, IN A FOLDER OF ITS OWN WHERE TAKES GO. IT IS MADE ON A RIG OF ITS OWN FROM THE SESSION AS IT STANDS, SO THIS ONE IS NOT TOUCHED; THE SOUND STOPS UNTIL IT IS DONE. EACH PART BRINGS ITS OWN NOTES; THE SNAPSHOT KNOBS STAND WHERE THEY ARE AND MOTION RUNS. PRESSED WHILE IT PRINTS: STOP." },
                { aPockets, "POCKETS", "THE MASTER'S PAGE FOR WHO HAS WHICH PART OF THE SPECTRUM: EVERY LANE'S POCKET ON A MAP OF THE MIX, WHAT EACH IS PLAYING AND WHAT IS BEING TAKEN FROM IT. P ON THE KEYBOARD TOO; ON THE E16, OPEN TWICE WITH THE MASTER IN FRONT (ITS CONFIG PAGES THEN HOLD EVERY LANE'S HOME, WIDTH, YIELD AND HOLD). THE NUMBER BESIDE IT IS AMOUNT." },
                { aPkAuto, "AUTO", "PLACES THE POCKETS FROM WHAT IS PLAYING. EVERY LANE HEARD IN THE LAST SECONDS IS GIVEN A HOME AND A WIDTH: THE LANES WITH MOST HOLD CHOOSE FIRST, EACH TAKING THE STRETCH THAT HAS MOST OF ITS OWN SOUND AND LEAST OF WHAT THOSE BEFORE IT TOOK. HOLD AND YIELD STAY AS THEY ARE. THE POCKETS TRAVEL THERE OVER THE GLIDE TIME." },
                { aPkReset, "RESET", "EVERY LANE'S POCKET BACK TO WHERE THE RIG STARTS IT: HOME, WIDTH, HOLD AND YIELD. AMOUNT, CLEAR, SPEED AND RETURNS STAY." },
                { aResample, "RESAMPLE", "THE BARS OF THE SNAPSHOT LAST TAKEN OR RECALLED BECOME THIS LANE'S SAMPLE: ITS WHOLE OUTPUT, AS IT WAS KEPT, WRITTEN INTO THE LIBRARY'S SAMPLES AND LOADED AS LOAD WOULD. WHAT THE RIG JUST PLAYED IS THEN SOMETHING TO SLICE (SAMPLE, NOTES: SLICES), LOOP, OR MAKE GRAINS OF (GRANULAR; SYNC 4 BARS HOLDS IT TO THE TEMPO). THE LANE'S MODEL IS LEFT AS IT IS." },
                { aSamplePlain, "PLAIN", "SETS THIS LANE TO PLAY ITS SAMPLE AS IT WAS RECORDED: THE FILTER OPEN, NO LAYER AND NO CROSSOVER (SPLIT TAKES THE SAMPLE'S OWN LOWS OUT), THE WHOLE NOTE, NO DRIVE, THE LANE'S EQ FLAT. WHERE THE NOTE IS THE PITCH (BASS, LEAD, CHORD, PAD), TUNE IS SET SO THAT THE FIRST LIT STEP PLAYS IT AT ITS OWN. LEVEL, THE SENDS AND THE LANE'S INSERTS ARE LEFT AS THEY ARE. A SAMPLE IS AS LOUD AS ITS FILE: PEAK, HERE, SAYS HOW LOUD THAT IS." },
                { aTransFire, "FIRE", "TRANSITION (T). SETS OFF A LEAD-IN THAT ENDS ON THE NEXT BAR LINE AND A HIT THAT LANDS ON IT, WHENEVER IT IS PRESSED: THE MACHINE KEEPS THE TIME. LINES ARE EVERY 4, 8 OR 16 BARS; UNDER A BEAT FROM ONE, IT IS THE ONE AFTER. WHILE IT IS ON ITS WAY THE KEY IS LIT AND SAYS THE BARS LEFT. WITH THE CLOCK STOPPED THE HIT PLAYS AT ONCE." },
                { aTransArm, "ARM", "TRANSITION (G). LIT: THE NEXT RECALL, MUTE OR SOLO DOES NOT HAPPEN AT ONCE. IT SETS THE TRANSITION OFF AND HAPPENS ON THE LINE THE TRANSITION LANDS ON, WITH WHATEVER ELSE IS ASKED FOR MEANWHILE (THE NUMBER WAITING IS SHOWN). ARM THEN GOES OUT. PUT OUT BY HAND, WHAT WAITS HAPPENS AT ONCE." },
                { aTransBars, "BARS", "TRANSITION: HOW FAR APART ITS LINES ARE. 4, 8 OR 16 BARS." },
                { aFinish, "FINISH", "THE MASTER'S LAST STAGE BEFORE ITS LIMITER, STEPPED BY A PRESS: OFF, 25, 50, 75, 100. IT HOLDS THE MIX IN THREE BANDS (THE MIDDLE ONE MOST, GENTLY AND FROM FAR DOWN; THE LOWS HARDLY; THE HIGHS ONLY WHEN THEY ARE LOUD), ADDS A LITTLE HARMONIC TO EACH, LIFTS THE MIDDLE OF THE SPECTRUM A DECIBEL AND EASES THE TOP, AND OPENS THE SIDES WHERE THEY ARE BRIGHT. IT FINDS ITS OWN LEVEL FROM THE LOUDEST OF THE LAST MINUTE, SO IT DOES THE SAME TO A QUIET SET AS TO A LOUD ONE. IT DOES NOT MAKE THE SET LOUDER: LIMIT DOES THAT. KEPT WITH THE SET, AND IN A PRINT." },
                { aTransLevel, "LEVEL", "TRANSITION: HOW LOUD ITS LEAD-IN AND ITS HIT ARE, INTO THE MIX BEFORE THE MASTER." },
                { aTransLead, "LEAD-IN", "TRANSITION: WHAT ENDS ON THE LINE. OFF, THE BUILT-IN RISER, OR A FILE DROPPED ON THIS PLATE (IT STARTS WHEN IT MUST TO END ON THE LINE; WITH LESS TIME THAN IT IS LONG, PART WAY IN)." },
                { aTransHit, "HIT", "TRANSITION: WHAT STARTS ON THE LINE. OFF, THE BUILT-IN IMPACT, OR A FILE DROPPED ON THIS PLATE (A STAB, A CRASH)." },
                { aTransDub, "TO DUB", "TRANSITION: HOW MUCH OF IT ALSO GOES INTO RETURN A, THE DUB DELAY." },
                { aTransSpace, "TO SPACE", "TRANSITION: HOW MUCH OF IT ALSO GOES INTO RETURN B, SPACE." },
                { aPhones, "PHONES", "FOR HEADPHONES ON THE ONE PAIR OF OUTPUTS. WHILE A SNAPSHOT PLAYS IN THEM, THE SET IS IN THE LEFT EAR AND THE SNAPSHOT IN THE RIGHT; WITH NONE PLAYING, THE SET IS AS ALWAYS. H ON THE KEYBOARD TOO, AND THE SMALL PLATE UNDER SETUP ON EVERY PAGE (GREEN WHILE IT IS LIT). IT IS OFF EACH TIME SF-000 OPENS, SO IT IS NEVER LEFT ON FOR A PA." },
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
    { "ESC", "BACK OUT: OF LOAD'S LIST, CONFIG, A PAGE, FULL SCREEN" },
    { "H", "PHONES" },
    { "N", "NOTES: WHETHER A RECALL BRINGS A SNAPSHOT'S NOTES" },
    { "UP  DOWN", "MAIN: THE ACTIVE LANE" },
    { "TAB", "MAIN: THE NEXT MODULE TO THE FRONT (SHIFT: THE ONE BEFORE)" },
    { "C", "CONFIG FOR WHAT IS IN FRONT, AND SHUT" },
    { "P", "POCKETS: WHO HAS WHICH PART OF THE SPECTRUM" },
    { "RETURN", "LEARN: KEEP WHAT THE KNOB HAS BEEN SHOWN" },
    { "LEFT  RIGHT", "MAIN: THE SEQUENCER'S PAGE OF STEPS" },
    { "ARROWS", "LIBRARY, MACROS, ARRANGE: THE MARK, OVER POCKETS, CHIPS, KNOBS, PARTS" },
    { "RETURN", "THERE: PRESSES WHAT THE MARK IS ON (SHIFT, ON A CHIP: RECALL)" },
    { "=", "THERE: ADD THE CHIP THE MARK IS ON" },
    { "BACKSPACE", "THERE: TAKE OUT WHAT THE MARK IS ON" },
    { "[  ]", "THERE: EARLIER, LATER (A TIE: LESS, MORE. SHIFT: BY TEN)" },
    { "TAB", "THERE: THE NEXT SHELF (SHIFT: THE ONE BEFORE)" },
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
    if (collect) { hits.clear(); namesAt.clear(); for (auto& r : stopRects) r = {}; kbStale = false; }
    if (lit) { skin.begin (g); if (vis (g, R (0, 0, kW, kH))) skin.body (g, kW, kH); }
    else if (vis (g, R (0, 0, kW, kH)))
    {
        g.setColour (col::sea);
        g.fillRoundedRectangle (R (8, 12, 1712, 1092), 8.0f);
    }
    // (a snapshot something else put in the headphones, a stack knob or a card of the column, is the one in hand, with the parts it plays with)
    if (const auto c = ses.cueing(); c != 0 && c != handId && ses.findSnap (c) != nullptr)
    {
        handId = c; handParts = Session::kAllLanes;
        if (const auto* sc = ses.cueNarrowed (c)) handParts = Session::lanesOf (*sc) & Session::kAllLanes;
    }
    drawBar (g);
    if (page == pMain) drawMain (g);
    else if (page == pLibrary) drawLibrary (g);
    else if (page == pPockets) drawPockets (g);
    else if (page == pArrange) drawArrange (g);
    else drawMacros (g);
    if (rackPage()) drawKb (g); else kbOn = false;
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
        if (page != pMain) title (g, 23 + w + 12, 24.5f, page == pLibrary ? "LIBRARY" : page == pPockets ? "POCKETS" : page == pArrange ? "ARRANGEMENT" : "MACRO DESIGNER");
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
            const float tx = kx + 196, tw2 = std::max (60.0f, 796.0f - tx);
            text (g, "SET THE KNOBS AS ITS FULL TURN SHOULD LEAVE THEM.", R (tx, 20, tw2, 17), fLbl, col::black, juce::Justification::centredLeft);
            text (g, n == 0 ? String ("NONE MOVED YET.") : String (n) + " MOVED (LIT YELLOW).", R (tx, 38, tw2, 17), fLbl, col::black, juce::Justification::centredLeft);
        }
        else if (ses.arrEdit >= 0)
        {
            // EDIT: which part of the arrangement the rig is, and the two ways out
            title (g, 23 + w + 12, 24.5f, editTitle(), true);
            const float kx = editKeysX();
            key (g, R (kx, 17.5f, 40, 40), col::yellow, pressed (aEditKeep) ? 6.0f : 2.0f);
            text (g, "KEEP", R (kx + 46, 17.5f, 44, 40), fLegend, col::black, juce::Justification::centredLeft);
            key (g, R (kx + 96, 17.5f, 40, 40), col::white, pressed (aEditDrop) ? 6.0f : 2.0f);
            text (g, "DROP", R (kx + 142, 17.5f, 44, 40), fLegend, col::black, juce::Justification::centredLeft);
            const float tx = kx + 196, tw2 = std::max (60.0f, 796.0f - tx);
            text (g, "KEEP: A NEW SNAPSHOT", R (tx, 20, tw2, 17), fLbl, col::black, juce::Justification::centredLeft);
            text (g, "IN THE PART'S PLACE.", R (tx, 38, tw2, 17), fLbl, col::black, juce::Justification::centredLeft);
        }
        else if (info)
        {
            text (g, "INFO (I): A CONTROL'S NUMBER IS ITS MIDI CC. A LANE'S IS ITS CHANNEL; ITS INSERTS ARE ON IT TOO.", R (23 + w + 16, 20, 780.0f - (23 + w + 16), 17), fLbl, col::black, juce::Justification::centredLeft);
            text (g, "RETURNS, MASTER, GLIDE, SNAPSHOT KNOBS: CHANNEL 16. REST THE MOUSE ON A CONTROL FOR WHAT IT DOES.", R (23 + w + 16, 38, 780.0f - (23 + w + 16), 17), fLbl, col::black, juce::Justification::centredLeft);
        }
        // NEW: held, the rig as it starts, for a new track (beside the flag, where LEARN's keys and INFO's lines go when they are up)
        if (page == pMain && ses.learning < 0 && ses.arrEdit < 0 && ! info)
        {
            const R nk (23 + w + 16, 17.5f, 40, 40);
            const float f = holdFill (aNewTrack);
            key (g, nk, f > 0.0f ? col::white.darker (0.55f) : col::white, pressed (aNewTrack) ? 6.0f : 2.0f);
            if (f > 0.0f)
            {
                if (! (recording || printing))
                {
                    juce::Graphics::ScopedSaveState ss (g);
                    g.reduceClipRegion (nk.withWidth (nk.getWidth() * f).getSmallestIntegerContainer());
                    if (lit) skin.key (g, nk, col::redLit, Parts::kHeld);
                    else { g.setColour (col::redLit); g.fillRect (nk.reduced (2.0f)); }
                }
            }
            text (g, "NEW", R (23 + w + 16 + 46, 17.5f, 110, 40), fLegend, col::black, juce::Justification::centredLeft);
        }
        if (transShown()) drawTrans (g, transX());
        // SNAPSHOT: everything on the main screen, kept (beside REC and RUN: the three keys of a set)
        key (g, R (808, 17.5f, 40, 40), col::yellow, pressed (aSnapshot) ? 6.0f : 2.0f);
        text (g, "SNAPSHOT", R (854, 17.5f, 92, 40), fLegend, col::black, juce::Justification::centredLeft);
        // REC: press to start a take, hold to end it. The time it has run stands in for its name.
        {
            const bool on = recorder != nullptr && recorder->recording();
            key (g, R (950, 17.5f, 40, 40), on ? (recorder->lostSeconds() > 0.0 ? col::amber : col::red) : col::putty,
                 pressed (aRec) ? 6.0f : 2.0f, on);
            String t ("REC");
            if (on) { const auto s = (juce::int64) recorder->seconds(); t = String (s / 3600) + ":" + String ((s / 60) % 60).paddedLeft ('0', 2) + ":" + String (s % 60).paddedLeft ('0', 2); }
            text (g, t, R (996, 17.5f, 70, 40), fLegend, col::black, juce::Justification::centredLeft);
            if ((page == pMain || page == pMacros) && recNote.isNotEmpty() && juce::Time::getMillisecondCounter() - recNoteAt < recNoteMs())
            {
                // (a note goes left from SNAPSHOT as far as TRANSITION's FIRE and ARM, over what it plays; a longer one is cut short)
                const float most = 798.0f - (transShown() ? transX() + 112.0f : 30.0f);
                String t = recNote;
                while (t.length() > 8 && std::ceil (tw (fNote, t)) + 12.0f > most) t = t.dropLastCharacters (4);
                if (t.length() < recNote.length()) t << "...";
                plate (g, 798.0f - (std::ceil (tw (fNote, t)) + 12.0f), 24.5f, t, fNote, 6, col::white, col::black);
            }
        }
        // SETUP is SETUP whatever is lit. Under its name, on every page, PHONES: a small plate, green while it is lit (this output
        // is then split while a snapshot plays in the headphones); pressed, it is lit or put out, as H does.
        text (g, "SETUP", R (1116, 16.5f, 60, 22), fLegend, col::black, juce::Justification::centredLeft);
        plate (g, 1116.0f, 39.5f, "PHONES", fLbl, 4, eng.phones() ? col::green : col::putty, col::black, 18);
        text (g, "RUN", R (1222, 17.5f, 40, 40), fLegend, col::black, juce::Justification::centredLeft);
        text (g, "PANIC", R (1314, 17.5f, 56, 40), fLegend, col::black, juce::Justification::centredLeft);
        key (g, R (1070, 17.5f, 40, 40), col::white, pressed (aAudio) ? 6.0f : 2.0f);
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
    if (editShown())
    {
        hit (R (editKeysX(), 17.5f, 40, 40).expanded (3), hAct, aEditKeep, 0);
        hit (R (editKeysX() + 96, 17.5f, 40, 40).expanded (3), hAct, aEditDrop, 0);
    }
    if (page == pMain && ses.learning < 0 && ses.arrEdit < 0 && ! info) hit (R (23 + std::ceil (tw (fFlag, "SF-000")) + 28.0f + 16, 17.5f, 40, 40), hAct, aNewTrack, 0);
    if (transShown())
    {
        const float x = transX();
        hit (R (x, 17.5f, 40, 40), hAct, aTransFire, 0);
        hit (R (x + 44, 38.5f, 64, 20), hAct, aTransArm, 0);
        const float cols[3] = { x + 112, x + 178, x + 292 }, ws[3] = { 62, 110, 76 };
        for (int c = 0; c < 3; ++c) { hit (R (cols[c] - 2, 16.5f, ws[c], 20), hAct, aTransBars + c * 2, 0); hit (R (cols[c] - 2, 38.5f, ws[c], 20), hAct, aTransLevel + c * 2, 0); }
    }
    hit (R (808, 17.5f, 40, 40), hAct, aSnapshot, 0);
    hit (R (950, 17.5f, 40, 40), hAct, aRec, 0);
    hit (R (1070, 17.5f, 40, 40), hAct, aAudio, 0);
    hit (R (1114, 38.5f, 56, 20), hAct, aPhones, 0);         // (PHONES, under SETUP's name: on every page)
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
    const bool plain = cfg.kind == 0 && ses.playsSample (cfg.lane);        // (a lane playing a sample: PLAIN is there, beside LOAD)
    if (cfg.kind == 0 && m.acceptsFiles())
    {
        const String f = juce::File (eng.laneFile (cfg.lane)).getFileName();
        status << (status.isEmpty() ? "" : "   ") << (f.isNotEmpty() ? "FILE: " + f : m.fileHint() + " ON THIS LANE");
        // (what the file is heard to be: its loudest point, which nothing here raises, and the note that plays it at its own pitch)
        if (f.isNotEmpty())
        {
            if (saysPath != eng.laneFile (cfg.lane)) { saysPath = eng.laneFile (cfg.lane); saysOf = ses.sampleSays (cfg.lane); }
            if (saysOf.has) status << "   PEAK " << String (saysOf.peakDb, 1) << " DB" << (saysOf.pitched ? "   ROOT " + saysOf.root : String());
        }
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
            const bool resample = takesFile && cfg.kind == 0;         // (RESAMPLE: a lane only)
            const R sr (x0, y, std::min ((plain ? 392.0f : takesFile ? 468.0f : 544.0f) - (resample ? 96.0f : 0.0f), std::ceil (tw (fNote, status)) + 12.0f), 22);
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
            if (cfg.kind == 0)
            {
                // RESAMPLE: the bars of the snapshot last taken or recalled become this lane's sample
                const R rr (x0 + (plain ? 302.0f : 378.0f), y - 1, 90, 24);
                key (g, rr, col::white, pressed (aResample) ? 6.0f : 2.0f);
                text (g, "RESAMPLE", rr, fLbl, col::black);
                hit (rr.expanded (2), hAct, aResample, 0);
            }
            if (plain)
            {
                // PLAIN: the lane set to play its sample as it was recorded
                const R pr (x0 + 398, y - 1, 70, 24);
                key (g, pr, col::white, pressed (aSamplePlain) ? 6.0f : 2.0f);
                text (g, "PLAIN", pr, fLbl, col::black);
                hit (pr.expanded (2), hAct, aSamplePlain, 0);
            }
        }
        y += 28;
    }
    if (loadOpen && takesFile) { drawLoad (g, R (x0, y, 544, r.getBottom() - 8 - y)); return; }
    loadOpen = false;

    // The controls, four across, in the order the E16's CONFIG pages hold them: the first twelve are its first page's
    // knobs 1 to 12, place for place (the top three rows of its sixteen), the next twelve its second page's, and so
    // on. Under each twelve stands the E16's bottom row there: SEL (the wheel), PAGE -, PAGE +, SHUT. What the hand
    // learns on one is where it is on the other. A box with its name still says what a run of controls belongs
    // to: it stops at a row's end and carries on in the next.
    const int cols = 4, perPage = e16::kCfgPer, rowsPer = perPage / cols, total = (int) cfgParams.size();
    const float cellW = 136.0f, rowH = 126.0f, titleH = 17.0f, stripH = 22.0f;
    const int pages = std::max (1, (total + perPage - 1) / perPage);
    const float pageH = (float) rowsPer * rowH + stripH, floorY = r.getBottom() - 6;
    cfgScroll = juce::jlimit (0, pages - 1, cfgScroll);                // (scrolled a page at a time: the page at the top is whole)
    int lastShown = -1;
    for (int pg = cfgScroll; pg < pages; ++pg)
    {
        const float top = y + (float) (pg - cfgScroll) * pageH;
        if (top + rowH > floorY) break;
        for (int rr = 0; rr < rowsPer; ++rr)
        {
            const float by = top + (float) rr * rowH;
            if (by + rowH > floorY) break;
            const int first = pg * perPage + rr * cols;
            if (first >= total) break;
            // the boxes of this row: each run of controls that belong together
            for (int c = 0; c < cols && first + c < total;)
            {
                const String title = cfgParams[(size_t) (first + c)].group;
                int n = 1;
                while (c + n < cols && first + c + n < total && cfgParams[(size_t) (first + c + n)].group == title) ++n;
                const float bx = x0 + (float) c * cellW;
                const R box (bx + 4, by + 7, (float) n * cellW - 8, 106.0f);
                if (title.isNotEmpty() && vis (g, box.expanded (2, 8)))
                {
                    // (its name on the box's upper edge where the run begins; the line stops either side of the name)
                    const bool head = first + c == 0 || cfgParams[(size_t) (first + c - 1)].group != title;
                    const R chip (bx + 12, by, std::ceil (tw (fLbl, title)) + 10.0f, 15.0f);
                    {
                        const juce::Graphics::ScopedSaveState state (g);
                        if (head) g.excludeClipRegion (chip.expanded (5.0f, 3.0f).getSmallestIntegerContainer());
                        g.setColour (tone (col::black).withAlpha (0.30f));
                        g.drawRoundedRectangle (box, 5.0f, 1.0f);
                    }
                    if (head) plate (g, chip.getX(), chip.getY(), title, fLbl, 5, col::black, col::white, 15);
                }
                c += n;
            }
            for (int c = 0; c < cols && first + c < total; ++c)
            {
                const int i = first + c;
                const auto& sp = cfgParams[(size_t) i];
                const float cxm = x0 + (float) c * cellW + cellW * 0.5f, cy = by + titleH + 3;
                const R kr (cxm - 20, cy, 40, 40);
                if (vis (g, R (cxm - 61, cy, 122, rowH - titleH)))
                {
                    if (sp.kind == Kind::Toggle) key (g, kr, sp.value > 0.5f ? col::green : col::putty, 2.0f, sp.value > 0.5f);
                    else if (sp.kind == Kind::Hold) key (g, kr, col::yellow, sp.value > 0.5f ? 8.0f : 2.0f);
                    else knob (g, cxm, cy + 20, 40, sp.value, sp.kind, sp.choices.size(), col::lit);
                    legend (g, cxm, cy + 43, sp.name);
                    // INFO: its number while CONFIG is open
                    if (info && sp.kind != Kind::Hold && i < e16::kCfgPer * e16::kCfgPages)
                        textRaw (g, String (e16::cfgCc (i)), sp.kind == Kind::Toggle ? kr : R (cxm - 14, cy + 10 - (lit ? 3.0f : 0.0f), 28, 20), fLbl, sp.kind == Kind::Toggle ? inkOn (cap (sp.value > 0.5f ? col::green : col::putty), col::black) : col::lit);
                    if (sp.kind == Kind::List)
                    {
                        const R vr (cxm - 50, cy + 73, 100, 16);
                        if (lit) skin.window (g, vr, glass (col::white)); else { g.setColour (col::white); g.fillRect (vr); }
                        g.setColour (tone (col::black)); g.setFont (fLbl);
                        g.drawFittedText (sp.text.toUpperCase(), vr.reduced (2, 0).getSmallestIntegerContainer(), juce::Justification::centred, 1, 0.72f);
                    }
                }
                hit (kr.expanded (4, 2), hSetup, i, 0);
            }
            lastShown = pg;
        }
        // the E16's bottom row on this page of its CONFIG, under the twelve: which page it is (lit while the E16 is on it),
        // and its four corners' and keys' names in their places
        const float sy = top + (float) rowsPer * rowH + 1;
        if (pg < e16::kCfgPages && sy + 18 <= floorY && vis (g, R (x0, sy - 2, 544, stripH)))
        {
            const bool here = ses.cfgE16Page == pg;
            const String num (pg + 1);
            const float wTag2 = std::max (22.0f, std::ceil (tw (fLbl, num)) + 10.0f);
            if (here) plate (g, x0 + 2, sy, num, fLbl, 5, col::lit, col::black, 16); else this->tag (g, R (x0 + 2, sy, wTag2, 16), num);
            static const char* const corner[4] = { "SEL", "PAGE -", "PAGE +", "SHUT" };
            for (int c = 0; c < cols; ++c)
            {
                const float w = std::ceil (tw (fLbl, corner[c])) + 10.0f;
                plate (g, x0 + (float) c * cellW + cellW * 0.5f - w * 0.5f, sy, corner[c], fLbl, 5, col::white, col::black, 16);
            }
        }
    }
    if (cfgScroll > 0 || lastShown < pages - 1)
    {
        const String more = "PAGE " + String (cfgScroll + 1) + " OF " + String (pages) + ": SCROLL";
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
        if (Session::isCorner (k))
        {
            // (no knob of the module: on the E16 these two corners are the lane wheel and OPEN, as on every page. INFO says so.)
            if (info && vis (g, R (cxm - 40, y, 80, 58))) text (g, k == 12 ? "E16 LANE" : "E16 OPEN", R (cxm - 40, y + 12, 80, 14), fLbl, col::black);
            continue;
        }
        if (ses.knobLive (k))
        {
            const float v = eng.shown (cx::kSnap + k);
            if (vis (g, R (cxm - 40, y, 80, 58)))
            {
                if (seen (g, lit ? kr : R (cxm - 40, y, 80, 58))) lastShown[(size_t) (cx::kSnap + k)] = v;
                // (a macro knob is lit white. A stack knob: teal while its turns morph the set, amber while CUE has it choosing in
                // the headphones. Standing on one of its snapshots, it says which: the one in the headphones, or the one the set has come to.)
                const auto& kn = ses.knobs[(size_t) k];
                const bool stack = Session::isStack (k);
                const auto light = ! stack ? col::white : ses.stackCue ? col::amber : col::teal;
                knobCtl = cx::kSnap + k; knobLight = light;
                knob (g, cxm, y + 20, 40, v, Kind::Knob, 0, light);
                knobCtl = -1;
                if (info) number (g, cx::kSnap + k, R (cxm - 14, y + 10 - (lit ? 3.0f : 0.0f), 28, 20), col::lit);
                const Snapshot* on = stack && kn.picked != 0 && (! ses.stackCue || ses.cueing() == kn.picked) ? ses.findSnap (kn.picked) : nullptr;
                String says = on != nullptr ? on->name : kn.name;
                // (a stack knob in the hand, or under one on a controller: the snapshot it is going toward and how far it has come
                // to it, of 100; on one, that one's name. As its tag says on the 3D stage.)
                if (stack && ! ses.stackCue)
                {
                    const auto now = juce::Time::getMillisecondCounter();
                    const bool inHand = mouseIsDown && drag.kind == hSnapKnob && drag.a == k;
                    if (inHand || now - ses.movedAt[(size_t) (cx::kSnap + k)].load() < 1300u)
                        if (const auto w = ses.stackWay (k, eng.target (cx::kSnap + k)); w.to >= 0)
                        {
                            const String pct = w.on >= 0 ? String() : " " + String (juce::jlimit (0, 99, juce::roundToInt (w.part * 100.0f)));
                            String to = w.toName;
                            while (to.length() > 1 && tw (fLbl, to) > 80.0f - tw (fLbl, pct)) to = to.dropLastCharacters (1);
                            says = to + pct;
                        }
                }
                text (g, says, R (cxm - 40, y + 42, 80, 14), fLbl, col::black);
            }
            hit (kr.expanded (4, 2), hSnapKnob, k, 0);
        }
        else if (vis (g, kr)) hollow (g, cxm, y + 20, 40, true);
    }
    const float ky = r.getBottom() - 62;
    // CUE: how the stack knobs are played (lit: they choose in the headphones; out: they morph the set). LOAD: the module, as the web library has it now.
    actKey (g, aStackCue, x0, ky, 42, "CUE", ses.stackCue ? col::amber : col::putty, 3, true, ses.stackCue);
    {
        const bool loading = online != nullptr && online->loading();
        // LOAD: a module kept on the instrument, into the chassis (the designer's list of them). The web library's module is LOAD in the library.
        actKey (g, aModules, x0 + 44, ky, 42, "LOAD", col::white, 3, true);
    }
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
    const float wChan = chan (g, x0 + wTitle + 2, y0, e16::kSnapChannel);
    for (int k = 0; k < 3; ++k) control (g, cx::kMaster + k, x0 + (float) k * 66, body, 64, col::lit);
    // FINISH, the stage before the limiter: a plate, stepped by a press (OFF, 25, 50, 75, 100); lit while it is in. It ends
    // where the drum bus begins (and gives way to the channel's plate while INFO shows that: only its number is left).
    {
        const String says = wChan > 0.0f ? (ses.finishAt > 0 ? "FIN " + String (ses.finishPct()) : String ("FIN OFF")) : ses.finishSays();
        const float fw = std::ceil (tw (fLbl, says)) + 8.0f, fx = x0 + 198 - fw;
        plate (g, fx, y0 + 4, says, fLbl, 4, ses.finishAt > 0 ? col::amber : col::putty, col::black, 18);
        hit (R (fx - 2, y0 + 3, fw + 4, 20), hAct, aFinish, 0);
    }
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
    // (lit: a module said something is wrong with its sound, or a buffer ran late in the last three seconds. SET UP says which)
    g.setColour (eng.fault.load() || eng.overLately() ? col::amber : tone (col::grey)); g.fillRect (R (x0 + 8, fy + 10, 10, 22));
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
    const int moves = seqMovesWas = ses.keyMoves (l);       // (a step says the note that sounds: the written one, moved by KEY)
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
        textRaw (g, d.pitched && on ? Session::noteSays (x, moves) : String (i + 1), nb, fLbl, sel ? (night() ? col::black : col::lit) : tone (col::black));
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
    nameAt (1, s.id, -1, {}, chipNameRect (r, index + 1));
    const float x0 = r.getX() + 6, y0 = r.getY() + 6;
    // (on every page the one in hand has a key for every part of the rig where the others draw their sound: what is heard of
    // it, and what goes with it when it is recalled, added to a stack or put in the arrangement)
    const bool inHand = s.id == handId && (hitKind == hPick || hitKind == hCard);
    if (inHand) for (int p = 0; p < kLanes + 3; ++p) hit (R (x0 + (float) p * 18.6f, y0 + 30, 17.0f, 24), hPickKey, index, p);
    if (! vis (g, r)) return;
    card (g, r, selected || inHand);
    const String num (index + 1);
    const float wTag = std::max (30.0f, std::ceil (tw (fLegend, num)) + 8.0f);
    tag (g, R (x0, y0, wTag, 24), num);
    const bool isBase = s.id == ses.baseId;
    // (in the designer the card is heard when it is pressed, its name green while it is in the headphones, and ADD puts it on the chosen knob)
    const bool adds = hitKind == hPick && (page == pMacros || page == pArrange), heard = s.id == ses.cueing();
    // the base scene's name is the black one; on the dark instrument, the lit one
    if (heard) plate (g, x0 + wTag, y0, s.name, fLegend, 8, col::green, col::lit, 24);
    else plate (g, x0 + wTag, y0, s.name, fLegend, 8, isBase ? (night() ? col::lit : col::black) : col::white, isBase ? (night() ? col::black : col::lit) : col::black, 24);
    if (adds) { const R ar (r.getRight() - 6 - 44, y0, 44, 24); window (g, ar, col::white); text (g, "ADD", ar, fNote, col::black); }
    else text (g, s.time.formatted ("%b %d  %H:%M").toUpperCase(), R (r.getRight() - 110, y0, 104, 24), fLbl, col::black, juce::Justification::centredRight);
    if (inHand)
    {
        static const char* const partName[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "R", "B", "K" };
        for (int p = 0; p < kLanes + 3; ++p)
        {
            const R kr (x0 + (float) p * 18.6f, y0 + 30, 17.0f, 24);
            if (((handParts >> p) & 1u) != 0) { window (g, kr, col::black); text (g, partName[p], kr, fNote, col::lit); }
            else { g.setColour (tone (col::sea)); g.drawRect (kr.reduced (0.5f), 1.0f); textRaw (g, partName[p], kr, fNote, tone (col::black).withAlpha (0.45f)); }
        }
        return;
    }
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

// The snapshots a page lists, each by its place in the library: every one, or a stack's (in the stack's order); in the
// order a finder gives them; and only those whose names hold what FIND was given.
std::vector<int> Screen::listed() const
{
    // (in the library the open stack lies out in its own row: what is listed under it is the bin, every chip)
    const int n = (int) ses.library.size(), in = page == pLibrary ? -1 : inAt();
    std::unordered_map<juce::int64, int> at;
    for (int i = 0; i < n; ++i) at[ses.library[(size_t) i].id] = i;
    std::vector<bool> ours ((size_t) n, in < 0), taken ((size_t) n, false);
    if (in >= 0) for (auto id : ses.stacks[(size_t) in].snaps) if (const auto f = at.find (id); f != at.end()) ours[(size_t) f->second] = true;
    std::vector<int> out;
    auto take = [&] (int i)
    {
        if (i < 0 || ! ours[(size_t) i] || taken[(size_t) i]) return;
        if (findText.isNotEmpty() && ! ses.library[(size_t) i].name.containsIgnoreCase (findText)) return;
        taken[(size_t) i] = true; out.push_back (i);
    };
    auto takeId = [&] (juce::int64 id) { if (const auto f = at.find (id); f != at.end()) take (f->second); };
    if (finder == 1)
    {
        std::vector<int> all ((size_t) n);
        for (int i = 0; i < n; ++i) all[(size_t) i] = i;
        std::stable_sort (all.begin(), all.end(), [this] (int a, int b) { return ses.library[(size_t) a].time > ses.library[(size_t) b].time; });
        for (int i : all) take (i);
    }
    else if (finder != 0) for (auto id : found) takeId (id);
    else if (in >= 0) for (auto id : ses.stacks[(size_t) in].snaps) takeId (id);
    else for (int i = 0; i < n; ++i) take (i);
    return out;
}

// The head of the snapshots, wherever a page lists them: whose they are (every one, or a stack's: pressed, the next stack),
// the four finders (the one lit is black), and FIND (a name, typed). One row where there is room, two where it is narrow.
// Returns where the cards begin.
float Screen::drawFind (G& g, float x, float w, float y)
{
    // (where the rack is, its pockets say whose chips are out: this row only names them. In the library it heads the bin.)
    const bool racked = page == pLibrary || page == pMacros || page == pArrange;
    const int in = page == pLibrary ? -1 : inAt(), n = (int) listed().size();
    const bool wide = w > 500.0f;
    float px = x, py = y;
    auto pl = [&] (String t, int what, bool on, bool can, float room)
    {
        while (t.length() > 4 && std::ceil (tw (fNote, t)) + 12 > room) t = t.dropLastCharacters (1);
        const float pw = std::ceil (tw (fNote, t)) + 12;
        const R r (px, py, pw, 26);
        if (vis (g, r)) plate (g, px, py, t, fNote, 6, on ? col::black : col::white, on ? col::lit : col::black);
        if (can) hit (r, hFind, what, 0);
        px += pw + 2;
        return r;
    };
    const String find = findText.isEmpty() ? String ("FIND") : "FIND: " + findText;
    const float wFind = std::ceil (tw (fNote, find)) + 12;
    auto finders = [&]
    {
        static const char* const names[] = { "RECENT", "FITS NOW", "MORE", "LESS" };
        for (int f = 1; f <= 4; ++f) pl (names[f - 1], f, finder == f, true, 200.0f);
    };
    // (the number is how many are listed now)
    const String whose = (in >= 0 ? ses.stacks[(size_t) in].name : racked ? String ("THE BIN") : String ("ALL")) + "  " + String (n);
    if (wide)
    {
        pl (whose, 0, in >= 0, ! racked && ! ses.stacks.empty(), 220.0f);
        px += 8; finders(); px += 8;
        findRect = pl (find, 5, findText.isNotEmpty(), true, 200.0f).withWidth (std::max (wFind, 140.0f));
    }
    else
    {
        pl (whose, 0, in >= 0, ! racked && ! ses.stacks.empty(), w - wFind - 2);
        px = x + w - wFind;
        findRect = pl (find, 5, findText.isNotEmpty(), true, 200.0f).withX (x + w - std::max (wFind, 140.0f)).withWidth (std::max (wFind, 140.0f));
        px = x; py = y + 32;
        finders();
    }
    const float top = py + 32;
    if (n == 0 && ! ses.library.empty() && vis (g, R (x, top + 2, 272, 44)))
    {
        g.setColour (tone (col::black)); g.setFont (fLbl);
        g.drawFittedText (finder >= 2 && found.empty() ? "NOTHING IS PLAYING TO GO BY, OR NO SNAPSHOT HAS A SOUND KEPT: FITS NOW, MORE AND LESS LISTEN TO THE SET."
                                                       : "NONE. PRESS WHAT IS LIT AT THE HEAD OF THE LIST TO SEE MORE.",
                          juce::Rectangle<int> ((int) x + 6, (int) top + 4, 260, 44), juce::Justification::topLeft, 3, 0.9f);
    }
    return top;
}

void Screen::pressFind (int what)
{
    libScroll = 0;
    if (what == 0)
    {
        // whose: the next stack, then every snapshot again
        const int at = inAt() + 1;
        inStack = at < (int) ses.stacks.size() ? ses.stacks[(size_t) at].id : String();
    }
    else if (what >= 1 && what <= 4)
    {
        if (finder == what) { finder = 0; found.clear(); return; }
        finder = what; found.clear();
        // (what is playing is listened to now, and the list stays put until the plate is pressed again)
        if (what == 2) found = ses.fitting (ses.heardNow(), Session::Fit::like, (int) ses.library.size());
        if (what == 3) found = ses.fitting (ses.heardNow(), Session::Fit::more, 12);
        if (what == 4) found = ses.fitting (ses.heardNow(), Session::Fit::less, 12);
    }
    else if (what == 5)
    {
        if (findText.isNotEmpty()) { findText = {}; return; }
        if (! findRect.isEmpty()) beginEdit (findRect, {}, [this] (String t) { findText = t.trim().toUpperCase().substring (0, 16); libScroll = 0; });
    }
}

// =========================================================================================== the rack
// The library is a rack (DECISIONS 602, 603). Along its top, pockets: the bin, which holds every chip, then the things
// of the shelf chosen, each in a pocket of its own. A stack is a pile of chips standing in its pocket: the top chip's
// face, and under it the edge of every chip below, each edge the chip's own colour, so that how tall is how many and
// the edges say which. A kept stack knob is a pile with a knob on its rim, a kept macro knob a cap on its base, a
// module a cartridge (four caps over ten pockets), an arrangement a rail of its parts. A pocket pressed is open: what
// it holds lies out on the bench under the rack as chip cards, and its pocket stands empty, green. Every chip card on
// the bench plays in the headphones when it is pressed, whatever it lies in.
namespace
{
const char* const kShelfName[] = { "STACKS", "KNOBS", "MODULES", "ARRANGEMENTS" };
constexpr float kWellW = 176.0f, kWellH = 120.0f, kWellPitch = 190.0f, kRackY = 80.0f, kRackX = 163.0f, kBenchY = 246.0f;
constexpr int kWellsAcross = 8;
}

int Screen::openKeptAt() const { for (int i = 0; i < (int) ses.keptKnobs.size(); ++i) if (ses.keptKnobs[(size_t) i].id == openKept && openKept.isNotEmpty()) return i; return -1; }
int Screen::openModAt() const  { for (int i = 0; i < (int) ses.modules.size(); ++i) if (ses.modules[(size_t) i].id == openMod && openMod.isNotEmpty()) return i; return -1; }
int Screen::openArrAt() const  { for (int i = 0; i < (int) ses.arrs.size(); ++i) if (ses.arrs[(size_t) i].id == openArr && openArr.isNotEmpty()) return i; return -1; }
int Screen::rackCount() const  { return rackShelf == 0 ? (int) ses.stacks.size() : rackShelf == 1 ? (int) ses.keptKnobs.size() : rackShelf == 2 ? (int) ses.modules.size() : (int) ses.arrs.size(); }

int Screen::rackOpen() const
{
    if (rackShelf == 0) return inAt() >= 0 ? 1 : 0;
    if (rackShelf == 1) return openKeptAt() >= 0 ? 2 : 0;
    if (rackShelf == 2) return openModAt() >= 0 ? 3 : 0;
    return openArrAt() >= 0 ? 4 : 0;
}

void Screen::rackSay (const String& t) { recNote = t; recNoteAt = juce::Time::getMillisecondCounter(); }

Colour Screen::chipColour (juce::int64 id) const
{
    if (const auto* s = ses.findSnap (id); s != nullptr && s->profile.has) return heat (s->profile.bright);
    return Colour (0xff8F8B82);         // (no sound kept with it: no colour to go by)
}

void Screen::well (G& g, R r)
{
    if (lit) { skin.well (g, r); return; }
    g.setColour (col::black); g.fillRect (r);
    g.setColour (col::sea.darker (0.18f)); g.fillRect (r.reduced (1.0f));
}

// A pile as it stands in its pocket. On the flat and the lit screen, seen from the player's side: the top chip's face,
// and under it an edge for each chip. On the 3D stage it is the chips themselves, a plate each, one standing on another
// (the parts' own rule: a plate wholly over an earlier one stands on it), and the face is printed on the top one.
void Screen::drawPile (G& g, R wr, const std::vector<juce::int64>& ids, const String& top, int most)
{
    const int n = std::min ((int) ids.size(), most);
    if (n == 0) return;
    const float fx = wr.getX() + 10.0f, fw = wr.getWidth() - 20.0f;
    const bool solid = lit && (recording || printing);
    String name = top;
    while (name.length() > 3 && std::ceil (tw (fNote, name)) + 12 > fw) name = name.dropLastCharacters (1);
    if (solid)
    {
        const R foot (fx, wr.getBottom() - 14.0f - 38.0f, fw, 38.0f);          // (a chip's own footprint, lying flat)
        for (int i = n - 1; i >= 1; --i) skin.panel (g, foot, chipColour (ids[(size_t) i]));
        skin.panel (g, foot, tone (col::putty));
        g.setColour (chipColour (ids[0])); g.fillRect (R (foot.getX() + 2.0f, foot.getBottom() - 7.0f, foot.getWidth() - 4.0f, 5.0f));       // (its own colour, along its near edge)
        text (g, name, R (foot.getX() + 6.0f, foot.getY() + 2.0f, foot.getWidth() - 12.0f, foot.getHeight() - 10.0f), fNote, col::black, juce::Justification::centredLeft);
        return;
    }
    const float eh = 5.0f, bottom = wr.getBottom() - 9.0f;
    const R face (fx, bottom - eh * (float) n - 26.0f, fw, 26.0f);
    for (int i = 0; i < n; ++i)
    {
        const R e (fx + 1.0f, face.getBottom() - 1.0f + (float) i * eh, fw - 2.0f, eh + 1.0f);
        g.setColour (tone (col::black)); g.fillRect (e);
        g.setColour (chipColour (ids[(size_t) i])); g.fillRect (e.reduced (1.0f));
    }
    card (g, face, false);
    text (g, name, face.reduced (6.0f, 0.0f), fNote, col::black, juce::Justification::centredLeft);
}

// One pocket of the rack, with its name on a plate at its near rim. kind: 0 the bin, 1 a stack, 2 a kept knob, 3 a
// module, 4 an arrangement, 5 a new stack.
void Screen::drawWell (G& g, R r, int kind, int index)
{
    const R wr (r.getX(), r.getY(), r.getWidth(), kWellH);
    wellsAt.push_back ({ r, kind, index });
    hit (r, hWell, kind, index);
    if (! vis (g, r.expanded (8.0f))) return;
    const int open = rackOpen();
    const bool isOpen = (kind == 0 && open == 0) || (kind == 1 && open == 1 && index == inAt()) || (kind == 2 && open == 2 && index == openKeptAt())
                     || (kind == 3 && open == 3 && index == openModAt()) || (kind == 4 && open == 4 && index == openArrAt());
    // (the chip in hand: every stack that holds it shows it)
    juce::int64 hand = handId;
    if (const auto pile = rowAt(); pile.what != 0 && rowSel >= 0) { const auto snaps = ses.pileSnaps (pile); if (rowSel < (int) snaps.size()) hand = snaps[(size_t) rowSel]; }
    const bool holds = kind == 1 && hand != 0 && std::find (ses.stacks[(size_t) index].snaps.begin(), ses.stacks[(size_t) index].snaps.end(), hand) != ses.stacks[(size_t) index].snaps.end();
    const bool inUse = (kind == 3 && index == ses.moduleIndex()) || (kind == 4 && index == ses.arrsIndex());
    String name, count;
    bool macro = false;
    std::vector<juce::int64> ids;
    if (kind == 0) { name = "ALL CHIPS"; count = String ((int) ses.library.size()); for (int i = (int) ses.library.size() - 1; i >= 0 && ids.size() < 10; --i) ids.push_back (ses.library[(size_t) i].id); }
    else if (kind == 1) { const auto& st = ses.stacks[(size_t) index]; name = st.name; count = String ((int) st.snaps.size()); for (auto id : st.snaps) if (ses.findSnap (id) != nullptr) ids.push_back (id); }
    else if (kind == 2)
    {
        const auto& kk = ses.keptKnobs[(size_t) index];
        name = kk.name().isEmpty() ? String ("UNNAMED") : kk.name(); macro = ! kk.stack;
        const MacroKnob kn = ses.knobFrom (kk.knob);
        if (kk.stack) { ids = kn.snaps; count = String ((int) ids.size()); } else count = String ((int) kn.links.size());
    }
    else if (kind == 3) { name = ses.modules[(size_t) index].name; count = inUse ? String ("IN") : String(); }
    else if (kind == 4) { name = ses.arrs[(size_t) index].name; int bars = 0; for (auto& pt : ses.arrsParts (index)) bars += pt.bars; count = String (bars); }
    else { name = "NEW STACK"; count = "+"; }
    const Colour frame = isOpen || inUse ? col::green : col::yellow;
    if (kind == 3)
    {
        // a module: a cartridge, four caps over ten pockets
        card (g, wr.reduced (0.0f, 8.0f), false);
        knobCtl = -1;
        const float ks = (wr.getWidth() - 48.0f) / 3.0f, hs = (wr.getWidth() - 48.0f) / 4.0f;
        for (int k = 0; k < 4; ++k) knob (g, wr.getX() + 24.0f + (float) k * ks, wr.getY() + 34.0f, 30, 0.0f, Kind::Knob, 0, col::white);
        for (int k = 0; k < 10; ++k) hollow (g, wr.getX() + 24.0f + (float) (k % 5) * hs, wr.getY() + 68.0f + (float) (k / 5) * 22.0f, 18.0f, false);
    }
    else if (kind == 2 && macro)
    {
        // a macro knob kept: a cap on its base, a pip for every knob it is tied to
        card (g, wr.reduced (28.0f, 8.0f), false);
        knobCtl = -1;
        knob (g, wr.getCentreX(), wr.getY() + 46.0f, 40, 0.0f, Kind::Knob, 0, col::white);
        const int pips = std::min (12, count.getIntValue());
        g.setColour (tone (col::black));
        for (int k = 0; k < pips; ++k) g.fillEllipse (wr.getCentreX() - (float) pips * 4.0f + (float) k * 8.0f + 1.0f, wr.getY() + 84.0f, 5.0f, 5.0f);
    }
    else
    {
        well (g, wr);
        if (isOpen && kind != 0 && ! (recording || printing)) text (g, "OUT ON THE BENCH", wr.reduced (8.0f), fNote, col::black);
        else if (isOpen && kind == 0) drawPile (g, wr, ids, count + " CHIPS");
        else if (kind == 5) text (g, "+", wr, fRead, col::black);
        else if (kind == 4)
        {
            // an arrangement: a rail, its parts as long as their bars
            const auto parts = ses.arrsParts (index);
            int bars = 0; for (auto& pt : parts) bars += std::max (1, pt.bars);
            float px = wr.getX() + 10.0f;
            const float room = wr.getWidth() - 20.0f;
            for (auto& pt : parts)
            {
                const float w = std::max (4.0f, room * (float) std::max (1, pt.bars) / (float) std::max (1, bars));
                const R pr (px, wr.getBottom() - 46.0f, w - 2.0f, 30.0f);
                if (pr.getWidth() >= 12.0f && lit) skin.panel (g, pr, chipColour (pt.snap));
                else { g.setColour (tone (col::black)); g.fillRect (pr); g.setColour (chipColour (pt.snap)); g.fillRect (pr.reduced (1.0f)); }
                px += w;
            }
            if (parts.empty()) text (g, "NO PARTS", wr, fNote, col::black);
        }
        else if (! isOpen) drawPile (g, wr, ids, kind == 0 ? count + " CHIPS" : ids.empty() ? String() : ses.findSnap (ids[0])->name);
        if (kind == 2) { knobCtl = -1; knob (g, wr.getRight() - 22.0f, wr.getY() + 22.0f, 30, 0.0f, Kind::Knob, 0, col::white); }       // (a stack knob kept: the knob in its corner, clear of the lip)
    }
    if (isOpen || holds || inUse) { g.setColour (tone (frame)); g.drawRoundedRectangle (wr.expanded (3.0f), 7.0f, 3.0f); }
    // its name, and how many it holds
    const float py = r.getY() + kWellH + 6.0f;
    const float wc = count.isEmpty() ? 0.0f : std::ceil (tw (fNote, count)) + 12.0f;
    while (name.length() > 3 && std::ceil (tw (fNote, name)) + 12 > r.getWidth() - wc - 2.0f) name = name.dropLastCharacters (1);
    plate (g, r.getX(), py, name, fNote, 6, isOpen ? col::green : col::black, col::lit, 24);
    if (wc > 0.0f) plate (g, r.getRight() - wc, py, count, fNote, 6, col::white, col::black, 24);
}

void Screen::drawChipRack (G& g)
{
    wellsAt.clear();
    // the shelves: what stands in the pockets beside the bin (down the left of the library; in a row over a third of a page)
    const float ww = rp.wide ? kWellW : 130.0f;        // (a third of a page: four narrower pockets)
    const float wx = rp.wide ? rp.x + 140.0f : rp.x, wy = rp.wide ? rp.y : rp.y + 32.0f, pitch = rp.wide ? kWellPitch : (rp.w - ww) / (float) std::max (1, rp.wells - 1);
    for (int k = 0; k < 4; ++k)
    {
        const float kw = (rp.w - 12.0f) / 4.0f;
        const R kr = rp.wide ? R (rp.x, rp.y + (float) k * 30.0f, 128.0f, 26.0f) : R (rp.x + (float) k * (kw + 4.0f), rp.y, kw, 26.0f);
        const bool on = rackShelf == k;
        if (vis (g, kr)) { if (on) window (g, kr, col::black); else window (g, kr, col::white); text (g, kShelfName[k], kr, fNote, on ? col::lit : col::black); }
        hit (kr, hLibTab, k, 0);
    }
    const int n = rackCount(), slots = rp.wells - 1;
    const int extra = rackShelf == 0 ? 1 : 0;      // (the stacks' shelf ends in an empty pocket: a new stack)
    rackFirst = juce::jlimit (0, std::max (0, n + extra - slots), rackFirst);
    rackArea = R (wx, wy, rp.x + rp.w - wx, kWellH + 30.0f);
    drawWell (g, R (wx, wy, ww, kWellH + 30.0f), 0, 0);
    for (int q = 0; q < slots; ++q)
    {
        const int i = rackFirst + q;
        const R r (wx + (float) (q + 1) * pitch, wy, ww, kWellH + 30.0f);
        if (i < n) drawWell (g, r, rackShelf + 1, i);
        else if (i == n && extra == 1) drawWell (g, r, 5, 0);
    }
    if (n + extra > slots)
    {
        const String says = String (rackFirst + 1) + " TO " + String (std::min (n, rackFirst + slots)) + " OF " + String (n);
        if (rp.wide) { if (vis (g, R (rp.x, rp.y + 124.0f, 130.0f, 30.0f))) text (g, says + "\n(THE WHEEL)", R (rp.x, rp.y + 122.0f, 128.0f, 30.0f), fNote, col::black, juce::Justification::centredLeft); }
        else if (vis (g, R (rp.x, wy + kWellH + 32.0f, rp.w, 14.0f))) text (g, says + (extra == 1 ? ", AND THE EMPTY ONE" : "") + " (THE WHEEL, OVER THE POCKETS)", R (rp.x, wy + kWellH + 32.0f, rp.w, 12.0f), fNote, col::black, juce::Justification::centredLeft);
    }
}

// The open thing's keys: plates after its name. One that cannot be pressed now is an empty frame. One that is held (DELETE)
// fills with light from the left while the hand is on it.
void Screen::headKey (G& g, float& x, float& y, int a, const String& t, Colour c, bool can)
{
    const float w = std::ceil (tw (fNote, t)) + 16.0f;
    if (x + w > rp.x + rp.w) { x = rp.x; y += 30.0f; }
    const R r (x, y + 1.0f, w, 24.0f);
    if (vis (g, r))
    {
        if (can)
        {
            const float f = holdFill (a);
            window (g, r, f > 0.0f ? c.darker (0.55f) : c);
            if (f > 0.0f) window (g, r.withWidth (std::max (4.0f, r.getWidth() * f)), col::redLit);
            onGlass (g, t, r, fNote, c, c == col::red || c == col::black ? col::lit : col::black);
        }
        else { g.setColour (tone (col::sea)); g.drawRect (r.reduced (0.5f), 1.0f); textRaw (g, t, r, fNote, tone (col::black).withAlpha (0.45f)); }
    }
    if (can) hit (r, hAct, a, 0);
    x += w + 4.0f;
}

float Screen::openHead (G& g, float top, const String& name, const String& says)
{
    const int open = rackOpen();
    float x = rp.x, y = top;
    String nm = name;
    while (nm.length() > 3 && std::ceil (tw (fLegend, nm)) + 16.0f > rp.w * 0.5f) nm = nm.dropLastCharacters (1);
    const float wn = plate (g, x, y, nm, fLegend, 8, col::green, col::lit, 26);
    openNameRect = R (x, y, std::max (wn, 180.0f), 26);
    nameAt (open == 1 ? 3 : open == 2 ? 4 : open == 3 ? 5 : 6, 0, -1, open == 1 ? inStack : open == 2 ? openKept : open == 3 ? openMod : openArr, openNameRect);
    x += wn + 10.0f;
    // (its row is out on the bench here: what the hand does in it, first)
    if (const auto pile = rowAt(); pile.what == 1 || pile.what == 2) { rowKeys (g, x, y, pile, (int) ses.pileSnaps (pile).size()); x += 10.0f; }
    // (a macro knob kept by itself, its ties out on the bench: the one pressed there is taken out)
    else if (tiesAt().what == 2) { headKey (g, x, y, aRowOut, "TAKE OUT", col::white, tieSel >= 0); x += 10.0f; }
    if (open == 1)
    {
        const int os = inAt();
        const int n = (int) ses.stacks[(size_t) os].snaps.size();
        headKey (g, x, y, aOpenLoad, "ON A KNOB", col::white, n > 0);
        headKey (g, x, y, aOpenCopy, "COPY", col::white);
        headKey (g, x, y, aOpenDelete, "DELETE", col::red);
    }
    else if (open >= 2)
    {
        const int at = open == 2 ? openKeptAt() : open == 3 ? openModAt() : openArrAt();
        const bool inUse = (open == 3 && at == ses.moduleIndex()) || (open == 4 && at == ses.arrsIndex());
        if (open != 2) { headKey (g, x, y, aOpenNew, "NEW", col::white); headKey (g, x, y, aOpenCopy, "COPY", col::white); }
        headKey (g, x, y, aOpenLoad, open == 2 ? "ON A KNOB" : open == 3 ? "INTO THE CHASSIS" : "ONTO THE PAGE", col::yellow, ! inUse);
        headKey (g, x, y, aOpenDelete, "DELETE", col::red, open == 2 || (open == 3 && ! inUse) || (open == 4 && ses.arrs.size() > 1));
    }
    // (what it is, in a line, where there is room for one)
    if (says.isNotEmpty() && rp.wide)
    {
        if (rp.x + rp.w - x < 420.0f) { x = rp.x; y += 30.0f; }
        text (g, says, R (x + 6.0f, y, rp.x + rp.w - x - 6.0f, 26), fLbl, col::black, juce::Justification::centredLeft);
    }
    return y + 34.0f;
}

void Screen::rackTidy()
{
    if (inStack.isNotEmpty() && inAt() < 0) inStack = {};
    if (openKept.isNotEmpty() && openKeptAt() < 0) openKept = {};
    if (openMod.isNotEmpty() && openModAt() < 0) { openMod = {}; openModKnob = -1; }
    if (openArr.isNotEmpty() && openArrAt() < 0) openArr = {};
    rackShelf = juce::jlimit (0, 3, rackShelf);
    openNameRect = {};
}

// A stack chip's row of keys: the eleven lanes, R, B, K (the parts of the rig it takes), then, set a little apart, N (its
// notes) and M (its models and samples), in the card's width.
static constexpr float kKeyPitch = 16.0f, kKeyWide = 15.2f, kKeyGap = 4.0f;
static juce::Rectangle<float> stackKeyRect (float x0, float y, int p) { return { x0 + (float) p * kKeyPitch + (p >= kLanes + 3 ? kKeyGap : 0.0f), y, kKeyWide, 24.0f }; }

// A part's key: black with a lit figure while the chip takes the part, an empty frame with a faint figure when it is put
// out. N and M are switches, and out is how they start: out, each is a pale key with its letter plain on it, so that it
// is seen to be there and to be pressable; lit, black as the others.
void Screen::stackKey (G& g, R kr, const char* name, bool lit, bool ownKey)
{
    if (lit) { window (g, kr, col::black); text (g, name, kr, fNote, col::lit); return; }
    if (ownKey)
    {
        g.setColour (tone (col::white)); g.fillRect (kr);
        g.setColour (tone (col::black)); g.drawRect (kr.reduced (0.5f), 1.0f);
        textRaw (g, name, kr, fNote, tone (col::black));
        return;
    }
    g.setColour (tone (col::sea)); g.drawRect (kr.reduced (0.5f), 1.0f);
    textRaw (g, name, kr, fNote, tone (col::black).withAlpha (0.45f));
}
// A chip of the open stack, in its row: its number, its name (green while it is in the headphones), and a key for every
// part of the rig it takes there, N last. Pressed, it is the chip in hand and is heard with those parts; carried along
// the row it is moved, off the row it is taken out of this stack.
void Screen::drawRowCard (G& g, R r, int i, juce::int64 id, juce::uint32 lanes, bool can)
{
    static const char* const partName[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "R", "B", "K", "N", "M" };
    const auto* sn = ses.findSnap (id);
    hit (r, hRowCard, i, 0);
    nameAt (1, id, -1, {}, chipNameRect (r, i + 1));
    const float x0 = r.getX() + 6, y0 = r.getY() + 6;
    const String num (i + 1);
    const float wTag = std::max (30.0f, std::ceil (tw (fLegend, num)) + 8.0f);
    if (vis (g, r))
    {
        const bool heard = sn != nullptr && ses.cueing() == sn->id;
        card (g, r, i == rowSel);
        tag (g, R (x0, y0, wTag, 24), num);
        String name = sn != nullptr ? sn->name : String ("GONE");
        while (name.length() > 3 && x0 + wTag + std::ceil (tw (fLegend, name)) + 16 > r.getRight() - 6) name = name.dropLastCharacters (1);
        plate (g, x0 + wTag, y0, name, fLegend, 8, heard ? col::green : col::white, heard ? col::lit : col::black, 24);
    }
    for (int p = 0; p < kLanes + 5; ++p)
    {
        const R kr = stackKeyRect (x0, y0 + 30, p);
        if (vis (g, kr)) stackKey (g, kr, partName[p], ((lanes >> p) & 1u) != 0, p >= kLanes + 3);
        if (can) hit (kr, hRowKey, i, p);
    }
}

Session::PileAt Screen::rowAt() const
{
    if (page != pLibrary) return {};
    const int open = rackOpen();
    Session::PileAt p;
    if (open == 1) p = { 1, inAt(), -1 };
    else if (open == 2) p = { 2, openKeptAt(), -1 };
    else if (open == 3 && openModKnob >= 0) p = { 3, openModAt(), openModKnob };
    return ses.pileIs (p) ? p : Session::PileAt();
}

Session::PileAt Screen::tiesAt() const
{
    if (page != pLibrary) return {};
    const int open = rackOpen();
    Session::PileAt p;
    if (open == 2) p = { 2, openKeptAt(), -1 };
    else if (open == 3 && openModKnob >= 0) p = { 3, openModAt(), openModKnob };
    return ses.tieIs (p) ? p : Session::PileAt();
}

// What the hand does in the row: the chip in hand in, last; the chip pressed in the row out, earlier, later.
void Screen::rowKeys (G& g, float& x, float& y, const Session::PileAt& p, int n)
{
    const bool can = ses.pileCan (p), inRow = rowSel >= 0, handIn = ses.findSnap (handId) != nullptr;
    headKey (g, x, y, aRowAdd, "ADD", col::yellow, can && handIn);
    headKey (g, x, y, aRowOut, "TAKE OUT", col::white, can && inRow);
    headKey (g, x, y, aRowEarlier, "EARLIER", col::white, can && inRow && rowSel > 0);
    headKey (g, x, y, aRowLater, "LATER", col::white, can && inRow && rowSel + 1 < n);
}

float Screen::drawStackRow (G& g, float top)
{
    const auto p = rowAt();
    const auto snaps = ses.pileSnaps (p);
    const int n = (int) snaps.size(), rowsAll = std::max (1, (n + 5) / 6), rows = std::min (2, rowsAll);
    rowSel = juce::jlimit (-1, n - 1, rowSel);
    most.row = rowsAll - rows;
    rowScroll = juce::jlimit (0, most.row, rowScroll);
    const bool can = ses.pileCan (p);
    const String count = String (n) + (n == 1 ? " CHIP" : " CHIPS");
    const String how = ! can ? String (". THE WEB LIBRARY'S: IT IS CHANGED THERE.")
                     : n == 0 ? String (". CARRY A CHIP UP HERE FROM THE BIN, OR PRESS ONE THERE AND THEN ADD.")
                              : String (", IN THE ORDER A KNOB TURNS THROUGH THEM. CARRY ONE ALONG THE ROW TO MOVE IT, OFF IT TO TAKE IT OUT.");
    const String lines = rowsAll > rows ? "  ROWS " + String (rowScroll + 1) + " TO " + String (rowScroll + rows) + " OF " + String (rowsAll) + " (THE WHEEL)" : String();
    float y = top;
    if (p.what == 3)
    {
        // a knob of the open module: its pile has a line of its own under the module's knobs (which knob, the row's keys, how many)
        float x = rp.x;
        const String which = "KNOB " + String (p.knob + 1) + ": " + ses.pileName (p);
        x += plate (g, x, y, which, fNote, 8, col::green, col::lit, 26) + 10.0f;
        rowKeys (g, x, y, p, n);
        text (g, count + how + lines, R (x + 6.0f, y, rp.x + rp.w - x - 6.0f, 26), fLbl, col::black, juce::Justification::centredLeft);
        y += 32.0f;
    }
    else
    {
        // its head: its name (green: it is the one out on the bench), its keys, how many
        const String name = ses.pileName (p);
        y = openHead (g, top, name.isEmpty() ? String ("UNNAMED") : name, (p.what == 2 ? "A STACK KNOB, KEPT BY ITSELF: " : "") + count + how + lines) - 2.0f;
    }
    rowArea = R (23.0f, y - 4.0f, 1682.0f, (float) rows * 74.0f + 2.0f);
    if (vis (g, rowArea)) { g.setColour (tone (col::white).withAlpha (0.16f)); g.fillRoundedRectangle (rowArea.expanded (4.0f, 2.0f), 5.0f); }
    for (int q = 0; q < rows * 6; ++q)
    {
        const int i = rowScroll * 6 + q;
        if (i >= n) break;
        drawRowCard (g, R (23.0f + (float) (q % 6) * 282, y + (float) (q / 6) * 74, 272, 66), i, snaps[(size_t) i], ses.pileKeysAt (p, i), can);
    }
    return y + (float) rows * 74.0f + 8.0f;
}

// A chip seen in something that is open and is not changed here (a kept knob's pile, a knob of a module, a part of an
// arrangement): its card, with the parts it takes there lit, or a line about it. Pressed, it is heard with those parts.
void Screen::drawSeenCard (G& g, R r, juce::int64 id, int num, juce::uint32 keys, const String& note)
{
    static const char* const partName[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "R", "B", "K", "N", "M" };
    const auto* sn = ses.findSnap (id);
    const int at = sn != nullptr ? ses.indexOf (id) : -1;
    if (at >= 0) { hit (r, hBenchChip, at, (int) (keys & Session::kAllKeys)); nameAt (1, id, -1, {}, chipNameRect (r, num)); }
    if (! vis (g, r)) return;
    const float x0 = r.getX() + 6, y0 = r.getY() + 6;
    const String n (num);
    const float wTag = std::max (30.0f, std::ceil (tw (fLegend, n)) + 8.0f);
    const bool heard = sn != nullptr && ses.cueing() == sn->id;
    card (g, r, sn != nullptr && sn->id == handId);
    tag (g, R (x0, y0, wTag, 24), n);
    String name = sn != nullptr ? sn->name : String ("GONE");
    while (name.length() > 3 && x0 + wTag + std::ceil (tw (fLegend, name)) + 16 > r.getRight() - 6) name = name.dropLastCharacters (1);
    plate (g, x0 + wTag, y0, name, fLegend, 8, heard ? col::green : col::white, heard ? col::lit : col::black, 24);
    if (note.isNotEmpty()) { text (g, note, R (x0, y0 + 30, r.getWidth() - 12, 24), fLbl, col::black, juce::Justification::centredLeft); return; }
    for (int p = 0; p < kLanes + 5; ++p)
    {
        stackKey (g, stackKeyRect (x0, y0 + 30, p), partName[p], ((keys >> p) & 1u) != 0, p >= kLanes + 3);
    }
}

// What a knob holds, laid out from `top`: a stack knob's chips in order, each with the parts it takes; a macro knob's
// ties, each the knob it pushes and how far.
// What a knob holds, on the bench: a stack knob's chips in order, each with the parts it takes; or what a macro knob is
// tied to, and how far it takes each. No more lines than the bench has room for above the strip: a longer one goes by the
// wheel, and says which lines are out.
void Screen::drawKnobHolds (G& g, float top, const MacroKnob& kn, bool stack)
{
    const int ac = rp.across;
    const float lineH = stack ? 74.0f : 32.0f, floor = 972.0f;
    const int n = stack ? (int) kn.snaps.size() : (int) kn.links.size(), linesAll = (n + ac - 1) / ac;
    int fit = std::max (1, (int) ((floor - top) / lineH));
    const bool more = linesAll > fit;
    if (more) fit = std::max (1, (int) ((floor - 18.0f - top) / lineH));         // (a line at the foot says which are out)
    most.bench = std::max (0, linesAll - fit);
    benchScroll = juce::jlimit (0, most.bench, benchScroll);
    benchArea = R (rp.x, top, rp.w, floor - top);
    // (a macro knob's ties are changed here, in the library: each is pressed, and its figure dragged)
    const bool can = ! stack && tiesAt().what != 0;
    tieSel = can ? juce::jlimit (-1, n - 1, tieSel) : -1;
    if (n == 0) { text (g, stack ? "NO CHIPS ON IT." : "TIED TO NOTHING.", R (rp.x, top, 240.0f, 24), fLbl, col::black, juce::Justification::centredLeft); return; }
    for (int q = 0; q < fit * ac; ++q)
    {
        const int i = benchScroll * ac + q;
        if (i >= n) break;
        if (stack) { drawSeenCard (g, R (rp.x + (float) (q % ac) * 282, top + (float) (q / ac) * 74, 272, 66), kn.snaps[(size_t) i], i + 1, kn.keysAt ((size_t) i), {}); continue; }
        const auto& l = kn.links[(size_t) i];
        const R r (rp.x + (float) (q % ac) * 282, top + (float) (q / ac) * 32, 272, 26);
        if (can) hit (r, hTie, i, 0);
        if (vis (g, r))
        {
            String where, label; const char* what = nullptr;
            describe (l.ctl, where, label, what);
            const bool chosen = can && i == tieSel;
            window (g, r, chosen ? col::green : col::white);
            onGlass (g, (where.isNotEmpty() ? where + "  " : String()) + label, r.reduced (8.0f, 0.0f).withTrimmedRight (52.0f), fNote, chosen ? col::green : col::white, col::black, juce::Justification::centredLeft);
            // (how far the full turn takes it: a figure, light on dark, as every number that is dragged is)
            tag (g, R (r.getRight() - 50.0f, r.getY() + 2.0f, 48.0f, r.getHeight() - 4.0f), (l.amount >= 0.0f ? "+" : "") + String (juce::roundToInt (l.amount * 100.0f)));
        }
    }
    if (more && vis (g, R (rp.x, floor - 16.0f, rp.w, 16.0f)))
        text (g, String (benchScroll * ac + 1) + " TO " + String (std::min (n, (benchScroll + fit) * ac)) + " OF " + String (n) + " (THE WHEEL)", R (rp.x, floor - 16.0f, rp.w, 14.0f), fNote, col::black, juce::Justification::centredLeft);
}

void Screen::drawKeptOpen (G& g, float top)
{
    const int at = openKeptAt();
    const auto& kk = ses.keptKnobs[(size_t) at];
    const MacroKnob kn = ses.knobFrom (kk.knob);
    const float y = openHead (g, top, kk.name().isEmpty() ? String ("UNNAMED") : kk.name(),
                              kk.stack ? "A STACK KNOB, KEPT BY ITSELF: ITS CHIPS IN ORDER, EACH WITH THE PARTS IT TAKES. PRESS A CHIP TO HEAR IT."
                                       : tiesAt().what != 0 ? "A MACRO KNOB, KEPT BY ITSELF: THE KNOBS IT IS TIED TO. DRAG A FIGURE UP OR DOWN TO CHANGE HOW FAR IT TAKES THAT KNOB; PRESS A TIE, THEN TAKE OUT, TO UNTIE IT."
                                                              : "A MACRO KNOB, KEPT BY ITSELF: THE KNOBS IT IS TIED TO, AND HOW FAR IT TAKES EACH.");
    drawKnobHolds (g, y, kn, kk.stack);
}

// A module open: its four macro knobs and its ten stack knobs in a row, each stack knob a pocket with its pile. A knob
// pressed has what it holds laid out under the row.
float Screen::drawModOpen (G& g, float top)
{
    const int at = openModAt();
    const auto& m = ses.modules[(size_t) at];
    const bool inUse = at == ses.moduleIndex();
    // (in a row across the library; six to a line in a third of a page)
    const float cw = rp.wide ? 112.0f : 84.0f, pitch = rp.wide ? 120.0f : 92.0f;
    const int perLine = rp.wide ? 14 : 6;
    const float y = openHead (g, top, m.name, String (inUse ? "THE MODULE IN THE CHASSIS: ITS KNOBS AS THEY STAND. " : "A MODULE KEPT: ITS KNOBS AS THEY WERE LEFT. ") + "PRESS A KNOB TO SEE WHAT IT HOLDS.") + 2.0f;
    int q = 0;
    MacroKnob shown; bool shownStack = false, has = false;
    for (int k = 0; k < 16; ++k)
    {
        if (Session::isCorner (k)) continue;
        const MacroKnob kn = ses.knobFrom (ses.moduleKnob (at, k));
        const R r (rp.x + (float) (q % perLine) * pitch, y + (float) (q / perLine) * 134.0f, cw, 96.0f);
        ++q;
        hit (r.withHeight (124.0f), hModKnob, k, 0);
        if (k == openModKnob) { shown = kn; shownStack = Session::isStack (k); has = true; }
        if (! vis (g, r.withHeight (126.0f).expanded (6.0f))) continue;
        knobCtl = -1;
        if (Session::isStack (k))
        {
            well (g, r);
            std::vector<juce::int64> ids;
            for (auto id : kn.snaps) if (ses.findSnap (id) != nullptr) ids.push_back (id);
            if (! ids.empty()) drawPile (g, r, ids, ses.findSnap (ids[0])->name, 6);
            knob (g, r.getRight() - 22.0f, r.getY() + 22.0f, 30, 0.0f, Kind::Knob, 0, col::white);       // (clear of the pocket's lip)
        }
        else
        {
            card (g, r.reduced (10.0f, 6.0f), false);
            knob (g, r.getCentreX(), r.getCentreY(), 40, 0.0f, Kind::Knob, 0, col::white);
        }
        if (k == openModKnob) { g.setColour (tone (col::green)); g.drawRoundedRectangle (r.expanded (3.0f), 7.0f, 3.0f); }
        String nm = String (k + 1) + (kn.name.isNotEmpty() ? " " + kn.name : String());
        while (nm.length() > 2 && std::ceil (tw (fNote, nm)) + 12 > cw) nm = nm.dropLastCharacters (1);
        plate (g, r.getX(), r.getBottom() + 6.0f, nm, fNote, 6, k == openModKnob ? col::green : col::black, col::lit, 22);
    }
    const float hy = y + (float) ((13 / perLine) + 1) * 134.0f + 2.0f;
    // (in the library a stack knob's pile is out in a row under the knobs, to be changed: the page draws it. Elsewhere, and for
    // a macro knob, what the pressed knob holds is shown)
    if (has && rowAt().what != 3)
    {
        if (tiesAt().what == 3)
        {
            // (in the library a macro knob's ties are changed here: which knob, its key, and how, in a line as a pile has)
            float x = rp.x, ly = hy;
            x += plate (g, x, ly, "KNOB " + String (openModKnob + 1) + (shown.name.isNotEmpty() ? ": " + shown.name : String()), fNote, 8, col::green, col::lit, 26) + 10.0f;
            headKey (g, x, ly, aRowOut, "TAKE OUT", col::white, tieSel >= 0);
            const int n = (int) shown.links.size();
            text (g, String (n) + (n == 1 ? " TIE" : " TIES") + (n == 0 ? ". LEARN, IN MACROS, TIES IT TO KNOBS." : ". DRAG A FIGURE UP OR DOWN TO CHANGE HOW FAR IT TAKES THAT KNOB; PRESS A TIE, THEN TAKE OUT, TO UNTIE IT."),
                  R (x + 6.0f, ly, rp.x + rp.w - x - 6.0f, 26), fLbl, col::black, juce::Justification::centredLeft);
            drawKnobHolds (g, hy + 34.0f, shown, false);
        }
        else
        {
            text (g, "KNOB " + String (openModKnob + 1) + (shown.name.isNotEmpty() ? ": " + shown.name : String()) + (shownStack ? ". ITS PILE:" : ". WHAT IT IS TIED TO:"),
                  R (rp.x, hy, std::min (900.0f, rp.w), 24), fLbl, col::black, juce::Justification::centredLeft);
            drawKnobHolds (g, hy + 28.0f, shown, shownStack);
        }
    }
    return hy;
}

void Screen::drawArrOpen (G& g, float top)
{
    const int at = openArrAt();
    const auto& a = ses.arrs[(size_t) at];
    const auto parts = ses.arrsParts (at);
    const bool inUse = at == ses.arrsIndex();
    int bars = 0; for (auto& pt : parts) bars += pt.bars;
    const float y = openHead (g, top, a.name, String (inUse ? "THE ARRANGEMENT ON THE PAGE: " : "AN ARRANGEMENT KEPT: ") + String ((int) parts.size()) + (parts.size() == 1 ? " PART, " : " PARTS, ") + String (bars)
                                               + " BARS, IN ORDER. PRESS A CHIP TO HEAR IT.");
    // (no more lines than the bench has room for above the strip: a longer one goes by the wheel)
    const int ac = rp.across, n = (int) parts.size(), linesAll = (n + ac - 1) / ac;
    const float floor = 972.0f;
    int fit = std::max (1, (int) ((floor - y) / 74.0f));
    const bool more = linesAll > fit;
    if (more) fit = std::max (1, (int) ((floor - 18.0f - y) / 74.0f));
    most.bench = std::max (0, linesAll - fit);
    benchScroll = juce::jlimit (0, most.bench, benchScroll);
    benchArea = R (rp.x, y, rp.w, floor - y);
    for (int q = 0; q < fit * ac; ++q)
    {
        const int i = benchScroll * ac + q;
        if (i >= n) break;
        const auto& pt = parts[(size_t) i];
        drawSeenCard (g, R (rp.x + (float) (q % ac) * 282, y + (float) (q / ac) * 74, 272, 66), pt.snap, i + 1, Session::kAllLanes, String (pt.bars) + (pt.bars == 1 ? " BAR" : " BARS"));
    }
    if (more && vis (g, R (rp.x, floor - 16.0f, rp.w, 16.0f)))
        text (g, String (benchScroll * ac + 1) + " TO " + String (std::min (n, (benchScroll + fit) * ac)) + " OF " + String (n) + " (THE WHEEL)", R (rp.x, floor - 16.0f, rp.w, 14.0f), fNote, col::black, juce::Justification::centredLeft);
    if (parts.empty()) text (g, "NO PARTS IN IT.", R (rp.x, y, 240.0f, 24), fLbl, col::black, juce::Justification::centredLeft);
}

const Screen::WellAt* Screen::wellAt (juce::Point<float> p) const
{
    for (const auto& w : wellsAt) if (w.r.contains (p)) return &w;
    return nullptr;
}

int Screen::rowSlotAt (juce::Point<float> p) const
{
    const auto pile = rowAt();
    if (pile.what == 0 || rowArea.isEmpty() || ! rowArea.expanded (4.0f, 6.0f).contains (p)) return -1;
    const int n = (int) ses.pileSnaps (pile).size();
    const int col = juce::jlimit (0, 6, (int) std::floor ((p.x - 23.0f + 141.0f) / 282.0f));
    const int row = juce::jlimit (0, 1, (int) std::floor ((p.y - rowArea.getY()) / 74.0f));
    return juce::jlimit (0, n, (rowScroll + row) * 6 + col);
}

// A pocket pressed. With ADD TO lit and a stack's pocket: the chip in hand goes into that stack. Otherwise it is open
// (the bin: whatever was open on this shelf is put away).
void Screen::openWell (int kind, int index)
{
    if (addTo && (kind == 1 || kind == 5) && ses.findSnap (handId) != nullptr)
    {
        const String name = ses.findSnap (handId)->name;
        if (kind == 5) index = ses.rackNew();
        if (index >= 0 && index < (int) ses.stacks.size() && ses.rackAdd (index, handId, handParts)) rackSay (name + " IS IN " + ses.stacks[(size_t) index].name);
        else rackSay ("THAT STACK IS THE WEB LIBRARY'S: IT IS CHANGED THERE");
        return;
    }
    addTo = false;
    libScroll = 0; rowSel = -1; rowScroll = 0; benchScroll = 0; tieSel = -1;
    if (kind == 0) { if (rackShelf == 0) inStack = {}; else if (rackShelf == 1) openKept = {}; else if (rackShelf == 2) { openMod = {}; openModKnob = -1; } else openArr = {}; named = {}; }
    else if (kind == 1 && index < (int) ses.stacks.size()) { inStack = ses.stacks[(size_t) index].id; nameIs (3, 0, -1, inStack); }
    else if (kind == 2 && index < (int) ses.keptKnobs.size()) { openKept = ses.keptKnobs[(size_t) index].id; nameIs (4, 0, -1, openKept); }
    else if (kind == 3 && index < (int) ses.modules.size()) { openMod = ses.modules[(size_t) index].id; openModKnob = -1; nameIs (5, 0, -1, openMod); }
    else if (kind == 4 && index < (int) ses.arrs.size()) { openArr = ses.arrs[(size_t) index].id; nameIs (6, 0, -1, openArr); }
    else if (kind == 5)
    {
        const int made = ses.rackNew();
        if (made >= 0) { inStack = ses.stacks[(size_t) made].id; nameIs (3, 0, -1, inStack); rackFirst = 9999; rackSay (ses.stacks[(size_t) made].name + ": A NEW STACK, OUT ON THE BENCH. CARRY CHIPS UP INTO IT FROM THE BIN, OR PRESS ONE AND THEN ADD"); }
    }
}

// What was carried in the library is let go. A chip of the bin: into the open stack's row where it was let go, or
// into the stack whose pocket it is over (a reference: the chip stays in the bin). A chip of the row: to another
// place in the row; onto another stack's pocket, into that stack too; off the row, out of this stack.
void Screen::rackDrop (juce::Point<float> p)
{
    const auto pile = rowAt();
    const int slot = rowSlotAt (p);
    const auto* w = wellAt (p);
    const char* const theirs = "THAT STACK IS THE WEB LIBRARY'S: IT IS CHANGED THERE";
    auto into = [&] (int index, juce::int64 id, juce::uint32 keys)
    {
        const auto* sn = ses.findSnap (id);
        if (sn == nullptr) return;
        if (w->kind == 5) index = ses.rackNew();
        if (index >= 0 && index < (int) ses.stacks.size() && ses.rackAdd (index, id, keys)) rackSay (sn->name + " IS IN " + ses.stacks[(size_t) index].name);
        else rackSay (theirs);
    };
    const bool pocket = w != nullptr && (w->kind == 1 || w->kind == 5);
    if (drag.kind == hCard && drag.a < (int) ses.library.size())
    {
        const auto id = ses.library[(size_t) drag.a].id;
        const auto parts = id == handId ? handParts : Session::kAllLanes;
        if (slot >= 0 && pile.what != 0) { if (! ses.pileInsert (pile, slot, id, parts)) rackSay (ses.pileCan (pile) ? "NO ROOM IN THAT PILE FOR ANOTHER" : theirs); else rowSel = -1; }
        else if (pocket) into (w->index, id, parts);
    }
    else if (drag.kind == hRowCard && pile.what != 0)
    {
        const auto snaps = ses.pileSnaps (pile);
        if (drag.a >= (int) snaps.size()) return;
        const auto id = snaps[(size_t) drag.a];
        const auto keys = ses.pileKeysAt (pile, drag.a);
        const bool itsOwn = pile.what == 1 && w != nullptr && w->kind == 1 && w->index == pile.index;       // (let go on its own pocket: nothing)
        if (! ses.pileCan (pile)) { if (pocket && ! itsOwn) into (w->index, id, keys); else rackSay (theirs); }
        else if (slot >= 0) { const int to = slot > drag.a ? slot - 1 : slot; ses.pileMove (pile, drag.a, to); rowSel = to; }
        else if (pocket) { if (! itsOwn) into (w->index, id, keys); }
        else
        {
            const auto* sn = ses.findSnap (id);
            const String from = ses.pileName (pile);
            ses.pileDrop (pile, drag.a);
            rowSel = -1;
            rackSay ((sn != nullptr ? sn->name : String ("IT")) + " IS OUT OF " + from + ". IT IS STILL IN THE BIN");
        }
    }
}

// The strip's keys for what is open. what: 0 NEW, 1 COPY, 2 put to work, 3 RENAME, 4 DELETE (held).
void Screen::rackOpenDo (int what)
{
    const int open = rackOpen();
    if (open == 1)
    {
        const int os = inAt();
        const String name = ses.stacks[(size_t) os].name;
        if (what == 1) { const int made = ses.rackCopy (os); if (made >= 0) { inStack = ses.stacks[(size_t) made].id; nameIs (3, 0, -1, inStack); rowSel = -1; rackFirst = 9999; rackSay (name + " IS KEPT AGAIN AS " + ses.stacks[(size_t) made].name + ", WHICH IS THE ONE OPEN NOW"); } }
        else if (what == 2)
        {
            const int k = ses.rackOntoFree (os);
            rackSay (k >= 0 ? name + " IS ON KNOB " + String (k + 1) + " (A COPY: THE STACK STAYS IN ITS POCKET)" : ses.freeStackKnob() < 0 ? String ("NO STACK KNOB IS FREE: CLEAR ONE IN MACROS FIRST") : String ("NOTHING IN IT TO PUT ON A KNOB"));
            if (k >= 0 && page == pMacros) { selKnob = k; colSel = -1; colScroll = 0; }        // (the designer: that knob is on the bench)
        }
        else if (what == 3) { if (ses.rackCanChange (os) && ! openNameRect.isEmpty()) beginEdit (openNameRect, name, [this, id = inStack] (String t) { ses.rackRename (ses.rackIndex (id), t); }); }
        else if (what == 4) { ses.stackForget (os); inStack = {}; rowSel = -1; rackSay (name + " IS DELETED. ITS CHIPS ARE STILL IN THE BIN"); }
        return;
    }
    // (a kept knob, a module, an arrangement: as the shelf of the seventieth build did them, for the one that is open)
    int kind = -1, sel = -1;
    if (open == 2)
    {
        const int at = openKeptAt();
        kind = ses.keptKnobs[(size_t) at].stack ? skStackKnobs : skMacroKnobs;
        sel = 0;
        for (int i = 0; i < at; ++i) if (ses.keptKnobs[(size_t) i].stack == ses.keptKnobs[(size_t) at].stack) ++sel;
    }
    else if (open == 3) { kind = skModules; sel = openModAt(); }
    else if (open == 4) { kind = skArrs; sel = openArrAt(); }
    else if (rackShelf == 2 && what == 0) { kind = skModules; sel = ses.moduleIndex(); }
    else if (rackShelf == 3 && what == 0) { kind = skArrs; sel = ses.arrsIndex(); }
    if (kind < 0) return;
    shelfSel[(size_t) kind] = sel;
    shelfNameRect = openNameRect;
    if (what == 4) shelfDelete (kind); else shelfDo (kind, what);
    // (what is open now: the one the key made or put to work; nothing, if it was deleted)
    const int now = shelfSel[(size_t) kind];
    if (kind == skModules) { openMod = what == 4 ? String() : now >= 0 && now < (int) ses.modules.size() ? ses.modules[(size_t) now].id : String(); if (what != 3) openModKnob = -1; }
    else if (kind == skArrs) openArr = what == 4 ? String() : now >= 0 && now < (int) ses.arrs.size() ? ses.arrs[(size_t) now].id : String();
    else if (what == 4) openKept = {};
    // (what NEW or COPY made is the one open, and the one RENAME names)
    if (what == 4) named = {};
    else if (kind == skModules && openMod.isNotEmpty()) nameIs (5, 0, -1, openMod);
    else if (kind == skArrs && openArr.isNotEmpty()) nameIs (6, 0, -1, openArr);
}

void Screen::drawLibrary (G& g)
{
    rp = RackPlace();
    rackTidy();
    benchArea = {};
    const int open = rackOpen();
    const auto pile = rowAt();
    if (pile.what == 0) { rowSel = -1; rowArea = {}; }
    drawChipRack (g);
    int total = 0;
    if (open == 0 || pile.what != 0)
    {
        // a pile that is out (the open stack's, an open stack knob's, the pressed knob's of an open module) is a row to be changed;
        // then the bin under it: every chip
        float head = kBenchY;
        if (open == 3) head = drawModOpen (g, kBenchY);         // (the module's knobs first: the pressed one's pile is the row under them)
        if (pile.what != 0) head = drawStackRow (g, head);
        const float top = drawFind (g, 23.0f, 1682.0f, head);
        const auto list = listed();
        const int rows = std::max (1, (int) ((974.0f - top + 8.0f) / 74.0f));
        total = (int) list.size();
        most.bin = std::max (0, (total - 1) / 6 - (rows - 1));
        libScroll = juce::jlimit (0, most.bin, libScroll);
        const auto inPile = ses.pileSnaps (pile);
        for (int n = 0; n < 6 * rows; ++n)
        {
            const int at = libScroll * 6 + n;
            if (at >= total) break;
            const int i = list[(size_t) at];
            const auto& s = ses.library[(size_t) i];
            const R r (23.0f + (float) (n % 6) * 282, top + (float) (n / 6) * 74, 272, 66);
            drawSnapCard (g, s, i, r, s.id == selSnap, hCard);
            // (a chip that is in the pile that is out says so, on its card)
            if (pile.what != 0 && s.id != handId && std::find (inPile.begin(), inPile.end(), s.id) != inPile.end() && vis (g, r))
            { g.setColour (tone (col::green)); g.fillRect (R (r.getX() + 8.0f, r.getBottom() - 6.0f, r.getWidth() - 16.0f, 3.0f)); }       // (a green line along its near edge)
        }
    }
    else if (open == 2) drawKeptOpen (g, kBenchY);
    else if (open == 3) drawModOpen (g, kBenchY);
    else drawArrOpen (g, kBenchY);

    const R strip (23, 980, 1682, 110);
    panel (g, strip, col::putty);
    const float x0 = strip.getX() + 5, y0 = strip.getY() + 5, ky = y0 + 40;
    const juce::ScopedValueSetter<bool> onOneLine (oneLine, true);      // (no room under these keys for a second line: a legend that ran on went off the plate)
    const int os = inAt();
    const float wTitle = title (g, x0, y0, open == 1 ? ses.stacks[(size_t) os].name + ": " + String ((int) ses.stacks[(size_t) os].snaps.size()) + (ses.stacks[(size_t) os].snaps.size() == 1 ? " CHIP" : " CHIPS")
                    : open == 2 ? String ("A KNOB KEPT") : open == 3 ? String ("A MODULE") : open == 4 ? String ("AN ARRANGEMENT")
                    : String (total) + (total == 1 ? " CHIP" : " CHIPS"));
    actKey (g, aMain, x0, ky, 64, "MAIN", col::white, 5);
    actKey (g, aLibrary, x0 + 66, ky, 64, "LIBRARY", col::green, 5, true, true);
    actKey (g, aMacros, x0 + 132, ky, 64, "MACROS", col::white, 5);
    actKey (g, aArrange, x0 + 198, ky, 64, "ARRANGE", ses.arrPlaying() ? col::green : col::white, 5);
    const float ax = x0 + 286;
    const bool handIn = ses.findSnap (handId) != nullptr, has = ses.findSnap (selSnap) != nullptr;
    String help;
    {
        // the chip in hand, whatever is open (what is open has its own keys, after its name)
        actKey (g, aRename, ax, ky, 64, "RENAME", col::yellow, 5, namedNow().what != 0);
        actKey (g, aDelete, ax + 66, ky, 64, "DELETE", col::red, 5, has);
        actKey (g, aRecallCue, ax + 148, ky, 64, "RECALL", col::yellow, 5, handIn || has);
        actKey (g, aAddTo, ax + 214, ky, 64, "ADD TO", addTo ? col::green : col::white, 5, handIn && ! ses.stacks.empty(), addTo);
        actKey (g, aPhones, ax + 296, ky, 64, "PHONES", eng.phones() ? col::green : col::putty, 5, true, eng.phones());
        actKey (g, aNotes, ax + 362, ky, 64, "NOTES", ses.recallNotes ? col::green : col::putty, 5, true, ses.recallNotes);
        juce::StringArray in;
        const int nIn = handIn ? ses.stacksHolding (handId, &in) : 0;
        help = addTo ? String ("ADD TO IS LIT: PRESS A STACK'S POCKET AND THE CHIP IN HAND GOES INTO IT. PRESS ADD TO AGAIN TO LEAVE OFF.")
             : open == 1 && rowSel >= 0 ? String ("TAKE OUT TAKES IT OUT OF THIS STACK ONLY: IT STAYS IN THE BIN AND IN EVERY OTHER STACK.")
             : handIn && nIn > 0 ? ses.findSnap (handId)->name + " IS IN " + String (nIn) + (nIn == 1 ? " STACK: " : " STACKS: ") + in.joinIntoString (", ") + ". ITS POCKETS ARE RINGED."
             : open == 1 ? String ("A CHIP CARRIED UP FROM THE BIN GOES INTO THE ROW WHERE IT IS LET GO. THE CHIP STAYS IN THE BIN: THE STACK HOLDS A REFERENCE.")
             : open == 2 ? String ("A KNOB KEPT IS A COPY: WHAT IS CHANGED HERE CHANGES THIS ONE, NOT A KNOB IT WAS PUT ON; WHAT IS CHANGED ON A PLACE CHANGES THE MODULE'S KNOB, NOT THIS ONE.")
             : open == 3 ? String ("PRESS A KNOB OF THE MODULE. A STACK KNOB: ITS PILE IS OUT IN A ROW, AND IS CHANGED AS A STACK'S IS. A MACRO KNOB: ITS TIES, TO CHANGE OR TAKE OUT (A NEW TIE IS MADE WITH LEARN, IN MACROS). THE MODULE IN THE CHASSIS IS GREEN.")
             : open == 4 ? String ("THE ARRANGEMENT ON THE PAGE IS GREEN: WHAT IS CHANGED ON ITS PAGE CHANGES IT.")
             : ! eng.cueIsHeard() ? String ("A CHIP PRESSED PLAYS IN THE HEADPHONES. THEY NEED OUTPUTS 3 + 4 (SETUP), OR PHONES LIT: THE SET LEFT, THE CHIP RIGHT.")
             : String ("PRESS A CHIP TO HEAR IT. RECALL (OR TWO QUICK PRESSES) TAKES IT TO THE SET. CARRY IT ONTO A STACK'S POCKET, OR ADD TO AND THEN THE POCKET, TO PUT IT IN THAT STACK.");
    }
    // (what the page is saying, on the strip's own line beside its title: the whole of it, with or without the web's keys under it)
    text (g, help, R (x0 + wTitle + 16.0f, y0, strip.getRight() - 10.0f - (x0 + wTitle + 16.0f), 26), fLbl, col::black, juce::Justification::centredLeft);
    if (online == nullptr) return;
    // ONLINE: each snapshot goes to the library on the web as it is taken. The lamp: green when the
    // library holds them all, yellow while sending, amber when it waits (no network, or not set up), red when turned away.
    const auto st = online->status().state;
    onlineStamp = online->stamp();
    const Colour lamp = st == Online::State::off ? col::putty : st == Online::State::upToDate ? col::green : st == Online::State::working ? col::yellow
                      : st == Online::State::refused ? col::red : col::amber;
    actKey (g, aOnline, x0 + 846, ky, 64, "ONLINE", lamp, 5, true, st != Online::State::off);
    actKey (g, aOnlineSetup, x0 + 912, ky, 64, "SET UP", col::white, 5);
    // LOAD: what the web library's tray holds, brought here (its stacks, its macros, a song, and the snapshots this library lacks)
    const bool loading = online->loading();
    actKey (g, aFetch, x0 + 978, ky, 64, "WEB LOAD", loading ? col::yellow : col::white, 5, online->setup().complete(), loading);
    const String said = online->loadLine();
    text (g, said.isNotEmpty() ? said : online->line(), R (x0 + 1060, ky, 606, 40), fLbl, col::black, juce::Justification::centredLeft);
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
// The chips the rack lists where it is a third of a page (the bin's, or the open stack's in its order): whose and in what
// order at their head, then their cards. Pressed: in hand, and heard. ADD on a card, or the card carried, puts it into
// what the page is making (the chosen stack knob's pile; the arrangement).
void Screen::drawPickList (G& g, float top)
{
    const float y0 = drawFind (g, rp.x, rp.w, top);
    const auto list = listed();
    const int total = (int) list.size(), across = rp.across, rows = std::max (1, (int) ((974.0f - y0) / 74.0f));
    most.bin = std::max (0, (total - 1) / across - (rows - 1));
    libScroll = juce::jlimit (0, most.bin, libScroll);
    for (int n = 0; n < rows * across; ++n)
    {
        const int at = libScroll * across + n;
        if (at >= total) break;
        const int i = list[(size_t) at];
        const R cr (rp.x + (float) (n % across) * 282, y0 + (float) (n / across) * 74, 272, 66);
        drawSnapCard (g, ses.library[(size_t) i], i, cr, false, hPick);
        hit (R (cr.getRight() - 6 - 44, cr.getY() + 6, 44, 24), hPickAdd, i, 0);
    }
    if (total == 0 && vis (g, R (rp.x, y0, rp.w, 40)))
        text (g, inAt() >= 0 ? "NOTHING IN THIS STACK YET. PRESS ALL CHIPS, THEN CARRY A CHIP ONTO ITS POCKET." : "NO CHIPS YET: SNAPSHOT, ON THE BAR, TAKES ONE.", R (rp.x, y0, rp.w, 24), fLbl, col::black, juce::Justification::centredLeft);
}

// The rack as a third of a page: under its pockets, what the open one holds.
void Screen::drawRackBench (G& g)
{
    const int open = rackOpen();
    const float top = benchY();
    rowArea = {}; rowSel = -1; addTo = false; benchArea = {};
    if (open <= 1) drawPickList (g, open == 1 ? openHead (g, top, ses.stacks[(size_t) inAt()].name, {}) : top);
    else if (open == 2) drawKeptOpen (g, top);
    else if (open == 3) drawModOpen (g, top);
    else drawArrOpen (g, top);
}

// The chosen knob, large: its number and name, what it moves, its turn (a macro knob's depth beside it; a stack knob's
// chips in their order), KEEP, and what it moves lane by lane.
void Screen::drawKnobDetail (G& g, R r)
{
    const int k = selKnob;
    const auto& kn = ses.knobs[(size_t) k];
    const bool stack = Session::isStack (k);
    if (vis (g, r)) card (g, r, false);
    const float x0 = r.getX() + 8, y0 = r.getY() + 8;
    const int wide = (int) r.getWidth() - 16;
    const String num (k + 1);
    const float wTag = std::max (30.0f, std::ceil (tw (fLegend, num)) + 8.0f);
    tag (g, R (x0, y0, wTag, 26), num);
    const float wn = plate (g, x0 + wTag, y0, kn.name.isEmpty() ? String ("UNNAMED") : kn.name, fLegend, 8, col::white, col::black);
    nameAt (2, 0, k, {}, R (x0 + wTag, y0, std::max (wn, 150.0f), 26));
    const int scope = (int) ses.knobScope (k).size();
    const String mv = "MOVES " + String (scope);
    const float mvX = r.getRight() - 8 - (std::ceil (tw (fNote, mv)) + 12);
    plate (g, mvX, y0, mv, fNote, 6, col::white, col::black);
    // (a MOTION on it says so, beside that: lit while its wave is running)
    if (const int on = ses.motionOn (k); on != 0)
    {
        String ms ("MOTION");
        for (int u = 0; u < cx::kMotUnits; ++u) if ((on >> u) & 1) ms << " " << String (u + 1);
        plate (g, mvX - 2 - (std::ceil (tw (fNote, ms)) + 12), y0, ms, fNote, 6, col::green, col::black);
    }
    const float ky = y0 + 42;
    if (ses.knobLive (k))
    {
        const float v = eng.shown (cx::kSnap + k);
        lastShown[(size_t) (cx::kSnap + k)] = v;
        knob (g, x0 + 32, ky + 20, 40, v, Kind::Knob, 0, k == ses.learning ? col::yellow : ! stack ? col::white : ses.stackCue ? col::amber : col::teal);
        hit (R (x0 + 8, ky - 2, 48, 44), hSnapKnob, k, 0);
    }
    else hollow (g, x0 + 32, ky + 20, 40, true);
    legend (g, x0 + 32, ky + 43, "TURN");
    String order;
    float kx = x0 + 74;
    if (! stack)
    {
        knob (g, x0 + 98, ky + 20, 40, kn.depth * 0.5f, Kind::Knob, 0, col::lit);
        hit (R (x0 + 74, ky - 2, 48, 44), hDepth, k, 0);
        legend (g, x0 + 98, ky + 43, "DEPTH");
        kx = x0 + 134 + plate (g, x0 + 134, ky + 8, k == Session::kCueKnob ? "PRESS: CUE" : "MACRO KNOB", fNote, 6, col::white, col::black) + 10;
    }
    else
    {
        // (fifteen of them shown; a pile with more says how many more. One that does not take every part of the rig is drawn amber.)
        const int n = (int) kn.snaps.size();
        for (int s = 0; s < 15; ++s)
        {
            const R sr (x0 + 74 + (float) (s % 5) * 37.0f, ky + (float) (s / 5) * 21, 34, 19);
            if (s == 14 && n > 15) { text (g, "+" + String (n - 14), sr, fLbl, col::black); continue; }
            if (s < n)
            {
                const int li = ses.indexOf (kn.snaps[(size_t) s]);
                const bool some = (kn.lanesAt ((size_t) s) & Session::kAllLanes) != Session::kAllLanes;
                window (g, sr, some ? col::amber : col::black);
                text (g, String (li + 1), sr, fLbl, some ? col::black : col::lit);
                hit (sr, hSlot, k, s);
            }
            else { g.setColour (tone (col::sea)); g.drawRect (sr.reduced (0.5f), 1.0f); }
        }
        for (size_t s = 0; s < kn.snaps.size(); ++s)
            if (auto* sn = ses.findSnap (kn.snaps[s])) order << (s > 0 ? "  >  " : "") << sn->name;
        kx = x0 + 74 + 5 * 37.0f + 10;
    }
    // KEEP: the knob as it is, into the knobs kept (a pocket on the rack's KNOBS shelf); a stack knob's pile, as a stack (STACKS)
    {
        auto keep = [&] (float y, int kind, const String& t)
        {
            const R pr (kx, y, std::ceil (tw (fNote, t)) + 16.0f, 24.0f);
            if (vis (g, pr)) { window (g, pr, col::white); text (g, t, pr, fNote, col::black); }
            hit (pr, kind, 0, 0);
        };
        if (ses.knobHolds (k))
        {
            keep (ky - 2, hKeptKeep, "KEEP KNOB " + String (k + 1));
            if (stack) keep (ky + 26, hRackKeep, "KEEP ITS PILE AS A STACK");
        }
    }
    g.setColour (tone (col::black)); g.setFont (fLbl);
    // (a knob tied straight to knobs says so; one being taught says that)
    const int tied = (int) kn.links.size();
    if (k == ses.learning) order = "BEING TAUGHT: " + String (ses.learnMoves()) + " MOVED. KEEP, OR DROP.";
    else if (! stack && tied > 0) order = "TIED TO " + String (tied) + (tied == 1 ? " KNOB." : " KNOBS.");
    g.drawFittedText (order.isNotEmpty() ? order : stack ? String ("NO CHIPS ON IT. CARRY ONE ONTO ITS PILE FROM THE RACK AT THE RIGHT, OR PRESS A CHIP'S ADD. A WHOLE PILE CARRIED ONTO ITS PLACE PUTS THAT STACK ON IT.") : String ("TIED TO NOTHING. PRESS LEARN."),
                      juce::Rectangle<int> ((int) x0, (int) (ky + 72), wide, 32), juce::Justification::topLeft, 2, 1.0f);
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
            if (! stack && l.ctl == i) { const int far = (int) std::lround (l.amount * kn.depth * 100.0f); moves << (far >= 0 ? " +" : " -") << std::abs (far); }
    }
    const int room = (int) (r.getBottom() - 8 - (ky + 110));
    g.drawFittedText (moves, juce::Rectangle<int> ((int) x0, (int) (ky + 110), wide, room), juce::Justification::topLeft, std::max (2, room / 15), 0.8f);
}

// The macro designer. Along the top, the module in the chassis: four macro knobs and ten stack knobs, a stack knob a
// pocket with its pile in it. Under it the chosen knob's own bench: a stack knob's pile as a column of cards, and the
// knob itself, large. The right-hand third is the rack, the same one the library is.
void Screen::drawMacros (G& g)
{
    rp = RackPlace { 1151.0f, 554.0f, 78.0f, 4, 2, false };
    rackTidy();
    if (Session::isCorner (selKnob)) selKnob = 0;
    // ---- the module in the chassis
    {
        const float wm = plate (g, 23.0f, 78.0f, "MODULE: " + ses.moduleName(), fLegend, 8, col::black, col::lit, 26);
        if (vis (g, R (23.0f + wm + 10.0f, 78.0f, 1118.0f - wm - 10.0f, 26)))
            text (g, "THE ONE IN THE CHASSIS. PRESS A KNOB'S PLACE TO CHOOSE IT. A STACK KNOB IS A POCKET: ITS PILE IS WHAT IT TURNS THROUGH.",
                  R (23.0f + wm + 10.0f, 78.0f, 1118.0f - wm - 10.0f, 26), fLbl, col::black, juce::Justification::centredLeft);
    }
    for (int k = 0; k < 16; ++k)
    {
        if (Session::isCorner (k)) continue;        // (13 and 16 hold no knob: on the E16 those corners are the lane wheel and OPEN, pressed)
        const R r = placeRect (k);
        const auto& kn = ses.knobs[(size_t) k];
        const bool sel = k == selKnob, stack = Session::isStack (k), live = ses.knobLive (k);
        hit (r.withHeight (126.0f), hKnobCard, k, 0);
        if (vis (g, r.withHeight (128.0f).expanded (6.0f)))
        {
            const float v = eng.shown (cx::kSnap + k);
            const Colour light = k == ses.learning ? col::yellow : ! stack ? col::white : ses.stackCue ? col::amber : col::teal;
            knobCtl = -1;
            if (stack)
            {
                well (g, r);
                std::vector<juce::int64> ids;
                for (auto id : kn.snaps) if (ses.findSnap (id) != nullptr) ids.push_back (id);
                if (! ids.empty()) drawPile (g, r, ids, ses.findSnap (ids[0])->name, 6);
                if (live) { lastShown[(size_t) (cx::kSnap + k)] = v; knob (g, r.getRight() - 20.0f, r.getY() + 19.0f, 30, v, Kind::Knob, 0, light); }
                else hollow (g, r.getRight() - 20.0f, r.getY() + 19.0f, 22, true);
            }
            else
            {
                card (g, r.reduced (4.0f, 6.0f), false);
                if (live) { lastShown[(size_t) (cx::kSnap + k)] = v; knob (g, r.getCentreX(), r.getY() + 44.0f, 40, v, Kind::Knob, 0, light); }
                else hollow (g, r.getCentreX(), r.getY() + 44.0f, 40, true);
            }
            if (sel) { g.setColour (tone (col::green)); g.drawRoundedRectangle (r.expanded (3.0f), 7.0f, 3.0f); }
            String nm = String (k + 1) + (kn.name.isNotEmpty() ? " " + kn.name : String());
            while (nm.length() > 2 && std::ceil (tw (fNote, nm)) + 12 > 76.0f) nm = nm.dropLastCharacters (1);
            plate (g, r.getX(), r.getBottom() + 6.0f, nm, fNote, 6, sel ? col::green : col::black, col::lit, 22);
        }
        // (the knob itself turns where it stands)
        if (live) hit (stack ? R (r.getRight() - 37.0f, r.getY() + 2.0f, 34.0f, 34.0f) : R (r.getCentreX() - 24.0f, r.getY() + 22.0f, 48.0f, 44.0f), hSnapKnob, k, 0);
    }
    // ---- the chosen knob's own bench: a stack knob's pile in a column, and the knob itself
    const bool column = columnShown();
    if (column) drawColumn (g); else colSel = -1;
    drawKnobDetail (g, column ? R (305.0f, kColHead, 836.0f, 330.0f) : R (23.0f, kColHead, 1118.0f, 330.0f));
    // ---- the rack: the same one the library is
    drawChipRack (g);
    drawRackBench (g);

    const R strip (23, 980, 1682, 110);
    panel (g, strip, col::putty);
    const float x0 = strip.getX() + 5, y0 = strip.getY() + 5, ky = y0 + 40;
    const juce::ScopedValueSetter<bool> onOneLine (oneLine, true);      // (no room under these keys for a second line: a legend that ran on went off the plate)
    title (g, x0, y0, "KNOB " + String (selKnob + 1));
    actKey (g, aMain, x0, ky, 64, "MAIN", col::white, 5);
    actKey (g, aLibrary, x0 + 66, ky, 64, "LIBRARY", col::white, 5);
    actKey (g, aMacros, x0 + 132, ky, 64, "MACROS", col::green, 5, true, true);
    actKey (g, aArrange, x0 + 198, ky, 64, "ARRANGE", ses.arrPlaying() ? col::green : col::white, 5);
    actKey (g, aRename, x0 + 286, ky, 64, "RENAME", col::yellow, 5, namedNow().what != 0);
    actKey (g, aClearKnob, x0 + 352, ky, 64, "CLEAR", col::red, 5, ses.knobHolds (selKnob));         // (held: the chosen knob holds nothing)
    // (LEARN is for the four macro knobs: a stack knob holds chips and is tied to nothing)
    if (! Session::isStack (selKnob)) actKey (g, aLearn, x0 + 418, ky, 64, ses.learning == selKnob ? "KEEP" : "LEARN", ses.learning == selKnob ? col::green : col::yellow, 5, true, ses.learning == selKnob);
    // CUE: how the stack knobs are played, as on the module's own panel
    actKey (g, aStackCue, x0 + 484, ky, 64, "CUE", ses.stackCue ? col::amber : col::putty, 5, true, ses.stackCue);
    // PHONES, as in the library: a chip pressed here is heard in the headphones, and this is how they are had on one pair of outputs
    actKey (g, aPhones, x0 + 1612, ky, 64, "PHONES", eng.phones() ? col::green : col::putty, 5, true, eng.phones());
    // MOTION: three waves, each put on a snapshot knob. The knob swings in time with the clock, and what it moves goes with it.
    motSig = motionSig();
    const float m0 = x0 + 572;
    for (int u = 0; u < cx::kMotUnits; ++u)
    {
        const float ux = m0 + (float) u * 254;
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
    drawCarry (g);          // (what is in the hand, over everything)
}

// ---- carrying a card
int Screen::placeAt (juce::Point<float> p)
{
    for (int k = 0; k < 16; ++k) if (placeRect (k).contains (p)) return k;
    return -1;
}

int Screen::columnPlaceAt (juce::Point<float> p) const
{
    if (! columnShown() || p.x < kColX - 5.0f || p.x > kColX + 277.0f || p.y < kColHead - 4.0f || p.y > 975.0f) return -1;
    const int n = (int) ses.knobs[(size_t) selKnob].snaps.size();
    return juce::jlimit (0, n, colScroll + (int) std::floor ((p.y - kColY + 37.0f) / 74.0f));
}

bool Screen::placeTakes (int k) const
{
    if (k < 0 || k >= 16 || Session::isCorner (k)) return false;
    // (a pocket of the rack: a kept knob goes on a place of its kind; a stack's pile on a stack knob; nothing else is carried there)
    if (drag.kind == hWell) return drag.a == 2 ? drag.b < (int) ses.keptKnobs.size() && ses.keptKnobs[(size_t) drag.b].stack == Session::isStack (k) : drag.a == 1 && Session::isStack (k);
    return Session::isStack (k);        // (a chip goes on a stack knob)
}

void Screen::drawCarry (G& g)
{
    if (! carrying()) return;
    if (page == pLibrary)
    {
        // the library: a chip carried shows where it would go (a bar between two chips of the open row, a ring on a stack's pocket) and its name at the pointer
        const auto pile = rowAt();
        const int os = pile.what == 1 ? pile.index : -1, slot = rowSlotAt (carryAt);
        String name;
        if (drag.kind == hCard && drag.a < (int) ses.library.size()) name = ses.library[(size_t) drag.a].name;
        else if (drag.kind == hRowCard && pile.what != 0) { const auto snaps = ses.pileSnaps (pile); if (drag.a < (int) snaps.size()) if (const auto* sn = ses.findSnap (snaps[(size_t) drag.a])) name = sn->name; }
        if (slot >= 0 && pile.what != 0)
        {
            const int q = slot - rowScroll * 6;
            const float bx = 23.0f + (float) (q % 6) * 282 - 6.0f, by = rowArea.getY() + 4.0f + (float) (q / 6) * 74;
            g.setColour (tone (col::yellow)); g.fillRect (R (juce::jlimit (19.0f, 1702.0f, bx), by, 4.0f, 66.0f));
        }
        else if (const auto* w = wellAt (carryAt); w != nullptr && (w->kind == 1 || w->kind == 5) && ! (drag.kind == hRowCard && w->kind == 1 && w->index == os))
        {
            g.setColour (tone (col::yellow)); g.drawRoundedRectangle (R (w->r.getX(), w->r.getY(), w->r.getWidth(), 120.0f).expanded (4.0f), 7.0f, 3.0f);
        }
        const float w = std::ceil (tw (fLegend, name)) + 16;
        plate (g, juce::jlimit (10.0f, kW - w - 10.0f, carryAt.x + 10), std::max (60.0f, carryAt.y - 32), name, fLegend, 8, col::yellow, col::black, 24);
        return;
    }
    if (page == pArrange)
    {
        // the arrangement's page: a chip, or a stack's pile, carried from the rack. Over the arrangement it would be added to it
        // (ringed); a chip over a stack's pocket would go into that stack
        String what;
        const bool chip = drag.kind == hPick && drag.a < (int) ses.library.size(), stackPile = drag.kind == hWell && drag.a == 1 && drag.b < (int) ses.stacks.size();
        if (chip) what = ses.library[(size_t) drag.a].name;
        else if (stackPile) what = ses.stacks[(size_t) drag.b].name + " (" + String ((int) ses.stacks[(size_t) drag.b].snaps.size()) + ")";
        else return;
        if (arrArea().contains (carryAt)) { g.setColour (tone (col::yellow)); g.drawRoundedRectangle (arrArea().reduced (2.0f), 7.0f, 3.0f); }
        else if (const auto* w = wellAt (carryAt); chip && w != nullptr && (w->kind == 1 || w->kind == 5))
        { g.setColour (tone (col::yellow)); g.drawRoundedRectangle (R (w->r.getX(), w->r.getY(), w->r.getWidth(), 120.0f).expanded (4.0f), 7.0f, 3.0f); }
        const float ww = std::ceil (tw (fLegend, what)) + 16;
        plate (g, juce::jlimit (10.0f, kW - ww - 10.0f, carryAt.x + 10), std::max (60.0f, carryAt.y - 32), what, fLegend, 8, col::yellow, col::black, 24);
        return;
    }
    String name;
    const bool pile = drag.kind == hWell && drag.a == 1 && drag.b < (int) ses.stacks.size();
    const bool keptPocket = drag.kind == hWell && drag.a == 2 && drag.b < (int) ses.keptKnobs.size();
    if (drag.kind == hWell && ! pile && ! keptPocket) return;       // (the bin, a module, an arrangement: not carried here)
    if (drag.kind == hPick && drag.a < (int) ses.library.size()) name = ses.library[(size_t) drag.a].name;
    else if (drag.kind == hColCard)
    {
        const auto& snaps = ses.knobs[(size_t) selKnob].snaps;
        if (drag.a < (int) snaps.size()) if (const auto* sn = ses.findSnap (snaps[(size_t) drag.a])) name = sn->name;
    }
    else if (pile) name = ses.stacks[(size_t) drag.b].name + " (" + String ((int) ses.stacks[(size_t) drag.b].snaps.size()) + ")";
    else if (keptPocket) name = ses.keptKnobs[(size_t) drag.b].name();
    // where it would land: a bar between two cards of the column (a pile goes in whole, after what is there), a ring on a
    // place, or (a chip) a ring on a stack's pocket
    const int at = columnPlaceAt (carryAt), pl = placeAt (carryAt);
    if (at >= 0 && ! keptPocket)
    {
        const int n = (int) ses.knobs[(size_t) selKnob].snaps.size();
        const float y = kColY + (float) ((pile ? n : at) - colScroll) * 74 - 6;
        g.setColour (tone (col::yellow)); g.fillRect (R (kColX, juce::jlimit (kColY - 6.0f, 962.0f, y), 272.0f, 4.0f));
    }
    else if (pl >= 0 && placeTakes (pl))
    {
        g.setColour (tone (col::yellow)); g.drawRoundedRectangle (placeRect (pl).expanded (4.0f), 7.0f, 3.0f);
    }
    else if (const auto* w = wellAt (carryAt); w != nullptr && (w->kind == 1 || w->kind == 5) && (drag.kind == hPick || drag.kind == hColCard))
    {
        g.setColour (tone (col::yellow)); g.drawRoundedRectangle (R (w->r.getX(), w->r.getY(), w->r.getWidth(), 120.0f).expanded (4.0f), 7.0f, 3.0f);
    }
    // the thing itself, by its name, at the pointer
    const float w = std::ceil (tw (fLegend, name)) + 16;
    plate (g, juce::jlimit (10.0f, kW - w - 10.0f, carryAt.x + 10), std::max (60.0f, carryAt.y - 32), name, fLegend, 8, col::yellow, col::black, 24);
}

void Screen::putKept (int index, int k)
{
    const String name = index >= 0 && index < (int) ses.keptKnobs.size() ? ses.keptKnobs[(size_t) index].name() : String();
    switch (ses.knobPut (index, k))
    {
        case Session::putDone:    recNote = name + " IS ON KNOB " + String (k + 1); break;
        case Session::putHolds:   recNote = "KNOB " + String (k + 1) + " HOLDS SOMETHING: CLEAR IT FIRST (KEEP IT, IF IT IS WORTH KEEPING)"; break;
        case Session::putNothing: recNote = "NOTHING OF " + name + (index >= 0 && index < (int) ses.keptKnobs.size() && ! ses.keptKnobs[(size_t) index].stack ? " TO PUT: IT IS TIED TO NO KNOB OF THIS RIG" : " IS LEFT HERE: ITS SNAPSHOTS ARE GONE FROM THE LIBRARY"); break;
        default:                  recNote = "A MACRO KNOB GOES ON 1 TO 4, A STACK KNOB ON 5 TO 12, 14 OR 15"; break;
    }
    recNoteAt = juce::Time::getMillisecondCounter();
}

// A snapshot was pressed to be heard, beside the knob. If nothing will be heard, the bar says why: it has no sound kept, or
// the headphones have nowhere to play (no outputs 3 + 4, and PHONES is out).
void Screen::heardSays (juce::int64 id, bool has)
{
    const auto* sn = ses.findSnap (id);
    if (! has) recNote = "NO SOUND WAS KEPT WITH " + (sn != nullptr ? sn->name : String ("IT")) + " (TAKEN WITH THE CLOCK STOPPED, OR ONE OF THE RIG'S OWN): NOTHING TO HEAR";
    else if (! eng.cueIsHeard()) recNote = "IT PLAYS IN THE HEADPHONES, AND THEY HAVE NOWHERE TO PLAY. PRESS PHONES (OR H): THE SET LEFT, THE SNAPSHOT RIGHT. OR TICK OUTPUTS 3 + 4 IN SETUP";
    else return;
    recNoteAt = juce::Time::getMillisecondCounter();
    dirty (R (8, 12, 1712, 52));
}

// What was carried is let go: between two cards of the column, on a place, or nowhere.
void Screen::drop (juce::Point<float> p)
{
    const int at = columnPlaceAt (p), pl = placeAt (p);
    auto say = [this] (const String& t) { recNote = t; recNoteAt = juce::Time::getMillisecondCounter(); };
    // (a chip let go over a stack's pocket in the rack goes into that stack, with its parts: a reference, as in the library)
    auto intoPocket = [&] (juce::int64 id, juce::uint32 keys)
    {
        const auto* w = wellAt (p);
        const auto* sn = ses.findSnap (id);
        if (w == nullptr || sn == nullptr || (w->kind != 1 && w->kind != 5)) return false;
        const int index = w->kind == 5 ? ses.rackNew() : w->index;
        if (index >= 0 && index < (int) ses.stacks.size() && ses.rackAdd (index, id, keys)) say (sn->name + " IS IN " + ses.stacks[(size_t) index].name);
        else say ("THAT STACK IS THE WEB LIBRARY'S: IT IS CHANGED THERE");
        return true;
    };
    if (page == pArrange)
    {
        // the arrangement's page: a chip let go over the arrangement is a part at its end, with the parts lit on its card (over a
        // stack's pocket: into that stack); a stack's pile let go there is a part for each of its chips, in its order, each with
        // the parts it takes in the stack
        const bool over = arrArea().contains (p);
        if (drag.kind == hPick && drag.a < (int) ses.library.size())
        {
            const auto id = ses.library[(size_t) drag.a].id;
            const auto parts = id == handId ? handParts : Session::kAllLanes;
            if (! over) intoPocket (id, parts);
            else if (ses.arrAdd (id, -1, parts)) selPart = (int) ses.arrangement.size() - 1;
            else say ("NO ROOM IN THE ARRANGEMENT FOR ANOTHER PART");
        }
        else if (drag.kind == hWell && drag.a == 1 && drag.b < (int) ses.stacks.size() && over)
        {
            const Stack st = ses.stacks[(size_t) drag.b];
            int added = 0;
            for (size_t i = 0; i < st.snaps.size(); ++i) if (ses.arrAdd (st.snaps[i], -1, st.keysAt (i) & Session::kAllLanes)) ++added;
            if (added > 0) selPart = (int) ses.arrangement.size() - added;
            say (st.snaps.empty() ? "NOTHING IN " + st.name + " TO MAKE PARTS OF"
                 : added == 0 ? String ("NO ROOM IN THE ARRANGEMENT FOR ANOTHER PART")
                 : st.name + ": " + String (added) + (added == 1 ? " PART" : " PARTS") + " AT THE END OF THE ARRANGEMENT, IN ITS ORDER" + (added < (int) st.snaps.size() ? " (NO ROOM FOR THE REST)" : ""));
        }
        return;
    }
    const char* const noRoom = "NO ROOM ON THAT KNOB";
    const char* const macroPlace = "1 TO 4 ARE MACRO KNOBS (LEARN). SNAPSHOTS GO ON A STACK KNOB";
    switch (drag.kind)
    {
        case hPick:
        {
            // a snapshot of the list, with the parts chosen on its card: into the column where it was let go, or onto a place's stack
            if (drag.a >= (int) ses.library.size()) break;
            const auto id = ses.library[(size_t) drag.a].id;
            const auto parts = id == handId ? handParts : Session::kAllLanes;
            if (at >= 0) { if (! ses.stackInsert (selKnob, at, id, parts)) say (noRoom); }
            else if (pl >= 0 && ! Session::isCorner (pl))
            {
                if (! Session::isStack (pl)) say (macroPlace);
                else if (! ses.stackAdd (pl, id, parts)) say (noRoom);
            }
            else intoPocket (id, parts);
            break;
        }
        case hColCard:
        {
            // a card of the column: to another place in it; onto another knob (a copy: the column keeps its own); off it, taken out
            const auto& snaps = ses.knobs[(size_t) selKnob].snaps;
            if (drag.a >= (int) snaps.size()) break;
            const auto id = snaps[(size_t) drag.a];
            const auto parts = ses.knobs[(size_t) selKnob].keysAt ((size_t) drag.a);      // (its keys go with it, N among them)
            if (at >= 0) { const int to = at > drag.a ? at - 1 : at; ses.stackMove (selKnob, drag.a, to); }
            else if (pl >= 0)
            {
                if (Session::isStack (pl) && pl != selKnob && ! ses.stackAdd (pl, id, parts)) say (noRoom);
            }
            else if (intoPocket (id, parts)) {}        // (onto a stack's pocket: into that stack too; the knob keeps its own)
            else { ses.stackDrop (selKnob, drag.a); colSel = -1; }
            break;
        }
        case hWell:
        {
            // a pocket of the rack. A stack's pile: onto a place's stack knob, or onto the chosen knob's column, whole, after
            // what is there (a copy: the stack stays in its pocket). A kept knob: onto a place of its kind.
            if (drag.a == 1 && drag.b < (int) ses.stacks.size())
            {
                const String name = ses.stacks[(size_t) drag.b].name;
                const int to = pl >= 0 && ! Session::isCorner (pl) ? pl : at >= 0 ? selKnob : -1;
                if (to < 0) break;
                if (! Session::isStack (to)) say (macroPlace);
                else if (ses.stacks[(size_t) drag.b].snaps.empty()) say ("NOTHING IN " + name + " TO PUT ON A KNOB");
                else if (! ses.stackOnto (drag.b, to)) say ("NO ROOM ON THAT KNOB FOR ALL OF THEM");
                else { say (name + " IS ON KNOB " + String (to + 1) + " (A COPY: THE STACK STAYS IN ITS POCKET)"); selKnob = to; colSel = -1; }
            }
            else if (drag.a == 2 && pl >= 0 && ! Session::isCorner (pl)) { putKept (drag.b, pl); if (ses.knobHolds (pl)) selKnob = pl; }
            break;
        }
        default: break;
    }
}

// The column: the chosen stack knob's stack as it is, in its order, beside the library it is built from. Each snapshot
// is a card with a key for every part of the rig: the eleven lanes, the returns (R), the drum bus (B), the key (K). Lit, this
// snapshot takes that part; put out, the part stays as it is when the knob comes to this snapshot, is pressed onto it,
// or plays it in the headphones. The card's number takes it off the stack; it is moved by carrying it; the
// card itself plays it in the headphones with the parts it takes. All of it is the session's own: no network is asked.
void Screen::drawColumn (G& g)
{
    const int k = selKnob;
    const auto& kn = ses.knobs[(size_t) k];
    const std::vector<juce::int64>& snaps = kn.snaps;
    auto keysAt = [&] (size_t i) { return kn.keysAt (i); };       // (its parts, and N)
    const int n = (int) snaps.size();
    const float x = kColX;
    // its head: whose stack this is, and how many it holds
    {
        const R hr (x, kColHead, 272, 26);
        const String num (k + 1);
        const float wTag = std::max (30.0f, std::ceil (tw (fLegend, num)) + 8.0f);
        if (vis (g, hr))
        {
            tag (g, R (x, kColHead, wTag, 26), num);
            const String name = kn.name.isEmpty() ? String ("UNNAMED") : kn.name;
            const float wn = plate (g, x + wTag, kColHead, name, fLegend, 8, col::white, col::black);
            const String count = String (n) + (n == 1 ? " CHIP" : " CHIPS");
            if (x + wTag + wn + 4 + std::ceil (tw (fNote, count)) + 12 <= x + 272) plate (g, x + 272 - (std::ceil (tw (fNote, count)) + 12), kColHead + 2.0f, count, fNote, 6, col::white, col::black);
        }
    }
    const int rows = kColRows;
    most.col = std::max (0, n - rows);
    colScroll = juce::jlimit (0, most.col, colScroll);
    colSel = juce::jlimit (-1, n - 1, colSel);
    // its keys: what the hand does in this pile (the same words as in a stack's row in the library)
    {
        float kx = x;
        const bool handIn = ses.findSnap (handId) != nullptr, in = colSel >= 0;
        auto key = [&] (int a, const String& t, Colour c, bool can)
        {
            const R r (kx, kColHead + 33.0f, std::ceil (tw (fNote, t)) + 16.0f, 24.0f);
            if (vis (g, r))
            {
                if (can) { window (g, r, c); onGlass (g, t, r, fNote, c, col::black); }
                else { g.setColour (tone (col::sea)); g.drawRect (r.reduced (0.5f), 1.0f); textRaw (g, t, r, fNote, tone (col::black).withAlpha (0.45f)); }
            }
            if (can) hit (r, hAct, a, 0);
            kx += r.getWidth() + 4.0f;
        };
        key (aRowAdd, "ADD", col::yellow, handIn);
        key (aRowOut, "TAKE OUT", col::white, in);
        key (aRowEarlier, "EARLIER", col::white, in && colSel > 0);
        key (aRowLater, "LATER", col::white, in && colSel + 1 < n);
    }
    if (n == 0 && vis (g, R (x, kColY + 2.0f, 272, 60)))
    {
        g.setColour (tone (col::black)); g.setFont (fLbl);
        g.drawFittedText ("NO CHIPS ON IT. CARRY ONE HERE FROM THE RACK AT THE RIGHT, OR PRESS A CHIP THERE AND THEN ADD, TO BEGIN ITS PILE.", juce::Rectangle<int> ((int) x + 6, (int) kColY + 6, 260, 56), juce::Justification::topLeft, 3, 1.0f);
    }
    static const char* const partName[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "R", "B", "K", "N", "M" };
    for (int row = 0; row < rows; ++row)
    {
        const int i = colScroll + row;
        if (i >= n) break;
        const R r (x, kColY + (float) row * 74, 272, 66);
        const auto* sn = ses.findSnap (snaps[(size_t) i]);
        hit (r, hColCard, i, 0);
        nameAt (1, snaps[(size_t) i], -1, {}, chipNameRect (r, i + 1));
        const float x0 = r.getX() + 6, y0 = r.getY() + 6;
        const String num (i + 1);
        const float wTag = std::max (30.0f, std::ceil (tw (fLegend, num)) + 8.0f);
        const juce::uint32 lanes = keysAt ((size_t) i);
        if (vis (g, r))
        {
            const bool heard = sn != nullptr && ses.cueing() == sn->id;
            card (g, r, heard || i == colSel);
            tag (g, R (x0, y0, wTag, 24), num);
            // (it is moved by carrying it: pressed and taken to between two others; off the column, it is taken out)
            String name = sn != nullptr ? sn->name : String ("GONE");
            while (name.length() > 3 && x0 + wTag + std::ceil (tw (fLegend, name)) + 16 > r.getRight() - 6) name = name.dropLastCharacters (1);
            plate (g, x0 + wTag, y0, name, fLegend, 8, heard ? col::green : col::white, heard ? col::lit : col::black, 24);
        }
        hit (R (x0, y0, wTag, 24), hColOut, i, 0);
        // its keys: black with a lit figure while this snapshot takes the part; an empty frame when the part is put out.
        // The last is N, its notes (as on a part of the arrangement): lit, the knob coming to this snapshot brings them.
        for (int p = 0; p < kLanes + 5; ++p)
        {
            const R kr = stackKeyRect (x0, y0 + 30, p);
            if (vis (g, kr)) stackKey (g, kr, partName[p], ((lanes >> p) & 1u) != 0, p >= kLanes + 3);
            hit (kr, hColKey, i, p);
        }
    }
    if (n > rows && vis (g, R (x, kColY + (float) rows * 74 - 4.0f, 272, 40)))
        text (g, String (colScroll + 1) + " TO " + String (std::min (n, colScroll + rows)) + " OF " + String (n) + " (THE WHEEL, OVER THE COLUMN)", R (x, kColY + (float) rows * 74 - 2.0f, 272, 16), fNote, col::black, juce::Justification::centredLeft);
}

// ------------------------------------------------------------------------------------ the shelf
// The things kept, by kind: stacks, macro knobs, stack knobs, modules, arrangements. One drawing and one set of keys for all
// of them, wherever they are shown.
int Screen::shelfCount (int kind) const
{
    if (kind == skStacks) return (int) ses.stacks.size();
    if (kind == skModules) return (int) ses.modules.size();
    if (kind == skArrs) return (int) ses.arrs.size();
    int n = 0;
    for (auto& k : ses.keptKnobs) n += k.stack == (kind == skStackKnobs) ? 1 : 0;
    return n;
}

int Screen::shelfItem (int kind, int i) const
{
    if (i < 0 || i >= shelfCount (kind)) return -1;
    if (kind != skMacroKnobs && kind != skStackKnobs) return i;
    for (int q = 0, n = 0; q < (int) ses.keptKnobs.size(); ++q)
        if (ses.keptKnobs[(size_t) q].stack == (kind == skStackKnobs)) { if (n == i) return q; ++n; }
    return -1;
}

String Screen::shelfName (int kind, int i) const
{
    const int at = shelfItem (kind, i);
    if (at < 0) return {};
    return kind == skStacks ? ses.stacks[(size_t) at].name : kind == skModules ? ses.modules[(size_t) at].name : kind == skArrs ? ses.arrs[(size_t) at].name : ses.keptKnobs[(size_t) at].name();
}

int Screen::shelfHere (int kind) const { return kind == skModules ? ses.moduleIndex() : kind == skArrs ? ses.arrsIndex() : -1; }

// (a knob is made in the designer and kept from there: none is made or copied on the shelf)
bool Screen::shelfHas (int kind, int key) const { return (kind != skMacroKnobs && kind != skStackKnobs) || key >= 2; }

bool Screen::shelfCan (int kind, int key) const
{
    if (! shelfHas (kind, key)) return false;
    const int n = shelfCount (kind), sel = shelfSel[(size_t) kind], at = shelfItem (kind, sel), here = shelfHere (kind);
    if (key == 0) return true;                                              // NEW
    if (key == 1) return kind == skStacks ? at >= 0 : n > 0;                // COPY: the chosen stack; the module in the chassis; the arrangement on the page
    if (at < 0) return false;
    if (key == 2) return here < 0 || sel != here;                           // LOAD: not the one that is on the bench already
    if (key == 3) return kind != skStacks || ses.rackCanChange (at);        // RENAME: not a song of the web library's
    return kind == skModules ? sel != here : kind == skArrs ? n > 1 : true; // DELETE: not the module in the chassis; not the only arrangement
}

void Screen::shelfDo (int kind, int key)
{
    auto says = [this] (const String& t) { recNote = t; recNoteAt = juce::Time::getMillisecondCounter(); };
    if (! shelfCan (kind, key)) return;
    int& sel = shelfSel[(size_t) kind];
    const int at = shelfItem (kind, sel);
    if (key == 3)
    {
        if (! shelfNameRect.isEmpty())
            beginEdit (shelfNameRect, shelfName (kind, sel), [this, kind, at] (String t)
            {
                if (kind == skStacks) ses.rackRename (at, t); else if (kind == skModules) ses.moduleRename (at, t); else if (kind == skArrs) ses.arrsRename (at, t); else ses.keptRename (at, t);
            });
        return;
    }
    if (kind == skMacroKnobs || kind == skStackKnobs)
    {
        const String nm = shelfName (kind, sel);
        const int k = ses.keptOntoFree (at);
        says (k >= 0 ? nm + " IS ON KNOB " + String (k + 1) : String (kind == skStackKnobs ? "NO STACK KNOB IS FREE FOR IT (OR ITS SNAPSHOTS HAVE GONE): CLEAR ONE IN MACROS FIRST" : "NO MACRO KNOB IS FREE FOR IT (OR IT IS TIED TO NOTHING): CLEAR ONE IN MACROS FIRST"));
    }
    else if (kind == skModules)
    {
        if (key == 0) { sel = ses.moduleNew(); says ("A NEW MODULE, IN THE CHASSIS: " + ses.moduleName()); }
        else if (key == 1) { sel = ses.moduleCopy(); says ("KEPT AGAIN AS " + ses.moduleName() + ": THE CHASSIS HOLDS THE COPY"); }
        else if (ses.moduleLoad (at)) says (ses.moduleName() + " IS IN THE CHASSIS");
    }
    else
    {
        if (key == 0) { const int made = ses.arrsNew(); if (made >= 0) { sel = made; selPart = -1; says ("A NEW ARRANGEMENT, ON THE PAGE: " + ses.arrsName()); } else says ("NOT WHILE IT PRINTS"); }
        else if (key == 1) { sel = ses.arrsCopy(); says ("KEPT AGAIN AS " + ses.arrsName() + ": THE PAGE HOLDS THE COPY"); }
        else if (ses.arrsLoad (at)) { selPart = -1; arrScroll = 0; says (ses.arrsName() + " IS ON THE PAGE" + (page == pArrange ? "" : ": ARRANGE SHOWS IT")); }
        else if (ses.printing()) says ("NOT WHILE IT PRINTS");
    }
}

void Screen::shelfDelete (int kind)
{
    auto says = [this] (const String& t) { recNote = t; recNoteAt = juce::Time::getMillisecondCounter(); };
    int& sel = shelfSel[(size_t) kind];
    const int at = shelfItem (kind, sel);
    if (at < 0 || ! shelfCan (kind, 4)) return;
    const String gone = shelfName (kind, sel);
    bool ok = true;
    if (kind == skModules) ok = ses.moduleRemove (at);
    else if (kind == skArrs) { ok = ses.arrsRemove (at); selPart = -1; }
    else ses.knobForget (at);
    const int n = shelfCount (kind), here = shelfHere (kind);
    sel = n == 0 ? -1 : here >= 0 ? here : juce::jlimit (0, n - 1, sel);
    says (! ok ? (ses.printing() ? String ("NOT WHILE IT PRINTS") : String ("IT COULD NOT BE DELETED")) : gone + " IS DELETED" + (kind == skArrs ? ". ON THE PAGE: " + ses.arrsName() : String()));
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
    // (each a card: its number and name; a key for every part of the rig it takes from its snapshot; how long it is and how it comes in)
    const int rows = 7;
    most.arr = std::max (0, (n - 1) / 4 - (rows - 1));
    arrScroll = juce::jlimit (0, most.arr, arrScroll);
    for (int q = 0; q < rows * 4; ++q)
    {
        const int i = arrScroll * 4 + q;
        if (i >= n) break;
        const auto& p = ses.arrangement[(size_t) i];
        const R r (23.0f + (float) (q % 4) * 282, 184.0f + (float) (q / 4) * 113, 272, 107);
        hit (r, hArrPart, i, 0);
        nameAt (1, ses.arrangement[(size_t) i].snap, -1, {}, chipNameRect (r, i + 1));
        const float x0 = r.getX() + 6, y0 = r.getY() + 6;
        const R br (x0, y0 + 54, 66, 40), mr (x0 + 70, y0 + 54, 96, 40);
        if (i > 0 || true) { hit (br, hArrBars, i, 0); if (i > 0) hit (mr, hArrMorph, i, 0); }
        for (int k = 0; k < kLanes + 4; ++k) hit (R (x0 + (float) k * 17.4f, y0 + 27, 16.0f, 24), hArrKey, i, k);      // (the last: N, its notes)
        hit (R (x0 + 184, y0 + 75, 37, 20), hArrTrans, i, 0); hit (R (x0 + 223, y0 + 75, 37, 20), hArrTrans, i, 1);      // (its IN and its HIT)
        if (! vis (g, r)) continue;
        card (g, r, i == selPart);
        {
            static const char* const partName[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "R", "B", "K", "N" };
            for (int k = 0; k < kLanes + 4; ++k)
            {
                const R kr (x0 + (float) k * 17.4f, y0 + 27, 16.0f, 24);
                if (k == kLanes + 3 ? p.notes : ((p.lanes >> k) & 1u) != 0) { window (g, kr, col::black); text (g, partName[k], kr, fNote, col::lit); }
                else { g.setColour (tone (col::sea)); g.drawRect (kr.reduced (0.5f), 1.0f); textRaw (g, partName[k], kr, fNote, tone (col::black).withAlpha (0.45f)); }
            }
        }
        const String num (i + 1);
        const float wTag = std::max (30.0f, std::ceil (tw (fLegend, num)) + 8.0f);
        tag (g, R (x0, y0, wTag, 24), num);
        const auto* sn = ses.findSnap (p.snap);
        const bool on = i == playing;
        plate (g, x0 + wTag, y0, fitted (sn != nullptr ? sn->name : String ("GONE"), fLegend, 150.0f), fLegend, 8, on ? col::green : i == ses.arrEdit ? col::amber : col::white, col::black, 24);
        const String from = "BAR " + String (ses.arrBarsBefore (i) + 1);
        plate (g, r.getRight() - 6 - (std::ceil (tw (fNote, from)) + 12), y0, from, fNote, 6, col::white, col::black, 24);
        readout (g, br, "BARS", String (p.bars));
        // (a morph longer than the part is the whole of the part)
        const bool whole = glideBarsFor ((float) p.morph / 11.0f) > (double) p.bars;
        readout (g, mr, i == 0 ? "COMES IN" : "MORPH", i == 0 ? String ("AT ONCE") : whole ? String ("ALL OF IT") : Session::morphName (p.morph));
        // (when it starts and how long it lasts, on one line: the line under it is the two plates')
        text (g, hms ((double) ses.arrBarsBefore (i) * 4.0 * 60.0 / tempo) + " FOR " + hms ((double) p.bars * 4.0 * 60.0 / tempo), R (x0 + 168, y0 + 54, 92, 20), fLbl, col::black, juce::Justification::centredRight);
        // TRANSITION into this part: IN, a lead-in that ends on its bar line; HIT, a hit that lands with it. Amber: with what the
        // bar's plates say; green: with a file of the part's own (dropped on the plate).
        plate (g, x0 + 186, y0 + 76, "IN", fLbl, 9, p.transIn == 2 ? col::green : p.transIn == 1 ? col::amber : col::putty, col::black, 18);
        plate (g, x0 + 225, y0 + 76, "HIT", fLbl, 6, p.transHit == 2 ? col::green : p.transHit == 1 ? col::amber : col::putty, col::black, 18);
    }
    // The rack, beside the parts: the same one the library is. The bin's chips, or an open stack's, are on its bench to take
    // parts from (ADD on a chip, or the chip carried onto the arrangement); a stack's pile carried onto the arrangement is a
    // part for each of its chips, in its order; ARRANGEMENTS is the shelf of those kept, of which the one on the page is one.
    rp = RackPlace { 1151.0f, 554.0f, 78.0f, 4, 2, false };
    rackTidy();
    drawChipRack (g);
    drawRackBench (g);
    const R strip (23, 980, 1682, 110);
    panel (g, strip, col::putty);
    const float x0 = strip.getX() + 5, y0 = strip.getY() + 5, ky = y0 + 40;
    const bool pr = ses.printing(), play = ses.arrPlaying();
    const juce::ScopedValueSetter<bool> onOneLine (oneLine, true);      // (no room under these keys for a second line)
    {
        // (which of the arrangements kept this is)
        const float wt = title (g, x0, y0, "ARRANGEMENT");
        String nm = ses.arrsName();
        while (nm.length() > 3 && std::ceil (tw (fNote, nm)) + 12.0f > 820.0f - wt) nm = nm.dropLastCharacters (1);
        if (nm.isNotEmpty()) plate (g, x0 + wt + 2, y0, nm, fNote, 6, col::white, col::black);
        nameAt (6, 0, -1, ses.arrAtId, R (x0 + wt + 2, y0, 220.0f, 24.0f));
    }
    actKey (g, aMain, x0, ky, 64, "MAIN", col::white, 5);
    actKey (g, aLibrary, x0 + 66, ky, 64, "LIBRARY", col::white, 5);
    actKey (g, aMacros, x0 + 132, ky, 64, "MACROS", col::white, 5);
    actKey (g, aArrange, x0 + 198, ky, 64, "ARRANGE", col::green, 5, true, true);
    actKey (g, aRename, x0 + 286, ky, 64, "RENAME", col::yellow, 5, namedNow().what != 0);
    const bool editing = ses.arrEdit >= 0;
    actKey (g, aArrPlay, x0 + 368, ky, 64, play ? "STOP" : "PLAY", play ? col::green : col::putty, 5, n > 0, play);
    actKey (g, aArrHere, x0 + 434, ky, 64, "FROM HERE", col::yellow, 5, selPart >= 0);
    actKey (g, aArrEdit, x0 + 500, ky, 64, "EDIT", editing ? col::amber : col::yellow, 5, selPart >= 0 && ! pr, editing);
    // (the words a stack's row and a knob's pile have: the chosen part earlier, later, or taken out of the arrangement)
    actKey (g, aArrEarlier, x0 + 582, ky, 64, "EARLIER", col::yellow, 5, selPart > 0);
    actKey (g, aArrLater, x0 + 648, ky, 64, "LATER", col::yellow, 5, selPart >= 0 && selPart < n - 1);
    actKey (g, aArrRemove, x0 + 714, ky, 64, "TAKE OUT", col::yellow, 5, selPart >= 0);
    actKey (g, aArrClear, x0 + 796, ky, 64, "CLEAR", col::red, 5, n > 0);
    actKey (g, aArrStack, x0 + 878, ky, 64, "TO STACK", col::yellow, 5, n > 0);
    actKey (g, aArrPrint, x0 + 944, ky, 64, pr ? "STOP" : "PRINT", pr ? col::green : col::yellow, 5, n > 0, pr);
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
        else if (editing) now = "PART " + String (ses.arrEdit + 1) + " IS BEING EDITED IN THE MAIN VIEW: KEEP OR DROP IT THERE (EDIT GOES BACK TO IT).";
        else now = ses.printNote;
        g.setColour (tone (col::black)); g.setFont (fLegend);
        g.drawFittedText (now, juce::Rectangle<int> ((int) x0 + 1028, (int) y0, 642, 24), juce::Justification::centredLeft, 2, 0.7f);
        const String help = "PRESS A CHIP IN THE RACK TO HEAR IT; ITS ADD PUTS IT AFTER THE CHOSEN PART, WITH THE PARTS LIT ON IT. CARRIED ONTO THE ARRANGEMENT IT GOES AT THE END; A STACK'S PILE CARRIED HERE IS A PART FOR EACH OF ITS CHIPS. "
                            "A PART'S KEYS: WHAT IT TAKES OF ITS CHIP. DRAG ITS BARS, AND ITS MORPH: HOW LONG THE WAY IN. EDIT: THE CHOSEN PART IN THE MAIN VIEW, KEPT AS A NEW CHIP IN ITS PLACE. "
                            "TO STACK: THE PARTS AS A STACK, FOR A KNOB. PRINT: THE TRACK AND ITS STEMS AS FILES, WHERE TAKES GO.";
        g.setFont (fLbl);
        g.drawFittedText (help, juce::Rectangle<int> ((int) x0 + 1028, (int) y0 + 28, 642, 68), juce::Justification::topLeft, 6, 0.75f);
    }
    drawCarry (g);          // (what is in the hand, over everything)
}

String Screen::arrangeSig() const
{
    String s;
    s << ses.arrEdit << "|" << ses.arrPart() << "|" << (ses.arrPlaying() ? (int) std::floor (ses.arrPos()) : -9) << "|" << (ses.printing() ? juce::roundToInt (ses.printDone() * 100.0f) : -1) << "|" << ses.printNote << "|" << (int) ses.arrangement.size();
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
        case aSamplePlain:
            if (cfgOpen && cfg.kind == 0)
            {
                const auto told = ses.samplePlain (cfg.lane);
                if (told.isNotEmpty()) { cfgNote = told; recNote = told; recNoteAt = juce::Time::getMillisecondCounter(); cfgStale = true; }
            }
            repaint();
            break;
        case aResample:
            if (cfgOpen && cfg.kind == 0)
            {
                const auto told = ses.resampleInto (cfg.lane);
                if (told.isNotEmpty()) { cfgNote = told; recNote = told; recNoteAt = juce::Time::getMillisecondCounter(); cfgStale = true; loadOpen = false; }
                if (onLook) onLook();
            }
            repaint();
            break;
        case aLoadUp:   if (loadDir.getParentDirectory() != loadDir) { loadDir = loadDir.getParentDirectory(); loadScroll = 0; readLoad(); } repaint(); break;
        case aCfgClose: cfgOpen = false; break;
        case aClipReset: eng.clipL = false; eng.clipR = false; break;
        case aSnapshot: selSnap = ses.take(); break;
        case aPhones:   eng.setPhones (! eng.phones()); repaint(); break;
        case aTransFire:  ses.transFire(); repaint(); break;
        case aTransArm:   ses.transArm(); repaint(); break;
        case aTransBars:  ses.transStep (Session::tBars); repaint(); break;
        case aTransLevel: ses.transStep (Session::tLevel); repaint(); break;
        case aFinish:     ses.finishStep(); repaint(); break;
        case aTransLead:  ses.transStep (Session::tLead); repaint(); break;
        case aTransHit:   ses.transStep (Session::tHit); repaint(); break;
        case aTransDub:   ses.transStep (Session::tDub); repaint(); break;
        case aTransSpace: ses.transStep (Session::tSpace); repaint(); break;
        case aNotes:    ses.setRecallNotes (! ses.recallNotes); repaint(); break;
        case aPockets:  setPage (page == pPockets ? pMain : pPockets); break;
        case aArrange:  setPage (page == pArrange ? pMain : pArrange); break;
        case aArrPlay:  if (ses.arrPlaying()) ses.arrStop(); else ses.arrPlay (0); break;
        case aArrHere:  ses.arrPlay (std::max (0, selPart)); break;
        case aArrEdit:
            // (the part chosen; the one already being edited: back to it)
            if (selPart >= 0 && (ses.arrEdit == selPart || ses.arrEditBegin (selPart))) setPage (pMain);
            break;
        case aEditKeep:
        {
            const int part = ses.arrEdit;
            const auto id = ses.arrEditKeep();
            if (const auto* sn = ses.findSnap (id))
            {
                String lit;
                static const char* const partName[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "R", "B", "K" };
                for (int k = 0; k < kLanes + 3; ++k) if ((ses.arrEditLit >> k) & 1u) lit << (lit.isEmpty() ? "" : " ") << partName[k];
                ses.printNote = "PART " + String (part + 1) + " IS NOW " + sn->name + ", A NEW SNAPSHOT IN THE LIBRARY." + (lit.isNotEmpty() ? "  KEYS LIT FOR WHAT WAS CHANGED: " + lit + "." : String());
            }
            selPart = part;
            setPage (pArrange);
            break;
        }
        case aEditDrop: { const int part = ses.arrEdit; ses.arrEditDrop(); ses.printNote = "NOTHING KEPT. THE RIG IS AS YOU LEFT IT."; selPart = part; setPage (pArrange); break; }
        case aArrStack:
        {
            const int at = ses.arrToStack();
            if (at >= 0)
            {
                const auto& st = ses.stacks[(size_t) at];
                const int open = ses.arrOpenParts();
                ses.printNote = "KEPT AS THE STACK " + st.name + ": " + String ((int) st.snaps.size()) + (st.snaps.size() == 1 ? " SNAPSHOT" : " SNAPSHOTS") + " IN ORDER, IN THE MACRO DESIGNER'S RACK."
                                + (open > 0 ? "  " + String (open) + (open == 1 ? " PART HAS" : " PARTS HAVE") + " A KEY OUT: ON A KNOB, AS HERE, " + (open == 1 ? "IT LEANS" : "THEY LEAN") + " ON WHAT CAME BEFORE." : String());
            }
            break;
        }
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
        case aFetch:    if (online != nullptr && ! online->loading()) { loadWas = online->loadLine(); online->askLoad(); loadAsked = page != pLibrary; loadSeen = false; } break;
        case aRecallCue:
            // the snapshot in hand (or, with none, the chosen one) to the set, with the parts lit on its card
            if (const auto id = ses.findSnap (handId) != nullptr ? handId : selSnap; ses.findSnap (id) != nullptr)
            {
                Session::Scope sc = id == handId ? Session::scopeOf (handParts) : Session::Scope();
                sc.notes = ses.recallNotes;
                selSnap = id; ses.recall (id, sc); ses.cue (0);
                handParts = Session::kAllLanes;         // (the next recall takes everything again)
                ses.cueWith (0, {});                    // (and the next one heard is whole)
            }
            break;
        case aLibrary:  setPage (pLibrary); break;
        // the rack: the chip in hand, and what is open
        case aAddTo:    addTo = ! addTo && ses.findSnap (handId) != nullptr; break;
        case aRowAdd:
            if (page == pMacros)
            {
                // (the designer: the chip in hand onto the chosen stack knob's pile, last, with the parts lit on its card)
                if (! Session::isStack (selKnob) || ses.findSnap (handId) == nullptr) break;
                if (ses.stackAdd (selKnob, handId, handParts)) { colSel = (int) ses.knobs[(size_t) selKnob].snaps.size() - 1; colScroll = 9999; }
                else rackSay ("NO ROOM ON THAT KNOB");
                break;
            }
            if (const auto pile = rowAt(); pile.what != 0 && ses.findSnap (handId) != nullptr)
            {
                const String name = ses.findSnap (handId)->name;
                if (ses.pileInsert (pile, -1, handId, handParts)) { rackSay (name + " IS IN " + ses.pileName (pile) + ", LAST"); rowScroll = 9999; }
                else rackSay (ses.pileCan (pile) ? "NO ROOM IN THAT PILE FOR ANOTHER" : "THAT STACK IS THE WEB LIBRARY'S: IT IS CHANGED THERE");
            }
            break;
        case aRowOut:
            if (page == pMacros)
            {
                if (Session::isStack (selKnob) && colSel >= 0 && colSel < (int) ses.knobs[(size_t) selKnob].snaps.size())
                { ses.stackDrop (selKnob, colSel); colSel = std::min (colSel, (int) ses.knobs[(size_t) selKnob].snaps.size() - 1); }
                break;
            }
            if (const auto at = tiesAt(); at.what != 0)
            {
                // (the tie pressed on the open macro knob: that knob of the rig is no longer moved by it)
                const auto ties = ses.ties (at);
                if (tieSel < 0 || tieSel >= (int) ties.size()) break;
                String where, label; const char* what = nullptr;
                describe (ties[(size_t) tieSel].ctl, where, label, what);
                if (ses.tieDrop (at, tieSel)) { rackSay ((where.isNotEmpty() ? where + " " : String()) + label + " IS NO LONGER TIED TO IT"); tieSel = -1; }
                else rackSay ("NOT WHILE THE KNOB IS BEING TAUGHT");
                break;
            }
            if (const auto pile = rowAt(); pile.what != 0)
            {
                const auto snaps = ses.pileSnaps (pile);
                if (rowSel < 0 || rowSel >= (int) snaps.size() || ! ses.pileCan (pile)) break;
                const auto* sn = ses.findSnap (snaps[(size_t) rowSel]);
                const String name = sn != nullptr ? sn->name : String ("IT");
                ses.pileDrop (pile, rowSel);
                rackSay (name + " IS OUT OF " + ses.pileName (pile) + ". IT IS STILL IN THE BIN");
                rowSel = std::min (rowSel, (int) snaps.size() - 2);
            }
            break;
        case aRowEarlier: case aRowLater:
            if (page == pMacros)
            {
                const int to = colSel + (a == aRowEarlier ? -1 : 1);
                if (Session::isStack (selKnob) && colSel >= 0 && to >= 0 && to < (int) ses.knobs[(size_t) selKnob].snaps.size())
                {
                    ses.stackMove (selKnob, colSel, to); colSel = to;
                    if (to < colScroll) colScroll = to; else if (to >= colScroll + kColRows) colScroll = to - kColRows + 1;
                }
                break;
            }
            if (const auto pile = rowAt(); pile.what != 0 && rowSel >= 0 && ses.pileCan (pile))
            {
                const int to = rowSel + (a == aRowEarlier ? -1 : 1);
                if (to >= 0 && to < (int) ses.pileSnaps (pile).size()) { ses.pileMove (pile, rowSel, to); rowSel = to; }
            }
            break;
        case aOpenNew:    rackOpenDo (0); break;
        case aOpenCopy:   rackOpenDo (1); break;
        case aOpenLoad:   rackOpenDo (2); break;
        case aMacros:   setPage (pMacros); break;
        case aModules:
            // (LOAD, on the snapshot module's own panel: the designer, its rack on the MODULES shelf with the one in the chassis open)
            setPage (pMacros);
            rackShelf = 2; inStack = {}; openModKnob = -1; libScroll = 0;
            if (const int at = ses.moduleIndex(); at >= 0 && at < (int) ses.modules.size()) { openMod = ses.modules[(size_t) at].id; rackFirst = std::max (0, at - 1); nameIs (5, 0, -1, openMod); }
            break;
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
        case aRename:       renameNamed(); return;        // (the thing last pressed that has a name)
        case aRenameKnob:   nameIs (2, 0, selKnob); renameNamed(); return;
        case aLearn:
            // LEARN on the knob that is being taught keeps it; on another, that one is kept and this one begun
            if (ses.learning == selKnob) ses.learnKeep();
            else { ses.learnBegin (selKnob); setPage (pMain); }
            break;
        case aLearnKeep: ses.learnKeep(); break;
        case aLearnDrop: ses.learnDrop(); break;
        case aClearKnob: break;         // (held: see the timer)
        case aStackCue:  ses.setStackCue (! ses.stackCue); break;
        default: break;
    }
    repaint();
}

void Screen::stepTap (int i)
{
    // An unlit step lights, at velocity 75, a whole step of gate and the key's note, and is selected. A lit step is
    // selected first; a second tap puts it out, and it forgets everything it held.
    const int l = ses.activeLane;
    Pattern p = ses.pattern (l);
    if (i >= p.length) return;
    auto& s = p.steps[(size_t) i];
    const int oct = lanes()[(size_t) l].octave;
    if (! s.on) { ses.lightStep (l, s); ses.selStep = i; ses.setPattern (l, p); }
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
    if (kbOn) { kbOn = false; repaint(); }                                  // (the mouse has it: the keyboard's mark is put away)
    if (h == nullptr) return;
    downOn (*h, p);
}

// The hand comes down on a control (the mouse's, or a key's where the keyboard's mark is).
void Screen::downOn (Hit on, juce::Point<float> p)
{
    const Hit* const h = &on;
    drag.kind = h->kind; drag.a = h->a; drag.b = h->b; drag.page = (int) page; drag.start = p; drag.t0 = juce::Time::getMillisecondCounter();
    switch (h->kind)
    {
        case hKnob: case hRead: drag.v0 = ses.value (h->a); break;
        case hPocket:   drag.v0 = ses.value (cx::pk (h->a, 0)); drag.g0 = ses.value (cx::pk (h->a, 1)); break;
        case hSnapKnob: drag.v0 = ses.value (cx::kSnap + h->a); if (page == pMacros) { selKnob = h->a; nameIs (2, 0, h->a); } break;
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
            if (heldAct (h->a)) { holdDirty = h->r.expanded (10.0f); holdRect = h->r; }        // (the stage lights the key at that place: none is there for a plate, which fills on the sheet)
            if (heldAct (h->a) || (h->a == aRec && recorder != nullptr && recorder->recording())) break;       // these are held
            act (h->a);
            drag.fired = true;                  // (the press that starts a take is not also the hold that ends it)
            if (h->a == aRec) repaint();
            return;
        case hCard:
        {
            // the library: a snapshot pressed is the chosen one and the one in hand (its card shows the keys for its parts), and is
            // heard in the headphones with the parts that are lit; pressed again, it stops. RECALL is what takes it to the set.
            const auto id = ses.library[(size_t) h->a].id;
            selSnap = id; rowSel = -1; nameIs (1, id);
            if (id != handId) { handId = id; handParts = Session::kAllLanes; }
            if (ses.cueing() == id) ses.cue (0);
            else { if (handParts == Session::kAllLanes) ses.cueWith (0, Session::Scope()); else ses.cueWith (id, Session::scopeOf (handParts)); heardSays (id, ses.cue (id)); }
            break;
        }
        case hKnobCard: if (selKnob != h->a) { colSel = -1; colScroll = 0; } selKnob = h->a; nameIs (2, 0, h->a); break;       // (a place chosen: its knob is on the bench)
        case hSlot:
        {
            // one of the snapshots on a stack knob's card, pressed: the place is chosen (its stack is in the column, with this one in
            // view) and the snapshot is heard with the parts it takes there; pressed again, it stops. (It is taken off in the column.)
            selKnob = h->a;
            const auto& kn = ses.knobs[(size_t) selKnob];
            if (h->b < (int) kn.snaps.size())
            {
                colSel = h->b; nameIs (1, kn.snaps[(size_t) h->b]);
                const auto id = kn.snaps[(size_t) h->b];
                const juce::uint32 lanes = kn.lanesAt ((size_t) h->b) & Session::kAllLanes;
                if (h->b < colScroll || h->b >= colScroll + kColRows) colScroll = std::max (0, h->b - kColRows / 2);
                if (ses.cueing() == id) ses.cue (0);
                else { ses.cueWith (id, Session::scopeOf (lanes)); heardSays (id, ses.cue (id)); }
            }
            break;
        }
        // the column: the chosen stack knob's pile
        case hColKey:
        {
            const auto& kn = ses.knobs[(size_t) selKnob];
            if (h->a >= (int) kn.snaps.size()) break;
            const juce::uint32 lanes = kn.keysAt ((size_t) h->a) ^ (1u << h->b);       // (b past the parts: N)
            const auto id = kn.snaps[(size_t) h->a];
            ses.stackLanes (selKnob, h->a, lanes);
            // (heard at once, if it is the one in the headphones)
            if (ses.cueing() == id) ses.cueWith (id, Session::scopeOf (lanes));
            break;
        }
        case hColOut:   ses.stackDrop (selKnob, h->a); colSel = -1; break;
        case hColCard: break;       // (pressed and let go where it is: heard. Carried: moved. See mouseUp)
        case hLibTab:   rackShelf = juce::jlimit (0, 3, h->a); rackFirst = 0; addTo = false; libScroll = 0; break;
        case hWell:     openWell (h->a, h->b); break;
        case hRowCard:  break;      // (pressed and let go where it is: in hand, and heard. Carried: moved, or taken out. See mouseUp)
        case hRowKey:
            // a key of a chip in the open stack's row: the part it takes there, put out or lit (N: whether a knob coming to it brings its notes)
            if (const auto pile = rowAt(); pile.what != 0 && h->a < (int) ses.pileSnaps (pile).size())
            {
                const juce::uint32 lanes = ses.pileKeysAt (pile, h->a) ^ (1u << h->b);
                const auto id = ses.pileSnaps (pile)[(size_t) h->a];
                ses.pileKeys (pile, h->a, lanes);
                if (ses.cueing() == id) ses.cueWith (id, Session::scopeOf (lanes & Session::kAllLanes), (lanes & Session::kNotesKey) != 0 ? 1 : -1);       // (heard at once, if it is the one in the headphones)
            }
            break;
        case hBenchChip:
            // a chip seen in what is open (a kept knob's pile, a knob of a module, a part of an arrangement): in hand, and heard with the parts it takes there
            if (h->a < (int) ses.library.size())
            {
                const auto id = ses.library[(size_t) h->a].id;
                const auto keys = (juce::uint32) h->b;
                handId = id; handParts = keys & Session::kAllLanes; selSnap = 0; rowSel = -1; nameIs (1, id);
                if (ses.cueing() == id) ses.cue (0);
                else { ses.cueWith (id, Session::scopeOf (handParts), (keys & Session::kNotesKey) != 0 ? 1 : -1); heardSays (id, ses.cue (id)); }
            }
            break;
        case hModKnob:  openModKnob = openModKnob == h->a ? -1 : h->a; benchScroll = 0; rowSel = -1; rowScroll = 0; tieSel = -1; break;
        case hTie:
            // a tie of the open macro knob: the chosen one (TAKE OUT is for it); dragged up or down, how far it takes its knob
            if (const auto ties = ses.ties (tiesAt()); h->a < (int) ties.size()) { tieSel = h->a; drag.v0 = std::round (ties[(size_t) h->a].amount * 100.0f); }
            break;
        case hArrPart:
        {
            // a part of the arrangement pressed: it is the chosen one, and is heard in the headphones with the parts of the rig it takes; pressed again, it stops
            selPart = h->a;
            if (h->a < (int) ses.arrangement.size())
            {
                const auto& pt = ses.arrangement[(size_t) h->a];
                nameIs (1, pt.snap);
                if (ses.cueing() == pt.snap) ses.cue (0);
                else if (ses.findSnap (pt.snap) != nullptr) { ses.cueWith (pt.snap, Session::scopeOf (pt.lanes & Session::kAllLanes), pt.notes ? 1 : 0); heardSays (pt.snap, ses.cue (pt.snap)); }
            }
            break;
        }
        case hArrTrans:
            if (h->a < (int) ses.arrangement.size()) { selPart = h->a; ses.arrTransStep (h->a, h->b); repaint(); }
            break;
        case hArrKey:
        {
            // a key on a part: that part of the rig comes from its snapshot when the arrangement reaches it, or stays as it is
            if (h->a >= (int) ses.arrangement.size()) break;
            selPart = h->a;
            if (h->b == kLanes + 3) { ses.arrNotes (h->a, ! ses.arrangement[(size_t) h->a].notes); repaint(); break; }       // N: its notes
            const auto id = ses.arrangement[(size_t) h->a].snap;
            const juce::uint32 lanes = (ses.arrangement[(size_t) h->a].lanes ^ (1u << h->b)) & Session::kAllLanes;
            ses.arrLanes (h->a, lanes);
            if (ses.cueing() == id) ses.cueWith (id, Session::scopeOf (lanes));
            break;
        }
        case hArrBars:  selPart = h->a; drag.v0 = (float) ses.arrangement[(size_t) h->a].bars; break;
        case hArrMorph: selPart = h->a; drag.v0 = (float) ses.arrangement[(size_t) h->a].morph; break;
        case hPick:
        {
            // (a card pressed and let go is heard in the headphones; in the designer, carried, it is put where it is let go. See mouseUp)
            break;
        }
        case hPickKey:
        {
            // a key on the snapshot in hand: that part goes with it, or does not (heard at once, if it is in the headphones)
            if (h->a >= (int) ses.library.size()) break;
            const auto id = ses.library[(size_t) h->a].id;
            if (id != handId) break;
            handParts = (handParts ^ (1u << h->b)) & Session::kAllLanes;
            if (ses.cueing() == id) ses.cueWith (id, Session::scopeOf (handParts));     // (it is made again from the parts lit now; all of them: the clip itself)
            break;
        }
        case hRackKeep:
            // (the chosen stack knob's snapshots, each with its parts, into the stacks kept under the knob's name)
            if (const int at = ses.stackKeep (selKnob); at >= 0)
            {
                rackShelf = 0; rackFirst = std::max (0, at - 1);        // (its pocket is in view)
                recNote = "KEPT AS THE STACK " + ses.stacks[(size_t) at].name + ": ITS POCKET IS IN THE RACK"; recNoteAt = juce::Time::getMillisecondCounter();
            }
            break;
        case hKeptKeep:
            // (the chosen knob, as it is, into the knobs kept)
            if (const int at = ses.knobKeep (selKnob); at >= 0)
            {
                rackShelf = 1; rackFirst = std::max (0, at - 1); inStack = {};
                recNote = "KEPT WITH THE KNOBS AS " + ses.keptKnobs[(size_t) at].name() + ": ITS POCKET IS IN THE RACK"; recNoteAt = juce::Time::getMillisecondCounter();
            }
            break;
        case hFind: pressFind (h->a); break;
        case hPickAdd:
        {
            // (onto the chosen stack knob, at the end of its pile. A macro knob holds no snapshots: LEARN is how one is made.)
            // (with the parts chosen on its card, if it is the one in hand)
            const auto id = ses.library[(size_t) h->a].id;
            const auto parts = id == handId ? handParts : Session::kAllLanes;
            if (page == pArrange)
            {
                // beside the arrangement: a part after the one that is chosen (with none chosen, at the end), taking those parts of the rig
                const int n = (int) ses.arrangement.size(), at = selPart >= 0 && selPart + 1 < n ? selPart + 1 : -1;
                if (ses.arrAdd (id, at, parts)) selPart = at >= 0 ? at : n;
                break;
            }
            if (Session::isStack (selKnob)) ses.stackAdd (selKnob, id, parts);
            else { recNote = "1 TO 4 ARE MACRO KNOBS (LEARN). SNAPSHOTS GO ON A STACK KNOB"; recNoteAt = juce::Time::getMillisecondCounter(); }
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
            else if (drag.kind == hSnapKnob && Session::isStack (drag.a))
            {
                // (a stack knob is turned finely unless SHIFT is held: its snapshots are close together on the turn, and a place
                // between two is something to find. It moves a click at a time of the E16's 127, so that the figure its tag
                // shows, AT, is a place either hand can come back to.)
                v = drag.v0 + dy / (e.mods.isShiftDown() ? 160.0f : 800.0f);
                v = std::round (juce::jlimit (0.0f, 1.0f, v) * 127.0f) / 127.0f;
            }
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
        case hTie:          // (three points of the hand to a hundredth)
            if (ses.tieSet (tiesAt(), drag.a, (int) std::lround (drag.v0 + dy / 3.0f))) repaint();
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
            if (! s.on) ses.lightStep (ses.activeLane, s);
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
        case hCard: case hRowCard:
            // the library: a chip of the bin, or of the open stack's row, pressed and moved: it goes with the pointer until it is let go
            if (page != pLibrary) break;
            if (! drag.moved && std::max (std::abs (dx), std::abs (dy)) < 8.0f) break;
            drag.moved = true; carryAt = p;
            repaint();
            break;
        case hWell:
            // (the designer: a stack's pile, or a kept knob, carried from its pocket onto a place. The arrangement's page: a stack's pile, onto the arrangement)
            if (! ((page == pMacros && (drag.a == 1 || drag.a == 2)) || (page == pArrange && drag.a == 1))) break;
            if (! drag.moved && std::max (std::abs (dx), std::abs (dy)) < 8.0f) break;
            drag.moved = true; carryAt = p;
            repaint();
            break;
        case hPick: case hColCard:
            // a card beside the knob, pressed and moved: it goes with the pointer until it is let go (a chip of the rack, beside the arrangement too)
            if (page != pMacros && ! (page == pArrange && drag.kind == hPick)) break;
            if (! drag.moved && std::max (std::abs (dx), std::abs (dy)) < 8.0f) break;
            drag.moved = true; carryAt = p;
            repaint();
            break;
        default: break;
    }
}

void Screen::mouseUp (const juce::MouseEvent& e)
{
    if (eyeDrag != 0)
    {
        eyeDrag = 0;
        if (stage != nullptr) eyePose = stage->home();
        if (onLook) onLook();
        return;
    }
    release (&e);
}

// The hand is taken off (no mouse: a key pressed it, and nothing was carried).
void Screen::release (const juce::MouseEvent* e)
{
    mouseIsDown = false;
    if (drag.kind == hHold) ses.userSet (drag.a, 0.0f);
    if (drag.kind == hSetup && drag.a < (int) cfgParams.size() && cfgParams[(size_t) drag.a].kind == Kind::Hold) eng.setSetup (cfg, cfgParams[(size_t) drag.a].id, 0.0f);
    if (drag.kind == hDisplay) if (auto* pr = eng.processorFor (cfg)) pr->mod().displayMouseUp();
    cfgStale = true;
    if (drag.kind == hStep && ! drag.moved) stepTap (drag.a);
    if (drag.kind == hAct && heldAct (drag.a) && ! drag.fired)
    {
        rackSay (drag.a == aDelete || drag.a == aOpenDelete ? "HOLD DELETE UNTIL IT IS FULL OF LIGHT: THEN IT IS DELETED" : drag.a == aNewTrack ? "HOLD NEW UNTIL IT IS FULL OF LIGHT: THEN THE RIG IS AS IT STARTS"
                                                              : "HOLD CLEAR UNTIL IT IS FULL OF LIGHT: THEN IT IS CLEARED");
        dirty (holdDirty);
    }
    if (page == pLibrary && drag.page == (int) page && (drag.kind == hCard || drag.kind == hRowCard))
    {
        if (drag.moved && e != nullptr) rackDrop (where (*e));
        else if (drag.kind == hRowCard)
        {
            // a chip of the open stack's row, pressed: it is the one in hand there, and is heard with the parts it takes; pressed again, it stops
            if (const auto pile = rowAt(); pile.what != 0 && drag.a < (int) ses.pileSnaps (pile).size())
            {
                const auto id = ses.pileSnaps (pile)[(size_t) drag.a];
                const auto keys = ses.pileKeysAt (pile, drag.a);
                rowSel = drag.a; handId = 0; selSnap = 0; nameIs (1, id);
                if (ses.cueing() == id) ses.cue (0);
                else { ses.cueWith (id, Session::scopeOf (keys & Session::kAllLanes), (keys & Session::kNotesKey) != 0 ? 1 : -1); heardSays (id, ses.cue (id)); }
            }
        }
    }
    if ((page == pMacros || (page == pArrange && (drag.kind == hPick || drag.kind == hWell))) && drag.page == (int) page && (drag.kind == hPick || drag.kind == hColCard || drag.kind == hWell))
    {
        if (drag.moved && e != nullptr) drop (where (*e));
        else if (drag.kind == hPick && drag.a < (int) ses.library.size())
        {
            // a snapshot of the list, pressed: it is the one in hand (its card shows the keys for its parts), and is heard
            // in the headphones with the parts that are lit; pressed again, it stops
            const auto id = ses.library[(size_t) drag.a].id;
            nameIs (1, id);
            if (id != handId) { handId = id; handParts = Session::kAllLanes; }
            if (ses.cueing() == id) ses.cue (0);
            else { if (handParts == Session::kAllLanes) ses.cueWith (0, Session::Scope()); else ses.cueWith (id, Session::scopeOf (handParts)); heardSays (id, ses.cue (id)); }
        }
        else if (drag.kind == hColCard)
        {
            const auto& snaps = ses.knobs[(size_t) selKnob].snaps;
            if (drag.a < (int) snaps.size())
            {
                colSel = drag.a;            // (the one TAKE OUT, EARLIER and LATER are for)
                const auto id = snaps[(size_t) drag.a];
                nameIs (1, id);
                const juce::uint32 lanes = ses.knobs[(size_t) selKnob].lanesAt ((size_t) drag.a) & Session::kAllLanes;
                if (ses.cueing() == id) ses.cue (0);
                else { ses.cueWith (id, Session::scopeOf (lanes)); heardSays (id, ses.cue (id)); }
            }
        }
    }
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
        else if (recallHit (*h)) {}                                         // (a chip: two quick presses take it to the set)
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

// A chip to the set, as two quick presses take it (and SHIFT with RETURN, where the keyboard's mark is): one of the bin, one
// of the pile that is out in a row (with the parts it takes there), one seen in what is open. False: it is not such a chip.
bool Screen::recallHit (const Hit& h)
{
    if (h.kind == hCard && page == pLibrary) { act (aRecallCue); return true; }
    if (const auto pile = rowAt(); h.kind == hRowCard && pile.what != 0 && h.a < (int) ses.pileSnaps (pile).size())
    {
        handId = ses.pileSnaps (pile)[(size_t) h.a]; handParts = ses.pileKeysAt (pile, h.a) & Session::kAllLanes;
        act (aRecallCue);
        handId = 0;
        return true;
    }
    if (h.kind == hBenchChip && h.a < (int) ses.library.size()) { handId = ses.library[(size_t) h.a].id; handParts = (juce::uint32) h.b & Session::kAllLanes; act (aRecallCue); return true; }
    return false;
}

void Screen::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (page == pArrange && where (e).x < 1146.0f) { arrScroll = std::max (0, arrScroll - (w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0)); repaint(); return; }      // (its parts; the library beside them, below)
    if (page == pLibrary || page == pMacros || page == pArrange)
    {
        // over the rack, its pockets; in the library, over the open stack's row, its rows; elsewhere the chips listed (below)
        const auto at = where (e);
        const int by = w.deltaY > 0 ? -1 : w.deltaY < 0 ? 1 : 0;
        if (rackArea.contains (at)) { rackFirst = std::max (0, rackFirst + by); repaint(); return; }
        if (rackOpen() >= 2 && benchArea.contains (at)) { benchScroll = std::max (0, benchScroll + by); repaint(); return; }
        if (page == pLibrary && ! rowArea.isEmpty() && rowArea.contains (at)) { rowScroll = std::max (0, rowScroll + by); repaint(); return; }
    }
    if (page == pMacros && columnShown() && where (e).x >= kColX - 5.0f && where (e).x < kColX + 277.0f && where (e).y >= kColHead)
    {
        // (over the column: the column moves; over the library beside it, the library)
        colScroll = std::max (0, colScroll - (w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0)); repaint(); return;
    }
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
            const bool stackKnob = h->kind == hSnapKnob && Session::isStack (h->a);       // (a click of its 127 for each notch)
            const float step = c.kind == Kind::List && c.steps() > 1 ? 1.0f / (float) (c.steps() - 1) : ctl == cx::kTempo ? 1.0f / 140.0f : stackKnob ? 1.0f / 127.0f : 0.02f;
            ses.userSet (ctl, stackKnob ? std::round ((ses.value (ctl) + dir * step) * 127.0f) / 127.0f : ses.value (ctl) + dir * step);
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
    if (k == juce::KeyPress::returnKey && editShown()) { act (aEditKeep); repaint(); return true; }
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
    if (k.getTextCharacter() == 't' || k.getTextCharacter() == 'T') { act (aTransFire); return true; }      // TRANSITION: FIRE
    if (k.getTextCharacter() == 'g' || k.getTextCharacter() == 'G') { act (aTransArm); return true; }       //             ARM
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
    if (rackPage() && kbKey (k)) return true;                           // (where the rack is: the mark, and what is done where it stands)
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

// ------------------------------------------------------------------------------------ the keyboard, where the rack is
// The things the mark can stand on: what is pressed in the rack, and on its page.
bool Screen::kbIs (const Hit& h) const
{
    if (! rackPage()) return false;
    if (h.kind == hWell || h.kind == hBenchChip || h.kind == hModKnob) return true;
    if (page == pLibrary) return h.kind == hCard || h.kind == hRowCard || h.kind == hTie;
    if (page == pMacros) return h.kind == hPick || h.kind == hKnobCard || h.kind == hColCard;
    return h.kind == hPick || h.kind == hArrPart;
}

// What the mark is on: the thing under where it stands, or the nearest to there (a list has moved under it).
const Screen::Hit* Screen::kbItem() const
{
    const Hit* nearest = nullptr;
    float best = 1.0e12f;
    for (size_t i = hits.size(); i-- > 0;)
    {
        const auto& h = hits[i];
        if (! kbIs (h)) continue;
        if (h.r.contains (kbAt)) return &h;
        const float d = h.r.getCentre().getDistanceSquaredFrom (kbAt);
        if (d < best) { best = d; nearest = &h; }
    }
    return nearest;
}

// The next thing that way. One that is in line with where the mark is, if there is one: the nearest line of them, and the
// one of it most nearly opposite. Else (unless only one in line will do) the nearest of all that lie wholly that way.
const Screen::Hit* Screen::kbToward (const Hit& from, int dx, int dy, bool sameKind, bool inLineOnly) const
{
    const R cr = from.r;
    const Hit* inLine = nullptr; const Hit* any = nullptr;
    float bestLine = 1.0e12f, bestAny = 1.0e12f;
    for (const auto& h : hits)
    {
        if (! kbIs (h) || (sameKind && h.kind != from.kind) || h.r == cr) continue;
        const R r = h.r;
        const float gap = dx > 0 ? r.getX() - cr.getRight() : dx < 0 ? cr.getX() - r.getRight() : dy > 0 ? r.getY() - cr.getBottom() : cr.getY() - r.getBottom();
        if (gap < -4.0f) continue;          // (it must lie wholly that way)
        const float over = dx != 0 ? std::min (r.getBottom(), cr.getBottom()) - std::max (r.getY(), cr.getY()) : std::min (r.getRight(), cr.getRight()) - std::max (r.getX(), cr.getX());
        const float off = dx != 0 ? std::abs (r.getCentreY() - cr.getCentreY()) : std::abs (r.getCentreX() - cr.getCentreX());
        const float least = dx != 0 ? std::min (r.getHeight(), cr.getHeight()) : std::min (r.getWidth(), cr.getWidth());
        if (over >= 0.34f * least) { const float d = std::round (gap / 8.0f) * 4000.0f + off; if (d < bestLine) { bestLine = d; inLine = &h; } }
        else { const float d = gap + off * 2.0f; if (d < bestAny) { bestAny = d; any = &h; } }
    }
    return inLine != nullptr || inLineOnly ? inLine : any;
}

// The mark has come to a thing that is chosen where it stands (a chip of a pile, a tie, a part): it is the chosen one, as a
// press of the mouse makes it, so the keys on the page are for it too. Nothing is heard: RETURN is the press.
void Screen::kbChoose (const Hit& h)
{
    if (h.kind == hRowCard) { rowSel = h.a; handId = 0; selSnap = 0; }
    else if (h.kind == hTie) tieSel = h.a;
    else if (h.kind == hColCard) colSel = h.a;
    else if (h.kind == hArrPart) selPart = h.a;
}

void Screen::kbMove (int dx, int dy)
{
    const Hit* at = kbItem();
    if (at == nullptr) return;
    const Hit c = *at;
    const Hit* to = nullptr;
    if (dx != 0)
    {
        // (at the shelf's edges the pockets go by under the mark, as under the wheel: the bin stays where it is)
        if (c.kind == hWell && c.a != 0 && dx < 0 && rackFirst > 0)
            if (const Hit* l = kbToward (c, -1, 0, true, true); l == nullptr || l->a == 0) { --rackFirst; repaint(); return; }
        to = kbToward (c, dx, 0, false, true);
        if (to == nullptr && c.kind == hWell && dx > 0) { ++rackFirst; repaint(); return; }      // (no further than there are pockets: the drawing sees to it)
        if (to == nullptr && c.kind != hWell)
        {
            // the end of a line of chips: on to the first of the line after it (back to the last of the one before)
            float bestY = 1.0e9f;
            for (const auto& h : hits)
            {
                if (h.kind != c.kind || ! kbIs (h)) continue;
                const float gap = dx > 0 ? h.r.getY() - c.r.getBottom() : c.r.getY() - h.r.getBottom();
                if (gap < -4.0f) continue;
                const float d = std::round (gap / 8.0f) * 4000.0f + (dx > 0 ? h.r.getX() : -h.r.getX());
                if (d < bestY) { bestY = d; to = &h; }
            }
        }
    }
    else
    {
        // within its own list first; at that list's edge the list goes by, where there is more of it; then on to whatever is that way
        to = kbToward (c, 0, dy, true, false);
        if (to == nullptr)
        {
            int* scroll = c.kind == hCard || c.kind == hPick ? &libScroll : c.kind == hRowCard ? &rowScroll : c.kind == hBenchChip || c.kind == hTie ? &benchScroll
                        : c.kind == hColCard ? &colScroll : c.kind == hArrPart ? &arrScroll : nullptr;
            const int far = c.kind == hCard || c.kind == hPick ? most.bin : c.kind == hRowCard ? most.row : c.kind == hBenchChip || c.kind == hTie ? most.bench : c.kind == hColCard ? most.col : most.arr;
            if (scroll != nullptr && (dy > 0 ? *scroll < far : *scroll > 0))
            {
                // (the list has gone by: what is under the mark now is the chosen one)
                *scroll += dy;
                layoutNow();
                if (const Hit* now = kbItem()) kbChoose (*now);
                repaint();
                return;
            }
            to = kbToward (c, 0, dy, false, false);
        }
    }
    if (to != nullptr) { kbAt = to->r.getCentre(); kbRect = to->r; kbChoose (*to); repaint(); }
}

// Pressed and let go, by a key: what a press of the mouse on it does.
void Screen::tap (const Hit& h)
{
    const Hit on = h;
    drag = {};
    mouseIsDown = true;
    downOn (on, on.r.getCentre());
    release (nullptr);
}

// The page laid out again with nothing drawn: every control's place as the page now stands. (A key is about to act on what
// the mark is on, and the page has changed since it was last drawn.)
void Screen::layoutNow()
{
    juce::Image none (juce::Image::ARGB, 1, 1, true, juce::SoftwareImageType());
    G g (none);
    g.reduceClipRegion (juce::Rectangle<int>());
    const bool was = collect;
    collect = true;
    drawAll (g);
    collect = was;
}

// A tie, a step less or more: a hundredth, or ten. Never nothing: on through it.
void Screen::tieStep (int i, int by)
{
    const auto ta = tiesAt();
    const auto ties = ses.ties (ta);
    if (i < 0 || i >= (int) ties.size()) return;
    tieSel = i;
    const int now = (int) std::lround (ties[(size_t) i].amount * 100.0f);
    int to = juce::jlimit (-100, 100, now + by);
    if (to == 0) to = by < 0 ? -1 : 1;
    ses.tieSet (ta, i, to);
}

// A key, on a page that has the rack. True: it was the rack's.
bool Screen::kbKey (const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    const auto ch = k.getTextCharacter();
    const bool shift = k.getModifiers().isShiftDown();
    const bool arrow = code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey;
    const bool tab = code == juce::KeyPress::tabKey, ret = code == juce::KeyPress::returnKey, back = code == juce::KeyPress::backspaceKey || code == juce::KeyPress::deleteKey;
    const bool add = ch == '=' || ch == '+', earlier = ch == '[' || ch == '{', later = ch == ']' || ch == '}';
    if (! (arrow || tab || ret || back || add || earlier || later)) return false;
    if (kbStale) layoutNow();               // (keys came faster than the page was drawn: where things are now)
    kbStale = true;
    const int way = earlier ? -1 : 1;
    if (tab)
    {
        // the next shelf (SHIFT: the one before), as its key pressed; the mark is on its first pocket
        const int to = (rackShelf + (shift ? 3 : 1)) % 4;
        for (const auto& h : hits) if (h.kind == hLibTab && h.a == to) { tap (h); break; }
        kbOn = true; kbSeek (hWell, 0);
        repaint();
        return true;
    }
    if (! kbOn)
    {
        if (arrow || ret)
        {
            // the mark comes out where the page is: on the pocket that is open (the bin, with nothing open). The next key moves or presses
            kbOn = true;
            const int open = rackOpen();
            const int index = open == 1 ? inAt() : open == 2 ? openKeptAt() : open == 3 ? openModAt() : open == 4 ? openArrAt() : 0;
            const Hit* start = nullptr;
            for (const auto& h : hits) if (h.kind == hWell && h.a == open && (open == 0 || h.b == index)) { start = &h; break; }
            if (start == nullptr) for (const auto& h : hits) if (h.kind == hWell && h.a == 0) { start = &h; break; }
            if (start != nullptr) { kbAt = start->r.getCentre(); kbRect = start->r; }
            repaint();
            return true;
        }
        // the mark is not out: the key is for what the mouse last chose (the chip in hand, the chip pressed in a pile, the tie, the part)
        if (page == pArrange)
        {
            if (add) { for (const auto& h : hits) if (h.kind == hPickAdd && h.a < (int) ses.library.size() && ses.library[(size_t) h.a].id == handId) { tap (h); break; } }
            else act (back ? aArrRemove : earlier ? aArrEarlier : aArrLater);
        }
        else if ((earlier || later) && tiesAt().what != 0 && tieSel >= 0) tieStep (tieSel, way * (shift ? 10 : 1));
        else act (add ? aRowAdd : back ? aRowOut : earlier ? aRowEarlier : aRowLater);
        repaint();
        return true;
    }
    if (arrow)
    {
        kbMove (code == juce::KeyPress::leftKey ? -1 : code == juce::KeyPress::rightKey ? 1 : 0, code == juce::KeyPress::upKey ? -1 : code == juce::KeyPress::downKey ? 1 : 0);
        return true;
    }
    const Hit* at = kbItem();
    if (at == nullptr) return true;
    const Hit c = *at;
    if (ret)
    {
        if (shift)
        {
            // (a chip of the bin: it is the one in hand first, as its first press of two makes it)
            if (c.kind == hCard && c.a < (int) ses.library.size())
                if (const auto id = ses.library[(size_t) c.a].id; id != handId) { handId = id; handParts = Session::kAllLanes; }
            if (! recallHit (c)) rackSay ("RECALL IS FOR A CHIP IN THE LIBRARY: THE MARK IS NOT ON ONE");
        }
        else tap (c);
        repaint();
        return true;
    }
    if (add)
    {
        // the chip the mark is on, into what the page is making: the pile that is out (the library), the chosen stack knob's
        // pile (the designer), the arrangement (beside it)
        if (c.kind == hCard && c.a < (int) ses.library.size())
        {
            const auto id = ses.library[(size_t) c.a].id;
            if (id != handId) { handId = id; handParts = Session::kAllLanes; }
            if (rowAt().what != 0) act (aRowAdd); else rackSay ("NO PILE IS OUT TO ADD IT TO: OPEN A STACK'S POCKET FIRST");
        }
        else if (c.kind == hPick)
        {
            for (const auto& h : hits) if (h.kind == hPickAdd && h.a == c.a) { tap (h); break; }
        }
        else rackSay ("ADD IS FOR A CHIP OF THE BIN OR OF AN OPEN STACK: THE MARK IS NOT ON ONE");
        repaint();
        return true;
    }
    if (back)
    {
        if (c.kind == hRowCard) { rowSel = c.a; handId = 0; act (aRowOut); }
        else if (c.kind == hTie) { tieSel = c.a; act (aRowOut); }
        else if (c.kind == hColCard) { colSel = c.a; act (aRowOut); }
        else if (c.kind == hArrPart) { selPart = c.a; act (aArrRemove); }
        else rackSay ("TAKE OUT IS FOR A CHIP OF A PILE, A TIE OR A PART: THE MARK IS NOT ON ONE");
        repaint();
        return true;
    }
    // [ and ]: earlier and later in its pile (the mark goes with it); a tie, a hundredth less or more (SHIFT: ten)
    if (c.kind == hRowCard)
    {
        rowSel = c.a; handId = 0;
        act (way < 0 ? aRowEarlier : aRowLater);
        if (const int line = rowSel / 6; line < rowScroll) rowScroll = line; else if (line > rowScroll + 1) rowScroll = line - 1;
        kbSeek (hRowCard, rowSel);
    }
    else if (c.kind == hColCard) { colSel = c.a; act (way < 0 ? aRowEarlier : aRowLater); kbSeek (hColCard, colSel); }
    else if (c.kind == hArrPart)
    {
        selPart = c.a;
        act (way < 0 ? aArrEarlier : aArrLater);
        if (const int line = selPart / 4; line < arrScroll) arrScroll = line; else if (line > arrScroll + 6) arrScroll = line - 6;
        kbSeek (hArrPart, selPart);
    }
    else if (c.kind == hTie) tieStep (c.a, way * (shift ? 10 : 1));
    else rackSay ("EARLIER AND LATER ARE FOR A CHIP OF A PILE, A TIE OR A PART: THE MARK IS NOT ON ONE");
    repaint();
    return true;
}

// The keyboard's mark: four yellow corners round the thing it is on.
void Screen::drawKb (G& g)
{
    if (! kbOn) { kbRect = {}; return; }
    if (collect)
    {
        if (kbSeekKind != 0)
        {
            for (const auto& h : hits) if (h.kind == kbSeekKind && h.a == kbSeekA && kbIs (h)) { kbAt = h.r.getCentre(); break; }
            kbSeekKind = 0;
        }
        const auto* h = kbItem();
        kbRect = h != nullptr ? h->r : R();
        if (h != nullptr) kbAt = h->r.getCentre();
    }
    if (kbRect.isEmpty() || ! vis (g, kbRect.expanded (12.0f))) return;
    // Four corners, not a ring: a whole ring round a pocket is what says it is open (green) or holds the chip in hand (yellow).
    // Yellow on a dark line, so that it reads on the light instrument's panel and on the dark one's.
    const R r = kbRect.expanded (6.0f);
    const float arm = juce::jlimit (9.0f, 22.0f, std::min (r.getWidth(), r.getHeight()) * 0.3f);
    juce::Path corners;
    for (int c = 0; c < 4; ++c)
    {
        const float x = (c & 1) ? r.getRight() : r.getX(), y = (c & 2) ? r.getBottom() : r.getY();
        const float sx = (c & 1) ? -1.0f : 1.0f, sy = (c & 2) ? -1.0f : 1.0f;
        corners.startNewSubPath (x + sx * arm, y);
        corners.lineTo (x, y);
        corners.lineTo (x, y + sy * arm);
    }
    g.setColour (Colour (0xff181918));
    g.strokePath (corners, juce::PathStrokeType (8.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (col::yellow);
    g.strokePath (corners, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void Screen::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (files.size() == 0) return;
    const auto p = three && stage != nullptr ? stage->unitsAt ({ (float) x, (float) y }) : logical ({ (float) x, (float) y });
    // TRANSITION: a file dropped on its lead-in or its hit is what that plays
    if (transShown())
        for (int slot = 0; slot < 2; ++slot)
            if (R (transX() + 176, slot == 0 ? 16.5f : 38.5f, 110, 20).contains (p))
            {
                const bool ok = ses.transLoad (slot, juce::File (files[0]));
                recNote = ok ? String (slot == 0 ? "LEAD-IN: " : "HIT: ") + juce::File (files[0]).getFileName().toUpperCase() : "COULD NOT READ " + juce::File (files[0]).getFileName().toUpperCase();
                recNoteAt = juce::Time::getMillisecondCounter();
                repaint();
                return;
            }
    // a part of the arrangement: a file dropped on its IN or its HIT is that part's own lead-in or hit
    if (page == pArrange)
    {
        for (auto& h : hits)
            if (h.kind == hArrTrans && h.r.contains (p))
            {
                const bool ok = ses.arrTransFile (h.a, h.b, juce::File (files[0]));
                recNote = ok ? String (h.b == 0 ? "LEAD-IN OF PART " : "HIT OF PART ") + String (h.a + 1) + ": " + juce::File (files[0]).getFileName().toUpperCase() : "COULD NOT READ " + juce::File (files[0]).getFileName().toUpperCase();
                recNoteAt = juce::Time::getMillisecondCounter();
                repaint();
                return;
            }
        return;
    }
    if (page != pMain) return;
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

String Screen::editTitle() const
{
    if (ses.arrEdit < 0 || ses.arrEdit >= (int) ses.arrangement.size()) return {};
    const auto* sn = ses.findSnap (ses.arrangement[(size_t) ses.arrEdit].snap);
    // (short, and its name no wider than a card gives it: KEEP and DROP stand after it, before SNAPSHOT)
    String name = sn != nullptr ? sn->name : String();
    while (name.length() > 1 && tw (fLegend, name) > 150.0f) name = name.dropLastCharacters (1);
    return "EDIT " + String (ses.arrEdit + 1) + (name.isNotEmpty() ? " " + name : String());
}

float Screen::editKeysX() const
{
    return 23.0f + std::ceil (tw (fFlag, "SF-000")) + 28.0f + 12.0f + std::ceil (tw (fLegend, editTitle())) + 16.0f + 14.0f;
}

float Screen::learnKeysX() const
{
    return 23.0f + std::ceil (tw (fFlag, "SF-000")) + 28.0f + 12.0f + std::ceil (tw (fLegend, learnTitle())) + 16.0f + 14.0f;
}

juce::uint64 Screen::barNow() const
{
    const bool rec = recorder != nullptr && recorder->recording();
    const bool note = recNote.isNotEmpty() && juce::Time::getMillisecondCounter() - recNoteAt < recNoteMs();
    return (juce::uint64) (eng.isRunning() ? 1 : 0) | (juce::uint64) (eng.following() ? 2 : 0)
         | ((juce::uint64) (juce::uint32) std::lround (eng.target (cx::kTempo) * 14000.0f) << 2)
         | ((juce::uint64) (rec ? 1 + ((juce::int64) recorder->seconds() & 0xfffff) : 0) << 20)
         | ((juce::uint64) (rec && recorder->lostSeconds() > 0.0 ? 1 : 0) << 41) | ((juce::uint64) (note ? 1 : 0) << 42)
         | ((juce::uint64) (ses.learning + 1) << 43) | ((juce::uint64) ((ses.learning >= 0 ? ses.learnMoves() : 0) & 0xfff) << 48)      // (a knob being taught, and how many it has been shown)
         | ((juce::uint64) (ses.keyRoot() + 1) << 60);            // (KEY, when something other than the hand sets it)
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
    if (mouseIsDown && (drag.kind == hKnob || drag.kind == hRead || drag.kind == hSnapKnob || drag.kind == hDepth || drag.kind == hLen || drag.kind == hSetup || drag.kind == hArrBars || drag.kind == hArrMorph || drag.kind == hTie))
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
// A stack knob's says where it is among its snapshots instead: the one it has left and the one it is going toward (the
// way it was last turned), how far it is from the first to the second as a figure and as the bar, and ON with the bar
// full and green when it stands on one: the rig is then that snapshot, whole.
juce::Image Screen::tagPicture (int ctl)
{
    const auto& c = eng.control (ctl);
    String where, label; const char* what = nullptr;
    describe (ctl, where, label, what);
    if (ctl >= cx::kSnap) { label = ses.knobs[(size_t) (ctl - cx::kSnap)].name; where = "SNAPSHOTS"; }
    int ch = 0, num = 0;
    if (ccx::addressOf (ctl, ch, num)) where << (where.isEmpty() ? "" : "  ") << "CH " << ch << " CC " << num;
    String value = ctl >= cx::kSnap ? String (juce::roundToInt (eng.shown (ctl) * 100.0f)) : eng.valueText (ctl);
    float v = juce::jlimit (0.0f, 1.0f, eng.shown (ctl));
    bool standing = false, way = false;
    int at = 0, count = 0, onPlace = -1; float whole = v;        // (a stack knob's: its own place, 0 to 127, and the whole turn)
    if (const int k = ctl - cx::kSnap; k >= 0 && Session::isStack (k) && ! ses.stackCue)
        if (const auto w = ses.stackWay (k, v); w.to >= 0)
        {
            standing = w.on >= 0; way = true;
            // (the row under the bar is the knob itself, where the hand has left it; the bar is the sound, which MOTION may be swinging about that place)
            whole = juce::jlimit (0.0f, 1.0f, eng.target (ctl));
            at = juce::roundToInt (whole * 127.0f); count = w.count; onPlace = w.on;
            if (label.isEmpty()) label = "KNOB " + String (k + 1);
            // (N after a name: its notes come as the knob comes to it)
            const String to = w.toName + (w.notes ? " +N" : "");
            where = standing ? "ON " + to : (w.fromName.isNotEmpty() ? w.fromName + " > " : String ("TO ")) + to;
            value = standing ? String ("ON") : String (juce::jlimit (0, 99, juce::roundToInt (w.part * 100.0f)));
            v = standing ? 1.0f : w.part;
        }
    const String key = label + "|" + value + "|" + where + "|" + String (juce::roundToInt (v * 200.0f)) + (way ? "|AT " + String (at) + "|" + String (juce::roundToInt (whole * 400.0f)) + "/" + String (count) : String()) + (eng.isGliding (ctl) ? "g" : "");
    auto& pic = tagPics[ctl];
    if (pic.key == key && pic.image.isValid()) return pic.image;
    // (light on dark whatever the instrument: it floats over it)
    Colour accent = col::lit;
    for (const auto& k : recorded.knobs) if (k.ctl == ctl) accent = Colour (k.light);
    if (eng.isGliding (ctl) || standing) accent = col::green;
    if (accent != col::lit) accent = accent.withMultipliedSaturation (1.1f).withMultipliedBrightness (1.5f);
    const float s = stage != nullptr ? stage->tagScale() : 2.0f;
    // (a stack knob's: its second line names two snapshots and runs under the figure, so it is as wide as both; its bar is the point of it, and thicker)
    // (and under the bar its own row: AT and the knob's place of 127, then the whole turn, a post where each snapshot stands)
    const float wide = std::max (way ? 190.0f : 150.0f, std::max (tw (fLbl, label) + tw (fRead, value) + 44.0f, tw (fNote, where) + 24.0f + (way ? tw (fRead, value) + 14.0f : 0.0f))), tall = way ? 86.0f : 58.0f;
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
        const float th = way ? 6.0f : 3.0f;
        const R track (11, way ? 46.0f : tall - 12.0f, wide - 22, th);
        g.setColour (Colour (0x30D8D3C8)); g.fillRoundedRectangle (track, th * 0.5f);
        const float lo = c.kind == Kind::Bipolar ? std::min (v, 0.5f) : 0.0f, hi = c.kind == Kind::Bipolar ? std::max (v, 0.5f) : v;
        g.setColour (accent); g.fillRoundedRectangle (R (track.getX() + track.getWidth() * lo, track.getY(), std::max (2.0f, track.getWidth() * (hi - lo)), th), th * 0.5f);
        // (a stack knob's: a post at each end, the snapshot left and the one ahead; the one ahead is lit once the knob stands on it)
        if (way)
        {
            g.setColour (night::dim); g.fillRect (R (track.getX() - 1.0f, track.getY() - 3.0f, 2.0f, th + 6.0f));
            g.setColour (standing ? accent : night::ink); g.fillRect (R (track.getRight() - 1.0f, track.getY() - 3.0f, 2.0f, th + 6.0f));
            // the knob's own place: the figure to come back to (the same whichever way the knob came), and where that is in the whole turn
            const float ry = 62.0f;
            const String fig (at);
            const auto fAt = fRead.withHeight (17.0f);
            g.setColour (night::dim); g.setFont (fNote.withHeight (10.5f)); g.drawText ("AT", R (11, ry + 3.0f, 16, 12), juce::Justification::centredLeft, false);
            g.setColour (night::ink); g.setFont (fAt); g.drawText (fig, R (28, ry - 2.0f, 40, 20), juce::Justification::centredLeft, false);
            const float fw = std::ceil (tw (fAt, "127"));
            g.setColour (night::dim); g.setFont (fNote.withHeight (10.5f)); g.drawText ("/127", R (28 + fw + 3.0f, ry + 3.0f, 30, 12), juce::Justification::centredLeft, false);
            const float mx = 28 + fw + 3.0f + std::ceil (tw (fNote.withHeight (10.5f), "/127")) + 10.0f;
            const R map (mx, ry + 8.0f, wide - 11.0f - mx, 2.0f);
            g.setColour (Colour (0x30D8D3C8)); g.fillRect (map);
            for (int i = 0; i < count; ++i)
            {
                const float px = map.getX() + map.getWidth() * Session::placeOf (i, count);
                g.setColour (i == onPlace ? accent : night::dim); g.fillRect (R (px - 0.75f, map.getY() - 3.0f, 1.5f, 8.0f));
            }
            const float hx = map.getX() + map.getWidth() * juce::jlimit (0.0f, 1.0f, whole);
            juce::Path tip; tip.addTriangle (hx - 4.0f, map.getY() - 9.0f, hx + 4.0f, map.getY() - 9.0f, hx, map.getY() - 2.5f);
            g.setColour (accent == col::lit ? night::ink : accent); g.fillPath (tip);
        }
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
                // (GLIDE is the one knob whose value is printed beside it, on the sheet: that is printed again, here as on the flat screen)
                if (three && h.kind == hKnob && ctl != cx::kGlide) { lastShown[(size_t) ctl] = eng.shown (ctl); ringsMoved = true; continue; }
                if (three && h.kind == hKnob) ringsMoved = true;
                dirty (ctl == cx::lane (ses.activeLane, cx::kSeqMute) ? seqRect : h.r.expanded (14, 6));       // a sequencer's mute greys its steps
            }
        }
    if (ringsMoved && stage != nullptr) stage->kick();
    // a take that ended by itself (the disk) says so
    if (recorder != nullptr)
        if (const bool r = recorder->recording(); r != recWas) { recWas = r; recNote = recorder->note(); recNoteAt = juce::Time::getMillisecondCounter(); }
    // CUE lit (on the module, in the designer, or by knob 4 of the E16) with nowhere for a cue to play: said, on the top
    // bar. And a stack knob come to a snapshot that has no sound kept with it: said too. (Otherwise the headphones
    // simply carry on as they were, and nothing says why.)
    // CONFIG's page on the E16: the panel shows that page's twelve at the top, as the E16's knobs have them
    if (ses.cfgE16Page != cfgE16Was)
    {
        cfgE16Was = ses.cfgE16Page;
        if (cfgOpen) { if (cfgE16Was >= 0) cfgScroll = cfgE16Was; dirty (cfgRect); }
    }
    // A snapshot taken, by whatever hand (the SNAPSHOT key, S, the E16's 13 and 16 together): said on the top bar with
    // its name, for a few seconds. (The E16's own display says it only on firmware that shows the names it is sent.)
    if (ses.takes != takesWas)
    {
        takesWas = ses.takes;
        recNote = "SNAPSHOT TAKEN: " + ses.takenName; recNoteAt = juce::Time::getMillisecondCounter();
        dirty (R (8, 12, 1712, 52));
    }
    // KEY set, or a lane's KEY FOLLOW: the steps' notes read differently
    if (page == pMain && ses.keyMoves (ses.activeLane) != seqMovesWas) dirty (seqRect);
    if (ses.stackCue != cueLitWas)
    {
        cueLitWas = ses.stackCue;
        if (cueLitWas && ! eng.cueIsHeard()) { recNote = "CUE HAS NOWHERE TO PLAY. SETUP: TICK OUTPUTS 3 + 4 FOR THE HEADPHONES. OR H: PHONES ON ONE PAIR"; recNoteAt = juce::Time::getMillisecondCounter(); dirty (R (8, 12, 1712, 52)); }
    }
    if (ses.cueLacked != cueLackedWas)
    {
        cueLackedWas = ses.cueLacked;
        if (const auto* sn = cueLackedWas != 0 ? ses.findSnap (cueLackedWas) : nullptr)
        { recNote = "NO SOUND WAS KEPT WITH " + sn->name + " (TAKEN WITH THE CLOCK STOPPED): NOTHING TO HEAR"; recNoteAt = juce::Time::getMillisecondCounter(); dirty (R (8, 12, 1712, 52)); }
    }
    if (barNow() != barSig) dirty (R (8, 12, 1712, 52));          // RUN and TEMPO, when an outside clock moves them; KEY; REC's time
    if (online != nullptr && page == pLibrary && online->stamp() != onlineStamp) dirty (R (23, 980, 1682, 110));    // what ONLINE has to say
    if (online != nullptr && loadAsked)
    {
        // LOAD, asked for from the snapshot module: its key is lit while it works, and what it did is said on the top bar (in full on the library screen)
        const bool busy = online->loading();
        if (busy != loadBusy) { loadBusy = busy; juce::Component::repaint(); }
        if (! busy && (loadSeen || online->loadLine() != loadWas))         // (done: it was seen at work, or was too quick to see and has said something new)
        {
            loadAsked = loadSeen = false;
            String said = online->loadLine();
            if (said.length() > 92) said = said.substring (0, said.substring (0, 74).lastIndexOfChar (' ')) + " (THE REST: LIBRARY)";
            sayOnBar (said);
        }
        loadSeen = loadSeen || busy;
    }
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
    // a key that is held: it is drawn again as it fills (the 3D stage lights the key itself), and when it is full what it does is done
    {
        const float f = mouseIsDown && drag.kind == hAct ? holdFill (drag.a) : 0.0f;
        if (f > 0.0f || holdLit)
        {
            dirty (holdDirty);
            if (stage != nullptr && three) stage->holdLight (holdRect, f);
            holdLit = f > 0.0f;
        }
    }
    if (mouseIsDown && drag.kind == hAct && drag.a == aClearKnob && ! drag.fired && juce::Time::getMillisecondCounter() - drag.t0 > kHoldMs)
    {
        drag.fired = true;
        const String was = ses.knobs[(size_t) selKnob].name;
        ses.knobClear (selKnob);
        colSel = -1;
        rackSay ("KNOB " + String (selKnob + 1) + (was.isNotEmpty() ? " (" + was + ")" : String()) + " HOLDS NOTHING NOW");
        repaint();
    }
    if (mouseIsDown && drag.kind == hAct && drag.a == aNewTrack && ! drag.fired && juce::Time::getMillisecondCounter() - drag.t0 > 700)
    {
        drag.fired = true;
        ses.newTrack();
        selSnap = 0; cfgStale = true;
        recNote = "A NEW TRACK: THE RIG AS IT STARTS. TEMPO AND THE SNAPSHOT MODULE ARE AS THEY WERE"; recNoteAt = juce::Time::getMillisecondCounter();
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
        // (a chip deleted is out of every stack that held a reference to it: said, as it goes)
        const auto* sn = ses.findSnap (selSnap);
        const String name = sn != nullptr ? sn->name : String();
        const int in = ses.stacksHolding (selSnap);
        ses.remove (selSnap);
        if (name.isNotEmpty()) rackSay (name + " IS DELETED" + (in > 0 ? ", AND IS OUT OF THE " + String (in) + (in == 1 ? " STACK" : " STACKS") + " THAT HELD IT" : String()));
        if (handId == selSnap) handId = 0;
        selSnap = 0;
        repaint();
    }
    if (mouseIsDown && drag.kind == hAct && drag.a == aOpenDelete && ! drag.fired && juce::Time::getMillisecondCounter() - drag.t0 > 700)
    {
        drag.fired = true;
        rackOpenDo (4);
        repaint();
    }
}
} // namespace sf0
