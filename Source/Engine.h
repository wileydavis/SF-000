#pragma once
// SF-000 engine: one clock, eleven lanes of the suite's instruments, three inserts a lane, two
// returns, a built-in lane EQ and master chain, in one fixed graph with delay compensation.
// Every screen control is a Control with a stable id and a value from 0 to 1; the engine glides
// values on the audio thread and hands them to the suite's processors.
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <map>
#include <memory>
#include "Rig.h"
#include "Sequencer.h"
#include "Pockets.h"
#include "Transition.h"
#include "CueShared.h"

class SFProcessor;
namespace sf { struct FinishSays; }

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
    constexpr int kSeqMute = kInsMS + kSlots * 2;           // the lane's own sequencer muted: its steps stay, unplayed
    constexpr int kPk = kSeqMute + 1;                       // its pocket: HOME, WIDTH, HOLD, YIELD
    constexpr int kPerLane = kPk + kPkLane;
    constexpr int kRet = kLanes * kPerLane;                 // 4 knobs for A, 4 for B as REVERB, 4 for B as CONVOLUTION
    constexpr int kRetKnobs = 4 + 4 * kSpaceModels;
    constexpr int kBus = kRet + kRetKnobs;                  // the drum bus's four knobs: DRIVE, CRUNCH, PUNCH, BOOM
    constexpr int kPkG = kBus + kBusKnobs;                  // POCKETS, for the whole rig: AMOUNT, CLEAR, SPEED, RETURNS
    constexpr int kRetMS = kPkG + kPkGlobals;               // A MUTE, A SOLO, B MUTE, B SOLO
    constexpr int kMaster = kRetMS + 4;                     // TAPE, COMP, LIMIT, and GAIN: the whole mix's level into them
    constexpr int kGain = kMaster + 3, kClip = kMaster + 4;     // (CLIP: how hard the limiter's clipper takes the peaks before it)
    constexpr int kTempo = kMaster + 5, kKey = kTempo + 1, kGlide = kKey + 1;
    // MOTION on the snapshot knobs: three of them, each a WAVE, a SPEED, the KNOB it is on and an AMOUNT
    constexpr int kMotUnits = 3, kMotKnobs = 4;
    constexpr int kMot = kGlide + 1;
    constexpr int kSnap = kMot + kMotUnits * kMotKnobs;     // the 16 snapshot knobs
    constexpr int kCount = kSnap + 16;
    inline int mot (int unit, int k)            { return kMot + unit * kMotKnobs + k; }  // 0 WAVE, 1 SPEED, 2 KNOB, 3 AMOUNT
    inline bool isMot (int i)                   { return i >= kMot && i < kSnap; }
    inline int lane (int l, int k)              { return l * kPerLane + k; }
    inline int ins (int l, int slot, int cell)  { return l * kPerLane + kIns + slot * kCells + cell; }
    inline int gen (int l, int k)               { return l * kPerLane + kGen + k; }
    inline int insMute (int l, int slot)        { return l * kPerLane + kInsMS + slot * 2; }
    inline int insSolo (int l, int slot)        { return insMute (l, slot) + 1; }
    inline int space (int model, int k)         { return kRet + 4 + model * 4 + k; }      // return B's knob k on that model
    inline int pk (int l, int k)                { return l * kPerLane + kPk + k; }        // a lane's pocket: 0 HOME, 1 WIDTH, 2 HOLD, 3 YIELD
    inline bool isPk (int i)                    { return i < kRet ? (i % kPerLane) >= kPk : (i >= kPkG && i < kPkG + kPkGlobals); }
}

double glideBarsFor (float v);      // the GLIDE list: OFF, 1/4 bar ... 256 bars
double tempoFor (float v);          // 60..200 bpm
float tempoToValue (double bpm);

class Engine : public juce::AudioIODeviceCallback, private juce::AudioPlayHead
{
public:
    Engine();
    ~Engine() override;

    // What this build's lanes, effects and returns are, each written out in full with a print of it
    // (a hash of the definition). A snapshot says which prints it was made with, and the session
    // file keeps the definitions, so a stored value can always be read for what it meant.
    // Message thread; worked out once.
    struct Defs
    {
        juce::StringArray slots;                                    // the lanes' slugs, in order
        std::array<juce::String, kLanes> lane;                      // each lane's print
        std::vector<std::pair<juce::String, juce::String>> fx;      // each effect: its name, its print
        juce::String dub, space, conv, bus, pockets, chassis;
        std::vector<std::pair<juce::String, juce::var>> all;        // every definition by its print (the sound engines' too)
    };
    const Defs& defs() const;

    // Message thread, with the audio stopped (the device callback does it before it starts).
    void prepare (double sampleRate, int maxBlock);
    // One block: out has 2 channels and at most maxBlock samples. The audio thread, or a test.
    void render (juce::AudioBuffer<float>& out);

    // ---- controls (message thread)
    const Control& control (int i) const        { return controls[(size_t) i]; }
    int find (const juce::String& id) const;
    float shown (int i) const                   { return shownV[(size_t) i].load(); }     // what sounds now
    float target (int i) const                  { return targetV[(size_t) i].load(); }    // where it is going
    float stands (int i) const                  { return baseV[(size_t) i].load(); }      // where it stands or has travelled to: what sounds, less MOTION's push
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
    // onBarLine: taken up on the next bar line while the clock runs. afterBeats (with it): not before a move that sets off
    // on the next beat and takes that long has ended (a recall's notes come in when its glide is over).
    void setPattern (int lane, const Pattern&, bool onBarLine = false, double afterBeats = 0.0);
    // A pitch wheel on a lane's channel (-1 to 1): it adds to the lane's TUNE, kBendSemis either way at its ends. The
    // note lanes, RUMBLE and VOICE follow it while a note sounds; a drum takes it as each hit starts. Any thread.
    static constexpr float kBendSemis = 2.0f;
    void bendIn (int lane, float v)             { if (lane >= 0 && lane < kLanes) bendWant[(size_t) lane] = juce::jlimit (-1.0f, 1.0f, v); }
    float bend (int lane) const                 { return lane >= 0 && lane < kLanes ? bendWant[(size_t) lane].load() : 0.0f; }
    // ---- what the master is doing, as its limiter itself says it (dB; a reduction is a positive figure)
    std::atomic<float> limGr { 0.0f }, clipGr { 0.0f }, autoTrim { 0.0f }, trimWanted { 0.0f }, truePeak { -100.0f };
    // HOLD: the limiter's automatic trim put where the last minute's loudest asks for it (or at db, if given) and held
    // there; off, it rides again from where it was held. Kept with the set by the session. Message thread.
    void setHold (bool on, float db = 1000.0f);
    bool hold() const;
    float heldDb() const;
    // ---- each lane's short-term loudness as it reaches the mix (LUFS; -70: silent)
    std::array<std::atomic<float>, kLanes> laneLufs;
    // ---- what a lane's EQ costs: its level after the EQ against before it, over the last seconds (dB), for the lane
    // being watched (-1: none, and nothing is measured)
    void watchEq (int lane)                     { eqWatch = lane; }
    float eqCost() const                        { return eqCostDb.load(); }
    int eqWatched() const                       { return eqWatch.load(); }
    int lowWatched() const                      { return lowWatch.load(); }
    // a lane's pocket as it is set: 0 its depth, 1 its shape, 2 whether it is made after the lane's effects (0: no pocket there)
    float pocketNow (int lane, int which) const;
    // ---- the low end across the bar: each lane's level under 200 Hz as it reaches the mix, kLowBins to the bar, and the
    // pocket the watched lane is given (its gain, 1: untouched). Measured only while a lane is being watched (-1: none).
    static constexpr int kLowBins = 192;
    void watchLow (int lane)                    { lowWatch = lane; }
    float lowSeen (int lane, int bin) const     { return lowBins[(size_t) lane][(size_t) bin].load (std::memory_order_relaxed); }
    float lowPocket (int bin) const             { return lowPk[(size_t) bin].load (std::memory_order_relaxed); }
    // Whether a lane has a pocket, and its two: the ids of its DEPTH and SHAPE (nullptr: none)
    static const char* pocketDepthId (int lane);
    // GAIN: what the whole mix is turned by on its way into the master chain (dB, 24 either way; 0 in the middle)
    static float gainDb (float v)               { return (juce::jlimit (0.0f, 1.0f, v) - 0.5f) * 48.0f; }
    static Pattern defaultPattern (int lane);

