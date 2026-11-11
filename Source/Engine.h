#pragma once
// SF-000 engine: one clock, eleven lanes of the suite's instruments, three inserts a lane, two
// returns, a built-in lane EQ and master chain, in one fixed graph with delay compensation.
// Every screen control is a Control with a stable id and a value from 0 to 1; the engine glides
// values on the audio thread and hands them to the suite's processors.
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <memory>
#include "Rig.h"
#include "Sequencer.h"

class SFProcessor;

namespace sf0
{
enum class Kind { Knob, Bipolar, List, Toggle, Hold };

struct Control
{
    juce::String id, label;
    Kind kind = Kind::Knob;
    juce::StringArray choices;      // List: the names, in order
    float def = 0.0f;
    bool used = true;               // false: an empty place (an insert cell with nothing in it)
    bool snapshot = true;           // stored in snapshots
    bool morph = false;             // a snapshot knob may move it (sound knobs only)
    int steps() const               { return choices.size(); }
};

// Where every control sits in the engine's table.
namespace cx
{
    constexpr int kSendA = kLaneKnobs, kSendB = kSendA + 1, kEqLow = kSendB + 1, kEqMid = kEqLow + 1, kEqHigh = kEqMid + 1;
    constexpr int kIns = kEqHigh + 1;                       // kSlots * kCells insert cells
    constexpr int kGen = kIns + kSlots * kCells;            // HITS, ROTATE, ACCENTS, SEED
    constexpr int kPerLane = kGen + 4;
    constexpr int kRet = kLanes * kPerLane;                 // 4 for A, 4 for B
    constexpr int kMaster = kRet + 8;                       // TAPE, COMP, LIMIT
    constexpr int kTempo = kMaster + 3, kKey = kTempo + 1, kGlide = kKey + 1;
    constexpr int kSnap = kGlide + 1;                       // the 16 snapshot knobs
    constexpr int kCount = kSnap + 16;
    inline int lane (int l, int k)              { return l * kPerLane + k; }
    inline int ins (int l, int slot, int cell)  { return l * kPerLane + kIns + slot * kCells + cell; }
    inline int gen (int l, int k)               { return l * kPerLane + kGen + k; }
}

double glideBarsFor (float v);      // the GLIDE list: OFF, 1/4 bar ... 256 bars
double tempoFor (float v);          // 60..200 bpm
float tempoToValue (double bpm);

class Engine : public juce::AudioIODeviceCallback, private juce::AudioPlayHead
{
public:
    Engine();
    ~Engine() override;

    // Message thread, with the audio stopped (the device callback does it before it starts).
    void prepare (double sampleRate, int maxBlock);
    // One block: out has 2 channels and at most maxBlock samples. The audio thread, or a test.
    void render (juce::AudioBuffer<float>& out);

    // ---- controls (message thread)
    const Control& control (int i) const        { return controls[(size_t) i]; }
    int find (const juce::String& id) const;
    float shown (int i) const                   { return shownV[(size_t) i].load(); }     // what sounds now
    float target (int i) const                  { return targetV[(size_t) i].load(); }    // where it is going
    void set (int i, float v, double glideBeats = 0.0);
    juce::String valueText (int i) const;

    // ---- transport
    void setRunning (bool r)                    { running = r; }
    bool isRunning() const                      { return running.load(); }
    double songPos() const                      { return ppqShown.load(); }
    double tempo() const                        { return tempoFor (target (cx::kTempo)); }
    void panic();

    // ---- patterns
    Pattern pattern (int lane) const;
    void setPattern (int lane, const Pattern&);
    static Pattern defaultPattern (int lane);

    // ---- inserts: -1 empty, else an index into effects()
    int insertType (int lane, int slot) const   { return insType[(size_t) lane][(size_t) slot]; }
    void setInsertType (int lane, int slot, int type, bool defaults = true);

    // A dropped audio file, for a lane whose module takes one.
    bool loadFile (int lane, const juce::File&);

