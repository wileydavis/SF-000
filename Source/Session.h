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
    // the files each insert holds (a convolution's impulse responses). Snapshots from before this was kept say nothing.
    bool hasInsFiles = false;
    std::array<std::array<std::vector<Engine::FileRef>, kSlots>, kLanes> insFiles;
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
struct MacroKnob
{
    juce::String name;
    std::vector<juce::int64> snaps;
    float depth = 1.0f;
    juce::NamedValueSet more;           // what the file says of it that this build does not know: carried
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
    // The modules in the order the screen has them, left to right and row by row: the stops of the
    // pointer (a controller's wheel; TAB on the keyboard).
    static constexpr int kStops = kLanes + 7;
    static Front stopFront (int stop, int& lane);       // lane: the lane that stop is, or -1
    int stopNow() const;                // the stop that is in front
    void bringStop (int stop, int by);
    juce::String stopName (int stop) const;
    // The pointer: where a controller's wheel is, while it is in use (a stop; -1: not in use). What
    // it rests on comes to the front; what it only passes over does not.
    int pointer = -1;
    void point (int stop);

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
    // ---- which screen is up (0 the main one, 1 the library, 2 the macro designer), and the one a
    // controller asks for (-1: none): the screen takes it up
    int showing = 0, wantShowing = -1;

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
    juce::int64 cueing() const          { return cueId; }
    void turnSnapKnob (int k, float v);
    void pressSnapKnob (int k);         // recall the knob's nearest snapshot as the new base; the knob goes home
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
    // A recall of part of it: the lanes that are chosen, the returns, the key come from the
    // snapshot; whatever is not chosen stays as it is now. (master: the key. The tempo, GLIDE and
    // the master's three knobs are the rig's and no snapshot's.)
    struct Scope
    {
        std::array<bool, kLanes> lane; bool returns = true, master = true;
        Scope() { lane.fill (true); }
        bool all() const { for (bool b : lane) if (! b) return false; return returns && master; }
    };
    void recall (juce::int64 id, const Scope&);
    const Snapshot* findSnap (juce::int64 id) const;
    int indexOf (juce::int64 id) const;
    void remove (juce::int64 id);
    void rename (juce::int64 id, const juce::String&);
    std::vector<int> knobScope (int k) const;   // the controls a snapshot knob moves

    // ---- patterns and inserts
    Pattern pattern (int lane) const    { return engine.pattern (lane); }
    void setPattern (int lane, const Pattern&);
    void pressGenerator (int lane, int type);
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
    static juce::File stateFile();
    static inline juce::File stateFileForTests;     // tests: the session is kept here instead
    static void backUp();               // a dated copy of the session as it was found, the last twenty kept
    std::function<void()> onChange;
    // ---- for the online library (the message thread, or with its lock)
    std::function<void()> onLibrary;                    // a snapshot was taken or renamed
    juce::var written (const Snapshot&) const;          // a snapshot as the session file holds it
    juce::var definition (const juce::String& print) const;    // void: not one this session knows

private:
    void timerCallback() override;
    void applyMorph (double glideBeats);
    void applySnapshot (const Snapshot&, double glideBeats);
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
    std::vector<std::pair<juce::String, juce::var>> defsKept;      // every definition the file has held, by print
    juce::String idInFile;
    std::array<Engine::SetupState, 2> retInit;
    double glideBeats() const;

    std::vector<float> base, offset;
    bool dirty = false, useDisk = true;
    juce::ThreadPool clipWriter { 1 };
    juce::int64 cueId = 0;
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
