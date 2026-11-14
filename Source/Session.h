#pragma once
// SF-000 session (message thread): what the screens edit. The base scene, the snapshot library,
// the sixteen snapshot knobs and their morph, the armed generators, and the file it is all kept in.
#include "Engine.h"
#include "Profile.h"
#include <functional>

namespace sf0
{
struct Snapshot
{
    juce::int64 id = 0;
    juce::String name;
    juce::Time time;
    std::vector<float> values;                              // one per control; below 0 = not stored
    std::array<Pattern, kLanes> patterns;
    std::array<std::array<int, kSlots>, kLanes> inserts {};
    std::array<int, kLanes> gen {};
    // what CONFIG sets: each lane's model and other setup, each insert's, the two returns'
    bool hasSetup = false;
    std::array<Engine::SetupState, kLanes> setup;
    std::array<std::array<Engine::SetupState, kSlots>, kLanes> insSetup;
    std::array<Engine::SetupState, 2> retSetup;
    // the drum bus's CONFIG (its four knobs are among the values). Snapshots from before there was one say nothing: the bus stays as it is.
    bool hasBus = false;
    Engine::SetupState busSetup;
    // the files each insert holds (a convolution's impulse responses). Snapshots from before this was kept say nothing.
    bool hasInsFiles = false;
    std::array<std::array<std::vector<Engine::FileRef>, kSlots>, kLanes> insFiles;
    // What each lane was playing that its own steps do not say: the notes that came in from
    // outside on its channel over the bars before it was taken, as steps (heardOn: some did),
    // and whether the lane's own sequencer was muted then (its steps were not sounding). The
    // notes themselves are kept as they came: [[beat in the loop, length in beats, note, velocity], ...].
    // Snapshots from before this was kept say nothing: a recall gives a lane its own steps, as it always did.
    bool hasPlayed = false;
    std::array<bool, kLanes> heardOn {}, seqMuted {};
    std::array<Pattern, kLanes> heard;
    std::array<juce::var, kLanes> heardNotes;
    // the audio file each lane held (a sample, a phrase); empty: none. Snapshots from before these were kept say nothing.
    bool hasFiles = false;
    std::array<juce::String, kLanes> laneFiles;
    // what it sounded like when it was taken, worked out from the bars kept with it (has: false if
    // nothing was playing, or it was taken before profiles were kept)
    Profile profile;
    // Its name everywhere, for good: this library's own id, then the snapshot's (FORMAT.md).
    juce::String uid;
    // What it was made with: the build, and the print of each lane, effect and return (Engine::defs).
    juce::var made;
    // As it was read from the session file, whole. A snapshot in the library is written back as it
    // was read, so whatever a newer build or another tool put in it is carried, at any depth. Only
    // its name changes here; anything else that comes to change one has to change this too.
    juce::var kept;
};

constexpr juce::int64 kCleanSlateId = 999;

// A snapshot knob: 2 to 10 snapshots in sweep order. Turning it moves every knob that differs
// among them, as an offset from the base scene, so knobs stack.
// It can also be tied straight to knobs (LEARN): each link is a knob and how far the full turn
// takes it, in that knob's own travel (-1 to 1). The two add up on a knob that has both.
struct MacroKnob
{
    struct Link { int ctl = -1; float amount = 0.0f; };
    juce::String name;
    std::vector<juce::int64> snaps;
    std::vector<Link> links;
    float depth = 1.0f;
    // A knob that holds a stack: it does not sweep through its snapshots, it steps through them.
    // A turn comes to the next and plays it in the headphones; a press recalls the one it is on,
    // over the GLIDE time, and the knob stays where it is. DEPTH and MOTION are not for it.
    bool pick = false;
    juce::int64 picked = 0;             // the one it is on (0: none come to yet)
    juce::NamedValueSet more;           // what the file says of it that this build does not know: carried
    juce::Array<juce::var> linksKept;   // links to knobs this build does not have, as the file had them: carried
};

// A stack made in the web library and brought down by LOAD: a named pile of snapshots, in an
// order (kind "song": parts in a line). The library screen shows one at a time.
struct Stack
{
    juce::String id, name, kind;        // (id: the web library's own for it)
    std::vector<juce::int64> snaps;
    juce::var kept;                     // as the file had it: what this build does not know of it is carried
};

class Session : private juce::Timer
{
public:
    explicit Session (Engine&, bool useDisk = true);
    ~Session() override;

