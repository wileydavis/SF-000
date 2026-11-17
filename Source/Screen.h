#pragma once
// SF-000 screens: the main screen, the snapshot library and the macro designer, drawn at
// 1728 x 1117 (5 px to the millimetre on a 16-inch MacBook Pro) and scaled to the window.
#include <juce_gui_extra/juce_gui_extra.h>
#include "Session.h"
#include "Skin.h"
#include "Stage.h"
#include <unordered_map>

namespace sf0
{
class Recorder;
class Online;
class Screen : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer, private juce::AsyncUpdater
{
public:
    explicit Screen (Session&);
    ~Screen() override;

    enum Page { pMain, pLibrary, pMacros, pPockets, pArrange };
    void setPage (Page);
    std::function<void()> onAudioSetup, onFullScreen, onOnlineSetup;
    std::function<void()> onLeaveFullScreen;    // Esc, with nothing else open: out of full screen (never into it)
    Online* online = nullptr;                   // the ONLINE key's; none: the key is not drawn
    Recorder* recorder = nullptr;               // the REC key's; none: the key does nothing
    // The lit skin (surfaces with light and shade, after the 3D model) or the flat drawing it replaced. L on the keyboard changes over.
    void setLit (bool on) { lit = on; warm = false; repaint(); }
    bool isLit() const { return lit; }
    // The lit skin's dark instrument: a graphite case and dark plates, light print, the same lights. D on the keyboard changes over.
    void setGlow (float hue, float bright, float pulse) { glowHue = hue; glowBright = bright; glowPulse = pulse; if (stage != nullptr) stage->setGlow (hue, bright, pulse); }
    void setGlow (float hue, float bright) { setGlow (hue, bright, glowPulse); }
    float glowHueNow() const { return glowHue; }
    float glowBrightNow() const { return glowBright; }
    float glowPulseNow() const { return glowPulse; }
    void setDark (bool on) { dark = on; skin.setDark (on); if (stage != nullptr) stage->setDark (on); warm = false; repaint(); }
    bool isDark() const { return dark; }
    // The 3D stage (3 on the keyboard): the instrument as a solid thing, lit, seen through an eye
    // that can be moved. It is the lit skin's layout and the same hand; only the drawing differs.
    // Asked for, it shows once OpenGL has started; if it cannot start, the lit skin stays and
    // threeNote() says why.
    void setThree (bool on);
    bool isThree() const { return wantThree; }
    bool threeShowing() const { return three; }
    juce::String threeNote() const { return stageNote; }
    Stage* stageOrNull() const { return stage.get(); }
    // Where the eye is kept, whether a drag off the controls may move it (locked: it may not), and
    // whether it goes to what a controller moves.
    struct Eye { Stage::Pose pose; bool locked = true, follow = true; };
    Eye eye() const;
    void setEye (const Eye&);
    void refreshNow() { cancelPendingUpdate(); refreshStage(); }       // the stage brought up to date at once (tests, screenshots)
    std::function<void()> onLook;               // the look was changed from the keyboard or by the hand: keep it for next time
    // The 3D stage is about to be started; and: it is up and has drawn a picture, or has said why
    // it is not, or was put away. (The app keeps a note between the two: see StageGuard.)
    std::function<void()> onThreeStarting, onThreeSettled;
    void sayOnBar (const juce::String& words);  // a few words on the top bar, for a few seconds
    juce::String loadFolder (bool fx) const     { return (fx ? loadFxDir : loadLaneDir).getFullPathName(); }
    void setLoadFolder (bool fx, const juce::String& path) { (fx ? loadFxDir : loadLaneDir) = path.isNotEmpty() ? juce::File (path) : juce::File(); }
    bool testLoadOpen() const                   { return loadOpen; }
    bool testScope (int i) const                { return ((handParts >> i) & 1u) != 0; }      // (a part of the snapshot in hand: lit)
    juce::int64 testHandId() const              { return handId; }
    void showCue (int putOut = -1)              { if (! ses.library.empty()) selSnap = handId = ses.library.back().id; handParts = Session::kAllLanes; if (putOut >= 0) handParts &= ~(1u << putOut); repaint(); }     // (the pictures: the library with a snapshot in hand)
    void showLoad (const juce::String& folder)  { setLoadFolder (cfg.kind != 0, folder); openLoad(); repaint(); }     // (the pictures: LOAD's list, open on that folder)
    bool testWatch (juce::uint32 now, bool inFront) { return watchStart (now, inFront); }
    // (these stand in front of the component's own: in 3D a repaint is the sheet and the parts brought up to date)
    void repaint();
    void repaint (juce::Rectangle<int>);
    // INFO (I on the keyboard): every control that MIDI reaches shows its CC number, a panel its
    // channel, and a control the mouse rests on says what it does and where MIDI finds it.
    void setInfo (bool on) { info = on; hovering = false; tipRect = {}; repaint(); }
    bool isInfo() const { return info; }
    bool testKeysShown() const { return info && keysDrawn; }
    juce::Rectangle<float> testKeysBox() const { return keysBox(); }
    void showConfig (Engine::Target t) { cfgOpen = false; openConfig (t); cfgT = 1.0f; slideX = slideTo = slideGoal(); slideT = 1.0f; repaint(); }     // open CONFIG at once (screenshots, tests)
    void hideConfig() { cfgOpen = false; cfgT = 0.0f; slideX = slideTo = slideGoal(); slideT = 1.0f; repaint(); }                                     // and shut it at once (tests)
    const Parts& testParts() const { return recorded; }                                                                                               // what was last recorded for the 3D stage (tests)
    static void releaseFonts();         // before JUCE shuts down: the embedded typefaces must not outlive it

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray&, int, int) override;

    static constexpr float kW = 1728.0f, kH = 1117.0f;

