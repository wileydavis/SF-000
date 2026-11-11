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

// ---------------------------------------------------------------------------------- instruments
std::vector<Preset> kick()
{
    return {
        { "DEEP/ROUND 909",       { { "model", 0 }, { "tune", -1 }, { "decay", 0.5f }, { "punch", 0.45f }, { "click", 0.35f }, { "drive", 0.12f } } },
        { "DEEP/LONG 808",        { { "model", 1 }, { "tune", -2 }, { "decay", 0.7f }, { "punch", 0.4f }, { "click", 0.1f }, { "drive", 0.1f } } },
        { "DEEP/SINE SUB",        { { "model", 2 }, { "tune", -3 }, { "decay", 0.65f }, { "punch", 0.35f }, { "click", 0.15f }, { "drive", 0.05f } } },
        { "DEEP/WARM LAYER",      { { "model", 0 }, { "layer_model", 1 }, { "layer", 0.45f }, { "tune", -2 }, { "decay", 0.6f }, { "punch", 0.45f }, { "click", 0.25f }, { "drive", 0.15f } } },
        { "LAYER/808 BODY 909 TOP", { { "model", 0 }, { "layer_model", 1 }, { "layer", 1.0f }, { "split", 1.0f }, { "split_hz", 120 }, { "tune", -1 }, { "decay", 0.55f }, { "punch", 0.5f }, { "click", 0.45f }, { "drive", 0.12f } } },
        { "LAYER/SINE SUB 909 TOP", { { "model", 0 }, { "layer_model", 2 }, { "layer", 0.85f }, { "split", 1.0f }, { "split_hz", 90 }, { "decay", 0.5f }, { "punch", 0.55f }, { "click", 0.35f }, { "drive", 0.15f } } },
        { "HYPNOTIC/TIGHT",       { { "model", 0 }, { "decay", 0.3f }, { "punch", 0.55f }, { "click", 0.5f }, { "drive", 0.2f }, { "pitch_time", 0.4f } } },
        { "HYPNOTIC/MUTED",       { { "model", 5 }, { "decay", 0.4f }, { "punch", 0.4f }, { "click", 0.0f }, { "drive", 0.1f }, { "filter", 0.42f } } },
        { "HYPNOTIC/PULSE",       { { "model", 4 }, { "decay", 0.35f }, { "punch", 0.5f }, { "click", 0.3f }, { "drive", 0.25f } } },
        { "DUB/FELT",             { { "model", 5 }, { "tune", -1 }, { "decay", 0.55f }, { "punch", 0.3f }, { "click", 0.0f }, { "drive", 0.05f }, { "filter", 0.44f } } },
        { "DUB/BOOM",             { { "model", 1 }, { "tune", -4 }, { "decay", 0.8f }, { "punch", 0.3f }, { "click", 0.0f }, { "drive", 0.2f } } },
        { "HARD/909 PUSHED",      { { "model", 0 }, { "decay", 0.45f }, { "punch", 0.65f }, { "click", 0.6f }, { "drive", 0.5f } } },
        { "HARD/INDUSTRIAL",      { { "model", 3 }, { "decay", 0.5f }, { "punch", 0.6f }, { "click", 0.7f }, { "drive", 0.7f } } },
        { "HARD/DISTORTED TAIL",  { { "model", 3 }, { "tune", -2 }, { "decay", 0.75f }, { "punch", 0.55f }, { "click", 0.5f }, { "drive", 0.9f }, { "res", 0.3f } } },
        { "SHORT/CLICK",          { { "model", 2 }, { "decay", 0.15f }, { "punch", 0.7f }, { "click", 0.8f }, { "drive", 0.2f }, { "pitch_time", 0.3f } } },
        { "SHORT/ROLLING 1/16",   { { "model", 0 }, { "decay", 0.3f }, { "punch", 0.5f }, { "click", 0.3f }, { "density", 0.5f }, { "echo_space", 1 }, { "drive", 0.15f } } },
    };
}