    Engine& engine;

    // Who last moved each control from outside, and when (the millisecond counter): 1 a hand on
    // the controller (the E16), 2 something on another port (a sequencer playing it). The 3D
    // stage's eye goes to the first and not to the second. Written from the MIDI thread.
    std::array<std::atomic<juce::uint32>, cx::kCount> movedAt;
    std::array<std::atomic<juce::uint8>, cx::kCount> movedBy;
    void moved (int ctl, int by) { if (ctl >= 0 && ctl < cx::kCount) { movedBy[(size_t) ctl] = (juce::uint8) by; movedAt[(size_t) ctl] = juce::Time::getMillisecondCounter(); } }

    // ---- what the screens show
    int activeLane = 7;
    int selStep = 6;                    // the selected step of the active lane; -1 none
    int page = 0;                       // which 16 steps are shown
    std::array<int, kLanes> genType;    // the armed generator per lane; -1 none

    // ---- what is in front: the module the hands are on. A controller turns to its page and the
    // 3D stage's eye goes to it. A lane in front is the active lane; an insert in front is one of
    // the active lane's three.
    enum { fLane = 0, fInsert, fReturns, fMaster, fSeq, fSnap };
    struct Front
    {
        int kind = fLane, slot = 0;
        bool operator== (const Front& o) const { return kind == o.kind && (kind != fInsert || slot == o.slot); }
        bool operator!= (const Front& o) const { return ! (*this == o); }
    };
    Front front;
    juce::uint32 frontAt = 0;           // when something last came to the front, or the pointer last moved
    int frontBy = 0;                    // what brought it: 0 the mouse, 1 the keyboard, 2 a controller
    int frontCount = 0;                 // (a number that is different after every one of those)
    void bringLane (int lane, int by);  // a lane to the front: it is the active lane
    void bring (Front, int by);         // an insert, the returns, the master, the sequencer strip, the snapshot knobs
    // The stops of the pointer (a controller's wheel; TAB on the keyboard), in order: a lane, then
    // those of its three inserts that have an effect in them, then the next lane and its inserts;
    // after the lanes, the snapshot knobs, the returns, the master, the sequencer strip.
    struct Stop
    {
        int kind = fLane, lane = 0, slot = 0;
        bool operator== (const Stop& o) const { return kind == o.kind && ((kind != fLane && kind != fInsert) || lane == o.lane) && (kind != fInsert || slot == o.slot); }
        bool operator!= (const Stop& o) const { return ! (*this == o); }
    };
    std::vector<Stop> stops() const;
    Stop stopNow() const;               // what is in front, as a stop
    static int stopIndex (const std::vector<Stop>&, Stop);     // where it is among them (an empty insert: its lane's place); -1: nowhere
    void bringStop (Stop, int by);
    juce::String stopName (Stop) const;
    // Which panel of the screen a stop is: the lanes' eleven and the snapshot knobs' in the order
    // the screen has them (0 to 11), the three inserts' (12 to 14), the returns', the master's,
    // the sequencer strip's (15, 16, 17).
    static constexpr int kPlaces = kLanes + 7;
    static int placeOf (Stop);
    // The pointer: where a controller's wheel is, while it is in use. Nothing comes to the front
    // for it: that is for the press that ends it. While it is on a lane or one of a lane's
    // inserts, the insert panels show that lane's.
    bool pointing = false;
    Stop pointAt;
    void point (Stop);
    void pointOff();
    int shownLane() const               { return pointing && (pointAt.kind == fLane || pointAt.kind == fInsert) ? pointAt.lane : activeLane; }

