#pragma once
// SF-000 session (message thread): what the screens edit. The base scene, the snapshot library,
// the sixteen snapshot knobs and their morph, the armed generators, and the file it is all kept in.
#include "Engine.h"
#include <set>
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
constexpr juce::int64 kCueNowId = 998;       // (in a cue's rig only: the first rig as it stood when the cue was asked for)
constexpr juce::int64 kInitId = 1000;         // INIT, the rig as it first came up; 1001 to 1005 are the five pushes made from it

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
    // A stack knob's snapshots each say which of the rig's parts they take (a bit each: the lanes
    // in order, then the returns, the drum bus, the master; all of them where this says nothing).
    std::vector<juce::uint32> lanes;
    static constexpr juce::uint32 kAll = 0xffffffffu;
    juce::uint32 lanesAt (size_t i) const       { return i < lanes.size() ? lanes[i] : kAll; }
    // One more key, the one after those: N, its notes. Lit, the knob coming to that snapshot brings what its lanes were
    // playing, as a part of the arrangement with N lit does. It is lit by hand only: where nothing is said, it is out.
    static constexpr juce::uint32 kNotes = 1u << (kLanes + 3), kParts = kNotes - 1u;
    // And one after N: M, its models and samples. Lit, a knob turned toward that snapshot blends each lane it takes from the
    // instrument that is playing to the snapshot's (its model, its sample, the rest of its CONFIG), both sounding, by how far
    // the knob has come: as a recall with GLIDE crosses them over, with the knob for the clock. Out, as where nothing is
    // said: the lanes keep the instruments they have, and the sound knobs alone follow the turn.
    static constexpr juce::uint32 kBlend = 1u << (kLanes + 4);
    juce::uint32 keysAt (size_t i) const        { const auto m = lanesAt (i); return m == kAll ? kParts : (m & (kParts | kNotes | kBlend)); }      // (its parts, its N and its M, as they are kept)
    bool notesAt (size_t i) const               { return (keysAt (i) & kNotes) != 0; }
    bool blendAt (size_t i) const               { return (keysAt (i) & kBlend) != 0; }
    void setLanes (size_t i, juce::uint32 m)    { if (i >= snaps.size()) return; if (lanes.size() <= i) lanes.resize (i + 1, kAll); lanes[i] = m; }
    void add (juce::int64 id, juce::uint32 m = kAll) { if (m != kAll || ! lanes.empty()) { lanes.resize (snaps.size(), kAll); lanes.push_back (m); } snaps.push_back (id); }
    void dropAt (size_t i)                      { if (i >= snaps.size()) return; snaps.erase (snaps.begin() + (long) i); if (i < lanes.size()) lanes.erase (lanes.begin() + (long) i); }
    void swapAt (size_t a, size_t b)            { if (a >= snaps.size() || b >= snaps.size()) return; const auto la = lanesAt (a), lb = lanesAt (b); std::swap (snaps[a], snaps[b]); if (la != lb) { setLanes (a, lb); setLanes (b, la); } }
    void drop (juce::int64 id)                  { for (size_t i = snaps.size(); i-- > 0;) if (snaps[i] == id) dropAt (i); }
    void clearSnaps()                           { snaps.clear(); lanes.clear(); }
    juce::int64 picked = 0;             // a stack knob: the one it was last on (0: none come to yet)
    int going = 0;                      // a stack knob: the way it was last turned (1 up its stack, -1 down, 0 not yet). Not kept in the file
    float sweptAt = 0.0f;               // a stack knob while CUE is lit: where it stood before it was used to choose
    juce::NamedValueSet more;           // what the file says of it that this build does not know: carried
    juce::Array<juce::var> linksKept;   // links to knobs this build does not have, as the file had them: carried
};

// A stack made in the web library and brought down by LOAD: a named pile of snapshots, in an
// order (kind "song": parts in a line). The library screen shows one at a time.
struct Stack
{
    juce::String id, name, kind;        // (id: the web library's own for it; one kept here has an id of its own)
    std::vector<juce::int64> snaps;
    std::vector<juce::uint32> lanes;    // the parts of the rig each snapshot takes, in the same order (none: every one takes everything)
    bool here = false;                  // made on the instrument (KEEP, in the designer), not in the web library
    juce::String mark;                  // its line (Session::stackPrint) as the web library and this one last agreed it was. Empty: they never have
    juce::var kept;                     // as the file had it: what this build does not know of it is carried
    juce::uint32 lanesAt (size_t i) const { return i < lanes.size() ? lanes[i] : 0xffffffffu; }
    juce::uint32 keysAt (size_t i) const  { const auto m = lanesAt (i); return m == 0xffffffffu ? MacroKnob::kParts : (m & (MacroKnob::kParts | MacroKnob::kNotes | MacroKnob::kBlend)); }     // (with its N: MacroKnob::kNotes)
};

// A knob kept by itself, off the module: a macro knob (what it is tied to) or a stack knob (its snapshots in order, each
// with its parts), to be put on a place of any module later.
struct KeptKnob
{
    juce::String id;
    bool stack = false;
    juce::var knob;                     // as the session file keeps a knob (its name is in it)
    juce::String name() const           { return knob.getProperty ("name", "").toString(); }
};

