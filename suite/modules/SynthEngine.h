#pragma once
// (SF-000, the hundred and second build) SYNTH: one polyphonic engine, the same on every lane, as SAMPLE is.
//
// Every voice is whole: two wavetable oscillators (A and B: SHAPE scans A's table, a second SHAPE scans B's; WARP bends
// both; FM lets B move A's phase; SYNC restarts B with A; MIX between them), a sine an octave under A (SUB), NOISE that
// runs from white through pink to a cluster of six square waves at the 808's ratios (COLOR), then DRIVE, one filter in
// four circuits (CLEAN as low, band or high pass; LADDER; DIODE, the 303's; MS-20), the AMP envelope, and BODY: a tuned
// resonator (PERC's struck objects) that rings on after the envelope. A MOD envelope opens the filter by ENV AMT; ENV
// DECAY sets both envelopes' fall. A PITCH ENV drops the pitch at the front; BURSTS repeat the front (the clap). ACCENT
// and SLIDE work on any sound: an accented step is louder and its filter opens further and closes faster; a step tied
// into the next slides into it. Every control is a parameter whose id begins "sy_", appended last to every lane's
// instrument (syn::with), found by id (bind), so a snapshot holds them all and the engine works the same in every module.
#include "Kit.h"
#include "../core/Wavetable.h"
#include "../core/SampleTables.h"
#include "../core/Oversample.h"

namespace sf
{
namespace syn
{
constexpr int kVoices = 16;         // VOICES at most
constexpr int kSlots = 20;          // and four more, for voices let go over 3 ms while their place is taken
constexpr int kUnison = 7;
constexpr int kModes = 16;          // BODY's resonators, at most
enum Circuit { kCleanLP = 0, kCleanBP, kCleanHP, kLadder, kDiode, kMs20 };
enum Table { kBasic = 0, kTableFile = 12, kTableSample = 13, kTableRecording = 14 };     // 1..11: the instrument's other tables (WavetableSet's, CLASSIC first)
constexpr int kParamCount = 65;      // how many params() appends

// The parameters, in the order they are appended (never moved: a snapshot is read by id, a session by place).
std::vector<ParamSpec> params();
inline std::vector<ParamSpec> with (std::vector<ParamSpec> v) { const auto e = params(); v.insert (v.end(), e.begin(), e.end()); return v; }

// The table names TABLE A and TABLE B offer, in their order.
const std::vector<std::string>& tableNames();
// SHAPE's knob as a place along the table: the five pure waves of BASIC (sine, triangle, saw, square, pulse) are at its
// quarters, each with a little flat ground about it, so the knob lands on them.
float shapePlace (float knob);
// ENV DECAY's knob in seconds (20 ms to 5 s).
inline float decaySeconds (float k) { return 0.02f * std::pow (250.0f, clamp01 (k)); }
// The cutoff in Hz for CUTOFF's knob alone (30 Hz to 18 kHz, evenly in octaves).
inline float cutoffHz (float k) { return 30.0f * std::exp2 (9.23f * clamp01 (k)); }

// A patch: a setting of every SYNTH control, in each one's own units. PATCH chosen by hand puts them all there.
struct Patch { const char* name; std::vector<std::pair<const char*, float>> values; };
const std::vector<Patch>& patches();
std::vector<std::string> patchNames();
int patchIndex (const char* name);
// The patch a lane's SYNTH opens on (by the lane's slug).
const char* lanePatch (const juce::String& laneSlug);

// What the display sees: each voice, written by the audio every block, read whenever the screen draws.
struct Seen
{
    struct V { std::atomic<int> key { -1 }, part { 0 }; std::atomic<float> level { 0.0f }, cutoff { 0.0f }, t { 0.0f }, rel { -1.0f }, place { -1.0f }; std::atomic<bool> accent { false }; };
    V v[kSlots];
    std::atomic<int> voices { 0 };
    std::atomic<float> cost { 0.0f };          // of one core, the last blocks
};
} // namespace syn

class SynthEngine
{
public:
    enum class Ends { keys, hits };
    void bind (const std::vector<ParamSpec>& specs, bool pitchedLane, Ends ends);
    // (SF-000, the hundred and twelfth build) the lane's PLAYS turned: HITS or NOTES, from the next note
    void plays (bool pitchedLane, Ends e) { pitched.store (pitchedLane); ends.store (e); }
    void prepare (double sr, int maxBlock);
    void reset();
    // The lane's recording, for TABLE FILE and TABLE SAMPLE (message thread: their frames are made here).
    void fileChanged (const SamplePtr& s);

