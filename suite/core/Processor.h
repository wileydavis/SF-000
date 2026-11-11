#pragma once
// SFProcessor: hosts one sf::Module. Owns parameters (Live-visible), smoothing, SLOW (glide),
// MACRO response, MOTION (two song-locked modulators), song slots (recalled exactly on the bar),
// presets, state save/load, PANIC and the output safety stage.
//
// Every host block is split into chunks of at most kChunk samples: parameters, MOTION, song
// recall and every module's control-rate work run per chunk, so the sound is identical at any
// Live buffer size (64 .. 2048) and no module ever sees a block larger than it prepared for.
#include "Bus.h"
#include "Dsp.h"
#include "Module.h"
#include "Motion.h"
#include <array>
#include <atomic>
#include <cstring>

class SFProcessor : public juce::AudioProcessor, private juce::Timer, private juce::AudioProcessorParameter::Listener
{
    // Declared first: the parameter layout is built from these.
    sf::ModulePtr module;
    std::vector<int> motionTargets;         // module parameter index for MOTION TARGET choice k+1
    std::vector<sf::ParamSpec> specs;
    int numModParams = 0;
    int numExtraOut = 0;                    // extra stereo outputs (DECK CUE); read once, never on the audio thread

public:
    static constexpr int kChunk = 64;
    static constexpr int kMaxParams = 128;
    static constexpr int kMotions = 2;

    explicit SFProcessor (sf::ModulePtr);
    ~SFProcessor() override;