    // ---- CONFIG: the setup panel, and what it is open on. The screen draws it; a controller can
    // open it and turn what is on it. It keeps to what is in front: a lane or an insert brought
    // to the front takes it along, and something that has no CONFIG shuts it.
    Engine::Target cfg;
    bool cfgOpen = false;
    void openConfig (Engine::Target);   // (on what it is already open on: it shuts)
    bool configOfFront (int by);        // open on what is in front, or shut (the returns: A, then B, then shut). False: what is in front has none
    std::vector<Engine::SetupParam> configList() const;     // its controls, in the order the panel has them
    void setConfig (int index, int value7);                 // one of them, from a controller (0 to 127; a list: its share of that; a switch: the upper half is on)
    juce::String cfgTouched;            // the control of CONFIG's a controller last turned (the panel shows its value)
    // ---- POCKETS: the master's page (showing 3). While it is up, a controller's CONFIG pages hold
    // its knobs, twelve to a page: the eleven lanes' HOME and AMOUNT; their WIDTH and CLEAR; their
    // YIELD and SPEED; their HOLD and RETURNS (configList and setConfig then mean these).
    static constexpr int kPocketList = 4 * (kLanes + 1);
    static int pocketControl (int index);       // the control in that place of the list (-1: none)
    static const char* pocketPageName (int page);       // HOME, WIDTH, YIELD, HOLD
    // AUTO: every lane that has been sounding gets its HOME and WIDTH from what it has been
    // playing (see Pockets::place). They travel there over the GLIDE time. How many were placed.
    int pocketsAuto();
    void pocketsReset();                // every lane's pocket back to where the rig starts it (AMOUNT and the rest stay)
    // ---- which screen is up (0 the main one, 1 the library, 2 the macro designer, 3 POCKETS), and the one a
    // controller asks for (-1: none): the screen takes it up
    int showing = 0, wantShowing = -1;
    bool pocketsUp() const              { return wantShowing == 3 || (showing == 3 && wantShowing < 0); }      // POCKETS is up, or has just been asked for
    // The macro designer (showing 2) is where MOTION's knobs are. While it is up, a controller's
    // CONFIG pages hold them: one page, a row for each MOTION (WAVE, SPEED, KNOB, AMOUNT).
    // motionUp: it was asked for from a controller for those knobs, and that is the page the
    // controller is sent to (otherwise it stays on the snapshot knobs, as it did).
    bool macrosUp() const               { return wantShowing == 2 || (showing == 2 && wantShowing < 0); }
    bool motionUp = false;

    // ---- controls
    // A knob or key moved by hand. sounding: the engine already has the value (a controller's knob,
    // set from the MIDI thread the moment it came); only the session's record of it is brought up.
    void userSet (int i, float v, bool sounding = false);
    void selectLane (int lane);         // the active lane, from outside the screen (a controller's page)
    float value (int i) const           { return engine.target (i); }