    // Each block: begin, the notes (each at its sample in the block), render.
    // decayKnob: the lane's DECAY (hits: how long a hit is held) or GATE (keys: unused here; the lane gives each note's
    // length in hold). mono: the lane's MONO (one note at a time, legato). keyShift: KEY FOLLOW's semitones.
    void begin (const Ctx& c, float decayKnob, bool mono, int keyShift = 0);
    // key: what a note-off names; pitch: the note it sounds. hold: samples until it is let go (-1: until its key is, or
    // on a drum lane as DECAY says). part: 0 MODEL, 1 LAYER.
    void noteOn (int offset, int key, float pitch, float vel, float gain, int gen, int part, int hold = -1);
    void noteOff (int offset, int key, int gen);         // key -1: all of them
    void render (const Ctx& c, float* L, float* R, int n, float layerGain = 1.0f);
    bool sounding() const { return active > 0; }
    // whether a note held goes on sounding: played as NOTES, and an envelope that sustains. (SF-000, the hundred and fifteenth
    // build: with SUSTAIN at nought a held note dies away, and a lone step longer than its sequence, tied to itself as a
    // drone, was heard once and never again; the chord lane's patch is so. Struck each time round instead.)
    bool holds() const { return ends == Ends::keys && blk.sustain > 0.001f; }
    // (SF-000, the hundred and third build) GATE turned as notes sound, on a lane that plays keys: `hold` is the length it now
    // gives a note (samples from where it began; -1: as long as its key), and `down` the keys still held. A note past its new
    // length lets go; one given back to its key keeps sounding only if that key is down. keySounding: a note of that key is
    // sounding and not let go (GATE brought back over half way plays again what had ended).
    void gateNow (int hold, const int* down, int numDown);
    void releaseScale (float m) { relScale = m; }       // (GATE: its lower half shortens RELEASE, its upper half lengthens it, up to eight times: Instrument.h, gateShape)
    bool keySounding (int key, int part) const;
    int indexOf (const char* id) const;

    // ---- the display (message thread): 544 by 156, as SAMPLE's
    static constexpr int kDisplayHeight = 230;     // (the hundred and nineteenth build: 156 till the table grew)
    void paint (juce::Graphics& g, juce::Rectangle<float> r, const Module& m, bool lit);
    void mouse (juce::Point<float> p, juce::Rectangle<float> r, bool down, Module& m);
    void mouseUp() { drag = 0; }
    bool wheel (juce::Point<float> p, juce::Rectangle<float> r, float dy, Module& m);

    syn::Seen seen;

    struct Ids
    {
        int tune = -1, shape = -1, cutoff = -1, res = -1, envamt = -1, decay = -1, accent = -1, drive = -1;
        int tableA = -1, warp = -1, warpAs = -1, tableB = -1, shapeB = -1, pitchB = -1, fineB = -1, mix = -1, fm = -1, sync = -1;
        int sub = -1, noise = -1, color = -1, phase = -1;
        int circuit = -1, keytrack = -1, mattack = -1, attack = -1, sustain = -1, release = -1, pitchEnv = -1, pitchTime = -1, bursts = -1, burstGap = -1;
        int voices = -1, glide = -1, slide = -1, chord = -1, unison = -1, detune = -1, width = -1, drift = -1;
        int body = -1, bodyMix = -1, bodyDecay = -1, bodyTone = -1, patch = -1, space = -1, gain = -1, scan = -1;
        int scanEnd = -1, steps = -1, wdepth = -1, wloop = -1, wvel = -1, wkey = -1, wt[4] = { -1, -1, -1, -1 }, wp[4] = { -1, -1, -1, -1 };
        int span = -1, scanSync = -1, frames = -1;           // (the hundred and nineteenth build)
        bool ok() const { return tune >= 0 && gain >= 0 && body >= 0; }
    } ids;

