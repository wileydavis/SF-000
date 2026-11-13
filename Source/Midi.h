#pragma once
// SF-000 MIDI: notes in for the lanes, the OXI E16 (knobs in, rings out) and the clock out.
//   Notes: any input, a lane's number is its channel. They go straight to the engine.
//   E16: the scene SF-000 writes itself (SF-000 --e16 file.oxie16), from the same table as below.
//   Clock: sent (24 ticks a beat with START and STOP, to every output that is not the E16), or
//   followed (the ticks, START, CONTINUE and STOP of one input at a time, never the E16's).
#include <juce_audio_utils/juce_audio_utils.h>
#include "Session.h"

namespace sf0
{
namespace e16
{
    // The scene's addresses. A lane's number is its channel and its page (1 KICK ... 11 VOICE). On a
    // lane's page the knob in place p (1-16, row by row from the top left) sends CC 11 + p: places
    // 1-12 are the lane's twelve knobs where the screen has them, 13 SEND A, 14 SEND B, 15 LOW,
    // 16 HIGH. A press on 16 swaps it to MID (CC 28) and back; the E16 does that itself.
    // Page 12 is the snapshot knobs, on channel 16: knob p sends CC 11 + p and its press CC 43 + p
    // (127 down, 0 up): the knob's nearest snapshot is recalled. A lane's page coming up sends
    // CC 119 on channel 16 with the lane's number, and that lane becomes the active one.
    constexpr int kKnobCc = 12, kMidCc = 28, kSnapChannel = 16, kPressCc = 44, kFocusCc = 119;
    // GLIDE, from any lane's page: a press on knob 1 swaps it to GLIDE (its ring green) and back,
    // as the suite's scene had SLOW. One address for all of them: CC 30 on channel 16. (The
    // snapshot page's presses recall; the library scene has GLIDE as knob 16 of FIND.)
    // A page that comes up says so (CC 119 on channel 16: a lane's number, 12 for the snapshot
    // page), and is told its sixteen values again: whatever the E16 made of them while the page
    // was away, the knobs start where the rig is.
    constexpr int kGlideCc = 30, kSnapPage = 12;
    // The library's scene (a second one: a scene has twelve pages and the first uses them all).
    // Page 1 is the map, on channel 15: the E16's grid is the library laid out by sound, energy up
    // the rows (the bottom row calm, the top row peak) and brightness along them (dark at the
    // left). Page 2 is the finders, on channel 14: RECENT, FITS NOW, MORE, LESS. On both, a knob
    // in place p sends CC 11 + p and its press CC 43 + p. Turning a knob goes through the
    // snapshots it holds and plays each in the headphones; pressing it recalls that one to the set.
    // The knob steps: so many of its clicks to the next snapshot (four, fewer where it holds more
    // than its travel has room for), counted from wherever the knob is. Its ring is then put where
    // that snapshot stands among the others, which moves the knob's number and nothing else: the
    // snapshot it is on stays until it is turned.
    constexpr int kMapChannel = 15, kFindChannel = 14, kLibKnobs = 32;      // 0-15 the map, 16-31 the finders
    // The second scene (SF-FX) holds the library's two pages and then everything the first has
    // no page for: the active lane's three inserts, the returns, the master, the sequencer strip
    // and CONFIG. What these pages hold depends on what is in front, so their knobs are on a
    // channel of their own (13) and SF-000 sends the script their names:
    //   pages 3-5   INS 1-3: knob p (1-8) is the place where the screen has it, CC 8 + 8 * slot + (p - 1);
    //               its press CC 40 + ... (a key among them: THROW, FREEZE)
    //   page 6      RETURNS: DUB's four and SPACE's four, at the addresses a sequencer has (channel 16, CC 60-67)
    //   page 7      MASTER: TAPE COMP LIMIT (channel 16, CC 68-70), on knobs 5 to 8 the drum bus's DRIVE CRUNCH PUNCH BOOM (CC 71-74),
    //               and on knobs 9 to 12 POCKETS' AMOUNT CLEAR SPEED RETURNS (CC 75-78)
    //   page 8      SEQ, the active lane's sequencer. Its top row: LENGTH (CC 70: the value and one is the steps), and the
    //               step under the hand: STEP (CC 64: through the pattern's steps; pressed, CC 33: that step lit, or put
    //               out), VEL (65), GATE (66). Its second: RATCH (67), NOTE (68), OCT (69). Its third: the generator's HITS
    //               ROTATE ACCENTS SEED (71-74; HITS pressed, CC 34, is RANDOM, and ROTATE pressed, CC 35, EUCLID).
    //               Knobs 15 and 16 pressed (CC 36, 37) nudge the pattern a step earlier and later.
    //   pages 9-11  CONFIG's controls in the order its panel has them, twelve to a page: CC 76 to 95,
    //               then 102 to 117 (96 to 101 are MIDI's own, as 0, 6, 32 and 38 are: nothing here is on those)
    //               While POCKETS (the master's page) is up, these pages are its knobs instead: the
    //               eleven lanes' HOME and AMOUNT, their WIDTH and CLEAR, their YIELD and SPEED, and
    //               a fourth, their HOLD and RETURNS, which is reached by PAGE + from the third and
    //               shown on the third's knobs.
    // The bottom row is the same on all of them: 13 the pointer's wheel, 14 OPEN, and on CONFIG's
    // pages 15 and 16 the page before and the next.
    constexpr int kFxChannel = 13, kInsCc = 8, kInsPressCc = 40, kLenCc = 70, kGenCc = 71, kCfgCc = 76, kCfgPer = 12, kCfgPages = 3;
    constexpr int kStepCc = 64, kStepKey = 33, kGenKey = 34, kNudgeKey = 36;      // (all on the FX channel: the chosen step's six knobs; STEP's press; RANDOM, EUCLID; earlier, later)
    constexpr int cfgCc (int n)         { return n < 20 ? kCfgCc + n : 102 + (n - 20); }       // CONFIG's nth control (0 to 35)
    constexpr int cfgAt (int number)    { return number >= kCfgCc && number < kCfgCc + 20 ? number - kCfgCc : number >= 102 && number < 102 + kCfgPer * kCfgPages - 20 ? 20 + number - 102 : -1; }
    enum { pgMap = 1, pgFind = 2, pgIns = 3, pgRet = 6, pgMaster = 7, pgSeq = 8, pgCfg = 9 };
    // On channel 16, from every page of both scenes. The wheel: knob 13 pressed (on the snapshot
    // page and the map, where a press recalls: held for half a second) takes up the pointer, and
    // its turns then go through the modules (a lane, its inserts that have an effect in them,
    // the next lane ...; then the snapshot knobs, the returns, the master, the sequencer strip).
    // Nothing comes to the front and the E16 stays on its page until the knob is pressed again:
    // that is the choice, and the E16 turns to what was chosen. Another knob turned, or eight
    // seconds untouched, puts the pointer down with nothing chosen. (On a page where knob 13 is
    // nothing else it sends CC 31, and a turn takes the pointer up.) OPEN: knob 14 pressed (held,
    // on those two pages) opens CONFIG on what is in front and shuts it; on the snapshot knobs it
    // opens the library, and in the library shuts it.
    constexpr int kWheelCc = 31, kWheelKey = 33, kOpenKey = 34, kCfgPrevKey = 35, kCfgNextKey = 36;
    // A page of the second scene says 21 to 32 where the first scene's say 1 to 12 (CC 119).
    constexpr int kFocusB = 20;
    // SF-000 turns the E16's page by Bank Select (the scene) and Program Change (the page), on the
    // channel the E16 is set to listen on (its MIDI Config: PC Receive Channel; PC Mode: Page).
    constexpr int kPcChannel = 12;
    constexpr int kPerLane = 17;                                // 12 knobs, SEND A, SEND B, LOW, HIGH, MID
    constexpr int kSlots = kLanes * kPerLane + 16;              // every address that carries a value
    // ring colours, by the OXI App's numbers (the suite's, as set in the App on Nov 8)
    constexpr int kWhite = 34, kRed = 63, kLightBlue = 18, kPink = 50, kViolet = 9, kGreen = 90;

