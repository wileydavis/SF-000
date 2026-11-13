#pragma once
// SF-000 rig: the one file that lists the lanes, what plays in each, and which knob drives what.
// A lane is one of the suite's instruments with its model fixed and most of its parameters set
// once here; the main screen's twelve knobs per lane drive the rest. To swap a lane's sound,
// change its line in lanes(); nothing else needs touching.
#include <vector>

namespace sf0
{
constexpr int kLanes = 11;
constexpr int kLaneKnobs = 12;    // 8 sound places, then DECAY or GATE, DENSITY, FILTER, LEVEL
constexpr int kSlots = 3;         // insert effects per lane
constexpr int kCells = 8;         // controls per insert effect
// The lane strip's own controls (the sends, the three-band EQ, the generator's knobs) and what a
// stored value of each means. Raise it when one of them changes its meaning (a new EQ curve, a new
// send law): snapshots say which they were made with.
constexpr int kChassisVersion = 1;

// kind: u knob, b bipolar knob (centre = off), m list knob (a menu), t switch, h hold key.
// lo..hi: the part of the parameter's travel the knob covers (0..1 = all of it).
struct KnobDef { const char* label; const char* param; char kind; float lo = 0.0f, hi = 1.0f; };
struct Fixed   { const char* param; float value; };     // plain units; a menu by index

struct LaneDef
{
    const char* name;       // on the panel
    const char* slug;       // in control ids: snapshots are stored by these, so they never change
    const char* module;     // the suite module that plays
    bool pitched;           // steps hold notes
    int octave;             // a new step's octave (C3 = MIDI 60)
    std::vector<Fixed> fixed;
    KnobDef knobs[kLaneKnobs];
};

struct FxDef
{
    const char* name;
    const char* module;
    std::vector<Fixed> fixed;
    KnobDef cells[kCells];  // label nullptr = an empty place
};

inline const std::vector<LaneDef>& lanes()
{
    static const std::vector<LaneDef> L = {
        { "KICK", "kick", "KICK", false, 3,
          // a round 909 with an 808's body underneath, brought in by LAYER (the 808 plays the lows)
          { { "model", 0 }, { "layer_model", 1 }, { "split", 1.0f }, { "split_hz", 110 }, { "layer", 0.3f }, { "tune", -1 }, { "decay", 0.5f },
            { "punch", 0.45f }, { "click", 0.35f }, { "drive", 0.12f }, { "level", -3 }, { "alloc", 0 } },
          { { "TUNE", "tune", 'b' }, { "LAYER", "layer", 'u' }, { "ECHO SPACE", "echo_space", 'm' }, { "ECHO SWING", "echo_swing", 'u' },
            { "PITCH TIME", "pitch_time", 'u' }, { "PUNCH", "punch", 'u' }, { "CLICK", "click", 'u' }, { "DRIVE", "drive", 'u' },
            { "DECAY", "decay", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } } },

        { "CLOSED HAT", "closed_hat", "PERC", false, 3,
          { { "model", 0 }, { "decay", 0.28f }, { "tone", 0.55f }, { "spread", 0.3f }, { "level", -13 }, { "alloc", 0 } },
          { { "TUNE", "tune", 'b' }, { "ECHO PROB", "echo_prob", 'u' }, { "ECHO SPACE", "echo_space", 'm' }, { "ECHO SWING", "echo_swing", 'u' },
            { "SPREAD", "spread", 'u' }, { "ACCENT", "accent", 'u' }, { "TONE", "tone", 'u' }, { "DRIVE", "drive", 'u' },
            { "DECAY", "decay", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } } },

        { "OPEN HAT", "open_hat", "PERC", false, 3,
          { { "model", 0 }, { "decay", 0.72f }, { "tone", 0.5f }, { "spread", 0.4f }, { "level", -15 }, { "alloc", 0 } },
          { { "TUNE", "tune", 'b' }, { "ECHO PROB", "echo_prob", 'u' }, { "ECHO SPACE", "echo_space", 'm' }, { "ECHO SWING", "echo_swing", 'u' },
            { "SPREAD", "spread", 'u' }, { "ACCENT", "accent", 'u' }, { "TONE", "tone", 'u' }, { "DRIVE", "drive", 'u' },
            { "DECAY", "decay", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } } },

        { "CLAP", "clap", "PERC", false, 3,
          { { "model", 5 }, { "decay", 0.4f }, { "tone", 0.5f }, { "spread", 0.5f }, { "level", -11 }, { "alloc", 0 } },
          { { "TUNE", "tune", 'b' }, { "ECHO PROB", "echo_prob", 'u' }, { "ECHO SPACE", "echo_space", 'm' }, { "ECHO SWING", "echo_swing", 'u' },
            { "SPREAD", "spread", 'u' }, { "ACCENT", "accent", 'u' }, { "TONE", "tone", 'u' }, { "DRIVE", "drive", 'u' },
            { "DECAY", "decay", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } } },

        { "PERC", "perc", "STRIKE", false, 3,
          // a struck membrane: SIZE, TENSION and MATERIAL take it from a tom to a pipe to a sheet of glass
          { { "model", 4 }, { "size", 0.55f }, { "material", 0.3f }, { "decay", 0.35f }, { "tension", 0.4f }, { "power", 0.45f }, { "distance", 0.25f },
            { "level", -14 }, { "alloc", 0 } },
          { { "TUNE", "tune", 'b' }, { "SIZE", "size", 'u' }, { "POSITION", "position", 'u' }, { "DISTANCE", "distance", 'u' },
            { "SPREAD", "spread", 'u' }, { "TENSION", "tension", 'u' }, { "MATERIAL", "material", 'u' }, { "POWER", "power", 'u' },
            { "DECAY", "decay", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } } },

        { "BASS", "bass", "BASS", true, 1,
          // a rolling ladder bass with a sine sub under it (LAYER); the kick's pocket is cut for it
          { { "model", 0 }, { "layer_model", 5 }, { "layer", 0.5f }, { "split", 1.0f }, { "split_hz", 120 }, { "cutoff", 0.3f }, { "res", 0.35f },
            { "env_amt", 0.45f }, { "gate", 0.4f }, { "decay", 0.4f }, { "pocket_depth", 0.7f }, { "level", -7 }, { "alloc", 0 } },
          { { "GLIDE", "glide", 'u' }, { "LAYER", "layer", 'u' }, { "RES", "res", 'u' }, { "POCKET DEPTH", "pocket_depth", 'u' },
            { "DECAY", "decay", 'u' }, { "ENV AMT", "env_amt", 'u' }, { "CUTOFF", "cutoff", 'u' }, { "DRIVE", "drive", 'u' },
            { "GATE", "gate", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } } },

        { "RUMBLE", "rumble", "RUMBLE", false, 3,
          // made from the kick itself (and from its own steps, if any are lit)
          { { "model", 0 }, { "source", 2 }, { "size", 0.7f }, { "dark", 0.68f }, { "grit", 0.25f }, { "pump", 0.78f }, { "level", -10 }, { "alloc", 0 } },
          { { "TUNE", "tune", 'b' }, { "SIZE", "size", 'u' }, { "POCKET SHAPE", "pocket_shape", 'u' }, { "PUMP", "pump", 'u' },
            { "WIDTH", "width", 'u' }, { "PREDELAY", "predelay", 'm' }, { "DARK", "dark", 'u' }, { "GRIT", "grit", 'u' },
            { "GATE", "gate", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } } },

        { "LEAD", "lead", "LEAD", true, 3,
          // the 303 circuit: a gate that reaches the next step slides, a hard step is the accent
          { { "model", 0 }, { "cutoff", 0.35f }, { "res", 0.75f }, { "env_amt", 0.6f }, { "accent", 0.7f }, { "gate", 0.5f }, { "level", -12 }, { "alloc", 0 } },
          { { "TUNE", "tune", 'b' }, { "ACCENT", "accent", 'u' }, { "RES", "res", 'u' }, { "COLOR", "color", 'u' },
            { "DECAY", "decay", 'u' }, { "ENV AMT", "env_amt", 'u' }, { "CUTOFF", "cutoff", 'u' }, { "DRIVE", "drive", 'u' },
            { "GATE", "gate", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } } },

        { "CHORD", "chord", "LEAD", true, 3,
          // the dub stab: one note plays the chord
          { { "model", 3 }, { "cutoff", 0.4f }, { "res", 0.3f }, { "env_amt", 0.5f }, { "space", 0.6f }, { "gate", 0.5f }, { "level", -15 }, { "alloc", 0 } },
          { { "DETUNE", "detune", 'u' }, { "ATTACK", "attack", 'u' }, { "RES", "res", 'u' }, { "POCKET DEPTH", "pocket_depth", 'u' },
            { "DECAY", "decay", 'u' }, { "ENV AMT", "env_amt", 'u' }, { "CUTOFF", "cutoff", 'u' }, { "SPACE", "space", 'u' },
            { "GATE", "gate", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } } },

        { "PAD", "pad", "PAD", true, 3,
          // a drifting dub wash that plays a minor seventh from one note
          { { "model", 4 }, { "chord", 2 }, { "cutoff", 0.4f }, { "space", 0.7f }, { "drift", 0.6f }, { "attack", 0.35f }, { "level", -15 }, { "alloc", 0 } },
          { { "DETUNE", "detune", 'u' }, { "COLOR", "color", 'u' }, { "RES", "res", 'u' }, { "POCKET DEPTH", "pocket_depth", 'u' },
            { "DECAY", "decay", 'u' }, { "ATTACK", "attack", 'u' }, { "CUTOFF", "cutoff", 'u' }, { "SPACE", "space", 'u' },
            { "GATE", "gate", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } } },

        { "VOICE", "voice", "VOICE", true, 3,
          // a voice over a bad radio link. With no phrase loaded it sings a formant choir from the notes (and TUNE,
          // SIGNAL and GATE have nothing to act on); drop an audio file on the lane to give it a phrase
          { { "model", 1 }, { "signal", 0.7f }, { "echo", 0.3f }, { "echo_fb", 0.55f }, { "level", -14 }, { "alloc", 0 } },
          { { "TUNE", "tune", 'b' }, { "SIGNAL", "signal", 'u' }, { "ECHO TIME", "echo_time", 'm' }, { "POCKET DEPTH", "pocket_depth", 'u' },
            { "ECHO FDBK", "echo_fb", 'u' }, { "ECHO", "echo", 'u' }, { "TONE", "tone", 'u' }, { "DRIVE", "drive", 'u' },
            { "GATE", "gate", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } } },
    };
    return L;
}

// The insert effects, in the order a slot steps through them.
inline const std::vector<FxDef>& effects()
{
    static const std::vector<FxDef> F = {
        { "DELAY", "DELAY", {},
          { { "TIME", "time", 'm' }, { "FEEDBACK", "feedback", 'u' }, { "TONE", "tone", 'b' }, { "DRIVE", "drive", 'u' },
            { "THROW", "throw", 'h' }, { "FREEZE", "freeze", 't' }, { "MIX", "mix", 'u' }, { "WIDTH", "width", 'u' } } },
        { "REVERB", "REVERB", {},
          { { "SIZE", "size", 'u' }, { "DECAY", "decay", 'u' }, { "DARK", "dark", 'u' }, { "PREDELAY", "predelay", 'm' },
            { "DUCK", "duck", 'u' }, { "FREEZE", "freeze", 't' }, { "THROW", "throw", 'h' }, { "MIX", "mix", 'u' } } },
        { "CONVOLUTION", "TERROIR", {},
          { { "SIZE", "size", 'u' }, { "DECAY", "decay", 'u' }, { "DARK", "dark", 'u' }, { "PREDELAY", "predelay", 'm' },
            { "DUCK", "duck", 'u' }, { "FREEZE", "freeze", 't' }, { "THROW", "throw", 'h' }, { "MIX", "mix", 'u' } } },
        { "DISTORTION", "DRIVE", {},
          { { "DRIVE", "drive", 'u' }, { "CHARACTER", "character", 'u' }, { "TONE", "tone", 'b' }, { "LOW KEEP", "low_keep", 'u' },
            { "MIX", "mix", 'u' }, { "LEVEL", "level", 'u' }, { "AUTO GAIN", "auto_gain", 't' }, { nullptr, nullptr, 'u' } } },
        { "COMPRESSOR", "GLUE", {},
          { { "PUSH", "push", 'u' }, { "ATTACK", "attack", 'm' }, { "RELEASE", "release", 'm' }, { "RATIO", "ratio", 'm' },
            { "SC HPF", "sc_hpf", 'u' }, { "MIX", "mix", 'u' }, { "LEVEL", "level", 'u' }, { nullptr, nullptr, 'u' } } },
        { "RADIO", "RADIO", {},
          { { "SIGNAL", "signal", 'u' }, { "BAND", "band", 'u' }, { "WIDTH", "width", 'u' }, { "NOISE", "noise", 'u' },
            { "DROPOUT", "dropout", 'u' }, { "SQUELCH", "squelch", 'u' }, { "RING", "ring", 'u' }, { "MIX", "mix", 'u' } } },
    };
    return F;
}

// The two returns every lane sends to: A is a dub delay, B a reverb. Wet only.
inline const FxDef& returnA()
{
    static const FxDef d { "DUB", "DELAY", { { "mix", 1.0f }, { "dry", 0 }, { "time", 6 }, { "feedback", 0.55f }, { "mode", 1 }, { "lo_cut", 160 }, { "wow", 0.15f } },
        { { "DUB TIME", "time", 'm' }, { "DUB FDBK", "feedback", 'u' }, { "DUB TONE", "tone", 'b' }, { "DUB DRIVE", "drive", 'u' },
          { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' } } };
    return d;
}
// B is one of two reverbs, chosen in its CONFIG: 0 the suite's REVERB, 1 its convolution reverb
// (TERROIR: a place, from an impulse response dropped on it). Each has its own four knobs.
constexpr int kSpaceModels = 2;
inline const char* spaceModelName (int model) { return model == 1 ? "CONVOLUTION" : "REVERB"; }
inline const FxDef& returnB (int model = 0)
{
    static const FxDef d[kSpaceModels] = {
        { "SPACE", "REVERB", { { "mix", 1.0f }, { "dry", 0 }, { "size", 0.65f }, { "decay", 3.5f }, { "dark", 0.55f }, { "duck", 0.4f }, { "lo_cut", 220 } },
          { { "SPACE SIZE", "size", 'u' }, { "SPACE DECAY", "decay", 'u' }, { "SPACE DARK", "dark", 'u' }, { "SPACE DUCK", "duck", 'u' },
            { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' } } },
        // the place as it was captured (SIZE and DECAY at 100 %), ducked and cut like the reverb it stands in for
        { "SPACE", "TERROIR", { { "mix", 1.0f }, { "dry", 0 }, { "duck", 0.4f }, { "lo_cut", 220 } },
          { { "SPACE SIZE", "size", 'u' }, { "SPACE DECAY", "decay", 'u' }, { "SPACE DARK", "dark", 'u' }, { "SPACE DUCK", "duck", 'u' },
            { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' } } } };
    return d[model == 1 ? 1 : 0];
}

// The drum bus, in the master: the lanes its CONFIG names (the five drums, to begin with) play
// through it together on their way to the mix. Compression, drive, crunch on the highs, punch, and a
// tuned boom underneath. Four knobs on the master's face; the rest is in its CONFIG. With the four
// at nought it is not there.
constexpr int kBusKnobs = 4;
inline const FxDef& drumBus()
{
    static const FxDef d { "DRUM BUS", "DRUMBUS", {},
        { { "DRIVE", "drive", 'u' }, { "CRUNCH", "crunch", 'u' }, { "PUNCH", "punch", 'b' }, { "BOOM", "boom", 'u' },
          { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' } } };
    return d;
}

// The master chain, built in: the lows made mono, tape, a bus compressor, a true-peak limiter that
// trims its own input until the loudest passage sits at the target loudness.
struct MasterStage { const char* module; std::vector<Fixed> fixed; KnobDef knob; };
inline const std::vector<MasterStage>& masterChain()
{
    static const std::vector<MasterStage> M = {
        { "LOWEND", { { "mono_freq", 150 }, { "rumble_cut", 25 } }, { nullptr, nullptr, 'u' } },
        { "DRIVE",  { { "character", 0.0f }, { "low_keep", 120 }, { "auto_gain", 1 }, { "drive", 0.12f } }, { "TAPE", "drive", 'u', 0.0f, 0.6f } },
        { "GLUE",   { { "attack", 4 }, { "release", 4 }, { "ratio", 0 }, { "sc_hpf", 110 }, { "push", 0.25f } }, { "COMP", "push", 'u', 0.0f, 0.8f } },
        { "LIMIT",  { { "auto", 1 }, { "auto_mode", 1 }, { "ceiling", -1.0f }, { "target", -9.0f }, { "clip", 0.3f }, { "input", 5.0f } }, { "LIMIT", "target", 'u' } },
    };
    return M;
}
} // namespace sf0
