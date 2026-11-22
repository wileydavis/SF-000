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

// A model with sound knobs of its own: while it is the lane's MODEL, the lane's eight sound places are these instead of
// the lane's usual ones (the last four places, DECAY or GATE, DENSITY, FILTER and LEVEL, are every model's). A place keeps
// its control id (snapshots are stored by those), so a snapshot's value at a place means what its lane's model made of it.
// What the usual knobs moved is then in CONFIG, where it stays as it was left.
constexpr int kSoundKnobs = 8;
// with: what choosing the model by hand also sets, each a place from 0 to 1 (KICK FORGE is a whole kick: the 808 laid under
// the rig's kick, and the crossover that takes the model's own lows out for it, are put away. They are in CONFIG.)
struct ModelWith { const char* param; float place; };
// plate: the colour of the lane's faceplate while the model is on, on the dark instrument (0: the lane's usual). KICK
// FORGE's is a dark red: the same worn anodized plate, dyed red where the others are black.
struct ModelKnobs { const char* model; KnobDef knobs[kSoundKnobs]; std::vector<ModelWith> with; unsigned int plate = 0; };

struct LaneDef
{
    const char* name;       // on the panel
    const char* slug;       // in control ids: snapshots are stored by these, so they never change
    const char* module;     // the suite module that plays
    bool pitched;           // steps hold notes
    int octave;             // a new step's octave (C3 = MIDI 60)
    std::vector<Fixed> fixed;
    KnobDef knobs[kLaneKnobs];
    std::vector<ModelKnobs> own;    // the models with sound knobs of their own (none: every model has the lane's)
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
    // (SF-000, the hundred and third build) Every lane plays one instrument (suite/modules/Lane.cpp) whose MODEL and LAYER
    // MODEL are each SAMPLE, SYNTH or KICK FORGE, and nothing else (Wiley, Nov 21: "every lane gets the three models and
    // nothing else"). Each has its own eight knobs, the same on every lane; the last four places are the lane's: DECAY (a
    // drum lane) or GATE (a lane that plays notes), DENSITY, FILTER, LEVEL. A lane that opens on SYNTH opens on its own
    // patch (its values below, as PATCH chosen by hand puts them); the first eight places' ids are SYNTH's, which no model
    // ever leaves standing (every model has its own).
    static const std::vector<LaneDef> L = {
        { "KICK", "kick", "KICK", false, 3,
          { { "model", 2 }, { "fg_tune", -1.0f }, { "fg_punch", 0.45f }, { "fg_click", 0.35f }, { "fg_drive", 0.12f }, { "level", -3 } },
          { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
            { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' },
            { "DECAY", "decay", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } },
          { { "SAMPLE", { { "TUNE", "sp_tune", 'b' }, { "POSITION", "sp_pos", 'u' }, { "SIZE", "sp_size", 'u' }, { "SCAN", "sp_scan", 'b' },
                          { "SPREAD", "sp_spread", 'u' }, { "ATTACK", "sp_attack", 'u' }, { "RELEASE", "sp_release", 'u' }, { "DRIVE", "sp_drive", 'u' } },
              {} },
            { "SYNTH", { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
                         { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' } },
              {} },
            { "KICK FORGE", { { "TUNE", "fg_tune", 'b' }, { "CHARACTER", "fg_char", 'u' }, { "IMPACT", "fg_impact", 'u' }, { "BODY", "fg_body", 'u' },
                              { "GROUND", "fg_ground", 'u' }, { "POSITION", "fg_pos", 'u' }, { "TONE", "fg_tone", 'u' }, { "DRIVE", "fg_drive", 'u' } },
              {}, 0xff5A1418u } } },

        { "CLOSED HAT", "closed_hat", "PERC", false, 3,
          { { "model", 1 }, { "sy_patch", 21.0f }, { "sy_noise", 1.0f }, { "sy_color", 0.85f }, { "sy_circuit", 2.0f }, { "sy_res", 0.15f }, { "sy_envamt", 0.0f }, { "sy_keytrack", 0.0f }, { "sy_attack", 0.5f }, { "sy_sustain", 0.0f }, { "sy_release", 30.0f }, { "sy_voices", 4.0f }, { "level", -13 } },
          { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
            { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' },
            { "DECAY", "decay", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } },
          { { "SAMPLE", { { "TUNE", "sp_tune", 'b' }, { "POSITION", "sp_pos", 'u' }, { "SIZE", "sp_size", 'u' }, { "SCAN", "sp_scan", 'b' },
                          { "SPREAD", "sp_spread", 'u' }, { "ATTACK", "sp_attack", 'u' }, { "RELEASE", "sp_release", 'u' }, { "DRIVE", "sp_drive", 'u' } },
              {} },
            { "SYNTH", { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
                         { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' } },
              {} },
            { "KICK FORGE", { { "TUNE", "fg_tune", 'b' }, { "CHARACTER", "fg_char", 'u' }, { "IMPACT", "fg_impact", 'u' }, { "BODY", "fg_body", 'u' },
                              { "GROUND", "fg_ground", 'u' }, { "POSITION", "fg_pos", 'u' }, { "TONE", "fg_tone", 'u' }, { "DRIVE", "fg_drive", 'u' } },
              {}, 0xff5A1418u } } },

        { "OPEN HAT", "open_hat", "PERC", false, 3,
          { { "model", 1 }, { "sy_patch", 24.0f }, { "sy_noise", 1.0f }, { "sy_color", 0.85f }, { "sy_circuit", 2.0f }, { "sy_res", 0.15f }, { "sy_envamt", 0.0f }, { "sy_keytrack", 0.0f }, { "sy_attack", 0.5f }, { "sy_sustain", 0.0f }, { "sy_release", 120.0f }, { "sy_voices", 4.0f }, { "level", -15 } },
          { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
            { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' },
            { "DECAY", "decay", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } },
          { { "SAMPLE", { { "TUNE", "sp_tune", 'b' }, { "POSITION", "sp_pos", 'u' }, { "SIZE", "sp_size", 'u' }, { "SCAN", "sp_scan", 'b' },
                          { "SPREAD", "sp_spread", 'u' }, { "ATTACK", "sp_attack", 'u' }, { "RELEASE", "sp_release", 'u' }, { "DRIVE", "sp_drive", 'u' } },
              {} },
            { "SYNTH", { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
                         { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' } },
              {} },
            { "KICK FORGE", { { "TUNE", "fg_tune", 'b' }, { "CHARACTER", "fg_char", 'u' }, { "IMPACT", "fg_impact", 'u' }, { "BODY", "fg_body", 'u' },
                              { "GROUND", "fg_ground", 'u' }, { "POSITION", "fg_pos", 'u' }, { "TONE", "fg_tone", 'u' }, { "DRIVE", "fg_drive", 'u' } },
              {}, 0xff5A1418u } } },

        { "CLAP", "clap", "PERC", false, 3,
          { { "model", 1 }, { "sy_patch", 27.0f }, { "sy_noise", 1.0f }, { "sy_color", 0.0f }, { "sy_circuit", 1.0f }, { "sy_res", 0.25f }, { "sy_envamt", 0.0f }, { "sy_keytrack", 0.0f }, { "sy_attack", 0.5f }, { "sy_sustain", 0.0f }, { "sy_release", 60.0f }, { "sy_bursts", 3.0f }, { "sy_burst_gap", 11.0f }, { "sy_voices", 4.0f }, { "level", -11 } },
          { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
            { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' },
            { "DECAY", "decay", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } },
          { { "SAMPLE", { { "TUNE", "sp_tune", 'b' }, { "POSITION", "sp_pos", 'u' }, { "SIZE", "sp_size", 'u' }, { "SCAN", "sp_scan", 'b' },
                          { "SPREAD", "sp_spread", 'u' }, { "ATTACK", "sp_attack", 'u' }, { "RELEASE", "sp_release", 'u' }, { "DRIVE", "sp_drive", 'u' } },
              {} },
            { "SYNTH", { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
                         { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' } },
              {} },
            { "KICK FORGE", { { "TUNE", "fg_tune", 'b' }, { "CHARACTER", "fg_char", 'u' }, { "IMPACT", "fg_impact", 'u' }, { "BODY", "fg_body", 'u' },
                              { "GROUND", "fg_ground", 'u' }, { "POSITION", "fg_pos", 'u' }, { "TONE", "fg_tone", 'u' }, { "DRIVE", "fg_drive", 'u' } },
              {}, 0xff5A1418u } } },

        { "PERC", "perc", "STRIKE", false, 3,
          { { "model", 1 }, { "sy_patch", 32.0f }, { "sy_shape", 0.0f }, { "sy_noise", 0.5f }, { "sy_color", 0.5f }, { "sy_circuit", 0.0f }, { "sy_res", 0.0f }, { "sy_envamt", 0.0f }, { "sy_attack", 0.5f }, { "sy_sustain", 0.0f }, { "sy_release", 10.0f }, { "sy_body", 1.0f }, { "sy_body_mix", 0.9f }, { "sy_body_decay", 0.45f }, { "sy_body_tone", 0.45f }, { "sy_voices", 4.0f }, { "sy_phase", 1.0f }, { "level", -14 } },
          { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
            { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' },
            { "DECAY", "decay", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } },
          { { "SAMPLE", { { "TUNE", "sp_tune", 'b' }, { "POSITION", "sp_pos", 'u' }, { "SIZE", "sp_size", 'u' }, { "SCAN", "sp_scan", 'b' },
                          { "SPREAD", "sp_spread", 'u' }, { "ATTACK", "sp_attack", 'u' }, { "RELEASE", "sp_release", 'u' }, { "DRIVE", "sp_drive", 'u' } },
              {} },
            { "SYNTH", { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
                         { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' } },
              {} },
            { "KICK FORGE", { { "TUNE", "fg_tune", 'b' }, { "CHARACTER", "fg_char", 'u' }, { "IMPACT", "fg_impact", 'u' }, { "BODY", "fg_body", 'u' },
                              { "GROUND", "fg_ground", 'u' }, { "POSITION", "fg_pos", 'u' }, { "TONE", "fg_tone", 'u' }, { "DRIVE", "fg_drive", 'u' } },
              {}, 0xff5A1418u } } },

        { "BASS", "bass", "BASS", true, 1,
          { { "model", 1 }, { "sy_patch", 1.0f }, { "sy_shape", 0.5f }, { "sy_shape_b", 0.5f }, { "sy_mix", 0.44f }, { "sy_fine_b", 8.0f }, { "sy_sub", 0.4f }, { "sy_circuit", 3.0f }, { "sy_cutoff", 0.38f }, { "sy_res", 0.25f }, { "sy_envamt", 0.4f }, { "sy_sustain", 0.8f }, { "sy_release", 80.0f }, { "sy_voices", 1.0f }, { "sy_phase", 1.0f }, { "sy_drift", 0.05f }, { "level", -7 } },
          { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
            { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' },
            { "GATE", "gate", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } },
          { { "SAMPLE", { { "TUNE", "sp_tune", 'b' }, { "POSITION", "sp_pos", 'u' }, { "SIZE", "sp_size", 'u' }, { "SCAN", "sp_scan", 'b' },
                          { "SPREAD", "sp_spread", 'u' }, { "ATTACK", "sp_attack", 'u' }, { "RELEASE", "sp_release", 'u' }, { "DRIVE", "sp_drive", 'u' } },
              {} },
            { "SYNTH", { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
                         { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' } },
              {} },
            { "KICK FORGE", { { "TUNE", "fg_tune", 'b' }, { "CHARACTER", "fg_char", 'u' }, { "IMPACT", "fg_impact", 'u' }, { "BODY", "fg_body", 'u' },
                              { "GROUND", "fg_ground", 'u' }, { "POSITION", "fg_pos", 'u' }, { "TONE", "fg_tone", 'u' }, { "DRIVE", "fg_drive", 'u' } },
              {}, 0xff5A1418u } } },

        { "RUMBLE", "rumble", "RUMBLE", false, 3,
          { { "model", 2 }, { "fg_ground", 0.9f }, { "fg_glen", 0.85f }, { "fg_gdark", 0.7f }, { "fg_impact", 0.1f }, { "fg_char", 0.15f }, { "level", -10 } },
          { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
            { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' },
            { "DECAY", "decay", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } },
          { { "SAMPLE", { { "TUNE", "sp_tune", 'b' }, { "POSITION", "sp_pos", 'u' }, { "SIZE", "sp_size", 'u' }, { "SCAN", "sp_scan", 'b' },
                          { "SPREAD", "sp_spread", 'u' }, { "ATTACK", "sp_attack", 'u' }, { "RELEASE", "sp_release", 'u' }, { "DRIVE", "sp_drive", 'u' } },
              {} },
            { "SYNTH", { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
                         { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' } },
              {} },
            { "KICK FORGE", { { "TUNE", "fg_tune", 'b' }, { "CHARACTER", "fg_char", 'u' }, { "IMPACT", "fg_impact", 'u' }, { "BODY", "fg_body", 'u' },
                              { "GROUND", "fg_ground", 'u' }, { "POSITION", "fg_pos", 'u' }, { "TONE", "fg_tone", 'u' }, { "DRIVE", "fg_drive", 'u' } },
              {}, 0xff5A1418u } } },

        { "LEAD", "lead", "LEAD", true, 3,
          { { "model", 1 }, { "sy_patch", 6.0f }, { "sy_shape", 0.5f }, { "sy_circuit", 4.0f }, { "sy_cutoff", 0.35f }, { "sy_res", 0.75f }, { "sy_envamt", 0.6f }, { "sy_accent", 0.7f }, { "sy_attack", 3.0f }, { "sy_sustain", 1.0f }, { "sy_release", 8.0f }, { "sy_voices", 1.0f }, { "sy_slide", 0.22f }, { "sy_space", 0.1f }, { "level", -12 } },
          { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
            { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' },
            { "GATE", "gate", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } },
          { { "SAMPLE", { { "TUNE", "sp_tune", 'b' }, { "POSITION", "sp_pos", 'u' }, { "SIZE", "sp_size", 'u' }, { "SCAN", "sp_scan", 'b' },
                          { "SPREAD", "sp_spread", 'u' }, { "ATTACK", "sp_attack", 'u' }, { "RELEASE", "sp_release", 'u' }, { "DRIVE", "sp_drive", 'u' } },
              {} },
            { "SYNTH", { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
                         { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' } },
              {} },
            { "KICK FORGE", { { "TUNE", "fg_tune", 'b' }, { "CHARACTER", "fg_char", 'u' }, { "IMPACT", "fg_impact", 'u' }, { "BODY", "fg_body", 'u' },
                              { "GROUND", "fg_ground", 'u' }, { "POSITION", "fg_pos", 'u' }, { "TONE", "fg_tone", 'u' }, { "DRIVE", "fg_drive", 'u' } },
              {}, 0xff5A1418u } } },

        { "CHORD", "chord", "LEAD", true, 3,
          { { "model", 1 }, { "sy_patch", 13.0f }, { "sy_shape", 0.5f }, { "sy_shape_b", 0.75f }, { "sy_mix", 0.33f }, { "sy_fine_b", 6.0f }, { "sy_circuit", 0.0f }, { "sy_cutoff", 0.45f }, { "sy_res", 0.25f }, { "sy_envamt", 0.4f }, { "sy_chord", 3.0f }, { "sy_sustain", 0.0f }, { "sy_release", 150.0f }, { "sy_voices", 12.0f }, { "sy_space", 0.15f }, { "level", -15 } },
          { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
            { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' },
            { "GATE", "gate", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } },
          { { "SAMPLE", { { "TUNE", "sp_tune", 'b' }, { "POSITION", "sp_pos", 'u' }, { "SIZE", "sp_size", 'u' }, { "SCAN", "sp_scan", 'b' },
                          { "SPREAD", "sp_spread", 'u' }, { "ATTACK", "sp_attack", 'u' }, { "RELEASE", "sp_release", 'u' }, { "DRIVE", "sp_drive", 'u' } },
              {} },
            { "SYNTH", { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
                         { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' } },
              {} },
            { "KICK FORGE", { { "TUNE", "fg_tune", 'b' }, { "CHARACTER", "fg_char", 'u' }, { "IMPACT", "fg_impact", 'u' }, { "BODY", "fg_body", 'u' },
                              { "GROUND", "fg_ground", 'u' }, { "POSITION", "fg_pos", 'u' }, { "TONE", "fg_tone", 'u' }, { "DRIVE", "fg_drive", 'u' } },
              {}, 0xff5A1418u } } },

        { "PAD", "pad", "PAD", true, 3,
          { { "model", 1 }, { "sy_patch", 18.0f }, { "sy_shape", 0.5f }, { "sy_shape_b", 0.75f }, { "sy_mix", 0.28f }, { "sy_fine_b", 5.0f }, { "sy_circuit", 0.0f }, { "sy_cutoff", 0.45f }, { "sy_res", 0.2f }, { "sy_envamt", 0.25f }, { "sy_chord", 3.0f }, { "sy_attack", 50.0f }, { "sy_sustain", 0.6f }, { "sy_release", 1500.0f }, { "sy_voices", 16.0f }, { "sy_space", 0.25f }, { "level", -15 } },
          { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
            { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' },
            { "GATE", "gate", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } },
          { { "SAMPLE", { { "TUNE", "sp_tune", 'b' }, { "POSITION", "sp_pos", 'u' }, { "SIZE", "sp_size", 'u' }, { "SCAN", "sp_scan", 'b' },
                          { "SPREAD", "sp_spread", 'u' }, { "ATTACK", "sp_attack", 'u' }, { "RELEASE", "sp_release", 'u' }, { "DRIVE", "sp_drive", 'u' } },
              {} },
            { "SYNTH", { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
                         { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' } },
              {} },
            { "KICK FORGE", { { "TUNE", "fg_tune", 'b' }, { "CHARACTER", "fg_char", 'u' }, { "IMPACT", "fg_impact", 'u' }, { "BODY", "fg_body", 'u' },
                              { "GROUND", "fg_ground", 'u' }, { "POSITION", "fg_pos", 'u' }, { "TONE", "fg_tone", 'u' }, { "DRIVE", "fg_drive", 'u' } },
              {}, 0xff5A1418u } } },

        { "VOICE", "voice", "VOICE", true, 3,
          { { "model", 0 }, { "level", -14 } },
          { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
            { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' },
            { "GATE", "gate", 'u' }, { "DENSITY", "density", 'u' }, { "FILTER", "filter", 'b' }, { "LEVEL", "level", 'u' } },
          { { "SAMPLE", { { "TUNE", "sp_tune", 'b' }, { "POSITION", "sp_pos", 'u' }, { "SIZE", "sp_size", 'u' }, { "SCAN", "sp_scan", 'b' },
                          { "SPREAD", "sp_spread", 'u' }, { "ATTACK", "sp_attack", 'u' }, { "RELEASE", "sp_release", 'u' }, { "DRIVE", "sp_drive", 'u' } },
              {} },
            { "SYNTH", { { "TUNE", "sy_tune", 'b' }, { "SHAPE", "sy_shape", 'u' }, { "CUTOFF", "sy_cutoff", 'u' }, { "RES", "sy_res", 'u' },
                         { "ENV AMT", "sy_envamt", 'u' }, { "ENV DECAY", "sy_decay", 'u' }, { "ACCENT", "sy_accent", 'u' }, { "DRIVE", "sy_drive", 'u' } },
              {} },
            { "KICK FORGE", { { "TUNE", "fg_tune", 'b' }, { "CHARACTER", "fg_char", 'u' }, { "IMPACT", "fg_impact", 'u' }, { "BODY", "fg_body", 'u' },
                              { "GROUND", "fg_ground", 'u' }, { "POSITION", "fg_pos", 'u' }, { "TONE", "fg_tone", 'u' }, { "DRIVE", "fg_drive", 'u' } },
              {}, 0xff5A1418u } } }

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
          // (the ninetieth build: the compressor made again. THRESHOLD turns up for more, from out at 0 dB down to -60; the
          // knobs are where the old one's were, with MAKEUP in the place that was empty)
          { { "THRESHOLD", "threshold", 'u', 1.0f, 0.0f }, { "ATTACK", "attack", 'u' }, { "RELEASE", "release", 'u' }, { "RATIO", "ratio", 'm' },
            { "SC HPF", "sc_hpf", 'u' }, { "MIX", "mix", 'u' }, { "LEVEL", "level", 'u' }, { "MAKEUP", "makeup", 'u' } } },
        { "RADIO", "RADIO", {},
          { { "SIGNAL", "signal", 'u' }, { "BAND", "band", 'u' }, { "WIDTH", "width", 'u' }, { "NOISE", "noise", 'u' },
            { "DROPOUT", "dropout", 'u' }, { "SQUELCH", "squelch", 'u' }, { "RING", "ring", 'u' }, { "MIX", "mix", 'u' } } },
        // (the ninety-first build) Six of Airwindows' (suite/modules/Airwindows.cpp). On all six the last three places
        // are the same three: OUTPUT (the effect's own, before MIX: how loud the effect is against the sound as it
        // came in; CLIP has none, and the place is empty on it), MIX and LEVEL. A file names an insert by its name,
        // so these are added at the end.
        { "WEAVE", "AIRWEAVE", {},
          { { "DRIVE", "drive", 'u' }, { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' },
            { nullptr, nullptr, 'u' }, { "OUTPUT", "output", 'u' }, { "MIX", "mix", 'u' }, { "LEVEL", "level", 'u' } } },
        { "DEREZ", "AIRDEREZ", {},
          { { "RATE", "rate", 'u' }, { "SQUARE", "square", 'u' }, { "TRIM", "trim", 'u' }, { "BRIGHT", "bright", 'u' },
            { "BASSIS", "bassis", 'u' }, { "OUTPUT", "output", 'u' }, { "MIX", "mix", 'u' }, { "LEVEL", "level", 'u' } } },
        { "FILTER", "AIRFILTER", {},
          { { "FREQ", "freq", 'u' }, { "RESO", "reso", 'u' }, { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' },
            { nullptr, nullptr, 'u' }, { "OUTPUT", "output", 'u' }, { "MIX", "mix", 'u' }, { "LEVEL", "level", 'u' } } },
        { "ENSEMBLE", "AIRENSEMBLE", {},
          { { "SPEED", "speed", 'u' }, { "DEPTH", "depth", 'u' }, { "BUFFER", "buffer", 'u' }, { "REGEN", "regen", 'u' },
            { "DEREZ", "derez", 'b' }, { "OUTPUT", "output", 'u' }, { "MIX", "mix", 'u' }, { "LEVEL", "level", 'u' } } },
        { "TAPE", "AIRTAPE", {},
          { { "INPUT", "input", 'b' }, { "TILT", "tilt", 'b' }, { "SHAPE", "shape", 'b' }, { "FLUTTER", "flutter", 'u' },
            { "HEAD BUMP", "head_bump", 'u' }, { "OUTPUT", "output", 'b' }, { "MIX", "mix", 'u' }, { "LEVEL", "level", 'u' } } },
        { "CLIP", "AIRCLIP", {},
          { { "BOOST", "boost", 'u' }, { "CEILING", "ceiling", 'u' }, { "NOISE", "noise", 'u' }, { nullptr, nullptr, 'u' },
            { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' }, { "MIX", "mix", 'u' }, { "LEVEL", "level", 'u' } } },
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
// B is one of three reverbs, chosen in its CONFIG: 0 the suite's REVERB, 1 its convolution reverb
// (TERROIR: a place, from an impulse response dropped on it), 2 GALAXY (the ninety-first build: Airwindows'
// Galactic3, suite/modules/Airwindows.cpp). Each has its own four knobs, and the like ones are in the same
// places on all three: DECAY second, DARK third, DUCK fourth.
constexpr int kSpaceModels = 3;
inline const char* spaceModelName (int model) { return model == 2 ? "GALAXY" : model == 1 ? "CONVOLUTION" : "REVERB"; }
// (what a model's four controls are called in a file: "ret.<this>_<param>")
inline const char* spaceModelSlug (int model) { return model == 2 ? "galaxy" : model == 1 ? "conv" : "space"; }
inline const FxDef& returnB (int model = 0)
{
    static const FxDef d[kSpaceModels] = {
        { "SPACE", "REVERB", { { "mix", 1.0f }, { "dry", 0 }, { "size", 0.65f }, { "decay", 3.5f }, { "dark", 0.55f }, { "duck", 0.4f }, { "lo_cut", 220 } },
          { { "SPACE SIZE", "size", 'u' }, { "SPACE DECAY", "decay", 'u' }, { "SPACE DARK", "dark", 'u' }, { "SPACE DUCK", "duck", 'u' },
            { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' } } },
        // the place as it was captured (SIZE and DECAY at 100 %), ducked and cut like the reverb it stands in for
        { "SPACE", "TERROIR", { { "mix", 1.0f }, { "dry", 0 }, { "duck", 0.4f }, { "lo_cut", 220 } },
          { { "SPACE SIZE", "size", 'u' }, { "SPACE DECAY", "decay", 'u' }, { "SPACE DARK", "dark", 'u' }, { "SPACE DUCK", "duck", 'u' },
            { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' } } },
        // GALAXY: ducked and cut like the other two. Its size (BIGNESS) is in CONFIG, as it cannot move while it sounds;
        // DETUNE, the drift that is its own, is where the others have SIZE
        { "SPACE", "AIRGALAXY", { { "mix", 1.0f }, { "duck", 0.4f }, { "lo_cut", 220 } },
          { { "SPACE DETUNE", "detune", 'u' }, { "SPACE DECAY", "decay", 'u' }, { "SPACE DARK", "dark", 'u' }, { "SPACE DUCK", "duck", 'u' },
            { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' }, { nullptr, nullptr, 'u' } } } };
    return d[model < 0 || model >= kSpaceModels ? 0 : model];
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

// POCKETS, in the master: who has which part of the spectrum (Pockets.h). Four knobs a lane (HOME,
// WIDTH, HOLD, YIELD) and four for the whole rig (AMOUNT, CLEAR, SPEED, RETURNS). Where each lane's
// pocket starts: its HOME in Hz, its WIDTH in octaves, how firmly it holds it and how much it gives.
// With AMOUNT at nought, where it starts, nothing is carved.
constexpr int kPkLane = 4, kPkGlobals = 4;
struct PocketDef { float homeHz, widthOct, hold, yield; };
inline const PocketDef& pocketDefault (int lane)
{
    static const PocketDef d[kLanes] = {
        // (each is where the lane's own sound, as the rig starts it, has most of itself)
        {   55.0f, 1.4f, 1.00f, 0.00f },    // KICK: the bottom is its own, and nothing is taken from it
        { 9000.0f, 1.6f, 0.55f, 0.40f },    // CLOSED HAT
        { 7500.0f, 2.0f, 0.50f, 0.40f },    // OPEN HAT
        { 1400.0f, 2.0f, 0.70f, 0.30f },    // CLAP
        {  220.0f, 1.6f, 0.50f, 0.50f },    // PERC: a struck membrane, low in the middle
        {   80.0f, 1.8f, 0.85f, 0.40f },    // BASS: over the kick's fundamental, and under everything else
        {   45.0f, 1.6f, 0.30f, 0.60f },    // RUMBLE: the floor, where the kick and the bass leave room
        {  900.0f, 2.4f, 0.75f, 0.40f },    // LEAD
        {  600.0f, 2.2f, 0.60f, 0.50f },    // CHORD
        {  350.0f, 2.6f, 0.40f, 0.60f },    // PAD: wide, and the first to give
        {  400.0f, 3.0f, 0.80f, 0.30f },    // VOICE
    };
    return d[lane];
}

// The master chain, built in: the lows made mono, tape, a bus compressor, a true-peak limiter that
// trims its own input until the loudest passage sits at the target loudness.
struct MasterStage { const char* module; std::vector<Fixed> fixed; KnobDef knob; };
inline const std::vector<MasterStage>& masterChain()
{
    static const std::vector<MasterStage> M = {
        { "LOWEND", { { "mono_freq", 150 }, { "rumble_cut", 25 } }, { nullptr, nullptr, 'u' } },
        { "DRIVE",  { { "character", 0.0f }, { "low_keep", 120 }, { "auto_gain", 1 }, { "drive", 0.12f } }, { "TAPE", "drive", 'u', 0.0f, 0.6f } },
        // (COMP is its THRESHOLD, from out at 0 dB down to -30: at rest the master is not compressed. The rest of it is in
        // its CONFIG, the C by the knob: Engine::Target kind 4.)
        { "GLUE",   { { "attack", 10.0f }, { "auto", 1 }, { "ratio", 2 }, { "sc_hpf", 110 }, { "knee", 6.0f }, { "threshold", 0.0f } }, { "COMP", "threshold", 'u', 1.0f, 0.5f } },
        // FINISH (SF-000's own, Finish.cpp): three bands held, a little harmonic, the sides opened. At nought, as it opens, it is
        // not there. It finds its own input (AUTO); INPUT is where that starts from: where the limiter's own does, less 3 dB.
        { "FINISH", { { "amount", 0.0f }, { "input", 2.0f }, { "auto", 1 } }, { nullptr, nullptr, 'u' } },
        { "LIMIT",  { { "auto", 1 }, { "auto_mode", 1 }, { "ceiling", -1.0f }, { "target", -9.0f }, { "clip", 0.3f }, { "input", 5.0f } }, { "LIMIT", "target", 'u' } },
    };
    return M;
}
} // namespace sf0