std::vector<Preset> perc()
{
    return {
        { "HATS/909 CLOSED",      { { "model", 0 }, { "decay", 0.3f }, { "tone", 0.55f } } },
        { "HATS/909 OPEN",        { { "model", 0 }, { "decay", 0.75f }, { "tone", 0.5f } } },
        { "HATS/808 SOFT",        { { "model", 1 }, { "decay", 0.35f }, { "tone", 0.4f } } },
        { "HATS/606 BRIGHT",      { { "model", 2 }, { "decay", 0.3f }, { "tone", 0.7f } } },
        { "HATS/ROLLING 1/16",    { { "model", 0 }, { "decay", 0.25f }, { "density", 0.5f }, { "echo_space", 1 }, { "spread", 0.4f } } },
        { "HATS/SHUFFLE",         { { "model", 2 }, { "decay", 0.25f }, { "density", 0.45f }, { "echo_space", 1 }, { "echo_swing", 0.5f } } },
        { "HATS/GHOST 1/32",      { { "model", 1 }, { "decay", 0.2f }, { "density", 0.85f }, { "echo_space", 1 }, { "echo_prob", 0.7f } } },
        { "HATS/DRIFTING TONE",   Values { { "model", 0 }, { "decay", 0.3f } } + mot ("PERC", 1, 7, 7, "tone", 0.35f) },
        { "RIDE/909 WASH",        { { "model", 3 }, { "decay", 0.6f }, { "tone", 0.5f } } },
        { "RIDE/DARK",            { { "model", 3 }, { "decay", 0.5f }, { "tone", 0.3f }, { "filter", 0.45f } } },
        { "CLAP/DRY",             { { "model", 5 }, { "decay", 0.35f } } },
        { "CLAP/LONG ROOM",       { { "model", 5 }, { "decay", 0.75f }, { "tone", 0.45f } } },
        { "RIM/TIGHT",            { { "model", 6 }, { "decay", 0.4f }, { "tone", 0.6f } } },
        { "BELL/COWBELL",         { { "model", 7 }, { "decay", 0.45f } } },
        { "WOOD/CLAVE",           { { "model", 8 }, { "decay", 0.4f } } },
        { "SHAKER/OFFBEAT",       { { "model", 9 }, { "decay", 0.4f }, { "spread", 0.4f } } },
        { "METAL/FM PING",        { { "model", 10 }, { "decay", 0.4f }, { "tone", 0.6f } } },
        { "METAL/MODAL TOM",      { { "model", 11 }, { "decay", 0.5f }, { "tone", 0.4f }, { "tune", -5 } } },
        { "SNARE/808",            { { "model", 13 }, { "decay", 0.45f }, { "tone", 0.5f } } },
        { "SNARE/909 SNAPPY",     { { "model", 14 }, { "decay", 0.5f }, { "tone", 0.75f } } },
        { "SNARE/909 DUB ECHO",   { { "model", 14 }, { "decay", 0.45f }, { "tone", 0.5f }, { "density", 0.35f }, { "echo_space", 5 } } },
        { "TOM/808 LOW",          { { "model", 15 }, { "tune", -5 }, { "decay", 0.6f } } },
        { "TOM/808 HIGH",         { { "model", 15 }, { "tune", 7 }, { "decay", 0.45f } } },
        { "TOM/909 MID",          { { "model", 16 }, { "decay", 0.5f }, { "tone", 0.4f } } },
    };
}

std::vector<Preset> rumble()
{
    return {
        { "REVERB/DEEP",          { { "model", 0 }, { "size", 0.7f }, { "dark", 0.65f }, { "grit", 0.25f }, { "pump", 0.75f } } },
        { "REVERB/LONG DARK",     { { "model", 0 }, { "size", 0.9f }, { "dark", 0.8f }, { "grit", 0.2f }, { "pump", 0.8f }, { "predelay", 2 } } },
        { "DELAY/OFFBEAT",        { { "model", 1 }, { "size", 0.55f }, { "dark", 0.6f }, { "grit", 0.3f }, { "pump", 0.7f } } },
        { "DRONE/SUB",            { { "model", 2 }, { "size", 0.5f }, { "dark", 0.7f }, { "grit", 0.15f }, { "pump", 0.85f } } },
        { "DISTORTED/GRIT",       { { "model", 3 }, { "size", 0.65f }, { "dark", 0.5f }, { "grit", 0.6f }, { "pump", 0.75f } } },
        { "DUB/BOOM",             { { "model", 4 }, { "size", 0.75f }, { "dark", 0.7f }, { "grit", 0.2f }, { "pump", 0.7f } } },
        { "TIGHT/GATED",          { { "model", 0 }, { "size", 0.6f }, { "grit", 0.3f }, { "pump", 0.7f }, { "gate", 0.4f } } },
        { "ROLLING/1/16",         { { "model", 0 }, { "size", 0.55f }, { "grit", 0.35f }, { "pump", 0.7f }, { "density", 0.5f }, { "echo_space", 1 } } },
        { "BERLIN/RUMBLE",        { { "model", 3 }, { "size", 0.75f }, { "dark", 0.7f }, { "grit", 0.45f }, { "pump", 0.85f } } },
        { "BERLIN/DEEP ROLL",     { { "model", 0 }, { "size", 0.85f }, { "dark", 0.8f }, { "grit", 0.3f }, { "pump", 0.8f }, { "density", 0.4f }, { "echo_space", 1 } } },
    };
}

