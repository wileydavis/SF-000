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
    constexpr int kMapChannel = 15, kFindChannel = 14, kLibKnobs = 32;      // 0-15 the map, 16-31 the finders
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
    juce::String libraryText();                                 // the library's scene
}

class Midi : private juce::MidiInputCallback, private juce::Timer, private juce::Thread
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
    std::atomic<int> focus { 0 };
    std::atomic<int> pageUp { 0 }, glideIn { -1 };
    int pageNow = 0, pageWait = 0, glideSent = -1;              // message thread
    juce::uint32 glideHeardAt = 0;
    // the library's scene
    std::array<std::atomic<int>, e16::kLibKnobs> libLatest, libSent, libPresses;
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
