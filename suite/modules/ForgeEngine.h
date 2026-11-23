#pragma once
// (SF-000, the hundred and second build) KICK FORGE on every lane but KICK, the same Forge (Forge.h) KICK plays: one
// hit a note, struck at the note's pitch on a lane that plays notes (the kick's resting note, G1, on one that plays hits),
// with its ground ringing between. Its controls are KICK's own (fg_char and the rest, the same ids) and five that on KICK
// are the lane's: TUNE, DRIVE, PUNCH, PITCH TIME, CLICK (fg_tune and the rest here). DECAY is the lane's last-four DECAY
// or GATE knob, as SAMPLE and SYNTH take it. Appended last to every other lane's instrument (frg::with), found by id.
// As on KICK a new hit lets what is ringing go over 3 ms (but not a hit struck at the same moment: a chord's notes ring
// together), and the note's length is not heard: a hit is a hit.
#include "Forge.h"
#include "../core/Oversample.h"

namespace sf
{
namespace frg
{
constexpr int kParamCount = 29;
std::vector<ParamSpec> params();
inline std::vector<ParamSpec> with (std::vector<ParamSpec> v) { const auto e = params(); v.insert (v.end(), e.begin(), e.end()); return v; }
}

class ForgeEngine
{
public:
    void bind (const std::vector<ParamSpec>& specs, bool pitchedLane);
    void plays (bool pitchedLane) { pitched.store (pitchedLane); }      // (SF-000, the hundred and twelfth build: the lane's PLAYS)
    void prepare (double sr, int maxBlock);
    void reset();
    // Each block: begin, the notes (each at its sample in the block), render.
    // decayKnob: the lane's DECAY or GATE; keyShift: KEY FOLLOW's semitones (a lane that plays notes).
    void begin (const Ctx& c, float decayKnob, const SampleData* smp, int keyShift = 0, int keyRoot = -1);
    // (the hundred and third build) what a hit struck now at this pitch would be, for the kick's bus (the pockets and RUMBLE
    // follow it): the pitch it falls from and to, how fast, how long
    void shapeOf (float pitch, int gen, float& f0, float& f1, float& tau, float& decay) const;
    // pitch: the note (MIDI); part: 0 MODEL (it rings the ground), 1 LAYER.
    void noteOn (int offset, float pitch, float vel, float gain, int gen, int part);
    void render (float* L, float* R, int n, float layerGain = 1.0f);
    bool sounding() const;

private:
    struct Voice : forge::Hit
    {
        bool active = false;
        int part = 0, startedAt = -1;
        float fadeStep = 0.0f;
        float level() const { return active ? vel * std::exp (-(float) t / std::max (0.01f, decay)) : 0.0f; }
    };
    struct Pending { int offset; float pitch, vel, gain; int gen, part; };
    void start (const Pending& p, int at);
    float hzOf (float pitch) const;
    float tick (Voice& v, float& side);

    static constexpr int kVoices = 8;
    std::array<Voice, kVoices> voices;
    std::array<Pending, 64> pending {};
    int numPending = 0;
    forge::GroundUnit ground;
    forge::Knobs k;
    float groundIn = 0.0f, tune = 0.0f, bend = 0.0f, cal = 1.0f;
    int keyShift = 0, keyRoot = -1, sideLive = 0;
    std::atomic<bool> pitched { true };     // (SF-000, the hundred and twelfth build: the lane's PLAYS)
    bool solo = false;
    const SampleData* smp = nullptr;
    SampleRegion whole;
    Noise rng;
    Halfband dec, decS;
    double sr = 48000.0, osr = 96000.0;
    struct Ids
    {
        int ch = -1, tone = -1, punch = -1, ptime = -1, click = -1, drive = -1, tune = -1, impact = -1, body = -1, ground = -1, pos = -1, glen = -1, low = -1;
        int hang = -1, curve = -1, hold = -1, hdec = -1, phase = -1, flip = -1, ilen = -1, ipitch = -1, icut = -1, iwidth = -1, vary = -1, solo = -1;
        int gdark = -1, gduck = -1, grec = -1, gwidth = -1;
    } ids;
};
} // namespace sf