std::vector<Preset> bass()
{
    return {
        { "SUB/SINE",             { { "model", 5 }, { "cutoff", 0.5f }, { "drive", 0.05f } } },
        { "SUB/SQUARE",           { { "model", 2 }, { "cutoff", 0.3f }, { "res", 0.1f }, { "env_amt", 0.2f } } },
        { "ROLLING/LADDER",       { { "model", 0 }, { "cutoff", 0.3f }, { "res", 0.35f }, { "env_amt", 0.45f }, { "gate", 0.35f }, { "decay", 0.4f } } },
        { "ROLLING/DARK",         { { "model", 0 }, { "cutoff", 0.2f }, { "res", 0.2f }, { "env_amt", 0.3f }, { "gate", 0.4f } } },
        { "ROLLING/1/16 ECHO",    { { "model", 0 }, { "cutoff", 0.28f }, { "res", 0.3f }, { "env_amt", 0.4f }, { "gate", 0.3f }, { "density", 0.5f }, { "echo_space", 1 } } },
        { "ROLLING/BREATHING",    Values { { "model", 0 }, { "cutoff", 0.28f }, { "res", 0.3f }, { "env_amt", 0.4f }, { "gate", 0.35f } } + mot ("BASS", 1, 1, 8, "cutoff", 0.12f) },
        { "MONO/101 PLUCK",       { { "model", 1 }, { "cutoff", 0.35f }, { "res", 0.25f }, { "env_amt", 0.55f }, { "gate", 0.3f }, { "decay", 0.35f } } },
        { "ACID/303 CLASSIC",     { { "model", 3 }, { "cutoff", 0.3f }, { "res", 0.7f }, { "env_amt", 0.6f }, { "accent", 0.7f }, { "glide", 0.3f } } },
        { "ACID/303 SQUELCH",     { { "model", 3 }, { "cutoff", 0.45f }, { "res", 0.85f }, { "env_amt", 0.75f }, { "accent", 0.9f }, { "drive", 0.3f } } },
        { "ACID/303 DARK",        { { "model", 3 }, { "cutoff", 0.2f }, { "res", 0.6f }, { "env_amt", 0.4f }, { "accent", 0.5f } } },
        { "FM/METALLIC",          { { "model", 4 }, { "cutoff", 0.5f }, { "color", 0.6f }, { "env_amt", 0.4f } } },
        { "FM/SOFT",              { { "model", 4 }, { "cutoff", 0.35f }, { "color", 0.2f } } },
        { "OFFBEAT/DUB",          { { "model", 6 }, { "cutoff", 0.35f }, { "res", 0.35f }, { "env_amt", 0.5f }, { "gate", 0.25f } } },
        { "OFFBEAT/BRIGHT",       { { "model", 6 }, { "cutoff", 0.5f }, { "res", 0.4f }, { "env_amt", 0.6f }, { "gate", 0.25f }, { "drive", 0.2f } } },
        { "LAYER/SUB + LADDER",   { { "model", 0 }, { "layer_model", 5 }, { "layer", 0.6f }, { "cutoff", 0.3f }, { "res", 0.3f }, { "env_amt", 0.4f } } },
        { "LAYER/SINE UNDER 303", { { "model", 3 }, { "layer_model", 5 }, { "layer", 0.8f }, { "split", 1.0f }, { "split_hz", 140 }, { "cutoff", 0.3f }, { "res", 0.75f }, { "env_amt", 0.6f }, { "accent", 0.7f } } },
        { "ACID/WAREHOUSE",       { { "model", 3 }, { "cutoff", 0.32f }, { "res", 0.82f }, { "env_amt", 0.65f }, { "accent", 0.85f }, { "decay", 0.35f }, { "drive", 0.35f } } },
        { "ROLLING/ANALOG DRIFT", { { "model", 0 }, { "cutoff", 0.3f }, { "res", 0.3f }, { "env_amt", 0.4f }, { "gate", 0.35f }, { "drift", 0.6f } } },
        // FM 4-OP: ratio index 0 0.5, 1 1, 3 2, 5 3, 6 3.5, 10 7, 16 14; algorithm 3 2 STACKS, 4 1 TO 3, 6 BELL.
        { "FM/4-OP SUB PLUCK",    { { "model", 8 }, { "fm_algo", 3 }, { "fm_a_ratio", 1 }, { "fm_a_level", 1.0f }, { "fm_a_decay", 0.8f }, { "fm_b_ratio", 1 }, { "fm_b_level", 0.6f }, { "fm_b_decay", 0.3f },
                                    { "fm_c_ratio", 0 }, { "fm_c_level", 0.7f }, { "fm_c_decay", 1.0f }, { "fm_d_ratio", 1 }, { "fm_d_level", 0.0f }, { "fm_d_decay", 0.5f }, { "cutoff", 0.5f } } },
    };
}

