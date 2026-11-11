#pragma once
// SF-000 screens: the main screen, the snapshot library and the macro designer, drawn at
// 1728 x 1117 (5 px to the millimetre on a 16-inch MacBook Pro) and scaled to the window.
#include <juce_gui_extra/juce_gui_extra.h>
#include "Session.h"

namespace sf0
{
class Screen : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit Screen (Session&);
    ~Screen() override;

    enum Page { pMain, pLibrary, pMacros };
    void setPage (Page);
    std::function<void()> onAudioSetup, onFullScreen;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray&, int, int) override;

    static constexpr float kW = 1728.0f, kH = 1117.0f;

    // For the offline screen test: every control's place (screen units), kind and index, as last drawn.
    struct TestHit { juce::Rectangle<float> r; int kind, a, b; };
    std::vector<TestHit> testHits() const { std::vector<TestHit> v; for (auto& h : hits) v.push_back ({ h.r, h.kind, h.a, h.b }); return v; }
    static bool testSkips (const TestHit& h) { return h.kind == hAct && (h.a == aAudio || h.a == aFull); }
    static bool testIsKnob (const TestHit& h) { return h.kind == hKnob || h.kind == hSnapKnob || h.kind == hRead || h.kind == hLen || h.kind == hDepth; }
    static bool testIsStep (const TestHit& h) { return h.kind == hStep; }

private:
    using G = juce::Graphics;
    using R = juce::Rectangle<float>;
    struct Hit { R r; int kind = 0, a = 0, b = 0; };
    enum HitKind { hKnob = 1, hToggle, hHold, hLane, hInsHead, hStep, hPageSel, hAct, hSnapKnob, hRead, hCard, hSlot, hKnobCard, hPick, hLanePanel, hLen, hDepth };
    enum Act { aRun, aPanic, aAudio, aSnapshot, aLibrary, aMacros, aMain, aNudgeL, aNudgeR, aRatchet, aNote, aOctave, aGen0, aGen1,
               aRename, aDelete, aRenameKnob, aClearKnob, aFull };

    void timerCallback() override;
    void dirty (R);
    juce::Point<float> logical (juce::Point<float> p) const { return p.transformedBy (xf.inverted()); }
    const Hit* hitAt (juce::Point<float>) const;
    bool vis (G&, R) const;

    // drawing kit
    void text (G&, const juce::String&, R, const juce::Font&, juce::Colour, juce::Justification = juce::Justification::centred);
    float tw (const juce::Font&, const juce::String&) const;
    float plate (G&, float x, float y, const juce::String&, const juce::Font&, float padX, juce::Colour bg, juce::Colour fg, float h = 26.0f);
    void legend (G&, float cx, float y, const juce::String&);
    void knob (G&, float cx, float cy, int size, float v, Kind, int steps, juce::Colour lit);
    void hollow (G&, float cx, float cy, float d, bool round);
    void key (G&, R, juce::Colour fill, float border = 2.0f);
    void lamp (G&, float cx, float cy, juce::Colour fill, float d = 12.0f);
    void readout (G&, R, const juce::String& label, const juce::String& value);
    void panel (G&, R, juce::Colour ground);
    void control (G&, int ctl, float cellX, float y, float cellW, juce::Colour lit, int size = 40, float legendH = 28.0f);
    void actKey (G&, int act, float cellX, float y, float cellW, const juce::String& label, juce::Colour fill, float legendGap = 3.0f, bool present = true);

    // pages
    void drawBar (G&);
    void drawMain (G&);
    void drawLane (G&, int lane, R);
    void drawSnapPanel (G&, R);
    void drawInsert (G&, int slot, R);
    void drawReturns (G&, R);
    void drawMaster (G&, R);
    void drawSequencer (G&, R);
    void drawLibrary (G&);
    void drawMacros (G&);
    void drawSnapCard (G&, const Snapshot&, int index, R, bool selected, int hitKind);

    void act (int a);
    void stepTap (int i);
    void beginEdit (R where, const juce::String& initial, std::function<void (juce::String)> done);
    void endEdit (bool keep);

    Session& ses;
    Engine& eng;
    Page page = pMain;
    juce::AffineTransform xf;
    std::vector<Hit> hits;
    juce::Font fLbl, fLegend, fNote, fRead, fFlag;
    std::array<float, cx::kCount> lastShown {};
    R masterRect, seqRect;
    bool wasRunning = false;
    int lastStep = -1;

    struct Drag { int kind = 0, a = 0, b = 0; juce::Point<float> start; float v0 = 0, g0 = 0; int r0 = 1; bool moved = false; char axis = 0; juce::uint32 t0 = 0; bool fired = false; } drag;
    juce::int64 selSnap = 0;
    int selKnob = 0, libScroll = 0;
    R selNameRect;
    bool mouseIsDown = false;
    std::unique_ptr<juce::TextEditor> editor;
    std::function<void (juce::String)> editDone;
};
} // namespace sf0