    int slotFor (int channel, int number);                      // -1: not one of them
    int controlFor (int slot);
    void addressOf (int slot, int& channel, int& number);
    int to7 (const Control&, float v);                          // a bipolar knob's centre is 64 exactly
    float from7 (const Control&, int v);
    // The scene file's text, as the OXI App exports one. snapNames: the sixteen snapshot knobs'
    // names (an empty one is SNAP n).
    juce::String sceneText (const Engine&, const juce::StringArray& snapNames);
    juce::String fxText (const Engine&, const juce::StringArray& snapNames);     // the second scene: the library, the inserts, the returns, the master, the sequencer strip, CONFIG, the snapshot knobs
}

// More addresses, for a sequencer to play (the OXI ONE's modulation lanes). They are on no page of
// the E16's scenes and nothing is sent back for them. Like every address, they are heard on any port.
namespace ccx
{
    // On a lane's channel: its three inserts' eight places (32-39, 40-47, 48-55, whatever effect is
    // in the slot), MUTE, SOLO and the mute of its own sequencer (64 and over: on), and its pocket's
    // HOME WIDTH HOLD YIELD (59-62).
    constexpr int kIns = 32, kMute = 56, kSolo = 57, kSeqMute = 58, kPk = 59;
    // On channel 16: the DUB return's four knobs, the SPACE return's four, TAPE COMP LIMIT, the
    // drum bus's DRIVE CRUNCH PUNCH BOOM, POCKETS' AMOUNT CLEAR SPEED RETURNS.
    constexpr int kDub = 60, kSpace = 64, kMaster = 68, kBus = 71, kPkG = 75;
    // On channel 16, MOTION on the snapshot knobs (102-113): for each of the three, WAVE SPEED KNOB
    // AMOUNT. A list takes the number's share of 0-127; AMOUNT has none at 64.
    constexpr int kMot = 102;
    // On channel 16, one for each snapshot knob (80-95): RECALL. The value says which of the
    // snapshots the knob holds (1 the first) and that one is recalled, as from the library: sound
    // knobs travel over the GLIDE time (CC 30, sent first), patterns change on the bar. A sequencer
    // says a value again and again: only a new value acts. 0 is none (and makes the next one new).
    constexpr int kRecall = 80;
    constexpr int kPerLane = kSlots * kCells + 3 + kPkLane;
    constexpr int kExtras = kLanes * kPerLane + 4 + 4 + 3 + kBusKnobs + kPkGlobals + cx::kMotUnits * cx::kMotKnobs;
    int extraFor (int channel, int number);                     // -1: not one of them
    int controlFor (int extra, int spaceModel);
    // Any control's address, on the E16's scene or above. False: it has none.
    bool addressOf (int control, int& channel, int& number);
}

class Midi : private juce::MidiInputCallback, private juce::Timer, private juce::Thread, private juce::AsyncUpdater
{
public:
    // devices false: no ports, no timer, no thread (tests drive incoming() and service() themselves)
    Midi (Engine&, Session&, bool devices = true);
    ~Midi() override;