    // For the offline screen test: every control's place (screen units), kind and index, as last drawn.
    struct TestHit { juce::Rectangle<float> r; int kind, a, b; };
    std::vector<TestHit> testHits() const { std::vector<TestHit> v; for (auto& h : hits) v.push_back ({ h.r, h.kind, h.a, h.b }); return v; }
    static bool testSkips (const TestHit& h) { return h.kind == hAct && (h.a == aAudio || h.a == aFull || h.a == aRec || h.a == aOnline || h.a == aOnlineSetup || h.a == aArrPrint || h.a == aFetch); }
    static bool testIsKnob (const TestHit& h) { return h.kind == hKnob || h.kind == hSnapKnob || h.kind == hRead || h.kind == hLen || h.kind == hDepth || h.kind == hSetup || h.kind == hDisplay || h.kind == hPocket; }
    static bool testIsStep (const TestHit& h) { return h.kind == hStep; }
    static bool testIsConfig (const TestHit& h) { return h.kind == hSetup || h.kind == hDisplay; }
    static bool testIsFile (const TestHit& h)   { return h.kind == hFile; }
    static bool testIsScope (const TestHit& h)  { return h.kind == hPickKey; }        // (a key on the snapshot in hand)
    static bool testIsRecallKey (const TestHit& h) { return h.kind == hAct && h.a == aRecallCue; }
    static bool testIsCard (const TestHit& h)   { return h.kind == hCard; }
    static bool testIsLoadKey (const TestHit& h){ return h.kind == hAct && h.a == aLoad; }
    static bool testIsControl (const TestHit& h, int ctl) { return (h.kind == hKnob || h.kind == hRead) && h.a == ctl; }
    static bool testIsToggle (const TestHit& h, int ctl) { return h.kind == hToggle && h.a == ctl; }
    static bool testIsRec (const TestHit& h) { return h.kind == hAct && h.a == aRec; }
    static bool testIsOnline (const TestHit& h, bool setUp) { return h.kind == hAct && h.a == (setUp ? aOnlineSetup : aOnline); }
    static bool testIsStackKey (const TestHit& h) { return h.kind == hFind && h.a == 0; }      // (whose snapshots are listed: every one, or a stack's. Pressed: the next stack)
    static bool testIsFind (const TestHit& h, int what) { return h.kind == hFind && h.a == what; }     // 0 whose, 1 RECENT, 2 FITS NOW, 3 MORE, 4 LESS, 5 FIND
    void testFinder (int f) { pressFind (f); }
    static bool testIsPlace (const TestHit& h, int k) { return h.kind == hKnobCard && h.a == k; }
    static bool testIsPickKey (const TestHit& h, int index, int part) { return h.kind == hPickKey && h.a == index && h.b == part; }
    // the rack: its shelf (0 the stacks, 1 the knobs kept, 2 the modules, 3 the arrangements) and what is open of it (-1: the bin; else which)
    void testRack (int shelf, int open = -1, int modKnob = -1)
    {
        rackShelf = juce::jlimit (0, 3, shelf); rackFirst = 0; rowSel = -1; rowScroll = 0; addTo = false; tieSel = -1;
        inStack = {}; openKept = {}; openMod = {}; openArr = {}; openModKnob = modKnob;
        if (open < 0) return;
        if (shelf == 0 && open < (int) ses.stacks.size()) inStack = ses.stacks[(size_t) open].id;
        else if (shelf == 1 && open < (int) ses.keptKnobs.size()) openKept = ses.keptKnobs[(size_t) open].id;
        else if (shelf == 2 && open < (int) ses.modules.size()) openMod = ses.modules[(size_t) open].id;
        else if (shelf == 3 && open < (int) ses.arrs.size()) openArr = ses.arrs[(size_t) open].id;
    }
    int testRackShelf() const { return rackShelf; }
    void testLibScroll (int rows) { libScroll = std::max (0, rows); }
    static bool testIsDeleteKey (const TestHit& h) { return h.kind == hAct && h.a == aDelete; }
    static bool testIsRenameKey (const TestHit& h) { return h.kind == hAct && h.a == aRename; }
    int testNamedWhat() const { return namedNow().what; }       // what RENAME would name: 0 nothing, 1 a chip, 2 a knob, 3 a stack, 4 a knob kept, 5 a module, 6 an arrangement
    juce::int64 testNamedChip() const { return namedNow().chip; }
    float testHoldFill (int which) const { return holdFill (which == 0 ? aDelete : which == 1 ? aOpenDelete : which == 2 ? aArrClear : which == 3 ? aNewTrack : aClearKnob); }
    static bool testIsClearKnobKey (const TestHit& h) { return h.kind == hAct && h.a == aClearKnob; }
    int testBenchScroll() const { return benchScroll; }
    static bool testIsAddToKey (const TestHit& h) { return h.kind == hAct && h.a == aAddTo; }
    int testRackOpen() const { return rackOpen(); }          // 0 the bin, 1 a stack, 2 a kept knob, 3 a module, 4 an arrangement
    int testRowSel() const { return rowSel; }
    void testRowSel (int i) { rowSel = i; handId = 0; }
    bool testAddTo() const { return addTo; }
    juce::Rectangle<float> testRowArea() const { return rowArea; }
    static bool testIsWell (const TestHit& h, int kind, int index) { return h.kind == hWell && h.a == kind && (index < 0 || h.b == index); }
    static bool testIsRowCard (const TestHit& h, int index) { return h.kind == hRowCard && (index < 0 || h.a == index); }
    static bool testIsRowKey (const TestHit& h, int index, int part) { return h.kind == hRowKey && h.a == index && h.b == part; }
    static bool testIsBenchChip (const TestHit& h) { return h.kind == hBenchChip; }
    static bool testIsModKnob (const TestHit& h, int k) { return h.kind == hModKnob && h.a == k; }
    static bool testIsTie (const TestHit& h, int i) { return h.kind == hTie && (i < 0 || h.a == i); }       // (a tie of the open macro knob)
    int testTieSel() const { return tieSel; }
    // the keyboard's mark: whether it is out, and the thing it is on (kind 0: nothing)
    bool testKbOn() const { return kbOn; }
    TestHit testKbItem() const { if (const auto* h = kbOn ? kbItem() : nullptr) return { h->r, h->kind, h->a, h->b }; return { {}, 0, 0, 0 }; }
    static bool testIsBinChip (const TestHit& h, int index) { return h.kind == hCard && (index < 0 || h.a == index); }       // (a chip of the bin, in the library)
    static bool testIsPlaceCard (const TestHit& h, int k) { return h.kind == hKnobCard && (k < 0 || h.a == k); }
    int testRackFirst() const { return rackFirst; }
    int testListScroll (int which) const { return which == 0 ? libScroll : which == 1 ? rowScroll : which == 2 ? benchScroll : which == 3 ? colScroll : arrScroll; }
    static bool testIsRackShelf (const TestHit& h, int shelf) { return h.kind == hLibTab && h.a == shelf; }
    // the rack's keys. which: 0 ADD, 1 TAKE OUT, 2 EARLIER, 3 LATER, 4 ADD TO, 5 (none: RENAME is the page's one key), 6 put to work, 7 NEW, 8 COPY, 9 DELETE (held)
    static bool testIsRackKey (const TestHit& h, int which)
    {
        static const int acts[] = { aRowAdd, aRowOut, aRowEarlier, aRowLater, aAddTo, -1, aOpenLoad, aOpenNew, aOpenCopy, aOpenDelete };
        return h.kind == hAct && which >= 0 && which < 10 && h.a == acts[which];
    }
    // (the rack's own: its shelf keys, a pocket, a chip of the open row or one of its keys, a chip seen in what is open, a knob of the open module, the strip's keys for them)
    static bool testIsOfRack (const TestHit& h)
    {
        if (h.kind == hLibTab || h.kind == hWell || h.kind == hRowCard || h.kind == hRowKey || h.kind == hBenchChip || h.kind == hModKnob || h.kind == hTie) return true;
        for (int k = 0; k < 10; ++k) if (k != 4 && k != 5 && testIsRackKey (h, k)) return true;     // (ADD TO is the bin's own key)
        return false;
    }
    void testHand (int index, juce::uint32 parts) { handId = index >= 0 && index < (int) ses.library.size() ? ses.library[(size_t) index].id : 0; handParts = parts & Session::kAllLanes; }
    static juce::Point<float> testPlaceAt (int k) { return placeRect (k).getCentre().translated (0.0f, 38.0f); }       // (on the place, clear of its knob)
    juce::Point<float> testColumnGap (int at) const { return { kColX + 136.0f, kColY + (float) (at - colScroll) * 74 - 4.0f }; }      // (between the column's cards: before the one at that place)
    static int testPickOf (const TestHit& h) { return h.kind == hPick ? h.a : -1; }
    void testFind (const juce::String& t) { findText = t; }
    void testPlain() { testRack (0); inStack = {}; finder = 0; found.clear(); findText = {}; libScroll = 0; handId = 0; handParts = Session::kAllLanes; ses.cue (0); }
    static bool testIsStackCue (const TestHit& h) { return h.kind == hAct && h.a == aStackCue; }
    static bool testIsFetchKey (const TestHit& h) { return h.kind == hAct && h.a == aFetch; }
    int testLibStack() const { return inAt(); }
    void testTick() { timerCallback(); }                        // what the 30-a-second timer does
    void testPointAt (juce::Point<float> p) { point (p); }      // the mouse resting there (screen units)
    juce::String testTip() const { return hovering ? tip.title + "\n" + tip.body.joinIntoString ("\n") + "\n" + tip.midi.joinIntoString ("\n") : juce::String(); }     // what the box says
    juce::Rectangle<int> testTipRect() const { return hovering ? tipRect.getSmallestIntegerContainer() : juce::Rectangle<int>(); }
    static bool testIsPhones (const TestHit& h) { return h.kind == hAct && h.a == aPhones; }
    static bool testIsTrans (const TestHit& h, int which) { return h.kind == hAct && h.a == aTransFire + which; }     // (0 FIRE, 1 ARM, 2 BARS, 3 LEVEL, 4 IN, 5 HIT, 6 DUB, 7 SPACE)
    static bool testIsFinish (const TestHit& h) { return h.kind == hAct && h.a == aFinish; }           // (the master's FINISH plate)
    static bool testIsPlain (const TestHit& h) { return h.kind == hAct && h.a == aSamplePlain; }       // (CONFIG, a lane playing a sample: PLAIN)
    juce::String testCfgNote() const { return cfgNote; }
    static bool testIsArrTrans (const TestHit& h, int part, int slot) { return h.kind == hArrTrans && h.a == part && h.b == slot; }     // (a part's IN, slot 0, or its HIT, 1)
    static bool testIsArrStem (const TestHit& h, int part, int stem) { return h.kind == hArrKey && h.a == part && h.b == stem; }      // (a key on a part of the arrangement)
    static bool testIsSlot (const TestHit& h, int k, int at) { return h.kind == hSlot && h.a == k && h.b == at; }       // (one of the snapshots on a stack knob's card)
    static bool testIsNotesKey (const TestHit& h) { return h.kind == hAct && h.a == aNotes; }      // NOTES: whether a recall brings a snapshot's notes
    // the kinds of thing that say what they are when pointed at with INFO on
    static bool testAnswers (const TestHit& h) { return h.kind == hKnob || h.kind == hToggle || h.kind == hHold || h.kind == hRead || h.kind == hSnapKnob || h.kind == hLane || h.kind == hConfig
                                                     || h.kind == hInsHead || h.kind == hStep || h.kind == hPageSel || h.kind == hLen || h.kind == hAct || h.kind == hPocket; }
    static bool testIsSnapKnob (const TestHit& h, int k) { return h.kind == hSnapKnob && h.a == k; }
    // the performance panel a CONFIG target belongs to: a lane's, an insert's, the returns'
    static bool testIsPanelOf (const TestHit& h, Engine::Target t) { return t.kind == 0 ? h.kind == hLanePanel && h.a == t.lane : h.kind == hDropFx && h.a == t.kind && (t.kind == 2 || h.b == t.slot); }
    juce::Rectangle<float> testConfigRect() const { return cfgRect; }
    // what the timer redraws besides knobs: the meters, the sequencer strip, CONFIG's display
    std::vector<juce::Rectangle<int>> testMovingRects() const { std::vector<juce::Rectangle<int>> v; for (auto r : { masterRect, seqRect, cfgDisplay }) if (! r.isEmpty()) v.push_back (r.getSmallestIntegerContainer().expanded (2)); return v; }
    // for the tests: what the eye is following (empty: nothing), and the tags that are up (a control's index, or -1 INFO's box, -2 the line of words)
    juce::Rectangle<float> testFollowing() const { return followOn ? followBox : juce::Rectangle<float>(); }
    juce::Rectangle<float> testStopRect (Session::Stop s) const { return stopRects[(size_t) Session::placeOf (s)]; }
    bool testEyeHeld() const { return eyeHeld; }
    static bool testIsFront (const TestHit& h, int kind, int slot) { return (h.kind == hFront && h.a == kind && h.b == slot) || (kind == Session::fInsert && h.kind == hDropFx && h.a == 1 && h.b == slot) || (kind == Session::fReturns && h.kind == hDropFx && h.a == 2); }
    static bool testIsLanePanel (const TestHit& h, int lane) { return h.kind == hLanePanel && h.a == lane; }
    std::vector<int> testTags() const { std::vector<int> v; for (const auto& t : tagsUp) v.push_back (t); return v; }
    static bool testIsRun (const TestHit& h) { return h.kind == hAct && h.a == aRun; }
    bool testDescribe (int ctl, juce::String& where, juce::String& label, const char*& what) const { return describe (ctl, where, label, what); }      // what INFO's box says of a control
    static bool testIsPanic (const TestHit& h) { return h.kind == hAct && h.a == aPanic; }
    // the arrangement's page: its key (0), PLAY, FROM HERE, EARLIER, LATER, REMOVE, CLEAR, PRINT; a part's card, its BARS, its MORPH
    static bool testIsArrKey (const TestHit& h, int which) { return h.kind == hAct && h.a == aArrange + which; }        // (0 the page's own, 1 PLAY, 2 FROM HERE, 3 EARLIER, 4 LATER, 5 REMOVE, 6 CLEAR, 7 PRINT, 8 EDIT, 9 TO STACK)
    static bool testIsEdit (const TestHit& h, int which) { return h.kind == hAct && h.a == aEditKeep + which; }         // (EDIT, in the main page's bar: 0 KEEP, 1 DROP)
    static bool testIsArrPart (const TestHit& h, int part, int what = 0) { return h.kind == (what == 0 ? hArrPart : what == 1 ? hArrBars : hArrMorph) && h.a == part; }
    static bool testIsPick (const TestHit& h, int index) { return h.kind == hPick && h.a == index; }
    static bool testIsPickAdd (const TestHit& h, int index) { return h.kind == hPickAdd && h.a == index; }
    // the column: what = 0 the card, 1 a key (b: the part), 2 its number (taken off)
    static bool testIsColumn (const TestHit& h, int what, int index, int b = 0) { return h.kind == hColCard + what && h.a == index && (what == 0 || what == 2 || h.b == b); }
    int testSelPart() const { return selPart; }
    // Legends that are not wholly on the plate they are printed on, as last recorded for the stage (none: as it should be).
    int testLegendCount() const { return (int) legendRects.size(); }
    std::vector<juce::Rectangle<float>> testLegendsOff() const
    {
        std::vector<juce::Rectangle<float>> off;
        for (const auto& l : legendRects)
        {
            const juce::Point<float> at (l.getCentreX(), l.getY() + 3.0f);
            bool on = false, seen = false;
            for (const auto& pl : recorded.plates)
            {
                if (! pl.clip.has (at.x, at.y)) continue;
                seen = true;
                if (pl.r.expanded (0.5f).contains (l)) { on = true; break; }
            }
            if (seen && ! on) off.push_back (l);
        }
        return off;
    }
    void testPickPart (int part) { selPart = part; }
    static bool testIsLearnKey (const TestHit& h, int which) { return h.kind == hAct && h.a == (which == 0 ? aLearn : which == 1 ? aLearnKeep : aLearnDrop); }     // LEARN, KEEP, DROP
    static bool testIsLibraryKey (const TestHit& h) { return h.kind == hAct && h.a == aLibrary; }
    static bool testIsConfigKey (const TestHit& h, int lane) { return h.kind == hConfig && h.a == 0 && h.b == lane * 8; }
    static bool testIsBusConfigKey (const TestHit& h) { return h.kind == hConfig && h.a == 3; }      // the master's C: its drum bus's CONFIG
    static bool testIsPocketsKey (const TestHit& h, int which = 0) { return h.kind == hAct && h.a == (which == 0 ? aPockets : which == 1 ? aPkAuto : aPkReset); }      // POCKETS (on the master); on its page AUTO, RESET
    static bool testIsPocket (const TestHit& h, int lane) { return h.kind == hPocket && h.a == lane; }      // a lane's pocket on the map
    Page testPage() const { return page; }
    double testPrintMs() const { return printMs; }
    double testSendMs() const { return sendMs; }
    void testRenameKnob() { act (aRenameKnob); }
    static bool testIsRackKeep (const TestHit& h) { return h.kind == hRackKeep; }
    void testLearn (int what) { act (what == 0 ? aLearn : what == 1 ? aLearnKeep : aLearnDrop); }      // LEARN, KEEP, DROP
    void testPickKnob (int k) { selKnob = juce::jlimit (0, 15, k); }
    int testKnobChosen() const { return selKnob; }
    int testColSel() const { return colSel; }
    bool testEditing() const { return editor != nullptr; }
    bool testEditorFloats() const { return editor != nullptr && editor->isOnDesktop(); }
    void testEndEdit (const juce::String& text) { if (editor != nullptr) { editor->setText (text, false); endEdit (true); } }
    void testEditDrop() { if (editor != nullptr) endEdit (false); }
    static bool testIsNewTrack (const TestHit& h) { return h.kind == hAct && h.a == aNewTrack; }
    static bool testIsModKey (const TestHit& h) { return h.kind == hAct && h.a == aModules; }       // (LOAD, on the snapshot module's own panel)
    static bool testIsKeptKeep (const TestHit& h) { return h.kind == hKeptKeep; }       // (KEEP KNOB, in the designer)
    juce::String testBarNote() const { return recNote.isNotEmpty() && juce::Time::getMillisecondCounter() - recNoteAt < recNoteMs() ? recNote : juce::String(); }     // what the top bar is saying now
    juce::String testTagText (int ctl) const { const auto it = tagPics.find (ctl); return it != tagPics.end() ? it->second.key : juce::String(); }
    juce::Image testTagPicture (int ctl) { return tagPicture (ctl); }      // (a moved knob's tag, drawn: for a picture of it)

private:
    using G = juce::Graphics;
    using R = juce::Rectangle<float>;
    struct Hit { R r; int kind = 0, a = 0, b = 0; };
    enum HitKind { hKnob = 1, hToggle, hHold, hLane, hInsHead, hStep, hPageSel, hAct, hSnapKnob, hRead, hCard, hSlot, hKnobCard, hPick, hLanePanel, hLen, hDepth, hConfig, hSetup, hDisplay, hBlock, hDropFx, hFile, hScope,
                   hFront,          // a module's own plate, off its controls: a press brings it to the front (a: which kind of module, b: its slot)
                   hPocket,         // POCKETS: a lane's pocket on the map (a: the lane): dragged, left and right its HOME, up and down its WIDTH
                   hArrPart, hArrBars, hArrMorph,       // the arrangement: a part (a: which), chosen; its BARS and its MORPH, dragged
                   // the column (the chosen stack knob's stack, in the designer). a: which of its snapshots
                   hColCard,        // the card itself: heard in the headphones, with the parts of the rig it takes
                   hColKey,         // one of its keys (b: the part: a lane, then the returns, the drum bus, the key): put out or lit
                   hColOut,         // its number: taken off the stack
                   hPickAdd,        // a chip's ADD key in the rack's list (a: which snapshot of the library): onto the chosen stack knob, or into the arrangement
                   hKeptKeep, hRackKeep,       // the chosen knob's KEEP plates, in the designer: the knob into the knobs kept; a stack knob's pile into the stacks
                   hFind,
                   hPickKey,        // a key on the snapshot in hand, beside the knob (a: which snapshot of the library; b: the part): what goes with it when it is added
                   hArrKey,         // a key on a part of the arrangement (a: the part; b: the part of the rig)
                   hLibTab,         // the rack's shelf keys (a: 0 the stacks, 1 the knobs kept, 2 the modules, 3 the arrangements)
                   // the rack (the library): pockets along the top, and what is open lying out on the bench under them
                   hWell,           // a pocket (a: 0 the bin, 1 a stack, 2 a kept knob, 3 a module, 4 an arrangement, 5 a new stack; b: which): pressed, open
                   hRowCard,        // a chip in the open stack's row (a: which): in hand, and heard with the parts it takes; carried, moved or taken out
                   hRowKey,         // one of its keys (b: the part, N last)
                   hBenchChip,      // a chip seen in what is open (a kept knob's, a module's knob's, an arrangement's part; a: its place in the library, b: the parts it takes there): heard
                   hModKnob,        // a knob of the open module (a: which of the sixteen): its pile, or what it is tied to, under the row
                   hArrTrans,       // a part's TRANSITION plate (a: the part)
                   hTie };          // a tie of the open macro knob, in the library (a: which): chosen; dragged up or down, how far it takes its knob
    enum Act { aRun, aPanic, aAudio, aSnapshot, aLibrary, aMacros, aMain, aNudgeL, aNudgeR, aRatchet, aNote, aOctave, aGen0, aGen1,
               aRename, aDelete, aRenameKnob, aClearKnob, aLearn, aLearnKeep, aLearnDrop, aFull, aCfgClose, aNewTrack, aModules, aClipReset, aRec, aRecallCue, aSamplePlain,
               aTransFire, aTransArm, aTransBars, aTransLevel, aTransLead, aTransHit, aTransDub, aTransSpace,
               aFinish,
               aOnline, aOnlineSetup, aPhones, aLoad, aLoadUp, aNotes,
               aPockets, aPkAuto, aPkReset,
               aArrange, aArrPlay, aArrHere, aArrEarlier, aArrLater, aArrRemove, aArrClear, aArrPrint, aArrEdit, aArrStack,
               aEditKeep, aEditDrop,
               aFetch,
               aStackCue,
               // the rack: the chip in hand (into the open stack; out of it; earlier, later; into another pocket), and the open thing itself
               aRowAdd, aRowOut, aRowEarlier, aRowLater, aAddTo, aOpenLoad, aOpenNew, aOpenCopy, aOpenDelete };

