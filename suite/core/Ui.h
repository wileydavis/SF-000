#pragma once
// Visual standard: black, white and greys. Colour only carries information:
//   RED = clip / fault, AMBER = conflict / warning, GREEN = active / locked to grid / gliding,
//   CYAN = the value of the control being touched.
// GREEN is the E16's own (the scene's ring colour for SLOW ALL and a glide, as the hardware lights
// it: a mint green, hue about 147 degrees), so a gliding knob on screen matches its ring. [Nov 6]
// The E16 grid's rings take the E16's colours, by role (E16Map.h kE16Rings, layout.py RING_RGB):
// they say what a knob does, as the E16 does, not a state. [Nov 8]
#include <juce_gui_basics/juce_gui_basics.h>
#include <atomic>

namespace sf::ui
{
const juce::Colour kBg       { 0xff000000 };
const juce::Colour kPanel    { 0xff0b0b0b };
const juce::Colour kLine     { 0xff2a2a2a };
const juce::Colour kGrid     { 0xff171717 };
const juce::Colour kText     { 0xffe8e8e8 };
const juce::Colour kDim      { 0xff8a8a8a };
const juce::Colour kFaint    { 0xff4a4a4a };
const juce::Colour kTrack    { 0xff2e2e2e };
const juce::Colour kAccent   { 0xff00c8ff };
const juce::Colour kRed      { 0xffff3b30 };
const juce::Colour kAmber    { 0xffffb000 };
const juce::Colour kGreen    { 0xff4cf59b };

inline juce::Font mono (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h, bold ? juce::Font::bold : juce::Font::plain));
}

inline void label (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Colour c = kDim,
                   float h = 11.0f, juce::Justification j = juce::Justification::centredLeft)
{
    g.setColour (c);
    g.setFont (mono (h));
    g.drawText (s, r, j, false);
}

// Text over up to maxLines lines (a status in the strip's narrow panels), squeezed a little before
// it is cut short.
inline void lines (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Colour c = kDim,
                   float h = 9.5f, int maxLines = 2, juce::Justification j = juce::Justification::topLeft)
{
    g.setColour (c);
    g.setFont (mono (h));
    g.drawFittedText (s, r.toNearestInt(), j, maxLines, 0.85f);
}

inline void frame (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title = {})
{
    g.setColour (kLine);
    g.drawRect (r, 1.0f);
    if (title.isNotEmpty()) label (g, title, r.reduced (6, 2).removeFromTop (14), kFaint, 10.0f);
}

// Horizontal level bar, dB scale -60..+6. Amber above -6 dBFS, red above -0.3.
inline void levelBar (juce::Graphics& g, juce::Rectangle<float> r, float linear)
{
    const float db = 20.0f * std::log10 (std::max (linear, 1.0e-6f));
    const float t = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
    g.setColour (kTrack);
    g.fillRect (r);
    g.setColour (db > -0.3f ? kRed : db > -6.0f ? kAmber : kText);
    g.fillRect (r.withWidth (r.getWidth() * t));
}

// Plain 0..1 value bar (no level colours).
inline void valueBar (juce::Graphics& g, juce::Rectangle<float> r, float v)
{
    g.setColour (kTrack);
    g.fillRect (r);
    g.setColour (kText);
    g.fillRect (r.withWidth (r.getWidth() * juce::jlimit (0.0f, 1.0f, v)));
}

// ---------------------------------------------------------------- the E16's encoders
// 16 LEDs round a black knurled cap. The first LED sits just left of the bottom; they fill clockwise,
// up the left, over the top, down to the last just right of the bottom. Positions run along that
// sweep in LEDs, 0..16: LED f lights over f..f+1 (the one at the end of a value lights partly, as
// the E16's does). Ring colours are the scene's (controllers/e16/layout.py, E16Map.h kE16Rings): white
// (kLedWhite), red, light blue, pink, violet, and green (kGreen) for SLOW ALL and a glide.
const juce::Colour kLedWhite { 0xffeef2ff };
const juce::Colour kLedOff   { 0xff383838 };    // an unlit LED
const juce::Colour kLedDark  { 0xff1f1f1f };    // an unlit LED of an encoder that isn't this plugin's

// Angle (radians, clockwise from the top) of a position along the sweep: 180 + 22.5 degrees per LED.
inline float ledAngle (float pos) { return juce::MathConstants<float>::pi * (1.0f + pos / 8.0f); }