    // ---- set once, kept beside the session (midi.json)
    bool sendsClock() const                     { return clockWanted; }
    void setSendsClock (bool);
    float lead() const                          { return leadMs; }
    void setLead (float ms);                    // how long a follower takes to answer the clock
    // Following an outside clock takes the place of sending one.
    bool followsClock() const                   { return followWanted; }
    void setFollowsClock (bool);
    bool onBeat() const                         { return beatWanted; }      // own steps played ahead, to be heard on the sender's beat (off: with the notes it plays in)
    void setOnBeat (bool);
    float trim() const                          { return trimMs; }
    void setTrim (float ms);
    juce::String clockSource() const;           // the input whose clock is followed now; empty: none is coming in

    // ---- what is connected
    juce::StringArray inputNames() const;
    juce::StringArray clockNames() const;
    juce::String e16Name() const;
    // The E16's pages turned from here: the channel it listens on for Program Change (0: SF-000
    // never turns its page), and the slots its two scenes are in (1 to 16; which: 0 SF-000, 1 SF-FX).
    int pcChannel() const                       { return pcCh; }
    void setPcChannel (int channel);
    int sceneSlot (int which) const             { return sceneSlots[(size_t) juce::jlimit (0, 1, which)]; }
    void setSceneSlot (int which, int slot);
    bool wheelInUse() const                     { return wheelMode.load(); }
    void setSceneAlways (bool on)               { sceneAlways = on; }   // (midi.json's "e16_scene_always"; tests)
    void setPageFirst (int n)                   { pageFirst = juce::jlimit (0, 1, n); }     // (midi.json's "e16_page_first"; tests)
    juce::String pageNote() const               { return pcNote; }      // set when the E16 went somewhere other than where it was sent

