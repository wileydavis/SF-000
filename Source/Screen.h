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

    enum Page { pMain, pLibrary, pMacros, pPockets };
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
    bool testScope (int i) const                { return scopeOn[(size_t) i]; }
    bool testCueMode() const                    { return cueMode; }
    void showCue (int putOut = -1)              { cueMode = true; if (! ses.library.empty()) selSnap = ses.library.back().id; scopeFor = selSnap; scopeOn.fill (true); if (putOut >= 0) scopeOn[(size_t) putOut] = false; repaint(); }     // (the pictures: the library with a snapshot chosen for RECALL)
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
    static bool testSkips (const TestHit& h) { return h.kind == hAct && (h.a == aAudio || h.a == aFull || h.a == aRec || h.a == aOnline || h.a == aOnlineSetup); }
    static bool testIsKnob (const TestHit& h) { return h.kind == hKnob || h.kind == hSnapKnob || h.kind == hRead || h.kind == hLen || h.kind == hDepth || h.kind == hSetup || h.kind == hDisplay || h.kind == hPocket; }
    static bool testIsStep (const TestHit& h) { return h.kind == hStep; }
    static bool testIsConfig (const TestHit& h) { return h.kind == hSetup || h.kind == hDisplay; }
    static bool testIsFile (const TestHit& h)   { return h.kind == hFile; }
    static bool testIsScope (const TestHit& h)  { return h.kind == hScope; }
    static bool testIsCueKey (const TestHit& h) { return h.kind == hAct && h.a == aCue; }
    static bool testIsRecallKey (const TestHit& h) { return h.kind == hAct && h.a == aRecallCue; }
    static bool testIsCard (const TestHit& h)   { return h.kind == hCard; }
    static bool testIsLoadKey (const TestHit& h){ return h.kind == hAct && h.a == aLoad; }
    static bool testIsControl (const TestHit& h, int ctl) { return (h.kind == hKnob || h.kind == hRead) && h.a == ctl; }
    static bool testIsToggle (const TestHit& h, int ctl) { return h.kind == hToggle && h.a == ctl; }
    static bool testIsRec (const TestHit& h) { return h.kind == hAct && h.a == aRec; }
    static bool testIsOnline (const TestHit& h, bool setUp) { return h.kind == hAct && h.a == (setUp ? aOnlineSetup : aOnline); }
    void testTick() { timerCallback(); }                        // what the 30-a-second timer does
    void testPointAt (juce::Point<float> p) { point (p); }      // the mouse resting there (screen units)
    juce::String testTip() const { return hovering ? tip.title + "\n" + tip.body.joinIntoString ("\n") + "\n" + tip.midi.joinIntoString ("\n") : juce::String(); }     // what the box says
    juce::Rectangle<int> testTipRect() const { return hovering ? tipRect.getSmallestIntegerContainer() : juce::Rectangle<int>(); }
    static bool testIsPhones (const TestHit& h) { return h.kind == hAct && h.a == aPhones; }
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
    void testLearn (int what) { act (what == 0 ? aLearn : what == 1 ? aLearnKeep : aLearnDrop); }      // LEARN, KEEP, DROP
    void testPickKnob (int k) { selKnob = juce::jlimit (0, 15, k); }
    bool testEditing() const { return editor != nullptr; }
    bool testEditorFloats() const { return editor != nullptr && editor->isOnDesktop(); }
    void testEndEdit (const juce::String& text) { if (editor != nullptr) { editor->setText (text, false); endEdit (true); } }
    juce::String testTagText (int ctl) const { const auto it = tagPics.find (ctl); return it != tagPics.end() ? it->second.key : juce::String(); }

private:
    using G = juce::Graphics;
    using R = juce::Rectangle<float>;
    struct Hit { R r; int kind = 0, a = 0, b = 0; };
    enum HitKind { hKnob = 1, hToggle, hHold, hLane, hInsHead, hStep, hPageSel, hAct, hSnapKnob, hRead, hCard, hSlot, hKnobCard, hPick, hLanePanel, hLen, hDepth, hConfig, hSetup, hDisplay, hBlock, hDropFx, hFile, hScope,
                   hFront,          // a module's own plate, off its controls: a press brings it to the front (a: which kind of module, b: its slot)
                   hPocket };       // POCKETS: a lane's pocket on the map (a: the lane): dragged, left and right its HOME, up and down its WIDTH
    enum Act { aRun, aPanic, aAudio, aSnapshot, aLibrary, aMacros, aMain, aNudgeL, aNudgeR, aRatchet, aNote, aOctave, aGen0, aGen1,
               aRename, aDelete, aRenameKnob, aClearKnob, aLearn, aLearnKeep, aLearnDrop, aFull, aCfgClose, aClipReset, aRec, aCue, aRecallCue, aOnline, aOnlineSetup, aPhones, aLoad, aLoadUp, aNotes,
               aPockets, aPkAuto, aPkReset };

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
    void key (G&, R, juce::Colour fill, float border = 2.0f);
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
    juce::uint32 onlineStamp = 0;               // the online status last drawn
    bool cueMode = false;                       // the library: a tap is heard in the headphones, not recalled
    juce::String recNote;                       // what the recorder last had to say, shown for a few seconds
    juce::uint32 recNoteAt = 0;
    juce::uint64 barNow() const;
    juce::String learnTitle() const;    // the top bar's plate while a knob is being taught
    float learnKeysX() const;           // where KEEP stands in the top bar (DROP is 96 on)
    int lastStep = -1;

    struct Drag { int kind = 0, a = 0, b = 0; juce::Point<float> start; float v0 = 0, g0 = 0; int r0 = 1; bool moved = false; char axis = 0; juce::uint32 t0 = 0; bool fired = false; } drag;
    juce::int64 selSnap = 0;
    // The library, with a snapshot cued or chosen: what RECALL will take from it. All of it, unless
    // parts are put out first; and all again for the next (0 to 10: the lanes; 11: the returns; 12: the key).
    std::array<bool, kLanes + 3> scopeOn;       // the lanes, the returns, the drum bus, the key
    juce::int64 scopeFor = 0;
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
    R cfgDisplay, cfgRect;
    std::unique_ptr<juce::TextEditor> editor;
    std::function<void (juce::String)> editDone;
};
} // namespace sf0
