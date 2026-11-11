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
    bool glides = false;            // a move of it travels over the GLIDE time: every sound knob, an insert's knobs too
    int steps() const               { return choices.size(); }
};

// Where every control sits in the engine's table.
namespace cx
{
    constexpr int kSendA = kLaneKnobs, kSendB = kSendA + 1, kEqLow = kSendB + 1, kEqMid = kEqLow + 1, kEqHigh = kEqMid + 1;
    constexpr int kIns = kEqHigh + 1;                       // kSlots * kCells insert cells
    constexpr int kGen = kIns + kSlots * kCells;            // HITS, ROTATE, ACCENTS, SEED
    constexpr int kMute = kGen + 4, kSolo = kMute + 1;      // the lane's MUTE and SOLO
    constexpr int kInsMS = kSolo + 1;                       // MUTE and SOLO for each insert slot
    constexpr int kPerLane = kInsMS + kSlots * 2;
    constexpr int kRet = kLanes * kPerLane;                 // 4 knobs for A, 4 for B as REVERB, 4 for B as CONVOLUTION
    constexpr int kRetKnobs = 4 + 4 * kSpaceModels;
    constexpr int kRetMS = kRet + kRetKnobs;                // A MUTE, A SOLO, B MUTE, B SOLO
    constexpr int kMaster = kRetMS + 4;                     // TAPE, COMP, LIMIT
    constexpr int kTempo = kMaster + 3, kKey = kTempo + 1, kGlide = kKey + 1;
    constexpr int kSnap = kGlide + 1;                       // the 16 snapshot knobs
    constexpr int kCount = kSnap + 16;
    inline int lane (int l, int k)              { return l * kPerLane + k; }
    inline int ins (int l, int slot, int cell)  { return l * kPerLane + kIns + slot * kCells + cell; }
    inline int gen (int l, int k)               { return l * kPerLane + kGen + k; }
    inline int insMute (int l, int slot)        { return l * kPerLane + kInsMS + slot * 2; }
    inline int insSolo (int l, int slot)        { return insMute (l, slot) + 1; }
    inline int space (int model, int k)         { return kRet + 4 + model * 4 + k; }      // return B's knob k on that model
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
    // onBarLine: while the clock runs the lane takes it up at the next bar line (a recall). Otherwise at once (an edit).
    void setPattern (int lane, const Pattern&, bool onBarLine = false);
    static Pattern defaultPattern (int lane);

    // ---- MIDI in: notes for the lanes. A lane's number is its channel. Any thread.
    // arrivedMs: juce::Time::getMillisecondCounterHiRes() when it came. It sounds one block later, at
    // the place in the block that matches when it came, so notes keep their spacing to the sample.
    void noteIn (const juce::MidiMessage&, double arrivedMs);
    void noteInAt (const juce::MidiMessage&, int sampleInNextBlock);     // tests: exactly there
    float notesLateMs() const                   { return lateMs.load(); }   // incoming notes against the sixteenths, averaged (+ late)
    int notesHeard() const                      { return lateCount.load(); }

    // ---- MIDI clock out: START, 24 ticks a beat while it runs, STOP, for whatever follows this rig. The clock
    // runs ahead of the sound by two blocks and LEAD, so a follower's notes are back here in time
    // to sound on the beat; RUN waits that long (a few ms) before the first sound.
    struct ClockMsg { double dueMs = 0.0; juce::int64 sample = 0; juce::uint8 byte = 0; };
    void setClockOut (bool on, float leadMs)    { clockOn = on; clockLead = juce::jlimit (0.0f, 60.0f, leadMs); }
    bool clockOut() const                       { return clockOn.load(); }
    float clockLeadMs() const                   { return clockLead.load(); }
    bool nextClock (ClockMsg&);                 // the sender's thread: the oldest one waiting

    // ---- inserts: -1 empty, else an index into effects()
    int insertType (int lane, int slot) const   { return insType[(size_t) lane][(size_t) slot]; }
    using SetupState = std::vector<std::pair<juce::String, float>>;
    // setup: the effect's CONFIG as it should be from its first sound (a snapshot's), else as it comes
    void setInsertType (int lane, int slot, int type, bool defaults = true, const SetupState* setup = nullptr);

    // ---- what a CONFIG panel, a dropped file or a setup change is aimed at
    struct Target { int kind = 0, lane = 0, slot = 0; };    // kind 0 a lane's instrument, 1 one of its inserts, 2 return A (slot 0) or B (slot 1)

    // ---- return B (SPACE) is one of two reverbs: 0 REVERB, 1 CONVOLUTION. Both stay built, so the
    // places a convolution holds are still there when it comes back; only the chosen one runs.
    int spaceModel() const                      { return spaceWant.load(); }
    void setSpaceModel (int model);

    // ---- dropped audio files, for a lane, insert or return whose module takes them. A convolution
    // keeps eight places: the files go to the place its PLACE is on (several fill the next ones).
    struct FileRef
    {
        int place = 0, type = -1;               // type: the insert effect they were given to
        juce::StringArray files;
        bool operator== (const FileRef& o) const { return place == o.place && type == o.type && files == o.files; }
    };
    bool acceptsFiles (Target) const;
    bool loadFiles (Target, const juce::StringArray& paths);
    bool loadFile (int lane, const juce::File& f)   { return loadFiles ({ 0, lane, 0 }, juce::StringArray (f.getFullPathName())); }
    juce::String laneFile (int lane) const      { return files[(size_t) lane]; }
    const std::vector<FileRef>& heldFiles (Target) const;
    // A session opening: a return reads them at once, an insert when that effect is next in the slot.
    void setHeldFiles (Target, const std::vector<FileRef>&);
    // An insert's files travel with snapshots. What the effect in a slot holds now (none if it takes
    // none); whether it is this effect holding exactly these; and an effect made ready ahead of
    // time with its files read, so that a recall puts it in without going to the disk.
    std::vector<FileRef> insertFiles (int lane, int slot) const;
    bool insertHolds (int lane, int slot, int type, const std::vector<FileRef>&) const;
    void warmInsert (int lane, int slot, int type, const std::vector<FileRef>&);
    void expectInsertFiles (int lane, int slot, const std::vector<FileRef>&);    // what the next effect put in that slot is to hold (nothing is read)