    // ---- snapshots
    std::vector<Snapshot> library;
    std::array<MacroKnob, 16> knobs;
    juce::int64 baseId = 0;             // the snapshot last recalled or taken
    juce::String lastTakenAt { "NONE YET" };
    // The bars a snapshot was taken over, kept beside the session as a small compressed clip.
    juce::File clipFile (juce::int64 id) const;
    // The lanes of a snapshot's bars, kept beside its clip: each thing the mix was made of (a lane
    // by its slug, the returns "dub" and "space", "bus": what the drum bus did), as it went into
    // the mix. Only those that sounded are there. only: those RECALL's scope takes (nullptr: all).
    juce::File stemsFile (juce::int64 id) const;
    struct Stems { double rate = 0.0; int bars = 0; std::vector<std::pair<juce::String, juce::AudioBuffer<float>>> parts; };
    struct Scope;
    bool readStems (juce::int64 id, Stems&, const Scope* only = nullptr) const;
    static juce::String stemKey (int index);        // 0 .. Engine::kStems - 1
    void waitForClips();                // until the clips being written are on the disk (tests; quitting)
    // ---- finding snapshots by their sound (those that have one kept)
    // The map: four rows of energy (0 calm .. 3 peak) by four columns of brightness (0 dark .. 3 bright).
    static int energyRow (const Profile& p)  { return juce::jlimit (0, 3, (int) (p.energy * 4.0f)); }
    static int brightCol (const Profile& p)  { return juce::jlimit (0, 3, (int) (p.bright * 4.0f)); }
    std::vector<juce::int64> mapCell (int row, int col) const;          // the snapshots in that square, calmest first
    std::vector<juce::int64> recent (int most) const;                   // newest first
    enum class Fit { like, more, less };
    // like: nearest in sound first. more, less: a step up or down in energy from it, nearest step
    // first, keeping to the ones nearest it in colour when there are more than fit.
    std::vector<juce::int64> fitting (const Profile& now, Fit, int most) const;
    const Profile& heardNow();          // what is playing, profiled (worked out again at most every second and a half)

    // Cue: a snapshot's clip in the headphones, apart from the set (0, or one with no clip: silence).
    // The rig itself is not touched.
    bool cue (juce::int64 id);          // false: that snapshot has no sound kept
    // The cue of that snapshot is to be only what this scope takes of it: its lanes, the returns,
    // the drum bus (from its stems; a scope that takes all of them is the clip itself, as it was
    // heard through the master). If it is playing, it changes now. id 0: every cue is whole again.
    void cueWith (juce::int64 id, const Scope&);
    // IN SET: under a narrowed cue, the rest of the set as it plays now (every lane, return and the
    // drum bus that the scope leaves out). The headphones then have what the set would be after
    // that recall. Off (as it starts): the snapshot's lanes alone. A whole cue is the clip either way.
    bool cueInSet = false;
    void setCueInSet (bool on);
    juce::int64 cueing() const          { return cueId; }
    void turnSnapKnob (int k, float v);
    // A knob that holds a stack (MacroKnob::pick): the snapshot it stands on (0: it holds none),
    // and making a knob one or a sweeping knob again.
    juce::int64 pickAt (int k) const;
    void setPick (int k, bool on);
    // Recall the knob's nearest snapshot as the new base; the knob goes home. A knob that is only
    // tied to knobs: the nearer end of its turn is kept (past half way, what it has pushed is
    // where those knobs now stand), and the knob goes home.
    void pressSnapKnob (int k);
    bool knobLive (int k) const;        // it does something: two snapshots or more, or a knob it is tied to
    // MOTION (the engine's three: cx::mot): which of them are on that knob, as a bit each (a wave chosen and an amount set)
    int motionOn (int k) const;
    static bool reaches (const Engine&, int ctl);      // a snapshot knob can move it: a lane's sound knobs, sends and EQ, the returns' knobs

    // ---- LEARN: a snapshot knob tied to knobs by showing it. learnBegin puts the knob at its full
    // turn; every knob moved from then on (on the screen, on a controller) is what the full turn
    // does. learnKeep ties them to it and leaves the rig sounding as it does, the knob at full:
    // turned back, they go back to where they were. learnDrop puts them back and forgets.
    // A knob turned back to where it was is not tied (and one that was tied is let go).
    int learning = -1;                  // the knob being taught; -1: none
    void learnBegin (int k);
    void learnKeep();
    void learnDrop();
    bool learnMoved (int ctl) const;    // it has been moved since LEARN was pressed
    int learnMoves() const;             // how many have
    // Recall the nth (1 the first) of the snapshots a snapshot knob holds, as a recall from the
    // library is: an arrangement's way in, from a sequencer. False: it holds no such one.
    bool recallFromKnob (int k, int n);
    Snapshot capture() const;
    juce::int64 take();                 // the SNAPSHOT key
    // A snapshot's sound is the bars the rig held its state through. Taken while something had just
    // moved (or was still travelling), it waits for four such bars; this looks whether they have
    // come. If the rig moves on first it takes what was held, or failing that the bars it was taken
    // over. all: do not wait any longer. (The timer calls it; so do the checks.)
    void finishTakes (bool all = false);
    int takesWaiting() const            { return (int) waiting.size(); }
    void recall (juce::int64 id);       // it becomes the base scene; sound knobs travel by GLIDE
    void recallOver (juce::int64 id, double beats, bool notes);     // the same, travelling over that long whatever GLIDE says

