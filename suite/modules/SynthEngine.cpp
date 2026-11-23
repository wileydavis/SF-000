// (SF-000, the hundred and second build) SYNTH: one polyphonic engine on every lane. See SynthEngine.h.
#include "SynthEngine.h"

namespace sf
{
namespace syn
{
std::vector<ParamSpec> params()
{
    std::vector<ParamSpec> v;
    auto add = [&v] (ParamSpec p) { v.push_back (p.noPreset()); };
    // the eight the main panel has (with the lane's last four: GATE or DECAY, DENSITY, FILTER, LEVEL)
    add (knob ("sy_tune", "TUNE", -24.0f, 24.0f, 0.0f, "st"));
    add (pct ("sy_shape", "SHAPE", 0.5f));
    add (pct ("sy_cutoff", "CUTOFF", 0.55f));
    add (pct ("sy_res", "RES", 0.2f));
    add (pct ("sy_envamt", "ENV AMT", 0.35f));
    add (pct ("sy_decay", "ENV DECAY", 0.35f));
    add (pct ("sy_accent", "ACCENT", 0.5f));
    add (pct ("sy_drive", "DRIVE", 0.0f));
    // OSCILLATORS
    add (choice ("sy_table_a", "TABLE A", tableNames(), 0));
    add (pct ("sy_warp", "WARP", 0.0f));
    add (choice ("sy_warp_as", "WARP AS", { "BEND", "SYNC", "FOLD", "PULSE" }, 0));
    add (choice ("sy_table_b", "TABLE B", tableNames(), 0));
    add (pct ("sy_shape_b", "SHAPE B", 0.5f));
    add (knob ("sy_pitch_b", "PITCH B", -24.0f, 24.0f, 0.0f, "st"));
    add (knob ("sy_fine_b", "FINE B", -50.0f, 50.0f, 0.0f, "ct"));
    add (pct ("sy_mix", "MIX", 0.0f));
    add (pct ("sy_fm", "FM", 0.0f));
    add (toggle ("sy_sync", "SYNC").next());        // (a struck sound that is ringing keeps how it began: the next note)
    add (pct ("sy_sub", "SUB", 0.0f));
    add (pct ("sy_noise", "NOISE", 0.0f));
    add (pct ("sy_color", "COLOR", 0.0f));
    add (choice ("sy_phase", "PHASE", { "FREE", "RESET" }, 0).next());
    // FILTER AND ENVELOPES
    add (choice ("sy_circuit", "CIRCUIT", { "CLEAN LP", "CLEAN BP", "CLEAN HP", "LADDER", "DIODE", "MS-20" }, 3));
    add (pct ("sy_keytrack", "KEY TRACK", 0.5f));
    add (knob ("sy_mattack", "MOD ATTACK", 0.0f, 2000.0f, 0.0f, "ms", 200.0f));
    add (knob ("sy_attack", "ATTACK", 0.5f, 4000.0f, 1.0f, "ms", 200.0f));
    add (pct ("sy_sustain", "SUSTAIN", 0.7f));
    add (knob ("sy_release", "RELEASE", 1.0f, 8000.0f, 120.0f, "ms", 400.0f));
    add (knob ("sy_pitch_env", "PITCH ENV", -48.0f, 48.0f, 0.0f, "st").next());
    add (knob ("sy_pitch_time", "PITCH TIME", 1.0f, 2000.0f, 60.0f, "ms", 150.0f));
    add (knob ("sy_bursts", "BURSTS", 1.0f, 4.0f, 1.0f, "").next().noGlide());
    add (knob ("sy_burst_gap", "BURST GAP", 3.0f, 40.0f, 10.0f, "ms").next());
    // VOICE AND BODY
    add (knob ("sy_voices", "VOICES", 1.0f, (float) kVoices, 8.0f, "").next().noGlide());
    add (pct ("sy_glide", "GLIDE", 0.0f).next());       // (how the next note comes in: a held note has nothing to glide to)
    add (pct ("sy_slide", "SLIDE", 0.3f));
    add (choice ("sy_chord", "CHORD", { "NONE", "MIN", "MAJ", "MIN7", "MAJ7", "MIN9", "SUS4", "5TH", "OCT" }, 0).next());
    add (knob ("sy_unison", "UNISON", 1.0f, (float) kUnison, 1.0f, "").noGlide());
    add (pct ("sy_detune", "DETUNE", 0.3f));
    add (pct ("sy_width", "WIDTH", 0.5f));
    add (pct ("sy_drift", "DRIFT", 0.1f));
    add (choice ("sy_body", "BODY", { "OFF", "MEMBRANE", "BAR", "PLATE", "TUBE", "BELL", "GONG" }, 0));
    add (pct ("sy_body_mix", "BODY MIX", 0.6f));
    add (pct ("sy_body_decay", "BODY DECAY", 0.5f));
    add (pct ("sy_body_tone", "BODY TONE", 0.5f));
    add (choice ("sy_patch", "PATCH", patchNames(), 0).next());
    add (pct ("sy_space", "SPACE", 0.0f));
    add (knob ("sy_gain", "GAIN", -24.0f, 36.0f, 0.0f, "dB"));      // (36 up: a struck object's patch, whose BODY rings from a strike a few hundredths of a second long, needs up to 30)
    // (SF-000, the hundred and second build: Wiley, Nov 21, "I want to use wave tables to mimic samples I can move
    // through") SCAN: each note moves along its tables from where SHAPE has it, as it sounds. The middle: it stays (SHAPE
    // and MOTION move it); right, forward, from a sixteenth of the recording's own pace to four times it; left, backward.
    // On SAMPLE and RECORDING the pace is the recording's (at 1x a note goes through it as it was spoken or played); on
    // the other tables one whole table a second at 1x. At an end it stays there. Last: appended, never moved.
    add (knob ("sy_scan", "SCAN", 0.0f, 1.0f, 0.5f, "BIP").ends ("BACK", "FWD"));      // (SF-000: no filter, its sides by name)
    // WAVE MOTION (Wiley, Nov 21: "By moving through the table at varying speeds or lingering in a section ... Similar to
    // the 3rd wave synth"): what SCAN does at an end; the frames crossfaded or stepped (the PPG's and the 3rd Wave's);
    // a wave envelope of four stages, each a time and a place along the tables, travelled from SHAPE (W DEPTH: how much
    // of it is heard; none, as it opens), looping its last stages if asked; and velocity and the key moving the place.
    add (choice ("sy_scan_end", "SCAN END", { "HOLD", "LOOP", "BOUNCE" }, 0));
    add (choice ("sy_steps", "STEPS", { "SMOOTH", "STEPPED" }, 0));
    add (pct ("sy_wdepth", "W DEPTH", 0.0f));
    for (int k = 1; k <= 4; ++k)
    {
        const std::string n = std::to_string (k), ti = "sy_wt" + n, tn = "W TIME " + n, pi = "sy_wp" + n, pn = "W PLACE " + n;
        add (knob (ti.c_str(), tn.c_str(), 1.0f, 20000.0f, 500.0f, "ms", 1000.0f));
        add (pct (pi.c_str(), pn.c_str(), 0.25f * (float) k));
    }
    add (choice ("sy_wloop", "W LOOP", { "OFF", "3-4", "2-4", "1-4" }, 0));
    add (knob ("sy_wvel", "W VEL", 0.0f, 1.0f, 0.5f, "BIP").ends ("-", "+"));      // (SF-000, the hundred and fourth build: heard at once, as it always was; a depth, signed)
    add (knob ("sy_wkey", "W KEY", 0.0f, 1.0f, 0.5f, "BIP").ends ("-", "+"));
    jassert ((int) v.size() == kParamCount);
    return v;
}

const std::vector<std::string>& tableNames()
{
    static const std::vector<std::string> n { "BASIC", "CLASSIC", "HARMONIC", "PWM", "SYNC", "FORMANT", "FM", "FOLD", "DIGITAL", "ORGAN", "BASS", "COMB", "FILE", "SAMPLE", "RECORDING" };      // (RECORDING: SAMPLE's frames at the levels the recording had)
    return n;
}

float shapePlace (float k)
{
    // flat ground of 0.02 either side of each quarter; the rest stretched to meet it
    k = clamp01 (k);
    const float q = k * 4.0f;
    const int i = juce::jlimit (0, 4, (int) std::lround (q));
    const float d = q - (float) i;
    constexpr float flat = 0.08f;        // (in quarters: 0.02 of the knob)
    if (std::abs (d) <= flat) return (float) i / 4.0f;
    const float s = d > 0.0f ? (d - flat) / (0.5f - flat) * 0.5f : (d + flat) / (0.5f - flat) * 0.5f;
    return clamp01 (((float) i + s) / 4.0f);
}

namespace
{
// ENV DECAY's knob for a time in seconds
float decayKnob (float sec) { return clamp01 (std::log (sec / 0.02f) / std::log (250.0f)); }
float cutKnob (float hz) { return clamp01 (std::log2 (hz / 30.0f) / 9.23f); }
}

const std::vector<Patch>& patches()
{
    // Each of today's models, made with SYNTH's controls (the model's own settings read across: its waves, second
    // oscillator, detune, sub, filter, envelopes, chord, ensemble), its level matched by GAIN to the model's own on the same
    // notes (--synthcover measures both). And the drum models SYNTH covers, and PERC's objects.
    static const std::vector<Patch> p {
        { "INIT", { } },
        { "BASS LADDER", { { "sy_shape", 0.5f }, { "sy_shape_b", 0.5f }, { "sy_mix", 0.44f }, { "sy_fine_b", 8.0f }, { "sy_sub", 0.4f }, { "sy_circuit", 3 },
                           { "sy_cutoff", 0.38f }, { "sy_res", 0.25f }, { "sy_envamt", 0.4f }, { "sy_decay", decayKnob (0.35f) }, { "sy_sustain", 0.8f }, { "sy_release", 80.0f },
                           { "sy_voices", 1 }, { "sy_phase", 1 }, { "sy_drift", 0.05f }, { "sy_gain", 3.0f } } },
        { "MONO 101", { { "sy_shape", 0.5f }, { "sy_shape_b", 0.75f }, { "sy_mix", 0.33f }, { "sy_sub", 0.3f }, { "sy_circuit", 3 }, { "sy_cutoff", 0.45f }, { "sy_res", 0.3f },
                        { "sy_envamt", 0.45f }, { "sy_decay", decayKnob (0.25f) }, { "sy_sustain", 0.6f }, { "sy_release", 80.0f }, { "sy_voices", 1 }, { "sy_space", 0.1f }, { "sy_gain", 7.0f } } },
        { "SUB OSC", { { "sy_shape", 0.75f }, { "sy_sub", 0.9f }, { "sy_circuit", 0 }, { "sy_cutoff", 0.45f }, { "sy_res", 0.1f }, { "sy_envamt", 0.15f },
                       { "sy_decay", decayKnob (0.6f) }, { "sy_sustain", 0.9f }, { "sy_release", 80.0f }, { "sy_voices", 1 }, { "sy_phase", 1 }, { "sy_gain", -2.7f } } },
        { "SINE SUB", { { "sy_shape", 0.0f }, { "sy_circuit", 0 }, { "sy_cutoff", 0.6f }, { "sy_res", 0.0f }, { "sy_envamt", 0.1f }, { "sy_drive", 0.15f },
                        { "sy_decay", decayKnob (0.5f) }, { "sy_sustain", 0.7f }, { "sy_release", 60.0f }, { "sy_voices", 1 }, { "sy_phase", 1 }, { "sy_gain", -2.8f } } },
        { "OFFBEAT", { { "sy_shape", 0.75f }, { "sy_shape_b", 0.5f }, { "sy_mix", 0.37f }, { "sy_fine_b", 5.0f }, { "sy_circuit", 3 }, { "sy_cutoff", 0.4f }, { "sy_res", 0.4f },
                       { "sy_envamt", 0.6f }, { "sy_decay", decayKnob (0.15f) }, { "sy_sustain", 0.0f }, { "sy_release", 60.0f }, { "sy_voices", 1 }, { "sy_gain", 13.5f } } },
        { "ACID 303", { { "sy_shape", 0.5f }, { "sy_circuit", 4 }, { "sy_keytrack", 0.0f }, { "sy_cutoff", 0.35f }, { "sy_res", 0.75f }, { "sy_envamt", 0.6f }, { "sy_decay", decayKnob (0.71f) },
                        { "sy_accent", 0.7f }, { "sy_attack", 3.0f }, { "sy_sustain", 1.0f }, { "sy_release", 8.0f }, { "sy_voices", 1 }, { "sy_slide", 0.22f }, { "sy_space", 0.1f }, { "sy_gain", 4.2f } } },
        { "FM BASS", { { "sy_shape", 0.0f }, { "sy_shape_b", 0.0f }, { "sy_fm", 0.45f }, { "sy_circuit", 0 }, { "sy_cutoff", 0.55f }, { "sy_res", 0.1f }, { "sy_envamt", 0.3f },
                       { "sy_decay", decayKnob (0.3f) }, { "sy_sustain", 0.7f }, { "sy_release", 80.0f }, { "sy_voices", 1 }, { "sy_phase", 1 }, { "sy_gain", 2.8f } } },
        { "FM BELL", { { "sy_shape", 0.0f }, { "sy_shape_b", 0.0f }, { "sy_pitch_b", 21.7f }, { "sy_fm", 0.55f }, { "sy_circuit", 0 }, { "sy_cutoff", 0.85f }, { "sy_res", 0.0f },
                       { "sy_envamt", 0.0f }, { "sy_decay", decayKnob (1.5f) }, { "sy_sustain", 0.0f }, { "sy_release", 1200.0f }, { "sy_voices", 8 }, { "sy_space", 0.1f }, { "sy_gain", 8.1f } } },
        { "FM 4-OP", { { "sy_shape", 0.0f }, { "sy_table_b", 6 }, { "sy_shape_b", 0.3f }, { "sy_pitch_b", 12.0f }, { "sy_fm", 0.4f }, { "sy_circuit", 0 }, { "sy_cutoff", 0.8f },
                       { "sy_envamt", 0.1f }, { "sy_decay", decayKnob (1.0f) }, { "sy_sustain", 0.6f }, { "sy_release", 300.0f }, { "sy_voices", 8 }, { "sy_space", 0.15f }, { "sy_gain", 9.6f } } },
        { "MS-20", { { "sy_shape", 0.5f }, { "sy_shape_b", 0.75f }, { "sy_mix", 0.37f }, { "sy_fine_b", 10.0f }, { "sy_circuit", 5 }, { "sy_cutoff", 0.45f }, { "sy_res", 0.55f },
                     { "sy_envamt", 0.45f }, { "sy_drive", 0.4f }, { "sy_decay", decayKnob (0.5f) }, { "sy_sustain", 0.7f }, { "sy_release", 100.0f }, { "sy_voices", 1 }, { "sy_space", 0.1f }, { "sy_gain", -0.7f } } },
        { "SYNC LEAD", { { "sy_shape", 0.5f }, { "sy_shape_b", 0.5f }, { "sy_mix", 1.0f }, { "sy_sync", 1 }, { "sy_pitch_b", 7.0f }, { "sy_circuit", 0 }, { "sy_cutoff", 0.7f },
                         { "sy_res", 0.15f }, { "sy_envamt", 0.3f }, { "sy_decay", decayKnob (0.5f) }, { "sy_sustain", 0.7f }, { "sy_release", 100.0f }, { "sy_voices", 1 }, { "sy_space", 0.1f }, { "sy_gain", 3.7f } } },
        { "HOOVER", { { "sy_table_a", 3 }, { "sy_shape", 0.35f }, { "sy_unison", 3 }, { "sy_detune", 0.5f }, { "sy_width", 0.8f }, { "sy_circuit", 0 }, { "sy_cutoff", 0.75f },
                      { "sy_res", 0.1f }, { "sy_envamt", 0.15f }, { "sy_pitch_env", -7.0f }, { "sy_pitch_time", 180.0f }, { "sy_decay", decayKnob (1.0f) }, { "sy_sustain", 0.9f },
                      { "sy_release", 300.0f }, { "sy_voices", 1 }, { "sy_space", 0.4f }, { "sy_gain", 4.2f } } },
        { "STAB DUB", { { "sy_shape", 0.5f }, { "sy_shape_b", 0.75f }, { "sy_mix", 0.33f }, { "sy_fine_b", 6.0f }, { "sy_circuit", 0 }, { "sy_cutoff", 0.45f }, { "sy_res", 0.25f },
                        { "sy_envamt", 0.4f }, { "sy_chord", 3 }, { "sy_decay", decayKnob (0.25f) }, { "sy_sustain", 0.0f }, { "sy_release", 150.0f }, { "sy_voices", 12 }, { "sy_space", 0.15f }, { "sy_gain", 0.5f } } },
        { "STAB DETROIT", { { "sy_shape", 0.5f }, { "sy_unison", 3 }, { "sy_detune", 0.3f }, { "sy_shape_b", 0.75f }, { "sy_mix", 0.23f }, { "sy_circuit", 0 }, { "sy_cutoff", 0.5f },
                            { "sy_res", 0.2f }, { "sy_envamt", 0.35f }, { "sy_chord", 5 }, { "sy_decay", decayKnob (0.35f) }, { "sy_sustain", 0.2f }, { "sy_release", 250.0f },
                            { "sy_voices", 16 }, { "sy_space", 0.3f }, { "sy_gain", -0.8f } } },
        { "ORGAN M1", { { "sy_table_a", 9 }, { "sy_shape", 0.6f }, { "sy_circuit", 0 }, { "sy_cutoff", 0.75f }, { "sy_res", 0.0f }, { "sy_envamt", 0.1f }, { "sy_chord", 1 },
                        { "sy_decay", decayKnob (0.3f) }, { "sy_sustain", 0.3f }, { "sy_release", 80.0f }, { "sy_voices", 12 }, { "sy_space", 0.1f }, { "sy_gain", 0.9f } } },
        { "STRING MACHINE", { { "sy_shape", 0.5f }, { "sy_unison", 4 }, { "sy_detune", 0.4f }, { "sy_width", 0.8f }, { "sy_shape_b", 0.5f }, { "sy_pitch_b", 12.0f }, { "sy_mix", 0.33f },
                              { "sy_circuit", 0 }, { "sy_cutoff", 0.6f }, { "sy_res", 0.05f }, { "sy_envamt", 0.05f }, { "sy_attack", 300.0f }, { "sy_decay", decayKnob (1.0f) },
                              { "sy_sustain", 0.75f }, { "sy_release", 800.0f }, { "sy_voices", 12 }, { "sy_space", 0.5f }, { "sy_gain", -1.4f } } },
        { "POLY PAD", { { "sy_shape", 0.5f }, { "sy_shape_b", 0.75f }, { "sy_mix", 0.37f }, { "sy_fine_b", 10.0f }, { "sy_sub", 0.3f }, { "sy_circuit", 0 }, { "sy_cutoff", 0.5f },
                        { "sy_res", 0.15f }, { "sy_envamt", 0.2f }, { "sy_mattack", 800.0f }, { "sy_attack", 500.0f }, { "sy_decay", decayKnob (2.0f) }, { "sy_sustain", 1.0f },
                        { "sy_release", 1200.0f }, { "sy_voices", 12 }, { "sy_space", 0.35f }, { "sy_gain", -0.7f } } },
        { "DUB WASH", { { "sy_shape", 0.5f }, { "sy_shape_b", 0.75f }, { "sy_mix", 0.28f }, { "sy_fine_b", 5.0f }, { "sy_circuit", 0 }, { "sy_cutoff", 0.45f }, { "sy_res", 0.2f },
                        { "sy_envamt", 0.25f }, { "sy_chord", 3 }, { "sy_attack", 50.0f }, { "sy_decay", decayKnob (1.5f) }, { "sy_sustain", 0.6f }, { "sy_release", 1500.0f },
                        { "sy_voices", 16 }, { "sy_space", 0.25f }, { "sy_gain", -5.3f } } },
        { "TAPE LOOP", { { "sy_shape", 0.25f }, { "sy_shape_b", 0.5f }, { "sy_mix", 0.23f }, { "sy_fine_b", 3.0f }, { "sy_noise", 0.02f }, { "sy_color", 0.5f }, { "sy_circuit", 0 },
                         { "sy_cutoff", 0.55f }, { "sy_res", 0.0f }, { "sy_envamt", 0.0f }, { "sy_drive", 0.3f }, { "sy_attack", 400.0f }, { "sy_decay", decayKnob (1.0f) },
                         { "sy_sustain", 0.7f }, { "sy_release", 1500.0f }, { "sy_drift", 1.0f }, { "sy_voices", 12 }, { "sy_space", 0.15f }, { "sy_gain", -2.7f } } },
        { "NOISE BED", { { "sy_mix", 0.0f }, { "sy_noise", 1.0f }, { "sy_color", 0.3f }, { "sy_shape", 0.0f }, { "sy_circuit", 1 }, { "sy_cutoff", 0.6f }, { "sy_res", 0.3f },
                         { "sy_envamt", 0.0f }, { "sy_attack", 800.0f }, { "sy_decay", decayKnob (1.0f) }, { "sy_sustain", 0.7f }, { "sy_release", 2000.0f }, { "sy_voices", 8 },
                         { "sy_space", 0.15f }, { "sy_gain", 5.4f } } },
        // the drums
        { "HAT 909", { { "sy_noise", 1.0f }, { "sy_color", 0.85f }, { "sy_circuit", 2 }, { "sy_cutoff", cutKnob (7000.0f) }, { "sy_res", 0.15f }, { "sy_envamt", 0.0f },
                       { "sy_keytrack", 0.0f }, { "sy_attack", 0.5f }, { "sy_decay", decayKnob (0.09f) }, { "sy_sustain", 0.0f }, { "sy_release", 30.0f }, { "sy_voices", 4 }, { "sy_gain", 9.1f } } },
        { "HAT 808", { { "sy_noise", 1.0f }, { "sy_color", 1.0f }, { "sy_circuit", 2 }, { "sy_cutoff", cutKnob (7500.0f) }, { "sy_res", 0.2f }, { "sy_envamt", 0.0f },
                       { "sy_keytrack", 0.0f }, { "sy_attack", 0.5f }, { "sy_decay", decayKnob (0.05f) }, { "sy_sustain", 0.0f }, { "sy_release", 20.0f }, { "sy_voices", 4 }, { "sy_gain", 9.2f } } },
        { "HAT 606", { { "sy_noise", 1.0f }, { "sy_color", 0.92f }, { "sy_circuit", 2 }, { "sy_cutoff", cutKnob (9000.0f) }, { "sy_res", 0.3f }, { "sy_envamt", 0.0f },
                       { "sy_keytrack", 0.0f }, { "sy_attack", 0.5f }, { "sy_decay", decayKnob (0.06f) }, { "sy_sustain", 0.0f }, { "sy_release", 20.0f }, { "sy_voices", 4 }, { "sy_gain", 8.5f } } },
        { "OPEN HAT", { { "sy_noise", 1.0f }, { "sy_color", 0.85f }, { "sy_circuit", 2 }, { "sy_cutoff", cutKnob (6500.0f) }, { "sy_res", 0.15f }, { "sy_envamt", 0.0f },
                        { "sy_keytrack", 0.0f }, { "sy_attack", 0.5f }, { "sy_decay", decayKnob (0.5f) }, { "sy_sustain", 0.0f }, { "sy_release", 120.0f }, { "sy_voices", 4 }, { "sy_gain", 5.6f } } },
        { "RIDE", { { "sy_noise", 1.0f }, { "sy_color", 1.0f }, { "sy_circuit", 2 }, { "sy_cutoff", cutKnob (4500.0f) }, { "sy_res", 0.1f }, { "sy_envamt", 0.0f },
                    { "sy_keytrack", 0.0f }, { "sy_attack", 0.5f }, { "sy_decay", decayKnob (1.2f) }, { "sy_sustain", 0.0f }, { "sy_release", 300.0f }, { "sy_body", 3 },
                    { "sy_body_mix", 0.35f }, { "sy_body_decay", 0.6f }, { "sy_body_tone", 0.8f }, { "sy_voices", 4 }, { "sy_gain", 2.4f } } },
        { "CRASH", { { "sy_noise", 1.0f }, { "sy_color", 0.7f }, { "sy_circuit", 2 }, { "sy_cutoff", cutKnob (3500.0f) }, { "sy_res", 0.05f }, { "sy_envamt", 0.0f },
                     { "sy_keytrack", 0.0f }, { "sy_attack", 0.5f }, { "sy_decay", decayKnob (1.8f) }, { "sy_sustain", 0.0f }, { "sy_release", 600.0f }, { "sy_body", 3 },
                     { "sy_body_mix", 0.3f }, { "sy_body_decay", 0.8f }, { "sy_body_tone", 0.7f }, { "sy_voices", 4 }, { "sy_gain", 2.7f } } },
        { "CLAP", { { "sy_noise", 1.0f }, { "sy_color", 0.0f }, { "sy_circuit", 1 }, { "sy_cutoff", cutKnob (1200.0f) }, { "sy_res", 0.25f }, { "sy_envamt", 0.0f },
                    { "sy_keytrack", 0.0f }, { "sy_attack", 0.5f }, { "sy_decay", decayKnob (0.15f) }, { "sy_sustain", 0.0f }, { "sy_release", 60.0f }, { "sy_bursts", 3 },
                    { "sy_burst_gap", 11.0f }, { "sy_voices", 4 }, { "sy_gain", 4.7f } } },
        { "SHAKER", { { "sy_noise", 1.0f }, { "sy_color", 0.0f }, { "sy_circuit", 2 }, { "sy_cutoff", cutKnob (5500.0f) }, { "sy_res", 0.1f }, { "sy_envamt", 0.0f },
                      { "sy_keytrack", 0.0f }, { "sy_attack", 8.0f }, { "sy_decay", decayKnob (0.08f) }, { "sy_sustain", 0.0f }, { "sy_release", 30.0f }, { "sy_voices", 4 }, { "sy_gain", 2.8f } } },
        { "RIM", { { "sy_shape", 0.0f }, { "sy_noise", 0.4f }, { "sy_color", 0.0f }, { "sy_circuit", 1 }, { "sy_cutoff", cutKnob (1700.0f) }, { "sy_res", 0.3f }, { "sy_envamt", 0.0f },
                   { "sy_keytrack", 0.0f }, { "sy_attack", 0.5f }, { "sy_decay", decayKnob (0.025f) }, { "sy_sustain", 0.0f }, { "sy_release", 10.0f }, { "sy_body", 2 },
                   { "sy_body_mix", 0.5f }, { "sy_body_decay", 0.2f }, { "sy_body_tone", 0.7f }, { "sy_tune", 12.0f }, { "sy_voices", 4 }, { "sy_phase", 1 }, { "sy_gain", 18.8f } } },
        { "COWBELL", { { "sy_shape", 0.75f }, { "sy_shape_b", 0.75f }, { "sy_pitch_b", 6.8f }, { "sy_mix", 0.5f }, { "sy_circuit", 1 }, { "sy_cutoff", cutKnob (1000.0f) },
                       { "sy_res", 0.2f }, { "sy_envamt", 0.0f }, { "sy_keytrack", 1.0f }, { "sy_attack", 0.5f }, { "sy_decay", decayKnob (0.25f) }, { "sy_sustain", 0.0f },
                       { "sy_release", 60.0f }, { "sy_tune", 1.0f }, { "sy_voices", 4 }, { "sy_phase", 1 }, { "sy_gain", 7.5f } } },
        { "CLAVE", { { "sy_shape", 0.0f }, { "sy_circuit", 1 }, { "sy_cutoff", cutKnob (2500.0f) }, { "sy_res", 0.2f }, { "sy_envamt", 0.0f }, { "sy_keytrack", 1.0f },
                     { "sy_attack", 0.5f }, { "sy_decay", decayKnob (0.04f) }, { "sy_sustain", 0.0f }, { "sy_release", 15.0f }, { "sy_body", 2 }, { "sy_body_mix", 0.7f },
                     { "sy_body_decay", 0.25f }, { "sy_body_tone", 0.4f }, { "sy_tune", 19.0f }, { "sy_voices", 4 }, { "sy_phase", 1 }, { "sy_gain", 27.0f } } },
        // PERC's struck objects: a click and a breath of noise into the body of that name
        { "MEMBRANE", { { "sy_shape", 0.0f }, { "sy_noise", 0.5f }, { "sy_color", 0.5f }, { "sy_circuit", 0 }, { "sy_cutoff", cutKnob (3000.0f) }, { "sy_res", 0.0f }, { "sy_envamt", 0.0f },
                        { "sy_attack", 0.5f }, { "sy_decay", decayKnob (0.03f) }, { "sy_sustain", 0.0f }, { "sy_release", 10.0f }, { "sy_body", 1 }, { "sy_body_mix", 0.9f },
                        { "sy_body_decay", 0.45f }, { "sy_body_tone", 0.45f }, { "sy_voices", 4 }, { "sy_phase", 1 }, { "sy_gain", 19.3f } } },
        { "BAR", { { "sy_shape", 0.0f }, { "sy_noise", 0.3f }, { "sy_color", 0.5f }, { "sy_circuit", 0 }, { "sy_cutoff", cutKnob (5000.0f) }, { "sy_res", 0.0f }, { "sy_envamt", 0.0f },
                   { "sy_attack", 0.5f }, { "sy_decay", decayKnob (0.02f) }, { "sy_sustain", 0.0f }, { "sy_release", 10.0f }, { "sy_body", 2 }, { "sy_body_mix", 0.95f },
                   { "sy_body_decay", 0.55f }, { "sy_body_tone", 0.5f }, { "sy_voices", 4 }, { "sy_phase", 1 }, { "sy_gain", 24.4f } } },
        { "PLATE", { { "sy_noise", 0.8f }, { "sy_color", 0.6f }, { "sy_circuit", 0 }, { "sy_cutoff", cutKnob (8000.0f) }, { "sy_res", 0.0f }, { "sy_envamt", 0.0f },
                     { "sy_attack", 0.5f }, { "sy_decay", decayKnob (0.03f) }, { "sy_sustain", 0.0f }, { "sy_release", 10.0f }, { "sy_body", 3 }, { "sy_body_mix", 0.9f },
                     { "sy_body_decay", 0.6f }, { "sy_body_tone", 0.6f }, { "sy_voices", 4 }, { "sy_gain", 22.0f } } },
        { "TUBE", { { "sy_shape", 0.0f }, { "sy_noise", 0.4f }, { "sy_color", 0.5f }, { "sy_circuit", 0 }, { "sy_cutoff", cutKnob (4000.0f) }, { "sy_res", 0.0f }, { "sy_envamt", 0.0f },
                    { "sy_attack", 0.5f }, { "sy_decay", decayKnob (0.025f) }, { "sy_sustain", 0.0f }, { "sy_release", 10.0f }, { "sy_body", 4 }, { "sy_body_mix", 0.95f },
                    { "sy_body_decay", 0.55f }, { "sy_body_tone", 0.5f }, { "sy_voices", 4 }, { "sy_phase", 1 }, { "sy_gain", 25.4f } } },
        { "BELL", { { "sy_shape", 0.0f }, { "sy_noise", 0.2f }, { "sy_color", 0.6f }, { "sy_circuit", 0 }, { "sy_cutoff", cutKnob (8000.0f) }, { "sy_res", 0.0f }, { "sy_envamt", 0.0f },
                    { "sy_attack", 0.5f }, { "sy_decay", decayKnob (0.02f) }, { "sy_sustain", 0.0f }, { "sy_release", 10.0f }, { "sy_body", 5 }, { "sy_body_mix", 0.95f },
                    { "sy_body_decay", 0.8f }, { "sy_body_tone", 0.55f }, { "sy_voices", 4 }, { "sy_phase", 1 }, { "sy_gain", 29.7f } } },
        { "GONG", { { "sy_noise", 0.6f }, { "sy_color", 0.5f }, { "sy_circuit", 0 }, { "sy_cutoff", cutKnob (4000.0f) }, { "sy_res", 0.0f }, { "sy_envamt", 0.0f },
                    { "sy_attack", 0.5f }, { "sy_decay", decayKnob (0.05f) }, { "sy_sustain", 0.0f }, { "sy_release", 10.0f }, { "sy_body", 6 }, { "sy_body_mix", 0.9f },
                    { "sy_body_decay", 0.9f }, { "sy_body_tone", 0.45f }, { "sy_voices", 4 }, { "sy_gain", 26.1f } } },
    };
    return p;
}
std::vector<std::string> patchNames() { std::vector<std::string> v; for (auto& p : patches()) v.push_back (p.name); return v; }
int patchIndex (const char* name) { const auto& p = patches(); for (size_t i = 0; i < p.size(); ++i) if (juce::String (p[i].name) == name) return (int) i; return 0; }
const char* lanePatch (const juce::String& s)
{
    if (s == "bass") return "BASS LADDER";
    if (s == "lead") return "ACID 303";
    if (s == "chord") return "STAB DUB";
    if (s == "pad") return "DUB WASH";
    if (s == "closed_hat") return "HAT 909";
    if (s == "open_hat") return "OPEN HAT";
    if (s == "clap") return "CLAP";
    if (s == "perc") return "MEMBRANE";
    return "INIT";
}

namespace
{
// BODY's objects: PERC's (Strike.cpp's), their modes' ratios and levels
struct Object { int n; float ratio[16]; float amp[16]; };
const Object kObjects[] = {
    { 10, { 1.0f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f, 3.6f, 3.652f }, { 1.0f, 0.7f, 0.55f, 0.5f, 0.4f, 0.35f, 0.3f, 0.25f, 0.2f, 0.2f } },        // MEMBRANE
    { 6, { 1.0f, 2.756f, 5.404f, 8.933f, 13.34f, 18.64f }, { 1.0f, 0.5f, 0.3f, 0.2f, 0.12f, 0.08f } },                                                              // BAR
    { 14, { 1.0f, 2.5f, 2.52f, 4.0f, 5.0f, 5.04f, 6.5f, 6.53f, 8.5f, 9.0f, 10.0f, 10.06f, 12.5f, 16.0f },
          { 1.0f, 0.8f, 0.7f, 0.6f, 0.55f, 0.5f, 0.45f, 0.4f, 0.35f, 0.33f, 0.3f, 0.28f, 0.22f, 0.16f } },                                                         // PLATE
    { 7, { 1.0f, 2.72f, 5.33f, 8.8f, 13.1f, 18.4f, 24.5f }, { 0.7f, 1.0f, 0.8f, 0.6f, 0.4f, 0.25f, 0.15f } },                                                      // TUBE
    { 11, { 0.5f, 1.0f, 1.183f, 1.506f, 2.0f, 2.514f, 2.662f, 3.011f, 4.166f, 5.433f, 6.796f }, { 0.6f, 0.9f, 0.8f, 0.45f, 1.0f, 0.5f, 0.35f, 0.4f, 0.3f, 0.2f, 0.12f } },  // BELL
    { 16, { 1.0f, 1.1f, 1.38f, 1.5f, 1.73f, 1.96f, 2.24f, 2.6f, 2.83f, 3.2f, 3.6f, 4.1f, 4.7f, 5.3f, 6.1f, 7.0f },
          { 1.0f, 0.8f, 0.75f, 0.7f, 0.65f, 0.6f, 0.55f, 0.5f, 0.45f, 0.42f, 0.38f, 0.34f, 0.3f, 0.26f, 0.22f, 0.18f } } };                                       // GONG
// The 808's six square oscillators, as ratios of the lowest (205.3, 304.4, 369.6, 522.7, 540, 800 Hz)
constexpr float kMetal[6] = { 1.0f, 1.4827f, 1.8003f, 2.5460f, 2.6303f, 3.8967f };
// CHORD's notes
const int kChords[9][4] = { { 0, -1, -1, -1 }, { 0, 3, 7, -1 }, { 0, 4, 7, -1 }, { 0, 3, 7, 10 }, { 0, 4, 7, 11 }, { 0, 3, 10, 14 }, { 0, 5, 7, -1 }, { 0, 7, -1, -1 }, { 0, 12, -1, -1 } };

inline void wrap (double& p) { if (p >= 1.0) p -= std::floor (p); else if (p < 0.0) p -= std::floor (p); }
// the BASIC table: five frames, the five pure waves, so the quarters of SHAPE are each one exactly
std::shared_ptr<const Wavetable> basicFive (double osr)
{
    static std::mutex m;
    static std::map<long, std::shared_ptr<const Wavetable>> cache;
    std::lock_guard<std::mutex> g (m);
    if (auto it = cache.find (std::lround (osr)); it != cache.end()) return it->second;
    constexpr int kH = Wavetable::kMaxHarm;
    std::vector<std::vector<float>> amp (5), ph (5);
    for (auto& x : amp) x.assign ((size_t) kH, 0.0f);
    for (auto& x : ph) x.assign ((size_t) kH, 0.0f);
    amp[0][0] = 1.0f;
    Wavetable::piecesToHarmonics ({ { 0.0, 0.25, 0.0, 1.0 }, { 0.25, 0.75, 1.0, -1.0 }, { 0.75, 1.0, -1.0, 0.0 } }, kH, amp[1], ph[1]);
    Wavetable::piecesToHarmonics ({ { 0.0, 0.5, 0.0, 1.0 }, { 0.5, 1.0, -1.0, 0.0 } }, kH, amp[2], ph[2]);
    Wavetable::piecesToHarmonics ({ { 0.0, 0.5, 1.0, 1.0 }, { 0.5, 1.0, -1.0, -1.0 } }, kH, amp[3], ph[3]);
    Wavetable::piecesToHarmonics ({ { 0.0, 0.1, 1.0, 1.0 }, { 0.1, 1.0, -1.0, -1.0 } }, kH, amp[4], ph[4]);
    for (auto& x : amp) Wavetable::toRms (x);
    auto t = std::make_shared<Wavetable>();
    t->build (amp, ph, osr);
    t->name = "BASIC";
    cache[std::lround (osr)] = t;
    return t;
}
// WARP: the cycle bent as it is read (as WAVETABLE's TWIST)
inline float warped (const Wavetable& w, float place, int oct, double ph, int as, float k, float foldIn)
{
    switch (as)
    {
        case 0: return w.at (place, oct, ph / (ph + (1.0 - ph) * (double) k));
        case 1:
        {
            double p2 = ph * (double) k; p2 -= std::floor (p2);
            float y = w.at (place, oct, p2);
            if (ph > 0.9375) { const float m = 0.5f + 0.5f * std::cos (3.14159265f * (float) ((ph - 0.9375) * 16.0)); y = y * m + w.at (place, oct, 0.0) * (1.0f - m); }
            return y;
        }
        case 2:
        {
            const float y = w.at (place, oct, ph), z = y * k * 0.25f + 0.25f;
            const float folded = 1.0f - 4.0f * std::abs (z - std::floor (z) - 0.5f);
            return y + foldIn * (folded - y);
        }
        default: return w.at (place, oct, ph * (double) k < 1.0 ? ph * (double) k : 0.0);
    }
}
inline float warpK (int as, float warp) { return as == 0 ? 1.0f + 8.0f * warp : as == 1 ? 1.0f + 7.0f * warp : as == 2 ? 1.0f + 5.0f * warp : 1.0f / (1.0f - 0.9f * warp); }
} // namespace
} // namespace syn

using namespace syn;

void SynthEngine::bind (const std::vector<ParamSpec>& specs, bool pitchedLane, Ends e)
{
    pitched = pitchedLane; ends = e;
    idList.clear();
    for (size_t i = 0; i < specs.size(); ++i) if (specs[i].id.rfind ("sy_", 0) == 0) idList.push_back ({ specs[i].id, (int) i });
    auto at = [this] (const char* id) { return indexOf (id); };
    ids.tune = at ("sy_tune"); ids.shape = at ("sy_shape"); ids.cutoff = at ("sy_cutoff"); ids.res = at ("sy_res"); ids.envamt = at ("sy_envamt");
    ids.decay = at ("sy_decay"); ids.accent = at ("sy_accent"); ids.drive = at ("sy_drive");
    ids.tableA = at ("sy_table_a"); ids.warp = at ("sy_warp"); ids.warpAs = at ("sy_warp_as"); ids.tableB = at ("sy_table_b"); ids.shapeB = at ("sy_shape_b");
    ids.pitchB = at ("sy_pitch_b"); ids.fineB = at ("sy_fine_b"); ids.mix = at ("sy_mix"); ids.fm = at ("sy_fm"); ids.sync = at ("sy_sync");
    ids.sub = at ("sy_sub"); ids.noise = at ("sy_noise"); ids.color = at ("sy_color"); ids.phase = at ("sy_phase");
    ids.circuit = at ("sy_circuit"); ids.keytrack = at ("sy_keytrack"); ids.mattack = at ("sy_mattack"); ids.attack = at ("sy_attack");
    ids.sustain = at ("sy_sustain"); ids.release = at ("sy_release"); ids.pitchEnv = at ("sy_pitch_env"); ids.pitchTime = at ("sy_pitch_time");
    ids.bursts = at ("sy_bursts"); ids.burstGap = at ("sy_burst_gap");
    ids.voices = at ("sy_voices"); ids.glide = at ("sy_glide"); ids.slide = at ("sy_slide"); ids.chord = at ("sy_chord"); ids.unison = at ("sy_unison");
    ids.detune = at ("sy_detune"); ids.width = at ("sy_width"); ids.drift = at ("sy_drift");
    ids.body = at ("sy_body"); ids.bodyMix = at ("sy_body_mix"); ids.bodyDecay = at ("sy_body_decay"); ids.bodyTone = at ("sy_body_tone");
    ids.patch = at ("sy_patch"); ids.space = at ("sy_space"); ids.gain = at ("sy_gain"); ids.scan = at ("sy_scan");
    ids.scanEnd = at ("sy_scan_end"); ids.steps = at ("sy_steps"); ids.wdepth = at ("sy_wdepth"); ids.wloop = at ("sy_wloop"); ids.wvel = at ("sy_wvel"); ids.wkey = at ("sy_wkey");
    for (int k = 0; k < 4; ++k) { ids.wt[k] = at (("sy_wt" + std::to_string (k + 1)).c_str()); ids.wp[k] = at (("sy_wp" + std::to_string (k + 1)).c_str()); }
}
int SynthEngine::indexOf (const char* id) const
{
    for (auto& p : idList) if (p.first == id) return p.second;
    return -1;
}

SynthEngine::Block SynthEngine::blockOf (const Ids& d, const std::function<float (int)>& P)
{
    Block b;
    auto v = [&] (int i, float def) { return i >= 0 ? P (i) : def; };
    auto ci = [&] (int i, int def) { return i >= 0 ? (int) std::lround (P (i)) : def; };
    b.tune = v (d.tune, 0); b.shape = v (d.shape, 0.5f); b.cutoff = v (d.cutoff, 0.55f); b.res = v (d.res, 0.2f); b.envamt = v (d.envamt, 0.35f);
    b.decay = v (d.decay, 0.35f); b.accent = v (d.accent, 0.5f); b.drive = v (d.drive, 0.0f);
    b.tableA = ci (d.tableA, 0); b.warp = v (d.warp, 0); b.warpAs = ci (d.warpAs, 0); b.tableB = ci (d.tableB, 0); b.shapeB = v (d.shapeB, 0.5f);
    b.pitchB = v (d.pitchB, 0); b.fineB = v (d.fineB, 0); b.mix = v (d.mix, 0); b.fm = v (d.fm, 0); b.sync = v (d.sync, 0) > 0.5f;
    b.sub = v (d.sub, 0); b.noise = v (d.noise, 0); b.color = v (d.color, 0); b.phase = ci (d.phase, 0);
    b.circuit = juce::jlimit (0, 5, ci (d.circuit, 3)); b.keytrack = v (d.keytrack, 0.5f); b.mattack = v (d.mattack, 0); b.attack = v (d.attack, 1);
    b.sustain = v (d.sustain, 0.7f); b.release = v (d.release, 120); b.pitchEnv = v (d.pitchEnv, 0); b.pitchTime = v (d.pitchTime, 60);
    b.bursts = juce::jlimit (1, 4, ci (d.bursts, 1)); b.burstGap = v (d.burstGap, 10);
    b.voices = juce::jlimit (1, kVoices, ci (d.voices, 8)); b.glide = v (d.glide, 0); b.slide = v (d.slide, 0.3f); b.chord = juce::jlimit (0, 8, ci (d.chord, 0));
    b.unison = juce::jlimit (1, kUnison, ci (d.unison, 1)); b.detune = v (d.detune, 0.3f); b.width = v (d.width, 0.5f); b.drift = v (d.drift, 0.1f);
    b.body = juce::jlimit (0, 6, ci (d.body, 0)); b.bodyMix = v (d.bodyMix, 0.6f); b.bodyDecay = v (d.bodyDecay, 0.5f); b.bodyTone = v (d.bodyTone, 0.5f);
    b.space = v (d.space, 0); b.gain = v (d.gain, 0); b.scan = v (d.scan, 0.5f);
    b.scanEnd = juce::jlimit (0, 2, ci (d.scanEnd, 0)); b.steps = ci (d.steps, 0) == 1; b.wdepth = v (d.wdepth, 0); b.wloop = juce::jlimit (0, 3, ci (d.wloop, 0));
    b.wvel = v (d.wvel, 0.5f); b.wkey = v (d.wkey, 0.5f);
    for (int k = 0; k < 4; ++k) { b.wt[k] = v (d.wt[k], 500.0f); b.wp[k] = v (d.wp[k], 0.25f * (float) (k + 1)); }
    return b;
}

void SynthEngine::prepare (double rate, int maxBlock)
{
    sr = rate;
    set = WavetableSet::get (2.0 * rate);
    basic = basicFive (2.0 * rate);
    const int mb = std::max (64, maxBlock);
    l4.assign ((size_t) mb * 4, 0.0f); r4.assign ((size_t) mb * 4, 0.0f);
    l2.assign ((size_t) mb * 2, 0.0f); r2.assign ((size_t) mb * 2, 0.0f);
    outL.assign ((size_t) mb, 0.0f); outR.assign ((size_t) mb, 0.0f);
    for (auto& d : dec4) d.set (HalfbandDesign::forStage (rate, 1));
    dec.prepare (rate);
    ens.size = (int) (rate * 0.05); ens.buf.assign ((size_t) ens.size, 0.0f); ens.w = 0;
    Noise dr { 0x5BD1E995u };
    for (int k = 0; k < kSlots; ++k) { auto& v = voices[(size_t) k]; v.slot = k; v.driftTune = dr.next(); v.driftCut = dr.next(); v.driftPh = dr.uni(); v.driftRate = 0.7f + 0.6f * dr.uni(); }
    reset();
}
void SynthEngine::reset()
{
    for (auto& v : voices) { const auto keep = v; v = Voice(); v.slot = keep.slot; v.driftTune = keep.driftTune; v.driftCut = keep.driftCut; v.driftPh = keep.driftPh; v.driftRate = keep.driftRate; }
    numPending = 0; active = 0; accentCap[0] = accentCap[1] = 0.0f;
    for (auto& d : dec4) d.reset();
    dec.reset();
    std::fill (ens.buf.begin(), ens.buf.end(), 0.0f);
    publish();
}
void SynthEngine::fileChanged (const SamplePtr& s)
{
    fromFile.set (s != nullptr ? tables::tablesFrom (s, 2.0 * sr) : nullptr);
    // SAMPLE's and RECORDING's frames: on a thread of their own (a long recording's take a good part of a second)
    const int gen = ++scanState->gen;
    scanState->slot.set (nullptr);
    if (s == nullptr) return;
    auto build = [state = scanState, s, rate = 2.0 * sr, gen]
    {
        auto made = tables::scanFrom (s, rate);
        if (state->gen.load() == gen) state->slot.set (made);
        --tables::scansBuilding;
    };
    ++tables::scansBuilding;
    if (tables::asyncScans.load()) std::thread (build).detach();
    else build();
}

// how long a table is to SCAN: a recording's own length (SAMPLE, RECORDING), else a second
double SynthEngine::secondsOf (int which) const
{
    if (which == kTableSample || which == kTableRecording)
        if (const auto* t = scanState->slot.audio(); t != nullptr && t->table != nullptr && t->table->frames() > 0) return t->seconds;
    return 1.0;
}
const Wavetable* SynthEngine::tableFor (int which) const
{
    if (which <= 0 || set == nullptr) return basic.get();
    if (which < kTableFile) return &set->t[which == 1 ? 0 : which];
    if (which == kTableFile)
    {
        if (const auto* t = fromFile.audio(); t != nullptr && t->file != nullptr && t->file->frames() > 0) return t->file.get();
    }
    else if (const auto* t = scanState->slot.audio(); t != nullptr && t->table != nullptr && t->table->frames() > 0)
        return t->table.get();       // (SAMPLE and RECORDING: the frames a hundredth of a second apart)
    return basic.get();
}

void SynthEngine::begin (const Ctx& c, float dk, bool mono, int keyShift)
{
    decayKnob = dk; monoNow = mono; keyShiftNow = keyShift; bendNow = c.bend;
    if (! ids.ok()) return;
    blk = blockOf (ids, [&c] (int i) { return c.v (i); });
    // the transport stopped: what is held by nothing but the transport lets go (a drum lane's hits end of themselves)
    if (wasPlaying && ! c.playing) for (auto& v : voices) if (v.on && ! v.releasing && ends == Ends::keys && v.holdLeft < 0) release (v);
    wasPlaying = c.playing;
}

void SynthEngine::noteOn (int offset, int key, float pitch, float vel, float gain, int gen, int part, int hold)
{
    if (numPending >= (int) pending.size()) return;
    pending[(size_t) numPending++] = { offset, key, pitch, vel, gain, gen, part, hold, true };
}
void SynthEngine::noteOff (int offset, int key, int gen)
{
    if (numPending >= (int) pending.size()) return;
    pending[(size_t) numPending++] = { offset, key, 0.0f, 0.0f, 0.0f, gen, 0, -1, false };
}

void SynthEngine::gateNow (int hold, const int* down, int numDown)
{
    for (auto& v : voices)
    {
        if (! v.on || v.releasing || v.choked || ! v.gated) continue;
        if (hold >= 0)
        {
            const int left = hold * v.os - v.lived;
            if (left <= 0) release (v); else { v.holdLeft = left; v.held = false; }
        }
        else
        {
            bool isDown = false;
            for (int k = 0; k < numDown; ++k) isDown = isDown || down[k] == v.key;
            if (isDown) { v.holdLeft = -1; v.held = true; }
        }
    }
}

bool SynthEngine::keySounding (int key, int part) const
{
    for (auto& v : voices) if (v.on && ! v.releasing && ! v.choked && v.key == key && v.part == part) return true;
    return false;
}

void SynthEngine::release (Voice& v)
{
    if (v.releasing) return;
    v.releasing = true;
    v.ampStage = v.ampStage == 4 ? 4 : 3;
    if (v.modStage == 0) v.modStage = 1;
    v.tRel = 0.0;
}

void SynthEngine::setBody (Voice& v)
{
    if (blk.body <= 0) { v.nModes = 0; return; }
    const auto& o = kObjects[blk.body - 1];
    const double vr = sr * v.os;
    const float base = noteHz (v.pitch + blk.tune + bendNow + (float) keyShiftNow);
    const float t60 = 0.03f * std::pow (100.0f, blk.bodyDecay);
    int n = 0;
    for (int k = 0; k < o.n && n < kModes; ++k)
    {
        const float f = base * o.ratio[k];
        if (f >= 0.45f * (float) vr || f < 20.0f) continue;
        auto& m = v.modes[n];
        const float t = t60 / std::sqrt (o.ratio[k]);
        const float r = std::exp (-6.9f / (t * (float) vr));
        m.c = 2.0f * r * std::cos (kTwoPi * f / (float) vr);
        m.r = r * r;
        // (a mode's gain at its own frequency is about 1 / ((1 - r) 2 sin w): both taken off, so an object rings about as loud
        // as what strikes it, at any note; until this was measured a low note's object rang some 30 dB over it)
        const float sw = std::max (1.0e-4f, std::sin (kTwoPi * f / (float) vr));
        m.w = o.amp[k] * std::pow (o.ratio[k], (blk.bodyTone - 0.5f) * 2.0f) * (1.0f - r) * 2.0f * sw;
        ++n;
    }
    if (n != v.nModes) for (int k = v.nModes; k < n; ++k) v.modes[k].y1 = v.modes[k].y2 = 0.0f;
    v.nModes = n;
}

void SynthEngine::start (const Pending& p)
{
    const bool mono = monoNow || blk.voices == 1;
    const float tunedPitch = p.pitch;
    // MONO: the voice of this part that sounds takes the note: tied (its key still down) it slides there; let go first, it
    // begins again from where it is (no click, its filter and oscillators running on)
    if (mono)
    {
        Voice* cur = nullptr;
        for (auto& v : voices) if (v.on && ! v.choked && v.part == p.part && (cur == nullptr || v.order > cur->order)) cur = &v;
        // (the hundred and eleventh build) a note that begins again, not slid, where SYNC now wants the rate the voice was not
        // started at: a new voice, the old one let go over 3 ms (the rate is chosen as a voice begins)
        const int wantOs = (blk.circuit >= kLadder || blk.fm > 0.001f || blk.sync) ? 4 : 2;
        if (cur != nullptr && ! (! cur->releasing && blk.slide > 0.001f) && blk.sync && cur->os != wantOs) { cur->choked = true; cur = nullptr; }
        if (cur != nullptr)
        {
            for (auto& v : voices) if (&v != cur && v.on && v.part == p.part && ! v.choked) v.choked = true;
            const bool tied = ! cur->releasing;
            cur->key = p.key; cur->gen = p.gen; cur->vel = lerp (1.0f, clamp01 (p.vel), 0.6f); cur->gain = p.gain;
            cur->pitch = tunedPitch; cur->accent = p.vel > 0.85f && p.gen == 0 && blk.accent > 0.001f;      // (the hundred and eleventh build: as a new voice's)
            cur->slid = tied && blk.slide > 0.001f;
            const float glideS = cur->slid ? 0.005f + 0.25f * blk.slide : blk.glide > 0.001f ? 0.005f + 0.5f * blk.glide : 0.0f;
            cur->slideK = glideS > 0.0f ? 1.0f - std::exp (-1.0f / (glideS * (float) (sr * cur->os))) : 1.0f;
            cur->holdLeft = p.hold >= 0 ? p.hold * cur->os : -1;
            if (ends == Ends::hits && p.hold < 0) cur->holdLeft = (int) (std::min (8.0f, 0.02f * std::pow (100.0f, clamp01 (decayKnob))) * (float) (sr * cur->os));
            cur->held = cur->holdLeft < 0; cur->gated = ends == Ends::keys;
            if (! cur->slid)
            {
                cur->lived = 0;
                cur->releasing = false; cur->ampStage = 0; cur->modStage = 0; cur->meg = 1.0f; cur->tRel = -1.0; cur->t = 0.0; cur->scanPos = 0.0f; cur->wStage = 0; cur->wT = 0.0f; cur->wFrom = cur->wNow = shapePlace (blk.shape);
                cur->pitchEnvSt = blk.pitchEnv;
                cur->sync = blk.sync; cur->bursts = blk.bursts; cur->burstGap = blk.burstGap;      // (the hundred and eleventh build: a note begun again takes its NEXT ones, as a new voice does)
                setBody (*cur);
            }
            else cur->releasing = false;
            return;
        }
    }
    // a free place; past VOICES (or out of places) the oldest is let go over 3 ms
    int sounding = 0;
    for (auto& v : voices) if (v.on && ! v.choked && ! v.releasing) ++sounding;
    if (sounding >= blk.voices)
    {
        Voice* old = nullptr;
        for (auto& v : voices) if (v.on && ! v.choked && (old == nullptr || v.order < old->order)) old = &v;
        if (old != nullptr) old->choked = true;
    }
    int vi = -1;
    for (int k = 0; k < kSlots && vi < 0; ++k) if (! voices[(size_t) k].on) vi = k;
    if (vi < 0) { uint32_t oldest = 0xffffffffu; for (int k = 0; k < kSlots; ++k) if (voices[(size_t) k].order < oldest) { oldest = voices[(size_t) k].order; vi = k; } }
    auto& v = voices[(size_t) vi];
    // the note before (for GLIDE): the newest sounding voice of this part
    float fromHz = 0.0f;
    { uint32_t newest = 0; for (auto& o : voices) if (o.on && o.part == p.part && o.order >= newest && &o != &v) { newest = o.order; fromHz = o.freq; } }
    const auto keep = v;
    v = Voice();
    v.slot = keep.slot; v.driftTune = keep.driftTune; v.driftCut = keep.driftCut; v.driftPh = keep.driftPh; v.driftRate = keep.driftRate;
    v.on = true; v.key = p.key; v.gen = p.gen; v.part = p.part; v.order = orderNext++;
    v.pitch = tunedPitch;
    v.vel = lerp (1.0f, clamp01 (p.vel), 0.6f); v.gain = p.gain;
    v.accent = p.vel > 0.85f && p.gen == 0 && blk.accent > 0.001f;      // (SF-000, the hundred and fourth build: ACCENT at nought is none of it, its faster fall too)
    // 4x: the driven ladders, the 303's filter, the MS-20, FM and hard sync; 2x else
    v.os = (blk.circuit >= kLadder || blk.fm > 0.001f || blk.sync) ? 4 : 2;
    v.sync = blk.sync; v.bursts = blk.bursts; v.burstGap = blk.burstGap;      // (SF-000) kept for the note, as their NEXT says (SYNC chose its 4x here)
    const float target = noteHz (v.pitch + blk.tune + bendNow + (float) keyShiftNow);
    v.target = target;
    v.freq = blk.glide > 0.001f && fromHz > 0.0f ? fromHz : target;
    v.slideK = blk.glide > 0.001f ? 1.0f - std::exp (-1.0f / ((0.005f + 0.5f * blk.glide) * (float) (sr * v.os))) : 1.0f;
    v.holdLeft = p.hold >= 0 ? p.hold * v.os : -1;
    if (ends == Ends::hits && p.hold < 0) v.holdLeft = (int) (std::min (8.0f, 0.02f * std::pow (100.0f, clamp01 (decayKnob))) * (float) (sr * v.os));
    v.held = v.holdLeft < 0; v.gated = ends == Ends::keys; v.lived = 0;
    v.meg = 1.0f;
    v.pitchEnvSt = blk.pitchEnv;
    v.wFrom = v.wNow = shapePlace (blk.shape);        // (the wave envelope starts where SHAPE is)
    // PHASE: FREE, where this place's oscillators were (and a little apart for the copies); RESET, each from nought
    for (int u = 0; u < kUnison; ++u)
    {
        if (blk.phase == 1) { v.pa[u] = u == 0 ? 0.0 : (double) u / (double) kUnison; v.pb[u] = v.pa[u]; }
        else { v.pa[u] = keep.pa[u] != 0.0 ? keep.pa[u] : rng.uni(); v.pb[u] = keep.pb[u] != 0.0 ? keep.pb[u] : rng.uni(); }
    }
    v.psub = blk.phase == 1 ? 0.0 : keep.psub;
    for (int k = 0; k < 6; ++k) v.pmetal[k] = blk.phase == 1 ? 0.0 : rng.uni();
    v.dl.setHighPass (DiodeLadder303::kHighPass, sr * v.os);
    setBody (v);
}

// The voice's controls for the next n samples at its rate: where its filter is, from CUTOFF, the MOD envelope by ENV AMT,
// KEY TRACK, the accent's sweep, DRIFT and an echo's darkening.
void SynthEngine::voiceControls (Voice& v, int n)
{
    juce::ignoreUnused (n);
    const float note = v.pitch + blk.tune + bendNow + (float) keyShiftNow;
    v.target = noteHz (note);            // (the one place a note becomes a frequency: tunings would go here)
    if (! v.slid && v.slideK >= 1.0f) v.freq = v.target;
    float oct = std::log2 (cutoffHz (blk.cutoff));
    const float accentK = v.accent ? blk.accent : 0.0f;
    oct += blk.envamt * v.mod * 7.0f * (1.0f + accentK);
    oct += blk.keytrack * (note - 60.0f) / 12.0f;
    oct += accentK * accentCap[v.part & 1] * (1.0f + 2.0f * blk.res);
    oct += blk.drift * 0.2f * v.driftCut;
    oct -= 0.5f * (float) v.gen;
    v.cutNow = std::min (0.45f * (float) (sr * v.os) * 0.9f, std::max (20.0f, std::exp2 (oct)));
    v.sideK = clamp01 (v.cutNow / 2000.0f);
    const double vr = sr * v.os;
    switch (blk.circuit)
    {
        case kCleanLP: v.svf.set (v.cutNow, 0.6f + blk.res * 9.4f, vr); break;
        case kCleanBP: case kCleanHP: v.svf.set (v.cutNow, 0.6f + blk.res * 9.4f, vr); v.svfS.set (v.cutNow, 0.6f + blk.res * 9.4f, vr); break;
        case kLadder: v.ladder.set (v.cutNow, blk.res * 0.98f, vr); break;
        case kMs20: v.svf.set (v.cutNow, 0.6f + blk.res * 12.0f, vr); v.svf2.set (std::max (20.0f, v.cutNow * 0.08f), 0.7f + blk.res * 3.0f, vr); break;
        default: break;            // DIODE: each sample
    }
    if (blk.body > 0 || v.nModes > 0) setBody (v);        // (BODY reaches a note that sounds: its object, decay and tone)
}

void SynthEngine::renderVoice (Voice& v, float* L, float* R, int n, float partGain)
{
    const Block& b = blk;
    const double vr = sr * v.os;
    const float invVr = (float) (1.0 / vr);
    const Wavetable* ta = tableFor (b.tableA);
    const Wavetable* tb = tableFor (b.tableB);
    const float placeA = shapePlace (b.shape), placeB = shapePlace (b.shapeB);
    // SCAN: how far along the tables a sample moves this note (A's table sets the pace: a recording's own length, or
    // a second a table), and RECORDING's levels
    const float scanD = (b.scan - 0.5f) * 2.0f, scanAbs = std::abs (scanD);
    const float scanSpeed = scanAbs < 0.02f ? 0.0f : std::copysign (0.0625f * std::pow (64.0f, (scanAbs - 0.02f) / 0.98f), scanD);
    const float scanStep = scanSpeed / (float) secondsOf (b.tableA) * invVr;
    // the wave envelope's pace through its stage now, and where velocity and the key put the place
    const bool wEnv = b.wdepth > 0.001f;
    const float velRaw = (v.vel - 0.4f) / 0.6f;        // (the note's own velocity: v.vel is it at six tenths)
    const float placeVK = (b.wvel - 0.5f) * 2.0f * (velRaw - 0.5f) + (b.wkey - 0.5f) * 2.0f * (v.pitch - 60.0f) / 48.0f;
    const int nA = ta->frames(), nB = tb->frames();
    const bool levA = b.tableA == kTableRecording && ta != basic.get(), levB = b.tableB == kTableRecording && tb != basic.get();
    const int U = b.unison;
    const bool useB = b.mix > 0.001f || b.fm > 0.001f || v.sync;
    const float wk = b.warp > 0.001f ? warpK (b.warpAs, b.warp) : 1.0f, foldIn = std::min (1.0f, b.warp * 10.0f);
    const bool warpOn = b.warp > 0.001f;
    const float ratioB = semis (b.pitchB + b.fineB * 0.01f);
    // the envelopes' rates for these samples
    const float attK = 1.0f / std::max (1.0f, b.attack * 0.001f * (float) vr);
    const float decK = std::exp (-3.0f / (decaySeconds (b.decay) * (float) vr));
    // (the 303's: an accented note's envelope falls in 0.2 s, whatever ENV DECAY says, as the 303's accent switches its decay out)
    const float megK = std::exp (-3.0f / ((v.accent ? 0.2f : decaySeconds (b.decay)) * (float) vr));
    const float relK = std::exp (-6.9f / (std::max (0.001f, b.release * relScale * 0.001f) * (float) vr));
    const float mattK = 1.0f / std::max (1.0f, b.mattack * 0.001f * (float) vr);
    const float pitchK = std::exp (-3.0f / (std::max (0.001f, b.pitchTime * 0.001f) * (float) vr));
    const float sus = b.sustain;
    const float level = dbToGain (b.gain) * v.vel * v.gain * (1.0f + (v.accent ? 0.6f * b.accent : 0.0f)) * partGain * 0.5f;
    // unison: the copies' detune (up to half a semitone either way) and place across
    float uniR[kUnison], panL[kUnison], panR[kUnison];
    const float uniN = 1.0f / std::sqrt ((float) U);
    for (int u = 0; u < U; ++u)
    {
        const float x = U == 1 ? 0.0f : (float) u / (float) (U - 1) * 2.0f - 1.0f;
        uniR[u] = semis (x * b.detune * 0.5f);
        const float pan = 0.5f + 0.5f * x * b.width;
        panL[u] = std::sqrt (1.0f - pan) * 1.41421f * uniN; panR[u] = std::sqrt (pan) * 1.41421f * uniN;
    }
    // DRIFT: the slot's own tuning, and its slow wander
    const float driftSt = b.drift * (0.05f * v.driftTune + 0.04f * std::sin (kTwoPi * (0.071f * (float) v.t * v.driftRate + v.driftPh)));
    // the noise's colour: white to pink to metal
    const float cWhite = clamp01 (1.0f - 2.0f * b.color), cMetal = clamp01 (2.0f * b.color - 1.0f), cPink = 1.0f - cWhite - cMetal;
    const float burstGap = v.burstGap * 0.001f;
    const float burstEnd = (float) (v.bursts - 1) * burstGap;
    // DIODE: the 303's cutoff law (an x0xb0x's, as ACID 303-TYPE has it), its feedback and the level it makes up
    const float accentK = v.accent ? b.accent : 0.0f;
    // (SF-000, the hundred and fourth build) and KEY TRACK, as the other circuits have it: an octave of cutoff for an octave of
    // key at full, from middle C. (The 303 had none: ACID 303 sets it to nought.)
    // (the hundred and eleventh build: as a factor on the whole cutoff, so at full an octave of key is an octave of cutoff
    // exactly; it was on the curve's moving part alone, under the 303's fixed 87.5 Hz)
    const float acidKey = std::exp (0.6931f * b.keytrack * (v.pitch + b.tune + bendNow + (float) keyShiftNow - 60.0f) / 12.0f);
    const float acidL0 = 5.2262f + 1.7042f * b.cutoff - 0.6838f * b.envamt - 0.3466f * (float) v.gen - 0.1625f + 0.6931f * b.drift * 0.2f * v.driftCut;
    const float acidL1 = 5.5592f + 2.1779f * b.cutoff + 1.9922f * b.envamt - 0.3466f * (float) v.gen - 0.1625f + 0.6931f * b.drift * 0.2f * v.driftCut;
    const float acidSweep = 0.6931f * (1.0f + 2.0f * b.res), acidFbK = 0.96f * std::pow (b.res, 0.7f);
    const float acidComp = std::pow (1.0f + 16.3f * std::pow (b.res, 0.7f), 0.7f);
    const float capC = 1.0f - std::exp (-1.0f / (0.02f * (float) vr)), capD = std::exp (-1.0f / ((0.15f + 0.35f * b.res) * (float) vr));
    float& cap = accentCap[v.part & 1];
    const float driveG = 1.0f + b.drive * 6.0f, driveN = 1.2f / std::sqrt (driveG), driveW = std::min (1.0f, b.drive / 0.1f);
    const int octA0 = ta->octaveFor (std::max (20.0f, v.freq) * (warpOn ? wk : 1.0f) * (b.fm > 0.001f ? 2.0f : 1.0f) * 1.06f);
    const int octB0 = tb->octaveFor (std::max (20.0f, v.freq) * ratioB * (warpOn ? wk : 1.0f) * 1.06f);
    const int octAW = std::min (Mipmap::kOct - 1, octA0 + (int) std::ceil (std::log2 (b.warpAs == 2 ? wk * 2.0f : wk)));
    float bodyPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        // envelopes
        switch (v.ampStage)
        {
            case 0: v.amp += attK; if (v.amp >= 1.0f) { v.amp = 1.0f; v.ampStage = 1; } break;
            case 1: v.amp = sus + (v.amp - sus) * decK; if (std::abs (v.amp - sus) < 1.0e-4f) { v.amp = sus; v.ampStage = 2; } break;
            case 2: v.amp = sus; if (sus <= 0.0f) v.ampStage = 4; break;
            case 3: v.amp *= relK; if (v.amp < 1.0e-5f) { v.amp = 0.0f; v.ampStage = 4; } break;
            default: break;
        }
        if (v.modStage == 0) { v.mod += mattK; if (v.mod >= 1.0f) { v.mod = 1.0f; v.modStage = 1; } }
        else v.mod *= v.accent ? std::sqrt (decK) * decK : decK;
        v.meg *= megK;
        if (v.ampStage == 4 && (v.nModes == 0 || v.bodyEnergy < 1.0e-6f)) { v.on = false; return; }
        ++v.lived;
        if (v.holdLeft > 0 && --v.holdLeft == 0) release (v);
        float g = v.amp;
        if (v.choked) { v.chokeGain -= 1.0f / (0.003f * (float) vr); if (v.chokeGain <= 0.0f) { v.on = false; return; } g *= v.chokeGain; }
        if (v.bursts > 1 && v.t < (double) burstEnd) { const float ph = std::fmod ((float) v.t, burstGap); g *= std::exp (-ph / (burstGap * 0.3f)) * 0.85f + 0.15f; }
        // where along the tables (SHAPE, and how far SCAN has taken it: it stays at an end)
        if (scanStep != 0.0f) { v.scanPos += scanStep; if (b.scanEnd == 0) v.scanPos = juce::jlimit (-1.0f, 1.0f, v.scanPos); else if (v.scanPos > 2.0f || v.scanPos < -2.0f) v.scanPos -= 2.0f * std::floor (v.scanPos * 0.5f); }
        // the wave envelope: from where SHAPE was at the note, to each stage's place in its time
        float wOff = 0.0f;
        if (wEnv)
        {
            if (v.wStage < 4)
            {
                v.wT += invVr;
                const float len = std::max (0.001f, b.wt[v.wStage] * 0.001f), x = std::min (1.0f, v.wT / len);
                v.wNow = v.wFrom + (b.wp[v.wStage] - v.wFrom) * x;
                if (x >= 1.0f)
                {
                    v.wFrom = b.wp[v.wStage]; v.wT = 0.0f;
                    if (++v.wStage == 4 && b.wloop > 0) v.wStage = 3 - b.wloop;       // (3-4: back to the third; 1-4: to the first)
                }
            }
            else v.wNow = b.wp[3];
            wOff = b.wdepth * (v.wNow - placeA);
        }
        auto along = [&] (float base, int frames)
        {
            float p = base + wOff + placeVK + v.scanPos;
            if (b.scanEnd == 1) p -= std::floor (p);                                            // LOOP
            else if (b.scanEnd == 2) { p = std::fmod (std::abs (p), 2.0f); if (p > 1.0f) p = 2.0f - p; }     // BOUNCE
            p = clamp01 (p);
            if (b.steps && frames > 1) p = std::round (p * (float) (frames - 1)) / (float) (frames - 1);       // STEPPED: one frame, then the next
            return p;
        };
        const float pA = along (placeA, nA), pB = useB ? along (placeB, nB) : 0.0f;
        const float gA = levA ? ta->levelAt (pA) : 1.0f, gB = levB ? tb->levelAt (pB) : 1.0f;
        // pitch: the slide or glide to its note, the PITCH ENV falling away, DRIFT
        v.freq += (v.target - v.freq) * v.slideK;
        v.pitchEnvSt *= pitchK;
        const float hz = v.freq * semis (v.pitchEnvSt + driftSt);
        const double inc = (double) (hz * invVr);
        // the oscillators
        float l = 0.0f, r = 0.0f;
        for (int u = 0; u < U; ++u)
        {
            const double ia = inc * uniR[u];
            float bv = 0.0f;
            if (useB)
            {
                bv = tb->at (pB, octB0, v.pb[u]) * gB;
                v.pb[u] += ia * ratioB; wrap (v.pb[u]);
            }
            const double pa = b.fm > 0.001f ? v.pa[u] + (double) (b.fm * 1.2f * bv) : v.pa[u];
            double pr = pa; wrap (pr);
            const float av = (warpOn ? warped (*ta, pA, octA0 >= Mipmap::kOct ? octA0 : octAW, pr, b.warpAs, wk, foldIn)
                                     : ta->at (pA, octA0, pr)) * gA;
            v.pa[u] += ia;
            if (v.pa[u] >= 1.0) { v.pa[u] -= 1.0; if (v.sync) v.pb[u] = v.pa[u] * ratioB; }
            const float s = av * (1.0f - b.mix) + bv * b.mix;
            l += s * panL[u]; r += s * panR[u];
        }
        float mono = 0.0f;
        if (b.sub > 0.001f) { mono += b.sub * 0.9f * std::sin (kTwoPi * (float) v.psub); v.psub += inc * 0.5; wrap (v.psub); }
        if (b.noise > 0.001f)
        {
            const float w = rng.next();
            float nz = 0.0f;
            if (cWhite > 0.0f) nz += cWhite * w * 0.9f;
            if (cPink > 0.0f)
            {
                v.pink[0] = 0.99765f * v.pink[0] + w * 0.0990460f; v.pink[1] = 0.96300f * v.pink[1] + w * 0.2965164f; v.pink[2] = 0.57000f * v.pink[2] + w * 1.0526913f;
                nz += cPink * (v.pink[0] + v.pink[1] + v.pink[2] + w * 0.1848f) * 0.25f;
            }
            if (cMetal > 0.0f)
            {
                const double mb = (double) (205.3f * hz / 261.6256f) * invVr;
                float m = 0.0f;
                for (int k = 0; k < 6; ++k)
                {
                    const double dt = mb * kMetal[k];
                    double& p = v.pmetal[k];
                    float sq = p < 0.5 ? 1.0f : -1.0f;
                    sq += polyBlep (p, dt); double p2 = p + 0.5; if (p2 >= 1.0) p2 -= 1.0; sq -= polyBlep (p2, dt);
                    m += sq;
                    p += dt; if (p >= 1.0) p -= 1.0;
                }
                nz += cMetal * m * 0.3f;
            }
            mono += b.noise * nz;
        }
        float mid = 0.5f * (l + r) + mono, side = 0.5f * (l - r);
        // DRIVE, then the filter
        if (b.drive > 0.001f) { const float d = fastTanh (mid * driveG) * driveN; mid += driveW * (d - mid); }
        float y;
        switch (b.circuit)
        {
            case kCleanLP: y = v.svf.tickLP (mid); break;
            case kCleanBP: y = v.svf.tickBP (mid) * (0.6f + b.res * 2.0f); break;
            case kCleanHP: y = v.svf.tickHP (mid); break;
            case kLadder: y = v.ladder.tick (mid); break;
            case kMs20: y = fastTanh (v.svf2.tickHP (v.svf.tickLP (mid)) * 1.5f); break;
            default:
            {
                // (SF-000, the hundred and fourth build) MOD ATTACK: the envelope rises over it, as the MOD envelope does (at
                // nought it is at its top from the first sample, the 303's own)
                const float meg = v.modStage == 0 ? v.meg * v.mod : v.meg;
                if (! v.releasing) { const float in = accentK * meg; cap = in > cap ? cap + (in - cap) * capC : cap * capD; }
                const float cutHz = std::min (16000.0f, (87.5f + std::exp (acidL0 + (acidL1 - acidL0) * meg + acidSweep * cap)) * acidKey);
                v.dl.set (cutHz, vr);
                y = v.dl.tick (mid, acidFbK * DiodeLadder303::oscillatesAt (cutHz)) * acidComp;
                v.cutNow = cutHz;
            }
        }
        // (SF-000, the hundred and fourth build) the side through the same filter on CLEAN BP and HP (it was let out: UNISON's
        // WIDTH went unheard on them). On the others it is let through as the cutoff opens, as before
        if (b.circuit == kCleanHP) side = v.svfS.tickHP (side);
        else if (b.circuit == kCleanBP) side = v.svfS.tickBP (side) * (0.6f + b.res * 2.0f);
        else side *= v.sideK;
        float outM = y * g, outS = side * g;
        // BODY: rung by what the envelope lets through, and ringing on after it
        if (v.nModes > 0)
        {
            float bs = 0.0f;
            for (int k = 0; k < v.nModes; ++k)
            {
                auto& m = v.modes[k];
                const float yk = m.c * m.y1 - m.r * m.y2 + outM;
                m.y2 = m.y1; m.y1 = yk;
                bs += m.w * yk;
            }
            bodyPeak = std::max (bodyPeak, std::abs (bs));
            outM = outM * (1.0f - b.bodyMix) + bs * b.bodyMix * 1.5f;
        }
        L[i] += (outM + outS) * level;
        R[i] += (outM - outS) * level;
        v.t += (double) invVr;
        if (v.tRel >= 0.0) v.tRel += (double) invVr;
        ++v.age;
    }
    v.bodyEnergy = bodyPeak;
}

