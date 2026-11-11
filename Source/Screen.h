#pragma once
// SF-000 screens: the main screen, the snapshot library and the macro designer, drawn at
// 1728 x 1117 (5 px to the millimetre on a 16-inch MacBook Pro) and scaled to the window.
#include <juce_gui_extra/juce_gui_extra.h>
#include "Session.h"
#include <unordered_map>

namespace sf0
{
class Recorder;
class Screen : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit Screen (Session&);
    ~Screen() override;

    enum Page { pMain, pLibrary, pMacros };
    void setPage (Page);
    std::function<void()> onAudioSetup, onFullScreen;
    Recorder* recorder = nullptr;               // the REC key's; none: the key does nothing
    void showConfig (Engine::Target t) { cfgOpen = false; openConfig (t); cfgT = 1.0f; slideX = slideTo = slideGoal(); slideT = 1.0f; repaint(); }     // open CONFIG at once (screenshots, tests)
    static void releaseFonts();         // before JUCE shuts down: the embedded typefaces must not outlive it

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
    static bool testSkips (const TestHit& h) { return h.kind == hAct && (h.a == aAudio || h.a == aFull || h.a == aRec); }
    static bool testIsKnob (const TestHit& h) { return h.kind == hKnob || h.kind == hSnapKnob || h.kind == hRead || h.kind == hLen || h.kind == hDepth || h.kind == hSetup || h.kind == hDisplay; }
    static bool testIsStep (const TestHit& h) { return h.kind == hStep; }
    static bool testIsConfig (const TestHit& h) { return h.kind == hSetup || h.kind == hDisplay; }
    static bool testIsControl (const TestHit& h, int ctl) { return (h.kind == hKnob || h.kind == hRead) && h.a == ctl; }
    static bool testIsToggle (const TestHit& h, int ctl) { return h.kind == hToggle && h.a == ctl; }
    static bool testIsRec (const TestHit& h) { return h.kind == hAct && h.a == aRec; }
    void testTick() { timerCallback(); }                        // what the 30-a-second timer does
    static bool testIsSnapKnob (const TestHit& h, int k) { return h.kind == hSnapKnob && h.a == k; }
    // the performance panel a CONFIG target belongs to: a lane's, an insert's, the returns'
    static bool testIsPanelOf (const TestHit& h, Engine::Target t) { return t.kind == 0 ? h.kind == hLanePanel && h.a == t.lane : h.kind == hDropFx && h.a == t.kind && (t.kind == 2 || h.b == t.slot); }
    juce::Rectangle<float> testConfigRect() const { return cfgRect; }
    // what the timer redraws besides knobs: the meters, the sequencer strip, CONFIG's display
    std::vector<juce::Rectangle<int>> testMovingRects() const { std::vector<juce::Rectangle<int>> v; for (auto r : { masterRect, seqRect, cfgDisplay }) if (! r.isEmpty()) v.push_back (r.getSmallestIntegerContainer().expanded (2)); return v; }

private:
    using G = juce::Graphics;
    using R = juce::Rectangle<float>;
    struct Hit { R r; int kind = 0, a = 0, b = 0; };
    enum HitKind { hKnob = 1, hToggle, hHold, hLane, hInsHead, hStep, hPageSel, hAct, hSnapKnob, hRead, hCard, hSlot, hKnobCard, hPick, hLanePanel, hLen, hDepth, hConfig, hSetup, hDisplay, hBlock, hDropFx };
    enum Act { aRun, aPanic, aAudio, aSnapshot, aLibrary, aMacros, aMain, aNudgeL, aNudgeR, aRatchet, aNote, aOctave, aGen0, aGen1,
               aRename, aDelete, aRenameKnob, aClearKnob, aFull, aCfgClose, aClipReset, aRec, aCue, aRecallCue };

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
    void knob (G&, float cx, float cy, int size, float v, Kind, int steps, juce::Colour lit, float target = -1.0f);
    void hit (R, int kind, int a = 0, int b = 0);
    void headKeys (G&, float right, float y, int kind, int lane, int slot, int muteCtl, int soloCtl);
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
    void drawConfig (G&, R);
    void openConfig (Engine::Target);
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
    // A paint of the whole screen lays everything out and gathers the hits. A paint of a part of it
    // (a travelling knob, the meters, the playing step) draws only the panels that part touches
    // and leaves the hits as they are: nothing has moved.
    bool collect = true;
    mutable std::unordered_map<std::string, float> widths;      // a plate's text, measured once
    juce::uint64 meterSig = 0;                                  // what the meters showed when last drawn
    juce::uint64 metersNow() const;
    juce::Font fLbl, fLegend, fNote, fRead, fFlag;
    std::array<float, cx::kCount> lastShown {};
    R masterRect, seqRect;
    bool wasRunning = false;
    juce::uint64 barSig = 0;
    bool recWas = false;
    bool cueMode = false;                       // the library: a tap is heard in the headphones, not recalled
    juce::String recNote;                       // what the recorder last had to say, shown for a few seconds
    juce::uint32 recNoteAt = 0;
    juce::uint64 barNow() const;
    int lastStep = -1;

    struct Drag { int kind = 0, a = 0, b = 0; juce::Point<float> start; float v0 = 0, g0 = 0; int r0 = 1; bool moved = false; char axis = 0; juce::uint32 t0 = 0; bool fired = false; } drag;
    juce::int64 selSnap = 0;
    int selKnob = 0, libScroll = 0;
    R selNameRect;
    bool mouseIsDown = false;
    // CONFIG: the setup panel that slides in from the left. The top three rows move so that the
    // panel being set up (a lane, an insert, the returns) stands right beside it; their six columns
    // go round, so what leaves one edge of the screen comes back at the other.
    float shift = 0.0f;
    float slideX = 0.0f, slideFrom = 0.0f, slideTo = 0.0f, slideT = 1.0f;
    float slideGoal() const;
    R rowsClip;
    Engine::Target cfg;
    bool cfgOpen = false, cfgStale = true;
    float cfgT = 0.0f;
    int cfgScroll = 0, cfgAge = 0;
    std::vector<Engine::SetupParam> cfgParams;
    juce::String cfgName, cfgNote, cfgStatus;
    R cfgDisplay, cfgRect;
    std::unique_ptr<juce::TextEditor> editor;
    std::function<void (juce::String)> editDone;
};
} // namespace sf0
