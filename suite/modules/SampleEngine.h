#pragma once
// (SF-000, the hundredth build) SAMPLE: one engine that plays a file, the same on every lane.
//
// A lane's SAMPLE model hands its notes to this and adds what it gives back before the lane's own output (its DJ
// FILTER, pocket and LEVEL), so a knob means the same thing on HAT as on PAD. Three ways to read the file (METHOD):
//   REPITCH  speed and pitch together, as tape does
//   STRETCH  time and pitch apart (Signalsmith Stretch, as DECK has it): the file at the set's tempo, BARS long; its
//            WARP markers put the moments chosen on the beats chosen
//   GRAIN    the sound made again from many short pieces, read about a place that moves at SCAN's pace
// POSITION, SIZE, SCAN and SPREAD mean the same in all three (where, how much, how fast it goes by, how far a note may
// stray). NOTES: PITCH (one note the file, played from ROOT) or SLICES (one note a slice, from BASE NOTE). Every
// control is a parameter with an id that begins "sp_", appended last to every lane's instrument (withSampleEngine), so a
// snapshot holds them all, and the engine finds them by id (bind): it works the same in every module.
#include "Kit.h"
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace sf
{
namespace smp
{
constexpr int kMarkers = 32;        // slice markers set by hand
constexpr int kWarps = 16;          // warp markers
constexpr int kMaxSlices = 128;
constexpr int kVoices = 16;
constexpr int kStretchers = 4;      // STRETCH's voices (each its own stretcher)
constexpr int kGrains = 96;         // GRAIN's, across all its voices
constexpr int kParamCount = 38 + kMarkers + 2 * 16;     // how many params() appends
enum Method { kRepitch = 0, kStretch = 1, kGrain = 2 };
enum SliceBy { kByMarkers = 0, kByOnsets = 1, kByEqual = 2, kByBeats = 3 };

// The note names SF-000 uses everywhere (C3 = 60).
inline juce::String noteName (int n) { static const char* const k[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return juce::String (k[((n % 12) + 12) % 12]) + juce::String (n / 12 - 2); }

// The parameters, in the order they are appended (never moved: a snapshot is read by id, a session by place).
std::vector<ParamSpec> params();
inline std::vector<ParamSpec> with (std::vector<ParamSpec> v) { const auto e = params(); v.insert (v.end(), e.begin(), e.end()); return v; }

// SCAN's knob as a pace: at the left nothing moves, in the middle as recorded, at the right four times.
inline float scanPace (float k) { return k <= 0.5f ? k * 2.0f : std::pow (4.0f, (k - 0.5f) * 2.0f); }
// GRAIN's SIZE: 5 ms to 400 ms.
inline float grainSeconds (float k) { return 0.005f * std::pow (80.0f, clamp01 (k)); }

// Where the file's samples land in beats, and back: its warp markers (each a place in the file and a beat), with the
// region's START at beat 0 and its END at the last. Both ways rise, so each has one answer.
struct WarpMap
{
    int n = 0;
    double src[kWarps + 2] {}, beat[kWarps + 2] {};
    double beats = 4.0;
    double srcAt (double b) const;
    double beatAt (double s) const;
    // the file's samples a beat at a beat
    double slopeAt (double b) const;
};

// The slices of the region, as SLICE BY makes them: their starts (the first is START), and the end of the last.
struct SliceMap
{
    int n = 1;
    double at[kMaxSlices + 1] {};
    int sliceAt (double pos) const { int k = 0; while (k + 1 < n && at[k + 1] <= pos) ++k; return k; }
};

// What the display sees of the audio: each voice and each grain, written by the audio every block, read by the screen
// whenever it draws (a value read while it is written is a frame out at worst: nothing is held for it).
struct Seen
{
    struct V { std::atomic<float> pos { -1.0f }, speed { 0.0f }, level { 0.0f }; std::atomic<int> key { -1 }, slice { -1 }, part { 0 }; std::atomic<float> st { 0.0f }; };
    struct G { std::atomic<float> pos { -1.0f }, len { 0.0f }, st { 0.0f }, amp { 0.0f }; };
    V v[kVoices];
    G g[kGrains];
    std::atomic<int> voices { 0 }, lastSlice { -1 };
    std::atomic<uint32_t> struckAt { 0 };          // the millisecond counter when lastSlice was struck
    std::atomic<float> bpm { 120.0f }, beatsPerBar { 4.0f };
    std::atomic<double> ppq { 0.0 };
    std::atomic<bool> playing { false };
    std::atomic<int> audition { -1 };              // a slice the screen asks to hear (the audio takes it)
};
} // namespace smp

class SampleEngine
{
public:
    // How a lane ends its notes: by the note's own end (BASS, LEAD, CHORD, PAD, VOICE: GATE gives the length) or as a
    // drum's hit, DECAY its length (KICK, the hats, CLAP, PERC, RUMBLE).
    enum class Ends { keys, hits };
    void bind (const std::vector<ParamSpec>& specs, bool pitchedLane, Ends ends);
    void prepare (double sr, int maxBlock);
    void reset();

    // Each block, in this order: begin, the notes (each at its sample in the block), render.
    // decayKnob: the lane's DECAY (hits) or GATE (keys), 0 to 1. mono: one note at a time.
    void begin (const Ctx& c, const SampleData* smp, float decayKnob, bool mono, int keyShift = 0);
    // key: the key (what a note-off names); pitch: the note it sounds (a chord's note, the key moved by the lane); hold:
    // samples until it is let go (-1: until its key is, or, on a drum lane, as DECAY says). part: 0 MODEL, 1 LAYER.
    void noteOn (int offset, int key, float pitch, float vel, float gain, int gen, int part, int hold = -1);
    void noteOff (int offset, int key, int gen);         // key -1: all of them
    // Adds this block's sound to L and R (layerGain: what a LAYER note is scaled by).
    void render (const Ctx& c, float* L, float* R, int n, float layerGain = 1.0f);
    bool sounding() const { return active > 0; }
    bool holds() const { return holdsNow; }
    int indexOf (const char* id) const;

    // ---- the display (message thread): 544 by 196
    static constexpr int kDisplayHeight = 196;
    void paint (juce::Graphics& g, juce::Rectangle<float> r, const Module& m, const SampleData* s, bool lit);
    void mouse (juce::Point<float> p, juce::Rectangle<float> r, bool down, Module& m, const SampleData* s);
    void mouseUp() { drag = Drag(); }
    void doubleClick (juce::Point<float> p, juce::Rectangle<float> r, Module& m, const SampleData* s);
    bool wheel (juce::Point<float> p, juce::Rectangle<float> r, float dy, const SampleData* s);

    // ---- shared with the display and the checks
    struct Settings;            // the parameters as numbers
    static smp::SliceMap slicesOf (const SampleData& s, const SampleRegion& reg, int by, int count, float sens, const float* cuts, double beats, double beatsPerBar);
    static smp::WarpMap warpOf (const SampleData& s, const SampleRegion& reg, double beats, bool on, const float* ws, const float* wd);
    static double barsOf (int choice, double regionSeconds, double bpm, double beatsPerBar);
    smp::Seen seen;

    // ids, as bound: -1 where the module has none
    struct Ids
    {
        int tune = -1, pos = -1, size = -1, scan = -1, spread = -1, attack = -1, release = -1, drive = -1;
        int start = -1, end = -1, fin = -1, fout = -1, gain = -1, norm = -1, rev = -1, root = -1;
        int loop = -1, lstart = -1, lend = -1, xfade = -1, sliceby = -1, count = -1, sens = -1, base = -1;
        int method = -1, notes = -1, play = -1, choke = -1, cycle = -1, vels = -1, vary = -1;
        int rate = -1, pspread = -1, shape = -1, gsync = -1, bars = -1, warp = -1, lock = -1;
        int cut0 = -1, ws0 = -1, wd0 = -1;
        bool ok() const { return tune >= 0 && cut0 >= 0 && wd0 >= 0; }
    } ids;

private:
    struct Voice
    {
        bool on = false, releasing = false, choked = false, looping = false, held = true;
        int key = -1, gen = 0, part = 0, slice = -1, method = 0, dir = 1, stretcher = -1;
        uint32_t order = 0;
        float pitchSt = 0.0f;       // the note against ROOT (semitones), TUNE not in it
        float vel = 1.0f, gain = 1.0f, env = 0.0f, chokeGain = 1.0f, varyLevel = 1.0f, varySt = 0.0f;
        double pos = 0.0;           // REPITCH: where it reads. STRETCH: the beat it is at. GRAIN: where its grains are read about
        double a = 0.0, b = 0.0;    // the part it plays (a slice, or the region)
        double from = 0.0;          // where in it it began (REPITCH, GRAIN: the file's samples; STRETCH: a beat)
        double tail = 0.0;          // STRETCH: the beat where the part ends
        double fadeIn = 0.0, fadeOut = 0.0;
        double beatsLeft = 0.0;     // STRETCH: what remains to be played, in beats
        double fed = 0.0;           // STRETCH: the next of the file's samples it is given
        int holdLeft = -1;          // samples until it is let go (-1: until its key is)
        int age = 0;
        double grainClock = 0.0;
        float stopGain = 1.0f;      // its end reached: down over 3 ms
        double lastToEnd = 1.0e30;  // REPITCH: how far it was from its end a sample ago
        bool stopping = false;
        Noise rng;
    };
    struct Grain { bool on = false; int voice = -1; double pos = 0.0, step = 1.0; int len = 0, age = 0; float amp = 0.0f, pan = 0.5f, st = 0.0f; };
    struct VoiceGrains { std::array<Grain, 24> g; };
    std::array<VoiceGrains, smp::kVoices> vg;
    struct Pending { int offset; int key; float pitch, vel, gain; int gen, part, hold; bool on; };

    void start (const Pending& p);
    void release (Voice& v);
    void stealFor (int part);
    float envTick (Voice& v);
    void renderRepitch (Voice& v, float* L, float* R, int n);
    void renderStretch (Voice& v, float* L, float* R, int n);
    void renderGrain (Voice& v, int vi, float* L, float* R, int n, int syncAt);
    float read (int ch, double pos, double rate) const;
    void publish();

    // the block's settings
    struct Block
    {
        int method = 0, notes = 0, play = 0, cycle = 0, sliceBy = 0, count = 16, base = 36, gsync = 0, barsChoice = 0, lock = 0;
        bool rev = false, loop = false, choke = false, warp = false, norm = false;
        float tune = 0, pos = 0, size = 1, scan = 0.5f, spread = 0, attack = 0, release = 30, drive = 0, sens = 0.5f;
        float rate = 24, pspread = 0, shape = 0.5f, vels = 0, vary = 0.5f, xfade = 0.1f, lstart = 0, lend = 1;
        float rootNote = 60.0f;
        double beats = 4.0, srcPerBeat = 1.0;
        SampleRegion reg;
        smp::SliceMap slices;
        smp::WarpMap map;
        double loopA = 0.0, loopB = 0.0, xfadeLen = 0.0;
    } blk;

    const SampleData* smpNow = nullptr;
    double sr = 48000.0, spb = 24000.0;
    bool pitched = true, holdsNow = false, monoNow = false;
    Ends ends = Ends::keys;
    float decayKnob = 0.5f;
    int keyShiftNow = 0;
    std::vector<std::pair<std::string, int>> idList;
    std::array<Voice, smp::kVoices> voices;
    std::array<Pending, 64> pending;
    int numPending = 0, active = 0;
    uint32_t orderNext = 1;
    int roundCount = 0;
    Noise rng { 0x5A17E5u };
    float driveWas = 0.0f;
    // STRETCH
    struct Stretcher { signalsmith::stretch::SignalsmithStretch<float> s { 0x5F3759DFL }; bool busy = false; float lastSt = 1.0e9f; };      // (a seed of its own: the same notes stretch the same way every time)
    std::array<Stretcher, smp::kStretchers> stretchers;
    juce::AudioBuffer<float> inBuf, outBuf, voiceBuf;

    // ---- the display's own (message thread)
    struct Drag { int what = 0; int index = -1; float grab = 0.0f; };       // what: 0 none, 1 START, 2 END, 3 a marker, 4 a warp marker, 5 the overview, 6 LOOP START, 7 LOOP END
    Drag drag;
    float zoomA = 0.0f, zoomB = 1.0f;       // the part of the file the wave shows (fractions); 0..1 and FOLLOW: the region
    bool zoomed = false;
};
} // namespace sf
