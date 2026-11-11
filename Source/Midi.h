#pragma once
// SF-000 MIDI: notes in for the lanes, the OXI E16 (knobs in, rings out) and the clock out.
//   Notes: any input, a lane's number is its channel. They go straight to the engine.
//   E16: the scene SF-000 writes itself (SF-000 --e16 file.oxie16), from the same table as below.
//   Clock: 24 ticks a beat with START and STOP, to every output that is not the E16.
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

    // ---- what is connected
    juce::StringArray inputNames() const;
    juce::StringArray clockNames() const;
    juce::String e16Name() const;

    // A message as if it had come in (the inputs call this; so do tests). Any thread.
    void incoming (const juce::MidiMessage&, bool fromE16);
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
    static juce::File file();

    Engine& engine;
    Session& ses;
    const bool useDevices;
    bool clockWanted = true;
    float leadMs = 0.0f;

    // from the inputs' threads to the message thread
    std::array<std::atomic<int>, e16::kSlots> latest, sent;
    std::array<std::atomic<juce::uint32>, e16::kSlots> heardAt;
    std::array<std::atomic<int>, 16> presses;
    std::atomic<int> focus { 0 };
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