    // System parameters appended after the module's own (order fixed: saved sets depend on ids).
    enum { SYS_GLIDE = 0, SYS_MACRO_SRC, SYS_MACRO_AMT,
           SYS_M1_SRC, SYS_M1_RATE, SYS_M1_TARGET, SYS_M1_DEPTH,
           SYS_M2_SRC, SYS_M2_RATE, SYS_M2_TARGET, SYS_M2_DEPTH, kNumSys };
    // SLOW: CONTROL (follow SF CONTROL's SLOW ALL; it was OFF before SLOW ALL, and sets saved then
    // keep their old sound while SLOW ALL is OFF), this plugin's own length, or OFF (appended, so
    // saved sets keep their lengths); 128 BAR after OFF [Nov 5], appended for the same reason.
    enum { SLOW_CONTROL = 0, SLOW_OFF = 8, SLOW_128 = 9 };
    static std::vector<std::string> glideNames() { return { "CONTROL", "1 BAR", "2 BAR", "4 BAR", "8 BAR", "16 BAR", "32 BAR", "64 BAR", "OFF", "128 BAR" }; }
    // Bars for a SLOW ALL index (0 = OFF, 1..8 = 1..128 BAR).
    static double glideBars (int i) { static const double b[] = { 0, 1, 2, 4, 8, 16, 32, 64, 128 }; return b[juce::jlimit (0, sf::kSlowAllTop, i)]; }
    // Bars for this plugin's own SLOW, not CONTROL: 1..7 = 1..64 BAR, OFF, 128 BAR.
    static double ownBars (int s) { return s == SLOW_OFF ? 0.0 : s == SLOW_128 ? 128.0 : glideBars (s); }

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SF " + juce::String (module->id()); }
    bool acceptsMidi() const override { return module->instrument(); }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return module->tailSeconds(); }

    // Programs: SF CONTROL exposes 16 (VST3 delivers Program Change only as a program change).
    int getNumPrograms() override { return std::max (1, module->numPrograms()); }
    int getCurrentProgram() override { return module->currentProgram(); }
    void setCurrentProgram (int i) override { module->setProgram (i); }
    const juce::String getProgramName (int i) override { return module->numPrograms() > 1 ? juce::String ("SONG ") + juce::String (i + 1).paddedLeft ('0', 2) : juce::String(); }
    void changeProgramName (int, const juce::String&) override {}

    void updateTrackProperties (const TrackProperties&) override;
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Editor / tests
    sf::Module& mod() { return *module; }
    const std::vector<sf::ParamSpec>& allSpecs() const { return specs; }
    int numModuleParams() const { return numModParams; }
    juce::RangedAudioParameter* param (int i) { return params[(size_t) i]; }
    int paramIndex (const std::string& id) const;
    float currentValue (int i) const { return cur[(size_t) i]; }     // what the module hears (glide, macro, motion)
    float baseValue (int i) const { return base[(size_t) i]; }       // after SLOW, before MACRO / MOTION
    bool isModulated (int i) const { return std::abs (cur[(size_t) i] - base[(size_t) i]) > 1e-4f * (1.0f + std::abs (base[(size_t) i])); }
    float motionValue (int k) const { return motionOut[(size_t) k].load(); }
    void storeSong();                       // store current values into the active song slot
    int activeSong() const { return songIndex.load(); }
    bool songStored (int s) const { return s >= 0 && s < sf::kSongs && songHas[(size_t) s].load(); }
    bool recallWaiting() const { return pendingSong.load() >= 0; }
    void flushHostNotifications();          // message thread (timer); public for tests

    // Presets (message thread).
    const std::vector<sf::Preset>& factoryPresets() const { return factory; }
    void applyPreset (const sf::Preset&);
    sf::Preset capturePreset (const juce::String& name) const;
    juce::File userPresetFolder() const;
    juce::Array<juce::File> userPresets() const;
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File&);
    juce::String presetName() const { return currentPreset; }

    // The patch randomiser [Nov 10] (message thread; the modules with factory presets). RANDOMISE sets every unlocked sound control
    // anywhere in its range (menus too); NUDGE moves every unlocked knob a little from where it is
    // (up to 15 % of its range either way; menus stay). Never: setup controls, the sample's, switches
    // and buttons. A lock is per control and saved with the set (the state's "locks"); at first the
    // controls a random turn would wreck a set with are locked (LEVEL, FILTER, DENSITY, MIX, TUNE,
    // SHIFT, ACID, WARP, ENERGY: the level, the key and the E16's performance knobs). Right-click
    // a knob to lock or unlock it.
    // RESAMPLE [Nov 10] (the samplers: modules that take files). The plugin's own output, what you
    // hear after its LEVEL, for 1, 2 or 4 bars from the next bar line (playing; at once when stopped,
    // starting as late as the plugin's latency so the take begins on the bar), becomes its sample:
    // written to Music/SF Live Suite/Resamples as a WAV and loaded as a dropped file is. Message
    // thread: arm and cancel; the audio thread records into a buffer made when it was armed; the
    // timer writes and loads it. Stages: 0 idle, 1 waiting for the bar, 2 recording, 3 recorded.
    void armResample (int bars);
    void cancelResample() { resampleState = 0; }
    int resampleStage() const { return resampleState.load(); }
    void serviceResample();                        // message thread (the timer); public for tests
    juce::File lastResample;                       // what the last one wrote
    static juce::File resampleFolder();
    static inline juce::File testResampleFolder;   // tests: where takes go (else the Music folder)

    bool randomisable (int i) const;
    bool isLocked (int i) const { return i >= 0 && i < kMaxParams && locked[(size_t) i].load(); }
    void setLocked (int i, bool on) { if (i >= 0 && i < kMaxParams) locked[(size_t) i] = on; }
    void defaultLocks();
    void randomise (bool nudge, uint32_t seed);

    // The Live track's name as Live last told it (VST3; an AU never knows it) and a count of its
    // changes: the editor's E16 grid follows it. Message thread.
    juce::String liveTrackName() const { return juce::String::fromUTF8 (trackName, (int) ::strnlen (trackName, sizeof (trackName) - 1)); }
    uint32_t liveTrackNameSeq() const { return trackNameSeq.load(); }
    // The editor's groups folded or opened against their defaults (a bit per group); saved with the
    // set, so a window opens as it was left.
    std::atomic<int> uiFold { 0 };

    // Panic: silence this plugin now (voices, tails, delay lines). SF CONTROL PANIC does it to all.
    void panic() { localPanic = true; }

    sf::MeterState outMeter;
    std::atomic<bool> clipped { false };
    std::atomic<bool> fault { false };      // NaN / runaway: the module was reset (header shows FAULT)
    std::atomic<int> faultCount { 0 };

    juce::AudioProcessorValueTreeState apvts;