    // the block's settings, as numbers (the display reads the same from the module)
    struct Block
    {
        float tune = 0, shape = 0.5f, cutoff = 0.55f, res = 0.2f, envamt = 0.35f, decay = 0.35f, accent = 0.5f, drive = 0;
        int tableA = 0, warpAs = 0, tableB = 0, phase = 0, circuit = 3, bursts = 1, voices = 8, chord = 0, unison = 1, body = 0;
        float warp = 0, shapeB = 0.5f, pitchB = 0, fineB = 0, mix = 0, fm = 0, sub = 0, noise = 0, color = 0;
        bool sync = false;
        float keytrack = 0.5f, mattack = 0, attack = 1, sustain = 0.7f, release = 120, pitchEnv = 0, pitchTime = 60, burstGap = 10;
        float glide = 0, slide = 0.3f, detune = 0.3f, width = 0.5f, drift = 0.1f, bodyMix = 0.6f, bodyDecay = 0.5f, bodyTone = 0.5f, space = 0, gain = 0, scan = 0.5f;
        int scanEnd = 0, wloop = 0; bool steps = false;
        float span = 1.0f; int scanSync = 0, frames = 0;
        float wdepth = 0, wvel = 0.5f, wkey = 0.5f, wt[4] = { 500, 500, 500, 500 }, wp[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    } blk;
    static Block blockOf (const Ids& ids, const std::function<float (int)>& P);

private:
    float relScale = 1.0f;
    struct Mode { float c = 0, r = 0, w = 0, y1 = 0, y2 = 0; };
    struct Voice
    {
        bool on = false, releasing = false, held = true, choked = false, accent = false, slid = false, gated = false;
        int key = -1, gen = 0, part = 0, os = 2, holdLeft = -1, age = 0, slot = 0, lived = 0;     // (lived: its samples, at its own rate)
        uint32_t order = 0;
        float pitch = 60.0f;          // the note it sounds (a chord's note), TUNE not in it
        float freq = 0.0f, target = 0.0f, slideK = 0.0f;
        float vel = 1.0f, gain = 1.0f, chokeGain = 1.0f;
        double pa[syn::kUnison] {}, pb[syn::kUnison] {}, psub = 0.0, pmetal[6] {};
        float bPrev[syn::kUnison] {};
        // envelopes: amp (ATTACK, ENV DECAY, SUSTAIN, RELEASE), mod (MOD ATTACK, ENV DECAY, none held), the 303's (DIODE)
        float amp = 0.0f, mod = 0.0f, meg = 0.0f;
        int ampStage = 0, modStage = 0;     // 0 attack, 1 decay, 2 held, 3 released, 4 over
        float relFrom = 0.0f;
        double t = 0.0, tRel = -1.0;        // seconds since it began, since it was let go
        float pitchEnvSt = 0.0f;
        // the filter
        Svf svf, svf2; Ladder ladder; DiodeLadder303 dl;
        Svf svfS;                       // (SF-000, the hundred and fourth build) CLEAN BP and HP on the side too: the unison's width stays, filtered
        float cutNow = 1000.0f, sideK = 1.0f;
        bool driveOn = false;
        // noise
        float pink[3] {};
        // BODY
        Mode modes[syn::kModes]; int nModes = 0; float bodyEnergy = 0.0f;
        // DRIFT: this slot's own
        float driftTune = 0.0f, driftCut = 0.0f, driftPh = 0.0f, driftRate = 1.0f;
        bool sync = false; int bursts = 1; float burstGap = 10.0f;     // (SF-000, the hundred and fourth build) SYNC, BURSTS and BURST GAP as the note began: they are NEXT
        float scanPos = 0.0f;           // SCAN: how far it has moved along the tables from SHAPE since it began
        double syncB = 0.0;             // SCAN SYNC: beats (from the note on HOLD, the song's else)
        float placeNow = -1.0f;         // where along A's table it is now, as last rendered (for the display)
        int wStage = 0; float wT = 0.0f, wFrom = 0.5f, wNow = 0.5f;      // the wave envelope: its stage, the time into it, where it set out from, where it is
    };
    struct Pending { int offset; int key; float pitch, vel, gain; int gen, part, hold; bool on; };

    void start (const Pending& p);
    double secondsOf (int table) const;
    void release (Voice& v);
    void voiceControls (Voice& v, int n);
    void renderVoice (Voice& v, float* L, float* R, int n, float partGain);
    void setBody (Voice& v);
    const Wavetable* tableFor (int which) const;
    const Wavetable* tableFor (int which, bool chop) const;      // (as FRAMES says: CHOP's slices for SAMPLE and RECORDING)
    std::pair<float, float> stripCover (const Block& b) const;     // (where along the recording strip the frames begin, and how much of it they are: CHOP's are a few seconds of it)
    void publish();

    double sr = 48000.0;
    std::atomic<bool> pitched { true };     // (SF-000, the hundred and twelfth build: the lane's PLAYS, which can change as it plays)
    bool monoNow = false, wasPlaying = false;
    std::atomic<Ends> ends { Ends::keys };
    float decayKnob = 0.5f;
    int keyShiftNow = 0;
    float bendNow = 0.0f;
    std::vector<std::pair<std::string, int>> idList;
    std::array<Voice, syn::kSlots> voices;
    std::array<Pending, 96> pending;
    int numPending = 0, active = 0;
    uint32_t orderNext = 1;
    float accentCap[2] {};
    Noise rng { 0x51A7E5u };
    std::shared_ptr<const WavetableSet> set;
    std::shared_ptr<const Wavetable> basic;          // BASIC's five pure waves, five frames
    SharedSlot<tables::SampleTables> fromFile;
    // SAMPLE's and RECORDING's frames, made on a thread of their own and put here when they are ready (the state is shared
    // with that thread, so the engine can go before it is done)
    double chunkBeat = 0.0, bpmNow = 120.0;          // (SCAN SYNC: the song's beat where the piece being rendered begins, the tempo)
    struct ScanState { SharedSlot<tables::Scan> slot; std::atomic<int> gen { 0 }; };
    std::shared_ptr<ScanState> scanState = std::make_shared<ScanState>();
    // the sums, oversampled, and the way down
    std::vector<float> l4, r4, l2, r2, outL, outR;
    Halfband dec4[2]; Decimator2x dec;
    // SPACE: the ensemble
    struct Ensemble { std::vector<float> buf; int w = 0, size = 0; float ph = 0.0f; } ens;
    float spaceWas = 0.0f;
    double costAvg = 0.0;

    // ---- the display's own (message thread)
    int drag = 0;           // 1 the filter's point, 2 an envelope's corner
    int dragCorner = 0;
};
} // namespace sf