std::vector<Preset> lead()
{
    return {
        { "ACID/LINE",            { { "model", 0 }, { "cutoff", 0.35f }, { "res", 0.75f }, { "env_amt", 0.6f }, { "accent", 0.7f } } },
        { "ACID/RESONANT",        { { "model", 0 }, { "cutoff", 0.5f }, { "res", 0.9f }, { "env_amt", 0.7f }, { "drive", 0.3f } } },
        { "ACID/WANDERING",       Values { { "model", 0 }, { "cutoff", 0.38f }, { "res", 0.8f }, { "env_amt", 0.6f } } + mot ("LEAD", 1, 7, 6, "cutoff", 0.2f) },
        { "ACID/WAREHOUSE",       { { "model", 0 }, { "cutoff", 0.3f }, { "res", 0.85f }, { "env_amt", 0.7f }, { "accent", 0.9f }, { "decay", 0.3f }, { "drive", 0.4f } } },
        { "FM/4-OP TINE KEYS",    { { "model", 10 }, { "fm_algo", 3 }, { "fm_a_ratio", 1 }, { "fm_a_level", 1.0f }, { "fm_a_decay", 0.7f }, { "fm_b_ratio", 16 }, { "fm_b_level", 0.35f }, { "fm_b_decay", 0.2f },
                                    { "fm_c_ratio", 1 }, { "fm_c_level", 0.8f }, { "fm_c_decay", 0.75f }, { "fm_d_ratio", 1 }, { "fm_d_level", 0.3f }, { "fm_d_decay", 0.5f }, { "fm_feedback", 0.1f } } },
        { "FM/4-OP DUB BELL STAB", { { "model", 10 }, { "fm_algo", 6 }, { "fm_a_ratio", 1 }, { "fm_a_level", 1.0f }, { "fm_a_decay", 0.6f }, { "fm_b_ratio", 6 }, { "fm_b_level", 0.6f }, { "fm_b_decay", 0.5f },
                                    { "fm_c_ratio", 3 }, { "fm_c_level", 0.5f }, { "fm_c_decay", 0.4f }, { "fm_d_ratio", 10 }, { "fm_d_level", 0.3f }, { "fm_d_decay", 0.2f }, { "density", 0.35f }, { "echo_space", 5 } } },
        { "MONO/101 LEAD",        { { "model", 1 }, { "cutoff", 0.5f }, { "res", 0.35f }, { "env_amt", 0.4f }, { "glide", 0.3f } } },
        { "MONO/MS20 SCREAM",     { { "model", 2 }, { "cutoff", 0.45f }, { "res", 0.6f }, { "env_amt", 0.5f }, { "drive", 0.3f } } },
        { "STAB/DUB CHORD",       { { "model", 3 }, { "cutoff", 0.4f }, { "res", 0.3f }, { "env_amt", 0.5f }, { "space", 0.6f } } },
        { "STAB/DUB DARK",        { { "model", 3 }, { "cutoff", 0.28f }, { "res", 0.25f }, { "env_amt", 0.35f }, { "space", 0.7f } } },
        { "STAB/DUB ECHO 1/8D",   { { "model", 3 }, { "cutoff", 0.4f }, { "density", 0.45f }, { "echo_space", 5 } } },
        { "STAB/DUB CHORD DRIFT", { { "model", 3 }, { "cutoff", 0.4f }, { "res", 0.3f }, { "env_amt", 0.5f }, { "space", 0.7f }, { "drift", 0.5f }, { "density", 0.35f }, { "echo_space", 5 } } },
        { "STAB/DETROIT",         { { "model", 4 }, { "cutoff", 0.5f }, { "res", 0.25f }, { "env_amt", 0.4f }, { "space", 0.5f } } },
        { "STAB/DETROIT MIN9",    { { "model", 4 }, { "cutoff", 0.5f }, { "chord", 3 } } },
        { "ORGAN/M1 HOUSE",       { { "model", 5 }, { "color", 0.6f } } },
        { "ORGAN/DARK",           { { "model", 5 }, { "color", 0.3f }, { "filter", 0.42f } } },
        { "HOOVER/RAVE",          { { "model", 6 }, { "cutoff", 0.6f }, { "detune", 0.6f } } },
        { "FM/BELL",              { { "model", 7 }, { "color", 0.45f } } },
        { "SYNC/SWEEP",           { { "model", 8 }, { "cutoff", 0.55f }, { "env_amt", 0.6f }, { "color", 0.4f } } },
    };
}