    void timerCallback() override;
    void handleAsyncUpdate() override;
    void dirty (R);
    // the 3D stage
    void stageState();                  // it has started, or could not
    void refreshStage();                // the parts recorded and the sheet printed, as the screen now stands
    void drawAll (G&);                  // the whole screen, to whatever is being drawn on
    juce::Point<float> where (const juce::MouseEvent&) const;      // the place on the screen (units) under the pointer
    float pointsPerUnit() const { return std::min ((float) getWidth() / kW, (float) getHeight() / kH); }
    std::unique_ptr<Stage> stage;
    bool three = false, wantThree = false, recording = false, printing = false;
    Parts recorded;
    juce::RectangleList<int> sheetDirty;
    bool sheetAll = true;
    int knobCtl = -1;                   // the control the knob about to be drawn shows (its lights then follow the engine themselves)
    juce::Colour knobLight;
    juce::Point<float> pressAt, eyeLast;
    int eyeDrag = 0;                    // the hand is on the view: 1 turning it, 2 sliding it
    Stage::Pose eyePose;
    bool eyeLocked = true, eyeFollow = true;
    juce::String stageNote;
    // Following: the eye goes to the module of a knob that a hand on the controller turns, and
    // out to hold every knob a snapshot is moving. A sequencer's own moves, and anything that is
    // not a knob (keys, the tempo under an outside clock), never move it.
    void followMoves();
    R panelOf (juce::Point<float>) const;       // the plate a place is on (or a little room round the place)
    std::array<float, cx::kCount> followWas {};
    std::array<bool, cx::kCount> bySequencer {};
    juce::uint32 followAt = 0;
    juce::uint32 startingSince = 0;             // when the stage was first seen still starting, in front
    float glowHue = Stage::kGlowHue, glowBright = Stage::kGlowBright, glowPulse = 0.0f;
    bool settledSaid = true;
    void settled();
    bool watchStart (juce::uint32 now, bool inFront);
    bool followOn = false;
    R followBox;
    // The tags over the stage: a moved knob's name and value beside it, INFO's box by the pointer,
    // and a line of words at the foot of the window for a moment.
    void showTags();
    void say (const juce::String&);
    juce::Image tagPicture (int ctl);
    struct TagPic { juce::String key; juce::Image image; juce::uint32 stamp = 0; };
    std::map<int, TagPic> tagPics;
    juce::String tagsWere, sayText;
    juce::uint32 sayAt = 0, tagStamp = 1;
    juce::Point<float> hoverAt;
    // INFO also lists the keyboard's keys: at the foot of the screen on the left, and on the right
    // while the pointer is where that would be (so nothing under it is out of INFO's reach)
    bool keysRight = false, keysDrawn = false;
    juce::Rectangle<float> keysBox() const;
    void drawKeys (G&, juce::Rectangle<float> box, bool onStage);
    std::vector<int> tagsUp;
    double printMs = 0.0, sendMs = 0.0;         // the last printing of the sheet: drawing it, and handing it to the stage
    juce::Point<float> logical (juce::Point<float> p) const { return p.transformedBy (xf.inverted()); }
    const Hit* hitAt (juce::Point<float>) const;
    bool vis (G&, R) const;         // is it in what is being painted (lit: or near enough for its shade to be)
    bool seen (G&, R) const;        // is it itself in what is being painted