    // A message as if it had come in (the inputs call this; so do tests). Any thread.
    void incoming (const juce::MidiMessage&, bool fromE16);
    // A clock byte (tick, START, CONTINUE, STOP) from one input. The first input to send one is
    // the clock; another is listened to only once that one has been silent for a second.
    void clockFrom (const void* source, const juce::String& name, juce::uint8 byte, double arrivedMs);
    // Message thread, 40 times a second: knobs heard, presses, the lane in focus, the rings.
    void service();
    std::function<void (const juce::MidiMessage&)> toE16;      // tests: what would be sent to the E16

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
    void timerCallback() override;
    void handleAsyncUpdate() override;          // a recall has come in: it is not kept waiting for the timer
    void takeGlide();
    void takeRecalls();
    void takeExtra (int extra, int val, bool fromE16);
    void held (int which, bool down, juce::uint32 nowMs, std::atomic<int>& shortPress, bool wheel);
    // ---- where the E16 is, and where what is in front would have it
    struct Where { int scene = -1, page = 0; bool operator== (const Where& o) const { return scene == o.scene && page == o.page; } bool operator!= (const Where& o) const { return ! (*this == o); } };
    Where whereNow() const;
    void pageCame (int said, juce::uint32 now); // the E16 said which page is up
    static juce::String pageName (Where);
    void arrived (Where, bool heard = false);                       // it is on that page now (it said so, or was sent there): the page is told its values and names again
    void turnTo (Where, juce::uint32 now);
    void openPressed();
    void wheelStart (juce::uint32 now);
    void wheelEnd();
    void wheelChoose();
    void serviceWheel (juce::uint32 now);
    void placeWheel (juce::uint32 now);
    struct WheelKnob { int ch = 0, num = -1, slot = -1; bool lib = false, own = false; };
    bool wheelKnob (WheelKnob&) const;                              // knob 13 of the page that is up: its address, and whose it is when the pointer is down
    static int wheelPer (int stops) { return stops > 36 ? 2 : 3; }  // clicks to the next (a knob has 128 numbers, and room is kept at both ends)
    struct PageKnob { int channel = 0, number = -1, want = -1; };
    std::array<PageKnob, 16> pageKnobs (int page);
    void feedPage (juce::uint32 now);
    void sayNames();
    int cfgPagesNow();
    void run() override;                        // the clock's own thread
    void scan();
    void sendE16 (const juce::MidiMessage&);
    void rings (double nowMs);
    void load();
    void save() const;
    void apply();
    static juce::File file();

    Engine& engine;
    Session& ses;
    const bool useDevices;
    bool clockWanted = true, followWanted = false, beatWanted = false;
    float leadMs = 0.0f, trimMs = 0.0f;
    mutable juce::SpinLock clockSrcLock;
    const void* clockSrc = nullptr;
    double clockSrcAt = 0.0;
    juce::String clockSrcName;