    // ---- MIDI in: notes for the lanes. A lane's number is its channel. Any thread.
    // arrivedMs: juce::Time::getMillisecondCounterHiRes() when it came. It sounds one block later, at
    // the place in the block that matches when it came, so notes keep their spacing to the sample.
    void noteIn (const juce::MidiMessage&, double arrivedMs);
    void noteInAt (const juce::MidiMessage&, int sampleInNextBlock);     // tests: exactly there
    float notesLateMs() const                   { return lateMs.load(); }   // incoming notes against the sixteenths, averaged (+ late)
    int notesHeard() const                      { return lateCount.load(); }
    // The notes that came in from outside while the clock ran, each at the song position it was
    // played at: a snapshot takes what every lane was being played from these. kind: 0 a note's
    // end, 1 its start, 2 the clock set off again (what came before it is another run's), 3 some
    // were lost (nobody took them for too long). The message thread takes them, oldest first.
    struct HeardNote { double ppq = 0.0; juce::uint8 lane = 0, note = 0, vel = 0, kind = 0; };
    int takeHeardNotes (HeardNote* into, int most);

    // ---- MIDI clock out: START, 24 ticks a beat while it runs, STOP, for whatever follows this rig. The clock
    // runs ahead of the sound by two blocks and LEAD, so a follower's notes are back here in time
    // to sound on the beat; RUN waits that long (a few ms) before the first sound.
    struct ClockMsg { double dueMs = 0.0; juce::int64 sample = 0; juce::uint8 byte = 0; };
    void setClockOut (bool on, float leadMs)    { clockOn = on; clockLead = juce::jlimit (0.0f, 60.0f, leadMs); }
    bool clockOut() const                       { return clockOn.load(); }
    float clockLeadMs() const                   { return clockLead.load(); }
    bool nextClock (ClockMsg&);                 // the sender's thread: the oldest one waiting

    // ---- following an outside clock (the OXI One on INT): its ticks, START, CONTINUE and STOP.
    // Any thread; arrivedMs as for noteIn. While follow is on and ticks come in, the tempo and the
    // transport are the sender's. This rig's own steps are not played ahead (the usual way: they
    // are heard with the notes the sender plays into it, which cannot be played ahead), or are
    // played ahead of that clock by the time the rig takes to sound (onBeat: they are heard on the
    // sender's beat, with gear it plays directly). trimMs moves them either way from there. With no ticks it runs on
    // its own clock, at the tempo it last had. It sends no clock while it follows.
    void clockIn (juce::uint8 byte, double arrivedMs);
    void setFollow (bool on, bool onBeat, float trimMs)   { followOn = on; followBeat = onBeat; followTrim = juce::jlimit (-40.0f, 40.0f, trimMs); }
    bool follows() const                        { return followOn.load(); }
    bool following() const                      { return locked.load(); }       // ticks are coming in and it is on them
    float followedBpm() const                   { return followBpm.load(); }
    // How long after a note from outside arrives it is heard, in ms, and that time's three parts:
    // the wait for the next buffer, the way out through the interface, the look-ahead inside (the
    // lanes' and the master's).
    float soundsAfterMs() const                 { return waitMs.load() + outMs.load() + insideMs.load(); }
    float waitPartMs() const                    { return waitMs.load(); }
    float outPartMs() const                     { return outMs.load(); }
    float insidePartMs() const                  { return insideMs.load(); }
    void setOutputDelayForTests (float ms)      { outMs = ms; }

    // ---- the last bars as they were heard: a snapshot takes its sound and its profile from here.
    // The output is kept on a tape that goes round (some twenty seconds), with where the bar lines
    // fell and how much of each lane and return there was.
    struct Heard
    {
        juce::AudioBuffer<float> audio;         // the output: whole bars from a bar line, if the clock was running
        double sampleRate = 48000.0, bpm = 0.0;
        int bars = 0;                           // 0: no bar lines to go by (the last seconds, as they were)
        int most = 4;                           // as many bars as the tape can hold at the tempo it was going (four, unless an outside clock is slower than 60)
        std::array<float, kLanes> laneDb {};    // each lane's level into the mix over those bars (-100: silent or not known)
        std::array<float, 2> retDb {};          // the two returns'
        float mixDb = -100.0f;                  // all of them together: what the master chain was given
        // If asked for: the same bars of each thing the mix is made of, as it went into the mix
        // (kStems of them: the lanes, the two returns, what the drum bus did). Together they are
        // what the master chain was given; the output above is that after the master.
        std::vector<juce::AudioBuffer<float>> stems;
    };
    static constexpr int kStems = kLanes + 2 + 1 + 1;       // (the last: TRANSITION's lead-in and hit, as they go into the mix)
    bool lastHeard (Heard&, int bars = 4, bool stems = false) const;      // message thread. false: there was nothing but silence
    // The sound's state: a number that moves whenever anything a snapshot holds is changed (a knob,
    // a step, an effect, a model, a file), or a lane is muted or soloed. Two moments with the same
    // number are the same rig.
    juce::uint32 stateStamp() const             { return stateStampV.load(); }
    juce::uint64 blocksDone() const             { return blockCount.load(); }       // buffers rendered so far
    // The latest whole bars over which the rig held that state: nothing changed in them, nothing was
    // still travelling, and a bar had passed since it last did (for the tails of what went before).
    // Up to `bars` of them, the newest; h.bars says how many. False: none (or they are off the tape).
    bool heldHeard (Heard& h, juce::uint32 stamp, int bars = 4, bool stems = false) const;