    // drawing kit
    void text (G&, const juce::String&, R, const juce::Font&, juce::Colour, juce::Justification = juce::Justification::centred);       // (black is print: light on the dark instrument)
    void textRaw (G&, const juce::String&, R, const juce::Font&, juce::Colour, juce::Justification = juce::Justification::centred);    // in exactly that colour
    bool night() const { return lit && dark; }                  // the dark instrument is what is drawn
    juce::Colour tone (juce::Colour) const;                     // a colour of the light instrument as the dark one has it: print, a display's ground, a plate
    juce::Colour glass (juce::Colour) const;                    // a pane's
    juce::Colour cap (juce::Colour) const;                      // a key's
    juce::Colour inkOn (juce::Colour surface, juce::Colour asked) const;       // letters on a surface of that colour
    float tw (const juce::Font&, const juce::String&) const;
    float plate (G&, float x, float y, const juce::String&, const juce::Font&, float padX, juce::Colour bg, juce::Colour fg, float h = 26.0f);
    void legend (G&, float cx, float y, const juce::String&);
    void knob (G&, float cx, float cy, int size, float v, Kind, int steps, juce::Colour lit, float target = -1.0f);
    void hit (R, int kind, int a = 0, int b = 0);
    void headKeys (G&, float right, float y, int kind, int lane, int slot, int muteCtl, int soloCtl);
    void hollow (G&, float cx, float cy, float d, bool round);
    // A key. on: it is latched (lit, it sits low until it is pressed again). Under the hand it is
    // all the way in, whatever kind it is (a thick border was the flat drawing's way to say so).
    void key (G&, R, juce::Colour fill, float border = 2.0f, bool on = false);
    bool pressed (int act) const;       // an action's key: the hand is on it, or it was just now pressed from somewhere else
    void flatKey (G&, R, juce::Colour fill, float border = 2.0f);
    void window (G&, R, juce::Colour glass);                    // a display's pane
    void tag (G&, R, const juce::String&);                      // a number, light on dark
    float title (G&, float x, float y, const juce::String&, bool inFront = false);     // a panel's name (lit up while the panel is in front)
    void card (G&, R, bool selected);
    bool off (G& g, R r) const { return ! collect && ! vis (g, r); }       // a part-paint that does not touch it
    // INFO
    struct Tip { juce::String title; juce::StringArray body, midi; R box; bool any = false; };
    Tip tipFor (const Hit&) const;
    bool describe (int ctl, juce::String& where, juce::String& label, const char*& what) const;
    juce::String midiOf (int ctl) const;
    juce::StringArray wrap (const juce::String&, const juce::Font&, float width) const;
    void point (juce::Point<float>);
    void drawTip (G&);
    void number (G&, int ctl, R on, juce::Colour ink);          // a control's CC number, on it
    float chan (G&, float x, float y, int channel);             // a panel's channel; the width it took (none with INFO off)
    void lamp (G&, float cx, float cy, juce::Colour fill, float d = 12.0f);
    void readout (G&, R, const juce::String& label, const juce::String& value);
    void panel (G&, R, juce::Colour ground);
    void control (G&, int ctl, float cellX, float y, float cellW, juce::Colour lit, int size = 40, float legendH = 28.0f);
    void actKey (G&, int act, float cellX, float y, float cellW, const juce::String& label, juce::Colour fill, float legendGap = 3.0f, bool present = true, bool on = false);

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
    void drawColumn (G&);               // the chosen stack knob's stack, as cards in its order, each with its keys
    void drawArrange (G&);
    juce::String arrangeSig() const;        // what the arrangement's page shows that moves by itself: when it differs, the page is drawn again
    juce::String arrSig;
    int selPart = -1, arrScroll = 0;
    // ---- the things kept, by kind: what an open pocket's keys (NEW, COPY, put to work, DELETE) do is done here, for the
    // one that is open (rackOpenDo). A kind's own list is the session's; the kept knobs of both kinds are one list there.
    enum ShelfKind { skStacks, skMacroKnobs, skStackKnobs, skModules, skArrs, skKinds };
    std::array<int, skKinds> shelfSel { { -1, -1, -1, -1, -1 } };
    R shelfNameRect;                            // where the open one's name is drawn (RENAME types over it)
    // ---- the rack (DECISIONS 602, 603): the library as pockets along the top and a bench under them. The first pocket is
    // the bin, which holds every chip; then the things of the shelf chosen. A pocket pressed is open: what it holds lies out
    // on the bench as chip cards. The open stack is inStack (the same one every page's list of chips goes by).
    int rackShelf = 0;                          // beside the bin: 0 the stacks, 1 the knobs kept, 2 the modules, 3 the arrangements
    int rackFirst = 0;                          // the first of them shown (the wheel, over the rack)
    juce::String openKept, openMod, openArr;    // the kept knob, the module, the arrangement that is open (its id)
    int openModKnob = -1;                       // a module open: the knob of it whose pile is out
    int rowSel = -1, rowScroll = 0;             // the open stack's row: the chip in hand there, and the first row of it shown
    bool addTo = false;                         // ADD TO is lit: the next pocket pressed takes the chip in hand
    struct WellAt { R r; int kind, index; };
    std::vector<WellAt> wellsAt;                // (as last drawn: where a pocket is, for what is carried)
    R rowArea;                                  // (as last drawn: the open stack's row)
    int rackOpen() const;                       // what is open: 0 the bin, 1 a stack, 2 a kept knob, 3 a module, 4 an arrangement
    int openKeptAt() const; int openModAt() const; int openArrAt() const;
    int rackCount() const;                      // how many things the shelf holds
    juce::Colour chipColour (juce::int64 id) const;   // a chip's edge: as hot as it is bright
    void well (G&, R);
    void drawPile (G&, R well, const std::vector<juce::int64>& ids, const juce::String& top, int most = 10);
    // Where the rack stands on the page being drawn. The library has the whole page: the shelf keys down its left, eight
    // pockets across, six cards across on the bench. The designer and the arrangement's page have it as their right-hand
    // third: the shelf keys in a row, three pockets, two cards.
    struct RackPlace { float x = 23.0f, w = 1682.0f, y = 80.0f; int wells = 8, across = 6; bool wide = true; };
    RackPlace rp;
    float benchY() const { return rp.wide ? 246.0f : rp.y + 200.0f; }
    R rackArea;                                 // (as last drawn: the pockets, for the wheel)
    R openNameRect;                             // where the open thing's name is drawn (its RENAME types over it)
    void rackTidy();                            // what was open and is no longer kept is shut
    // The open thing's name (green) and, after it, its own keys as plates: the same keys in the same place on every page
    // that has the rack. Where what it holds begins.
    float openHead (G&, float top, const juce::String& name, const juce::String& says);
    void headKey (G&, float& x, float& y, int act, const juce::String& legend, juce::Colour, bool can = true);
    void drawRackBench (G&);                    // the rack as a third of a page: what the open pocket holds, under it
    int benchScroll = 0;                        // what an open knob, module or arrangement holds, when it is more than the bench has room for: the first line shown
    R benchArea;                                // (as last drawn: where that is, for the wheel)
    // Type on a plate's glass: black on a light glass (yellow and green are light on either instrument), the instrument's own ink on a dark one.
    void onGlass (G&, const juce::String&, R, const juce::Font&, juce::Colour glassOf, juce::Colour asked, juce::Justification = juce::Justification::centred);
    // ---- RENAME: one key, in the same place on every page that has it, for the thing last pressed that has a name: a chip, a
    // knob of the module, a stack, a knob kept, a module, an arrangement. With nothing pressed on the page yet, the page's own:
    // the chosen knob in the designer, the arrangement on its page, the chosen chip in the library.
    struct Named { int what = 0; juce::int64 chip = 0; int knob = -1; juce::String id; };      // what: 0 nothing, 1 a chip, 2 a knob, 3 a stack, 4 a knob kept, 5 a module, 6 an arrangement
    Named named;
    struct NameAt { Named n; R r; };
    std::vector<NameAt> namesAt;                // (as last drawn: where each name is, for the box that is typed in)
    void nameAt (int what, juce::int64 chip, int knob, const juce::String& id, R r) { namesAt.push_back ({ Named { what, chip, knob, id }, r }); }
    juce::Point<float> namedAt;                 // (where the hand was when it pressed it: a chip shown twice is named where it was pressed)
    void nameIs (int what, juce::int64 chip = 0, int knob = -1, const juce::String& id = {}) { named = Named { what, chip, knob, id }; namedAt = drag.start; }
    R chipNameRect (R card, int number) const;  // where a chip's card has its name
    bool namedThere (const Named&) const;       // it is still kept
    Named namedNow() const;                     // what RENAME would name now
    void renameNamed();
    // ---- a key that is held: it fills with light while the hand is on it, and what it does is done when it is full
    static constexpr juce::uint32 kHoldMs = 700;
    static bool heldAct (int a)         { return a == aDelete || a == aOpenDelete || a == aArrClear || a == aNewTrack || a == aClearKnob; }
    float holdFill (int a) const;               // how full that key is (0: the hand is not on it)
    R holdRect;                                 // (as last drawn: the key being held, for the 3D stage's light)
    R holdDirty;                                // (the held key or plate, to be drawn again as it fills)
    bool holdLit = false;                       // (the stage has been told of a light)
    // ---- the keyboard, where the rack is (LIBRARY, MACROS, ARRANGE): a mark (four yellow corners) that the arrow keys move over the things that
    // are pressed there: a pocket, a chip, a knob of an open module, a tie; in the designer a place and a card of the chosen
    // knob's pile too, beside the arrangement a part. RETURN presses what the mark is on (SHIFT: a chip to the set); = adds
    // the chip it is on, BACKSPACE takes out, [ and ] move earlier and later (a tie: less and more); TAB is the next shelf.
    // The mouse puts the mark away.
    bool kbOn = false;
    juce::Point<float> kbAt;                    // where the mark is: the thing under that place has it
    R kbRect;                                   // (as last drawn: that thing)
    int kbSeekKind = 0, kbSeekA = -1;           // (the mark goes to that thing when the page is next drawn: a chip that was moved)
    struct ScrollMost { int bin = 0, row = 0, bench = 0, col = 0, arr = 0; } most;      // (as last drawn: how far each list goes)
    bool rackPage() const { return page == pLibrary || page == pMacros || page == pArrange; }
    bool kbIs (const Hit&) const;               // the mark can stand on it
    const Hit* kbItem() const;                  // what it is on (none: nothing on the page takes it)
    const Hit* kbToward (const Hit& from, int dx, int dy, bool sameKind, bool inLineOnly) const;
    void kbMove (int dx, int dy);
    void kbChoose (const Hit&);                 // the mark has come to it: where a thing is chosen by being pressed, it is the chosen one
    void kbSeek (int kind, int a) { kbSeekKind = kind; kbSeekA = a; }
    bool kbKey (const juce::KeyPress&);         // a key, where the rack is: taken, or not the rack's
    bool kbStale = false;                       // (a key has changed the page since it was last laid out)
    void layoutNow();
    void tieStep (int i, int by);               // a tie, that many hundredths less or more
    void drawKb (G&);
    void downOn (Hit, juce::Point<float>);      // the hand comes down on a control
    void release (const juce::MouseEvent*);     // and is taken off (no mouse: a key pressed it, where the mark is)
    void tap (const Hit&);                      // pressed and let go, by a key
    bool recallHit (const Hit&);                // a chip to the set, as two quick presses take it
    void drawPickList (G&, float top);          // the chips listed there (the bin's, or the open stack's): pressed, heard; ADD, or carried, into what the page is making
    void drawWell (G&, R, int kind, int index);
    void drawChipRack (G&);
    // The pile that is out in a row on the library's bench, to be changed there: the open stack's, an open stack knob's kept
    // by itself, or that of the knob pressed in an open module. (On the other pages the rack is for taking from: none.)
    Session::PileAt rowAt() const;
    // The macro knob whose ties are out on the library's bench, to be changed there: one kept by itself, or the macro knob
    // pressed in an open module. (On the other pages the rack is for taking from: none.)
    Session::PileAt tiesAt() const;
    int tieSel = -1;                            // the tie last pressed there (TAKE OUT is for it)
    void rowKeys (G&, float& x, float& y, const Session::PileAt&, int n);       // ADD, TAKE OUT, EARLIER, LATER: what the hand does in that row
    float drawStackRow (G&, float top);         // that pile's chips, in order; where the bin under them begins
    void drawRowCard (G&, R, int i, juce::int64 id, juce::uint32 keys, bool can);
    void drawSeenCard (G&, R, juce::int64 id, int num, juce::uint32 keys, const juce::String& note);
    void drawKeptOpen (G&, float top);
    float drawModOpen (G&, float top);          // (where its knobs end: a pile out in a row begins there)
    void drawArrOpen (G&, float top);
    void drawKnobHolds (G&, float top, const MacroKnob&, bool stack);
    int rowSlotAt (juce::Point<float>) const;   // between which chips of the open stack's row (-1: not over it)
    const WellAt* wellAt (juce::Point<float>) const;
    void rackDrop (juce::Point<float>);         // what was carried in the library is let go
    void openWell (int kind, int index);
    void rackSay (const juce::String&);
    void rackOpenDo (int what);                 // the strip's keys for what is open: 0 NEW, 1 COPY, 2 put to work, 3 RENAME, 4 DELETE
    int shelfCount (int kind) const;
    int shelfItem (int kind, int i) const;      // its place in the session's own list (the kept knobs of both kinds are one list); -1: none
    juce::String shelfName (int kind, int i) const;
    int shelfHere (int kind) const;             // the one on the bench: the module in the chassis, the arrangement on the page (-1: the kind has no bench)
    bool shelfHas (int kind, int key) const;    // the kind has that key at all
    bool shelfCan (int kind, int key) const;    // and it can be pressed for the chosen one now
    void shelfDo (int kind, int key);           // NEW, COPY, LOAD, RENAME (DELETE is held: shelfDelete)
    void shelfDelete (int kind);
    bool oneLine = false;               // legends drawn now stay on one line (there is no room on the plate under them for two)
    std::vector<R> legendRects;         // where every legend was printed, at the last record of the parts (the checks)
    bool arrLit = false;
    juce::String motionSig() const;     // MOTION's strip, as words
    juce::String motSig;
    void drawPockets (G&);
    void drawPocketMap (G&, int part, R);       // one row's map: the lane's pocket, what it is playing, what is taken from it
    void drawConfig (G&, R);
    void openConfig (Engine::Target);
    void drawSnapCard (G&, const Snapshot&, int index, R, bool selected, int hitKind);

