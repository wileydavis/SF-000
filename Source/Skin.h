#pragma once
// The lit skin. Every surface on the screen is a patch of heights, colours and finishes, with the
// heights taken from the 3D model of the instrument (5.46 screen units to its millimetre). A patch
// is lit once, from the upper left and a little behind, seen from a little in front of straight
// above, and kept as a picture; the screen is then put together from the pictures.
#include <juce_gui_basics/juce_gui_basics.h>
#include "Parts.h"
#include <functional>
#include <map>
#include <tuple>
#include <vector>

namespace sf0
{
class Relief
{
public:
    // gloss: how much it shines (0 chalk, 1 glass). grain: how rough the surface is to the light.
    // glow: how much of its colour it gives off by itself (a lit key, an LED).
    struct Finish { juce::Colour colour; float gloss = 0.12f, grain = 0.0f, glow = 0.0f; };
    using R = juce::Rectangle<float>;

    // w by h units of surface with `margin` more on every side (for what stands over the edge and
    // the shade it casts), worked at pxPerUnit, `fine` times finer and brought down (smooth edges).
    Relief (float w, float h, float pxPerUnit, float margin, int fine = 1);

    // Shapes, put down one over another. `top` is the height its top comes to; `round` rounds its
    // top edge over. dent: the surface it lies on is pushed to that height and keeps its own
    // colour (a pocket, a dimple), where a shape otherwise brings its own.
    void slab (R, float corner, float top, float round, const Finish&, bool dent = false);
    void disc (float cx, float cy, float radius, float top, float round, const Finish&, bool dent = false, float dome = 0.0f);   // dome: how much lower its rim is than its middle
    void ring (float cx, float cy, float r0, float r1, int count, float gap, float top, const Finish&);     // `count` segments, the first starting at six o'clock, clockwise
    void letters (const juce::Image& alpha, R where, float top, const Finish&);                            // the alpha of a picture, as a raised shape

    enum class As { lit, glowOnly };        // glowOnly: no light falls on it and it casts no shade: what a lit LED adds
    juce::Image picture (As = As::lit) const;
    float margin() const { return m; }
    float fall = 0.0f;                      // how much dimmer the far corner (lower right) is than the near: the light is not at infinity

private:
    template <class Sdf> void stamp (float x0, float y0, float x1, float y1, Sdf&&, float top, float round, const Finish&, bool dent, float dome = 0.0f, float across = 1.0f);
    float shade (int x, int y, float from, float zmax) const;
    int pw = 0, ph = 0, fine = 1;
    float s = 1.0f, m = 0.0f;               // working pixels per unit; the margin in units
    std::vector<float> z, cr, cg, cb, gl, gr, so, em;      // height; colour (linear); gloss; grain; how solid (0: the surface below shows); glow
};

class Skin
{
public:
    using G = juce::Graphics;
    using R = juce::Rectangle<float>;

    // Call at the start of a paint, after the screen's transform is on. The pictures are made for
    // the device's pixels; when the window's size changes they are remade once it has held still.
    void begin (G&);
    bool settled();                         // true once: the size has held, the pictures were dropped: paint again
    // The dark instrument: a graphite case, dark plates. The same shapes and the same light.
    // (Each keeps its own pictures, so changing over costs nothing after the first time.)
    void setDark (bool on) { dark = on ? 1 : 0; }

    // For the 3D stage the same calls do one of two other things. Recording, each writes down the
    // solid thing it stands for (and draws nothing). Printing, the solid things draw nothing at
    // all: only what is printed on them is wanted, and the screen draws that itself.
    enum class Mode { pictures, record, print };
    void setMode (Mode m, Parts* into = nullptr) { mode = m; rec = into; }
    bool recording() const { return mode == Mode::record; }
    bool printing() const { return mode == Mode::print; }
    void setClip (const R* r) { clip = Parts::Clip(); if (r != nullptr) clip = { r->getX(), r->getY(), r->getRight(), r->getBottom() }; }      // what is being recorded shows only inside this
    void setShift (float dx) { shift = dx; }                        // and stands this far to the right of where it is drawn
    // A knob, whole: which of the engine's controls it shows (or -1), and what is lit if none.
    void recordKnob (float cx, float cy, int size, int ctl, int kind, int steps, juce::Colour light, float a, float b, float mark);

    void body (G&, float w, float h);                               // the case, corner to corner
    void panel (G&, R, juce::Colour ground);                        // a module's plate, in its pocket
    void makePanel (float w, float h, juce::Colour ground);         // its picture made now, ahead of its first showing (a big plate takes a moment)
    bool ready() const { return scale > 0.0f; }                     // a paint has said what the device's pixels are
    void knob (G&, float cx, float cy, int size);                   // base, ring unlit, cap
    void ringLit (G&, float cx, float cy, int size, float a, float b, juce::Colour);     // the ring's segments from a to b (turns from six o'clock)
    void key (G&, R, juce::Colour, bool down);
    void lamp (G&, float cx, float cy, juce::Colour, float d, bool on);
    void hollow (G&, float cx, float cy, float d, bool round);      // where a control could be and is not
    void window (G&, R, juce::Colour glass);                        // a display: a pane set into the surface
    void badge (G&, R, const juce::String&, const juce::Font&);     // the name plate: raised, with raised letters

    static bool emits (juce::Colour);       // the colours that mean a light is on

private:
    struct Pic { juce::Image img; float ox = 0.0f, oy = 0.0f, s = 1.0f; };
    using Key = std::tuple<int, int, int, juce::uint32, int>;       // what, width, height, colour, state
    const Pic& pic (const Key&, const std::function<Pic()>& make);
    const Pic& panelPic (int w, int h, juce::Colour ground);
    Pic from (const Relief&, Relief::As = Relief::As::lit) const;
    void put (G&, const Pic&, float x, float y) const;
    std::map<Key, Pic> pics[2];             // the light instrument's, the dark one's
    juce::Image caseImg[2];
    int dark = 0;
    Mode mode = Mode::pictures;
    Parts* rec = nullptr;
    Parts::Clip clip;
    float shift = 0.0f;
    float scale = 0.0f, want = 0.0f;
    juce::uint32 wantAt = 0;
};
} // namespace sf0