inline juce::Point<float> onRing (juce::Point<float> c, float r, float pos)
{
    const float a = ledAngle (pos);
    return { c.x + r * std::sin (a), c.y - r * std::cos (a) };
}

inline void drawCap (juce::Graphics& g, juce::Point<float> c, float r)
{
    juce::ColourGradient body (juce::Colour (0xff202020), c.x - r * 0.45f, c.y - r * 0.55f,
                               juce::Colour (0xff070707), c.x + r * 0.5f, c.y + r * 0.7f, false);
    g.setGradientFill (body);
    g.fillEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r);
    g.setColour (juce::Colour (0xff2b2b2b));                        // the knurl
    for (int k = 0; k < 24; ++k)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) k / 24.0f, s = std::sin (a), co = -std::cos (a);
        g.drawLine (c.x + s * r * 0.80f, c.y + co * r * 0.80f, c.x + s * r * 0.97f, c.y + co * r * 0.97f, 0.8f);
    }
    g.setColour (juce::Colour (0xff141414));
    g.fillEllipse (c.x - r * 0.72f, c.y - r * 0.72f, 1.44f * r, 1.44f * r);
    g.setColour (juce::Colour (0xff303030));
    g.drawEllipse (c.x - r, c.y - r, 2.0f * r, 2.0f * r, 0.8f);
}

// The LEDs from `from` to `to` (positions, from <= to) lit in `on`; the rest show their windows in `off`.
inline void drawLeds (juce::Graphics& g, juce::Point<float> c, float r, float led, float from, float to,
                      juce::Colour on, juce::Colour off)
{
    for (int f = 0; f < 16; ++f)
    {
        const float a = ledAngle ((float) f + 0.5f), s = std::sin (a), co = -std::cos (a);
        g.setColour (off);
        g.drawLine (c.x + s * (r - led * 0.9f), c.y + co * (r - led * 0.9f),
                    c.x + s * (r + led * 0.9f), c.y + co * (r + led * 0.9f), std::max (1.0f, led * 0.62f));
        const float lit = juce::jlimit (0.0f, 1.0f, std::min (to, (float) f + 1.0f) - std::max (from, (float) f));
        if (lit <= 0.02f) continue;
        const float x = c.x + s * r, y = c.y + co * r;
        g.setColour (on.withAlpha (0.17f * lit));                      // the glow round it
        g.fillEllipse (x - led * 2.3f, y - led * 2.3f, led * 4.6f, led * 4.6f);
        g.setColour (on.withAlpha (lit));
        g.fillEllipse (x - led, y - led, 2.0f * led, 2.0f * led);
    }
}

// An LED drawn as a ring: where a travelling knob is going.
inline void drawLedMark (juce::Graphics& g, juce::Point<float> c, float r, float led, float pos, juce::Colour col)
{
    const int f = juce::jlimit (0, 15, (int) std::ceil (pos) - 1);
    const auto p = onRing (c, r, (float) f + 0.5f);
    g.setColour (col);
    g.drawEllipse (p.x - led - 0.8f, p.y - led - 0.8f, 2.0f * led + 1.6f, 2.0f * led + 1.6f, 1.0f);
}

// A dot outside the ring: where MACRO / MOTION have the value now.
inline void drawModDot (juce::Graphics& g, juce::Point<float> c, float r, float led, float pos, juce::Colour col)
{
    const auto p = onRing (c, r + led * 2.7f, pos);
    g.setColour (col);
    g.fillEllipse (p.x - led * 0.85f, p.y - led * 0.85f, led * 1.7f, led * 1.7f);
}

// Ring-buffer trace (values 0..1), newest at the right.
template <int N>
inline void trace (juce::Graphics& g, juce::Rectangle<float> r, const std::atomic<float> (&v)[N], int writePos, juce::Colour c)
{
    juce::Path p;
    for (int i = 0; i < N; ++i)
    {
        const float val = juce::jlimit (0.0f, 1.0f, v[(writePos + i) % N].load());
        const float x = r.getX() + r.getWidth() * (float) i / (float) (N - 1);
        const float y = r.getBottom() - val * r.getHeight();
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (c);
    g.strokePath (p, juce::PathStrokeType (1.2f));
}
} // namespace sf::ui