    void act (int a);
    void stepTap (int i);
    void beginEdit (R where, const juce::String& initial, std::function<void (juce::String)> done);
    void endEdit (bool keep);

    Session& ses;
    Engine& eng;
    Skin skin;
    bool lit = true, dark = false, warm = false;      // warm: the plates that are not on the first screen have been made
    bool info = false, hovering = false;
    Hit hover;
    Tip tip;
    R tipRect;
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
    int seqMovesWas = 0;                // what KEY moved the shown lane's steps by when the sequencer was last drawn
    bool cueLitWas = false;             // CUE (the stack knobs') as it last was: lit with nowhere for a cue to play is said
    juce::int64 cueLackedWas = 0;       // the snapshot last said to have no sound kept
    juce::uint32 onlineStamp = 0;               // the online status last drawn
    bool loadAsked = false, loadSeen = false, loadBusy = false;     // LOAD pressed on the snapshot module: asked for, seen at work, its key lit
    juce::String loadWas;                       // (what LOAD had last said when it was asked for)
    juce::String recNote;                       // what the recorder last had to say, shown for a few seconds
    juce::uint32 recNoteAt = 0;
    int cfgE16Was = -1;                         // the E16's page of CONFIG as the panel last followed it
    int takesWas = 0;                           // the session's count of snapshots taken, as last said on the bar
    juce::uint32 recNoteMs() const              { return recNote.startsWith ("SNAPSHOT TAKEN") ? 3500u : 8000u; }      // how long the bar's note stays
    juce::uint64 barNow() const;
    juce::String learnTitle() const;    // the top bar's plate while a knob is being taught
    float learnKeysX() const;           // where KEEP stands in the top bar (DROP is 96 on)
    int lastStep = -1;