    // from the inputs' threads to the message thread
    std::array<std::atomic<int>, e16::kSlots> latest, sent;
    std::array<std::atomic<juce::uint32>, e16::kSlots> heardAt;
    std::array<std::atomic<int>, 16> presses;
    std::atomic<int> pageUp { 0 }, glideIn { -1 };
    // the pointer's wheel, OPEN, and CONFIG's pages
    std::atomic<bool> wheelMode { false }, otherTurned { false };
    std::atomic<int> wheelTurn { 0 }, wheelKeys { 0 }, openKeys { 0 }, motionKeys { 0 }, cfgStep { 0 };
    // The wheel's knob: the address it last sent from, the number it last said, and the number it
    // was last told to stand at (-1: none since it last spoke). A turn is counted from whichever
    // of the two it is nearer to, so it counts right whether or not the E16 took the number told.
    std::atomic<int> wheelAddr { -1 }, wheelHeard { -1 }, wheelTold { -1 };
    void wheelIn (int address, int val, juce::uint32 nowMs);
    std::atomic<int> pageHeard { 0 };                           // the page a knob just turned on the E16 is on (as the script would say it)
    std::atomic<juce::uint32> wheelHeardAt { 0 };
    std::array<std::atomic<juce::uint32>, 5> heldSince;         // knobs 13 and 14 of the snapshot page, then of the map, then the snapshot page's 15: down since (0: up)
    std::array<std::atomic<bool>, 5> heldFired;
    int wheelRest = 0;                                          // message thread from here
    juce::uint32 wheelBusyAt = 0, wheelPlacedAt = 0;
    int wheelTries = 0, wheelIdx = 0;
    bool wheelFresh = false;                                    // the pointer has just been taken up: the knob has not been set for it yet
    std::vector<Session::Stop> wheelList;                       // the stops as they stood when the pointer was taken up
    bool syncWanted = true;                                     // an E16 newly here is turned to what is in front
    Where e16At, whereWas, asked;
    bool whereKnown = false, cfgWasOpen = false;
    juce::uint32 turnedAt = 0;
    juce::String pcNote;                                        // it was sent to one page and said it was on another, or said nothing
    int pcCh = e16::kPcChannel;
    bool sceneAlways = false;
    int pageFirst = 0;                                          // the Program Change number that is the first page (midi.json's "e16_page_first": 0, or 1 if every turn lands a page short)
    // a turn it has not answered yet
    bool settling = false, sceneTurn = false;                   // it was sent somewhere (to another scene), and has not said it is there
    juce::uint32 settleUntil = 0, answerBy = 0, againAt = 0;    // until then what it says is its answer; by then it should have said; when the page is said again unasked
    int pageTries = 0, againLeft = 0;                           // the page said again by itself: because it said another, and without its saying anything
    Where strayed;                                              // where it said it was instead (a page of the scene it was leaving)
    bool e16Speaks = false;                                     // its script has said a page since it came: a turn left unanswered is worth a note
    void settle (juce::uint32 now);
    std::array<int, 2> sceneSlots { { 1, 2 } };
    int cfgPage = 0;                    // (POCKETS has a fourth: it stands on the third page's knobs)
    int cfgFirst (int physicalPage) const       { return physicalPage == e16::kCfgPages - 1 && cfgPage >= e16::kCfgPages ? cfgPage * e16::kCfgPer : physicalPage * e16::kCfgPer; }      // the place in CONFIG's list of that page's first knob
    bool cfgUp() const                          { return ses.cfgOpen || ses.pocketsUp() || ses.macrosUp(); }        // CONFIG's pages have something to hold
    bool pocketsWereUp = false, macrosWereUp = false;
    void motionPressed();
    Engine::Target cfgWas;
    // the second scene's page that is up: what its knobs were last told or said, and its names
    std::array<std::atomic<int>, 16> bSent;
    std::array<std::atomic<juce::uint32>, 16> bHeardAt;
    void bMark (int knob, int val, juce::uint32 nowMs) { bSent[(size_t) knob] = val; bHeardAt[(size_t) knob] = nowMs; }
    int bWait = 0;
    bool bHeard = false;
    std::atomic<int> laneNow { 0 };                             // the active lane, for the inputs' threads
    std::atomic<int> lenIn { -1 };
    std::array<std::atomic<int>, 4> genIn;
    std::array<std::atomic<int>, Session::kStepKnobs> stepIn;      // the chosen step's knobs, turned (-1: not)
    std::array<std::atomic<int>, 5> seqKeys;                       // STEP pressed, RANDOM, EUCLID, earlier, later: how many times
    std::array<std::atomic<int>, e16::kCfgPer * e16::kCfgPages> cfgIn;
    std::vector<Engine::SetupParam> cfgCache;
    int cfgCacheAge = 0;
    juce::String namesSaid;
    std::atomic<bool> glideFromE16 { false };                   // the GLIDE that came in was the E16's own (it knows it; anyone else's it is told)
    int pageNow = 0, pageWait = 0, glideSent = -1;              // message thread
    juce::uint32 glideHeardAt = 0;
    // The addresses that are not the E16's, in the order they came, every one of them: a key
    // pressed and let go between two visits of the message thread is two events, not none.
    struct ExEvent { juce::uint16 extra = 0; juce::uint8 val = 0, by = 0; bool done = false; };     // done: a key, already put to the engine from the MIDI thread
    static constexpr int kExQ = 1024;
    juce::SpinLock exLock;
    std::array<ExEvent, kExQ> exQ;
    int exN = 0;
    std::array<std::atomic<int>, ccx::kExtras> exLatest;         // (when that list is full: the last value of each, as it used to be)
    std::array<std::atomic<int>, ccx::kExtras> exBy;             // and whose port each last came in on (1 the E16's, 2 another)
    std::array<std::atomic<int>, 16> recallHeard, recallIn;     // RECALL: the value last said; the one to act on
    // the library's scene
    std::array<std::atomic<int>, e16::kLibKnobs> libLatest, libSent, libPresses, libTurn;      // libTurn: the clicks turned since last looked at
    std::array<int, e16::kLibKnobs> libRest {};                 // clicks turned that have not yet made a step
    std::array<std::atomic<juce::uint32>, e16::kLibKnobs> libHeardAt;
    std::array<juce::int64, e16::kLibKnobs> libPick {};         // the snapshot each knob holds now
    std::array<std::vector<juce::int64>, e16::kLibKnobs> libList;
    std::array<juce::uint32, e16::kLibKnobs> libTouched {};
    int libTick = 0;
    std::vector<juce::int64> libFor (int knob);
    void serviceLibrary();
    std::atomic<juce::MidiInput*> e16In { nullptr };

    // the rings of travelling knobs
    std::array<bool, e16::kSlots> ringOn {};
    std::array<int, e16::kSlots> ringLast {};
    std::array<double, e16::kSlots> ringSentAt {};

    std::vector<std::unique_ptr<juce::MidiInput>> inputs;
    std::unique_ptr<juce::MidiOutput> e16Out;
    juce::String e16OutId;
    std::vector<std::unique_ptr<juce::MidiOutput>> clockOuts;
    juce::CriticalSection outLock;
    int scanIn = 0;
};
} // namespace sf0