    // ---- setup: every parameter of a hosted module that the performance screen does not show
    struct SetupParam { juce::String id, name, text; Kind kind = Kind::Knob; juce::StringArray choices; float value = 0.0f, def = 0.0f; };
    SFProcessor* processorFor (Target) const;
    std::vector<SetupParam> setupParams (Target) const;
    void setSetup (Target, const juce::String& id, float v);
    SetupState getSetup (Target) const;
    void applySetup (Target, const SetupState&);
    const SetupState& initialSetup (int lane) const { return initSetup[(size_t) lane]; }
    bool isGliding (int i) const                { return glideV[(size_t) i].load() > 0.0f && std::abs (shownV[(size_t) i].load() - targetV[(size_t) i].load()) > 1e-4f; }

    // ---- what the screen reads
    std::atomic<float> peakL { -100.0f }, peakR { -100.0f }, gainRed { 0.0f }, lufs { -70.0f };
    std::atomic<bool> clipL { false }, clipR { false }, fault { false };
    std::atomic<double> cpu { 0.0 };
    std::array<std::atomic<float>, kLanes> laneDb;           // each lane's level as it reaches the mix (dB, falling)
    std::array<double, kLanes> laneSq {};                    // sum of squares since the start (tests)
    std::array<float, kLanes> laneMax {};                    // highest sample since the start (tests)
    std::atomic<int> solo { -1 };                            // tests: only this lane reaches the mix
    // tests: where a block's time goes, in seconds summed, while profile is on
    bool profile = false;
    std::array<double, kLanes> profLane {}, profIns {};
    double profRet = 0.0, profMaster = 0.0, profAll = 0.0;
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
        int type = -1, mixCell = -1;
        int lane = -1, slot = -1;                   // where it was made for
        std::vector<FileRef> files;                 // the files it holds (a convolution's places)
        std::atomic<float>* dryRaw = nullptr;       // the effect's own DRY switch, if it has one
        float dryWas = 1.0f, gWas = 1.0f;
        bool soloOn = false;
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
        float sendWas[2] = { 0, 0 }, gainWas = 1.0f, hold = -100.0f;
        std::vector<float> dl[2];
        int dlPos = 0;
        std::atomic<int> delay { 0 };
        Pattern shared;                         // under patLock
        std::atomic<bool> patDirty { true }, patWait { false };    // patWait: taken up at the next bar line
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
    std::unique_ptr<SFProcessor> retA;
    std::array<std::unique_ptr<SFProcessor>, kSpaceModels> space;
    std::atomic<int> spaceWant { 0 };
    int spaceNow = 0, spaceHold = 0;                         // audio thread: the one that runs; samples it stays silent after a change
    std::vector<FileRef> spaceFiles;
    std::array<std::array<std::vector<FileRef>, kSlots>, kLanes> insFiles;
    void replayFiles (SFProcessor&, const std::vector<FileRef>&);
    // Message thread: file-holding inserts that were taken out, or made ahead for a snapshot. They
    // keep their places loaded, so they go back in with the right room from the first block.
    std::vector<InsertInst*> parked;
    InsertInst* takeParked (int lane, int slot, int type, const std::vector<FileRef>&);
    void park (InsertInst*);
    static void applySetupTo (SFProcessor&, const std::vector<std::string>& bound, const SetupState&);
    std::vector<std::unique_ptr<SFProcessor>> master;
    std::vector<std::pair<InsertInst*, uint64_t>> retired;
    juce::SpinLock patLock;

    double sr = 48000.0;
    int maxBlock = 512;
    bool prepared = false;
    juce::AudioBuffer<float> buf, mix, sendA, sendB, work, pre;
    std::array<SetupState, kLanes> initSetup;
    std::array<juce::String, kLanes> files;
    std::vector<std::string> boundIds (Target) const;
    float dryMixWas = 1.0f, retGainWas[2] = { 1.0f, 1.0f };
    juce::MidiBuffer noMidi;

    // MIDI in and clock out (audio thread, but for the queues' other ends)
    struct NoteEv { juce::uint8 b[3] = { 0, 0, 0 }; double ms = 0.0; int at = -1; };
    static constexpr int kNoteQ = 1024, kClockQ = 512;
    juce::AbstractFifo noteFifo { kNoteQ }, clockFifo { kClockQ };
    std::array<NoteEv, kNoteQ> noteQ;
    std::array<ClockMsg, kClockQ> clockQ;
    juce::SpinLock noteWrite;
    std::array<NoteEv, 128> noteKept;           // came after this block's start: for the next one
    int noteKeptN = 0;
    juce::MidiBuffer liveMidi;
    void gatherNotes (int n, double spb);
    void pushClock (juce::uint8 byte, double dueMs, juce::int64 sample);
    std::atomic<bool> clockOn { false };
    std::atomic<float> clockLead { 0.0f }, lateMs { 0.0f };
    std::atomic<int> lateCount { 0 };
    double wallMs = -1.0, virtualMs = 0.0, blockMs = 0.0, prevBlockMs = 0.0, lastDurMs = 0.0, clkPpq = 0.0;
    bool wallValid = false;

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
