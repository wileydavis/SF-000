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
#include "Pockets.h"

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
    constexpr int kSeqMute = kInsMS + kSlots * 2;           // the lane's own sequencer muted: its steps stay, unplayed
    constexpr int kPk = kSeqMute + 1;                       // its pocket: HOME, WIDTH, HOLD, YIELD
    constexpr int kPerLane = kPk + kPkLane;
    constexpr int kRet = kLanes * kPerLane;                 // 4 knobs for A, 4 for B as REVERB, 4 for B as CONVOLUTION
    constexpr int kRetKnobs = 4 + 4 * kSpaceModels;
    constexpr int kBus = kRet + kRetKnobs;                  // the drum bus's four knobs: DRIVE, CRUNCH, PUNCH, BOOM
    constexpr int kPkG = kBus + kBusKnobs;                  // POCKETS, for the whole rig: AMOUNT, CLEAR, SPEED, RETURNS
    constexpr int kRetMS = kPkG + kPkGlobals;               // A MUTE, A SOLO, B MUTE, B SOLO
    constexpr int kMaster = kRetMS + 4;                     // TAPE, COMP, LIMIT
    constexpr int kTempo = kMaster + 3, kKey = kTempo + 1, kGlide = kKey + 1;
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
    void setPattern (int lane, const Pattern&, bool onBarLine = false);
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
    };
    bool lastHeard (Heard&, int bars = 4) const;      // message thread. false: there was nothing but silence
    // The sound's state: a number that moves whenever anything a snapshot holds is changed (a knob,
    // a step, an effect, a model, a file), or a lane is muted or soloed. Two moments with the same
    // number are the same rig.
    juce::uint32 stateStamp() const             { return stateStampV.load(); }
    // The latest whole bars over which the rig held that state: nothing changed in them, nothing was
    // still travelling, and a bar had passed since it last did (for the tails of what went before).
    // Up to `bars` of them, the newest; h.bars says how many. False: none (or they are off the tape).
    bool heldHeard (Heard& h, juce::uint32 stamp, int bars = 4) const;

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

    // ---- recording: what the audio thread hands on, a buffer at a time. Track 0 is the set (the
    // output), 1 to 11 the lanes, each as it leaves its strip, then the two returns as they go into
    // the mix, then what the drum bus does to the lanes that play through it (their sum as the bus
    // makes it, less their sum as it came: silence while the bus is at rest). Together, what the
    // master chain is given. begin and end bracket each buffer. Nothing in a sink may wait or allocate.
    static constexpr int kRecTracks = 1 + kLanes + 2 + 1;
    struct RecSink
    {
        virtual ~RecSink() = default;
        virtual void begin (int n) noexcept = 0;
        virtual void track (int index, const float* l, const float* r, int n) noexcept = 0;
        virtual void end (int n) noexcept = 0;
    };
    // Message thread. Returns once the audio thread has let go of the sink there was (nullptr: none).
    // (A sink is fed at the rate the engine had when it was given. If the audio starts again at another, the engine lets go of it there and then.)
    void setRecSink (RecSink* s)                { const juce::SpinLock::ScopedLockType g (recLock); recSink = s; recSinkRate = sr; }
    bool hasRecSink() const                     { const juce::SpinLock::ScopedLockType g (recLock); return recSink != nullptr; }
    int masterLatency() const                   { return masterLat.load(); }      // samples the master chain holds the set back
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
    struct SetupParam { juce::String id, name, text, group; Kind kind = Kind::Knob; juce::StringArray choices; float value = 0.0f, def = 0.0f; bool ownRow = false; };
    // MOTION: the two modulators every instrument has (the suite's own): a wave, a speed, one of
    // the instrument's knobs to move, and how far. Set in a lane's CONFIG, kept in snapshots.
    static bool isMotionId (const juce::String& id);
    // What MOTION's RANDOM, DRIFT and STEPS are drawn from, for a place in the rig: a lane, an
    // insert's slot, a return. Always the same there, so a snapshot's random wave is the same
    // wave when it is recalled, after a voice has crossed over, and after the app has been shut.
    static juce::uint32 motionSeedFor (Target t)    { return 1u | (0x9E3779B1u * (juce::uint32) (17 + t.kind * 1000 + t.lane * 10 + t.slot)); }
    SetupState motionOff (Target) const;        // the two of them as they are when never set: a snapshot from before they could be set means this
    SFProcessor* processorFor (Target) const;
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
        static constexpr int kNodes = 10;
        static constexpr float kNone = 1.0e9f;          // (that snapshot says nothing of this control)
        struct Row { int ctl = -1; float link = 0.0f; std::array<float, kNodes> node {}; };
        int nodes = 0;                                  // how many snapshots it sweeps (0: it is only tied to knobs)
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
        Bind bind[kLaneKnobs];
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
        float slotG[kSlots];                    // an insert's MUTE, as it stood last block
        Voice* voiceSeen = nullptr;
        LanePlayer player;
        juce::MidiBuffer midi;
        Biquad eq[3];
        float eqDb[3] = { 0, 0, 0 }, eqSet[3] = { 99, 99, 99 };
        float sendWas[2] = { 0, 0 }, gainWas = 1.0f, hold = -100.0f;
        bool seqOff = false;                    // audio thread: its sequencer was muted last block
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
    void advanceControls (double beats, int n);
    void setEq (Lane&, int band, float db);
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
    bool fadeStep (Lane&, int stage, double beats, float& g0, float& g1);
    juce::MidiBuffer ghostMidi;
    juce::SpinLock patLock;

    double sr = 48000.0;
    int maxBlock = 512;
    bool prepared = false;
    juce::AudioBuffer<float> buf, mix, sendA, sendB, work, pre, ghost, busBuf, busDry;
    juce::AudioBuffer<float> laneBuf;                        // every lane's strip, side by side (two channels each): POCKETS sees them all before any reaches the mix
    Pockets pockets;
    std::atomic<bool> pkWatch { false };
    std::array<SetupState, kLanes> initSetup;
    std::array<juce::String, kLanes> files;
    std::vector<std::string> boundIds (Target) const;
    std::vector<int> motionOrder (Target, SFProcessor&, juce::StringArray* names = nullptr) const;       // TARGET's choices as CONFIG lists them: the panel's own knobs first
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
    std::atomic<int> masterLat { 0 };
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
    juce::int64 tapePos = 0;                    // audio thread
    std::atomic<juce::int64> tapeEnd { 0 };
    // steady, stamp: of the bar that ended at this line (steady: it was held, see heldHeard)
    struct Mark { juce::int64 sample = 0; int run = 0; std::array<double, kLanes + 2> sq {}; juce::uint32 stamp = 0; bool steady = false; };
    std::array<Mark, kMarks> marks;             // the bar lines, where their sound leaves on the tape
    std::atomic<int> markCount { 0 };
    int runCount = 0;
    std::atomic<juce::uint32> stateStampV { 1 };
    void stateMoved()                           { stateStampV.fetch_add (1); }
    bool heard (Heard&, int bars, bool heldOnly, juce::uint32 stamp) const;
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
    mutable juce::SpinLock recLock;
    RecSink* recSink = nullptr;
    double recSinkRate = 0.0;
    juce::AudioBuffer<float> recBuf;
    struct Loud;
    std::unique_ptr<Loud> loud;
    float peakHold[2] = { -100.0f, -100.0f };
};
} // namespace sf0
