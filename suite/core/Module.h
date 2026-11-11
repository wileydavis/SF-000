#pragma once
// The contract every SF module implements. One SFProcessor + SFEditor hosts any module.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace sf
{
class Bus;

enum class PType { Float, Choice, Toggle, Momentary };

struct ParamSpec
{
    std::string id, name;
    PType type = PType::Float;
    float min = 0.0f, max = 1.0f, def = 0.0f;
    float centre = NAN;             // value at the knob's midpoint (skew); NAN = linear
    std::string unit;               // "%", "dB", "Hz", "ms", "st", "BIP" (bipolar 0..1, centre = off)
    std::vector<std::string> choices;
    bool primary = false;           // shown in the top row; first 8 params = Live's device view
    bool glide = true;              // may follow SLOW (menus and switches never do; noGlide() vetoes a knob)
    bool slows = false;             // a detail knob SLOW moves too, like the top row (the E16's TEXTURE
                                    // COLOR...): where SLOW reaches is not where a knob is drawn
    bool song = true;               // stored in song slots
    int macroDir = 0;               // response to the plugin's MACRO: +1 / -1 / 0
    bool hidden = false;            // not in the generic grid (the module draws its own control)
    bool preset = true;             // set by presets (false: setup values a preset must not touch)
    bool setup = false;             // set and forget (how the plugin sits in the set, not a sound):
                                    // the editor's SETUP group, below everything else
    bool perform = false;           // played, though not in the top row (DECK's PLAY, CONTROL's
                                    // PANIC): the editor shows it with the top row
    bool inSample = false;          // the sample's own (START, END, its fades): the editor's SAMPLE group
    bool inLayer = false;           // the second layer's (LAYER MODEL, LAYER, SPLIT, SPLIT HZ): the editor's LAYER group
    bool inEngine = false;          // one model's own (FM 4-OP's operators, WAVETABLE's TABLE): the editor's ENGINE group

    ParamSpec& prim()           { primary = true; return *this; }
    ParamSpec& dir (int d)      { macroDir = d; return *this; }
    ParamSpec& noGlide()        { glide = false; return *this; }
    ParamSpec& slow()           { slows = true; return *this; }
    bool glidesWithSlow() const { return glide && type == PType::Float && (primary || slows); }
    ParamSpec& noSong()         { song = false; return *this; }
    ParamSpec& u (const char* s){ unit = s; return *this; }
    ParamSpec& hide()           { hidden = true; return *this; }
    ParamSpec& noPreset()       { preset = false; return *this; }
    ParamSpec& forget()         { setup = true; return *this; }
    ParamSpec& performs()       { perform = true; return *this; }
    ParamSpec& sampleGroup()    { inSample = true; return *this; }
    ParamSpec& layerGroup()     { inLayer = true; return *this; }
    ParamSpec& engineGroup()    { inEngine = true; return *this; }
};

inline ParamSpec knob (const char* id, const char* name, float mn, float mx, float def, const char* unit = "", float centre = NAN)
{
    ParamSpec p; p.id = id; p.name = name; p.min = mn; p.max = mx; p.def = def; p.unit = unit; p.centre = centre; return p;
}
inline ParamSpec pct (const char* id, const char* name, float def) { return knob (id, name, 0.0f, 1.0f, def, "%"); }
inline ParamSpec bip (const char* id, const char* name)             { return knob (id, name, 0.0f, 1.0f, 0.5f, "BIP"); }
inline ParamSpec choice (const char* id, const char* name, std::vector<std::string> c, int def = 0)
{
    ParamSpec p; p.id = id; p.name = name; p.type = PType::Choice; p.choices = std::move (c);
    p.min = 0; p.max = (float) p.choices.size() - 1; p.def = (float) def; p.glide = false; return p;
}
inline ParamSpec toggle (const char* id, const char* name, bool def = false)
{
    ParamSpec p; p.id = id; p.name = name; p.type = PType::Toggle; p.def = def ? 1.0f : 0.0f; p.glide = false; return p;
}
inline ParamSpec momentary (const char* id, const char* name)
{
    ParamSpec p; p.id = id; p.name = name; p.type = PType::Momentary; p.glide = false; p.song = false; return p;
}

// A preset: parameter values by id (plain units). Anything a preset does not name is set to its
// default, so a preset always fully defines the sound. Factory presets: Source/modules/Presets.cpp.
struct Preset
{
    std::string name;
    std::vector<std::pair<std::string, float>> values;
};

// Per-chunk context handed to Module::process (the processor splits every host block into
// chunks of at most SFProcessor::kChunk = 64 samples, so the sound never depends on Live's
// buffer size).
struct Ctx
{
    double sr = 48000.0;
    int n = 0;                    // samples in this block
    double bpm = 120.0;
    double ppq = 0.0;             // quarter-note position at block start (internal clock when stopped)
    bool playing = false;
    double beatsPerBar = 4.0;
    int keyRoot = -1;             // SF CONTROL KEY: 0..11 = C..B, -1 = not set
    int64_t samplePos = 0;        // stream position of this chunk (chunks sit on a fixed 64-sample grid)
    double barPhase = 0.0;        // ppq of a bar line (Live's last bar start, mod the bar)
    bool recallWaiting = false;   // a song recall is waiting for its bar line in this plugin
    bool recalledThisBlock = false;   // a song recall landed here in this host block, up to this chunk
    bool lastChunk = false;       // the last chunk of the host block
    bool offline = false;         // Live is rendering (export, freeze): work may block, timing must not depend on threads
    const float* p = nullptr;     // parameter values at block end (glided, macro applied)
    const float* p0 = nullptr;    // values at block start (for per-sample ramps)
    Bus* bus = nullptr;
    const char* trackName = "";  // Live track name (VST3 only; empty if unknown)
    uint32_t trackNameSeq = 0;
    std::atomic<uint64_t>* uid = nullptr;   // this plugin instance's id, saved with the set (ALLOC's order)
    // SF BUILD's cut (DROP), for a plugin that takes part (Module::takesDropCut): silent from cutAt
    // until backAt, song positions of the music (this plugin's own position less its latency);
    // -1: no cut / not asked back yet.
    double cutAt = -1.0, backAt = -1.0;
    bool inCut (double q) const     { return cutAt >= 0.0 && q >= cutAt && (backAt < 0.0 || q < backAt); }

    double beatsPerSample() const   { return bpm / 60.0 / sr; }
    double samplesPerBeat() const   { return sr * 60.0 / bpm; }
    float v (int i) const           { return p[i]; }
    float v0 (int i) const          { return p0[i]; }
    int ci (int i) const            { return (int) std::lround (p[i]); }
    bool b (int i) const            { return p[i] > 0.5f; }
    double ppqAt (int s) const      { return ppq + s * beatsPerSample(); }
    // Sample offset in this block of the next multiple of `beats`, or -1.
    // Samples to the next grid line of `beats` in this block (-1: none). offsetBeats moves the grid
    // later: the grid a late groove plays on (KickGroove).
    int nextGrid (double beats, double offsetBeats = 0.0) const
    {
        const double first = std::ceil ((ppq - offsetBeats) / beats - 1e-9) * beats + offsetBeats;
        const double off = (first - ppq) * samplesPerBeat();
        return off < n ? (int) off : -1;
    }
};

class Module
{
public:
    virtual ~Module() = default;

    virtual std::string id() const = 0;                          // "KICK"
    virtual bool instrument() const                 { return false; }
    virtual bool sidechain() const                  { return false; }
    virtual std::vector<std::string> extraOutputs() const { return {}; }   // extra stereo outputs (instruments)
    virtual std::vector<ParamSpec> params() const = 0;           // module params; system params are appended

    // maxBlock is the largest chunk the processor will pass (the processor always splits the
    // host block into chunks of at most SFProcessor::kChunk samples).
    virtual void prepare (double sr, int maxBlock) = 0;
    // Silence everything now: voices off, delay lines and reverbs cleared (PANIC, NaN recovery).
    virtual void reset() {}
    // main: in/out stereo. side: sidechain (may be null). extra: extra outputs (may be null).
    virtual void process (Ctx& c, juce::AudioBuffer<float>& main, juce::AudioBuffer<float>* side,
                          juce::AudioBuffer<float>* extra, juce::MidiBuffer& midi) = 0;
    virtual int latency() const                     { return 0; }
    virtual double tailSeconds() const              { return 2.0; }
    // Silent through SF BUILD's cut (DROP): every instrument but DECK and CONTROL; STRIP and TAP
    // unless their ROLE is DECK. c.p holds this chunk's parameters.
    virtual bool takesDropCut (const Ctx&) const    { return instrument(); }

    // Files (samplers, DECK). Message thread.
    virtual bool acceptsFiles() const               { return false; }
    virtual void loadFiles (const juce::StringArray&) {}
    virtual juce::String fileHint() const           { return "DROP AUDIO FILE"; }

    // State beyond parameters (samples, track lists, stored tables). Message thread.
    virtual void save (juce::ValueTree&) const {}
    virtual void load (const juce::ValueTree&) {}
    // A state is in place (its parameters, then load): what the module makes from both can be made
    // ready here, off the audio thread, for the next block. Message thread; the audio may be running.
    virtual void restored() {}

    // Display panel (message thread). Read only atomics / benign values set by the audio thread.
    virtual int displayHeight() const               { return 140; }
    // The status line the display shows (SF TERROIR's imports and captures), as text. Message thread.
    virtual juce::String statusLine() const         { return {}; }
    virtual void paint (juce::Graphics&, juce::Rectangle<float>) {}
    virtual void displayMouse (juce::Point<float>, juce::Rectangle<float>, bool down) { juce::ignoreUnused (down); }
    virtual void displayMouseUp() {}

    // Program list (only SF CONTROL uses it: songs).
    virtual int numPrograms() const                 { return 1; }
    virtual int currentProgram() const              { return 0; }
    virtual void setProgram (int)                   {}

    // Message-thread service from the host processor: set a parameter and tell Live.
    std::function<void (int index, float value)> setParam;
    std::function<float (int index)> getParam;
    // The value a stored song holds for a parameter (NaN: that song isn't stored). Audio thread.
    std::function<float (int song, int index)> getSongValue;
};

using ModulePtr = std::unique_ptr<Module>;
} // namespace sf