    // ---- cue: a clip played to the headphones (outputs 3 and 4), apart from the set. While the
    // clock runs it plays in time: its bars on the rig's bars, at the rig's tempo (so its pitch
    // moves with the tempo's ratio to its own). Stopped, it plays as it was kept. A change of clip
    // fades over 10 ms each way.
    struct CueClip { juce::AudioBuffer<float> audio; int bars = 0; double rate = 0.0; };       // rate: the one it was kept at (0: taken to be the engine's)
    // Message thread. The clip must outlive its use: keep it a second past replacing it (nullptr: silence).
    void setCue (const CueClip* c)              { cueWant = c; }
    // Silence at once, and the audio thread has let go of every clip when this returns (before clips are destroyed).
    void dropCue()                              { const juce::SpinLock::ScopedLockType g (cueLock); cueWant = nullptr; cueNow = nullptr; cueGain = 0.0f; }
    bool hasCueOutput() const                   { return cueOuts.load(); }       // the device has outputs 3 and 4 open
    // PHONES: one pair of outputs, and headphones on it. While a cue plays, the set is on the left
    // ear (both its sides) and the cue on the right; with none playing the set is as it always
    // is. Only what goes to outputs 1 and 2 changes: a recording, the meters and a snapshot's
    // bars are the set itself.
    void setPhones (bool on)                    { phonesOn = on; }
    bool phones() const                         { return phonesOn.load(); }
    bool cueIsHeard() const                     { return cueOuts.load() || phonesOn.load(); }
    void phonesSplit (float* left, float* right, int n);        // this buffer's outputs 1 and 2, in place (the device callback; tests)
    const juce::AudioBuffer<float>& cueBlock() const { return cueBuf; }          // the last buffer's cue (the device callback; tests)

    // ---- the cue's rig (CueRig.h): a second process renders the rig as a recall would leave it, and its sound comes
    // back through shared memory to the headphones in place of a clip. The block is this rig's to write its clock into
    // and read the sound from; nullptr: there is no second rig, and a cue is a clip as it always was. The audio thread
    // never waits on it: frames that are not there in time are silence, and counted.
    void setCueFeed (CueShared* s)              { cueFeed = s; }
    // Message thread: the cue wanted from the second rig (the serial its sound will carry), 0 for none. The one
    // playing fades over 10 ms, and the wanted one comes in when the second rig has it.
    void cueRigWant (int serial)                { cueRigSerial = serial; }
    int cueRigWanted() const                    { return cueRigSerial.load(); }
    bool cueRigHeard() const                    { return cueRigOn.load(); }      // the wanted cue's sound is in the headphones now
    int cueRigGaps() const                      { return cueRigMissed.load(); }  // buffers it was wanted for and not there, since the audio started
    // ---- a rig led by another's clock (the second rig itself): the song position its next buffer starts at and
    // ends at, and the tempo. Called by whoever renders it, before each render, on that thread. Off: its own clock.
    void ledBy (double from, double end, double bpm) { ledFrom = from; ledEnd = end; ledBpm = bpm; ledOn = true; }
    void ledOff()                               { ledOn = false; }

    // ---- recording: what the audio thread hands on, a buffer at a time. Track 0 is the set (the
    // output), 1 to 11 the lanes, each as it leaves its strip, then the two returns as they go into
    // the mix, then what the drum bus does to the lanes that play through it (their sum as the bus
    // makes it, less their sum as it came: silence while the bus is at rest). Together, what the
    // master chain is given. begin and end bracket each buffer. Nothing in a sink may wait or allocate.
    static constexpr int kRecTracks = 1 + kStems;
    struct RecSink
    {
        virtual ~RecSink() = default;
        virtual void begin (int n) noexcept = 0;
        virtual void track (int index, const float* l, const float* r, int n) noexcept = 0;
        virtual void end (int n) noexcept = 0;
        // More of the same buffer, for a sink that asks for it (a print does, for stems a mastering engineer can use):
        // each lane's two ways into the mix, which are the lane's own buffer by a gain ramped across it (what goes there
        // direct, and what goes through the drum bus); the drum bus as it leaves it; the mix as the master chain is given
        // it (the stems together); and the mix as the master's last stage, the limiter, is given it.
        enum { kBusOut, kMasterIn, kLimiterIn };
        virtual bool wantsMore() const noexcept { return false; }
        virtual void lanePaths (int lane, const float* l, const float* r, int n, float directWas, float directNow, float busWas, float busNow) noexcept { juce::ignoreUnused (lane, l, r, n, directWas, directNow, busWas, busNow); }
        virtual void also (int which, const float* l, const float* r, int n) noexcept { juce::ignoreUnused (which, l, r, n); }
    };
    // Message thread. Returns once the audio thread has let go of the sink there was (nullptr: none).
    // (A sink is fed at the rate the engine had when it was given. If the audio starts again at another, the engine lets go of it there and then.)
    void setRecSink (RecSink* s)                { const juce::SpinLock::ScopedLockType g (recLock); recSink = s; recSinkRate = sr; }
    bool hasRecSink() const                     { const juce::SpinLock::ScopedLockType g (recLock); return recSink != nullptr; }
    int masterLatency() const                   { return masterLat.load(); }      // samples the master chain holds the set back
    int masterLatencyBeforeLast() const         { return masterLatBefore.load(); } // and by how many of them before its last stage (the limiter)
    int laneLatency() const                     { return maxLat.load(); }         // samples every lane is held back, to keep them in step
    // How many times the clock has set off, as the audio thread has seen it: after RUN is pressed,
    // the song position is the new run's once this has moved.
    int starts() const                          { return startsV.load(); }
    // A print: every module renders as in a bounce (what it would build on another thread, it builds
    // there and then, so a render is the same every time). Message thread, with the audio stopped.
    void setOffline (bool);
    bool isOffline() const                      { return offline; }