    // ---- the arrangement: snapshots in a line. Each part is a snapshot, how many bars it lasts,
    // and how long the way into it takes from its first bar (its MORPH: the GLIDE list's own
    // lengths, CUT to 256 bars, and never longer than the part). Played, each is recalled so as
    // to set off on its bar line: the knobs travel over the morph, patterns change on the bar,
    // and what cannot travel is crossed over, as in any recall. Printed, the whole of it is
    // rendered to files faster than it plays, the same every time.
    struct ArrPart
    {
        juce::int64 snap = 0;
        int bars = 8;
        int morph = 0;
        juce::NamedValueSet more;       // what the file says of it that this build does not know: carried
    };
    std::vector<ArrPart> arrangement;
    static constexpr int kArrMost = 64, kArrMaxBars = 256;
    static juce::String morphName (int morph);          // CUT, 1/4 BAR ... 256 BARS
    int arrBarsBefore (int part) const;                 // the bars before that part begins
    int arrBars() const                 { return arrBarsBefore ((int) arrangement.size()); }
    double arrMorphBars (int part) const;               // its morph in bars, as it will be played (no longer than the part)
    double arrSeconds() const           { return (double) arrBars() * 4.0 * 60.0 / engine.tempo(); }
    bool arrAdd (juce::int64 snap, int before = -1);    // at the end, or before that part; false: there is no room, or no such snapshot
    void arrRemove (int part);
    void arrMove (int part, int by);                    // earlier (-1) or later (1)
    void arrSet (int part, int bars, int morph);        // (either below 0: as it is)
    // Playing it. From stopped: its first part (or `from`) is recalled at once and the clock sets
    // off from the top. While the clock runs: that part comes in on the next bar line. At its end
    // the clock stops. RUN stopped by hand puts the arrangement down.
    void arrPlay (int from = 0);
    void arrStop();
    bool arrPlaying() const             { return arrOn; }
    int arrPart() const                 { return arrOn ? arrAt : -1; }      // the part playing (-1: none yet, or not playing)
    double arrPos() const;              // where it is, in beats from the arrangement's start (below 0: it has not come in yet)
    void arrTick();                     // (the timer's while it plays; the checks')
    // Printing it: to a folder of its own under `root`, the set (24-bit) and every lane, return
    // and the drum bus as a take has them. The audio must be stopped while it runs: audioPause
    // and audioResume are called for that (the app sets them; a check has no device). printBegin
    // returns what is wrong, or nothing; then printStep is called until it says no more.
    std::function<void()> audioPause, audioResume;
    juce::String printBegin (const juce::File& root);
    bool printStep (double msToSpend);                  // false: it is over (finished, or it failed: printNote says)
    void printCancel();
    bool printing() const               { return print != nullptr; }
    float printDone() const;                            // 0 to 1
    juce::String printNote;                             // the last thing worth telling: where it went, how loud it is, why it stopped
    juce::File printFolder;                             // the last print's folder
    // A recall of part of it: the lanes that are chosen, the returns, the drum bus, the key come
    // from the snapshot; whatever is not chosen stays as it is now. (master: the key. The tempo,
    // GLIDE and the master's three knobs are the rig's and no snapshot's.)
    // notes: the lanes' sequencers are given what each lane was playing (see recallNotes). Out:
    // every sequencer stays as it is, and the recall is the sound alone.
    struct Scope
    {
        std::array<bool, kLanes> lane; bool returns = true, bus = true, master = true, notes = true;
        Scope() { lane.fill (true); }
        bool all() const { for (bool b : lane) if (! b) return false; return returns && bus && master && notes; }
    };
    void recall (juce::int64 id, const Scope&);
    // NOTES: whether a recall brings a snapshot's notes. Lit (as it starts): each lane's sequencer
    // is given what the lane was playing when the snapshot was taken, its own steps and the notes
    // that were coming in from outside together, and plays them (its sequencer's mute is set to
    // match: off where there are notes to play). Out: a recall changes the sound and leaves every
    // sequencer, its steps and its mute, exactly as it is. Kept with the session. Every recall
    // goes by it: a tap in the library, a snapshot knob's press, a sequencer's RECALL.
    bool recallNotes = true;
    void setRecallNotes (bool on)       { if (on != recallNotes) { recallNotes = on; touch(); if (onChange) onChange(); } }
    // What a recall with notes gives a lane's sequencer: its own steps as the snapshot holds
    // them; or, if notes were coming in from outside, those as steps (over its own steps where
    // both were sounding, alone if its own sequencer was muted).
    static Pattern played (const Snapshot&, int lane);
    // The notes from outside, taken from the engine and kept for the last bars. What a lane was
    // being played over the bars before now, as steps: as many bars as the part takes to come
    // round (one, two, four or eight), or the last eight if it does not. False: nothing came.
    void pullHeard();
    bool heardPattern (int lane, Pattern&, juce::var* notes = nullptr) const;
    const Snapshot* findSnap (juce::int64 id) const;
    int indexOf (juce::int64 id) const;
    void remove (juce::int64 id);
    void rename (juce::int64 id, const juce::String&);
    // ---- LOAD: what the web library's tray holds (its GET /api/load), brought here. Message thread.
    std::vector<Stack> stacks;
    // A snapshot as the web library keeps it: one this library has (by uid) is only given the name
    // it has there; one it lacks is added whole, with the definitions it names. Its id here, or 0.
    juce::int64 takeFromLibrary (const juce::var& snapshot, const juce::String& name, const juce::var& defs);
    // The tray itself: its stacks replace the stacks loaded before; a macro goes on its knob if the
    // knob holds nothing (or holds that macro already); a song becomes the arrangement if there is
    // none. Nothing here is ever taken away by it. Says what it did.
    juce::String takeTray (const juce::var& tray);
    // Audio files a snapshot names that are not on this computer (a sample only the machine it was taken on has).
    int filesMissing (juce::int64 id) const;
    std::vector<int> knobScope (int k) const;   // the controls a snapshot knob moves