// A module: the snapshot module's knobs as a set (four macro knobs, ten stack knobs), kept under a name. The rig carries
// one at a time, chosen from those kept here: a set's worth of knobs for each set.
struct Module
{
    juce::String id, name;
    juce::var knobs;                    // sixteen, each as the session file keeps a knob
    juce::var motion;                   // MOTION on its knobs: each of MOTION's controls by its id
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
    int cfgE16Page = -1;                // the page of CONFIG the E16 is on (its twelve are the panel's top three rows), or -1: it is not on one
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
    int takes = 0;                      // snapshots taken since the session opened (the key, S, the E16's 13 and 16 together): the screen says each one
    juce::String takenName;             // ... and the last one's name
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
    static juce::String madeBy();                   // this build: its version and the commit it is built from
    static juce::String hashOf (const juce::String& text);       // a text's SHA-256, in hex
    static juce::String hashOf (const void* data, size_t bytes);
    // The same bars without loss, kept beside those for the library to be studied from (the instrument itself plays the
    // Ogg): the output ("mix") and each stem that sounded, each a FLAC file of 24 bits, in one file. A part that ran over
    // full scale was written quieter by a power of two, and says by how much; read back, it is as it was.
    juce::File losslessFile (juce::int64 id) const;
    struct Lossless { double rate = 0.0; int bars = 0, bits = 0; std::vector<std::pair<juce::String, juce::AudioBuffer<float>>> parts; std::vector<float> gains; };
    static bool readLossless (const juce::File&, Lossless&);
    static void writeLossless (const juce::File&, const juce::AudioBuffer<float>& mix, const std::vector<juce::AudioBuffer<float>>* stems, double rate, int bars);
    bool keepLossless = true;           // (whether a snapshot taken keeps them: the tests of other things turn it off)
    // ---- the log: what is done with the chips, a line for each thing done, for the library to learn from (FORMAT.md, The
    // log). A chip heard, recalled, taken, put in a pile or taken out of one, a part of the arrangement reached, the clock
    // started or stopped. Written beside the session (log/<year>-<month>.jsonl) by a thread of its own and sent by ONLINE;
    // nothing here reads it. What ONLINE or a LOAD does to the library is not the player's, and is not logged.
    void logAction (const char* what, juce::int64 chip = 0, const juce::NamedValueSet& more = {});
    juce::File logDir() const;
    void waitForLog();                  // until what was logged is on the disk (tests; quitting)
    juce::StringArray logTail;          // (the last lines logged, the newest last: the checks read them)
    int logQuiet = 0;                   // (above 0: what is done now is not the player's own, or is one step of something already logged)
    struct Quiet { Session& s; explicit Quiet (Session& x) : s (x) { ++s.logQuiet; } ~Quiet() { --s.logQuiet; } };
    juce::String uidOf (juce::int64 id) const;      // a chip's name everywhere ("": none, or it has none)
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
    // notes: whether the recall this cue stands for would bring the snapshot's notes (a stack knob's N, a part's N).
    // -1: as NOTES is set. It matters only to a cue that is the rig (below); a clip has the notes it was taken with.
    void cueWith (juce::int64 id, const Scope&, int notes = -1);
    juce::int64 cueing() const          { return cueId; }
    // ---- a cue that is the rig (CueRig.h): a second rig, in a process of its own, is made what this one is now and
    // then given the recall the cue stands for, and its sound is what plays in the headphones.
    // Set by whoever runs one. play: that snapshot, with the parts of the rig (as a stack's keys: lanes, R, B, K) and
    // the notes its recall would take; false: there is no second rig now, and the cue is the snapshot's clip as it
    // always was. stop: none is wanted.
    std::function<bool (juce::int64 id, juce::uint32 lanes, bool notes)> cueRigPlay;
    std::function<void()> cueRigStop;
    bool cueIsRig() const               { return cueByRig; }        // the cue playing is the second rig's
    // What a second rig needs to be this one: all the file holds as it stands, but of the library only two
    // snapshots, the rig as it is now with what every lane is playing (kCueNowId), and the one to be heard.
    juce::var cueState (juce::int64 target);
    // The second rig's own side: made what `state` says, given what every lane was playing, and then the recall.
    void cueRigTake (const juce::var& state, juce::int64 target, juce::uint32 lanes, bool notes);
    // A second rig has no folders of its own to settle: it reads the samples where the first one's library is.
    static void placeForRig (const juce::File& libraryDir);
    juce::uint32 changes = 0;           // counts up with everything that changes what the file would hold
    // The snapshot a stack knob last came to with CUE lit that has no sound kept with it (taken with the clock stopped,
    // or one of the rig's own): there is nothing of it to play in the headphones. 0: none, or the last one had its sound.
    juce::int64 cueLacked = 0;
    // The parts of the rig the snapshot in the headphones is narrowed to (nullptr: it is not narrowed, or that one is not it)
    const Scope* cueNarrowed (juce::int64 id) const;
    void turnSnapKnob (int k, float v);
    // ---- The snapshot module's sixteen knobs are of two kinds, by where they are.
    // Knobs 1 to 4 are MACRO knobs: each is tied to knobs of the rig (LEARN) and turns them.
    // Knobs 5 to 12, 14 and 15 are STACK knobs: each holds an ordered list of snapshots, one at each of even
    // steps along its turn (one alone: at the top). How a stack knob is played is CUE's to say:
    //  - CUE out: a turn morphs the set itself, from wherever it is toward the next snapshot in
    //    the direction of the turn, by as much of the way as the knob has gone of its way there;
    //    at that snapshot's place the set is that snapshot, and the turn goes on to the next. It
    //    never jumps: it starts from what is playing. A press goes all the way to the nearest.
    //  - CUE lit: a turn goes through the stack in the headphones and the set is not touched;
    //    a press morphs the set to the one the knob is on, over the GLIDE time.
    // Each snapshot of a stack takes only the parts of the rig its own lanes say.
    static constexpr int kMacroKnobs = 4;
    static constexpr int kStackKnobs = 10;
    // 13 and 16 are no knob of the module: on the E16 they are two of the four corners every page has (13 the lane
    // wheel, 16 OPEN), and a stack knob there would need its press for itself. So: 1 to 4 macro, 5 to 12 and 14, 15 stack.
    static bool isCorner (int k)        { return k == 12 || k == 15; }
    static bool isMacro (int k)         { return k >= 0 && k < kMacroKnobs; }
    static bool isStack (int k)         { return k >= kMacroKnobs && k < 16 && ! isCorner (k); }
    bool stackCue = false;
    void setStackCue (bool on);
    static constexpr juce::uint32 kAllLanes = (1u << (kLanes + 3)) - 1u;
    // (a stack's snapshot has one key more than the parts of the rig: N, whether it brings its notes. kAllKeys: all of them as kept)
    // Which lists of models, and which knobs of theirs, a snapshot is written on ("lists" in it; absent: 1, every build
    // before the seventy-ninth). Session.cpp, fromListsBefore.
    static constexpr int kListsNow = 3;       // (3: the PERC lane has SAMPLE after its objects)
    static constexpr juce::uint32 kNotesKey = MacroKnob::kNotes, kBlendKey = MacroKnob::kBlend, kAllKeys = kAllLanes | kNotesKey | kBlendKey;
    static Scope scopeOf (juce::uint32 lanes);
    static int mixCellOf (int type);                        // an effect's MIX among its eight knobs, or -1 (effects(): Rig.h)
    static juce::uint32 lanesOf (const Scope&);
    struct Held { const Snapshot* s = nullptr; juce::int64 id = 0; juce::uint32 lanes = kAllLanes; bool notes = false, blend = false; };       // (notes: its N is lit. blend: its M)
    std::vector<Held> held (int k) const;                   // a knob's snapshots that are here, in order, each with its lanes
    static float placeOf (int i, int n)  { return n <= 1 ? 1.0f : (float) i / (float) (n - 1); }
    int nearest (int k, float v) const;                     // the index (in held) of the snapshot nearest that place of the knob; -1: none
    juce::int64 pickAt (int k) const;                       // the snapshot a stack knob stands on (0: it holds none)
    void sweepKnob (int k, float from, float to);           // CUE out: the set, morphed as that turn of the knob has it
    // A snapshot's notes into the lanes it takes, as a recall with its notes brings them: what each lane was playing into its
    // sequencer (on the next bar line while the clock runs), its generator and that generator's knobs, its sequencer's mute.
    // A stack knob that comes to a snapshot whose N is lit does this; its sound has come with the turn.
    void bringNotes (const Snapshot&, const Scope&);
    // Where a stack knob stands among its snapshots, for the screen to say while it is turned: the one it is going toward
    // (the way it was last turned), the one behind it, how far it is from that one to this (0 to 1), and whether it
    // stands on one (within a click of a controller's knob). to < 0: it holds none.
    // (at: the knob's own place, 0 to 127, a figure for each click of the E16's knob and the same whichever way the knob came:
    // the figure to come back to. count: how many snapshots the knob holds, each at placeOf (i, count) of the turn.)
    struct StackWay { int from = -1, to = -1, on = -1; float part = 0.0f; juce::String fromName, toName; bool notes = false; int at = 0, count = 0; };
    StackWay stackWay (int k, float v) const;
    // A press. Knob 4 (the top right of the module, here and on the E16): CUE, on or off.
    // A stack knob: the set goes to the snapshot the knob is nearest (the parts of the rig that
    // snapshot takes), over the GLIDE time; the knob stands on its place. A macro knob (1 to 3):
    // the nearer end of its turn is kept (past half way, what it has pushed is where those knobs
    // now stand), and the knob goes home.
    static constexpr int kCueKnob = 3;
    void pressSnapKnob (int k);
    // What a stack knob holds, changed by hand (the designer): a snapshot added (with the lanes it takes), one taken out,
    // two exchanged, one's lanes set. Nothing sounds different for it: the set moves when the knob is turned or pressed.
    static constexpr int kStackMost = 128;
    bool stackAdd (int k, juce::int64 id, juce::uint32 lanes = kAllLanes);
    void stackDrop (int k, int index);
    void stackSwap (int k, int a, int b);
    void stackMove (int k, int from, int to);       // one of its snapshots to another place among them, its parts with it
    bool stackInsert (int k, int at, juce::int64 id, juce::uint32 lanes = kAllLanes);       // as stackAdd, at that place
    void stackLanes (int k, int index, juce::uint32 lanes);
    void stackSettle (int k, juce::int64 on);
    void stacksLeft (int but);
    std::vector<float> knobsBroughtOver (const std::array<MacroKnob, 16>& read, const std::array<bool, 16>& stepped, bool twoKinds = false);
    void knobClear (int k, bool told = true);               // the knob holds nothing (a macro: what it pushed is let go of). told: the web library is to hear of it
    // Knobs a session from before the two kinds had that found no slot of their kind (more than four macros, more than
    // twelve stacks): kept as the file had them, for ONLINE to send to the library as modules off the chassis.
    juce::Array<juce::var> knobsAside;
    juce::var asideBody (int i, bool& whole) const;         // one of them as the library takes a module (whole: every snapshot of it has a uid)
    // Modules of the web library's chassis whose knob was emptied here: each by its id there and the knob it was on, for
    // ONLINE to take off the chassis there (it stays in the library).
    juce::Array<juce::var> knobsUnseated;
    static bool moduleFits (int k, const juce::var& module);    // a module of the chassis is of the kind that knob takes
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
    // A new track: the rig as it starts. CLEAN SLATE whole and at once, its empty steps too whatever NOTES says (no steps,
    // no effects in the racks, every knob where the rig starts it, every generator let go); and what a snapshot leaves
    // alone that was the last track's: every MUTE and SOLO out, the macro knobs at rest, nothing cued, the notes heard
    // from outside forgotten. TEMPO, GLIDE, the master, the snapshot module's knobs and the library stay as they are.
    void newTrack();
    // A lane whose model is SAMPLE, set to play its sample as it was recorded: everything of the lane that shapes the sound
    // put where it does nothing (the filter open, no layer and no crossover, the whole note, no drive, the lane's EQ flat),
    // and, where the lane plays a sample at the pitch of the note, TUNE set so that its first lit step plays it at its own.
    // LEVEL, the sends and the lane's insert effects are left. What it did, in words (empty: the lane plays no sample).
    bool playsSample (int lane) const;
    struct SampleSays { bool has = false; juce::String root; float peakDb = 0.0f; bool pitched = false; int rootNote = 60; double seconds = 0.0; };
    // LOOP on a lane's sample: how many bars it is taken for now and the tempo it was made at ("8 BARS AT 131.1"); nothing with LOOP off
    juce::String loopSays (int lane, const SampleSays&) const;
    SampleSays sampleSays (int lane) const;         // what the lane's file is heard to be (its root, its loudest point)
    // A CONFIG control set by hand, from the panel or a controller: the engine's, and where a lane's MODEL brings sound
    // knobs of its own the session's knobs stand where those start.
    void setupByHand (Engine::Target, const juce::String& id, float v);
    juce::String samplePlain (int lane);
    // RESAMPLE: the last four bars the rig played (from a bar line, as they left the master; its last seconds where no
    // clock ran) become this lane's sample: written as a file into the library's samples (RESAMPLE and the time) and
    // loaded as LOAD would. What the rig just played is then something to slice, loop and make grains of. What it did,
    // in a few words (they are shown on one line).
    juce::String resampleInto (int lane);
    // A snapshot's sound is the bars the rig held its state through. Taken while something had just
    // moved (or was still travelling), it waits for four such bars; this looks whether they have
    // come. If the rig moves on first it takes what was held, or failing that the bars it was taken
    // over. all: do not wait any longer. (The timer calls it; so do the checks.)
    void finishTakes (bool all = false);
    int takesWaiting() const            { return (int) waiting.size(); }
    void recall (juce::int64 id);       // it becomes the base scene; sound knobs travel by GLIDE
    void recallOver (juce::int64 id, double beats, bool notes, juce::uint32 lanes = kAllLanes);     // the same, travelling over that long whatever GLIDE says (and only those parts of the rig)

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
        juce::uint32 lanes = kAllLanes; // the parts of the rig it takes from its snapshot (the others stay as they are when it comes in)
        // It is come into with the TRANSITION: a lead-in that ends on the part's bar line (transIn) and a hit that lands with the
        // part (transHit). Each: 0 not, 1 with what the bar's plates say, 2 with a file of the part's own (inFile, hitFile).
        int transIn = 0, transHit = 0;
        juce::String inFile, hitFile;
        bool notes = true;              // it brings its snapshot's notes (its own key, N: not the library's NOTES, which is for recalls made by hand)
        bool edited = false;            // its snapshot was made for it by EDIT (and is written over by the next, if nothing else uses it)
        juce::NamedValueSet more;       // what the file says of it that this build does not know: carried
    };
    std::vector<ArrPart> arrangement;
    static constexpr int kArrMost = 64, kArrMaxBars = 256;
    static juce::String morphName (int morph);          // CUT, 1/4 BAR ... 256 BARS
    int arrBarsBefore (int part) const;                 // the bars before that part begins
    int arrBars() const                 { return arrBarsBefore ((int) arrangement.size()); }
    double arrMorphBars (int part) const;               // its morph in bars, as it will be played (no longer than the part)
    double arrSeconds() const           { return (double) arrBars() * 4.0 * 60.0 / engine.tempo(); }
    bool arrAdd (juce::int64 snap, int before = -1, juce::uint32 lanes = kAllLanes);    // at the end, or before that part, taking those parts of the rig; false: there is no room, or no such snapshot
    void arrRemove (int part);
    void arrMove (int part, int by);                    // earlier (-1) or later (1)
    void arrTransSet (int part, int in, int hit);       // a part's lead-in and hit: 0 not, 1 the bar's, 2 its own file (if it has one)
    void arrTransStep (int part, int slot);             // the lead-in (0) or the hit (1) stepped: its own file (if it has one), the bar's, not
    bool arrTransFile (int part, int slot, const juce::File&);     // a file of the part's own for it, which it then plays
    void arrNotes (int part, bool on);                  // a part that brings its snapshot's notes, or leaves the sequencers as they are
    void arrSet (int part, int bars, int morph);        // (either below 0: as it is)
    void arrLanes (int part, juce::uint32 lanes);       // the parts of the rig it takes from its snapshot
    // Playing it. From stopped: its first part (or `from`) is recalled at once and the clock sets
    // off from the top. While the clock runs: that part comes in on the next bar line. At its end
    // the clock stops. RUN stopped by hand puts the arrangement down.
    void arrPlay (int from = 0);
    void arrStop();
    // The rig as the arrangement has it once that part has arrived: what every part before it left, in order, each with
    // the parts of the rig it takes and its notes or not, over the rig as it stands now. notes: the lanes whose sequencer
    // some part up to it gives (the others' stay as they are). arrBring makes the rig that, over that many beats.
    Snapshot arrStateAt (int part, std::array<bool, kLanes>& notes) const;
    void arrBring (int part, double beats = 0.0);
    // ---- EDIT: a part changed in the main view, and its snapshot left as it is.
    // arrEditBegin: the arrangement and the clock stop, the rig is made what the arrangement has at that part, and it
    // plays. arrEditKeep: the rig as it then stands is a new snapshot in the library, named after the part's, and takes
    // its place in the part; the part's keys are what they were and those of whatever part of the rig was changed
    // meanwhile (N, if a lane's notes were). A snapshot an EDIT made for this part before, that nothing else uses, goes:
    // the new one has its name. arrEditDrop: nothing is kept (the rig stays as it is). Playing or printing the
    // arrangement, or changing its parts, drops it too.
    int arrEdit = -1;                                   // the part being edited (-1: none)
    bool arrEditBegin (int part);
    juce::int64 arrEditKeep();                          // the new snapshot (0: nothing was being edited, or none could be taken)
    void arrEditDrop();
    bool usedBesides (juce::int64 snap, int part) const;    // a snapshot that something other than that part of the arrangement holds: another part, a knob, a stack, a knob or a module kept
    // TO STACK: the arrangement as a stack in the rack, its parts' snapshots in order, each with the parts of the rig its
    // part takes. Where it is in the rack (-1: the arrangement is empty). arrOpenParts: how many parts have a key out
    // (those lean on what came before, on a knob as here).
    int arrToStack();
    int arrOpenParts() const;
    bool arrPlaying() const             { return arrOn; }
    int arrPart() const                 { return arrOn ? arrAt : -1; }      // the part playing (-1: none yet, or not playing)
    double arrPos() const;              // where it is, in beats from the arrangement's start (below 0: it has not come in yet)
    void arrTick();                     // (the timer's while it plays; the checks')
    // ---- the arrangements kept. The one on the page IS one of them (arrAtId), as the module in the chassis is one of the
    // modules kept: what is changed on the page changes it, and nothing is kept by hand. NEW makes another, empty, and puts
    // it on the page; COPY keeps the one on the page again under another name, and the page holds the copy; LOAD puts
    // another on the page, whole, and the one that was there is kept as it stood. A snapshot that has left the library has
    // left every arrangement's parts, when that arrangement is next on the page. No network is asked.
    struct KeptArr { juce::String id, name; juce::var parts; };      // parts: an arrangement as the session file keeps one (arrToVar)
    std::vector<KeptArr> arrs;
    juce::String arrAtId;                                   // the id of the one on the page
    void arrsReady();                                       // there is at least one, and the one on the page is among them (a session from before them: its arrangement is ARRANGEMENT 1)
    int arrsIndex() const;                                  // the place among `arrs` of the one on the page
    juce::String arrsName() const;
    int arrsNew (const juce::String& name = {});            // an empty one, kept, and put on the page. Its place
    int arrsCopy (const juce::String& name = {});           // the one on the page kept again under another name: the page holds the copy from then on. Its place
    bool arrsLoad (int index);                              // that one onto the page, whole. False: no such one, it is the one there, or a print is being made
    void arrsRename (int index, const juce::String&);
    bool arrsRemove (int index);                            // out of those kept. The one on the page: the one before it (or after) takes the page first. False: it is the only one
    juce::String arrsSays (int index) const;                // what it holds, in a line
    // ---- TRANSITION: a lead-in that ends on a bar line and a hit that starts on it (Engine::trans), fired by hand.
    // FIRE sets them off for the next line (every 4, 8 or 16 bars). ARM: lit, the next recall, mute or solo does not
    // happen at once: it sets the transition off and happens on the line the transition lands on, with whatever else
    // is asked for meanwhile; ARM then goes out. With the clock stopped nothing waits: FIRE is the hit, at once.
    int transBars = 8;
    bool transArmed = false;
    double transAt = -1.0;                              // the line the last firing lands on, in beats of the song
    void transFire();
    void transArm();                                    // lit, or put out (put out: what was waiting happens at once)
    bool transWaits() const                             { return ! transPend.empty(); }
    int transWaiting() const                            { return (int) transPend.size(); }
    double transBeatsLeft() const;                      // until that line (0: none is on its way)
    void transTick();                                   // (the timer's while something waits; the checks')
    enum { tBars, tLevel, tLead, tHit, tDub, tSpace };
    void transStep (int what);                          // a setting, stepped to its next value
    // FINISH: the master's stage before its limiter (suite/modules/Finish.cpp). Five places, from OFF to all of it; kept with
    // the set. finishHeld: the most any of its bands is being held, dB (for the screen).
    int finishAt = 0;
    void finishStep();
    void finishSet (int place);
    int finishPct() const;
    juce::String finishSays() const;
    juce::String transSays (int what) const;
    bool transLoad (int slot, const juce::File&);       // a file for the lead-in (0) or the hit (1)
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
    int printJoined = 0;                                // in the last print: lanes that went into the mix both ways and are one stem (the drum bus did nothing)
    juce::uint32 arrEditLit = 0;                        // the last KEEP: the keys it lit on the part that were out (tests, and the page's note)
    // (a tool's: a print that writes nothing and keeps the time each bar took to render, as a share of the time the bar
    // lasts: its blocks' mean, and its slowest block. To find where a set asks more of the processor than it has.)
    static inline bool printTimeOnly = false;
    std::vector<std::pair<float, float>> printTimes;
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
    // ---- a knob as the web library keeps one (a module). LOAD makes the sixteen knobs what the
    // library's chassis says: Online first sends up whatever a knob holds that the library has
    // not got, so nothing is lost, and then tells takeTray what may be done with each knob.
    bool knobHolds (int k) const;
    juce::String knobPrint (int k) const;                   // what it holds, in a line: two knobs with the same line hold the same
    juce::var knobBody (int k, bool& whole) const;          // as the library takes a module. whole: every snapshot of it has a uid
    bool knobIs (int k, const juce::var& module) const;     // it holds what that module of the chassis is (name and depth too)
    struct KnobPlan { enum What { leave, take, clear }; What what = leave; juce::String lib; };      // (lib: the module the knob is now known to be)
    // A stack knob taken to another stack knob, one that holds nothing: all it is goes with it (its snapshots and their
    // parts, its name, where it stands, which module of the library it is), and MOTION follows it. LOAD makes room so.
    bool knobMove (int from, int to);
    // The rack: stacks kept on the instrument, off the knobs (and those LOAD brought for the library screen).
    // A stack knob's stack kept under its name: a copy, each snapshot with the parts it takes. One kept here under
    // that name before is replaced by it. Gives its place among the stacks, or -1.
    int stackKeep (int k);
    // A stack of the rack put onto a stack knob: its snapshots, each with its parts, at the end of what the knob
    // holds (a knob that held nothing takes the stack's name too). False: not a stack knob, or no room for them all.
    bool stackOnto (int index, int k);
    void stackForget (int index);       // out of the rack (and, once ONLINE has said so there, out of the web library's rack for the instrument)
    // A stack of its own, made and changed with no knob: a collection of snapshots under a name, to find them by.
    // (A song is the web library's to arrange and is never changed here. An empty one is the instrument's alone
    // until it holds something: the web library is not told of it.)
    int  rackNew (const juce::String& name = {});           // an empty one, kept here; where it is
    bool rackCanChange (int index) const;
    bool rackAdd (int index, juce::int64 id, juce::uint32 lanes = kAllLanes);
    void rackDrop (int index, int at);
    void rackSwap (int index, int a, int b);
    void rackMove (int index, int from, int to);
    bool rackInsert (int index, int at, juce::int64 id, juce::uint32 lanes = kAllLanes);
    int  freeStackKnob() const;                             // the first stack knob's place that holds nothing (-1: none is free)
    int  rackOntoFree (int index);                          // the stack onto that place, whole (-1: none is free, or it holds nothing)
    void rackLanes (int index, int at, juce::uint32 lanes);
    void rackRename (int index, const juce::String& name);
    int  rackCopy (int index);                              // that stack kept again under another name (its own, the instrument's); where it is, or -1
    int  rackIndex (const juce::String& id) const;          // -1: no such stack
    // ---- the knobs kept: a collection of macro knobs and stack knobs, each a copy, to put on a place of whatever module is
    // in the chassis. No network is asked.
    std::vector<KeptKnob> keptKnobs;
    int knobKeep (int k);                                   // that knob, as it is, kept under its name (one of its kind kept under that name before is replaced). Its place, or -1: it holds nothing
    enum PutSays { putDone, putWrongKind, putHolds, putNothing };
    PutSays knobPut (int index, int k);                     // a kept knob onto that place: only one of the place's kind, and only where the knob holds nothing
    void knobForget (int index);
    void keptRename (int index, const juce::String& name);  // a kept knob's name
    int  keptOntoFree (int index);                          // a kept knob onto the first place of its kind that holds nothing; that place, or -1 (none is free, or it has nothing to put)
    juce::String keptSays (int index) const;                // what it is, in a line
    // ---- modules. The sixteen knobs here ARE the module in the chassis (moduleAt), and it is always one of the kept
    // ones: what is changed while playing changes that module. LOAD puts another in its place, whole, and the one that
    // was there is kept as it stood. Nothing is heard to change when one is loaded: what the macro knobs were adding
    // stays in the scene, and the new module's knobs start at rest. No network is asked.
    std::vector<Module> modules;
    juce::String moduleAt;                                  // the id of the module in the chassis
    void modulesReady();                                    // there is at least one, and the one in the chassis is among them (a session from before modules: its knobs are MODULE 1)
    int moduleIndex() const;                                // the place among `modules` of the one in the chassis
    juce::String moduleName() const;
    int moduleNew (const juce::String& name = {});          // an empty module, kept, and put in the chassis. Its place
    int moduleCopy (const juce::String& name = {});         // the one in the chassis kept again under another name: the chassis holds the copy from then on. Its place
    bool moduleLoad (int index);                            // that module into the chassis, whole. False: no such one, or it is the one there
    void moduleRename (int index, const juce::String&);
    bool moduleRemove (int index);                          // out of the kept ones. Never the one in the chassis
    juce::String moduleSays (int index) const;              // what it holds, in a line
    // ---- the rack and the web library. ONLINE keeps the two the same, each way: a stack kept here goes there (lit for
    // the instrument, off its module), and one made, changed, unlit or removed there is so here. Where both have changed
    // the instrument's stands. Nothing is sent that names a snapshot the library lacks; nothing is taken until every
    // snapshot it names is here. Session decides (rackPlan) and does its own part (rackDone); Online asks the library.
    juce::Array<juce::var> stacksGone;                      // stacks put out of the rack here that the library is still to be told of: { id, mark, here }
    juce::String stackPrint (const Stack&) const;           // what it is, in a line: two with the same line are the same stack
    juce::String stackPrintOf (const juce::var& there, const std::set<juce::String>& none) const;      // the same of one as the library gives it (none: uids no library has any more)
    juce::var stackBody (const Stack&, bool& whole) const;  // its snapshots as the library takes them. whole: every one has a uid
    struct RackStep
    {
        enum Net { none, post, patch, unlight, removeThere, restore };      // what the library is asked
        enum Here { nothing, setMark, take, drop };                         // what is done here once it has said yes (with none: at once)
        Net net = none; Here here = nothing;
        juce::String id, was, mark;     // was: the stack's line here when this was decided (done only if that still holds). mark: the line the two agree on after
        juce::var body, there;          // body: what is sent. there: the library's stack (take)
        bool gone = false;              // it settles one of stacksGone
    };
    // What is to be done, given every stack the library has (GET /api/stacks). lacking: the uids its stacks for the
    // instrument name that are not here and not in `none`: such a stack is left out of the plan until they are.
    std::vector<RackStep> rackPlan (const juce::var& stacksThere, const std::set<juce::String>& none, juce::StringArray& lacking) const;
    juce::var knobVar (const MacroKnob&) const;             // a knob as the session file keeps one
    MacroKnob knobFrom (const juce::var&) const;            // ... and read back (a snapshot that is gone is left out, a tie to a knob this rig lacks is carried)
    juce::var moduleKnobsNow() const;                       // the sixteen as they stand
    // ---- what a kept thing holds, for the rack to show it
    juce::var moduleKnob (int module, int k) const;         // that knob of a kept module as the file keeps one (the one in the chassis: as it stands)
    struct PartSeen { juce::int64 snap = 0; int bars = 0; };
    std::vector<PartSeen> arrsParts (int index) const;      // a kept arrangement's parts, in order (the one on the page: as it stands)
    int stacksHolding (juce::int64 id, juce::StringArray* names = nullptr) const;   // the stacks that hold that snapshot (a snapshot may be in many)
    // ---- a pile, wherever it is kept: a stack of the rack, a stack knob kept by itself, a stack knob of a module (the one in
    // the chassis: the knob itself, as it plays). The same things are done to each: a chip put in at a place, moved, taken
    // out, and the parts of the rig it takes there (N among them).
    struct PileAt { int what = 0, index = -1, knob = -1; };      // what: 0 none, 1 a stack (index), 2 a knob kept (index), 3 a module's knob (index: the module, knob)
    bool pileIs (const PileAt&) const;                      // there is such a pile
    bool pileCan (const PileAt&) const;                     // and it is changed here (a stack that is the web library's is changed there)
    juce::String pileName (const PileAt&) const;
    std::vector<juce::int64> pileSnaps (const PileAt&) const;
    juce::uint32 pileKeysAt (const PileAt&, int i) const;   // the parts it takes there, and its N
    bool pileInsert (const PileAt&, int at, juce::int64 id, juce::uint32 keys = kAllLanes);     // (at below 0, or past the end: last). False: it cannot be changed, there is no such chip, or it is full
    void pileMove (const PileAt&, int from, int to);
    void pileDrop (const PileAt&, int i);
    void pileKeys (const PileAt&, int i, juce::uint32 keys);
    // ---- a macro knob's ties, where it is kept or stands (PileAt: 2 a knob kept, 3 a knob of a module): each a knob of the
    // rig and how far the macro knob's full turn takes it, in hundredths of that knob's travel. A tie is never nothing:
    // asked for none, it is the nearest of one hundredth either way to where it was (taking it out is how a tie goes).
    // The knob in the chassis is changed as it plays; a knob kept, or a module's that is not in the chassis, where it is kept.
    bool tieIs (const PileAt&) const;                       // there is such a macro knob
    std::vector<MacroKnob::Link> ties (const PileAt&) const;
    bool tieSet (const PileAt&, int i, int hundredths);     // False: no such tie, the knob is being taught, or it is that already
    bool tieDrop (const PileAt&, int i);
    juce::var moduleMotionNow() const;
    bool rackDone (const RackStep&);                        // the step's part here. False: the stack changed meanwhile (next time)
    void rackOrder (const juce::var& stacksThere);          // the library's order; those it has not got after
    // A stack knob holding a snapshot the web library is never given (the rig's own: CLEAN SLATE, INIT and its pushes;
    // or one that is gone): such a knob can never be sent there.
    bool knobStaysHere (int k) const;
    juce::String takeTray (const juce::var& tray, const std::array<KnobPlan, 16>* plan = nullptr);
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
    // KEY and the steps. Parts are written in C, and a lane that follows the key plays them moved by
    // it (up as far as F, down from F#: the suite's own law). What a step SAYS is the note that
    // sounds, and a step newly lit is the key's own note (with no key set: C, as written).
    int keyRoot() const;                                // KEY as a note, C = 0; -1: none is set
    int keyMoves (int lane) const;                      // the semitones the lane's steps sound from where they are written (0: no key, or the lane does not follow it)
    void lightStep (int lane, Step&) const;             // a step as it is first lit: everything as new, its note the key's
    juce::String noteSays (int lane, const Step& s) const   { return noteSays (s, keyMoves (lane)); }     // "A0": the step's note as it sounds
    static juce::String noteSays (const Step&, int moves);
    juce::String stepSays() const;                      // the chosen step in a few letters: "7 D#3", "12", or nothing
    void nudgeLane (int lane, int dir);
    void stepInsert (int lane, int slot);

    // ---- disk (the file's layout and the rules it keeps to: FORMAT.md)
    static constexpr int kFormat = 1;   // rises only for a change an older build could not read past
    // Why nothing is written to the disk this time: the session file is from a newer SF-000, in a
    // format this build cannot read, and is left exactly as it is. Empty: all is kept as usual.
    juce::String notSavingWhy;
    juce::String libraryId;             // this library's own id: every snapshot's uid begins with it
    void touch()                        { dirty = true; ++changes; }
    void save();
    bool load();
    juce::var stateVar (bool withChips = true);     // all the file holds, as it stands (without its chips and the definitions: what ONLINE sends of the session itself)
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
    bool tieChange (const PileAt&, const std::function<bool (MacroKnob&)>&);        // (a macro knob's ties changed, where it is kept or as it plays)
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
    // ---- a slot of a lane's rack, between two snapshots (a stack knob pressed between two, or turned with CUE out)
    // The effect in a slot and its knobs, as a snapshot has them or as the rig stands (from: whose CONFIG and files
    // the effect has if it is to be put in; nullptr: the rig's own, as they are).
    struct SlotSide { int type = -1; std::array<float, kCells> cells; const Snapshot* from = nullptr; SlotSide() { cells.fill (-1.0f); } };
    SlotSide slotOf (const Snapshot*, int lane, int slot) const;        // nullptr: the rig as it stands
    static SlotSide slotBetween (const SlotSide& a, const SlotSide& b, float f);
    void slotTo (int lane, int slot, const SlotSide&, double glideBeats);
    void sweepSlots (const Held& toward, float f, float g0, float g1);
    // The lanes' EQs with a knob's turn: a band's frequency, gain and width go that much of the way; what kind it is, is
    // the snapshot's from half way on.
    void sweepEq (const Held& toward, float f, float g0, float g1);
    static std::array<Engine::EqBand, Engine::kEqBands> eqOf (const Snapshot&, int lane);      // a lane's EQ as a snapshot has it (as it starts, where it says nothing)
    // ---- a lane's instrument between two snapshots (a stack knob turned toward one whose M is lit, with CUE out)
    // What of a lane's instrument cannot travel on a knob: its sample, its CONFIG (its model among it), a menu or a switch.
    bool instrumentDiffers (const Snapshot&, int lane) const;               // from the one the lane has now
    void bringInstrument (const Snapshot&, int lane, std::vector<std::pair<int, float>>* was);      // onto the lane's instrument (was: the menus and switches it changed, as they stood)
    // A lane the knob holds between two: the snapshot it left (0: none) and the one it is going to, whose instrument the
    // one that arrived has; and the menus and switches as they stood, to be put back if the knob comes back.
    struct LaneHold { juce::int64 behind = 0, ahead = 0; std::vector<std::pair<int, float>> was; };
    std::array<LaneHold, kLanes> laneHold;
    void sweepInstruments (const Held& toward, juce::int64 behind, float g1, bool arrived);
    void settleHolds();                 // the set is going somewhere by another way: each held lane settles on the one that arrived
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
    int arrTransFor = -1;                               // (the part the TRANSITION was last set off for, while the arrangement plays)
    struct TransPend { int kind = 0; juce::int64 id = 0; Scope scope; int ctl = -1; float v = 0.0f; bool sounding = true, onAudio = false; };
    std::vector<TransPend> transPend;                   // what waits for the line: recalls (kind 0) and controls set (1)
    bool transDoing = false;
    int transLevelAt = 3, transDubAt = 0, transSpaceAt = 2;
    std::array<juce::String, 2> transRef;               // (a slot's file as the set's file said it, when it could not be found: kept, and written back)
    bool transDefer (TransPend);
    void transApply();
    void finishApply();
    juce::var transToVar() const;
    void transFromVar (const juce::var&);
    double arrZero = 0.0;               // the song position at which the arrangement's first bar begins
    double glideOver = -1.0;            // a recall travelling over a time of its own (below 0: by GLIDE)
    Snapshot arrEditWas;                // EDIT: the rig as it was brought, to tell what was changed
    bool arrBringNext = false;          // FROM HERE while the clock runs: the part that comes in is brought with all that came before it
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
    juce::ThreadPool logWriter { 1 };
    juce::String logRun;                // (this run's own mark in a line's name)
    int logCount = 0;
    bool logRunWas = false;             // (the clock as the log last saw it)
    const char* logFrom = nullptr;      // (what a recall about to be made is from: a stack knob's press)
    int logKnob = -1;
    bool cueDo (juce::int64 id);
    juce::int64 cueId = 0;
    juce::int64 cueScopeFor = 0; Scope cueScope;                // (the scope a cue of that snapshot is narrowed to)
    int cueNotesWish = -1;                                      // (and whether its recall would bring its notes: -1 as NOTES is set)
    bool cueByRig = false;
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