    // ---- what the screen reads
    std::atomic<float> peakL { -100.0f }, peakR { -100.0f }, gainRed { 0.0f }, lufs { -70.0f };
    std::atomic<bool> clipL { false }, clipR { false }, fault { false };
    std::atomic<double> cpu { 0.0 };
    std::array<std::atomic<float>, kLanes> lanePeak;         // each lane's level after its EQ (linear, falling)
    std::array<double, kLanes> laneSq {};                    // sum of squares since the start (tests)
    std::atomic<int> solo { -1 };                            // tests: only this lane reaches the mix
    double sampleRate() const                   { return sr; }
    int latencySamples() const                  { return maxLat.load(); }

    // ---- the audio device
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const*, int, int, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override {}

private:
    juce::Optional<PositionInfo> getPosition() const override;

    struct Bind
    {
        std::atomic<float>* raw = nullptr;
        juce::RangedAudioParameter* param = nullptr;
        float lo = 0.0f, hi = 1.0f;
        int n = 0;                              // > 0: a menu or switch with n positions
    };
    struct InsertInst
    {
        std::unique_ptr<SFProcessor> proc;
        Bind bind[kCells];
        int type = -1;
        InsertInst();
        ~InsertInst();
    };
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1[2] = { 0, 0 }, z2[2] = { 0, 0 };
        float tick (float x, int ch) { const float y = b0 * x + z1[ch]; z1[ch] = b1 * x - a1 * y + z2[ch]; z2[ch] = b2 * x - a2 * y; return y; }
    };
    struct Lane
    {
        std::unique_ptr<SFProcessor> proc;
        std::atomic<InsertInst*> ins[kSlots];
        LanePlayer player;
        juce::MidiBuffer midi;
        Biquad eq[3];
        float eqDb[3] = { 0, 0, 0 }, eqSet[3] = { 99, 99, 99 };
        float sendWas[2] = { 0, 0 };
        std::vector<float> dl[2];
        int dlPos = 0;
        std::atomic<int> delay { 0 };
        Pattern shared;                         // under patLock
        std::atomic<bool> patDirty { true };
        Lane();
        ~Lane();
    };

    std::unique_ptr<SFProcessor> makeProc (const char* module, const std::vector<Fixed>& fixed);
    Bind makeBind (SFProcessor&, const KnobDef&, Control* describe);
    static float readBind (const Bind&);
    static void applyBind (const Bind&, float v);
    void advanceControls (double beats);
    void setEq (Lane&, int band, float db);
    void updateLatency();
    void collectRetired (bool all);

    std::vector<Control> controls;
    std::array<Bind, cx::kCount> bind {};
    std::array<std::atomic<float>, cx::kCount> targetV, shownV, glideV;
    std::array<std::atomic<uint32_t>, cx::kCount> seqV;
    // audio thread
    std::array<float, cx::kCount> cur {}, gFrom {}, gTo {}, applied {};
    std::array<double, cx::kCount> gStart {}, gLen {};
    std::array<uint32_t, cx::kCount> seen {};
    std::array<bool, cx::kCount> gliding {};

    std::array<Lane, kLanes> lane;
    std::array<std::array<int, kSlots>, kLanes> insType {};
    std::unique_ptr<SFProcessor> retA, retB;
    std::vector<std::unique_ptr<SFProcessor>> master;
    std::vector<std::pair<InsertInst*, uint64_t>> retired;
    juce::SpinLock patLock;

    double sr = 48000.0;
    int maxBlock = 512;
    bool prepared = false;
    juce::AudioBuffer<float> buf, mix, sendA, sendB, work;
    juce::MidiBuffer noMidi;

    std::atomic<bool> running { false }, panicNow { false };
    bool wasRunning = false;
    double ppq = 0.0, gClock = 0.0, blockBpm = 132.0;
    int64_t samplesDone = 0;
    std::atomic<double> ppqShown { 0.0 };
    std::atomic<uint64_t> blockCount { 0 };
    std::atomic<int> maxLat { 0 };
    struct Loud;
    std::unique_ptr<Loud> loud;
    float peakHold[2] = { -100.0f, -100.0f };
};
} // namespace sf0
