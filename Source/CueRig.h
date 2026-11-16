#pragma once
// A cue that is the rig (DECISIONS 595 on). What plays in the headphones for a snapshot was the
// four bars recorded when it was taken: that day's notes and tempo, all of the rig whatever keys
// were out, the master as it stood. This is a second rig, made what the first is now and then
// given the recall the cue stands for, and its own sound is what plays.
//
// It runs in a process of its own (this same program, started with a flag): it has its own
// memory, its own copy of the suite's bus, and its own core. Nothing it does can reach the set:
// if it is slow, late or gone, the headphones fall silent or go back to the clips, and the first
// rig's audio thread has done nothing but read a block of shared memory (CueShared.h).
//
//   CueRig     in the instrument: starts the second process, hands it each cue, knows if it is there
//   CueWorker  in the second process: the rig, and the thread that renders it in step with the first
#include <juce_events/juce_events.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include <memory>
#include "CueShared.h"

namespace sf0
{
class Engine;
class Session;

// The shared block: made under a name by the instrument, opened by that name in the second process.
class CueMemory
{
public:
    CueMemory() = default;
    ~CueMemory();
    bool create();                                  // a new one, cleared
    bool open (const juce::String& name);
    void forgetName();                              // (the maker's: once the other side has it open the name is no longer needed, and nothing is left behind)
    CueShared* get() const                          { return data; }
    const juce::String& name() const                { return shmName; }

private:
    CueShared* data = nullptr;
    juce::String shmName;
    bool named = false;
    JUCE_DECLARE_NON_COPYABLE (CueMemory)
};

class CueRig : private juce::ChildProcessCoordinator,
               private juce::Timer
{
public:
    CueRig (Engine&, Session&);
    ~CueRig() override;

    // Starts the second process (the audio must be prepared: it is told the rate). Nothing waits for it: until it
    // answers, up() is false and a cue is a clip.
    void start();
    void stop();
    bool up() const                                 { return state == ready; }
    // A cue: that snapshot as its recall would leave the rig now. False: no second rig (the caller plays the clip).
    bool play (juce::int64 id, juce::uint32 lanes, bool notes);
    void quiet();
    // One line for SET UP: whether it is there, how long its last cue took to be ready, what it costs.
    juce::String says() const;
    int lastReadyMs() const                         { return readyMs; }
    int cuesPlayed() const                          { return played; }
    std::function<void()> onChange;                 // it came up, went, or a cue fell back to its clip

    static juce::String uid()                       { return "sf000cue"; }
    void killForTests();                            // the second process dies where it stands

private:
    void handleMessageFromWorker (const juce::MemoryBlock&) override;
    void handleConnectionLost() override;
    void timerCallback() override;
    void took (const juce::String& text);
    void lost (const juce::String& why);
    void send (const juce::var&);
    void endWorker();                               // the second process is told to go, and made to if it does not

    Engine& engine;
    Session& ses;
    enum State { off, starting, ready, gone } state = off;
    std::unique_ptr<CueMemory> mem;
    std::vector<std::unique_ptr<CueMemory>> retired;        // (blocks the audio thread may still be reading: kept until this object goes)
    double openedRate = 0.0;
    int serial = 0, awaiting = 0, readyMs = 0, played = 0, fellBack = 0, starts = 0, workerPid = 0;
    juce::uint32 sentAt = 0, startedAt = 0, goneAt = 0;
    juce::int64 lastId = 0; juce::uint32 lastLanes = 0, lastChanges = 0, lastStamp = 0; bool lastNotes = false;
    bool skipOnce = false;
    juce::String why;
    JUCE_DECLARE_WEAK_REFERENCEABLE (CueRig)
    JUCE_DECLARE_NON_COPYABLE (CueRig)
};

class CueWorker : private juce::ChildProcessWorker,
                  private juce::Thread
{
public:
    CueWorker();
    ~CueWorker() override;
    // Is this process to be the second rig (the flag the instrument starts it with is on its command line)?
    static bool wanted (const juce::String& commandLine);
    bool begin (const juce::String& commandLine);

private:
    void handleMessageFromCoordinator (const juce::MemoryBlock&) override;
    void handleConnectionLost() override;
    void run() override;
    void took (const juce::String& text);
    void say (const juce::var&);
    void rest();                                    // the render thread is between buffers and stays there until play is set again

    static constexpr int kBlock = 128;
    CueMemory mem;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<Session> ses;
    double rate = 48000.0;
    std::atomic<bool> play { false }, resting { true };
    std::atomic<int> serialNow { 0 }, arrived { 0 };
    JUCE_DECLARE_WEAK_REFERENCEABLE (CueWorker)
    JUCE_DECLARE_NON_COPYABLE (CueWorker)
};
} // namespace sf0