std::vector<Preset> pad()
{
    return {
        { "STRINGS/WARM",         { { "model", 0 }, { "cutoff", 0.5f } } },
        { "STRINGS/DARK",         { { "model", 0 }, { "cutoff", 0.35f }, { "filter", 0.45f } } },
        { "STRINGS/ANALOG DRIFT", { { "model", 0 }, { "cutoff", 0.45f }, { "drift", 0.7f } } },
        { "POLY/SOFT",            { { "model", 1 }, { "cutoff", 0.45f } } },
        { "POLY/SLOW BLOOM",      Values { { "model", 1 }, { "attack", 0.6f }, { "cutoff", 0.4f } } + mot ("PAD", 1, 7, 7, "cutoff", 0.15f) },
        { "WAVETABLE/VOWEL",      { { "model", 2 }, { "color", 0.7f } } },
        { "WAVETABLE/DRIFT",      Values { { "model", 2 }, { "color", 0.5f } } + mot ("PAD", 1, 7, 6, "color", 0.3f) },
        { "GRANULAR/CLOUD",       { { "model", 3 }, { "color", 0.6f }, { "detune", 0.7f } } },
        { "DUB/WASH",             { { "model", 4 }, { "cutoff", 0.45f }, { "space", 0.7f } } },
        { "DUB/CHORD MIN7",       { { "model", 4 }, { "chord", 2 }, { "cutoff", 0.4f } } },
        { "DUB/DRIFTING MIN7",    { { "model", 4 }, { "chord", 2 }, { "cutoff", 0.4f }, { "space", 0.7f }, { "drift", 0.6f } } },
        { "FM/4-OP GLASS",        { { "model", 8 }, { "fm_algo", 4 }, { "fm_a_ratio", 1 }, { "fm_a_level", 1.0f }, { "fm_a_decay", 1.0f }, { "fm_b_ratio", 3 }, { "fm_b_level", 0.6f }, { "fm_b_decay", 1.0f },
                                    { "fm_c_ratio", 5 }, { "fm_c_level", 0.4f }, { "fm_c_decay", 1.0f }, { "fm_d_ratio", 1 }, { "fm_d_level", 0.3f }, { "fm_d_decay", 0.6f }, { "color", 0.3f }, { "space", 0.6f } } },
        { "TAPE/LOOP",            { { "model", 5 }, { "cutoff", 0.5f } } },
        { "TAPE/WORN",            { { "model", 5 }, { "cutoff", 0.4f }, { "drive", 0.25f } } },
        { "NOISE/BED",            { { "model", 6 }, { "color", 0.4f }, { "cutoff", 0.5f } } },
        { "NOISE/BREATHING BED",  Values { { "model", 6 }, { "color", 0.4f }, { "cutoff", 0.45f } } + mot ("PAD", 1, 1, 8, "cutoff", 0.15f) },
    };
}

