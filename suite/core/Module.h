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

// (SF-000) How a change of a parameter reaches a note that is already sounding. Every parameter says which,
// and is held to it (SF-000's Main.cpp, reachTest):
//   Live   the sounding note follows it, while it sounds
//   Eased  by a made change-over: the voices that are sounding are let go and the keys that are down are played again
//          as it now stands, or one sound is crossed into the other. Heard at once, though not by bending the note
//   Next   from the next note (on a drum: the next hit). One that is sounding keeps what it began with, and the
//          instrument says so where the control is shown
enum class Reach { Live, Eased, Next };

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
    Reach reach = Reach::Live;      // (SF-000) how a change reaches a sounding note
    bool quick = false;             // (SF-000, the hundredth build) taken as it is set, not over 30 ms: the module makes its own
                                    // way there (SAMPLE's SIZE and END: a note they cut is let go over 3 ms)
    std::vector<std::string> liveIn;    // (SF-000) the models on which a NEXT one is in fact heard at once (GRANULAR reads its region as it plays)
    std::vector<std::string> nextIn;    // (SF-000, the hundredth build) and those on which a LIVE one is heard only from the next note (SAMPLE: what only the layer hears)
    std::string lo, hi;             // (SF-000, the hundred and fourth build) a "BIP" knob that is no filter: what its two sides are
                                    // called ("SHORT 40", "LONG 40", MIDDLE between). "-" and "+": a signed depth, OFF between

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
    ParamSpec& eased()          { reach = Reach::Eased; return *this; }    // (SF-000)
    ParamSpec& next()           { reach = Reach::Next; return *this; }     // (SF-000)
    ParamSpec& quickly()        { quick = true; return *this; }            // (SF-000)
    ParamSpec& liveOn (const char* model) { liveIn.push_back (model); return *this; }      // (SF-000)
    ParamSpec& nextOn (const char* model) { nextIn.push_back (model); return *this; }      // (SF-000)
    ParamSpec& ends (const char* l, const char* h) { lo = l; hi = h; return *this; }      // (SF-000)
    Reach reachIn (const std::string& model) const { for (auto& m : liveIn) if (m == model) return Reach::Live; for (auto& m : nextIn) if (m == model) return Reach::Next; return reach; }
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
    float bend = 0.0f;            // (SF-000) what a pitch wheel adds to TUNE, in semitones (0: at rest)
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
    // (SF-000, the ninety-fourth build) Does a note held for good go on sounding? Not a sample that plays once and is over:
    // a sequence's only step, longer than the sequence, is then struck every time round and not tied to itself (LanePlayer).
    virtual bool holdsNotes() const                 { return true; }
    // The status line the display shows (SF TERROIR's imports and captures), as text. Message thread.
    virtual juce::String statusLine() const         { return {}; }
    virtual void paint (juce::Graphics&, juce::Rectangle<float>) {}
    virtual void displayMouse (juce::Point<float>, juce::Rectangle<float>, bool down) { juce::ignoreUnused (down); }
    virtual void displayMouseUp() {}
    virtual void displayDouble (juce::Point<float>, juce::Rectangle<float>) {}      // (SF-000) a double-click on the display
    virtual bool displayWheel (juce::Point<float>, juce::Rectangle<float>, float dy) { juce::ignoreUnused (dy); return false; }      // (SF-000) the wheel over the display: true if it took it
    // (SF-000) A reading of the module's own for the host to show (LIMIT: 0 the limiter's gain reduction, 1 the clipper's,
    // 2 the automatic trim as it stands, 3 the output's true peak, 4 the trim the last minute's loudest asks for; all dB). Any thread.
    virtual float meter (int) const { return 0.0f; }

    // Program list (only SF CONTROL uses it: songs).
    virtual int numPrograms() const                 { return 1; }
    virtual int currentProgram() const              { return 0; }
    virtual void setProgram (int)                   {}

    // Message-thread service from the host processor: set a parameter and tell Live.
    std::function<void (int index, float value)> setParam;
    std::function<float (int index)> getParam;
    // The value a stored song holds for a parameter (NaN: that song isn't stored). Audio thread.
    std::function<float (int song, int index)> getSongValue;
    // (SF-000, the hundredth build) The lane's steps as its sequencer has them, for a display: how many, the one playing
    // (-1: stopped), and for each whether it is lit and its note. Set by the host; message thread.
    struct StepsView { static constexpr int kMax = 64; int count = 0, now = -1; bool on[kMax] {}; int note[kMax] {}; float vel[kMax] {}; };
    std::function<void (StepsView&)> stepsOf;
};

using ModulePtr = std::unique_ptr<Module>;
} // namespace sf
