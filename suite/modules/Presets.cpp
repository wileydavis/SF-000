// Factory presets. Names are descriptive ("GROUP/NAME": GROUP is a sub-menu). A preset sets every
// sound parameter: anything not named here goes to its default (setup values such as ROLE,
// ALLOC and KEY FOLLOW are never touched). Values are plain units: % as 0..1, menus as the
// item index, toggles 0/1, dB / st / Hz / s as shown on the knob.
// MOTION: mot (module, k, source, rate, target parameter id, depth).
//   sources: 1 SINE 2 TRIANGLE 3 SQUARE 4 RAMP UP 5 RAMP DOWN 6 RANDOM 7 DRIFT 8 KICK 9 INPUT/NOTES
//   rates:   0 1/16 1 1/8 2 1/4 3 1/2 4 1 BAR 5 2 BAR 6 4 BAR 7 8 BAR 8 16 BAR 9 32 BAR 10 64 BAR
//            11 3/16 12 5/16 13 7/16 14 1/4D 15 1/2D 16 1/4T 17 1/2T 18 3 BAR 19 5 BAR 20 7 BAR
#include "../core/Registry.h"
#include <map>
#include <mutex>

namespace sf
{
namespace
{
using Values = std::vector<std::pair<std::string, float>>;

// MOTION TARGET index of a parameter (its place among the module's visible continuous
// parameters, + 1 for OFF), as the processor builds the menu.
int targetIndex (const std::string& module, const std::string& param)
{
    static std::map<std::string, std::vector<std::string>> cache;
    static std::mutex lock;
    const std::lock_guard<std::mutex> g (lock);
    auto it = cache.find (module);
    if (it == cache.end())
    {
        std::vector<std::string> ids;
        for (auto& p : makeModule (module)->params())
            if (p.type == PType::Float && ! p.hidden) ids.push_back (p.id);
        it = cache.emplace (module, ids).first;
    }
    for (size_t i = 0; i < it->second.size(); ++i) if (it->second[i] == param) return (int) i + 1;
    jassertfalse;
    return 0;
}

Values mot (const std::string& module, int k, int src, int rate, const std::string& target, float depth)
{
    const std::string p = "m" + std::to_string (k);
    return { { p + "_src", (float) src }, { p + "_rate", (float) rate }, { p + "_target", (float) targetIndex (module, target) }, { p + "_depth", depth } };
}

Values operator+ (Values a, const Values& b) { a.insert (a.end(), b.begin(), b.end()); return a; }
// ---------------------------------------------------------------------------------- effects
std::vector<Preset> filter()
{
    return {
        { "DJ/CLEAN",             { { "res", 0.15f } } },
        { "DJ/RESONANT",          { { "res", 0.55f } } },
        { "DRIVEN/WARM",          { { "res", 0.3f }, { "drive", 0.3f } } },
        { "ENVELOPE/WAH",         { { "filter", 0.35f }, { "env_amt", 0.6f }, { "env_speed", 0.7f }, { "res", 0.5f } } },
        { "MOTION/SLOW SWEEP",    Values { { "res", 0.4f } } + mot ("FILTER", 1, 1, 8, "filter", 0.2f) },
    };
}

std::vector<Preset> gate()
{
    return {
        { "TRANCE/1/16",          { { "depth", 0.8f }, { "length", 0.5f }, { "pattern_a", 0 }, { "rate", 1 } } },
        { "OFFBEAT/CHOP",         { { "depth", 0.9f }, { "length", 0.6f }, { "pattern_a", 2 } } },
        { "TRESILLO",             { { "depth", 0.85f }, { "pattern_a", 4 } } },
        { "GALLOP",               { { "depth", 0.8f }, { "pattern_a", 5 } } },
        { "MORPH/8THS TO OFFBEAT",{ { "depth", 0.8f }, { "pattern_a", 1 }, { "pattern_b", 2 }, { "morph", 0.5f } } },
        { "SOFT/PUMP",            { { "depth", 0.5f }, { "pattern_a", 9 }, { "shape", 0.7f } } },
    };
}

std::vector<Preset> density()
{
    return {
        { "1/8 TO 1/16",          { { "density", 0.5f }, { "space", 1 } } },
        { "ROLLING 1/32",         { { "density", 0.85f }, { "space", 3 } } },
        { "TRIPLET 1/16T",        { { "density", 0.5f }, { "space", 2 } } },
        { "DOTTED DUB 1/8D",      { { "density", 0.5f }, { "space", 5 }, { "darken", 0.6f } } },
    };
}

std::vector<Preset> delay()
{
    return {
        { "DUB/1/8D",             { { "time", 6 }, { "feedback", 0.55f }, { "tone", 0.4f }, { "mix", 0.3f } } },
        { "DUB/1/4 DARK",         { { "time", 2 }, { "feedback", 0.6f }, { "tone", 0.35f }, { "mix", 0.25f }, { "drive", 0.3f } } },
        { "DUB/SPIRAL",           { { "time", 6 }, { "feedback", 0.95f }, { "tone", 0.38f }, { "drive", 0.4f }, { "mix", 0.3f } } },
        { "PING/1/8",             { { "time", 5 }, { "feedback", 0.45f }, { "mode", 1 }, { "mix", 0.25f } } },
        { "TAPE/WOBBLE",          { { "time", 3 }, { "feedback", 0.5f }, { "wow", 0.5f }, { "tone", 0.42f } } },
        { "THIN/HP REPEATS",      { { "time", 6 }, { "feedback", 0.55f }, { "tone", 0.62f } } },
        { "THROW/ONLY",           { { "input", 1 }, { "time", 6 }, { "feedback", 0.6f }, { "mix", 0.4f } } },
        { "SLAP/1/16",            { { "time", 8 }, { "feedback", 0.2f }, { "mix", 0.2f } } },
        { "DUCKED/1/8D",          { { "time", 6 }, { "feedback", 0.6f }, { "tone", 0.42f }, { "mix", 0.35f }, { "duck", 0.7f } } },
    };
}

std::vector<Preset> reverb()
{
    return {
        { "ROOM/SHORT",           { { "size", 0.35f }, { "decay", 0.8f }, { "dark", 0.4f }, { "mix", 0.2f } } },
        { "HALL/DARK",            { { "size", 0.75f }, { "decay", 5.0f }, { "dark", 0.75f }, { "mix", 0.25f } } },
        { "DUB/CHAMBER",          { { "size", 0.6f }, { "decay", 3.0f }, { "dark", 0.6f }, { "predelay", 3 }, { "mix", 0.3f } } },
        { "SPACE/HUGE",           { { "size", 1.0f }, { "decay", 14.0f }, { "dark", 0.6f }, { "mod", 0.5f }, { "mix", 0.3f } } },
        { "SPACE/DUCKED",         { { "size", 0.8f }, { "decay", 8.0f }, { "duck", 0.7f }, { "mix", 0.35f } } },
        { "GATED/1/8",            { { "gated", 1 }, { "gate_time", 1 }, { "size", 0.6f }, { "decay", 3.0f } } },
        { "THROW/ONLY",           { { "input", 1 }, { "size", 0.8f }, { "decay", 10.0f }, { "mix", 0.5f } } },
        { "BRIGHT/PLATE",         { { "size", 0.45f }, { "decay", 2.5f }, { "dark", 0.15f }, { "lo_cut", 200.0f } } },
    };
}

// TERROIR presets shape whatever place is loaded; they never change the PLACE.
std::vector<Preset> terroir()
{
    return {
        { "AS RECORDED",          { } },
        { "ROOM/TIGHT",           { { "decay", 0.45f }, { "lo_cut", 180.0f }, { "mix", 0.2f } } },
        { "ROOM/SMALLER",         { { "size", 0.7f }, { "decay", 0.8f }, { "mix", 0.2f } } },
        { "BIG/DARK",             { { "size", 1.6f }, { "dark", 0.35f }, { "mix", 0.3f } } },
        { "TAIL/ONLY",            { { "start", 40.0f }, { "predelay", 3 }, { "mix", 0.3f } } },
        { "GATED/1/4",            { { "length", 5 }, { "mix", 0.3f } } },
        { "GATED/1/8 DARK",       { { "length", 6 }, { "dark", 0.4f }, { "mix", 0.3f } } },
        { "REVERSE/1/2 SWELL",    { { "length", 4 }, { "reverse", 1 }, { "mix", 0.35f } } },
        { "THROW/ONLY",           { { "input", 1 }, { "mix", 0.5f } } },
        { "MIX/IN THE ROOM",      { { "mix", 0.2f }, { "duck", 0.0f }, { "lo_cut", 60.0f } } },   // on the whole mix (DRY on)
    };
}

std::vector<Preset> drive()
{
    return {
        { "TAPE/WARM",            { { "drive", 0.25f }, { "character", 0.1f } } },
        { "TUBE/PUSH",            { { "drive", 0.4f }, { "character", 0.35f } } },
        { "FUZZ/HARD",            { { "drive", 0.55f }, { "character", 0.65f } } },
        { "INDUSTRIAL",           { { "drive", 0.7f }, { "character", 0.95f } } },
        { "PARALLEL/KEEP LOW",    { { "drive", 0.5f }, { "character", 0.4f }, { "low_keep", 120.0f }, { "mix", 0.6f } } },
    };
}

std::vector<Preset> radio()
{
    return {
        { "AM/DISTANT",           { { "signal", 0.3f } } },
        { "SHORTWAVE/BROKEN",     { { "signal", 0.15f }, { "dropout", 0.6f }, { "noise", 0.5f } } },
        { "TELEPHONE",            { { "band", 1500.0f }, { "width", 0.3f }, { "signal", 0.8f }, { "noise", 0.05f } } },
    };
}

std::vector<Preset> repeat()
{
    return {
        { "1/16 STRAIGHT",        { { "length", 2 } } },
        { "1/32 PITCH DOWN",      { { "length", 3 }, { "pitch", -7.0f }, { "decay", 0.3f } } },
        { "1/8T DARKEN",          { { "length", 4 }, { "filter", 0.7f }, { "decay", 0.2f } } },
    };
}

std::vector<Preset> duck()
{
    return {
        { "KICK/SOFT",            { { "depth", 0.4f }, { "shape", 0.3f } } },
        { "KICK/PUMP",            { { "depth", 0.8f }, { "shape", 0.85f } } },
        { "GRID/1/4",             { { "source", 2 }, { "depth", 0.6f }, { "rate", 1 } } },
    };
}

std::vector<Preset> energy()
{
    return {
        { "DRUMS",                { { "profile", 0 } } },
        { "BASS",                 { { "profile", 1 } } },
        { "MUSIC",                { { "profile", 2 } } },
        { "MASTER",               { { "profile", 4 }, { "drive_amt", 0.2f } } },
    };
}

std::vector<Preset> isolator()
{
    return {
        { "DJ/STANDARD",          {} },
        { "DJ/LOW 120 HZ",        { { "xover_lo", 120.0f } } },
    };
}

std::vector<Preset> build()
{
    return {
        { "STANDARD",             { { "noise", 0.4f }, { "space", 0.4f }, { "width", 0.4f } } },
        { "DARK RISE",            { { "noise", 0.25f }, { "space", 0.6f }, { "hp_max", 800.0f } } },
        { "BRIGHT NOISE",         { { "noise", 0.7f }, { "space", 0.3f }, { "hp_max", 2500.0f } } },
    };
}

std::vector<Preset> glue()
{
    // (SF-000, the ninetieth build: the compressor's own controls. Places to start from; THRESHOLD and MAKEUP are left
    // where they are out, to be set by ear for the sound in hand.)
    return {
        { "TRANSIENT SHAPE",      { { "attack", 25.0f }, { "release", 120.0f }, { "ratio", 4 }, { "knee", 3.0f } } },
        { "BASS ARTICULATION",    { { "attack", 80.0f }, { "release", 200.0f }, { "ratio", 3 }, { "knee", 6.0f } } },
        { "BUS GLUE",             { { "attack", 20.0f }, { "auto", 1 }, { "ratio", 2 }, { "knee", 9.0f }, { "sc_hpf", 90.0f } } },
    };
}

std::vector<Preset> tone()
{
    return {
        { "MASTER/NEUTRAL",       {} },
        { "MASTER/CLUB WEIGHT",   { { "low", 1.5f }, { "mud", 1.5f }, { "air", 1.0f } } },
        { "MASTER/WARM",          { { "tilt", -1.0f }, { "mud", 1.0f } } },
        { "MASTER/BRIGHT",        { { "presence", 1.0f }, { "air", 2.0f }, { "mud", 1.0f } } },
        { "MASTER/DARK HYPNOTIC", { { "tilt", -1.5f }, { "low", 1.0f }, { "presence", -1.0f } } },
    };
}

std::vector<Preset> lowend()
{
    return {
        { "CLUB/MONO 120",        { { "mono_freq", 120.0f }, { "sub_clip", 0.2f } } },
        { "FESTIVAL/MONO 150",    { { "mono_freq", 150.0f }, { "sub_clip", 0.35f }, { "rumble_cut", 30.0f } } },
        { "CLEAN",                { { "sub_clip", 0.0f } } },
    };
}

std::vector<Preset> limit()
{
    return {
        { "CLUB/-8 LUFS",         { { "target", -8.0f }, { "ceiling", -0.5f }, { "clip", 0.4f }, { "auto", 1 }, { "release", 100.0f } } },
        { "FESTIVAL/-7 LUFS",     { { "target", -7.0f }, { "ceiling", -0.5f }, { "clip", 0.55f }, { "auto", 1 }, { "release", 80.0f } } },
        { "STREAM/-14 LUFS",      { { "target", -14.0f }, { "ceiling", -1.0f }, { "clip", 0.1f }, { "auto", 1 }, { "release", 150.0f } } },
        { "BEDROOM/-12 LUFS",     { { "target", -12.0f }, { "ceiling", -1.0f }, { "clip", 0.2f }, { "auto", 1 }, { "release", 120.0f } } },
    };
}

std::vector<Preset> resonator()
{
    // chord: 0 OCTAVES 1 FIFTHS 2 MIN 3 MIN7 4 MIN9 5 SUS4 6 MAJ 7 MAJ7; octave: 0 C1 .. 3 C4.
    return {
        { "DUB/MIN7 WASH",        { { "chord", 3 }, { "octave", 2 }, { "decay", 4.0f }, { "damp", 0.5f }, { "mix", 0.4f }, { "spread", 0.7f } } },
        { "DUB/MIN9 HAZE",        { { "chord", 4 }, { "octave", 2 }, { "decay", 6.0f }, { "damp", 0.65f }, { "mix", 0.35f }, { "spread", 0.8f }, { "tone", 0.6f } } },
        { "METAL/OCTAVES",        { { "chord", 0 }, { "octave", 2 }, { "decay", 5.0f }, { "damp", 0.15f }, { "mix", 0.45f }, { "spread", 0.6f } } },
        { "METAL/HAT STRINGS",    { { "chord", 2 }, { "octave", 3 }, { "decay", 1.2f }, { "damp", 0.25f }, { "mix", 0.4f }, { "spread", 0.8f } } },
        { "DRONE/DARK FIFTHS",    { { "chord", 1 }, { "octave", 1 }, { "decay", 9.0f }, { "damp", 0.75f }, { "mix", 0.5f }, { "drive", 0.3f } } },
        { "SHORT/WOOD MIN",       { { "chord", 2 }, { "octave", 2 }, { "decay", 0.5f }, { "damp", 0.7f }, { "mix", 0.5f } } },
        { "GRIT/SUS4 RATTLE",     { { "chord", 5 }, { "octave", 2 }, { "decay", 2.5f }, { "damp", 0.35f }, { "mix", 0.45f }, { "drive", 0.6f } } },
    };
}

std::vector<Preset> strip()
{
    return {
        { "HATS/ROLL 1/16",       { { "density", 0.5f }, { "echo_space", 1 } } },
        { "PERC/GATED",           { { "gate", 0.3f } } },
        { "SYNTH/DUB THROW",      { { "space", 0.3f }, { "space_type", 1 }, { "space_size", 0.6f } } },
        { "SYNTH/WASH",           { { "space", 0.4f }, { "space_type", 0 }, { "space_size", 0.7f } } },
        { "BASS/POCKET",          { { "pocket", 0.6f }, { "pocket_shape", 0.3f } } },
        { "DRIVE/WARM",           { { "drive", 0.3f }, { "character", 0.1f } } },
        { "DRIVE/FUZZ",           { { "drive", 0.5f }, { "character", 0.7f } } },
        { "PATTERN/OFFBEAT",      { { "gate", 0.6f }, { "gate_mode", 1 }, { "pattern", 2 } } },
    };
}
} // namespace

std::vector<Preset> factoryPresets (const std::string& id)
{
    // (SF-000, the hundred and third build) the lanes' instruments have no presets: their sounds are SYNTH's patches and
    // snapshots (Lane.cpp)
    if (id == "KICK" || id == "PERC" || id == "RUMBLE" || id == "BASS" || id == "LEAD" || id == "PAD" || id == "VOICE" || id == "STRIKE") return {};
    if (id == "FILTER") return filter();
    if (id == "GATE") return gate();
    if (id == "DENSITY") return density();
    if (id == "DELAY") return delay();
    if (id == "REVERB") return reverb();
    if (id == "TERROIR") return terroir();
    if (id == "DRIVE") return drive();
    if (id == "RADIO") return radio();
    if (id == "REPEAT") return repeat();
    if (id == "DUCK") return duck();
    if (id == "ENERGY") return energy();
    if (id == "ISOLATOR") return isolator();
    if (id == "BUILD") return build();
    if (id == "GLUE") return glue();
    if (id == "TONE") return tone();
    if (id == "LOWEND") return lowend();
    if (id == "LIMIT") return limit();
    if (id == "STRIP") return strip();
    if (id == "RESONATOR") return resonator();
    return {};   // CONTROL, DECK, ALLOC, TAP, ROOMEQ: setup modules
}
} // namespace sf