    // ---- inserts: -1 empty, else an index into effects()
    int insertType (int lane, int slot) const   { return insType[(size_t) lane][(size_t) slot]; }
    using SetupState = std::vector<std::pair<juce::String, float>>;
    // setup: the effect's CONFIG as it should be from its first sound (a snapshot's), else as it comes
    // fadeBeats: the effect that was there leaves, and this one arrives, over that long (a recall's
    // GLIDE). 0: over a fiftieth of a second, enough not to click.
    // cells: the eight knobs' values for the new effect (below 0: as the slot's knob stands). They are
    // given to the new effect alone: the one leaving keeps its own to the end.
    void setInsertType (int lane, int slot, int type, bool defaults = true, const SetupState* setup = nullptr, double fadeBeats = 0.0, const float* cells = nullptr);
    // A lane's instrument is about to change in a way that cannot travel: its model or another of
    // CONFIG's settings, a menu, a switch. The one playing carries on exactly as it is and fades out
    // over fadeBeats; a copy of it takes its place, to be changed. Knobs that travel move both.
    void forkVoice (int lane, double fadeBeats);
    // ---- a model's own sound knobs (Rig.h, ModelKnobs)
    // The knobs a lane has now: its usual ones, or its model's own in the eight sound places.
    const KnobDef& laneKnob (int lane, int k) const;