void SynthEngine::render (const Ctx& c, float* L, float* R, int n, float layerGain)
{
    const auto t0 = juce::Time::getHighResolutionTicks();
    std::stable_sort (pending.begin(), pending.begin() + numPending, [] (const Pending& x, const Pending& y) { return x.offset < y.offset; });
    n = std::min (n, (int) outL.size());
    std::fill (outL.begin(), outL.begin() + n, 0.0f); std::fill (outR.begin(), outR.begin() + n, 0.0f);
    int e = 0, i0 = 0;
    while (i0 < n || e < numPending)
    {
        while (e < numPending && pending[(size_t) e].offset <= i0)
        {
            const auto& p = pending[(size_t) e++];
            if (p.on)
            {
                // CHORD: its notes, each a voice of its own (not with MONO: one voice is one note)
                const bool mono = monoNow || blk.voices == 1;
                const auto& ch = kChords[mono ? 0 : blk.chord];
                for (int k = 0; k < 4 && ch[k] >= 0; ++k) { Pending q = p; q.pitch = p.pitch + (float) ch[k]; start (q); }
            }
            else
                for (auto& v : voices)
                    if (v.on && v.held && ! v.releasing && (p.key < 0 || (v.key == p.key && (p.gen == 0 || v.gen == p.gen)))) release (v);
        }
        if (i0 >= n) break;
        const int i1 = e < numPending ? std::min (n, std::max (i0 + 1, pending[(size_t) e].offset)) : n;
        const int m = i1 - i0;
        bool any4 = false, any2 = false;
        for (auto& v : voices)
        {
            if (! v.on || ! ids.ok()) continue;
            voiceControls (v, m * v.os);
            const float pg = v.part == 1 ? layerGain : 1.0f;
            if (v.os == 4)
            {
                if (! any4) { std::fill (l4.begin(), l4.begin() + 4 * m, 0.0f); std::fill (r4.begin(), r4.begin() + 4 * m, 0.0f); any4 = true; }
                renderVoice (v, l4.data(), r4.data(), 4 * m, pg);
            }
            else
            {
                if (! any2) { std::fill (l2.begin(), l2.begin() + 2 * m, 0.0f); std::fill (r2.begin(), r2.begin() + 2 * m, 0.0f); any2 = true; }
                renderVoice (v, l2.data(), r2.data(), 2 * m, pg);
            }
        }
        // the 4x down to 2x (always run, so the halfbands' state stays continuous), then 2x down to the rate
        if (! any4) { std::fill (l4.begin(), l4.begin() + 4 * m, 0.0f); std::fill (r4.begin(), r4.begin() + 4 * m, 0.0f); }
        if (! any2) { std::fill (l2.begin(), l2.begin() + 2 * m, 0.0f); std::fill (r2.begin(), r2.begin() + 2 * m, 0.0f); }
        for (int i = 0; i < 2 * m; ++i) { l2[(size_t) i] += dec4[0].down (l4[(size_t) (2 * i)], l4[(size_t) (2 * i + 1)]); r2[(size_t) i] += dec4[1].down (r4[(size_t) (2 * i)], r4[(size_t) (2 * i + 1)]); }
        dec.process (0, l2.data(), outL.data() + i0, m);
        dec.process (1, r2.data(), outR.data() + i0, m);
        // the accent's sweep drains while no accented note holds it
        for (int pt = 0; pt < 2; ++pt) accentCap[pt] *= std::exp (-(float) m / ((0.3f + 0.35f * blk.res) * (float) sr));
        i0 = i1;
    }
    numPending = 0;
    // SPACE: an ensemble (three modulated delays) on the engine's own sound, eased across the block
    const float sp0 = spaceWas, sp1 = blk.space;
    spaceWas = sp1;
    if ((sp0 > 0.001f || sp1 > 0.001f) && ens.size > 0)
        for (int i = 0; i < n; ++i)
        {
            const float amt = sp0 + (sp1 - sp0) * (float) i / (float) n;
            ens.ph += 0.6f / (float) sr; if (ens.ph > 1.0f) ens.ph -= 1.0f;
            const float mIn = 0.5f * (outL[(size_t) i] + outR[(size_t) i]);
            ens.buf[(size_t) ens.w] = mIn;
            float wl = 0.0f, wr = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                const float lfo = std::sin (kTwoPi * (ens.ph + (float) k / 3.0f)) + 0.15f * std::sin (kTwoPi * (ens.ph * 9.0f + (float) k / 3.0f));
                const float ds = (7.0f + 4.0f * lfo) * 0.001f * (float) sr;
                float rp = (float) ens.w - ds; while (rp < 0.0f) rp += (float) ens.size;
                const int ia = (int) rp; const float fr = rp - (float) ia;
                const float yv = ens.buf[(size_t) ia] + fr * (ens.buf[(size_t) ((ia + 1) % ens.size)] - ens.buf[(size_t) ia]);
                if (k == 0) wl += yv; else if (k == 1) wr += yv; else { wl += 0.5f * yv; wr += 0.5f * yv; }
            }
            ens.w = (ens.w + 1) % ens.size;
            outL[(size_t) i] = outL[(size_t) i] * (1.0f - amt * 0.5f) + wl * amt * 0.7f;
            outR[(size_t) i] = outR[(size_t) i] * (1.0f - amt * 0.5f) + wr * amt * 0.7f;
        }
    else if (ens.size > 0)
        for (int i = 0; i < n; ++i) { ens.buf[(size_t) ens.w] = 0.5f * (outL[(size_t) i] + outR[(size_t) i]); ens.w = (ens.w + 1) % ens.size; }
    for (int i = 0; i < n; ++i) { L[i] += outL[(size_t) i]; R[i] += outR[(size_t) i]; }
    int act = 0;
    for (auto& v : voices) act += v.on ? 1 : 0;
    active = act;
    const double took = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    costAvg += 0.05 * (took / std::max (1.0e-6, (double) n / sr) - costAvg);
    seen.cost.store ((float) costAvg);
    publish();
}

void SynthEngine::publish()
{
    int nv = 0;
    for (int k = 0; k < kSlots; ++k)
    {
        const auto& v = voices[(size_t) k];
        auto& o = seen.v[k];
        if (! v.on) { o.key.store (-1); o.level.store (0.0f); continue; }
        ++nv;
        o.key.store ((int) std::lround (v.pitch)); o.part.store (v.part);
        o.level.store (v.amp * (v.choked ? v.chokeGain : 1.0f)); o.cutoff.store (v.cutNow);
        o.t.store ((float) v.t); o.rel.store ((float) v.tRel); o.accent.store (v.accent);
    }
    seen.voices.store (nv);
}
} // namespace sf