std::vector<Preset> voice()
{
    return {
        { "PHRASE/CLEAN",         { { "model", 0 }, { "echo", 0.15f } } },
        { "PHRASE/DUB ECHO",      { { "model", 0 }, { "echo", 0.45f }, { "echo_fb", 0.65f } } },
        { "RADIO/TRANSMISSION",   { { "model", 1 }, { "signal", 0.4f } } },
        { "VOCODER/DARK",         { { "model", 2 }, { "tone", 0.3f } } },
        { "ROBOT/8-BIT",          { { "model", 3 }, { "tone", 0.5f } } },
        { "STUTTER/1/16",         { { "model", 5 }, { "tone", 0.6f }, { "density", 0.3f } } },
    };
}

std::vector<Preset> strike()
{
    return {
        { "BELL/CHURCH",          { { "model", 0 }, { "size", 0.75f }, { "material", 0.65f }, { "decay", 0.75f }, { "power", 0.45f }, { "distance", 0.4f } } },
        { "BELL/SMALL",           { { "model", 0 }, { "size", 0.35f }, { "material", 0.7f }, { "decay", 0.55f } } },
        { "BELL/DISTANT",         { { "model", 0 }, { "size", 0.7f }, { "distance", 0.8f }, { "decay", 0.8f } } },
        { "PLATE/STEEL",          { { "model", 1 }, { "size", 0.5f }, { "material", 0.75f }, { "decay", 0.6f } } },
        { "PLATE/HIT",            { { "model", 1 }, { "size", 0.4f }, { "power", 0.6f }, { "decay", 0.4f } } },
        { "BAR/WOOD",             { { "model", 2 }, { "size", 0.4f }, { "material", 0.15f }, { "decay", 0.35f } } },
        { "BAR/METAL",            { { "model", 2 }, { "size", 0.45f }, { "material", 0.8f }, { "decay", 0.6f } } },
        { "TUBE/CHIME",           { { "model", 3 }, { "size", 0.35f }, { "material", 0.75f }, { "decay", 0.75f } } },
        { "MEMBRANE/TOM",         { { "model", 4 }, { "size", 0.6f }, { "material", 0.3f }, { "decay", 0.35f }, { "tension", 0.4f } } },
        { "MEMBRANE/DEEP DRUM",   { { "model", 4 }, { "size", 0.85f }, { "decay", 0.5f }, { "tension", 0.6f }, { "power", 0.55f } } },
        { "GONG/TAM",             { { "model", 5 }, { "size", 0.8f }, { "decay", 0.85f }, { "tension", 0.6f }, { "power", 0.5f } } },
        { "BLAST/FRACTURE",       { { "model", 1 }, { "power", 0.9f }, { "distance", 0.2f }, { "decay", 0.5f } } },
        { "BLAST/FAR THUNDER",    { { "model", 5 }, { "power", 0.85f }, { "distance", 0.9f }, { "size", 0.9f }, { "decay", 0.8f } } },
    };
}

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
    return {
        { "BUS/GENTLE",           { { "push", 0.25f }, { "attack", 3 }, { "release", 4 }, { "ratio", 0 } } },
        { "BUS/PUNCH",            { { "push", 0.35f }, { "attack", 4 }, { "release", 1 }, { "ratio", 1 } } },
        { "DRUMS/SMASH",          { { "push", 0.6f }, { "attack", 1 }, { "release", 1 }, { "ratio", 2 }, { "mix", 0.5f } } },
        { "MASTER/GLUE",          { { "push", 0.2f }, { "attack", 4 }, { "release", 4 }, { "ratio", 0 }, { "sc_hpf", 120.0f } } },
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
    if (id == "KICK") return kick();
    if (id == "PERC") return perc();
    if (id == "RUMBLE") return rumble();
    if (id == "BASS") return bass();
    if (id == "LEAD") return lead();
    if (id == "PAD") return pad();
    if (id == "VOICE") return voice();
    if (id == "STRIKE") return strike();
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
