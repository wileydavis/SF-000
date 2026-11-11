#pragma once
// SF-000 session (message thread): what the screens edit. The base scene, the snapshot library,
// the sixteen snapshot knobs and their morph, the armed generators, and the file it is all kept in.
#include "Engine.h"
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
    void userSet (int i, float v);      // a knob or key moved by hand
    float value (int i) const           { return engine.target (i); }

    // ---- snapshots
    std::vector<Snapshot> library;
    std::array<MacroKnob, 16> knobs;
    juce::int64 baseId = 0;             // the snapshot last recalled or taken
    juce::String lastTakenAt { "NONE YET" };
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
    std::function<void()> onChange;

private:
    void timerCallback() override;
    void applyMorph (double glideBeats);
    void applySnapshot (const Snapshot&, double glideBeats);
    void regenerate (int lane);
    void makeFactory();
    void ensureCleanSlate();
    std::array<Engine::SetupState, 2> retInit;
    double glideBeats() const;

    std::vector<float> base, offset;
    bool dirty = false, useDisk = true;
};
} // namespace sf0
