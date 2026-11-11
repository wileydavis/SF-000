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
};

constexpr juce::int64 kCleanSlateId = 999;

// A snapshot knob: 2 to 10 snapshots in sweep order. Turning it moves every knob that differs
// among them, as an offset from the base scene, so knobs stack.
struct MacroKnob
{
    juce::String name;
    std::vector<juce::int64> snaps;
    float depth = 1.0f;
};

class Session : private juce::Timer
{
public:
    explicit Session (Engine&, bool useDisk = true);
    ~Session() override;

    Engine& engine;

    // ---- what the screens show
    int activeLane = 7;
    int selStep = 6;                    // the selected step of the active lane; -1 none
    int page = 0;                       // which 16 steps are shown
    std::array<int, kLanes> genType;    // the armed generator per lane; -1 none

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
    // Cue: a snapshot's clip in the headphones, apart from the set (0, or one with no clip: silence).
    // The rig itself is not touched.
    bool cue (juce::int64 id);          // false: that snapshot has no sound kept
    juce::int64 cueing() const          { return cueId; }
    void turnSnapKnob (int k, float v);
    void pressSnapKnob (int k);         // recall the knob's nearest snapshot as the new base; the knob goes home
    Snapshot capture() const;
    juce::int64 take();                 // the SNAPSHOT key
    void recall (juce::int64 id);       // it becomes the base scene; sound knobs travel by GLIDE
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

    // ---- disk
    void touch()                        { dirty = true; }
    void save();
    bool load();
    static juce::File stateFile();
    static inline juce::File stateFileForTests;     // tests: the session is kept here instead
    static void backUp();               // a dated copy of the session as it was found, the last twenty kept
    std::function<void()> onChange;

private:
    void timerCallback() override;
    void applyMorph (double glideBeats);
    void applySnapshot (const Snapshot&, double glideBeats);
    void regenerate (int lane);
    void makeFactory();
    void ensureCleanSlate();
    void warmInserts();                 // every file-holding insert the library's snapshots name, read ahead
    std::array<Engine::SetupState, 2> retInit;
    double glideBeats() const;

    std::vector<float> base, offset;
    bool dirty = false, useDisk = true;
    juce::ThreadPool clipWriter { 1 };
    juce::int64 cueId = 0;
    // the clips handed to the engine: each kept until a second after it was replaced
    std::vector<std::pair<std::unique_ptr<Engine::CueClip>, juce::uint32>> cueKept;
    void pruneCues();
    void keepClip (juce::int64 id, const Engine::Heard&);
};
} // namespace sf0