private:
    static BusesProperties busesFor (const sf::Module&);
    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void timerCallback() override;
    void parameterValueChanged (int index, float) override;
    void parameterGestureChanged (int, bool) override {}
    bool movedSinceRecall (size_t i) const;
    bool overrideHolds (size_t i, int advance);
    bool overrideInForce (size_t i) const;      // overrideHolds without dropping anything (any thread)
    void lockSongs();                           // songLock, not on the audio thread
    void applySong (int s);
    void updateParams (const sf::Ctx&, int n, float motionInput);
    void processChunk (juce::AudioBuffer<float>& buffer, juce::AudioBuffer<float>& mainOut, juce::AudioBuffer<float>* sideAll,
                       int start, int n, sf::Ctx& c, const juce::MidiBuffer& midi, bool panicNow, bool panicHold, bool lastChunk);

    std::vector<juce::RangedAudioParameter*> params;
    std::vector<std::atomic<float>*> raw;
    std::vector<float> cur, prev, base, macroOff, glideFrom, glideTo, modAmt;
    std::unique_ptr<std::atomic<float>[]> ovr;     // read by the message thread (host notification)
    // A recall's value holds until Live reports it back, unless Live's value is moved after the
    // recall (the performer, automation): extGen counts every change that isn't this plugin's own
    // notification; ovrGen / ovrFrom are the count and Live's value at the recall.
    // recallGen counts recalls per parameter, reportedGen is the last one reported to Live: the
    // 3 s give-up runs only once Live has been told (a message thread busy saving the set mustn't
    // undo a recall). reportExtGen: extGen just before the report: once Live holds the recalled
    // value, every change it makes after that is the performer's, whatever its value.
    // departGen: the recall (recallGen) during which Live sent a value away from ovrFrom: a move,
    // which stays newer than the recall even if Live's value comes back to ovrFrom before the report.
    std::unique_ptr<std::atomic<float>[]> ovrFrom;
    std::unique_ptr<std::atomic<uint32_t>[]> extGen, ovrGen, recallGen, reportedGen, reportExtGen, departGen;
    std::atomic<int> notifying { -1 };            // the parameter flushHostNotifications is writing
    // SLOW: a move's length (beats) is fixed when it is made; 0 = not travelling.
    std::vector<double> glideElapsed, glideBeats;
    int slowAllNow = 0;                           // SF CONTROL's SLOW ALL this block (0 while CONTROL isn't running)
    uint32_t slowPassed = 0xffffffffu;            // the SLOW ALL change whose fence this plugin has passed (slowAllFor)
    // GLIDE RINGS: this plugin's knobs on the E16 (E16Map.h, found by the Live track's name), and
    // the bus slot where the travelling ones are reported for SF CONTROL. Audio thread only.
    struct E16Entry { int param; uint32_t addr; float lo, hi; };
    std::array<E16Entry, 24> e16Knobs {};
    int numE16 = 0, glideSlot = -1;
    uint32_t e16Seq = 0xffffffffu;
    void findE16Knobs();
    void publishGlides (sf::Bus&);
    std::vector<int> ovrAge;
    double internalPpq = 0.0, barPhase = 0.0;
    char trackName[24] {};
    std::atomic<uint32_t> trackNameSeq { 0 };
    std::atomic<uint64_t> instanceUid { 1 };   // saved with the set; a duplicated track's copy takes a new one (AllocTap)
    bool clockAnchored = false;    // ovr: recalled value in force until Live reports it back (NaN = none)
    bool firstBlock = true;
    double sr = 48000.0;

    // MOTION
    std::array<sf::Motion, kMotions> motion;
    std::array<std::atomic<float>, kMotions> motionOut {};
    uint32_t motionSeed = 1;
    float noteVel = 0.0f;

    // Songs. songLock: the one writer of songVals at a time (a STORE on the audio thread, a state
    // load) and the state save's read: a song is never half stored, saved or loaded. The audio
    // thread only tries it (a STORE that finds it taken waits for the next block).
    std::array<std::array<float, kMaxParams>, sf::kSongs> songVals {};
    std::atomic<bool> songLock { false };
    std::array<std::atomic<bool>, sf::kSongs> songHas {};
    std::atomic<int> songIndex { -1 }, pendingSong { -1 };
    uint32_t lastSongSeq = 0, lastStoreSeq = 0, lastPanicSeq = 0;
    bool seqInit = false;
    std::atomic<bool> notifyHost { false }, storeRequested { false }, localPanic { false };
    // A state load (setStateInformation) is newer than everything asked for before it, whatever its
    // values. stateGen counts loads, odd while one is under way. A recall belongs to the count at
    // its landing (ovrState): one from before a load is no longer in force, and never reported to
    // Live. The audio thread, seeing a new count (seenStateGen), ends a song still waiting for its
    // bar line and a STORE not yet made, and passes over SF CONTROL's requests up to the counts at
    // the load's end (songSeqAtLoad, storeSeqAtLoad). Requests after it go ahead as ever.
    std::atomic<uint32_t> stateGen { 0 }, ovrState { 0 }, songSeqAtLoad { 0 }, storeSeqAtLoad { 0 };
    std::atomic<uint32_t> storeAskGen { 0 };   // the count when this plugin's own STORE was pressed
    uint32_t seenStateGen = 0;
    bool storeWanted = false;              // audio thread: a STORE to make (waits while songLock is taken)
    int storeTo = -1;                      // ... into this song (SF CONTROL's STORE TO), or the bus song

    // Safety
    int overSamples = 0;
    // PANIC: output gain (fades 5 ms), a press still to complete (fade out, then reset).
    int panicPos = 240, panicInPos = 240;   // PANIC output / input gain, in samples of the 5 ms ramp
    bool panicDown = false;
    int panicLen = 240;
    // SF BUILD's cut (DROP): the bus's, read whole once per host block; this block's window (-1:
    // none); the first note just before the way back (it comes back 1 ms before that); the gain.
    sf::DropCut cutRead;
    double cutA = -1.0, cutB = -1.0, cutEarly = -1.0, cutAtSeen = -2.0;
    float cutGain = 1.0f;
    double pendingAt = -1.0;               // ppq of the bar line the pending song lands on (-1 = own)
    bool pendingJumped = false;            // the pending song already lost its bar line to a jump back
    bool recalledInBlock = false;          // a recall landed in this host block (SF CONTROL's STORE ALL waits)
    bool lastPlaying = false;              // the transport was playing in the last block
    int pendingKey = -2;                   // the KEY the pending song brings (-2 = no change)
    int keyUsed = -1, keyHold = 0;         // the key in force here; samples it holds against the bus
    uint64_t streamPos = 0;

    // Presets
    std::vector<sf::Preset> factory;
    juce::String currentPreset;
    std::array<std::atomic<bool>, kMaxParams> locked {};    // the randomiser's locks
    std::atomic<int> resampleState { 0 };          // RESAMPLE's stage (armResample)
    juce::AudioBuffer<float> resampleBuf;          // made on arming; the audio thread fills it
    int resampleLen = 0, resamplePos = 0, resampleBars = 1;
    int64_t resampleAt = -1;                       // the stream sample it starts at, and resampleLen: set at the first block
    void recordResample (const juce::AudioBuffer<float>& out, int n, int64_t blockStart, double ppq0, double bps, bool playing, double beatsPerBar);

    double lastBpm = 120.0, lastBpb = 4.0;
    juce::AudioBuffer<float> extraScratch, mainView, sideView, extraView;   // views: never allocate
    juce::MidiBuffer chunkMidi;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SFProcessor)
};