    // ---- patterns and inserts
    Pattern pattern (int lane) const    { return engine.pattern (lane); }
    void setPattern (int lane, const Pattern&);
    void pressGenerator (int lane, int type);
    // ---- the chosen step, as knobs (a controller's SEQ page). STEP goes through the pattern's
    // steps and chooses one; the others are what that step holds, by the laws the screen has:
    // velocity 15 to 100 hundredths, a gate up to the step's edge at the knob's middle and on to
    // the next lit step (and a little past it: legato) at its top, one to four hits, a note, an
    // octave. VEL and GATE light a step that is out, as dragging it does.
    enum { stPos = 0, stVel, stGate, stRatchet, stNote, stOctave, kStepKnobs };
    int stepKnob (int which, int has = -1) const;       // where that knob stands, 0 to 127 (a knob of shares: `has` if that is already in the right share). -1: nothing to say
    void setStepKnob (int which, int value7);
    void stepPress();                                   // the chosen step lit; lit, put out (it forgets what it held)
    juce::String stepSays() const;                      // the chosen step in a few letters: "7 D#3", "12", or nothing
    void nudgeLane (int lane, int dir);
    void stepInsert (int lane, int slot);

    // ---- disk (the file's layout and the rules it keeps to: FORMAT.md)
    static constexpr int kFormat = 1;   // rises only for a change an older build could not read past
    // Why nothing is written to the disk this time: the session file is from a newer SF-000, in a
    // format this build cannot read, and is left exactly as it is. Empty: all is kept as usual.
    juce::String notSavingWhy;
    juce::String libraryId;             // this library's own id: every snapshot's uid begins with it
    void touch()                        { dirty = true; }
    void save();
    bool load();
    juce::var stateVar();               // all the file holds, as it stands
    void takeState (const juce::var&);  // made what that says
    bool printRig = false;              // this is a print's own rig: its parts bring their notes, and the print says when it ends
    static juce::File stateFile();
    static inline juce::File stateFileForTests;     // tests: the session is kept here instead
    // ---- where things are kept
    // home: this machine's own settings (audio, MIDI, the look, the online address), always in
    // the system's place for them. library: all the instrument needs to be itself, in one folder
    // (session.json, clips/, samples/, backups/): home, unless another folder is chosen in SETUP.
    // recordings: where takes go, Music/SF-000 unless chosen. What is chosen is kept in
    // places.json, in home. A library chosen is taken up at the next start (settlePlaces, before
    // anything is read): if there is no library in that folder yet, the one in use is copied
    // there. A chosen library that is not there at a start (its drive is not plugged in) is left
    // chosen, and home is used that time.
    struct Places { juce::String library, next, recordings; bool libraryMissing = false; juce::String note; };
    static juce::File homeDir();
    static juce::File homeFile (const juce::String& name)   { return homeDir().getChildFile (name); }
    static juce::File libraryDir();
    static juce::File recordingsDir();
    static juce::File usualRecordingsDir();
    static const Places& places();
    static void settlePlaces (bool again = false);      // once a run (again: a test's next "start")
    static juce::File libraryFolderFor (const juce::File& chosen);   // the folder a choice comes to: itself if a library is in it (or it is empty, or named SF-000), else SF-000 inside it
    static juce::String chooseLibrary (const juce::File&);           // what will happen, to say
    static juce::String chooseRecordings (const juce::File&);
    static inline juce::File homeForTests;                           // tests: home is here (and places.json is read)
    // ---- the samples. Every audio file the rig is given (a lane's sample or phrase, an insert's
    // or SPACE's impulse responses) is copied into samples/ in the library folder, and read from
    // there: the library holds what it needs, and goes with its folder. A copy keeps its name, in
    // a folder named for what is in it (samples/3fa9c2e1/kick.wav), so the same file given twice
    // is kept once. The session lists the copies it made and where each came from. In the file a
    // copy is written from the library folder down; a path written whole, by a build from before
    // this, is read as its copy if one was made from it. A copy that nothing holds any more (no
    // lane, insert or return, no snapshot) is removed: when a snapshot is deleted, and a moment
    // after another sample is given. It is deleted if the file it came from is still where it
    // was (nothing is lost), and put in the Trash if not.
    struct Sample { juce::String file, from, hash; juce::int64 bytes = 0; };
    std::vector<Sample> samples;
    juce::String keepSample (const juce::String& path);         // the copy's full path (the path itself if it is one already, or cannot be copied)
    static juce::String refOf (const juce::String& fullPath);   // as written in the file
    juce::String pathOf (const juce::String& ref) const;        // what is written, as a full path
    juce::String cameFrom (const juce::String& fullPath) const; // where a copy was copied from (itself, if it is not one)
    int sweepSamples();                                         // copies nothing holds: gone. How many
    bool samplesMoved = false;
    static void backUp();               // a dated copy of the session as it was found, the last twenty kept
    std::function<void()> onChange;
    // ---- for the online library (the message thread, or with its lock)
    std::function<void()> onLibrary;                    // a snapshot was taken or renamed
    juce::var written (const Snapshot&) const;          // a snapshot as the session file holds it
    juce::var definition (const juce::String& print) const;    // void: not one this session knows

private:
    void timerCallback() override;
    void applyMorph (double glideBeats);
    // MOTION on the snapshot knobs: the laws of the knobs it is on, written down for the audio
    // thread whenever one of those knobs, or what MOTION is on, has changed (and only then).
    void motionLaws();
    juce::uint64 lawsSig = 0;
    // notes: the lanes whose sequencers are given what the snapshot was playing (nullptr: all of them)
    void applySnapshot (const Snapshot&, double glideBeats, const std::array<bool, kLanes>* notes = nullptr);
    void recallWhole (juce::int64 id);
    struct HeardEv { double on = 0.0, len = -1.0; int note = 0, vel = 0; };      // len below 0: still held
    std::array<std::vector<HeardEv>, kLanes> heardLog;
    double heardFrom = 0.0;             // nothing before this song position is known (the clock set off there, or the log reaches no further back)
    bool laneJumps (const Snapshot&, int lane) const;           // its instrument is to change in a way that cannot travel
    bool insertStays (const Snapshot&, int lane, int slot) const;
    void regenerate (int lane);
    void makeFactory();
    void ensureCleanSlate();
    void warmInserts();                 // every file-holding insert the library's snapshots name, read ahead
    void findLibraryId (const juce::String& fromFile);
    void sign();                        // a snapshot with no uid is given its own
    juce::var madeNow (const Snapshot&) const;
    // carried through a save: what the file held that this build does not know
    juce::NamedValueSet rootMore, controlsMore;
    // the arrangement, playing
    bool arrOn = false, arrWait = false;
    int arrAt = -1, arrStarts = 0, arrTicks = 0;
    double arrZero = 0.0;               // the song position at which the arrangement's first bar begins
    double glideOver = -1.0;            // a recall travelling over a time of its own (below 0: by GLIDE)
    double recallGlide() const          { return glideOver >= 0.0 ? glideOver : glideBeats(); }
    void arrRecall (int part, double beats);
    juce::var arrToVar() const;
    void arrFromVar (const juce::var&);
    juce::NamedValueSet arrMore;
    struct Print;
    std::shared_ptr<Print> print;       // (shared: it is let go where it is not known in full)
    void printEnd (bool keep, const juce::String& why);
    std::vector<std::pair<juce::String, juce::var>> defsKept;      // every definition the file has held, by print
    juce::String idInFile;
    std::array<Engine::SetupState, 2> retInit;
    Engine::SetupState busInit;
    double glideBeats() const;

    std::vector<float> base, offset;
    std::vector<float> learnBase;       // the scene under the knobs as it stood when LEARN was pressed
    float learnWas = 0.0f;              // where the knob being taught stood
    bool dirty = false, useDisk = true;
    juce::ThreadPool clipWriter { 1 };
    juce::int64 cueId = 0;
    juce::int64 cueScopeFor = 0; Scope cueScope;                // (the scope a cue of that snapshot is narrowed to)
    Profile nowProfile;
    juce::uint32 nowAt = 0;
    // the clips handed to the engine: each kept until a second after it was replaced
    std::vector<std::pair<std::unique_ptr<Engine::CueClip>, juce::uint32>> cueKept;
    void pruneCues();
    void keepClip (juce::int64 id, const Engine::Heard&);
    struct Waiting { juce::int64 id = 0; juce::uint32 stamp = 0; bool hasBefore = false; Engine::Heard before; };
    std::vector<Waiting> waiting;       // snapshots whose sound has not settled yet
    void settle (juce::int64 id, const Engine::Heard&, bool steady);
    juce::uint32 savedAt = 0;
};
} // namespace sf0