    // ---- the lane EQ: eight bands on every lane, after its inserts. A band is OFF, a BELL, a LOW or HIGH SHELF, a LOW or
    // HIGH CUT (12 dB an octave; two of them for 24) or a NOTCH, at a frequency, with a gain (bell and shelves) and a
    // width (Q). Three of the bands are the panel's LOW, MID and HIGH knobs (bands 1, 4 and 7: a shelf at 200 Hz, a wide
    // bell at 700, a shelf at 2.5 kHz, as the three-knob EQ always was): their gain is the knob's, and is not set here;
    // their kind, frequency and width are. The other five start OFF. Kept in a lane's setup (eq1_type ... eq8_q), so in
    // snapshots, and listed in CONFIG, so on the E16.
    static constexpr int kEqBands = 8;
    enum EqType { eqOff = 0, eqBell, eqLowShelf, eqHighShelf, eqLowCut, eqHighCut, eqNotch, kEqTypes };
    struct EqBand { int type = eqOff; float hz = 1000.0f, db = 0.0f, q = 0.70710678f; };
    static const char* eqTypeName (int type);
    static EqBand eqDefault (int band);
    static int eqKnobOf (int band)              { return band == 0 ? 0 : band == 3 ? 1 : band == 6 ? 2 : -1; }      // LOW, MID, HIGH, or none
    static bool eqHasGain (int type)            { return type == eqBell || type == eqLowShelf || type == eqHighShelf; }
    EqBand eqBand (int lane, int band) const;                   // (a knob band's db: what its knob says now)
    void setEqBand (int lane, int band, EqBand);                // (a knob band's db is left to its knob)
    bool eqIsDefault (int lane) const;                          // (but for the three knobs)
    void eqReset (int lane);
    // What the EQ does at a frequency, dB: for the graph. One band's alone, or (band -1) all of them.
    float eqResponse (int lane, float hz, int band = -1) const;
    // A band's control as CONFIG has it, 0 to 1: kinds along their list, 20 Hz to 20 kHz, -18 to +18 dB, a Q of 0.3 to 12.
    static float eqHzPlace (float hz)           { return juce::jlimit (0.0f, 1.0f, std::log (std::max (1.0f, hz) / 20.0f) / std::log (1000.0f)); }
    static float eqHzOf (float v)               { return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, v)); }
    static float eqQPlace (float q)             { return juce::jlimit (0.0f, 1.0f, std::log (std::max (0.01f, q) / 0.3f) / std::log (40.0f)); }
    static float eqQOf (float v)                { return 0.3f * std::pow (40.0f, juce::jlimit (0.0f, 1.0f, v)); }
    static float eqDbPlace (float db)           { return juce::jlimit (0.0f, 1.0f, (db + 18.0f) / 36.0f); }
    static float eqDbOf (float v)               { return -18.0f + 36.0f * juce::jlimit (0.0f, 1.0f, v); }
    static float eqKnobDb (float v)             { return v < 0.5f ? -24.0f * (1.0f - 2.0f * v) : 9.0f * (2.0f * v - 1.0f); }   // a LOW, MID or HIGH knob's
    static float eqKnobPlace (float db)         { return db < 0.0f ? 0.5f * (1.0f + std::max (-24.0f, db) / 24.0f) : 0.5f + 0.5f * std::min (9.0f, db) / 9.0f; }
    // "eq3_hz" and the like: which band (0 to 7) and which of its four (0 type, 1 hz, 2 gain, 3 q); false: not one of them
    static bool eqId (const juce::String& id, int& band, int& what);
    static juce::String eqIdOf (int band, int what);
    unsigned int lanePlate (int lane) const;        // its model's own faceplate colour (ARGB), or 0: the lane's usual
    // The lane's model may have changed: its knobs reach what that model's knobs are, and are named for them. fresh: a
    // place whose meaning changed starts where that control starts (a model chosen by hand); else every place keeps the
    // value it has (a snapshot or a file is being put back, and brings its own). True if the knobs changed.
    bool remapLane (int lane, bool fresh);
    // A lane held between two instruments (a stack knob standing between two snapshots whose lane is another model or
    // another sample): the one playing carries on as it is, a copy takes its place to be changed, as forkVoice; but
    // nothing fades by the clock. Both sound, played the same notes, mixed as blendVoice says (how much of the one
    // that arrived, 0 to 1), for as long as the knob stands between them. endHold settles on one of the two: the other
    // goes to nothing over a fiftieth of a second and is let go. holdVoice false: no audio is running, there is nothing
    // to hold (what follows simply changes the lane). A recall's forkVoice on a held lane ends the hold at once.
    bool holdVoice (int lane);
    void blendVoice (int lane, float share);
    void endHold (int lane, bool onTheOneThatArrived);
    bool holding (int lane) const               { return heldLane[(size_t) lane]; }
    bool holdEnding (int lane) const            { return heldLane[(size_t) lane] && this->lane[(size_t) lane].holdEnd.load() != 0; }
    // Message thread, now and then: lets go of the effects and instruments that have finished leaving.
    void tidy()                                 { collectRetired (false); }
    // How far a lane's fades have got: 1 when nothing is leaving. stage: 0 to kSlots - 1 an insert, kSlots the instrument.
    float fadeShown (int lane, int stage) const { return this->lane[(size_t) lane].fadeAt[stage].load(); }
    bool isPrepared() const                     { return prepared; }
    int blockSize() const                       { return maxBlock; }              // the largest block it was prepared for
    int leaving() const;                        // instruments and effects still on their way out (or waiting to be let go)

    // ---- what a CONFIG panel, a dropped file or a setup change is aimed at
    struct Target { int kind = 0, lane = 0, slot = 0; };    // kind 0 a lane's instrument, 1 one of its inserts, 2 return A (slot 0) or B (slot 1), 3 the master's drum bus
    // ---- the drum bus (in the master): the lanes its CONFIG names play through it together
    bool laneOnBus (int lane) const             { return busIn[(size_t) lane] != nullptr && busIn[(size_t) lane]->load() > 0.5f; }

    // ---- POCKETS (in the master): each lane's place in the spectrum, and who gives way to whom.
    // What it sees and does, band by band, for the screen (and for AUTO, which places the pockets
    // from what is playing). watch: it looks even with AMOUNT at nought, while the screen shows it.
    void pocketsSeen (Pockets::Seen& s) const   { pockets.seen (s); }
    void watchPockets (bool on)                 { pkWatch = on; }
    bool pocketsWatched() const                 { return pkWatch.load(); }
    Pockets::Set pocketSet (bool shownNow = true) const;        // its knobs as they sound now (or as they are set: where a glide is going)
    const Pockets& pocketsForTests() const      { return pockets; }

    // ---- return B (SPACE) is one of two reverbs: 0 REVERB, 1 CONVOLUTION. Both stay built, so the
    // places a convolution holds are still there when it comes back; only the chosen one runs.
    int spaceModel() const                      { return spaceWant.load(); }
    void setSpaceModel (int model);

    // ---- dropped audio files, for a lane, insert or return whose module takes them. A convolution
    // keeps eight places: the files go to the place its PLACE is on (several fill the next ones).
    // Asked of every audio file the rig is given, before it is read: the path to read it from in
    // its place (the session keeps its own copy of each: see Session::keepSample). Unset: as given.
    std::function<juce::String (const juce::String& path)> keepFile;
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
    // group: the box it is drawn in (controls of one group follow one another). ownRow: its box starts a row of its own.
    // section: 0 what the sound is (MODEL and what only that model has, LAYER, the knobs a model's own put away), which
    // changes with the model and is given rows of its own; 1 what is always there; 2 what comes and goes at the foot
    // (a sample's controls, a convolution's bands). A control that brings others or takes them away is before them.
    struct SetupParam { juce::String id, name, text, group; Kind kind = Kind::Knob; juce::StringArray choices; float value = 0.0f, def = 0.0f; bool ownRow = false; int section = 1; };
    // MOTION: the two modulators every instrument has (the suite's own): a wave, a speed, one of
    // the instrument's knobs to move, and how far. Set in a lane's CONFIG, kept in snapshots.
    static bool isMotionId (const juce::String& id);
    // What MOTION's RANDOM, DRIFT and STEPS are drawn from, for a place in the rig: a lane, an
    // insert's slot, a return. Always the same there, so a snapshot's random wave is the same
    // wave when it is recalled, after a voice has crossed over, and after the app has been shut.
    static juce::uint32 motionSeedFor (Target t)    { return 1u | (0x9E3779B1u * (juce::uint32) (17 + t.kind * 1000 + t.lane * 10 + t.slot)); }
    SetupState motionOff (Target) const;        // the two of them as they are when never set: a snapshot from before they could be set means this
    // MOTION's TARGET through the builds. A state holds it as a place along the list of what the module's MOTION can move,
    // and that list grows when a module gains a knob: a place kept on a shorter list then falls on something else. So a
    // snapshot is written with the parameter itself beside the place (`m1_to`: its number in the module, less one, over
    // 1024; 0: OFF), which is what is read; and one from before that is told by when it was taken and by which of the
    // lengths the list has had its place falls on. (Before the eighty-fifth build a place was read on whatever the list
    // had become: from the seventy-ninth on, most targets of older snapshots were one to five parameters out.)
    static const std::vector<int>& motionParamsOf (const juce::String& module);
    static float motionTo (const juce::String& module, float place);                            // what is kept beside a place of now
    static float motionPlaceOfTo (const juce::String& module, float to);                        // and its place now
    static float motionPlaceFrom (const juce::String& module, float place, juce::int64 takenMs, int lists); // a place kept with no `to`, as a place of now (lists: the snapshot's own mark of which build's lists it was taken on)
    static void motionWrite (SetupState&, const juce::String& module);                          // a state as it is written: `to` beside each TARGET
    static void motionRead (SetupState&, const juce::String& module, juce::int64 takenMs, int lists);   // and as it is read: each TARGET a place of now
    // The controls the seventy-ninth build added (GRANULAR's own, a sample's slices, loop and starts, the kick FORGE's),
    // each where it changes nothing: a snapshot from before them means this, whatever the lane was left at.
    SetupState newerOff (Target) const;
    SetupState laterOff (Target) const;     // what came after that: the same, for any snapshot that does not name them
    // Whether a lane's state has it on that model (its MODEL or its LAYER MODEL; where the state names neither, the lane as it stands)
    bool setupPlays (Target, const SetupState&, const juce::String& model) const;
    float moduleSays (Target, int what) const;      // a figure the module there reports (sf::Module::meter); 0 where there is none
    // A menu that has grown by one (BASS's and LEAD's MODEL and LAYER MODEL: WAVETABLE, second to last). A setup keeps a
    // menu's place as how far down the list it is; one kept from before the list grew is told by where it falls, and is
    // given as the same entry of the list as it is now.
    SetupState grown (Target, SetupState) const;
    SetupState sampleOwnOff (Target) const;
    // ---- TRANSITION (Transition.h): a lead-in that ends on a bar line, a hit that starts on it
    Transition trans;
    bool transLoad (int slot, const juce::File&);       // a file for the lead-in (0) or the hit (1), which then plays it (a file that cannot be read: false, nothing changed)
    juce::String transFile (int slot) const             { return transFiles[(size_t) juce::jlimit (0, 1, slot)]; }
    // FIRE: the lead-in from now to the next line that many bars apart (the one after, if this one is under a beat away), the
    // hit on it. That line, in beats of the song; or -1 with the clock stopped, when the hit plays at once.
    // FINISH, the master's stage before its limiter (Finish.cpp): how much of it, 0 to 1 (0: not there)
    void setFinish (float amount);
    float finish() const;
    const sf::FinishSays* finishSays() const;           // what it says of itself (Registry.h), or null
    SFProcessor* masterStage (const juce::String& module);      // one of the master's stages by its module ("LIMIT"), or null (tools)
    double transFire (int bars);
    // The same, for a line that is given (the arrangement's: a part's own bar line), and with what it plays if that is not what
    // the two slots say (a kind of -1: the slot's; Transition::kOff; or kFile with a file from transAsset).
    double transFireTo (double atPpq, int leadKind = -1, const sf::SampleData* lead = nullptr, int hitKind = -1, const sf::SampleData* hit = nullptr);
    const sf::SampleData* transAsset (const juce::String& path);       // a file read once and held (an arrangement part's own lead-in or hit); nullptr: it cannot be read
    // A control set on a line of the song, made by the audio thread a block or two before the one the line falls in (ARM's
    // mutes and solos: on time whatever the screen's thread is doing). A few at a time: false, there was no room. With the
    // clock stopped they are let go (the session then makes them itself).
    bool setAt (int ctl, float v, double atPpq);
    void clearSetsAt();     // a sample's own controls (GAIN, NORMALISE, PLAY, ENVELOPE) as they open: the same, for a snapshot from before them
    SFProcessor* processorFor (Target) const;
    bool laneFollowsKey (int lane) const;       // its KEY FOLLOW, in CONFIG, is on (false: the lane has none, or it is off)
    std::vector<SetupParam> setupParams (Target) const;
    void setSetup (Target, const juce::String& id, float v);
    SetupState getSetup (Target) const;
    void applySetup (Target, const SetupState&);
    const SetupState& initialSetup (int lane) const { return initSetup[(size_t) lane]; }
    // (on its way under GLIDE. What MOTION adds to a knob is not travel: it is left out.)
    bool isGliding (int i) const                { return glideV[(size_t) i].load() > 0.0f && std::abs (baseV[(size_t) i].load() - targetV[(size_t) i].load()) > 1e-4f; }

    // ---- MOTION on the snapshot knobs. A snapshot knob moves many knobs, each by a law of its
    // own: a line through what each of its snapshots says of that knob, and a slope for a knob it
    // is tied to. The session writes those laws down for the audio thread, which swings the
    // snapshot knob in time with the clock and moves what it moves, exactly as a hand turning it
    // would, with nothing waiting on the screen. What the knobs show is what sounds.
    struct KnobLaw
    {
        static constexpr int kNodes = 16;
        static constexpr float kNone = 1.0e9f;          // (that snapshot says nothing of this control)
        struct Row { int ctl = -1; float link = 0.0f; std::array<float, kNodes> node {}; };
        int nodes = 0;                                  // how many snapshots it sweeps (0: it is only tied to knobs)
        float centre = -1.0f;                           // where its swing is about, if not where the knob stands (a stack knob while CUE is lit: where it was left)
        std::vector<Row> rows;                          // every control it moves
        // how far the snapshot knob at v has that control from where the base scene has it
        float at (const Row& r, float v) const
        {
            float f = r.link * v;
            if (nodes >= 2)
            {
                const float p = v * (float) (nodes - 1);
                const int a = std::min (nodes - 2, (int) p);
                const float na = r.node[(size_t) a], nb = r.node[(size_t) a + 1];
                if (na < kNone * 0.5f && nb < kNone * 0.5f) f += na + (nb - na) * (p - (float) a);
            }
            return f;
        }
    };
    struct KnobLaws { std::array<KnobLaw, 16> knob; };
    void setKnobLaws (std::unique_ptr<KnobLaws>);       // message thread. Nothing: no MOTION is on anything
    static juce::StringArray motionWaves();             // WAVE's choices
    static juce::StringArray motionSpeeds();            // SPEED's: the suite's own rates
    float motionPush (int i) const              { return shownV[(size_t) i].load() - baseV[(size_t) i].load(); }      // how far MOTION has that control from where it stands, now

    // ---- what the screen reads
    std::atomic<float> peakL { -100.0f }, peakR { -100.0f }, gainRed { 0.0f }, lufs { -70.0f };
    std::atomic<bool> clipL { false }, clipR { false }, fault { false };
    std::atomic<double> cpu { 0.0 };
    // A buffer that takes longer to make than it lasts leaves a gap in the sound (a click; many of them, a crackle). Counted
    // since the audio started, with the most a buffer took of its own time lately (it falls back over some seconds), so
    // that a crackle heard can be told from one that is in the sound. The FAULT lamp lights for three seconds after one.
    std::atomic<double> cpuMost { 0.0 };
    std::atomic<int> overCount { 0 };
    std::atomic<juce::uint32> overAt { 0 };
    bool overLately() const                     { return overCount.load() > 0 && juce::Time::getMillisecondCounter() - overAt.load() < 3000; }
    std::array<std::atomic<float>, kLanes> laneDb;           // each lane's level as it reaches the mix (dB, falling)
    std::array<double, kLanes> laneSq {};                    // sum of squares since the start (tests)
    std::array<float, kLanes> laneMax {};                    // highest sample since the start (tests)
    std::atomic<int> solo { -1 };                            // tests: only this lane reaches the mix
    // tests: where a block's time goes, in seconds summed, while profile is on
    bool profile = false;
    std::array<double, kLanes> profLane {}, profIns {};
    double profRet = 0.0, profMaster = 0.0, profAll = 0.0, profPockets = 0.0;
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
        int pi = -1;                            // which of the module's parameters (two instruments' binds mean the same if this is the same)
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
    // A lane's instrument: the processor, and what each of the lane's twelve knobs moves in it.
    struct Voice
    {
        std::unique_ptr<SFProcessor> proc;
        // Its knobs' binds: the lane's usual twelve (0), then one set for each model with sound knobs of its own (Rig.h,
        // ModelKnobs). All are made once, when the voice is; `map` says which the knobs reach now, and only that changes
        // afterwards (the message thread sets it as the model changes, the audio thread reads it).
        std::vector<std::array<Bind, kLaneKnobs>> maps;
        std::atomic<int> map { 0 };
        const Bind& bind (int k) const          { return maps[(size_t) map.load (std::memory_order_relaxed)][(size_t) k]; }
        // its pocket's switch (made after the lane's effects), depth and shape, where the instrument has a pocket
        std::atomic<float>* pkPost = nullptr; std::atomic<float>* pkDepth = nullptr; std::atomic<float>* pkShape = nullptr;
        Voice();
        ~Voice();
    };
    static constexpr int kStages = kSlots + 1;      // the three inserts, then the instrument
    struct Lane
    {
        // What plays, and what is leaving: at each stage one may be on its way out while another
        // takes its place. Only the message thread changes these; the audio thread reads them and
        // says when a fade is done (fadeDone), and the message thread then lets the old one go.
        std::atomic<Voice*> voice { nullptr }, voiceOut { nullptr };
        std::atomic<InsertInst*> ins[kSlots], out[kSlots];
        std::atomic<juce::uint32> fadeSeq[kStages], fadeDone[kStages];
        std::atomic<uint64_t> fadeDoneAt[kStages];      // the buffer a fade ended in: after it, the one that left is not touched again
        std::atomic<float> fadeBeats[kStages], fadeAt[kStages];
        // audio thread
        juce::uint32 fadeSeen[kStages] = {};
        double fadeStart[kStages] = {}, fadeLen[kStages] = {};
        float fadeG[kStages];
        bool fading[kStages] = {};
        std::atomic<juce::uint32> knobsMoved { 0 };     // the model's knobs are others now (remapLane): the audio thread applies them all
        juce::uint32 knobsSeen = 0;                     // audio thread
        // the instrument held between two by a knob (holdVoice). holdG: how much of the one that arrived (below 0: no hold).
        // holdEnd: 1 settling on the one that arrived, 2 on the one that was playing. holdBack: the audio thread says the
        // one that arrived is at nothing (the fade's number), and the message thread then gives the lane back.
        std::atomic<float> holdG { -1.0f };
        std::atomic<int> holdEnd { 0 };
        std::atomic<juce::uint32> holdBack { 0 };
        bool holdingNow = false;                // audio thread
        float slotG[kSlots];                    // an insert's MUTE, as it stood last block
        Voice* voiceSeen = nullptr;
        LanePlayer player;
        juce::MidiBuffer midi;
        // the lane EQ (see eqBand): what is wanted, set by the message thread, and what the audio thread has made of it
        std::atomic<int> eqType[kEqBands];
        std::atomic<float> eqHz[kEqBands], eqGain[kEqBands], eqQ[kEqBands];
        Biquad eq[kEqBands];
        float eqDb[kEqBands] = {}, eqHzNow[kEqBands] = {}, eqQNow[kEqBands] = {};
        float eqSet[kEqBands], eqHzSet[kEqBands] = {}, eqQSet[kEqBands] = {};
        int eqTypeSet[kEqBands];
        bool eqLive[kEqBands] = {};
        float sendWas[2] = { 0, 0 }, gainWas = 1.0f, hold = -100.0f;
        bool seqOff = false;                    // audio thread: its sequencer was muted last block
        std::vector<float> dl[2];
        int dlPos = 0;
        std::atomic<int> delay { 0 };
        Pattern shared;                         // under patLock
        std::atomic<bool> patDirty { true }, patWait { false };    // patWait: taken up at the next bar line
        std::atomic<float> patAfter { 0.0f }; std::atomic<bool> patFresh { false }; double patDue = -1.0e18;      // (not before this bar line: setPattern's afterBeats)
        int insLat = 0;                         // what its inserts delay it by
        float lowZ[2] = { 0.0f, 0.0f }, lowAcc = 0.0f; int lowBin = -1;      // the low end across the bar: its low-pass, and the bin being filled
        float lowEnv = 0.0f;
        Lane();
        ~Lane();
    };

    std::unique_ptr<SFProcessor> makeProc (const char* module, const std::vector<Fixed>& fixed);
    Bind makeBind (SFProcessor&, const KnobDef&, Control* describe);
    static float readBind (const Bind&);
    static void applyBind (const Bind&, float v);
    void advanceControls (double beats, int n);
    void setEq (Lane&, int band, int type, float hz, float db, float q);
    static void eqCoefs (int type, double hz, double db, double q, double sr, double c[5]);
    void updateLatency();
    void collectRetired (bool all);

    std::vector<Control> controls;
    mutable std::unique_ptr<Defs> defsMade;
    std::array<Bind, cx::kCount> bind {};
    std::array<std::atomic<float>, cx::kCount> targetV, shownV, glideV;
    std::array<std::atomic<float>, cx::kCount> baseV;         // where each stands or has travelled to, without MOTION's push
    std::array<std::atomic<uint32_t>, cx::kCount> seqV;
    // audio thread
    // cur: where each control stands (travelling under GLIDE). snd: what sounds, which is cur and
    // MOTION's push on it (mOff). Everything the render reads is snd.
    std::array<float, cx::kCount> cur {}, gFrom {}, gTo {}, applied {}, snd {}, mOff {}, curSaid {};
    bool mOn = false;
    double motPpq = 0.0;
    struct MotState;
    std::unique_ptr<MotState> motState;
    std::atomic<KnobLaws*> knobLaws { nullptr };
    std::vector<std::pair<KnobLaws*, uint64_t>> retiredLaws;
    void knobMotion (double beats, int n);
    std::array<double, cx::kCount> gStart {}, gLen {};
    std::array<uint32_t, cx::kCount> seen {};
    std::array<bool, cx::kCount> gliding {};

    std::array<Lane, kLanes> lane;
    std::array<std::array<int, kSlots>, kLanes> insType {};
    std::unique_ptr<SFProcessor> retA;
    std::unique_ptr<SFProcessor> busProc;                    // the drum bus
    std::array<std::atomic<float>*, kLanes> busIn {};        // its CONFIG's LANE switches
    std::array<float, kLanes> busWas {};                     // audio thread: whether each lane was on the bus last block
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
    std::vector<std::pair<Voice*, uint64_t>> retiredVoices;
    int mapOf (int lane, SFProcessor&) const;           // which set of knobs that instrument's model has (0: the lane's usual)
    void bindVoice (int lane, Voice&);                  // every set, made
    static float startOf (const Bind&);                 // where a bound control starts, as its knob's value
    std::array<bool, kLanes> heldLane {};               // message thread: the lanes held between two instruments
    std::array<juce::String, kLanes> heldFile;          // the file each was playing before the one that arrived was given another
    void dropHold (int lane);
    bool fadeStep (Lane&, int stage, double beats, float& g0, float& g1);
    juce::MidiBuffer ghostMidi;
    juce::SpinLock patLock;

    double sr = 48000.0;
    int maxBlock = 512;
    bool prepared = false;
    juce::AudioBuffer<float> buf, mix, sendA, sendB, work, pre, ghost, busBuf, busDry, transBuf;
    std::array<juce::String, 2> transFiles;
    std::map<juce::String, std::shared_ptr<const sf::SampleData>> transAssets;      // (the message thread's)
    struct SetAt { std::atomic<int> state { 0 }; int ctl = 0; float v = 0.0f; double at = 0.0; };       // (state: 0 free, 1 being written, 2 waiting)
    std::array<SetAt, 32> setsAt;
    std::vector<std::shared_ptr<const sf::SampleData>> transKept;      // (every file a slot has had: the audio thread may still hold one)
    juce::AudioBuffer<float> laneBuf;                        // every lane's strip, side by side (two channels each): POCKETS sees them all before any reaches the mix
    Pockets pockets;
    std::atomic<bool> pkWatch { false };
    std::array<SetupState, kLanes> initSetup;
    std::array<juce::String, kLanes> files;
    std::vector<std::string> boundIds (Target) const;
    std::vector<int> motionOrder (Target, SFProcessor&, juce::StringArray* names = nullptr) const;       // TARGET's choices as CONFIG lists them: the panel's own knobs first
    float dryMixWas = 1.0f, retGainWas[2] = { 1.0f, 1.0f };
    float mixGainWas = 1.0f;            // GAIN, at the last block's end
    std::atomic<int> eqWatch { -1 }, lowWatch { -1 };
    std::atomic<float> eqCostDb { 0.0f };
    int eqCostLane = -1; double eqPre = 0.0, eqPost = 0.0;
    std::array<std::array<std::atomic<float>, kLowBins>, kLanes> lowBins {};
    std::array<std::atomic<float>, kLowBins> lowPk {};
    int lowPkBin = -1; float lowPkMin = 1.0f;
    void postPocket (int l, juce::AudioBuffer<float>& b2, int n, bool probe);
    std::array<std::atomic<float>, kLanes> bendWant {}; std::array<float, kLanes> bendNow {};      // the pitch wheels: where each is, and where its lane's pitch has come to
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
    static constexpr int kHeardQ = 8192;
    juce::AbstractFifo heardFifo { kHeardQ };
    std::array<HeardNote, kHeardQ> heardQ;
    bool heardLost = false;                     // audio thread: the queue was full
    void pushHeard (const HeardNote&);
    void pushClock (juce::uint8 byte, double dueMs, juce::int64 sample);
    struct ClockIn { double ms = 0.0; juce::uint8 byte = 0; };
    juce::AbstractFifo clockInFifo { kNoteQ };
    std::array<ClockIn, kNoteQ> clockInQ;
    juce::SpinLock clockInWrite;
    std::atomic<bool> followOn { false }, followBeat { false }, locked { false };
    std::atomic<float> followTrim { 0.0f }, followBpm { 0.0f }, waitMs { 0.0f }, outMs { 0.0f }, insideMs { 0.0f };
    std::atomic<int> masterLat { 0 }, masterLatBefore { 0 };
    // The sender's clock as a line through its ticks: each tick's time filtered by a delay-locked
    // loop, so a millisecond of jitter on a tick moves the line by a small part of it.
    struct Follow
    {
        bool have = false;                      // ticks are coming in
        bool rolling = false;                   // the sender's transport runs: its ticks are counted
        bool arm = false, top = false;          // START (top) or CONTINUE came: it rolls from the next tick
        double t0 = 0.0, t1 = 0.0, per = 0.0, lastArr = -1.0e12, bpmSlow = 0.0, bpmSaid = 0.0;
        juce::int64 idx = 0;                    // the tick at t0, counted from START
        int count = 0, big = 0;
    } fol;
    bool followClock (double aheadMs);
    std::atomic<bool> clockOn { false };
    std::atomic<float> clockLead { 0.0f }, lateMs { 0.0f };
    std::atomic<int> lateCount { 0 };
    double wallMs = -1.0, virtualMs = 0.0, blockMs = 0.0, prevBlockMs = 0.0, lastDurMs = 0.0, clkPpq = 0.0;
    bool clkWas = false;                // the clock was being sent in the block before this one
    bool wallValid = false;

    std::atomic<bool> running { false }, panicNow { false };
    std::atomic<int> startsV { 0 };
    bool offline = false;
    bool wasRunning = false;
    double ppq = 0.0, gClock = 0.0, blockBpm = 132.0;
    double devBufMs = 0.0;                      // the device's buffer; 0 with no device (a test: the block it renders)
    int64_t samplesDone = 0;
    std::atomic<double> ppqShown { 0.0 };
    std::atomic<uint64_t> blockCount { 0 };
    std::atomic<int> maxLat { 0 };
    int holdLat = 0;                            // what every lane is held to, whatever is on it
    // The tape: the last sixteen seconds and a little more of the set, whatever the rate (four bars at 60, the slowest). Its length is a power of two.
    static constexpr int kMarks = 64;
    int kTape = 1 << 20;
    std::array<std::vector<float>, 2> tape;
    std::vector<std::array<std::vector<float>, 2>> stemTape;      // the same stretch of each stem, before the master (so the master's delay ahead of the tape above)
    juce::int64 tapePos = 0;                    // audio thread
    std::atomic<juce::int64> tapeEnd { 0 };
    // steady, stamp: of the bar that ended at this line (steady: it was held, see heldHeard)
    struct Mark { juce::int64 sample = 0; int run = 0; std::array<double, kLanes + 2> sq {}; juce::uint32 stamp = 0; bool steady = false; };
    std::array<Mark, kMarks> marks;             // the bar lines, where their sound leaves on the tape
    std::atomic<int> markCount { 0 };
    int runCount = 0;
    std::atomic<juce::uint32> stateStampV { 1 };
    void stateMoved()                           { stateStampV.fetch_add (1); }
    bool heard (Heard&, int bars, bool heldOnly, juce::uint32 stamp, bool stems) const;
    // A stem as it goes into the mix (1 to kStems, as the recorder numbers them): onto its own
    // tape, and to the recorder if one is listening.
    void stemOut (RecSink*, int index, const float* l, const float* r, int n) noexcept;
    // audio thread: whether the bar being played has been held so far
    juce::uint32 stampSeen = 0;
    bool barMoved = true, settling = true, glidingNow = false;
    int movedFor = 0;                           // samples a change still colours (the lanes' hold, the master's)
    std::array<double, 2> retSq {};
    std::atomic<const CueClip*> cueWant { nullptr };
    const CueClip* cueNow = nullptr;            // audio thread
    double cuePos = 0.0;
    float cueGain = 0.0f;
    juce::AudioBuffer<float> cueBuf;
    std::atomic<bool> cueOuts { false }, phonesOn { false };
    float splitGain = 0.0f;                     // audio thread: how far the split is in
    juce::SpinLock cueLock;
    void renderCue (int n, bool run, double ppq0, double beats);
    std::atomic<CueShared*> cueFeed { nullptr };
    std::atomic<int> cueRigSerial { 0 }, cueRigMissed { 0 };
    std::atomic<bool> cueRigOn { false };
    float cueRigGain = 0.0f;                    // audio thread: how far the second rig's sound is in
    int cueRigPlaying = 0;                      // audio thread: the serial that gain is of
    int devBlock = 0;                           // the device's buffer in samples (0: none, a test: the block rendered)
    void renderCueRig (CueShared&, int n);
    bool ledOn = false;                         // (the rendering thread's own)
    double ledFrom = 0.0, ledEnd = 0.0, ledBpm = 132.0;
    mutable juce::SpinLock recLock;
    RecSink* recSink = nullptr;
    double recSinkRate = 0.0;
    juce::AudioBuffer<float> recBuf;
    struct Loud;
    std::unique_ptr<Loud> loud;
    float peakHold[2] = { -100.0f, -100.0f };
};
} // namespace sf0