    struct Drag { int kind = 0, a = 0, b = 0, page = 0; juce::Point<float> start; float v0 = 0, g0 = 0; int r0 = 1; bool moved = false; char axis = 0; juce::uint32 t0 = 0; bool fired = false; } drag;
    juce::int64 selSnap = 0;
    // The library, with a snapshot cued or chosen: what RECALL will take from it. All of it, unless
    // parts are put out first; and all again for the next (0 to 10: the lanes; 11: the returns; 12: the key).
    int selKnob = 0, libScroll = 0, colScroll = 0;
    juce::String inStack;               // the snapshots listed are this stack's (its id; empty: every snapshot)
    int inAt() const                    { return ses.rackIndex (inStack); }
    bool columnShown() const            { return Session::isStack (selKnob); }      // (the chosen stack knob's own pile, out on the designer's bench)
    int colSel = -1;                    // the chip of that pile last pressed (TAKE OUT, EARLIER and LATER are for it)
    static constexpr float kColX = 23.0f, kColHead = 246.0f, kColY = 312.0f;       // the column: its left edge, its head, its first card
    static constexpr int kColRows = 8;
    void drawKnobDetail (G&, R);        // the chosen knob, large: what it is, what it moves, its turn, KEEP
    // Which of them, and in what order: 0 as they are kept, 1 RECENT (newest first), 2 FITS NOW (nearest what is playing first),
    // 3 MORE and 4 LESS (a step up or down in energy from it). What 2 to 4 found when pressed stays put. And only names holding findText.
    int finder = 0;
    std::vector<juce::int64> found;
    juce::String findText;
    R findRect;
    // TRANSITION, in the bar on the main and the macro pages: FIRE, ARM under its name, and what it plays in three columns
    float transX() const;
    bool transShown() const { return (page == pMain || page == pMacros) && ses.learning < 0 && ! info && ! (page == pMain && ses.arrEdit >= 0); }
    bool editShown() const { return page == pMain && ses.learning < 0 && ses.arrEdit >= 0; }         // EDIT's line and its two keys, in the bar
    juce::String editTitle() const;
    float editKeysX() const;
    void drawTrans (G&, float x);
    float drawFind (G&, float x, float w, float y);     // the row (two, where it is narrow) at the head of the snapshots; where their cards begin
    void pressFind (int what);
    // The snapshot in hand, beside the knob: the one last pressed there, and the parts of the rig chosen on its card. They go
    // with it when it is added (ADD, or carried into the column or onto a place).
    juce::int64 handId = 0;
    juce::uint32 handParts = Session::kAllLanes;
    // Carrying: a card pressed and moved goes with the pointer (a snapshot of the list, a card of the column, a stack, a kept
    // knob), and is put where it is let go: between two cards of the column, or on a place.
    juce::Point<float> carryAt;
    bool carrying() const               { return mouseIsDown && drag.moved && ((page == pMacros && (drag.kind == hPick || drag.kind == hColCard || drag.kind == hWell)) || (page == pArrange && (drag.kind == hPick || drag.kind == hWell)) || (page == pLibrary && (drag.kind == hCard || drag.kind == hRowCard))); }
    static R arrArea()                  { return R (23.0f, 74.0f, 1118.0f, 900.0f); }      // (the arrangement, on its page: where what is carried from the rack is let go)
    int columnPlaceAt (juce::Point<float>) const;       // between which cards of the column that is (0: before the first). -1: not over the column
    static int placeAt (juce::Point<float>);            // which place's card (-1: none)
    // (the module in the chassis, along the top of the designer: its fourteen knobs in a row; 13 and 16 hold none)
    static int placeOrder (int k)       { return k < 0 || k > 15 || Session::isCorner (k) ? -1 : k < 12 ? k : k - 1; }
    static R placeRect (int k)          { const int q = placeOrder (k); return q < 0 ? R() : R (23.0f + (float) q * 80.0f, 112.0f, 76.0f, 96.0f); }
    bool placeTakes (int k) const;                      // what is carried can go on that place
    void drop (juce::Point<float>);
    void putKept (int index, int k);
    void heardSays (juce::int64 id, bool has);
    void drawCarry (G&);
    std::vector<int> listed() const;    // the snapshots it lists, each by its place in the library
    bool mouseIsDown = false;
    static constexpr juce::uint32 kTapMs = 120;
    int tapAct = -1; juce::uint32 tapAt = 0;       // the action last done, and when: its key stays down this long
    // CONFIG: the setup panel that slides in from the left. The top three rows move so that the
    // panel being set up (a lane, an insert, the returns) stands right beside it; their six columns
    // go round, so what leaves one edge of the screen comes back at the other.
    float shift = 0.0f;
    float slideX = 0.0f, slideFrom = 0.0f, slideTo = 0.0f, slideT = 1.0f;
    float slideGoal() const;
    R rowsClip;
    Engine::Target& cfg;                // (the session's: a controller opens CONFIG too)
    bool& cfgOpen;
    bool cfgStale = true;
    Engine::Target cfgSeen;             // what it was open on when last looked at, to see a change made from outside
    bool cfgOpenSeen = false;
    // What is in front (the session's), as the screen has it: where each of the pointer's stops
    // stands as last drawn, the frame round the one the pointer is on, and the eye going to it.
    std::array<R, Session::kPlaces> stopRects;
    void stopAt (int place, R drawn);
    R frontBox() const;                 // what the eye holds: the module in front, with CONFIG if it is open
    int frontSeen = 0;
    // The eye stays on what was last chosen (by a key or a controller) until something else is, or
    // CONFIG is opened or shut, or the pointer is taken up (it goes home, to show every module),
    // or Esc, V, M or a hand on the view lets it go.
    bool eyeHeld = false, heldBefore = false, wasPointing = false;
    juce::uint32 knobAt = 0;            // when a knob under a hand (or a snapshot's sweep) last had the eye
    bool knobHas = false;
    float cfgT = 0.0f;
    int cfgScroll = 0, cfgAge = 0;
    std::vector<Engine::SetupParam> cfgParams;
    // LOAD: a list of folders and sound files in CONFIG's own panel, for a lane, an effect or a
    // return that takes a file. No window of the system's is opened, so nothing leaves full screen.
    bool loadOpen = false;
    juce::File loadDir, loadLaneDir, loadFxDir;     // where it is looking; where a lane's file and an effect's were last taken from
    juce::Array<juce::File> loadFolders, loadFiles;
    int loadScroll = 0;
    void openLoad();
    void readLoad();
    void pickLoad (int index);
    void drawLoad (G&, R area);
    // POCKETS: what the engine last said of each part's bands, as drawn (so a row is drawn again only when it has moved)
    Pockets::Seen pkSeen;
    std::array<R, Pockets::kParts> pkMaps;
    std::array<juce::uint64, Pockets::kParts> pkSig {};
    std::array<float, kLanes> pkSaid {};        // each lane's HOME and WIDTH as last written beside its name
    juce::uint64 pocketSig (int part) const;
    juce::String pkNote;                // what AUTO last had to say, for a few seconds
    juce::uint32 pkNoteAt = 0;
    juce::String cfgName, cfgNote, cfgStatus;
    juce::String saysPath; Session::SampleSays saysOf;      // (the lane's file in CONFIG: what it is heard to be, read when the file changes)
    R cfgDisplay, cfgRect;
    std::unique_ptr<juce::TextEditor> editor;
    std::function<void (juce::String)> editDone;
};
} // namespace sf0
