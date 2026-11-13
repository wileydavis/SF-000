#pragma once
// The 3D stage: the instrument as a solid thing, lit, seen through an eye that can be moved.
//
// It is a third look for the same screen, after the flat drawing and the lit skin. The screen
// still lays everything out and still takes the hand; what changes is where its drawing goes.
// The skin records the solid parts (Parts.h); everything flat (the print on the plates, what the
// displays show, the bars on the step keys) is drawn into one picture, the sheet, which is laid
// over the tops of the parts. The stage draws both with OpenGL on a thread of its own, so the
// eye moves and the knobs' lights follow the engine without waiting on anything else.
#include <juce_gui_basics/juce_gui_basics.h>
#include "Parts.h"
#include <functional>
#include <memory>

namespace sf0
{
class Engine;
class Stage
{
public:
    static constexpr float kW = 1728.0f, kH = 1117.0f;      // the screen's units, corner to corner of the case

    explicit Stage (Engine&);
    ~Stage();

    // On the screen's own component: the picture covers it. Starting takes a moment (and can fail:
    // no OpenGL, a shader the driver will not take), so there are three states.
    void attachTo (juce::Component&);
    void detach();
    bool attached() const;
    enum class State { starting, running, failed };
    State state() const;
    juce::String whyNot() const;                // failed: what the driver said
    void giveUp (const juce::String& why);      // failed, for a reason seen from outside (it never began)
    std::function<void()> onState;              // running or failed, told on the message thread

    // ---- what it draws
    void setParts (const Parts&);               // a whole new record
    void setDark (bool);
    // The glow under the case and the warm light up its sides: its colour (round the wheel, 0 to 1)
    // and how bright (0 none, 0.5 as it comes, 1 four times that).
    // pulse: how much it beats with the kick (0 steady; 1 nearly out between hits and three times as bright on one).
    static constexpr float kGlowHue = 0.0279f, kGlowBright = 0.5f;
    void setGlow (float hue, float bright, float pulse = 0.0f);
    // The sheet: kW x kH units at sheetScale() pixels to the unit, transparent where nothing is
    // printed. Draw into it, then say which pixels changed.
    juce::Image& sheet();
    float sheetScale() const;
    void sheetChanged (juce::Rectangle<int> pixels);

    // A tag: a small picture that floats by a place on the instrument (a moved knob's value), or
    // by the pointer (INFO's box), always facing the eye and never off the window.
    struct Tag
    {
        int id = 0;                     // the same id keeps the same place on the window from frame to frame
        juce::Image image;              // at tagScale() pixels to the point
        juce::uint32 stamp = 0;         // changes when the picture has
        float x = 0, y = 0, height = 0, reach = 0;      // where on the instrument (units), how high, and how far round it the thing reaches
        // where it goes: by its place on the instrument; by the pointer (px, component points); or
        // with the middle of its foot at px exactly (a line of words at the foot of the window)
        enum Place { byThing, byPointer, fixed };
        Place place = byThing;
        juce::Point<float> px;
    };
    void setTags (std::vector<Tag>);
    float tagScale() const;

    // ---- the eye
    // Home is where the player has put it: looking from `turn` degrees round (0: from the front)
    // and `tilt` degrees up (90: straight down), at a point on the instrument, from `away` units.
    // view: 0 TOP, 1 PLAYER, 2 HERO (the whole instrument, fitted to the window), or -1: as placed.
    struct Pose { float turn = 0.0f, tilt = 56.0f, away = 0.0f, tx = 0.0f, ty = 0.0f, tz = 0.0f; int view = 1; };
    void setHome (const Pose&, bool fly);
    Pose home() const;
    void turn (float dxPoints, float dyPoints);                 // round it, and over it
    void slide (float dxPoints, float dyPoints);                // sideways and up
    void closer (float clicks);                                 // toward what it looks at (negative: away)
    // Following: hold this much of the instrument (units) in the window, from home's side; nothing: go home.
    void lookAt (const juce::Rectangle<float>* box);
    bool isAway() const;                                        // it is somewhere other than home, or on its way

    // ---- through the eye (once a frame has been drawn)
    bool ready() const;
    // What the pointer is on: the place on the screen (units) that the hand would be touching.
    // A knob answers with its own middle, so that a press on its cap is a press on the knob.
    juce::Point<float> unitsAt (juce::Point<float> points) const;
    juce::Point<float> pointsOf (float ux, float uy, float height) const;       // where a place on the instrument is on the window
    float heightAt (float ux, float uy) const;                                  // how high the top of whatever stands there is

    // ---- for the tests and the numbers
    void kick();                                // something changed: draw
    void settle();                              // the eye goes to where it is going at once
    juce::Image capture (int timeoutMs = 20000);        // the next frame, as drawn
    double frameMs() const;                     // how long the last frame took to draw
    int frames() const;                         // how many have been drawn in full
    int builds() const;                         // how many times its shapes were built and sent to the card
    float keyTopNow (float ux, float uy) const; // how far the key there stands above its plate in the picture now (it travels), or -1: no key
    int keyBottoms() const;                     // how many times a key has gone to the bottom on its way from up to latched, or back
    int keyFrames() const;                      // how many frames have had a key on its way in them
    bool wearReady() const;                     // the sheet of scratches reached the card (without it the plates are simply unscratched)
    void showWear (bool);                       // for the checks: the picture with the sheet left out, to see what it adds
    int firstPictureSpread() const;             // darkest to brightest point of the first picture, of 255 (under 6 it is blank; -1: not looked at yet; -2: it could not be read back)
    juce::String driver() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace sf0
